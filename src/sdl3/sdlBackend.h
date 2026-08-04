#ifndef SDL_BACKEND_H
#define SDL_BACKEND_H

// -----------------------------------------------------------------------------
// Platform-only extras that main.c needs beyond machineDependent.h's own
// contract - main.c and sdlBackend.c are both part of the "SDL platform
// backend" half of this project (see machineDependent.h's own header
// comment on the TU split), so both are free to #include SDL.h directly and
// share declarations here. The "game world" side (avrCompat.h, the shims,
// every games/*.c file, menu.c) never sees this header.
//
// machineDependent.h itself has no concept of "pump OS events" or "should
// the app quit" - Vircon32 never needed either (no real OS window to close,
// the emulator/BIOS owns that), so these are genuinely new, SDL-specific
// additions rather than a port of anything.
// -----------------------------------------------------------------------------

#include <stdbool.h>

// Config setters - call any of these BEFORE sdlBackend_init() to override
// its defaults (matching cglpSDL3.c's own CLI-parsed-then-apply ordering).
// sdlBackend_init() itself still takes argc/argv (used only for logging),
// so CLI parsing/ownership stays in main.c rather than duplicated here.
void sdlBackend_setWindowSize( int width, int height );
void sdlBackend_setFullscreen( bool fullscreen );
// Vsync-locked pacing (the default) vs uncapped ("-nd", run as fast as
// possible) - matches cglpSDL3.c's own nodelay flag.
void sdlBackend_setVsync( bool enabled );

// Creates the window/renderer/framebuffer surface, opens a gamepad (if any)
// via CInput, and initializes audio. Returns false if SDL init failed.
bool sdlBackend_init( int argc, char** argv );

void sdlBackend_shutdown();

// Pumps SDL's OS event queue and refreshes CInput's button state - call
// once per real frame, before reading any md_input*() function.
void sdlBackend_pollEvents();

// True once the window has been closed / quit requested (F4, window X).
bool sdlBackend_shouldQuit();

// Saves the current screen contents (post md_endFrame()'s own gScreen
// state) as a BMP - used by -ms's batch screenshot mode. Returns false on
// failure (e.g. video not initialized yet).
bool sdlBackend_saveScreenshot( const char* path );

// Feeds one simulated frame of the Fire button's raw held-frame counter,
// bypassing real device polling entirely - used only by -ms's batch
// screenshot mode to script a "tap Fire once" gesture per game the same
// way cglpSDL3.c's own makescreenshots pass does (one frame released, one
// frame pressed, one frame released again, then a plain run of idle
// frames) without needing a real keyboard/gamepad press.
void sdlBackend_simulateFireFrame( bool pressed );

// Same idea as sdlBackend_simulateFireFrame(), for Up - some games need a
// direction genuinely HELD across many frames to reach a real "still
// alive, actively playing" state for -ms (e.g. UFO's ship falls and dies
// within a couple of seconds unless up/down is held the whole time, same
// as a real player would) rather than a single tap.
void sdlBackend_simulateUpFrame( bool held );

#endif
