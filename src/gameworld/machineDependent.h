#ifndef MACHINE_DEPENDENT_H
#define MACHINE_DEPENDENT_H

// -----------------------------------------------------------------------------
// The per-port interface every TinyJoypad compatibility shim (tinyJoypadShim.h,
// obonoCoreShim.h) is built on top of - ported verbatim from the Vircon32
// build's own machineDependent.h (see tinyjoypad_vircon32/src/machineDependent.h),
// which was already pure standard-C-compatible declarations with zero
// Vircon32-specific syntax. Only addition here: <stdbool.h>/<stddef.h>,
// since Vircon32 has bool/true/false/NULL as real language keywords and
// this project never needed to #include anything for them.
//
// Bodies now live in sdlBackend.c (the "SDL platform backend" translation
// unit - see CLAUDE.md/the porting plan for why this must stay a SEPARATE
// TU from the "game world" side: it's the only file allowed to #include
// SDL.h, since SDL headers pull in <stdint.h>, which would hard-conflict
// with avrCompat.h's own uint8_t-aliased-to-int typedefs if both were ever
// visible in the same translation unit).
//
// Both upstream driver lineages (Lorandil/phoenixbozo's tinyJoypadUtils, and
// Obono's TinyJoypadWorks core) ultimately stream the real SSD1306 display
// one byte at a time, one hardware "page" at a time - each byte packs 8
// vertical pixels of a single column (bit 0 = top, bit 7 = bottom). There
// are only 256 possible byte values; the Vircon32 build pre-baked a 256-
// tile texture atlas to blit from (its GPU has no CPU-writable framebuffer)
// - the SDL build needs no such trick. md_drawColumn() draws each set pixel
// as a real GAME_SCALE x GAME_SCALE filled rect directly onto one shared
// 640x360 "real screen" canvas (see sdlBackend.c's own VIDEO section
// comment for why this is one canvas, not a separate small framebuffer
// scaled up at present time) - the same canvas md_drawSolidRect() and
// biosFont.h's menu/dialog text draw onto directly at native 1:1 scale.
// -----------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>

#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_PAGES  8

// Vircon32's own real screen resolution (its `screen_width`/`screen_height`
// BIOS constants) - the coordinate space md_drawSolidRect() and biosFont.h's
// text both draw in directly. Games never need these (they only ever use
// OLED_WIDTH/HEIGHT-space col/page coordinates via md_drawColumn()) - only
// the menu (menu.c) and the quit-confirmation dialog (main.c) do.
#define MD_SCREEN_WIDTH  640
#define MD_SCREEN_HEIGHT 360

// =============================================================================
//   VIDEO
// =============================================================================

void md_initVideo();

// Call once per real frame (from gamesMain_dispatchFrame()) with whether a
// game is actually running right now (currentGameIndex != -1) vs. the menu
// being shown - lets the platform side gate its own presentation-only
// effects (glow/CRT/pixel-grid overlay, see sdlBackend.c) the same way
// crisp-game-lib-portable-sdl's own cglpSDL3.c gates them on its own
// `!isInMenu`: those effects are about making actual gameplay look like a
// specific kind of display, so they'd look wrong applied to the plain
// BIOS-font menu screen too.
void md_setInGame( bool inGame );

// Real 640x360 screen-space rect of the quit-confirmation dialog box (see
// gamesMain.c's own drawConfirmQuitDialog()) - shared here, not just a
// local inside that function, so the platform side (see
// md_setDialogShowing() below) can know exactly which sub-rect to keep
// crisp/effect-free without duplicating (and risking drifting out of sync
// with) the same 4 numbers a second time.
#define MD_DIALOG_X 160
#define MD_DIALOG_Y 110
#define MD_DIALOG_W 320
#define MD_DIALOG_H 140

// Call once per real frame (from gamesMain_dispatchFrame(), alongside
// md_setInGame()) with whether the quit-confirmation dialog is showing
// right now. Unlike md_setInGame() (which stays true for the dialog's own
// entire duration too - it's still real gameplay, just paused), this is
// its own separate signal: the SDL ports' own presentation effects
// (glow/CRT/pixel-grid, gated on md_setInGame()) are meant to make the
// frozen game screen behind/around the dialog keep looking like actual
// gameplay while it's up, but the dialog box itself (drawn at MD_DIALOG_X/
// Y/W/H, above) should stay crisp and unaffected by them - this is what
// lets the platform side tell the two apart.
void md_setDialogShowing( bool showing );

