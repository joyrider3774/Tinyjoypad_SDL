// -----------------------------------------------------------------------------
// Entry point + top-level frame loop. The actual menu<->game dispatch logic
// (quit-confirm dialog, fire-gate arming, menu selection) lives in
// gamesMain.c's own gamesMain_dispatchFrame() - modeled on the Vircon32
// build's own portVircon32.c main() - NOT copied from crisp-game-lib-
// portable-sdl's own cglpSDL3.c main()/update(), which is woven into
// crisp-game-lib's own Game/getGame()/highscore-persistence system this
// project has no equivalent of.
//
// This file DOES reuse cglpSDL3.c's own command-line-parameter surface and
// FPS-display concept directly (see printHelp() below for the exact flags
// kept, and which two - "-a"/"-nsd" - were deliberately dropped and why).
//
// md_endFrame() is called unconditionally at the bottom of every real
// frame, matching the Vircon32 build's own main() exactly - individual
// games/shims decide for themselves whether to actually call
// md_beginFrame() this frame (obonoCoreShim-lineage games skip it on
// frames where nothing changed, via refreshScreen()'s own isInvalid check;
// tinyJoypadShim-lineage games call it every frame unconditionally). See
// sdlBackend.c's own md_endFrame() for how it stays correct either way.
// -----------------------------------------------------------------------------

#if defined _WIN32 || defined __CYGWIN__
    #include <windows.h>
#endif

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "machineDependent.h"
#include "sdlBackend.h"
#include "gamesMain.h"

static void printHelp( char* exeName )
{
    printf( "Usage: %s [options] [file.joy]\n", exeName );
    printf( "  -w <WIDTH>     Window width (default %d)\n", 640 );
    printf( "  -h <HEIGHT>    Window height (default %d)\n", 360 );
    printf( "  -f             Start in fullscreen\n" );
    printf( "  -ns            No sound (skip audio init)\n" );
    printf( "  -fps           Show fps counter overlay\n" );
    printf( "  -nd            No delay (uncapped framerate, vsync off)\n" );
    printf( "  -s             Force software rendering (no GPU acceleration)\n" );
    printf( "  -list          List all game names, then exit\n" );
    printf( "  -g <NAME>      Launch a specific game directly by title\n" );
    printf( "  -ms            Batch-capture a screenshot of every game (./<TITLE>.bmp), then exit\n" );
    printf( "  -joy           Write a .joy stub file for every game, then exit\n" );
    printf( "  file.joy       Launch the game named by this file directly\n" );
    printf( "  -?/-help       Show this help\n" );
}

// Writes one plain-text ".joy" stub file per registered game (just the
// title, matching cglpSDL3.c's own ".cgl" stub format exactly) - lets an
// external ROM-list frontend have a per-game file to point at. Needs
// gamesMain_init() (so games[]/gameCount are populated) but no SDL/window
// at all, same as -list below.
static void writeJoyFiles()
{
    gamesMain_init();

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
    {
        char* title = gamesMain_getGameTitle( i );
        if( !title || strlen( title ) == 0 )
          continue;

        char filename[ 512 ];
        snprintf( filename, sizeof( filename ), "./%s.joy", title );

        FILE* f = fopen( filename, "w" );
        if( f )
        {
            fwrite( title, sizeof( char ), strlen( title ), f );
            fclose( f );
        }
    }
}

static void listGames()
{
    gamesMain_init();

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
      printf( "%d. %s\n", i + 1, gamesMain_getGameTitle( i ) );
}

// One frame of a screenshot script's simulated input - a Fire tap (edge:
// released/pressed/released across 3 calls, done by the caller) plus
// whichever direction(s) this game's script needs held throughout (most
// need none - see ScreenshotScript.holdUp below).
static void screenshotHeldDirectionFrame( bool holdUp )
{
    sdlBackend_simulateUpFrame( holdUp );
    gamesMain_dispatchFrame();
    md_updateAudio();
    obonoCoreShimUpdateSound();
    md_endFrame();
}

