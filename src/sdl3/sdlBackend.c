// -----------------------------------------------------------------------------
// The "SDL platform backend" - implements every machineDependent.h function
// plus the sdlBackend.h platform-only extras main.c needs. Freely includes
// SDL.h (unlike the "game world" TU - see machineDependent.h's own comment
// for why the two sides are kept apart).
//
// Video: TinyJoypad games only ever draw through md_drawColumn() (one
// SSD1306-style byte = 8 vertical pixels of one column at a time) plus
// md_drawSolidRect() for the quit-confirmation dialog - both write directly
// into a native 128x64 SDL_Surface framebuffer here. Unlike the Vircon32
// build, no texture atlas is needed: Vircon32's GPU is a blit-only texture
// engine with no CPU-writable framebuffer and a hard 1024x1024 texture cap
// (the reason that build pre-baked a 256-tile atlas and, later, needed a
// *second* atlas texture once the first filled up) - SDL has neither
// restriction, so md_drawColumn() just sets pixels.
//
// Input: CInput (copied near-verbatim from crisp-game-lib-portable-sdl's
// own cglpSDL3/CInput.c) only ever exposes the CURRENT frame's button
// state. machineDependent.h's md_input*Frames() contract needs a raw
// held-frame counter (positive N = held N consecutive frames, negative N =
// released N frames ago) - Vircon32's own gamepad_left()/etc already
// provide this straight from the emulator's hardware registers, so this
// backend maintains the equivalent counters itself, updated once per real
// frame in sdlBackend_pollEvents().
//
// Audio: TinyJoypad's original hardware is a single piezo buzzer - every
// shim's Sound()/playTone() call expects exactly one tone active at a time,
// so (matching the Vircon32 build's own single-voice choice) this is a
// single continuously-running sine oscillator, not the richer scheduled/
// multi-note model crisp-game-lib-portable-sdl's own cglpSDL3.c uses -
// collapsed down per machineDependent.h's simpler 2-arg fire-and-forget
// md_playTone(freqHz, durationSeconds) contract.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "machineDependent.h"
#include "biosFont.h"
#include "sdlBackend.h"
#include "CInput.h"
#include "tinyjoypadSDL3.h"
#include "glowEffect.h"
#include "crtEffect.h"
#include "pixelGridEffect.h"

// =============================================================================
//   Shared platform state
// =============================================================================

static SDL_Window*   gWindow   = NULL;
static SDL_Renderer* gRenderer = NULL;
static CInput*       gInput    = NULL;
static bool          gQuit     = false;

static void sdlLog( const char* fmt, ... )
{
    char buf[ 1024 ];
    va_list args;
    va_start( args, fmt );
    SDL_vsnprintf( buf, sizeof( buf ), fmt, args );
    va_end( args );
    SDL_Log( "%s", buf );
}

// =============================================================================
//   VIDEO
// =============================================================================

// One persistent canvas at Vircon32's own real screen resolution (640x360)
// - NOT a separate small 128x64 "OLED" surface scaled up at present time.
// This matters for two real reasons, both found while wiring up the BIOS
// font (see biosFont.h):
//  1. md_drawSolidRect()'s own coordinates in the Vircon32 build (see
//     portVircon32.c's drawConfirmQuitDialog(), e.g. `boxX=160, boxY=110,
//     boxW=320, boxH=140`) are already real 640x360 screen-space, not
//     128x64 OLED-space - Vircon32's own md_drawColumn() there draws
//     directly at final *scaled* screen coordinates
//     (`draw_region_at(ORIGIN_X + col*TILE_SCALE, ...)`), never through a
//     separate small framebuffer at all. Matching that single-coordinate-
//     space model here (rather than the tiny 128x64 space this file
//     originally used) is what lets md_drawSolidRect/the BIOS-font menu
//     text/game columns all share one consistent canvas.
//  2. Skipping a frame's redraw and having the PREVIOUS frame's pixels
//     simply still be on screen (obonoCoreShim's own `isInvalid`-gated
//     skip, and the quit-dialog's "game update() not called this frame"
//     behavior) requires a truly *persistent* surface - an SDL renderer's
//     own backbuffer is not guaranteed to retain its contents across
//     SDL_RenderPresent() calls (most swapchain-backed backends explicitly
//     do NOT retain it), so this has to be a real CPU-side surface that
//     WE keep alive and just re-present unchanged on a skipped frame, not
//     something recomputed fresh from the renderer's own state each frame.
//
// Games' own md_drawColumn() draws each set pixel as a real GAME_SCALE x
// GAME_SCALE filled rect at its scaled position (128x64 "OLED" space *
// GAME_SCALE, offset to center a 640x320 image inside the 640x360 canvas
// with a 20px letterbox top/bottom - matching the Vircon32 build's own
// "128x64 -> 5x -> 640x320 centered in 640x360" comment) - the menu's own
// BIOS-font text (biosFont.h's md_drawColumnPixels() calls) and
// md_drawSolidRect() both draw directly at native 1:1 scale in this same
// space, no further scaling needed since the font was already sized for
// this exact canvas.
//
// Final window-fit scaling (the canvas vs. whatever size the user's
// resizable OS window actually is) is handled entirely by SDL's own
// logical-presentation feature (SDL_SetRenderLogicalPresentation, set
// once in md_initVideo()) rather than any manual scale/offset math here -
// it re-derives the fit on every present, so no resize-event-watcher is
// needed either.
#define SCREEN_LOGICAL_W 640
#define SCREEN_LOGICAL_H 360
#define GAME_SCALE    5  // 128*5=640, 64*5=320
#define GAME_ORIGIN_X 0
#define GAME_ORIGIN_Y 20 // (360-320)/2 - letterbox bar top/bottom

static SDL_Surface* gScreen        = NULL; // SCREEN_LOGICAL_W x H, RGBA32 - persistent
static SDL_Texture*  gScreenTexture = NULL; // streamed from gScreen every frame
static Uint32         gWhitePixel    = 0;
static Uint32         gBlackPixel    = 0;
static Uint32         gDarkGrayPixel = 0;

