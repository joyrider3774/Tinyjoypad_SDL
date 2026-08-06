// -----------------------------------------------------------------------------
// The Playdate port. Structured after the same-author sibling
// crisp-game-lib-portable-sdl project's own src/cglpPlaydate/src/cglpPlaydate.c
// (single-file eventHandler()+update()-callback shape, matching the official
// SDK's own Examples/Hello World convention too) - NOT split into a separate
// "backend" + "main" pair the way src/sdl3/ and src/sdl2/ are, since Playdate
// has no CLI/window/event-queue surface for a second file to own.
//
// Reuses src/gameworld/ (all 33 games, the shim layer, addGames()'s
// registration table) completely unmodified - same as every other port. Does
// NOT reuse gameworld/menu.c's own menu_update() or gamesMain.c's own
// gamesMain_dispatchFrame()/drawConfirmQuitDialog(), both of which assume a
// real 640x360 canvas and the BIOS font (biosFont.h) - Playdate's screen is a
// fixed 400x240 1-bit panel, an entirely different scale/color model that
// doesn't fit that design at all (per direct user request: "we may need a
// new menu for playdate"). This file's own menuUpdate() below is a from-
// scratch, Playdate-native replacement (Playdate's own system font via
// pd->graphics->drawText(), a scrolling list, no thumbnails) - it still
// walks the exact same menu.h Game table (menu_getGame()/gameCount,
// populated by the unmodified menuGameList.c's addGames()), just rendered
// and navigated differently. Same reasoning for the quit-confirmation
// dialog: this port has no equivalent UI at all, matching cglpPlaydate.c's
// own precedent exactly - holding A+B+Up+Right together returns to the menu
// immediately, no YES/NO prompt (see update()'s own comment below).
//
// Explicitly out of scope per direct user request ("we don't need the
// effects, we don't need color to pattern stuff... we don't need all the
// command line and SDL specific stuff"): the glow/CRT/pixel-grid
// presentation effects (meaningless on a fixed 1-bit panel), any color-to-
// dither-pattern conversion (TinyJoypad's own game data is already pure
// black/white, unlike crisp-game-lib's own RGB-per-object model that
// cglpPlaydate.c's own md_drawRect() has to convert via LCDPattern), and
// every CLI flag src/sdl3's main.c parses (-w/-h/-f/-ns/-fps/-nd/-list/-g/
// -ms/-joy - none of them make sense on fixed hardware with no shell to
// launch from). Also skips the volume/mute/fullscreen keybinds added to the
// SDL ports this same session (BUTTON_VOLUP/DOWN/SOUNDSWITCH/GLOWSWITCH/
// F3) - Playdate has no window to un-fullscreen, no keyboard, and system
// volume is the player's own hardware buttons, not something a game reads.
// -----------------------------------------------------------------------------

#include "pd_api.h"
#include <string.h>

#include "machineDependent.h"
#include "menu.h"
#include "menuGameList.h"
#include "obonoCoreShim.h"
#include "eepromShim.h"

static PlaydateAPI* pd;

// =============================================================================
//   VIDEO
// =============================================================================

// IMPORTANT: this does NOT use pd->display->setScale()/setOffset() (an
// earlier revision of this port tried to, and it silently "had no effect" -
// see the investigation below) - GAME_SCALE is a manual per-draw-call
// multiply in md_drawColumn(), matching every SDL port's own GAME_SCALE
// approach exactly, just emitting Playdate fillRect() calls instead of
// SDL_FillSurfaceRect() ones.
//
// Why not pd->display->setScale(): its own doc comment
// (Inside Playdate with C.html) states plainly "Valid values for scale are
// 1, 2, 4, and 8" - 3 (which would have been the natural choice, since
// 128*3=384/64*3=192 fits neatly under 400x240) is not a legal value at
// all, which is exactly why it silently did nothing when tried. Even a
// legal value wouldn't have worked the way this port originally assumed,
// though: per that same doc comment, setScale() doesn't transform
// subsequent drawing-call coordinates the way SDL's own logical-
// presentation feature (see src/sdl3/sdlBackend.c) does - it re-samples a
// small TOP-LEFT REGION of the real, always-400x240-sized framebuffer and
// magnifies just that region to fill the display ("the pixels in rectangle
// [0,100]x[0,60] are drawn on the screen as 4x4 squares" is the docs' own
// example at scale 4). Even the one legal value that doesn't overflow
// (scale 2, exposing a 200x120 sample region) would only have used
// 128x64 of that available 200x120, wasting most of the screen - scale 4
// overflows outright (512x256 > 400x240). Manual scaling has no such
// power-of-2 restriction, so it can hit the actual best fit (384x192) this
// port always intended.
#define GAME_SCALE    3  // 128*3=384, 64*3=192 - the best fit under 400x240
#define GAME_ORIGIN_X 8  // (400 - 128*3) / 2
#define GAME_ORIGIN_Y 24 // (240 - 64*3)  / 2

// Nothing to do here - unlike SDL (which needs a window/renderer/streaming
// texture set up before any draw call is legal), Playdate's graphics
// context always exists from process start, and (per the GAME_SCALE
// comment above) this port never touches pd->display's own scale/offset
// state at all, so there's no per-mode display transform to set up either.
void md_initVideo() {}

// Present only to satisfy machineDependent.h's own contract (main.c's
// gamesMain_dispatchFrame()-equivalent dispatch, below, calls it once per
// real frame the same way every other port's own main loop does) - has
// nothing to actually track since GAME_SCALE's own fixed origin/scale
// never changes between menu and gameplay.
void md_setInGame( bool inGame ) { (void)inGame; }

