// -----------------------------------------------------------------------------
// The "SDL platform backend" - implements every machineDependent.h function
// plus the sdlBackend.h platform-only extras main.c needs. Freely includes
// SDL.h (unlike the "game world" TU - see machineDependent.h's own comment
// for why the two sides are kept apart).
//
// This is the SDL2 port, ported from src/sdl3/sdlBackend.c (see that file's
// own comments for the full design rationale behind the video/input/audio
// choices below - only genuinely SDL2-vs-SDL3 API differences are called
// out here; everything else carries over unchanged).
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
// own cglpSDL2/CInput.c) only ever exposes the CURRENT frame's button
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
// single continuously-running square-wave oscillator, not the richer
// scheduled/multi-note model crisp-game-lib-portable-sdl's own cglpSDL2.c
// uses - collapsed down per machineDependent.h's simpler 2-arg fire-and-
// forget md_playTone(freqHz, durationSeconds) contract. SDL2 has no
// SDL_AudioStream "push a callback, get a stream object" convenience API
// (an SDL3 addition) - this uses SDL2's own classic SDL_OpenAudioDevice()
// model instead, where the callback fills the output buffer directly
// in-place rather than calling a separate "put data" function.
// -----------------------------------------------------------------------------

#include <SDL.h>
#include <stdbool.h>
#include <math.h>

#include "machineDependent.h"
#include "sdlBackend.h"
#include "CInput.h"
#include "tinyjoypadSDL2.h"
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
// logical-size feature (SDL_RenderSetLogicalSize, set once in
// md_initVideo()) rather than any manual scale/offset math here - it
// re-derives the fit on every present, so no resize-event-watcher is
// needed either. SDL2's own SDL_RenderSetLogicalSize predates SDL3's
// newer, more configurable SDL_SetRenderLogicalPresentation (which
// supports several presentation modes - letterbox, stretch, overscan,
// integer-scale) but produces the same letterboxed, aspect-preserving fit
// by default, which is all this project ever asked SDL3's own version for
// anyway.
#define SCREEN_LOGICAL_W 640
#define SCREEN_LOGICAL_H 360
#define GAME_SCALE    5  // 128*5=640, 64*5=320
#define GAME_ORIGIN_X 0
#define GAME_ORIGIN_Y 20 // (360-320)/2 - letterbox bar top/bottom

static SDL_Surface* gScreen        = NULL; // SCREEN_LOGICAL_W x H, RGBA32 - persistent
static SDL_Texture*  gScreenTexture = NULL; // streamed from gScreen every frame
static Uint32         gWhitePixel    = 0;
static Uint32         gBlackPixel    = 0;

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
// The overlay/glow combination and its single-button cycle are ported
// directly from crisp-game-lib-portable-sdl's own cglpSDL2.c (the ButX/
// BUTTON_GLOWSWITCH handler) - one press steps through: nothing ->
// pixel-grid+glow -> pixel-grid alone -> CRT alone -> glow alone -> (back
// to nothing). Pixel-grid and CRT are mutually exclusive (cglp's own
// design - both are "what kind of display is this" choices, glow is a
// separate "how bright are the pixels" one that can combine with
// pixel-grid but, in cglp's own cycle, never with CRT).
// gOverlayMode: 0 = none, 1 = pixel-grid, 2 = CRT.
static GlowEffect*      gGlowEffect      = NULL;
static bool             gGlowEnabled     = false;
static CrtEffect*       gCrtEffect       = NULL;
static PixelGridEffect* gPixelGridEffect = NULL;
static int              gOverlayMode     = 0;
// Uint32, not SDL3-flavor Uint64: SDL2's own SDL_GetTicks() returns a
// 32-bit millisecond count (wraps after ~49 days of continuous uptime;
// SDL3's own SDL_GetTicks() returns 64-bit specifically to remove that
// wraparound) - only used here for a per-frame delta, so the eventual
// wraparound is a one-frame glitch in the CRT scroll speed at worst, not a
// correctness issue worth working around.
static Uint32            gLastFrameTicks  = 0;

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
#define CRT_SCANLINE_ALPHA 45 // matches cglpSDL2.c's own non-dark-mode value