// Post-process presentation effects (glowEffect.h/crtEffect.h/
// pixelGridEffect.h) - all three draw directly to the renderer's own
// backbuffer, AFTER gScreen's own content has already been blitted there
// via gScreenTexture, and BEFORE SDL_RenderPresent(). This can never
// accumulate frame over frame: SDL_RenderClear() wipes the backbuffer
// fresh at the top of every md_endFrame() call regardless of what any
// effect drew onto it last time, and gScreen itself (the actual
// persistent game-content buffer - see its own comment above) is only
// ever READ by these effects, never written to.
//
// The overlay/glow combination originally shipped as a single-button
// cycle ported directly from crisp-game-lib-portable-sdl's own
// cglpSDL3.c - a curated 5-state subset (nothing -> pixel-grid+glow ->
// pixel-grid alone -> CRT alone -> glow alone -> back to nothing) that
// treated pixel-grid and CRT as mutually exclusive by design (both being
// "what kind of display is this" choices). Replaced with a real 3-bit
// counter enumerating all 8 combinations of the three effects instead,
// matching the sibling gamebuino_classic_sdl project's own identical
// change there (that project ported glow/CRT in from here, but "per
// direct request" - its own comment's wording - deliberately made every
// combination reachable rather than keeping this cycle's own curated
// subset). Found to differ via a direct user question asking whether the
// two projects' own effect cycles matched - they didn't (this project's
// own 5 states are a strict subset of the sibling's 8, missing pixel-
// grid+CRT together, glow+CRT together, and all three together) - fixed
// by adopting the sibling's own fuller version here, since it has
// strictly more reachable states.
static GlowEffect*      gGlowEffect      = NULL;
static bool             gGlowEnabled     = false;
static CrtEffect*       gCrtEffect       = NULL;
static bool             gCrtEnabled      = false;
static PixelGridEffect* gPixelGridEffect = NULL;
static bool             gPixelGridEnabled = false;
static int              gEffectState     = 0;
static Uint64            gLastFrameTicks  = 0;

// All three effects are only ever relevant to actual gameplay, not the
// menu (matching cglp's own `!isInMenu` gate on every one of them) - see
// machineDependent.h's own md_setInGame() comment for why the game-world
// side is what reports this, once per frame, rather than this file
// guessing at it some other way.
static bool gInGame = false;

// Set by md_setDialogShowing() (gamesMain.c, once per real frame) - true
// for the quit-confirmation dialog's entire duration. Unlike gInGame
// itself (which stays true the whole time too, so the frozen game screen
// behind/around the dialog keeps showing whatever effect combination was
// already active - see md_endFrame()'s own comment), this is what tells
// md_endFrame() to re-composite MD_DIALOG_X/Y/W/H crisply, on top of
// those effects, so just the dialog box itself stays effect-free.
static bool gDialogShowing = false;

void md_setDialogShowing( bool showing )
{
    gDialogShowing = showing;
}

// Set by md_setFpsOverlayShowing() (gamesMain.c's own
// gamesMain_drawFpsOverlay(), once per real frame that "-fps" is drawing
// it) - same re-composite-on-top-of-the-effects idea as gDialogShowing
// above, just for a rect whose width isn't a fixed compile-time constant
// (it depends on the current FPS reading's own digit count), so it's
// tracked here instead of a pair of #defines like MD_DIALOG_W/H.
static bool gFpsOverlayShowing = false;
static int  gFpsOverlayW = 0;
static int  gFpsOverlayH = 0;

void md_setFpsOverlayShowing( bool showing, int width, int height )
{
    gFpsOverlayShowing = showing;
    gFpsOverlayW = width;
    gFpsOverlayH = height;
}

// Tentative declaration: the real definition sits further down with the
// rest of the audio state, but the status badges below need to read it.
static bool gMuted;

// -----------------------------------------------------------------------------
//   Toggle status badges + the transient "you just changed this" toast
// -----------------------------------------------------------------------------
//
// Ported from the sibling gamebuino_classic_sdl project's own "menu sound,
// effect, gray indicators + toasts" feature - two things: a permanent row
// of badges showing every global toggle's own current state (drawn in the
// menu's own top-right corner, see md_drawToggleStatusIcons() below,
// called from menu.c), and a short-lived toast drawn over whatever is
// already on screen the moment any of them flips, so a mid-game press is
// acknowledged rather than leaving the player guessing. The sibling
// project's own third toggle (a three-state real-hardware gray-dithering/
// LCD-persistence simulation, exposed as its own badge + toast) is
// Gamebuino-Classic-specific and has no equivalent on this project's own
// plain monochrome SSD1306 OLED, so only its two other toggles - sound and
// the glow/CRT/pixel-grid effect cycle - were ported.
//
// Same dim-vs-bright brightness scheme the sibling project's own badges
// use ("on" white, "off" a dim gray) - this project didn't have a real
// gray color at all before this feature, so machineDependent.h grew one
// (MD_COLOR_DARKGRAY) plus a color-aware column-pixel primitive purely for
// this, and biosFont.h grew biosDrawTextColor()/biosDrawCharColor() on top
// of that - see each one's own comment. Every actual game still only ever
// draws real MD_COLOR_BLACK/WHITE, matching the real monochrome SSD1306
// OLED every one of them was authored for.

#define MD_STATUS_TOAST_FRAMES 60

static char gStatusToastText[ 64 ];
static int  gStatusToastFrames = 0;
static bool gStatusToastExpiredFlag = false;

void md_showStatusToast( char* text )
{
    int i = 0;

    while( text[ i ] != 0 && i < (int)sizeof( gStatusToastText ) - 1 )
    {
        gStatusToastText[ i ] = text[ i ];
        i++;
    }

    gStatusToastText[ i ] = 0;
    gStatusToastFrames = MD_STATUS_TOAST_FRAMES;
}

bool md_statusToastJustExpired()
{
    bool result = gStatusToastExpiredFlag;
    gStatusToastExpiredFlag = false;
    return result;
}

// Builds the effect cycle's own message from whichever of the three are
// actually active, so what it says always matches what is on screen -
// "GRID+GLOW", "ALL OFF" and so on, mirroring the sibling project's own
// identical wording/format exactly.
static void showEffectToast()
{
    if( !gPixelGridEnabled && !gGlowEnabled && !gCrtEnabled )
    {
        md_showStatusToast( "EFFECTS: NONE" );
        return;
    }

    char text[ 64 ];
    SDL_strlcpy( text, "EFFECTS: ", sizeof( text ) );

    if( gPixelGridEnabled )
      SDL_strlcat( text, "GRID", sizeof( text ) );

    if( gGlowEnabled )
    {
        if( SDL_strlen( text ) > 9 ) SDL_strlcat( text, "+", sizeof( text ) );
        SDL_strlcat( text, "GLOW", sizeof( text ) );
    }

    if( gCrtEnabled )
    {
        if( SDL_strlen( text ) > 9 ) SDL_strlcat( text, "+", sizeof( text ) );
        SDL_strlcat( text, "CRT", sizeof( text ) );
    }

    md_showStatusToast( text );
}

// One badge: bright white when the toggle is on, dim (MD_COLOR_DARKGRAY)
// when it is off, so the row reads as lit/unlit at a glance rather than
// having to be read - matching the sibling gamebuino_classic_sdl
// project's own identical badge treatment exactly (see biosFont.h's own
// biosDrawTextColor() comment for how the dim color itself works).
static void drawStatusBadge( char* label, int x, int y, bool enabled )
{
    if( enabled )
      biosDrawText( label, x, y );
    else
      biosDrawTextColor( label, x, y, MD_COLOR_DARKGRAY );
}