// No quit-confirmation dialog on this port at all (see this file's own
// header comment - A+B+Up+Right held returns to the menu immediately,
// with no dialog to keep effect-free in the first place) - and no
// presentation effects either (glow/CRT/pixel-grid, also out of scope per
// this file's own header comment), so there's nothing for this signal to
// actually drive here. Still needs a real definition to satisfy
// machineDependent.h's own contract, same reasoning as md_inputStart().
void md_setDialogShowing( bool showing ) { (void)showing; }

// Same reasoning as md_setDialogShowing() just above - no "-fps" flag on
// this port at all (see this file's own header comment's own CLI-flag
// scope list) and no presentation effects for it to stay crisp on top of
// either, but gamesMain.c's own gamesMain_drawFpsOverlay() (compiled as
// part of this port's own shared ../gameworld build regardless of whether
// anything here actually calls it) still references this symbol, so a
// real no-op definition is needed to link.
void md_setFpsOverlayShowing( bool showing, int width, int height ) { (void)showing; (void)width; (void)height; }

// No -g/.joy-file direct launch on this port at all (no CLI, no shell to
// launch from - see this file's own header comment's own CLI-flag scope
// list), so gamesMain_setLaunchedDirectly() is never called here and this
// should never actually run - but gamesMain.c's own dispatch (compiled as
// part of this port's own shared ../gameworld build regardless of whether
// anything here calls it) still references this symbol, same reasoning as
// md_setFpsOverlayShowing() just above. Real Playdate hardware has no
// "quit the app" concept anyway (Menu button/A+B+Up+Right chord both
// return to THIS port's own menu, they don't exit) - a genuine no-op, not
// a stand-in for some equivalent this port doesn't have yet.
void md_requestQuit() {}

void md_beginFrame()
{
    // kColorBlack background / kColorWhite "on" bit (md_drawColumn() below)
    // - direct user choice, overriding an earlier revision of this port's
    // own reasoning (which had these two swapped: kColorWhite background /
    // kColorBlack "on" bit, on the theory that Playdate's reflective panel
    // - dark ink on a light background, like paper - is the opposite
    // polarity from TinyJoypad's own emissive OLED, where a "lit" pixel is
    // the bright foreground). This matches the *original* OLED's own
    // polarity directly instead (bright "on" pixels on a dark background),
    // not inverted for the panel. Clears the WHOLE physical panel (game
    // content is only ever drawn in the GAME_ORIGIN_X/Y-offset region -
    // everything outside that stays this background color, acting as the
    // border), matching every SDL port's own md_beginFrame() clearing its
    // whole persistent canvas including its own letterbox border.
    pd->graphics->clear( kColorBlack );
}

void md_drawColumn( int col, int page, int value )
{
    // Same byte-truncation risk as every other port (avrCompat.h's
    // uint8_t/etc are plain `int`s here too) - mask once here, the single
    // choke point every game/shim's column value funnels through.
    value &= 0xFF;

    if( value == 0 )
      return;

    int x  = GAME_ORIGIN_X + col * GAME_SCALE;
    int y0 = GAME_ORIGIN_Y + page * OLED_PAGES * GAME_SCALE;

    // kColorWhite, not kColorBlack: TinyJoypad's original SSD1306 OLED is
    // emissive (a "lit" pixel - this function's own "on" bits - is bright
    // foreground against a dark background), matching every other port's
    // own gWhitePixel-for-"on" convention directly - see md_beginFrame()'s
    // own comment for why this port matches that polarity as-is rather
    // than inverting it for Playdate's own reflective (dark-ink-on-light)
    // panel, which an earlier revision of this port did instead.
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

        pd->graphics->fillRect( x, y0 + runStart * GAME_SCALE, GAME_SCALE, runLen * GAME_SCALE, kColorWhite );
    }
}

// =============================================================================
//   PIXEL GRID EFFECT - the only one of the 3 SDL-port presentation
//   effects this port has an equivalent of (see this file's own header
//   comment for why glow/CRT aren't built here at all: glow is a soft
//   blur, meaningless on a fixed 1-bit panel, and CRT scanlines don't fit
//   the "we don't need the effects" descope either - pixel grid is
//   different, a crisp on/off outline, cheap and legible even at 1-bit,
//   so it's worth having, gated behind Playdate's own system menu instead
//   of a keybind, per direct user request).
// =============================================================================

// A static "LCD pixel grid" overlay - a thin opaque black outline drawn
// around every source pixel's own boundary, so each of the original low-
// res OLED pixels reads as its own distinct visible cell once scaled up
// by GAME_SCALE, instead of blending into one smooth block. Built the same
// way every SDL port's own pixelGridEffect.c does it (see that file's own
// header comment for the full "why a thin line at EVERY source-pixel
// boundary, not cglp's own thicker-line-every-other-pixel formula"
// reasoning) - pre-baked ONCE into an LCDBitmap at Create time (here,
// pixelGridEffectInit(), called once from init() below), then just drawn
// unchanged every frame it's enabled, rather than re-drawing GAME_SCALE
// lines by hand each frame.
static LCDBitmap* gPixelGridBitmap  = NULL;
static bool       gPixelGridEnabled = false; // default OFF - see the system-menu checkmark item below