void md_initVideo()
{
    // SDL_CreateRGBSurfaceWithFormat, not SDL3's SDL_CreateSurface - see
    // glowEffect.c's own note (this project's SDL2 port) on the same call.
    gScreen = SDL_CreateRGBSurfaceWithFormat( 0, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H, 32, SDL_PIXELFORMAT_RGBA32 );
    gScreenTexture = SDL_CreateTexture( gRenderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    if( gScreenTexture )
      SDL_SetTextureScaleMode( gScreenTexture, SDL_ScaleModeNearest );

    SDL_RenderSetLogicalSize( gRenderer, SCREEN_LOGICAL_W, SCREEN_LOGICAL_H );

    if( gScreen )
    {
        gWhitePixel = SDL_MapRGBA( gScreen->format, 255, 255, 255, 255 );
        gBlackPixel = SDL_MapRGBA( gScreen->format, 0, 0, 0, 255 );
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

    SDL_FillRect( gScreen, NULL, gBlackPixel );
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
        SDL_FillRect( gScreen, &r, gWhitePixel );
    }
}

void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    if( !gScreen )
      return;

    Uint32 pixel = ( color == MD_COLOR_WHITE ) ? gWhitePixel : gBlackPixel;
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect( gScreen, &rect, pixel );
}

void md_drawColumnPixels( int x, int y, int bits, int count )
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
        SDL_FillRect( gScreen, &r, gWhitePixel );
    }
}

