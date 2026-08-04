#ifndef GAMES_MAIN_H
#define GAMES_MAIN_H

// -----------------------------------------------------------------------------
// The narrow surface main.c (the "SDL platform backend" side) needs to
// reach into the "game world" side. This header itself never includes
// SDL.h or avrCompat.h, so it's safe for main.c to include alongside
// SDL3/SDL.h.
// -----------------------------------------------------------------------------

// Registers every ported game (addGames()) and initializes the menu -
// call once, before the main loop starts.
void gamesMain_init();

// Runs one frame of the menu<->game top-level dispatch (quit-confirm
// dialog, fire-gate arming, menu selection, or the current game's own
// update()) - call once per real frame from main.c's own loop. Does NOT
// call md_updateAudio()/obonoCoreShimUpdateSound()/md_endFrame() itself -
// those stay in main.c, called unconditionally every frame regardless of
// what this function did (matches the sibling Vircon32 build's own main()
// shape).
void gamesMain_dispatchFrame();

// The obonoCoreShim note-sequencer's own once-per-frame poll - call once
// per real frame alongside md_updateAudio(), regardless of which game (if
// any) is running.
void obonoCoreShimUpdateSound();

// -----------------------------------------------------------------------------
// CLI-support surface (-list / -g <NAME> / -ms / .joy file handling) - main.c
// needs these to enumerate/launch games directly, bypassing the menu, without
// reaching into menu.c's own games[]/gameCount internals itself.
// -----------------------------------------------------------------------------

// How many games are registered (games[]/gameCount's own registration order,
// same order menu_getGame()/gamesMain_getGameTitle() index by - NOT the
// menu's alphabetized display order).
int gamesMain_getGameCount();

// Title of the game at registration index idx (as passed to addGame() in
// menuGameList.c - already all-uppercase there). NULL if idx is out of range.
char* gamesMain_getGameTitle( int idx );

// Case-insensitive exact-match lookup by title (matching cglpSDL3.c's own
// -g/.cgl-file title-matching convention) - returns the registration index,
// or -1 if no game has that title.
int gamesMain_findGameByTitle( char* title );

// Launches game idx directly, bypassing the menu entirely (same effect as
// picking it from the menu and pressing Fire) - used by -g/.joy-file direct
// launch and by -ms's batch screenshot mode. No-op if idx is out of range.
void gamesMain_launchGameDirect( int idx );

// Draws a small "fps: NN.NN" readout (black backing rect + white BIOS-font
// text) in the screen's top-left corner - call once per real frame, after
// gamesMain_dispatchFrame(), when -fps is active.
void gamesMain_drawFpsOverlay( float fps );

#endif