static void pixelGridEffectInit()
{
    // Sized to exactly the scaled game canvas (GAME_SCALE*128 x
    // GAME_SCALE*64 = 384x192), not the full 400x240 panel - there's
    // nothing to grid-line outside the actual game content area.
    // kColorClear background: only the grid lines themselves should be
    // opaque, so drawBitmap() below leaves every other pixel of the
    // already-drawn game content showing through untouched.
    gPixelGridBitmap = pd->graphics->newBitmap( GAME_SCALE * OLED_WIDTH, GAME_SCALE * OLED_HEIGHT, kColorClear );
    if( !gPixelGridBitmap )
      return;

    pd->graphics->pushContext( gPixelGridBitmap );

    for( int x = 0; x < GAME_SCALE * OLED_WIDTH; x += GAME_SCALE )
      pd->graphics->fillRect( x, 0, 1, GAME_SCALE * OLED_HEIGHT, kColorBlack );

    for( int y = 0; y < GAME_SCALE * OLED_HEIGHT; y += GAME_SCALE )
      pd->graphics->fillRect( 0, y, GAME_SCALE * OLED_WIDTH, 1, kColorBlack );

    pd->graphics->popContext();
}

// Called once per real frame during actual gameplay only (see update()
// below) - draws the pre-baked grid bitmap at the same GAME_ORIGIN_X/Y
// offset md_drawColumn() itself draws through, so it lines up exactly
// with the scaled game content underneath.
static void pixelGridEffectRender()
{
    if( !gPixelGridEnabled || !gPixelGridBitmap )
      return;

    pd->graphics->drawBitmap( gPixelGridBitmap, GAME_ORIGIN_X, GAME_ORIGIN_Y, kBitmapUnflipped );
}

// Playdate's own update-callback model presents automatically once
// update() (below) returns 1 - no manual "present"/vsync-wait call exists
// or is needed, unlike SDL's SDL_RenderPresent()/vsync-locked loop.
void md_endFrame()
{
    // Standard NDEBUG convention (CMake's own Debug/Release configuration
    // types - see CMakeLists.txt's own CMAKE_CONFIGURATION_TYPES - define
    // this automatically for Release, not Debug) rather than a bespoke
    // macro: the SDK's own FPS counter is a debug tool, not something a
    // released build should show players.
#ifndef NDEBUG
    pd->system->drawFPS(0,0);
#endif
}

// Both unused on this port - see this file's own header comment for why
// (no BIOS-font menu, no quit-confirmation dialog) - kept as real, callable
// no-op definitions purely to satisfy machineDependent.h's own contract
// (every other gameworld file that might reference them - biosFont.h's
// md_drawColumnPixels() calls, gamesMain.c's drawConfirmQuitDialog()'s
// md_drawSolidRect() calls - still needs *something* to link against, even
// though this port's own main()/update() below never calls gamesMain.c's
// dispatch function that would have invoked them).
void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}

void md_drawColumnPixels( int x, int y, int bits, int count )
{
    (void)x; (void)y; (void)bits; (void)count;
}

// No thumbnails on this port (no BIOS-font/640x360 menu to show them in -
// see this file's own header comment) - 0 unconditionally, matching
// md_getThumbnailCount()'s own documented "no thumbnail yet" contract.
int md_getThumbnailCount() { return 0; }

void md_drawGameThumbnail( int gameIndex, int x, int y )
{
    (void)gameIndex; (void)x; (void)y;
}

// =============================================================================
//   INPUT
// =============================================================================

// Raw held-frame counters, updated once per real frame in update() below -
// same shape/contract as every other port's own copy (see src/sdl3/
// sdlBackend.c's identical helper for the full reasoning).
static int gLeftFrames  = 0;
static int gRightFrames = 0;
static int gUpFrames    = 0;
static int gDownFrames  = 0;
static int gFireRawFrames = 0; // pre-gate, see md_inputFireFrames() below
static bool gButB = false;     // Fire2 - level, not a counter (only TinyMinez reads it)

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
// - same contract/reasoning as every other port (ported from the Vircon32
// build's own portVircon32.c originally) - armed once when a game is
// (re)launched from the menu, so the same A press that confirmed the menu
// selection can't also be misread as the game's own first-frame input.
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

// No Start-button concept on this port: there is no quit-confirmation
// dialog to trigger (see this file's own header comment and update()'s own
// A+B+Up+Right gesture) - gamesMain.c's own dispatch (the only caller of
// md_inputStart() anywhere in the game world) isn't used by this port
// either, so this is never actually read, but still needs a real
// definition to satisfy machineDependent.h's own contract.
bool md_inputStart() { return false; }

bool md_inputFire2() { return gButB; }

void md_armInputFireGate() { gInputFireGateActive = true; }

// =============================================================================
//   AUDIO
// =============================================================================

// Real Playdate hardware caps out at 50fps for full-screen refreshes (a
// hardware/panel limit, not a tunable setting) - requested via
// pd->display->setRefreshRate() in init() below. Declared up here (not
// next to that call) because md_updateAudio()'s own gFrameCounter
// compensation needs it too - see that function's own comment for why
// requesting MD_FRAMES_PER_SECOND (60, gameworld's own shared "SDL ports
// run at 60fps" constant) directly wouldn't have been correct here.
#define PLAYDATE_REFRESH_RATE 50

// One synth, not a pool (unlike the sibling cglpPlaydate.c's own
// SYNTH_COUNT==4 round-robin, needed there for crisp-game-lib's own
// overlapping multi-note sound effects) - TinyJoypad's original hardware is
// a single piezo buzzer, so every shim's Sound()/playTone() call already
// expects exactly one tone active at a time, matching every other port's
// own single-voice choice.
static PDSynth* gSynth = NULL;
static int gFrameCounter = 0;
static float gFrameCounterAccumulator = 0.0f; // see md_updateAudio()'s own comment

