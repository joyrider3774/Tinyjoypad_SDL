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

    return s;
}

// Runs each game's own screenshotScriptFor() tap sequence, then saves the
// resulting screen as a BMP - via sdlBackend_simulateFireFrame()/
// simulateUpFrame() instead of a real device press. Needs a real window/
// renderer already initialized (gScreen only exists after md_initVideo()),
// unlike -list/-joy above.
static void runBatchScreenshots()
{
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