// The Vircon32 build's own menu-thumbnail capture pass (see CLAUDE.md's
// own "Menu game-select thumbnails" section) found that no single generic
// button sequence reaches real gameplay for every game - some go straight
// to gameplay off one Fire press, several need 2-3 presses through a
// logo/title/ready screen first, tuned per game by trial and error. This
// table is that same per-game tuning, done the same way (empirically,
// checking the actual resulting screenshot - see the per-title comments
// below for what each override was for), matched by title since that's
// stable across any future menu reordering (unlike a raw registration
// index). Default (title not listed) is 4 Fire taps ~1.5s apart, no held
// direction, no extra wait after the last tap - this alone was already
// enough for the large majority of games (confirmed by inspecting every
// one of the 33 resulting screenshots, not assumed).
typedef struct
{
    int  tapCount;
    int  gapFrames;     // real frames waited after each tap
    int  finalWaitFrames; // extra real frames waited after the last tap
    bool holdUp;        // held through every gap/final-wait frame
} ScreenshotScript;

#define SS_DEFAULT_TAPS  4
#define SS_DEFAULT_GAP   90

static ScreenshotScript screenshotScriptFor( char* title )
{
    ScreenshotScript s = { SS_DEFAULT_TAPS, SS_DEFAULT_GAP, 0, false };

    // TINY MINEZ needs exactly 2 taps to reach PLAYING (intro/rules ->
    // difficulty-select -> confirm), then genuinely no more: a 3rd tap
    // while PLAYING isn't a harmless no-op here, it's Fire-release, which
    // uncovers whatever cell the cursor (left at its untouched default
    // position, since this script sends no movement input) is sitting on
    // - if that happens to be a mine, it triggers BOOM_FLASH -> GAME_OVER,
    // and a further tap from there resets back to the title screen. This
    // was caught by direct user report (a "Tiny/Minez" title-logo-style
    // screen showing up in a run that had shown genuine minesweeper
    // gameplay moments earlier with the exact same script) - confirmed by
    // reading tmzState's own PLAYING branch, not assumed.
    if( SDL_strcmp( title, "TINY MINEZ" ) == 0 )
    {
        s.tapCount = 2;
        s.finalWaitFrames = 60;
    }
    // TINY PIPE's own title->playing transition is an unusually long
    // sequential state chain (fade in -> title -> fade out -> level-load
    // x2 -> level fade-in -> playing) - the default budget reliably lands
    // mid-chain (confirmed: showed a bare "LEVELS:01" transition screen)
    // rather than never leaving the title screen, so it just needs more
    // total wait, not different input.
    if( SDL_strcmp( title, "TINY PIPE" ) == 0 )
    {
        s.finalWaitFrames = 250;
    }
    // UFO's ship falls and dies within ~1-2 seconds unless up or down is
    // genuinely held the whole time it's airborne (matching a real
    // player) - without that, the default script's death+respawn-wait
    // cycle reliably lands back on the attract screen by the time the
    // screenshot is taken. Held only briefly (not the full default
    // budget): holding up CONTINUOUSLY still eventually flies into an
    // obstacle around frame ~70-80 (confirmed empirically), so this
    // captures shortly after liftoff instead, while still alive.
    else if( SDL_strcmp( title, "UFO" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 45;
        s.holdUp = true;
    }
    // DINO GAME needs precisely-timed jumps to survive at all - no
    // scripted input can reliably clear an obstacle, and holding up
    // continuously (auto-jump whenever grounded) still dies around
    // frame ~70-90 (confirmed empirically, likely jumping straight into
    // an obstacle rather than over it). Captured shortly after the run
    // begins instead, while it's still alive and running.
    else if( SDL_strcmp( title, "DINO GAME" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 40;
    }
    // FOUR IN A ROW's AI opponent picks moves via random Monte Carlo
    // rollouts (arand()-seeded) - the default 4-tap script risks
    // completing (and, since a finished match calls firoNewGame(),
    // resetting to an empty board) a whole match before the screenshot,
    // purely by chance (confirmed: reproduced both an empty board AND a
    // mid-game board from the exact same script parameters, different
    // runs). A single player move plus enough wait for the AI's own
    // reply (confirmed empirically to reliably show 2-3 marks placed)
    // keeps the total risk window much smaller.
    else if( SDL_strcmp( title, "FOUR IN A ROW" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 200;
    }
    // SNAKEGAME85 never reads Fire at all (see this game's own header
    // comment - all 4 directions double as "any button" to start/restart,
    // matching upstream's own checkButtonStateChange()), so the default
    // Fire-tap script never leaves the attract screen (confirmed: showed
    // the "PRESS ANY BUTTON" title screen). tapCount=0 skips the Fire-tap
    // loop entirely, leaving only the held-Up final wait - Up alone both
    // starts the game (attract screen) and then keeps the snake actually
    // moving during real gameplay.
    else if( SDL_strcmp( title, "SNAKEGAME85" ) == 0 )
    {
        s.tapCount = 0;
        s.finalWaitFrames = 90;
        s.holdUp = true;
    }
    // BREAKOUT's paddle only reads Left/Right (no Fire-based movement,
    // and this batch-screenshot script has no held-Left/Right capability
    // the way it does for Up) - with the paddle left stationary, the
    // default script's longer capture window reliably outlives the ball
    // falling straight past it (confirmed: showed "GAME OVER SCORE: 1").
    // One tap to leave ATTRACT, then a short wait - just long enough to
    // render a real PLAYING frame with blocks/paddle/ball all visible,
    // short enough the stationary paddle hasn't been missed yet.
    else if( SDL_strcmp( title, "BREAKOUT" ) == 0 )
    {
        s.tapCount = 4;
        s.gapFrames = 15;
        s.finalWaitFrames = 10;
    }
    // BAT BONANZA's own left paddle only reads Up/Down (or Left/Right) -
    // this script sends neither, so it sits fixed wherever it clamps to
    // once PLAYING starts. The default script's own long total wait
    // (4 taps * 90-frame gaps, no cap on PLAYING time) reliably outlives
    // a real point being scored, landing on PONG_STATE_ROUND_FLASH
    // instead - which only blinks the two score digits, no bats/ball at
    // all (confirmed: that's exactly what the shipped screenshot/
    // thumbnail show). PONG_STATE_COUNTDOWN itself takes a fixed 180 real
    // frames before PLAYING even starts - a 60-frame wait BEFORE the
    // first decrement, then 60 more for each of "3"->"2"->"1" (a first
    // attempt here budgeted only 120, missing that leading wait, and
    // landed on "GET READY -- 1" instead of real gameplay). One tap to
    // leave ATTRACT, then just enough wait to clear that countdown plus a
    // small buffer, short enough the stuck paddle hasn't missed a real
    // point yet.
    else if( SDL_strcmp( title, "BAT BONANZA" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 190;
    }
    // BLOCKS GOLD's own attract->playing transition isn't a plain fire-tap
    // edge: a single tap starts a MUSICWAIT state that plays a full title
    // jingle (visually identical to the attract screen the whole time -
    // same gldRenderFrame( GLD_MODE_ATTRACT ) call) before gldBeginGame()
    // ever actually starts PLAYING. gldGhostEnabled defaults true, which
    // selects the LONGER of the two jingle variants (GLD_MUSIC_FULL_COUNT,
    // ~7.8s per that file's own comment =~ 468 real frames) - the default
    // script's total budget (4 taps * 90-frame gaps =~ 368 frames) doesn't
    // reach that, so it reliably captured mid-jingle (confirmed: showed the
    // plain "BLOCKS GOLD" title card, no board/HUD at all). One tap plus a
    // wait comfortably past 468 frames instead.
    else if( SDL_strcmp( title, "BLOCKS GOLD" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 550;
    }
    // METEOR STORM's ship has no scripted way to dodge (this script can
    // only hold UP, but this game's own "rise" input is FIRE, not Up) - and
    // the real, faster death here isn't even an obstacle: with Fire never
    // pressed, metrCheckMovement()'s own acceleration ramps every movement
    // tick (every METR_TICK_DIVISOR==2 real frames) up to a capped +5,
    // pushing the ship down from its METR_PLAYER_INITIAL_Y==40 spawn past
    // the METR_HEIGHT==64 floor in exactly 13 movement ticks - 26 real
    // frames - regardless of any obstacle. A first attempt here (50-frame
    // wait) still reliably outlived that and landed on the post-collision
    // METR_MODE_FLASH render (confirmed: an inverted, almost-all-white
    // frame, not the attract screen as first assumed). Shortened well
    // under that 26-frame hard ceiling instead, same "capture shortly
    // after the run begins" shape as DINO GAME above.
    else if( SDL_strcmp( title, "METEOR STORM" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 5;
        s.finalWaitFrames = 15;
    }
    // FLAPPY BIRD's bird only moves row when Up/Down is pressed - this
    // script's own held-Up support is edge-triggered (one step, not
    // continuous), so the bird effectively sits at its fixed spawn row
    // (row 0) the entire capture. The default script's long total wait
    // reliably outlives the first wall reaching column 0 with a
    // misaligned gap, ending in GAME OVER (confirmed: showed the skull/
    // "Game Over" screen). Walls don't even spawn until ~72 real frames in
    // (4 movement ticks * 18 frames/tick) and the first spawned wall can't
    // reach column 0 for a further ~270 frames after that - a shorter wait
    // lands safely inside that window with a real wall already visible
    // on-screen, well before any collision is possible.
    else if( SDL_strcmp( title, "FLAPPY BIRD" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 180;
    }
    // PIPE BIRD wraps its ENTIRE update() (including the ATTRACT branch's
    // own Fire check) in a PIPB_TICK_DIVISOR==2 real-frame skip, so
    // isFirePressed() - and therefore md_inputFireFrames()'s own fire-gate
    // disarm check (see machineDependent.h's md_armInputFireGate()) - is
    // only ever called on every OTHER real frame. A single tap's "true"
    // pulse (this script's 2nd dispatch, an even/processed frame) lands
    // BEFORE the gate has had any processed "released" frame to disarm
    // against yet (the 1st dispatch, an odd/skipped frame, never called
    // isFirePressed() at all) - so that first pulse is silently swallowed
    // by the still-armed gate, and with only one tap there's no second
    // pulse left to actually start PLAYING (confirmed: stayed on the
    // attract screen the entire capture, even after a generous wait).
    // Two taps fixes it: the gate reliably disarms during the gap between
    // them (plenty of processed released-frame reads by then), so the
    // second tap's pulse is read for real. Once PLAYING, the same
    // edge-triggered-single-flap/gravity budget as before applies (~54
    // real frames alive with the one starting flap this script's holdUp
    // gives, traced through the fixed-point gravity/velocity math) - kept
    // short and well inside that window, same "capture shortly after the
    // run begins" shape as DINO GAME/METEOR STORM above.
    else if( SDL_strcmp( title, "PIPE BIRD" ) == 0 )
    {
        s.tapCount = 2;
        s.gapFrames = 10;
        s.finalWaitFrames = 25;
        s.holdUp = true;
    }
    // ATTINY SNAKE always starts heading straight DOWN from the grid's own
    // (0,0) corner and this script sends no turn input at all, so the head
    // just wraps around the same 8-row column forever - not a bug, but the
    // default script's own total frame budget (~365 real frames after the
    // single tap that starts PLAYING) divided by this game's own 15-frame
    // movement tick lands on exactly 24 moves, and 24 is an exact multiple
    // of ASNK_ROWS==8 - the head wraps back to EXACTLY its start position,
    // making a real, moving game look frozen at the attract-screen's own
    // spawn cell (confirmed: showed just the single starting cell, no
    // visible movement). Fixed by landing on a real frame count that isn't
    // a multiple of 8*15==120 real frames, so the head is visibly
    // elsewhere on its column when captured.
    else if( SDL_strcmp( title, "ATTINY SNAKE" ) == 0 )
    {
        s.tapCount = 1;
        s.gapFrames = 10;
        s.finalWaitFrames = 100;
    }

    return s;
}

// Runs each game's own screenshotScriptFor() tap sequence, then saves the
// resulting screen as a BMP - via sdlBackend_simulateFireFrame()/
// simulateUpFrame() instead of a real device press. Needs a real window/
// renderer already initialized (gScreen only exists after md_initVideo()),
// unlike -list/-joy above.
static void runBatchScreenshots()
{
    // Menu screenshot first, before any game's own launch overwrites
    // currentGameIndex (gamesMain_init() already left it at -1, showing
    // the menu's own default first-page/first-selection state) - matches
    // the sibling tinyjoypad_vircon32 project's own metadata/menu.png,
    // used at the top of its own README.md. A few frames' worth of
    // buffer (not strictly required - menu_update() already draws on its
    // very first call) rather than exactly one, just so this isn't the
    // single most timing-sensitive capture in the whole batch.
    for( int j = 0; j < 5; j++ )
      screenshotHeldDirectionFrame( false );
    sdlBackend_saveScreenshot( "./menu.bmp" );
    printf( "Captured ./menu.bmp\n" );

    for( int i = 0; i < gamesMain_getGameCount(); i++ )
    {
        gamesMain_launchGameDirect( i );

        char* title = gamesMain_getGameTitle( i );
        ScreenshotScript script = screenshotScriptFor( title ? title : "" );

        for( int tap = 0; tap < script.tapCount; tap++ )
        {
            sdlBackend_simulateFireFrame( false );
            screenshotHeldDirectionFrame( script.holdUp );
            sdlBackend_simulateFireFrame( true );
            screenshotHeldDirectionFrame( script.holdUp );
            sdlBackend_simulateFireFrame( false );

            for( int j = 0; j < script.gapFrames; j++ )
              screenshotHeldDirectionFrame( script.holdUp );
        }

        for( int j = 0; j < script.finalWaitFrames; j++ )
          screenshotHeldDirectionFrame( script.holdUp );

        char filename[ 512 ];
        snprintf( filename, sizeof( filename ), "./%s.bmp", title ? title : "game" );
        sdlBackend_saveScreenshot( filename );
        printf( "Captured %s\n", filename );
    }
}

int main( int argc, char** argv )
{
    // Attach to a potential console when launched with -mwindows so output
    // is visible in a cmd/msys prompt, but stays invisible when launched
    // from Explorer/a shortcut. Unlike cglpSDL3.c's own identical-looking
    // block, this one first checks GetStdHandle(STD_OUTPUT_HANDLE) - a
    // -mwindows GUI-subsystem process genuinely has no stdout handle at
    // all when launched with no redirection (that's the whole reason
    // AttachConsole+freopen("CON",...) is needed to make output visible in
    // the launching shell) - but when the shell DID explicitly redirect
    // stdout (`> file`, a pipe), STD_OUTPUT_HANDLE is already a real,
    // valid handle to that file/pipe regardless of subsystem type, and
    // unconditionally freopen("CON",...)-ing over it (as cglpSDL3.c's own
    // block does) silently discards that redirection - confirmed directly:
    // `-list > out.txt` produced an empty file until this check was added.
#if defined _WIN32 || defined __CYGWIN__
    if( GetStdHandle( STD_OUTPUT_HANDLE ) == NULL && AttachConsole( (DWORD)-1 ) )
    {
        freopen( "CON", "w", stderr );
        freopen( "CON", "w", stdout );
    }
#endif

    bool fullscreen  = false;
    bool noAudioInit = false;
    bool showFps     = false;
    bool noDelay     = false;
    bool makeScreenshots = false;
    bool softwareRendering = false;
    int  windowWidth  = 0; // 0 = keep sdlBackend's own default
    int  windowHeight = 0;
    char startGameTitle[ 100 ] = { 0 };

    // First pass: detect a positional ".joy" file argument (its filename,
    // minus path and extension, IS the game title to launch - matching
    // cglpSDL3.c's own ".cgl" filename-based, not-file-content-based,
    // convention) and every other flag.
    for( int i = 1; i < argc; i++ )
    {
        char* ext = strrchr( argv[ i ], '.' );
        if( ext != NULL && SDL_strcasecmp( ext, ".joy" ) == 0 )
        {
            // Last path separator of either flavor - scanned by hand
            // (rather than comparing two strrchr() results, one of which
            // may be NULL, with a relational operator) since a mixed-
            // separator path is possible on Windows.
            char* nameStart = argv[ i ];
            for( char* p = argv[ i ]; p < ext; p++ )
              if( *p == '/' || *p == '\\' )
                nameStart = p + 1;

            memset( startGameTitle, 0, sizeof( startGameTitle ) );
            size_t len = (size_t)( ext - nameStart );
            if( len >= sizeof( startGameTitle ) )
              len = sizeof( startGameTitle ) - 1;
            memcpy( startGameTitle, nameStart, len );
        }

        if( SDL_strcmp( argv[ i ], "-joy" ) == 0 )
        {
            writeJoyFiles();
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-list" ) == 0 )
        {
            listGames();
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-?" ) == 0 || SDL_strcmp( argv[ i ], "--?" ) == 0 ||
            SDL_strcmp( argv[ i ], "/?" ) == 0 || SDL_strcmp( argv[ i ], "-help" ) == 0 ||
            SDL_strcmp( argv[ i ], "--help" ) == 0 )
        {
            printHelp( argv[ 0 ] );
            return 0;
        }

        if( SDL_strcmp( argv[ i ], "-f" ) == 0 )
          fullscreen = true;

        if( SDL_strcmp( argv[ i ], "-ns" ) == 0 )
          noAudioInit = true;

        if( SDL_strcmp( argv[ i ], "-fps" ) == 0 )
          showFps = true;

        if( SDL_strcmp( argv[ i ], "-nd" ) == 0 )
          noDelay = true;

        if( SDL_strcmp( argv[ i ], "-s" ) == 0 )
          softwareRendering = true;

        if( SDL_strcmp( argv[ i ], "-ms" ) == 0 )
          makeScreenshots = true;

        if( SDL_strcmp( argv[ i ], "-w" ) == 0 && i + 1 < argc )
          windowWidth = SDL_atoi( argv[ i + 1 ] );

        if( SDL_strcmp( argv[ i ], "-h" ) == 0 && i + 1 < argc )
          windowHeight = SDL_atoi( argv[ i + 1 ] );

        if( SDL_strcmp( argv[ i ], "-g" ) == 0 && i + 1 < argc )
        {
            memset( startGameTitle, 0, sizeof( startGameTitle ) );
            strncpy( startGameTitle, argv[ i + 1 ], sizeof( startGameTitle ) - 1 );
        }
    }

    // "-nsd" (disable scaled drawing) from cglpSDL3.c's own flag set is
    // deliberately not carried over here: there is no glow/CRT scaled-
    // drawing pipeline built the way cglp's own is for "-nsd" to disable.
    // cglp's own "-a" (force hardware-accelerated) has no equivalent
    // either, but for the opposite reason now - hardware-accelerated is
    // already this backend's own default (SDL auto-picks its best-
    // available driver), so there's nothing "-a" would need to force;
    // "-s" below covers the one real use this project actually has for a
    // renderer-choice flag, forcing the *software* renderer instead.

    sdlBackend_setWindowSize( windowWidth, windowHeight );
    sdlBackend_setFullscreen( fullscreen );
    sdlBackend_setVsync( !noDelay );
    sdlBackend_setSoftwareRendering( softwareRendering );

    if( !sdlBackend_init( argc, argv ) )
    {
        SDL_Log( "sdlBackend_init failed, aborting.\n" );
        return 1;
    }

    md_initVideo();

    if( !noAudioInit )
      md_initAudio();

    gamesMain_init();

    if( makeScreenshots )
    {
        runBatchScreenshots();
        sdlBackend_shutdown();
        return 0;
    }

    if( startGameTitle[ 0 ] != 0 )
    {
        int idx = gamesMain_findGameByTitle( startGameTitle );
        if( idx != -1 )
        {
            gamesMain_launchGameDirect( idx );
            gamesMain_setLaunchedDirectly( true );
        }
        else
          SDL_Log( "No game titled '%s' found - showing the menu instead.\n", startGameTitle );
    }

    Uint64 perfFreq       = SDL_GetPerformanceFrequency();
    Uint64 fpsWindowStart = SDL_GetPerformanceCounter();
    int    fpsFrameCount  = 0;
    float  avgFps         = 0.0f;

    // showFps never toggles at runtime (no keybind for it, unlike glow/CRT/
    // pixel-grid's own G cycle - see machineDependent.h's own
    // md_setFpsOverlayShowing() comment), so this one-time "off" call is
    // all that's needed to keep the platform side's own effect-exemption
    // state correct for the whole process lifetime when "-fps" wasn't
    // passed at all.
    if( !showFps )
      md_setFpsOverlayShowing( false, 0, 0 );

    while( !sdlBackend_shouldQuit() )
    {
        sdlBackend_pollEvents();

        if( sdlBackend_shouldQuit() )
          break;

        gamesMain_dispatchFrame();

        md_updateAudio();
        obonoCoreShimUpdateSound();

        if( showFps )
        {
            fpsFrameCount++;
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed = (double)( now - fpsWindowStart ) / (double)perfFreq;
            if( elapsed >= 1.0 )
            {
                avgFps = (float)( (double)fpsFrameCount / elapsed );
                fpsFrameCount = 0;
                fpsWindowStart = now;
            }
            gamesMain_drawFpsOverlay( avgFps );
        }

        md_endFrame();
    }

    md_stopTone();
    sdlBackend_shutdown();

    return 0;
}