void md_drawToggleStatusIcons()
{
    int y = 10;
    int gap = BIOS_FONT_CHAR_W;

    // Laid out right-to-left from the screen edge so the row stays anchored
    // to the corner whatever the labels are - same layout as the sibling
    // project's own row, just without its GRAY badge.
    int crtX  = MD_SCREEN_WIDTH - 12 - biosTextWidth( "CRT" );
    int glowX = crtX - gap - biosTextWidth( "GLOW" );
    int gridX = glowX - gap - biosTextWidth( "GRID" );
    int sndX  = gridX - gap - biosTextWidth( "SND" );

    drawStatusBadge( "SND", sndX, y, !gMuted );
    drawStatusBadge( "GRID", gridX, y, gPixelGridEnabled );
    drawStatusBadge( "GLOW", glowX, y, gGlowEnabled );
    drawStatusBadge( "CRT", crtX, y, gCrtEnabled );
}

// Drawn straight onto the persistent gScreen surface, same as the dialog
// box (MD_DIALOG_X/Y/W/H) and FPS overlay are - see machineDependent.h's
// own comment on why the toast being baked in like that (rather than a
// true renderer-only overlay) is a genuine tradeoff, not an oversight.
// gToastRectX/Y/W/H record exactly where THIS frame's box landed (its
// width depends on the current message's own length, the same reason the
// FPS overlay tracks gFpsOverlayW/H instead of a fixed #define) so
// md_endFrame() below can re-composite that same rect crisply on top of
// the glow/CRT/pixel-grid effects, the same way it already does for the
// dialog box and FPS overlay - a system toast should stay legible
// regardless of which display effect is currently active, not get
// scanlined/blurred/grid-lined along with actual gameplay. gToastDrawnThisFrame
// is what tells md_endFrame() whether that re-composite is even needed
// this frame - checking gStatusToastFrames alone wouldn't work, since it's
// already been decremented (possibly to exactly 0, on the toast's own
// last real visible frame) by the time this function returns.
static int  gToastRectX, gToastRectY, gToastRectW, gToastRectH;
static bool gToastDrawnThisFrame = false;

// Sets gStatusToastExpiredFlag on the exact tick the countdown reaches
// zero (never drawing again after) - see md_statusToastJustExpired()'s own
// header comment above for why gamesMain_dispatchFrame() needs to know
// that, specifically, rather than just "is it showing right now".
static void drawStatusToast()
{
    gToastDrawnThisFrame = false;

    if( gStatusToastFrames <= 0 )
      return;

    gStatusToastFrames--;
    if( gStatusToastFrames == 0 )
      gStatusToastExpiredFlag = true;

    int textW = biosTextWidth( gStatusToastText );
    int boxW = textW + 28;
    int boxH = BIOS_FONT_CHAR_H + 20;
    int boxX = ( MD_SCREEN_WIDTH - boxW ) / 2;
    int boxY = MD_SCREEN_HEIGHT - boxH - 14;
    int border = 4;

    md_drawSolidRect( boxX, boxY, boxW, boxH, MD_COLOR_WHITE );
    md_drawSolidRect( boxX + border, boxY + border,
                      boxW - border * 2, boxH - border * 2, MD_COLOR_BLACK );

    biosDrawText( gStatusToastText, boxX + 14, boxY + 10 );

    gToastRectX = boxX;
    gToastRectY = boxY;
    gToastRectW = boxW;
    gToastRectH = boxH;
    gToastDrawnThisFrame = true;
}

// Reuses the same gQuit flag sdlBackend_pollEvents() sets on a real
// window-close/ButQuit event (see this file's own "Shared platform state"
// section) - sdlBackend_shouldQuit() (main.c's own loop condition) can't
// tell the two causes apart and doesn't need to.
void md_requestQuit()
{
    gQuit = true;
}

#define GLOW_DOWNSCALE_FACTOR 8
#define GLOW_INTENSITY 140 // alpha, 0-255

#define CRT_SCANLINE_SPACING   6
#define CRT_SCANLINE_THICKNESS 2
#define CRT_SCANLINE_SCROLL_FPS 10.0f // pixels/second
#define CRT_SCANLINE_ALPHA 45 // matches cglpSDL3.c's own non-dark-mode value

void md_initVideo()
{
    gScreen = SDL_CreateSurface( SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, SDL_PIXELFORMAT_RGBA32 );
    gScreenTexture = SDL_CreateTexture( gRenderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    if( gScreenTexture )
      SDL_SetTextureScaleMode( gScreenTexture, SDL_SCALEMODE_NEAREST );

    SDL_SetRenderLogicalPresentation( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H,
        SDL_LOGICAL_PRESENTATION_LETTERBOX );

    if( gScreen )
    {
        gWhitePixel = SDL_MapSurfaceRGBA( gScreen, 255, 255, 255, 255 );
        gBlackPixel = SDL_MapSurfaceRGBA( gScreen, 0, 0, 0, 255 );
        gDarkGrayPixel = SDL_MapSurfaceRGBA( gScreen, 90, 90, 90, 255 );
    }

    gGlowEffect = GlowEffect_Create( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, GLOW_DOWNSCALE_FACTOR );
    gCrtEffect = CrtEffect_Create( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H,
        CRT_SCANLINE_SPACING, CRT_SCANLINE_THICKNESS, CRT_SCANLINE_SCROLL_FPS,
        128, 128, 128, CRT_SCANLINE_ALPHA );
    gPixelGridEffect = PixelGridEffect_Create( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, GAME_SCALE, 1 );
    gLastFrameTicks = SDL_GetTicks();
}

void md_setInGame( bool inGame )
{
    gInGame = inGame;
}

void md_beginFrame()
{
    if( !gScreen )
      return;

    SDL_FillSurfaceRect( gScreen, NULL, gBlackPixel );
}

void md_drawColumn( int col, int page, int value )
{
    // Same byte-truncation risk as the Vircon32 build (avrCompat.h's
    // uint8_t/etc are plain `int`s here too, so upstream shift/OR sprite-
    // compositing code that used to overflow harmlessly out of a real AVR
    // byte can leave stray high bits set) - mask once here, the single
    // choke point every game/shim's column value funnels through.
    value &= 0xFF;

    if( value == 0 || !gScreen )
      return;

    int screenX = GAME_ORIGIN_X + col * GAME_SCALE;
    int screenY0 = GAME_ORIGIN_Y + page * OLED_PAGES * GAME_SCALE;

    int i = 0;
    while( i < 8 )
    {
        if( !( ( value >> i ) & 1 ) )
        {
            i++;
            continue;
        }

        int runStart = i;
        while( i < 8 && ( ( value >> i ) & 1 ) )
          i++;
        int runLen = i - runStart;

        SDL_Rect r = { screenX, screenY0 + runStart * GAME_SCALE, GAME_SCALE, runLen * GAME_SCALE };
        SDL_FillSurfaceRect( gScreen, &r, gWhitePixel );
    }
}

void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    if( !gScreen )
      return;

    Uint32 pixel = ( color == MD_COLOR_WHITE ) ? gWhitePixel : gBlackPixel;
    SDL_Rect rect = { x, y, w, h };
    SDL_FillSurfaceRect( gScreen, &rect, pixel );
}