void md_initAudio()
{
    gSynth = pd->sound->synth->newSynth();
    // Square, not Playdate's own default sine - matches every other port's
    // own square-wave choice (see src/sdl3/sdlBackend.c's sdlAudioCallback()
    // for the full reasoning: matches the real hardware's own bit-banged
    // square wave, and a sine is frequently inaudible at the very low
    // nominal frequencies/short durations several games' own sound effects
    // use). Playdate's synth engine supports this directly as a built-in
    // waveform - no manual oscillator/sample-buffer code needed at all,
    // unlike SDL's own raw-callback approach.
    pd->sound->synth->setWaveform( gSynth, kWaveformSquare );
}

void md_playTone( float freqHz, float durationSeconds )
{
    if( freqHz <= 0.0f )
    {
        // freq <= 0 is a deliberate silent "rest" (matches every other
        // port's own contract) - stop whatever's currently sounding, start
        // nothing new.
        pd->sound->synth->noteOff( gSynth, 0 );
        return;
    }

    // when=0 ("now", no scheduling - this contract never schedules ahead)
    // - Playdate's own synth handles the note's automatic stop after `len`
    // (durationSeconds) itself, unlike every other port's own manual
    // gFrameCounter/gToneStopFrame bookkeeping in md_updateAudio() - no
    // equivalent needed here at all, since the SDK's own audio engine
    // already tracks real elapsed time sample-accurately.
    pd->sound->synth->playNote( gSynth, freqHz, 0.5f, durationSeconds, 0 );
}

void md_stopTone()
{
    pd->sound->synth->noteOff( gSynth, 0 );
}

// gFrameCounter still needs to advance even though md_playTone()'s own
// duration handling doesn't need it any more (see above) -
// obonoCoreShim.c's own note-sequencer (obonoCoreShimAdvanceScore(), used
// by the 3 obonoCoreShim-lineage games) schedules its OWN next-note timing
// as md_getFrameCounter() + (durationMs/1000)*MD_FRAMES_PER_SECOND - a
// shared gameworld constant (60) this port can't change, since the SDL
// ports really do run at 60fps. This port's own real update()-callback
// rate is only PLAYDATE_REFRESH_RATE (50 - see init()'s own comment: the
// real hardware's own cap for full-screen refreshes), not 60, so advancing
// gFrameCounter by a flat +1 per real callback would make obonoCoreShim's
// own music/timing run 60/50 = 1.2x too SLOW in real wall-clock time.
// Accumulating fractional "virtual" frames instead (MD_FRAMES_PER_SECOND /
// PLAYDATE_REFRESH_RATE == 1.2 per real callback, only incrementing the
// real int counter once the fractional remainder crosses a whole number)
// keeps md_getFrameCounter() advancing at the same real-time rate
// obonoCoreShim's own math already assumes, regardless of the mismatch
// between the two frame rates.
void md_updateAudio()
{
    gFrameCounterAccumulator += (float)MD_FRAMES_PER_SECOND / (float)PLAYDATE_REFRESH_RATE;
    while( gFrameCounterAccumulator >= 1.0f )
    {
        gFrameCounter++;
        gFrameCounterAccumulator -= 1.0f;
    }
}

int md_getFrameCounter() { return gFrameCounter; }

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// A real implementation - saves into this game's own private per-
// cartridge data folder via pd->file (kFileWrite/kFileReadData), the same
// real Playdate persistence mechanism crisp-game-lib-portable-sdl's own
// cglpPlaydate.c already uses for its own high-score save
// (loadHighScores()/saveHighScores(), "savestate.srm"). No home-directory
// concept applies here the way it does on the SDL ports - a plain
// relative filename with no leading slash is automatically written into
// this game's own sandboxed Data/<bundleid> folder, the platform's own
// equivalent of "the user's persistent storage for this app".
//
// Playdate's own file API has no true random-access read-modify-write the
// way stdio's "r+b" mode gives the SDL ports (kFileWrite always creates
// fresh/truncates) - so instead of seeking within an open handle, every
// read/write here works against one whole in-memory copy of the file:
// read pulls the entire file in, write patches the in-memory copy at the
// given offset (growing/zero-filling it as needed) then writes the WHOLE
// thing back out in one kFileWrite pass. The file tops out around 36KB
// (eepromShim.c's own EEPROM_MAX_SLOTS * ~570 bytes/slot) and this only
// ever runs once per game launch (a read) or on a genuine new high score
// (a write), never per-frame, so the extra whole-file I/O each time isn't
// a real cost.
#define CARD_FILE_NAME "highscores.dat"
#define CARD_SIGNATURE_TEXT "TINYJOYPADSDL01"
#define CARD_SIGNATURE_BYTES 32 // just needs to not overlap eepromShim.c's own first slot offset, not to numerically match it

// Reads the whole file into a pd->system->realloc()'d buffer - the file
// API has no direct handle-based "get size" call, so this just grows a
// buffer in chunks until read() stops returning data. *outBuf is NULL and
// *outLen is 0 (a legitimate "no file yet" result, not an error the
// caller needs to special-case) if the file doesn't exist.
static int cardReadWholeFile( unsigned char** outBuf, int* outLen )
{
    *outBuf = NULL;
    *outLen = 0;

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileReadData );
    if( !fp )
      return 0;

    int cap = 4096;
    unsigned char* buf = pd->system->realloc( NULL, (size_t)cap );
    int len = 0;
    for( ;; )
    {
        if( len + 4096 > cap )
        {
            cap *= 2;
            buf = pd->system->realloc( buf, (size_t)cap );
        }
        int n = pd->file->read( fp, buf + len, 4096 );
        if( n <= 0 )
          break;
        len += n;
    }
    pd->file->close( fp );

    *outBuf = buf;
    *outLen = len;
    return 1;
}

