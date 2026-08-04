#ifndef GAMES_H
#define GAMES_H

// -----------------------------------------------------------------------------
// One shared header for every ported game's own tiny public surface (just
// `_init`/`_update`, plus `_forceRedraw` for the games that have their own -
// see menu.h's own Game struct) - deliberately ONE file for all 33 games,
// not a header per game: each games/gameXxx.c is otherwise a fully self-
// contained translation unit (see CLAUDE.md's "Translation-unit boundary"
// section) with no cross-game symbol sharing, so there is nothing else here
// for a per-game header to usefully declare. The only consumer is
// menuGameList.c's own addGames(), which needs every one of these visible
// before it can take their address.
//
// Declared exactly the way this whole codebase already declares every
// other zero-argument function (bare `()`, not `(void)` - see
// gamesMain.h/menu.h/etc) for consistency, not because it matters here.
// -----------------------------------------------------------------------------

void gameNumberPlace_init();
void gameNumberPlace_update();

void gameTinyInvaders_init();
void gameTinyInvaders_update();

void gameTinyDDug_init();
void gameTinyDDug_update();
void gameTinyDDug_forceRedraw();

void gameT2048_init();
void gameT2048_update();

void gameHollowSeeker_init();
void gameHollowSeeker_update();

void gameFourInRow_init();
void gameFourInRow_update();

void gameStacker_init();
void gameStacker_update();
void gameStacker_forceRedraw();

void gameOroboros_init();
void gameOroboros_update();
void gameOroboros_forceRedraw();

void gameDinoGame_init();
void gameDinoGame_update();

void gameRunDudeRun_init();
void gameRunDudeRun_update();
void gameRunDudeRun_forceRedraw();

void gamePong_init();
void gamePong_update();
void gamePong_forceRedraw();

void gameUFO_init();
void gameUFO_update();
void gameUFO_forceRedraw();

void gameWrenRollercoaster_init();
void gameWrenRollercoaster_update();
void gameWrenRollercoaster_forceRedraw();

void gameTinyPinball_init();
void gameTinyPinball_update();

void gameTinyGilbert_init();
void gameTinyGilbert_update();
void gameTinyGilbert_forceRedraw();

void gameTinyArkanoid_init();
void gameTinyArkanoid_update();

void gameTinyArena_init();
void gameTinyArena_update();
void gameTinyArena_forceRedraw();

void gameTinyLander_init();
void gameTinyLander_update();
void gameTinyLander_forceRedraw();

void gameTinyPacman_init();
void gameTinyPacman_update();

void gameTinyBomber_init();
void gameTinyBomber_update();

void gameTinyMinez_init();
void gameTinyMinez_update();

void gameFrogger_init();
void gameFrogger_update();
void gameFrogger_forceRedraw();

void gameTinyBert_init();
void gameTinyBert_update();

void gameTinyMorpion_init();
void gameTinyMorpion_update();
void gameTinyMorpion_forceRedraw();

void gameTinyBike_init();
void gameTinyBike_update();
void gameTinyBike_forceRedraw();

void gameTinyTris_init();
void gameTinyTris_update();
void gameTinyTris_forceRedraw();

void gameTinyTrick_init();
void gameTinyTrick_update();
void gameTinyTrick_forceRedraw();

void gameTinyPipe_init();
void gameTinyPipe_update();
void gameTinyPipe_forceRedraw();

void gameTinyDoc_init();
void gameTinyDoc_update();

void gameTinyMissile_init();
void gameTinyMissile_update();

void gameTinySQuest_init();
void gameTinySQuest_update();
void gameTinySQuest_forceRedraw();

void gameTinyPlaque_init();
void gameTinyPlaque_update();
void gameTinyPlaque_forceRedraw();

void gameTinyDungeon_init();
void gameTinyDungeon_update();

#endif