static void drawColumnPixelRuns( int x, int y, int bits, int count, Uint32 pixel )
{
    if( !gScreen )
      return;

    int i = 0;
    while( i < count )
    {
        if( !( ( bits >> i ) & 1 ) )
        {
            i++;
            continue;
        }

        int runStart = i;
        while( i < count && ( ( bits >> i ) & 1 ) )
          i++;
        int runLen = i - runStart;

        SDL_Rect r = { x, y + runStart, 1, runLen };
        SDL_FillSurfaceRect( gScreen, &r, pixel );
    }
}

void md_drawColumnPixels( int x, int y, int bits, int count )
{
    drawColumnPixelRuns( x, y, bits, count, gWhitePixel );
}

// Only ever reached via biosFont.h's own biosDrawTextColor()/
// biosDrawCharColor() (see machineDependent.h's own MD_COLOR_DARKGRAY
// comment) - real game code only ever calls the plain md_drawColumnPixels()
// above, always white.
void md_drawColumnPixelsColor( int x, int y, int bits, int count, int color )
{
    Uint32 pixel = gWhitePixel;
    if( color == MD_COLOR_BLACK )         pixel = gBlackPixel;
    else if( color == MD_COLOR_DARKGRAY ) pixel = gDarkGrayPixel;
    drawColumnPixelRuns( x, y, bits, count, pixel );
}

void md_endFrame()
{
    if( !gScreen )
      return;

    // Last of all onto gScreen itself (not the renderer backbuffer, see
    // drawStatusToast()'s own comment), so it lands on top of the game's
    // own finished frame, the menu and the quit dialog alike - has to run
    // before the texture upload just below picks up gScreen's own current
    // pixels for this exact frame.
    drawStatusToast();

    // gScreen is persistent (see this section's own header comment) - even
    // on a frame where nothing actually redrew it (md_beginFrame() wasn't
    // called this frame), re-uploading+re-presenting its unchanged pixels
    // is correct, just a plain repeat of the last real frame's content.
    if( gScreenTexture )
    {
        SDL_UpdateTexture( gScreenTexture, NULL, gScreen->pixels, gScreen->pitch );

        SDL_SetRenderDrawColor( gRenderer, 0, 0, 0, 255 );
        SDL_RenderClear( gRenderer );
        SDL_RenderTexture( gRenderer, gScreenTexture, NULL, NULL );

        // All three draw straight onto the backbuffer just cleared and
        // filled above - see this file's own declaration comment for why
        // that (rather than gScreen itself) is what makes none of them
        // able to accumulate frame over frame - and only during actual
        // gameplay, matching cglp's own `!isInMenu` gate.
        Uint64 nowTicks = SDL_GetTicks();
        float deltaTime = (float)( nowTicks - gLastFrameTicks ) / 1000.0f;
        gLastFrameTicks = nowTicks;

        if( gInGame )
        {
            if( gGlowEnabled )
              GlowEffect_Render( gRenderer, gScreen, gGlowEffect, 255, 255, 255, GLOW_INTENSITY );

            // Independent `if`s, not `else if` - pixel-grid and CRT are no
            // longer mutually exclusive (see gEffectState's own comment
            // above), so both can genuinely be active and drawn in the
            // same frame now, matching the sibling gamebuino_classic_sdl
            // project's own identical independent-if shape.
            if( gPixelGridEnabled )
              PixelGridEffect_Render( gRenderer, gPixelGridEffect );

            if( gCrtEnabled )
            {
                CrtEffect_Update( gCrtEffect, deltaTime );
                CrtEffect_Render( gRenderer, gCrtEffect );
            }
        }

        // Re-composite the dialog box's own rect crisply, ON TOP of
        // whatever effects just drew - gScreenTexture already holds it
        // (drawConfirmQuitDialog() draws directly onto the same gScreen
        // every other game column does, see machineDependent.h's own
        // MD_DIALOG_X/Y/W/H comment), so this is just a second, identically-
        // sized/positioned blit of that one sub-rect, restoring its
        // pre-effect pixels without touching the rest of the screen (the
        // frozen game view behind/around it), which is meant to keep
        // showing whatever effect combination was already active.
        if( gDialogShowing )
        {
            SDL_FRect dialogRect = { (float)MD_DIALOG_X, (float)MD_DIALOG_Y, (float)MD_DIALOG_W, (float)MD_DIALOG_H };
            SDL_RenderTexture( gRenderer, gScreenTexture, &dialogRect, &dialogRect );
        }

        // Same idea, for the "-fps" overlay's own top-left rect (see
        // machineDependent.h's own md_setFpsOverlayShowing() comment) -
        // never overlaps the dialog box above (that one's centered, this
        // one's pinned to (0,0)), so the two re-composites are independent
        // and order between them doesn't matter.
        if( gFpsOverlayShowing )
        {
            SDL_FRect fpsRect = { 0.0f, 0.0f, (float)gFpsOverlayW, (float)gFpsOverlayH };
            SDL_RenderTexture( gRenderer, gScreenTexture, &fpsRect, &fpsRect );
        }

        // Same idea again, for the status toast's own rect (see
        // drawStatusToast()'s own comment on gToastRectX/Y/W/H) - a system
        // toast should stay crisp/legible on top of whichever display
        // effect is currently active, the same reasoning the dialog box
        // and FPS overlay above already get, not something the sibling
        // gamebuino_classic_sdl project's own identical toast feature
        // actually does (confirmed by reading its own md_endFrame() - it
        // bakes the toast into gScreen the same way this one does, but
        // never re-composites it crisply afterward, so an active glow/CRT/
        // pixel-grid effect there really does wash over its own toast too)
        // - a genuine improvement over the reference implementation, not
        // a port of anything.
        if( gToastDrawnThisFrame )
        {
            SDL_FRect toastRect = { (float)gToastRectX, (float)gToastRectY, (float)gToastRectW, (float)gToastRectH };
            SDL_RenderTexture( gRenderer, gScreenTexture, &toastRect, &toastRect );
        }
    }

    SDL_RenderPresent( gRenderer );
}

// Real gameplay screenshots (generated via -ms, then cropped to the
// 640x320 game area and resized to MD_THUMBNAIL_WIDTH x HEIGHT with
// ImageMagick - see assets/thumbnails/'s own directory, no runtime
// scaling code needed since every file is already the exact target size)
// - "thumb_00.bmp".."thumb_32.bmp", indexed by registration order (the
// same order addGame() is called in menuGameList.c, which is also what
// gameIndex here always means - menu.c's own displayOrder[] indirection
// already resolves the alphabetized display position back to this before
// calling md_drawGameThumbnail()). Unlike the Vircon32 build's own
// 256-tile-atlas-with-a-1024x1024-cap workaround, SDL has no texture-size
// constraint to design around - these are just plain loaded surfaces.
//
// The .bmp bytes themselves are compiled directly into the executable via
// thumbnailData.h (gThumbnailBlobs[]) - generated by tools/gen_thumbnails.py
// from assets/thumbnails/*.bmp, not read from disk at runtime, so there's
// no SDL_GetBasePath()-relative assets/ directory to ship or find
// alongside the exe for this anymore.
#include "thumbnailData.h"