bool md_cardIsConnected()
{
    // Always true - Playdate's own sandboxed per-game data folder always
    // exists and is always writable, unlike Vircon32's real removable
    // memory card, which can genuinely be absent.
    return true;
}

bool md_cardHasOurSignature()
{
    unsigned char* buf;
    int len;
    if( !cardReadWholeFile( &buf, &len ) )
      return false;

    bool matches = ( len >= (int)strlen( CARD_SIGNATURE_TEXT ) )
                 && ( memcmp( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) ) == 0 );

    pd->system->realloc( buf, 0 );
    return matches;
}

void md_cardWriteSignature()
{
    unsigned char* buf;
    int len;
    cardReadWholeFile( &buf, &len ); // buf==NULL/len==0 (fresh file) is fine

    if( len < CARD_SIGNATURE_BYTES )
    {
        unsigned char* grown = pd->system->realloc( buf, (size_t)CARD_SIGNATURE_BYTES );
        memset( grown + len, 0, (size_t)( CARD_SIGNATURE_BYTES - len ) );
        buf = grown;
        len = CARD_SIGNATURE_BYTES;
    }

    memset( buf, 0, CARD_SIGNATURE_BYTES );
    memcpy( buf, CARD_SIGNATURE_TEXT, strlen( CARD_SIGNATURE_TEXT ) );

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileWrite );
    if( fp )
    {
        pd->file->write( fp, buf, (unsigned int)len );
        pd->file->close( fp );
    }

    pd->system->realloc( buf, 0 );
}

void md_cardReadData( void* dest, int offsetBytes, int sizeBytes )
{
    memset( dest, 0, (size_t)sizeBytes );

    unsigned char* buf;
    int len;
    if( !cardReadWholeFile( &buf, &len ) )
      return;

    int avail = len - offsetBytes;
    if( avail > 0 )
    {
        int copyLen = avail < sizeBytes ? avail : sizeBytes;
        memcpy( dest, buf + offsetBytes, (size_t)copyLen );
    }

    pd->system->realloc( buf, 0 );
}

void md_cardWriteData( void* src, int offsetBytes, int sizeBytes )
{
    unsigned char* buf;
    int len;
    cardReadWholeFile( &buf, &len ); // buf==NULL/len==0 (fresh file) is fine

    int neededLen = offsetBytes + sizeBytes;
    if( len < neededLen )
    {
        unsigned char* grown = pd->system->realloc( buf, (size_t)neededLen );
        memset( grown + len, 0, (size_t)( neededLen - len ) );
        buf = grown;
        len = neededLen;
    }

    memcpy( buf + offsetBytes, src, (size_t)sizeBytes );

    SDFile* fp = pd->file->open( CARD_FILE_NAME, kFileWrite );
    if( fp )
    {
        pd->file->write( fp, buf, (unsigned int)len );
        pd->file->close( fp );
    }

    pd->system->realloc( buf, 0 );
}

// =============================================================================
//   MENU - Playdate-native (see this file's own header comment for why
//   gameworld/menu.c's own menu_update() isn't used here)
// =============================================================================

static LCDFont* gMenuFont = NULL;
static int gMenuRowHeight = 14;
static int gMenuSelection = 0; // a DISPLAY position (0..gameCount-1, alphabetized) - see gDisplayOrder[] below
static bool gPrevMenuUp = false;
static bool gPrevMenuDown = false;
static bool gPrevMenuLeft = false;
static bool gPrevMenuRight = false;

// games[] itself (menu.h's own Game table, populated by menuGameList.c's
// unmodified addGames()) stays in registration order - that's also what
// thumbnails/thumb_NN.png (below) are keyed by, since they were generated
// in that same order (see assets/thumbnails/'s own convention, shared by
// every port). Alphabetical sorting is purely a *display* concern, kept in
// this separate indirection array instead - gDisplayOrder[i] holds the
// registration index shown at display position i. Re-implements
// gameworld/menu.c's own identical displayOrder[]/menu_buildDisplayOrder()
// locally rather than exposing that header-private array through a new
// cross-port accessor, since nothing else needs it.
// Bumped 48->64 alongside gameworld/menu.c's own identical MAX_GAMES bump
// (see that file's own comment) when this project's own registered count
// reached 50 - keep the two in sync, since this array is a from-scratch
// reimplementation of that one, not a shared constant.
#define MENU_MAX_GAMES 64
static int gDisplayOrder[ MENU_MAX_GAMES ];

static LCDBitmap* gThumbnails[ MENU_MAX_GAMES ];
static int gThumbnailCount = 0;

#define MENU_TITLE_Y      2
#define MENU_HINT_Y       16
#define MENU_PAGE_Y       28
#define MENU_LIST_TOP     42
#define MENU_LIST_BOTTOM  236
#define MENU_LEFT_MARGIN  12
#define MENU_THUMB_X      264
#define MENU_THUMB_W      128
#define MENU_THUMB_H      64
#define MENU_THUMB_AUTHOR_GAP 4

// Appends `n` (0-99) as exactly 2 zero-padded decimal digits - the "NN. "
// row-number prefix is always 2 digits for this project's own 33 games, so
// this is simpler than a general itoa (avrCompat.h's own itoa() isn't
// available here - this file is part of the "platform" side, which never
// includes avrCompat.h, same reasoning as every game/shim file's own
// deliberate not-including-headers-they-don't-need).
static void appendZeroPadded2( char* buf, int n )
{
    int len = strlen( buf );
    buf[ len ]     = '0' + ( ( n / 10 ) % 10 );
    buf[ len + 1 ] = '0' + ( n % 10 );
    buf[ len + 2 ] = 0;
}