// Call once per real frame that the "-fps" overlay is actually being drawn
// (gamesMain_drawFpsOverlay(), main.c's own showFps branch), with the exact
// pixel rect it just drew (always screen-space (0,0), but width varies with
// the digit count of the current FPS text - so unlike MD_DIALOG_X/Y/W/H
// above, this can't be a fixed compile-time constant). Same reasoning as
// md_setDialogShowing(): the SDL ports' own presentation effects should
// keep making actual gameplay look like a specific kind of display, but the
// debug FPS readout itself should stay crisp and legible on top of them,
// not blurred/scanlined/pixel-gridded along with everything else. Pass
// showing=false (width/height ignored) once "-fps" isn't in effect - the
// platform side otherwise has no way to know the overlay stopped being
// drawn this frame.
void md_setFpsOverlayShowing( bool showing, int width, int height );

// Requests the app quit at the top of the next real frame - the game-world
// side's own equivalent of the platform side's window-close/F4 handling
// (gamesMain_dispatchFrame()'s own Start-button handling calls this
// directly when gamesMain_setLaunchedDirectly() marked the current game as
// launched via -g/.joy, skipping the quit-confirmation dialog entirely -
// see that function's own comment in gamesMain.h for why). Vircon32 never
// needed a "game world requests the emulator/BIOS process exit" concept at
// all (no real OS process to quit), so - like md_setInGame()/
// md_setDialogShowing() above - this is a genuinely new, SDL-platform-
// specific addition, not a port of anything.
void md_requestQuit();

// clears the screen to black - called once at the start of every game frame,
// before that frame's md_drawColumn() calls
void md_beginFrame();

// col: 0..127 (OLED x). page: 0..7 (OLED y / 8). value: the raw SSD1306
// column byte (0-255) - a value of 0 means "all 8 pixels off" and is a no-op,
// since the frame was already cleared to black by md_beginFrame()
void md_drawColumn( int col, int page, int value );

// waits for vsync (wraps time.h's end_frame())
void md_endFrame();

// Draws a solid-color filled rectangle with its top-left corner at (x, y)
// - used for the quit-confirmation dialog's box (see main.c's dispatch
// loop), not something individual games call. This project is monochrome
// throughout (matching the real SSD1306 OLED every game was authored for),
// so `color` is just one of the two constants below rather than a real RGB
// value - unlike Vircon32's own color_white/color_black (opaque built-ins
// of a type this port doesn't have an equivalent for).
#define MD_COLOR_BLACK 0
#define MD_COLOR_WHITE 1
void md_drawSolidRect( int x, int y, int w, int h, int color );

// Sets up to 32 vertical white pixels in one column, starting at absolute
// pixel row y (NOT page-aligned like md_drawColumn() - bit0 is the pixel
// at row y, bit1 at y+1, etc, for `count` bits). Used by biosFont.h's menu
// text blitter, whose glyphs are 20px tall and don't line up with the 8px
// SSD1306 "page" every game's own md_drawColumn() calls assume - games
// themselves never call this. count is a plain 20-30-ish for a text glyph
// column - well under 32, so a single int always holds the whole run.
void md_drawColumnPixels( int x, int y, int bits, int count );

// Pixel size of a game's menu thumbnail - shared here so callers (menu.c)
// can lay out around it (e.g. centering it vertically) without duplicating
// the actual asset dimensions.
#define MD_THUMBNAIL_WIDTH  256
#define MD_THUMBNAIL_HEIGHT 128

// How many games have a pre-baked gameplay thumbnail. The menu uses this
// to skip drawing a thumbnail for any game index at or past it (e.g. a
// newly-added game before a thumbnail exists for it), rather than
// assuming every menu entry has one.
int md_getThumbnailCount();