// Matches gameworld/menu.c's own MAX_GAMES (bumped 64->128 alongside it,
// same reason - see that file's own comment).
#define THUMBNAIL_MAX_COUNT 128
static SDL_Surface* gThumbnails[ THUMBNAIL_MAX_COUNT ];
static int  gThumbnailCount = -1; // -1 = not yet probed

// Decodes gThumbnailBlobs[] in order, stopping at the first blob that
// fails to decode as a BMP (matches the old disk-probe's own "stop at the
// first missing file" behavior, so a future 34th game with no thumbnail
// yet is still a silent no-op) - done once, lazily, on the first call from
// either public function below.
static void thumbnailsProbeIfNeeded()
{
    if( gThumbnailCount != -1 )
      return;

    gThumbnailCount = 0;
    for( int i = 0; i < gThumbnailBlobCount && i < THUMBNAIL_MAX_COUNT; i++ )
    {
        SDL_IOStream* io = SDL_IOFromConstMem( gThumbnailBlobs[ i ].data, gThumbnailBlobs[ i ].len );
        SDL_Surface* surf = SDL_LoadBMP_IO( io, true );
        if( !surf )
          break;

        gThumbnails[ i ] = surf;
        gThumbnailCount = i + 1;
    }
}

int md_getThumbnailCount()
{
    thumbnailsProbeIfNeeded();
    return gThumbnailCount;
}

void md_drawGameThumbnail( int gameIndex, int x, int y )
{
    thumbnailsProbeIfNeeded();

    if( gameIndex < 0 || gameIndex >= gThumbnailCount || !gScreen )
      return;

    SDL_Rect dst = { x, y, MD_THUMBNAIL_WIDTH, MD_THUMBNAIL_HEIGHT };
    SDL_BlitSurface( gThumbnails[ gameIndex ], NULL, gScreen, &dst );
}

// =============================================================================
//   INPUT
// =============================================================================

// Raw held-frame counters, updated once per real frame in
// sdlBackend_pollEvents() - see this file's own header comment.
static int gLeftFrames  = 0;
static int gRightFrames = 0;
static int gUpFrames    = 0;
static int gDownFrames  = 0;
static int gFireRawFrames = 0; // pre-gate, see md_inputFireFrames() below

static void updateHeldCounter( int* counter, bool held )
{
    if( held )
      *counter = ( *counter > 0 ) ? ( *counter + 1 ) : 1;
    else
      *counter = ( *counter < 0 ) ? ( *counter - 1 ) : -1;
}

int md_inputLeftFrames()  { return gLeftFrames;  }
int md_inputRightFrames() { return gRightFrames; }
int md_inputUpFrames()    { return gUpFrames;    }
int md_inputDownFrames()  { return gDownFrames;  }

bool md_inputLeft()  { return md_inputLeftFrames()  > 0; }
bool md_inputRight() { return md_inputRightFrames() > 0; }
bool md_inputUp()    { return md_inputUpFrames()    > 0; }
bool md_inputDown()  { return md_inputDownFrames()  > 0; }

// Suppresses md_inputFire() until the physical button is actually released
// - ported verbatim (same semantics, same reasoning) from the Vircon32
// build's own portVircon32.c. Armed once when a game is (re)launched from
// the menu, so the same press that confirmed the menu selection can't also
// be misread as the game's own first-frame input.
static bool gInputFireGateActive = false;

int md_inputFireFrames()
{
    if( gInputFireGateActive )
    {
        if( gFireRawFrames <= 0 )
          gInputFireGateActive = false;

        return -3600;
    }

    return gFireRawFrames;
}

bool md_inputFire() { return md_inputFireFrames() > 0; }

bool md_inputStart()
{
    // BUTTON_MENU (Escape) drives ButBack, and the unused-here dark-color-
    // switch keybind (D / gamepad Start) drives ButStart - either is
    // accepted as TinyJoypad's own single "Start" button (quit-confirm
    // dialog trigger), so a keyboard-only player isn't forced to use D.
    return gInput && ( gInput->Buttons.ButStart || gInput->Buttons.ButBack );
}

bool md_inputFire2()
{
    return gInput && gInput->Buttons.ButB;
}

void md_armInputFireGate()
{
    gInputFireGateActive = true;
}

// =============================================================================
//   AUDIO
// =============================================================================

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_AMPLITUDE   10000

static SDL_AudioStream* gAudioStream   = NULL;
static SDL_AudioDeviceID gAudioDevice  = 0;
static int   gFrameCounter = 0;

// A bank of independent voices, not a single shared tone - matches the
// sibling Vircon32 build's own md_playTone() fix (see its own extensive
// header comment in portVircon32.c): TinyJoypad's original hardware is a
// single piezo buzzer, so every game's own Sound()/playTone() call site
// was written assuming "this replaces whatever's currently sounding," but
// that's a property of the ORIGINAL hardware, not something this function
// needs to enforce - a single shared gToneActive/gToneFreq/gTonePhase
// meant two genuinely concurrent cues (e.g. Tiny Pacman's continuously-
// retriggered power-pellet siren and its dot-eaten/ghost-eaten SFX)
// could never be heard at once, cutting each other off instead. Fixed by
// finding a free voice slot per call and mixing every active voice's own
// square wave together in the callback, instead of forcing everything
// through one slot - same fix, adapted to this backend's own plain
// software-oscillator model (no hardware channel picker to lean on the
// way Vircon32's PlayNote library provides).
#define AUDIO_MAX_VOICES 16
static bool  gToneActive[ AUDIO_MAX_VOICES ];
static float gToneFreq[ AUDIO_MAX_VOICES ];
static float gTonePhase[ AUDIO_MAX_VOICES ];
static int   gToneStopFrame[ AUDIO_MAX_VOICES ];

// Master volume/mute - live-adjustable via BUTTON_VOLUP/BUTTON_VOLDOWN
// (PageUp/PageDown, ButRB/ButLB) and BUTTON_SOUNDSWITCH (S, ButY), edge-
// checked in sdlBackend_pollEvents() below. Matches cglpSDL3.c's own
// audioVolume step size (0.05f/press) for the volume control; mute is this
// project's own addition (cglp's ButY handler does something unrelated -
// a "buggy sine" waveform toggle - not a real mute).
static float gVolume = 1.0f;
static bool  gMuted   = false;