// General append for the page indicator ("PAGE 1/3") - unlike the row
// prefix above, totalPages/currentPage aren't known to always be 2 digits.
static void appendInt( char* buf, int n )
{
    char digits[ 12 ];
    int count = 0;
    if( n == 0 )
    {
        digits[ count++ ] = '0';
    }
    else
    {
        while( n > 0 )
        {
            digits[ count++ ] = '0' + ( n % 10 );
            n /= 10;
        }
    }

    int len = strlen( buf );
    while( count > 0 )
      buf[ len++ ] = digits[ --count ];
    buf[ len ] = 0;
}

// Selection sort on gDisplayOrder (by title) - gameCount is always a small
// handful of entries (33 today), so O(n^2) costs nothing measurable here.
static void menuBuildDisplayOrder()
{
    for( int i = 0; i < gameCount; i++ )
      gDisplayOrder[ i ] = i;

    for( int i = 0; i < gameCount - 1; i++ )
    {
        int best = i;
        for( int j = i + 1; j < gameCount; j++ )
          if( strcmp( menu_getGame( gDisplayOrder[ j ] )->title,
                      menu_getGame( gDisplayOrder[ best ] )->title ) < 0 )
            best = j;

        if( best != i )
        {
            int tmp = gDisplayOrder[ i ];
            gDisplayOrder[ i ] = gDisplayOrder[ best ];
            gDisplayOrder[ best ] = tmp;
        }
    }
}

// thumbnails/thumb_00.png .. thumb_32.png (src/playdate/Source/thumbnails/,
// converted from the same assets/thumbnails/*.bmp every SDL port already
// uses - see that directory's own generation note - downscaled 256x128 ->
// 128x64, an exact 2x reduction that lands precisely on TinyJoypad's own
// real OLED resolution) - loaded once here, referenced by path with no
// extension (Playdate's own `pdc` build tool converts a Source/*.png into
// its own bundled bitmap format at build time; pd->graphics->loadBitmap()
// looks it up by that same relative path at runtime). Probed sequentially
// like every other port's own md_getThumbnailCount(), stopping at the
// first missing index - registration-order-keyed, matching every other
// port's own thumbnail convention, not display order.
static void menuLoadThumbnails()
{
    gThumbnailCount = 0;
    for( int i = 0; i < MENU_MAX_GAMES; i++ )
    {
        char* path = NULL;
        // pd->system->formatString(), not snprintf() - keeps this file
        // libc-light, relevant if this port is ever actually built for
        // real hardware (TOOLCHAIN=armgcc), which links a much smaller
        // libc than the simulator build does.
        pd->system->formatString( &path, "thumbnails/thumb_%02d", i );
        const char* err = NULL;
        LCDBitmap* bmp = pd->graphics->loadBitmap( path, &err );
        pd->system->realloc( path, 0 ); // frees - see pd_api_sys.h's own realloc() doc comment

        if( !bmp )
          break;

        gThumbnails[ i ] = bmp;
        gThumbnailCount = i + 1;
    }
}

static void menuInit()
{
    const char* err = NULL;
    // Roobert-10-Bold: a compact built-in system font (loaded by path, not
    // bundled with this game - every Playdate ships every /System/Fonts/*
    // font already, the same way src/sdl3/'s own biosFont.h/menuFont.h
    // reproduce actual font ASSETS as embedded data instead).
    gMenuFont = pd->graphics->loadFont( "/System/Fonts/Roobert-10-Bold.pft", &err );
    if( gMenuFont )
      gMenuRowHeight = pd->graphics->getFontHeight( gMenuFont ) + 4;

    pd->graphics->setFont( gMenuFont );

    menuBuildDisplayOrder();
    menuLoadThumbnails();
}

static void menuDrawCentered( const char* text, int y )
{
    size_t len = strlen( text );
    int w = pd->graphics->getTextWidth( gMenuFont, text, len, kASCIIEncoding, 0 );
    pd->graphics->drawText( text, len, kASCIIEncoding, ( 400 - w ) / 2, y );
}