// Draws gameIndex's pre-baked gameplay screenshot (MD_THUMBNAIL_WIDTH x
// MD_THUMBNAIL_HEIGHT) with its top-left corner at (x, y). No-op if
// gameIndex is out of range - callers should still gate on
// md_getThumbnailCount() first rather than relying on this no-op alone,
// since drawing nothing there is a silent no-op, not an error.
void md_drawGameThumbnail( int gameIndex, int x, int y );

// =============================================================================
//   INPUT
// =============================================================================

bool md_inputLeft();
bool md_inputRight();
bool md_inputUp();
bool md_inputDown();

// A level read like the rest, EXCEPT immediately after md_armInputFireGate()
// is called: from then until the physical button is actually released,
// this always reports false - see md_armInputFireGate()'s own comment.
bool md_inputFire();

bool md_inputStart();

// A second, independent action button (Vircon32's B) - only TinyMinez
// needs this so far. Unlike Fire, it's never involved in the menu->game
// launch handoff, so it needs no fire-gate equivalent.
bool md_inputFire2();

// Call once, right when a game is (re)launched from the menu, to suppress
// md_inputFire() until the confirm press that launched it is physically
// released - otherwise that same press can bleed into the game's very
// first frame and be misread as the player's own input.
void md_armInputFireGate();

// Raw held-frame counters: positive N means "held for N real frames"
// (N==1 the instant it was pressed), negative N means "released N real
// frames ago". Games that only tick their own logic once every few real
// frames (see the TICKS_PER_FRAME-style frame-skip pattern used by the
// reduced-fps ports) must not rely on md_inputXXX()'s plain bool for
// one-shot "just pressed" detection: a tap that both started and ended
// during a skipped real frame would already show a *negative* frames
// value again by the time the next logic tick finally reads it, but a tap
// that started during a skipped frame and is still held now would read as
// N==2 (or more) instead of the N==1 an unthrottled game's edge-check
// expects - so a naive "== 1" check silently misses it. Use
// md_recentlyPressed() below against these raw values instead, sized to
// the game's own frame-skip window.
int md_inputLeftFrames();
int md_inputRightFrames();
int md_inputUpFrames();
int md_inputDownFrames();
int md_inputFireFrames();

// True if a button's raw held-frame counter shows it became newly pressed
// at any point within the last `window` real frames (inclusive) - the
// safe replacement for a plain "== 1" edge check in any game whose logic
// only ticks once every `window` real frames, so a press landing on one
// of the skipped frames in between still gets recognized as "just
// pressed" on the next tick that actually runs. window == 1 (an
// unthrottled game, ticking every real frame) reduces this to exactly the
// traditional single-frame edge check.
#define md_recentlyPressed(framesValue, window) ( (framesValue) >= 1 && (framesValue) <= (window) )

// =============================================================================
//   TIMING
// =============================================================================

// Vircon32's own BIOS exposes get_frame_counter()/frames_per_second as real
// built-ins (time.h) - obonoCoreShim.c's note-sequencer (obonoCoreShimAdvanceScore())
// uses both to schedule each note's start relative to real elapsed frames.
// No SDL equivalent exists, so this backend provides its own: a plain
// incrementing counter, advanced once per real frame by md_updateAudio()
// (matching the Vircon32 build's own frame-counted md_playTone()/
// md_stopTone() duration tracking, which already needed the same idea).
#define MD_FRAMES_PER_SECOND 60
int md_getFrameCounter();

// =============================================================================
//   AUDIO
// =============================================================================

void md_initAudio();

// Starts playing freqHz for durationSeconds, replacing whatever tone is
// currently sounding - TinyJoypad's original hardware is a single piezo
// buzzer, so games only ever expect one tone active at a time. freqHz <= 0
// is treated as silence (used by ports of Sound(0, dur) rest/pause calls).
// Does not block: it returns immediately and the tone is stopped
// automatically by md_updateAudio() once its duration elapses, so
// gameplay/animation keeps running during a sound effect instead of
// freezing for it.
void md_playTone( float freqHz, float durationSeconds );

// stops the current tone immediately (no fade) - used when leaving a game
// (returning to the menu) so no audio survives into the next screen
void md_stopTone();

// advances the scheduled auto-stop - call exactly once per frame,
// regardless of which game (if any) is running
void md_updateAudio();

#endif