static void SDLCALL sdlAudioCallback( void* userdata, SDL_AudioStream* stream,
    int additionalAmount, int totalAmount )
{
    (void)userdata;
    (void)totalAmount;

    if( additionalAmount <= 0 )
      return;

    Uint8* raw = (Uint8*)SDL_stack_alloc( Uint8, additionalAmount );
    if( !raw )
      return;

    Sint16* buffer = (Sint16*)raw;
    int sampleCount = additionalAmount / (int)sizeof( Sint16 );

    for( int i = 0; i < sampleCount; i++ )
    {
        // Square, not sine, per voice - matches the real hardware this
        // shim's own Sound() comment describes ("a square wave toggled
        // every (255-freq) microseconds", tinyJoypadShim.c), and the
        // sibling Vircon32 build's own PlayNote library, which - though it
        // technically supports any single-cycle waveform - ships and
        // uses a SAW wavetable (sounds/wt_saw.wav) specifically because
        // a pure sine has none of a buzzer's harmonic buzz. Both
        // reference implementations are harmonically rich; a sine is
        // not just a different timbre, it's frequently silent where
        // they aren't: several games (e.g. gameTinyPacman.c's siren/
        // pellet cues) call md_playTone() with very low nominal
        // frequencies (2-20Hz) for very short durations - a sine barely
        // completes a fraction of one cycle in that time (near-zero
        // amplitude throughout, genuinely inaudible), whereas a square
        // wave's hard 0->+-amplitude edge at tone-start/stop is itself
        // an audible click regardless of the nominal frequency, same as
        // a real piezo buzzer being switched on for a moment.
        int mixed = 0;
        if( !gMuted )
        {
            for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
            {
                if( !gToneActive[ v ] || gToneFreq[ v ] <= 0.0f )
                  continue;

                mixed += (int)( ( gTonePhase[ v ] < (float)M_PI ? 1.0f : -1.0f )
                    * AUDIO_AMPLITUDE * gVolume );

                gTonePhase[ v ] += 2.0f * (float)M_PI * gToneFreq[ v ] / (float)AUDIO_SAMPLE_RATE;
                if( gTonePhase[ v ] >= 2.0f * (float)M_PI )
                  gTonePhase[ v ] -= 2.0f * (float)M_PI;
            }
        }

        // AUDIO_AMPLITUDE(10000) leaves enough headroom under Sint16's
        // +-32767 range for several simultaneous voices to sum cleanly -
        // this clamp only actually engages if more than 3 or so genuinely
        // overlap at their peak at once, which no game in this cartridge
        // does (usually at most 2 concurrent cues) - a hard clamp rather
        // than dynamic rescaling, same tradeoff this project already
        // accepts for a buzzer-style square wave's own harsh edges.
        if( mixed > 32767 ) mixed = 32767;
        if( mixed < -32768 ) mixed = -32768;
        buffer[ i ] = (Sint16)mixed;
    }

    SDL_PutAudioStreamData( stream, raw, additionalAmount );
    SDL_stack_free( raw );
}

void md_initAudio()
{
    if( !SDL_InitSubSystem( SDL_INIT_AUDIO ) )
    {
        sdlLog( "Failed to init SDL audio subsystem: %s\n", SDL_GetError() );
        return;
    }

    SDL_AudioSpec spec = { 0 };
    spec.freq     = AUDIO_SAMPLE_RATE;
    spec.format   = SDL_AUDIO_S16;
    spec.channels = 1;

    gAudioStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec, sdlAudioCallback, NULL );

    if( !gAudioStream )
    {
        sdlLog( "Failed to open audio stream: %s\n", SDL_GetError() );
        return;
    }

    gAudioDevice = SDL_GetAudioStreamDevice( gAudioStream );
    if( gAudioDevice )
      SDL_ResumeAudioDevice( gAudioDevice );
}

void md_playTone( float freqHz, float durationSeconds )
{
    // A rest (freq<=0) is a genuine no-op here, same as the Vircon32
    // sibling's own fix - it doesn't stop anything, it just doesn't add a
    // new voice, so whatever else is independently playing on other
    // voices continues unaffected.
    if( freqHz <= 0.0f )
      return;

    int slot = -1;
    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        if( !gToneActive[ v ] )
        {
            slot = v;
            break;
        }
    }
    if( slot < 0 )
      slot = 0; // all voices busy - steal the first one rather than silently dropping the note

    gTonePhase[ slot ] = 0.0f;
    gToneFreq[ slot ] = freqHz;
    gToneActive[ slot ] = true;

    int durationFrames = (int)( durationSeconds * 60.0f );
    if( durationFrames < 1 )
      durationFrames = 1;

    // +1, not just gFrameCounter+durationFrames: main.c's own loop calls
    // gamesMain_dispatchFrame() (where every game's own Sound()/
    // md_playTone() call actually happens) BEFORE md_updateAudio() in the
    // very same real frame. Without this margin, a game's own minimum-
    // length (1-frame) tone - the single most common case, since most
    // games' own short SFX durations round down to it - would get its
    // gToneStopFrame set to gFrameCounter+1, then md_updateAudio() (called
    // moments later, same iteration) increments gFrameCounter to exactly
    // that value and immediately expires it - cancelling the tone before
    // the audio callback thread has had virtually any real chance to
    // render it, not after a genuine frame of playback. Confirmed as the
    // root cause of several games (Frogger - every one of its own note
    // durations round-trips through this same sub-frame math and hits
    // this exact case; HollowSeeker's move blip, 0.02s = 1.2 frames)
    // playing effectively silent, and others (Pacman, whose calls span a
    // wider range of durations) sounding thin/wrong rather than silent -
    // only the ones landing on other games' own longer calls survived.
    // The extra frame guarantees at least one full md_updateAudio() tick
    // elapses after gamesMain_dispatchFrame() returns, regardless of
    // which one of the two calls this frame's tone happened to start from.
    gToneStopFrame[ slot ] = gFrameCounter + durationFrames + 1;
}

void md_stopTone()
{
    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        gToneActive[ v ] = false;
        gToneStopFrame[ v ] = -1;
    }
}

void md_updateAudio()
{
    gFrameCounter++;

    for( int v = 0; v < AUDIO_MAX_VOICES; v++ )
    {
        if( gToneActive[ v ] && gToneStopFrame[ v ] >= 0 && gFrameCounter >= gToneStopFrame[ v ] )
        {
            gToneActive[ v ] = false;
            gToneStopFrame[ v ] = -1;
        }
    }
}

int md_getFrameCounter()
{
    return gFrameCounter;
}

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// Backed by a real file - a dotfile in the user's home directory, matching
// crisp-game-lib-portable-sdl's own cglpSDL3.c precedent (loadHighScores()/
// saveHighScores(), "%s/.cglpscore.dat" against SDL_getenv("HOME")) for the
// general approach, with one deliberate addition: that reference only ever
// checks HOME, with no Windows-specific fallback - fine for the shell
// environments (Git Bash/MSYS/WSL, all of which do set HOME) that project
// is normally built/run from, but not a safe assumption for every real
// Windows launch context this project needs to support (a stock cmd.exe
// session or an Explorer-double-clicked .exe usually has no HOME set at
// all). USERPROFILE is what Windows itself always sets for every real user
// session instead - checked here as a genuine second-choice fallback
// before finally falling back to "." (current directory) as a last
// resort, matching the reference's own final fallback.
#define CARD_FILE_NAME ".tinyjoypad_highscores"
#define CARD_SIGNATURE_TEXT "TINYJOYPADSDL01"
#define CARD_SIGNATURE_BYTES 32 // just needs to not overlap eepromShim.c's own first slot offset, not to numerically match it

static char gCardFilePath[ 1024 ];
static bool gCardPathResolved = false;