// Runs one frame of the menu's own navigation+render (Up/Down move the
// selection, Left/Right jump a whole page, A launches it) and returns the
// chosen game's registration index (see menu.h's own note on
// menu_getGame()'s indexing) once A is pressed, or -1 otherwise - called
// from update() below exactly the way gameworld/menu.c's own menu_update()
// would be, just with an entirely different rendering/navigation
// implementation behind that same shape (see this file's own header
// comment for why gameworld/menu.c's own version isn't reused here).
static int menuUpdate()
{
    bool up    = md_inputUp();
    bool down  = md_inputDown();
    bool left  = md_inputLeft();
    bool right = md_inputRight();
    bool fire  = md_inputFire();

    int visibleRows = ( MENU_LIST_BOTTOM - MENU_LIST_TOP ) / gMenuRowHeight;
    if( visibleRows < 1 )
      visibleRows = 1;
    int totalPages = ( gameCount + visibleRows - 1 ) / visibleRows;

    if( down && !gPrevMenuDown )
    {
        gMenuSelection++;
        if( gMenuSelection >= gameCount )
          gMenuSelection = 0;
    }
    if( up && !gPrevMenuUp )
    {
        gMenuSelection--;
        if( gMenuSelection < 0 )
          gMenuSelection = gameCount - 1;
    }

    // LEFT/RIGHT jump a whole page at a time (wrapping past the last/first
    // page), same idea as gameworld/menu.c's own identical LEFT/RIGHT
    // handling - and why "fewer games per page" (this port's own header/
    // hint/page-indicator lines plus the thumbnail column leave less
    // vertical room than gameworld/menu.c's own 9-per-page BIOS-font menu)
    // still makes sense as a real paged model, not just a cosmetic choice.
    if( right && !gPrevMenuRight )
    {
        int currentPage = gMenuSelection / visibleRows;
        currentPage++;
        if( currentPage >= totalPages )
          currentPage = 0;
        gMenuSelection = currentPage * visibleRows;
        if( gMenuSelection >= gameCount )
          gMenuSelection = gameCount - 1;
    }
    if( left && !gPrevMenuLeft )
    {
        int currentPage = gMenuSelection / visibleRows;
        currentPage--;
        if( currentPage < 0 )
          currentPage = totalPages - 1;
        gMenuSelection = currentPage * visibleRows;
        if( gMenuSelection >= gameCount )
          gMenuSelection = gameCount - 1;
    }

    gPrevMenuUp = up;
    gPrevMenuDown = down;
    gPrevMenuLeft = left;
    gPrevMenuRight = right;

    pd->graphics->clear( kColorWhite );
    menuDrawCentered( "TINYJOYPAD FOR PLAYDATE", MENU_TITLE_Y );
    menuDrawCentered( "UP/DOWN SELECT  A PLAY", MENU_HINT_Y );

    int currentPage = gMenuSelection / visibleRows;

    if( totalPages > 1 )
    {
        char pageLabel[ 32 ];
        pageLabel[ 0 ] = 0;
        strcat( pageLabel, "LEFT/RIGHT PAGE " );
        appendInt( pageLabel, currentPage + 1 );
        strcat( pageLabel, "/" );
        appendInt( pageLabel, totalPages );
        menuDrawCentered( pageLabel, MENU_PAGE_Y );
    }

    int startIndex = currentPage * visibleRows;

    int y = MENU_LIST_TOP;
    for( int row = 0; row < visibleRows; row++ )
    {
        int pos = startIndex + row;
        if( pos >= gameCount )
          break;

        int gameIdx = gDisplayOrder[ pos ];

        char label[ 64 ];
        label[ 0 ] = 0;
        strcat( label, ( pos == gMenuSelection ) ? "> " : "  " );
        appendZeroPadded2( label, pos + 1 );
        strcat( label, ". " );
        strcat( label, menu_getGame( gameIdx )->title );

        pd->graphics->drawText( label, strlen( label ), kASCIIEncoding, MENU_LEFT_MARGIN, y );
        y += gMenuRowHeight;
    }

    // Real gameplay thumbnail + "BY <author>" of the currently-selected
    // game, matching gameworld/menu.c's own identical feature - switches
    // immediately whenever the selection moves, since it's just read
    // straight off gMenuSelection every frame. Indexed through
    // gDisplayOrder[] like the row label above - the thumbnail set is
    // keyed by registration index, not by alphabetical display position.
    // Centered vertically (as a group with the author line below it)
    // within the list area (MENU_LIST_TOP down to MENU_LIST_BOTTOM),
    // matching gameworld/menu.c's own identical blockY centering formula,
    // not top-aligned with the list.
    int selectedGameIdx = gDisplayOrder[ gMenuSelection ];
    if( selectedGameIdx < gThumbnailCount )
    {
        int blockHeight = MENU_THUMB_H + MENU_THUMB_AUTHOR_GAP + gMenuRowHeight;
        int blockY = MENU_LIST_TOP + ( ( MENU_LIST_BOTTOM - MENU_LIST_TOP ) - blockHeight ) / 2;

        pd->graphics->drawBitmap( gThumbnails[ selectedGameIdx ], MENU_THUMB_X, blockY, kBitmapUnflipped );

        char authorText[ 40 ];
        strcpy( authorText, "BY " );
        strcat( authorText, menu_getGame( selectedGameIdx )->author );
        int authorW = pd->graphics->getTextWidth( gMenuFont, authorText, strlen( authorText ), kASCIIEncoding, 0 );
        int authorX = MENU_THUMB_X + ( MENU_THUMB_W - authorW ) / 2;
        pd->graphics->drawText( authorText, strlen( authorText ), kASCIIEncoding, authorX, blockY + MENU_THUMB_H + MENU_THUMB_AUTHOR_GAP );
    }

    if( fire )
      return selectedGameIdx;

    return -1;
}

// =============================================================================
//   TOP-LEVEL DISPATCH
// =============================================================================

static int gCurrentGameIndex = -1;

// Playdate's own system menu (opened by the player's own physical Menu
// button during gameplay - not this port's own in-game menu, see
// menuUpdate() above) can hold up to 3 custom entries alongside the OS's
// own defaults. Both of this port's own entries only make sense while a
// game is actually running (a pixel-grid toggle affecting the game canvas;
// a shortcut back to this port's own game-select menu) - added once a
// game launches, removed once it doesn't, mirroring gInGame/gDialogShowing's
// own "only relevant during actual gameplay" scoping elsewhere in this file.
static PDMenuItem* gPixelGridMenuItem = NULL;

static void returnToMenu()
{
    md_stopTone();
    gCurrentGameIndex = -1;
    pd->system->removeAllMenuItems();
    gPixelGridMenuItem = NULL;
}