void md_endFrame()
{
    if( !gScreen )
      return;

    // gScreen is persistent (see this section's own header comment) - even
    // on a frame where nothing actually redrew it (md_beginFrame() wasn't
    // called this frame), re-uploading+re-presenting its unchanged pixels
    // is correct, just a plain repeat of the last real frame's content.
    if( gScreenTexture )
    {
        SDL_UpdateTexture( gScreenTexture, NULL, gScreen->pixels, gScreen->pitch );

        SDL_SetRenderDrawColor( gRenderer, 0, 0, 0, 255 );
        SDL_RenderClear( gRenderer );
        SDL_RenderCopy( gRenderer, gScreenTexture, NULL, NULL );

        // All three draw straight onto the backbuffer just cleared and
        // filled above - see this file's own declaration comment for why
        // that (rather than gScreen itself) is what makes none of them
        // able to accumulate frame over frame - and only during actual
        // gameplay, matching cglp's own `!isInMenu` gate.
        Uint32 nowTicks = SDL_GetTicks();
        float deltaTime = (float)( nowTicks - gLastFrameTicks ) / 1000.0f;
        gLastFrameTicks = nowTicks;

        if( gInGame )
        {
            if( gGlowEnabled )
              GlowEffect_Render( gRenderer, gScreen, gGlowEffect, 255, 255, 255, GLOW_INTENSITY );

            if( gOverlayMode == 1 )
              PixelGridEffect_Render( gRenderer, gPixelGridEffect );
            else if( gOverlayMode == 2 )
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
            SDL_Rect dialogRect = { MD_DIALOG_X, MD_DIALOG_Y, MD_DIALOG_W, MD_DIALOG_H };
            SDL_RenderCopy( gRenderer, gScreenTexture, &dialogRect, &dialogRect );
        }

        // Same idea, for the "-fps" overlay's own top-left rect (see
        // machineDependent.h's own md_setFpsOverlayShowing() comment) -
        // never overlaps the dialog box above (that one's centered, this
        // one's pinned to (0,0)), so the two re-composites are independent
        // and order between them doesn't matter.
        if( gFpsOverlayShowing )
        {
            SDL_Rect fpsRect = { 0, 0, gFpsOverlayW, gFpsOverlayH };
            SDL_RenderCopy( gRenderer, gScreenTexture, &fpsRect, &fpsRect );
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

static SDL_Surface* gThumbnails[ 48 ];
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
    for( int i = 0; i < gThumbnailBlobCount && i < 48; i++ )
    {
        SDL_RWops* rw = SDL_RWFromConstMem( gThumbnailBlobs[ i ].data, gThumbnailBlobs[ i ].len );
        SDL_Surface* surf = SDL_LoadBMP_RW( rw, 1 );
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

static SDL_AudioDeviceID gAudioDevice  = 0;
static int   gFrameCounter = 0;

// A bank of independent voices, not a single shared tone - matches the
// sibling Vircon32 build's own md_playTone() fix (see its own extensive
// header comment in portVircon32.c) and this project's own sdl3/
// sdlBackend.c, ported the same way here: TinyJoypad's original hardware
// is a single piezo buzzer, so every game's own Sound()/playTone() call
// site was written assuming "this replaces whatever's currently
// sounding," but that's a property of the ORIGINAL hardware, not
// something this function needs to enforce - a single shared
// gToneActive/gToneFreq/gTonePhase meant two genuinely concurrent cues
// (e.g. Tiny Pacman's continuously-retriggered power-pellet siren and its
// dot-eaten/ghost-eaten SFX) could never be heard at once, cutting each
// other off instead. Fixed by finding a free voice slot per call and
// mixing every active voice's own square wave together in the callback.
#define AUDIO_MAX_VOICES 16
static bool  gToneActive[ AUDIO_MAX_VOICES ];
static float gToneFreq[ AUDIO_MAX_VOICES ];
static float gTonePhase[ AUDIO_MAX_VOICES ];
static int   gToneStopFrame[ AUDIO_MAX_VOICES ];

// Master volume/mute - live-adjustable via BUTTON_VOLUP/BUTTON_VOLDOWN
// (PageUp/PageDown, ButRB/ButLB) and BUTTON_SOUNDSWITCH (S, ButY), edge-
// checked in sdlBackend_pollEvents() below. Matches cglpSDL2.c's own
// audioVolume step size (0.05f/press) for the volume control; mute is this
// project's own addition (cglp's ButY handler does something unrelated -
// a "buggy sine" waveform toggle - not a real mute).
static float gVolume = 1.0f;
static bool  gMuted   = false;

// void(void*, Uint8*, int), not SDL3's void(void*, SDL_AudioStream*, int,
// int) - SDL2's classic audio callback fills the destination buffer
// directly in-place (no separate "put data into a stream object" call
// needed the way SDL3's SDL_PutAudioStreamData() works), so this is
// actually simpler than the SDL3 version despite the older API: `stream`
// IS the buffer to fill, `len` is how many bytes it holds.
static void SDLCALL sdlAudioCallback( void* userdata, Uint8* stream, int len )
{
    (void)userdata;

    Sint16* buffer = (Sint16*)stream;
    int sampleCount = len / (int)sizeof( Sint16 );

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
        // see sdl3/sdlBackend.c's own identical fix for the full reasoning.
        if( mixed > 32767 ) mixed = 32767;
        if( mixed < -32768 ) mixed = -32768;
        buffer[ i ] = (Sint16)mixed;
    }
}

void md_initAudio()
{
    if( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 )
    {
        sdlLog( "Failed to init SDL audio subsystem: %s\n", SDL_GetError() );
        return;
    }

    // want/have, not a single spec + a returned stream object: SDL2's
    // SDL_OpenAudioDevice() negotiates against the device's own actual
    // capabilities and reports back what it really granted in `have` -
    // `want.samples` (a buffer size in SAMPLES, not bytes) has no SDL3
    // equivalent parameter at all, since SDL3's simplified
    // SDL_OpenAudioDeviceStream() picks a buffer size on its own.
    SDL_AudioSpec want, have;
    SDL_zero( want );
    want.freq     = AUDIO_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = sdlAudioCallback;

    gAudioDevice = SDL_OpenAudioDevice( NULL, 0, &want, &have, 0 );
    if( gAudioDevice == 0 )
    {
        sdlLog( "Failed to open audio device: %s\n", SDL_GetError() );
        return;
    }

    // SDL2 audio devices start PAUSED - unlike SDL3's SDL_OpenAudioDeviceStream
    // (which starts already-resumed, needing an explicit SDL_ResumeAudioDevice()
    // call to actually hear anything, matching this project's own src/sdl3/
    // code), SDL2's own device needs an explicit "un-pause" instead.
    SDL_PauseAudioDevice( gAudioDevice, 0 );
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
    // render it, not after a genuine frame of playback. See src/sdl3/
    // sdlBackend.c's own copy of this comment for the full investigation
    // (found via direct user report across Frogger/Pacman/HollowSeeker) -
    // this SDL2 port inherits the exact same fix, same root cause, since
    // main.c's own call order (gamesMain_dispatchFrame() before
    // md_updateAudio()) is unchanged between ports.
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

    // SDL_Init() returns 0 on success in SDL2 (a plain int), not the bool
    // SDL3 returns - every SDL_Init()/SDL_InitSubSystem() check in this
    // file is `!= 0`, not SDL3's `!(...)`, for that reason.
    if( SDL_Init( SDL_INIT_VIDEO ) != 0 )
    {
        sdlLog( "Failed to init SDL video: %s\n", SDL_GetError() );
        return false;
    }

    Uint32 windowFlags = SDL_WINDOW_RESIZABLE;
    // SDL_WINDOW_FULLSCREEN_DESKTOP (borderless, matches the current
    // desktop video mode), not plain SDL_WINDOW_FULLSCREEN (an actual
    // display-mode change) - matches cglpSDL2.c's own choice, generally
    // the more robust default on modern multi-monitor/HiDPI setups.
    if( gConfigFullscreen )
      windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    // 6-argument SDL_CreateWindow (title, x, y, w, h, flags), not SDL3's
    // 4-argument form (title, w, h, flags) - SDL2 never split window
    // position out of creation the way SDL3 did.
    gWindow = SDL_CreateWindow( "TinyJoypad SDL2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        gConfigWindowW, gConfigWindowH, windowFlags );

    if( !gWindow )
    {
        sdlLog( "Failed to create window: %s\n", SDL_GetError() );
        return false;
    }

    // (window, index, flags), not SDL3's (window, name) - index -1 means
    // "first driver matching flags" (SDL2 has no NULL-for-best-available
    // shorthand); SDL_RENDERER_PRESENTVSYNC folded into the flags here
    // rather than a separate post-creation call, since the runtime-
    // toggleable SDL_RenderSetVSync() is a newer (2.0.18+) SDL2 addition
    // this project has no reason to depend on when the flag does the same
    // job at creation time.
    // SDL_RENDERER_SOFTWARE ("-s") instead of the default _ACCELERATED -
    // SDL2 has no NULL/"best available" driver-name shorthand the way
    // SDL3 does (see this function's own comment above), so forcing
    // software here means swapping which one of these two mutually
    // exclusive flags gets requested, not passing a different name.
    Uint32 rendererFlags = gConfigSoftwareRendering ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    if( gConfigVsync )
      rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

    gRenderer = SDL_CreateRenderer( gWindow, -1, rendererFlags );
    if( !gRenderer )
    {
        sdlLog( "Failed to create renderer: %s\n", SDL_GetError() );
        return false;
    }

    gInput = CInput_Create();

    // SDL_GetRendererInfo(), not SDL3's SDL_GetRendererName() - SDL2 never
    // added a name-only shorthand, only the full info struct.
    SDL_RendererInfo rendererInfo;
    SDL_GetRendererInfo( gRenderer, &rendererInfo );
    sdlLog( "sdlBackend initialized: renderer=%s vsync=%s\n",
        rendererInfo.name, gConfigVsync ? "on" : "off" );

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
    return SDL_SaveBMP( gScreen, path ) == 0;
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
        SDL_FreeSurface( gScreen );
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

    // BUTTON_GLOWSWITCH (tinyjoypadSDL2.h) maps to ButX (CInput.c) - a
    // plain edge check against Input->PrevButtons, matching CInput's own
    // existing "current vs previous frame" tracking rather than adding a
    // second held-frame counter just for this one toggle. Only live during
    // actual gameplay (matching cglp's own `!isInMenu` gate on this same
    // handler) - toggling while on the menu, where none of these effects
    // ever render anyway (see md_endFrame()'s own gInGame check), would be
    // an invisible no-op that's more confusing than just ignoring it.
    //
    // Cycle ported directly from cglpSDL2.c's own ButX handler (see
    // gOverlayMode's own declaration comment for the exact 5-state
    // sequence and why pixel-grid/CRT are mutually exclusive there).
    if( gInGame && gInput->Buttons.ButX && !gInput->PrevButtons.ButX )
    {
        if( gOverlayMode == 0 )
        {
            if( gGlowEnabled )
              gGlowEnabled = false;
            else
            {
                gOverlayMode = 1;
                gGlowEnabled = true;
            }
        }
        else if( gOverlayMode == 1 )
        {
            if( gGlowEnabled )
              gGlowEnabled = false;
            else
            {
                gOverlayMode = 2;
                gGlowEnabled = false;
            }
        }
        else // gOverlayMode == 2
        {
            gOverlayMode = 0;
            gGlowEnabled = true;
        }
    }

    // BUTTON_VOLDOWN/BUTTON_VOLUP (PageDown/PageUp) map onto ButLB/ButRB
    // (CInput.c) - also the gamepad's shoulder buttons, but no game ever
    // reads ButLB/ButRB (machineDependent.h only exposes md_inputLeft/
    // Right/Up/Down/Fire/Fire2/Start to game code), so repurposing them
    // here doesn't steal input from anything. Edge-checked like the glow
    // cycle above, but deliberately NOT gated to gInGame - volume is a
    // player-level setting, not a gameplay effect. Not held-repeat, one
    // 0.05 step per press, same step size as cglpSDL2.c's own audioVolume
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
    }

    // BUTTON_FULLSCREEN (F3) drives ButFullscreen (CInput.c) - a live
    // toggle via SDL_SetWindowFullscreen(), independent of the -f startup
    // flag (sdlBackend_setFullscreen()/gConfigFullscreen, read once at
    // sdlBackend_init() time). Reuses gConfigFullscreen itself as the
    // current live state rather than a separate variable, since nothing
    // else reads it after init. SDL_SetWindowFullscreen() takes a Uint32
    // flags value here (0 = windowed, SDL_WINDOW_FULLSCREEN_DESKTOP =
    // fullscreen), not SDL3's plain bool - matches the same flag used at
    // window-creation time above.
    if( gInput->Buttons.ButFullscreen && !gInput->PrevButtons.ButFullscreen )
    {
        gConfigFullscreen = !gConfigFullscreen;
        SDL_SetWindowFullscreen( gWindow, gConfigFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0 );
    }
}

bool sdlBackend_shouldQuit()
{
    return gQuit;
}