static void sdlBackendResolveCardFilePath()
{
    if( gCardPathResolved )
      return;
    gCardPathResolved = true;

    const char* homeDir = SDL_getenv( "HOME" );
#if defined( _WIN32 )
    if( homeDir == NULL || homeDir[ 0 ] == 0 )
      homeDir = SDL_getenv( "USERPROFILE" );
#endif
    if( homeDir == NULL || homeDir[ 0 ] == 0 )
      homeDir = ".";

    SDL_snprintf( gCardFilePath, sizeof( gCardFilePath ), "%s/%s", homeDir, CARD_FILE_NAME );
}

// Extends a shorter-than-needed file up to minSize with real zero bytes,
// rather than relying on fseek()-past-EOF-then-write's own implementation-
// defined gap-filling behavior - a few explicit fwrite() calls is cheap
// (this file tops out around 36KB - EEPROM_MAX_SLOTS(64) * ~570 bytes/slot)
// and guarantees identical, correct behavior on every filesystem this
// project targets rather than trusting an unspecified corner of the C
// standard.
static void sdlBackendEnsureFileSize( FILE* fp, long minSize )
{
    fseek( fp, 0, SEEK_END );
    long currentSize = ftell( fp );
    if( currentSize >= minSize )
      return;

    static const char zeroBuf[ 4096 ] = { 0 };
    long remaining = minSize - currentSize;
    while( remaining > 0 )
    {
        long chunk = remaining < (long)sizeof( zeroBuf ) ? remaining : (long)sizeof( zeroBuf );
        fwrite( zeroBuf, 1, (size_t)chunk, fp );
        remaining -= chunk;
    }
}

bool md_cardIsConnected()
{
    sdlBackendResolveCardFilePath();

    // "ab" (append) creates the file if missing without touching any
    // existing content otherwise - a real permissions/disk-full/etc
    // failure here correctly leaves eepromCardAvailable false upstream
    // (eepromShim.c), rather than silently pretending persistence works.
    FILE* fp = fopen( gCardFilePath, "ab" );
    if( !fp )
      return false;

    fclose( fp );
    return true;
}

bool md_cardHasOurSignature()
{
    char buf[ CARD_SIGNATURE_BYTES ];
    memset( buf, 0, sizeof( buf ) );

    FILE* fp = fopen( gCardFilePath, "rb" );
    if( !fp )
      return false;

    fread( buf, 1, sizeof( buf ), fp );
    fclose( fp );

    return strncmp( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) ) == 0;
}

void md_cardWriteSignature()
{
    char buf[ CARD_SIGNATURE_BYTES ];
    memset( buf, 0, sizeof( buf ) );
    strncpy( buf, CARD_SIGNATURE_TEXT, sizeof( buf ) - 1 );

    FILE* fp = fopen( gCardFilePath, "r+b" );
    if( !fp )
      return;

    sdlBackendEnsureFileSize( fp, (long)sizeof( buf ) );
    fseek( fp, 0, SEEK_SET );
    fwrite( buf, 1, sizeof( buf ), fp );
    fclose( fp );
}

void md_cardReadData( void* dest, int offsetBytes, int sizeBytes )
{
    // Zeroed first so a slot past the current real file length (never
    // written yet) reads back as real zeroed storage - eepromShim.c's own
    // "is this slot empty" check (magic != EEPROM_MAGIC) already handles
    // that correctly with no special-casing needed here.
    memset( dest, 0, (size_t)sizeBytes );

    FILE* fp = fopen( gCardFilePath, "rb" );
    if( !fp )
      return;

    fseek( fp, offsetBytes, SEEK_SET );
    fread( dest, 1, (size_t)sizeBytes, fp );
    fclose( fp );
}

void md_cardWriteData( void* src, int offsetBytes, int sizeBytes )
{
    FILE* fp = fopen( gCardFilePath, "r+b" );
    if( !fp )
      return;

    sdlBackendEnsureFileSize( fp, (long)( offsetBytes + sizeBytes ) );
    fseek( fp, offsetBytes, SEEK_SET );
    fwrite( src, 1, (size_t)sizeBytes, fp );
    fclose( fp );
}

// =============================================================================
//   sdlBackend.h - platform-only extras for main.c
// =============================================================================

// Set by sdlBackend_setWindowSize()/setFullscreen()/setVsync() - read once,
// at sdlBackend_init() time. See sdlBackend.h for why these are separate
// setters rather than sdlBackend_init() parsing argc/argv itself: CLI
// parsing/ownership stays entirely in main.c.
static int  gConfigWindowW   = DEFAULT_WINDOW_WIDTH;
static int  gConfigWindowH   = DEFAULT_WINDOW_HEIGHT;
static bool gConfigFullscreen = false;
static bool gConfigVsync      = true;
static bool gConfigSoftwareRendering = false;

void sdlBackend_setWindowSize( int width, int height )
{
    if( width > 0 )
      gConfigWindowW = width;
    if( height > 0 )
      gConfigWindowH = height;
}

void sdlBackend_setFullscreen( bool fullscreen )
{
    gConfigFullscreen = fullscreen;
}

void sdlBackend_setVsync( bool enabled )
{
    gConfigVsync = enabled;
}

void sdlBackend_setSoftwareRendering( bool enabled )
{
    gConfigSoftwareRendering = enabled;
}

