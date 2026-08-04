#include "menu.h"
#include "menuGameList.h"
#include "machineDependent.h"
#include "obonoCoreShim.h"
#include "games/games.h"

// Only the 3 games ported so far (Phase 2's proof-of-concept trio) are
// registered here - the remaining ~30 from the sibling tinyjoypad_vircon32
// build get appended as they're ported in Phase 4 (see that project's own
// menuGameList.c for the full list/credits/onResume-audit commentary this
// will eventually mirror).
void addGames()
{
    addGame( "NUMBERPLACE", "OBONO", &gameNumberPlace_init, &gameNumberPlace_update, &obonoCoreShimForceRedraw );
    // Credited to both names: the .ino's own header says "Programmer:
    // Daniel C 2018-2020, Enhancements: Sven B 2021" for this specific
    // v4.2 release.
    addGame( "TINY INVADERS", "DANIEL C / SVEN B", &gameTinyInvaders_init, &gameTinyInvaders_update, NULL );
    // Tiny DDug: while waiting for the attract screen's confirm press to be
    // released, there is no timer at all and no redraw happens - a genuine
    // indefinite risk, so onResume is wired (matches the Vircon32 build's
    // own reasoning).
    addGame( "TINY DDUG", "DANIEL C", &gameTinyDDug_init, &gameTinyDDug_update, &gameTinyDDug_forceRedraw );
    addGame( "2048", "OBONO", &gameT2048_init, &gameT2048_update, &obonoCoreShimForceRedraw );
    addGame( "HOLLOWSEEKER", "OBONO", &gameHollowSeeker_init, &gameHollowSeeker_update, &obonoCoreShimForceRedraw );
    // Four in a Row: calls its own render function unconditionally at the
    // end of every branch (no dirty-flag/skip-redraw path), so NULL here
    // is correct, not an oversight.
    addGame( "FOUR IN A ROW", "UNKNOWN", &gameFourInRow_init, &gameFourInRow_update, NULL );
    addGame( "STACKER", "ANDY JACKSON", &gameStacker_init, &gameStacker_update, &gameStacker_forceRedraw );
    addGame( "OROBOROS", "ILYA TITOV", &gameOroboros_init, &gameOroboros_update, &gameOroboros_forceRedraw );
    // Dino Game: calls its own render function unconditionally regardless
    // of state, so NULL here is correct, not an oversight.
    addGame( "DINO GAME", "TINY HANDHELD", &gameDinoGame_init, &gameDinoGame_update, NULL );
    addGame( "RUN DUDE RUN", "ILYA TITOV", &gameRunDudeRun_init, &gameRunDudeRun_update, &gameRunDudeRun_forceRedraw );
    // Pong (menu title "BAT BONANZA", not "PONG"): the .ino's own header
    // credits this as "Pong game by Andy Jackson", but the game's own
    // title screen literally spells out "BAT"/"BONANZA" on-screen -
    // matching what a player actually sees takes priority over the
    // header's attribution comment, which stays as its own credit below.
    addGame( "BAT BONANZA", "ANDY JACKSON", &gamePong_init, &gamePong_update, &gamePong_forceRedraw );
    addGame( "UFO", "ILYA TITOV", &gameUFO_init, &gameUFO_update, &gameUFO_forceRedraw );
    addGame( "WREN ROLLERCOASTER", "ANDY JACKSON", &gameWrenRollercoaster_init, &gameWrenRollercoaster_update, &gameWrenRollercoaster_forceRedraw );
    addGame( "TINY PINBALL", "DANIEL C", &gameTinyPinball_init, &gameTinyPinball_update, NULL );
    addGame( "TINY GILBERT", "DANIEL C", &gameTinyGilbert_init, &gameTinyGilbert_update, &gameTinyGilbert_forceRedraw );
    addGame( "TINY ARKANOID", "DANIEL C", &gameTinyArkanoid_init, &gameTinyArkanoid_update, NULL );
    addGame( "TINY ARENA", "DANIEL C", &gameTinyArena_init, &gameTinyArena_update, &gameTinyArena_forceRedraw );
    addGame( "TINY LANDER", "ROGER BUEHLER", &gameTinyLander_init, &gameTinyLander_update, &gameTinyLander_forceRedraw );
    addGame( "TINY PACMAN", "DANIEL C", &gameTinyPacman_init, &gameTinyPacman_update, NULL );
    addGame( "TINY BOMBER", "DANIEL C", &gameTinyBomber_init, &gameTinyBomber_update, NULL );
    // Credited to both names: the .ino's own header says "Programmer: Sven
    // B" with contact email Lorandil@gmx.de - the same contact address
    // Tiny Invaders' own header uses for its "Sven B"-credited enhancements.
    addGame( "TINY MINEZ", "SVEN B / LORANDIL", &gameTinyMinez_init, &gameTinyMinez_update, NULL );
    addGame( "FROGGER", "ANDY JACKSON", &gameFrogger_init, &gameFrogger_update, &gameFrogger_forceRedraw );
    addGame( "TINY BERT", "DANIEL C", &gameTinyBert_init, &gameTinyBert_update, NULL );
    addGame( "TINY MORPION", "DANIEL C", &gameTinyMorpion_init, &gameTinyMorpion_update, &gameTinyMorpion_forceRedraw );
    addGame( "TINY BIKE", "DANIEL C", &gameTinyBike_init, &gameTinyBike_update, &gameTinyBike_forceRedraw );
    addGame( "TINY TRIS", "DANIEL C", &gameTinyTris_init, &gameTinyTris_update, &gameTinyTris_forceRedraw );
    addGame( "TINY TRICK", "DANIEL C", &gameTinyTrick_init, &gameTinyTrick_update, &gameTinyTrick_forceRedraw );
    addGame( "TINY PIPE", "DANIEL C", &gameTinyPipe_init, &gameTinyPipe_update, &gameTinyPipe_forceRedraw );
    addGame( "TINY DOC", "DANIEL C", &gameTinyDoc_init, &gameTinyDoc_update, NULL );
    addGame( "TINY MISSILE", "DANIEL C", &gameTinyMissile_init, &gameTinyMissile_update, NULL );
    addGame( "TINY SQUEST", "DANIEL C", &gameTinySQuest_init, &gameTinySQuest_update, &gameTinySQuest_forceRedraw );
    addGame( "TINY PLAQUE", "DANIEL C", &gameTinyPlaque_init, &gameTinyPlaque_update, &gameTinyPlaque_forceRedraw );
    // Tiny Dungeon: every state in its own update() calls its render
    // function unconditionally at the end (no dirty-flag skipping
    // anywhere in this port), so NULL here is correct, not an oversight.
    addGame( "TINY DUNGEON", "SVEN B / LORANDIL", &gameTinyDungeon_init, &gameTinyDungeon_update, NULL );
    // SnakeGame85: gameSnakeGame85_update() calls snkRenderImage()
    // unconditionally at the end of every single state branch (no dirty-
    // flag/skip-redraw path anywhere in this port), so NULL here is
    // confirmed correct, not an oversight.
    addGame( "SNAKEGAME85", "TEREZAZA", &gameSnakeGame85_init, &gameSnakeGame85_update, NULL );
    // Jump Slime: gameJumpSlime_update() calls jslmRender() unconditionally
    // at the end of every single state branch (no dirty-flag/skip-redraw
    // path anywhere in this port), so NULL here is confirmed correct, not
    // an oversight.
    addGame( "JUMP SLIME", "KONDOLAB", &gameJumpSlime_init, &gameJumpSlime_update, NULL );
    // TinyRoG: gameTinyRoG_update() calls trogRenderStage()/trogRenderCave()
    // unconditionally at the end of every single state branch (no dirty-
    // flag/skip-redraw path anywhere in this port), so NULL here is
    // confirmed correct, not an oversight.
    addGame( "TINYROG", "KONDOLAB", &gameTinyRoG_init, &gameTinyRoG_update, NULL );
    // TinY Fi: gameTinYFi_update() calls tfiRender() unconditionally at
    // the end of every single state branch (no dirty-flag/skip-redraw
    // path anywhere in this port), so NULL here is confirmed correct, not
    // an oversight.
    addGame( "TINY FI", "KONDOLAB", &gameTinYFi_init, &gameTinYFi_update, NULL );
    addGame( "BREAKOUT", "ILYA TITOV", &gameBreakout_init, &gameBreakout_update, &gameBreakout_forceRedraw );
    addGame( "SPACE ATTACK", "ANDY JACKSON", &gameSpaceAttack_init, &gameSpaceAttack_update, &gameSpaceAttack_forceRedraw );
    // Menu title deliberately avoids the trademarked falling-block puzzle
    // genre name this game is a clone of - the game's own attract screen
    // originally spelled it out via a plain font-rendered string (not
    // baked bitmap data), so that string was also changed in the source
    // (see gameFallingBlocks.c) rather than left as shipped.
    addGame( "FALLING BLOCKS", "ANDY JACKSON", &gameFallingBlocks_init, &gameFallingBlocks_update, &gameFallingBlocks_forceRedraw );
}