// Fires when the player toggles the checkmark from Playdate's own system
// menu - reads the value the OS already flipped back out, rather than
// tracking a second, independently-toggled copy of it here that could
// drift out of sync with what the checkmark itself is showing.
static void pixelGridMenuCallback( void* userdata )
{
    (void)userdata;
    gPixelGridEnabled = pd->system->getMenuItemValue( gPixelGridMenuItem ) != 0;

    // Same problem menu.h's own onResume hook already exists to solve (see
    // its own comment): a game that skips its own redraw on frames where
    // nothing changed (an isInvalid-style dirty-flag optimization) won't
    // naturally paint over the grid lines this toggle just added or removed
    // - without forcing one real redraw here, an OFF toggle would leave the
    // old grid lines "burned in" on screen indefinitely (nothing left to
    // draw over them), same failure mode the sibling Vircon32 port's own
    // menu-resume path forces a redraw for. Cheap enough (one extra full
    // redraw on a rare, player-initiated toggle) not to bother special-
    // casing ON vs OFF.
    if( gCurrentGameIndex != -1 )
    {
        GameFunc onResume = menu_getGame( gCurrentGameIndex )->onResume;
        if( onResume != NULL )
          onResume();
    }
}

static void menuMenuCallback( void* userdata )
{
    (void)userdata;
    returnToMenu();
}

static void addGameSystemMenuItems()
{
    pd->system->addMenuItem( "Menu", menuMenuCallback, NULL );
    // gPixelGridEnabled itself (not a hardcoded 0) as the initial
    // checkmark value - state persists across games/menu visits within
    // the same session (matching every SDL port's own glow/CRT/pixel-grid
    // toggle, which also never auto-resets), so re-entering a game after
    // switching it on once shows the checkmark already checked, not
    // silently reset back to its own default-off.
    gPixelGridMenuItem = pd->system->addCheckmarkMenuItem(
        "Pixel Grid", gPixelGridEnabled ? 1 : 0, pixelGridMenuCallback, NULL );
}

static int update( void* userdata )
{
    (void)userdata;

    PDButtons current, pushed, released;
    (void)pushed; (void)released;
    pd->system->getButtonState( &current, &pushed, &released );

    updateHeldCounter( &gLeftFrames,  ( current & kButtonLeft )  != 0 );
    updateHeldCounter( &gRightFrames, ( current & kButtonRight ) != 0 );
    updateHeldCounter( &gUpFrames,    ( current & kButtonUp )    != 0 );
    updateHeldCounter( &gDownFrames,  ( current & kButtonDown )  != 0 );
    updateHeldCounter( &gFireRawFrames, ( current & kButtonA )   != 0 );
    gButB = ( current & kButtonB ) != 0;

    md_setInGame( gCurrentGameIndex != -1 );

    if( gCurrentGameIndex == -1 )
    {
        int chosen = menuUpdate();
        if( chosen != -1 )
        {
            gCurrentGameIndex = chosen;
            md_armInputFireGate();

            // Same fix as every SDL port's own gamesMain.c (ported from
            // the sibling tinyjoypad_vircon32 project's own portVircon32.c,
            // commit b75fccf) - clear to black once, immediately on
            // selection and before the chosen game's own init() runs any
            // of its own code, so a game whose init() doesn't draw a full
            // frame right away doesn't leave the last menu frame sitting
            // on screen for that one gap tick instead of a clean black
            // transition.
            md_beginFrame();

            // Same call as every SDL port's own gamesMain.c - resolves/
            // loads this game's own persistent save slot (see this file's
            // own md_card*() implementation below, backed by a real file
            // in this game's sandboxed Data folder via pd->file) before
            // init() runs, since a game's own init() is what actually
            // calls eeprom_read_byte()/etc to load its saved high score.
            eepromSelectGame( menu_getGame( chosen )->title );

            menu_getGame( chosen )->init();
            addGameSystemMenuItems();
        }
    }
    else
    {
        // No quit-CONFIRMATION dialog on this port (see this file's own
        // header comment) - A+B+Up+Right held together returns to the menu
        // immediately, ported directly from the sibling cglpPlaydate.c's
        // own identical chorded gesture (its own update()'s isInMenu check)
        // rather than inventing a different one - an unlikely-to-happen-by-
        // accident combo none of these 33 games' own real controls use
        // simultaneously. Checked before this frame's game update() runs
        // (not after, unlike cglpPlaydate.c's own ordering), so the exit
        // frame doesn't render one extra frame of gameplay first. Same
        // destination as the system menu's own "Menu" item above - both
        // just call returnToMenu().
        if( ( current & kButtonA ) && ( current & kButtonB ) &&
            ( current & kButtonUp ) && ( current & kButtonRight ) )
        {
            returnToMenu();
        }
        else
        {
            menu_getGame( gCurrentGameIndex )->update();
            pixelGridEffectRender();
        }
    }

    md_updateAudio();
    obonoCoreShimUpdateSound();
    md_endFrame();

    return 1;
}

static void init()
{
    // Real Playdate hardware caps out at 50fps for full-screen refreshes (a
    // hardware/panel limit, not a tunable setting) - requesting
    // MD_FRAMES_PER_SECOND (60, gameworld's own shared "SDL ports run at
    // 60fps" constant, defined up in the AUDIO section alongside
    // PLAYDATE_REFRESH_RATE) here would either get silently clamped by the
    // OS or paced inconsistently, neither of which is what was actually
    // asked for. See md_updateAudio()'s own comment for how gFrameCounter
    // compensates for the resulting 50-vs-60 mismatch so obonoCoreShim.c's
    // own timing math (built around MD_FRAMES_PER_SECOND) still stays
    // correct in real elapsed time.
    pd->display->setRefreshRate( PLAYDATE_REFRESH_RATE );

    md_initVideo();
    md_initAudio();
    pixelGridEffectInit();

    addGames();
    menuInit();

    pd->system->setUpdateCallback( update, NULL );
}

#ifdef _WINDLL
__declspec( dllexport )
#endif
int eventHandler( PlaydateAPI* _pd, PDSystemEvent event, uint32_t arg )
{
    (void)arg;

    if( event == kEventInit )
    {
        pd = _pd;
        init();
    }

    return 0;
}