bool sdlBackend_init( int argc, char** argv )
{
    (void)argc;
    (void)argv;

    if( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        sdlLog( "Failed to init SDL video: %s\n", SDL_GetError() );
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    if( gConfigFullscreen )
      windowFlags |= SDL_WINDOW_FULLSCREEN;

    gWindow = SDL_CreateWindow( "TinyJoypad SDL3", gConfigWindowW,
        gConfigWindowH, windowFlags );

    if( !gWindow )
    {
        sdlLog( "Failed to create window: %s\n", SDL_GetError() );
        return false;
    }

    // NULL = let SDL auto-pick its own best-available driver (typically
    // hardware-accelerated - D3D11/D3D12/OpenGL/Vulkan/Metal depending on
    // platform); SDL_SOFTWARE_RENDERER ("software", a real SDL3-defined
    // driver name, not a made-up string) forces its built-in CPU rasterizer
    // instead - "-s" on the command line, see main.c.
    gRenderer = SDL_CreateRenderer( gWindow,
        gConfigSoftwareRendering ? SDL_SOFTWARE_RENDERER : NULL );
    if( !gRenderer )
    {
        sdlLog( "Failed to create renderer: %s\n", SDL_GetError() );
        return false;
    }

    SDL_SetRenderVSync( gRenderer, gConfigVsync ? 1 : 0 );

    gInput = CInput_Create();

    sdlLog( "sdlBackend initialized: renderer=%s vsync=%s\n",
        SDL_GetRendererName( gRenderer ), gConfigVsync ? "on" : "off" );

    return true;
}

bool sdlBackend_saveScreenshot( const char* path )
{
    if( !gScreen )
      return false;

    // Deliberately gScreen (the clean, pre-effects game content), not
    // whatever the glow/CRT presentation toggles currently look like -
    // -ms's screenshots double as this project's own thumbnail source
    // material (see assets/thumbnails/'s own generation process), which
    // should stay crisp regardless of whatever a player happens to have
    // glow/CRT set to.
    return SDL_SaveBMP( gScreen, path );
}

void sdlBackend_simulateFireFrame( bool pressed )
{
    updateHeldCounter( &gFireRawFrames, pressed );
}

void sdlBackend_simulateUpFrame( bool held ) { updateHeldCounter( &gUpFrames, held ); }

void sdlBackend_shutdown()
{
    if( gAudioDevice )
    {
        SDL_CloseAudioDevice( gAudioDevice );
        gAudioDevice = 0;
    }

    if( gInput )
    {
        CInput_Destroy( gInput );
        gInput = NULL;
    }

    if( gGlowEffect )
    {
        GlowEffect_Destroy( gGlowEffect );
        gGlowEffect = NULL;
    }

    if( gCrtEffect )
    {
        CrtEffect_Destroy( gCrtEffect );
        gCrtEffect = NULL;
    }

    if( gPixelGridEffect )
    {
        PixelGridEffect_Destroy( gPixelGridEffect );
        gPixelGridEffect = NULL;
    }

    if( gScreenTexture )
    {
        SDL_DestroyTexture( gScreenTexture );
        gScreenTexture = NULL;
    }

    if( gScreen )
    {
        SDL_DestroySurface( gScreen );
        gScreen = NULL;
    }

    if( gRenderer )
    {
        SDL_DestroyRenderer( gRenderer );
        gRenderer = NULL;
    }

    if( gWindow )
    {
        SDL_DestroyWindow( gWindow );
        gWindow = NULL;
    }

    SDL_Quit();
}

void sdlBackend_pollEvents()
{
    if( !gInput )
      return;

    CInput_Update( gInput );

    if( gInput->Buttons.ButQuit )
      gQuit = true;

    bool left  = gInput->Buttons.ButLeft  || gInput->Buttons.ButDpadLeft;
    bool right = gInput->Buttons.ButRight || gInput->Buttons.ButDpadRight;
    bool up    = gInput->Buttons.ButUp    || gInput->Buttons.ButDpadUp;
    bool down  = gInput->Buttons.ButDown  || gInput->Buttons.ButDpadDown;
    bool fire  = gInput->Buttons.ButA;

    updateHeldCounter( &gLeftFrames,  left );
    updateHeldCounter( &gRightFrames, right );
    updateHeldCounter( &gUpFrames,    up );
    updateHeldCounter( &gDownFrames,  down );
    updateHeldCounter( &gFireRawFrames, fire );

    // BUTTON_GLOWSWITCH (tinyjoypadSDL3.h) maps to ButX (CInput.c) - a
    // plain edge check against Input->PrevButtons, matching CInput's own
    // existing "current vs previous frame" tracking rather than adding a
    // second held-frame counter just for this one toggle. Used to be gated
    // to gInGame - toggling while on the menu, where none of these effects
    // ever render anyway (see md_endFrame()'s own gInGame check), used to
    // be an invisible no-op. Now that the menu itself shows a live status
    // badge row (md_drawToggleStatusIcons(), see menu.c) and a toast on
    // every change (md_showStatusToast() below), toggling from the menu is
    // no longer invisible - matches the sibling gamebuino_classic_sdl
    // project's own identical un-gating once it grew the same badges/toast
    // feature - so the gInGame gate was dropped. The effects still only
    // ever *render* during actual gameplay (unchanged, see md_endFrame()'s
    // own gInGame check), only the toggle input itself is no longer
    // restricted.
    //
    // A real 3-bit counter enumerating all 8 combinations of the three
    // effects - see gEffectState's own declaration comment above for why
    // (matches the sibling gamebuino_classic_sdl project's own identical
    // handler exactly, one press always advances to the next of the 8,
    // wrapping back to "all off" after the 8th).
    if( gInput->Buttons.ButX && !gInput->PrevButtons.ButX )
    {
        gEffectState = ( gEffectState + 1 ) % 8;
        gPixelGridEnabled = ( gEffectState & 1 ) != 0;
        gGlowEnabled      = ( gEffectState & 2 ) != 0;
        gCrtEnabled       = ( gEffectState & 4 ) != 0;
        showEffectToast();
    }

    // BUTTON_VOLDOWN/BUTTON_VOLUP (PageDown/PageUp) map onto ButLB/ButRB
    // (CInput.c) - also the gamepad's shoulder buttons, but no game ever
    // reads ButLB/ButRB (machineDependent.h only exposes md_inputLeft/
    // Right/Up/Down/Fire/Fire2/Start to game code), so repurposing them
    // here doesn't steal input from anything. Edge-checked like the glow
    // cycle above, but deliberately NOT gated to gInGame - volume is a
    // player-level setting, not a gameplay effect. Not held-repeat, one
    // 0.05 step per press, same step size as cglpSDL3.c's own audioVolume
    // control.
    if( gInput->Buttons.ButLB && !gInput->PrevButtons.ButLB )
    {
        gVolume -= 0.05f;
        if( gVolume < 0.0f )
          gVolume = 0.0f;
        sdlLog( "Volume: %d%%\n", (int)( gVolume * 100.0f + 0.5f ) );
    }

    if( gInput->Buttons.ButRB && !gInput->PrevButtons.ButRB )
    {
        gVolume += 0.05f;
        if( gVolume > 1.0f )
          gVolume = 1.0f;
        sdlLog( "Volume: %d%%\n", (int)( gVolume * 100.0f + 0.5f ) );
    }

    // BUTTON_SOUNDSWITCH (S) maps onto ButY (CInput.c, also the gamepad
    // North button) - toggles gMuted, read by sdlAudioCallback() above.
    // Leaves gToneActive/gToneStopFrame bookkeeping untouched, so a game's
    // own Sound()/md_playTone() call timing stays correct either way - it
    // just plays back silent while muted.
    if( gInput->Buttons.ButY && !gInput->PrevButtons.ButY )
    {
        gMuted = !gMuted;
        sdlLog( "Sound: %s\n", gMuted ? "muted" : "unmuted" );
        md_showStatusToast( gMuted ? "SOUND OFF" : "SOUND ON" );
    }

    // BUTTON_FULLSCREEN (F3) drives ButFullscreen (CInput.c) - a live
    // toggle via SDL_SetWindowFullscreen(), independent of the -f startup
    // flag (sdlBackend_setFullscreen()/gConfigFullscreen, read once at
    // sdlBackend_init() time). Reuses gConfigFullscreen itself as the
    // current live state rather than a separate variable, since nothing
    // else reads it after init.
    if( gInput->Buttons.ButFullscreen && !gInput->PrevButtons.ButFullscreen )
    {
        gConfigFullscreen = !gConfigFullscreen;
        SDL_SetWindowFullscreen( gWindow, gConfigFullscreen );
    }
}

bool sdlBackend_shouldQuit()
{
    return gQuit;
}
