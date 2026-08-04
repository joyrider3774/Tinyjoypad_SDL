#ifndef MENU_H
#define MENU_H

// Game-select menu. Deliberately simpler than crisp-game-lib-portable's own
// menu.c/cglp.c pair (no custom glyph renderer, no per-character collision
// hit-testing) - this project draws menu text with the real Vircon32 BIOS
// font instead (see biosFont.h), matching what the sibling tinyjoypad_vircon32
// build's own menu did via Vircon32's built-in print_at().
//
// Ported from that build's own menu.h - two dialect fixes:
//  - GameFunc is a real standard-C function-pointer typedef
//    (`typedef void (*GameFunc)( void );`) - Vircon32's own dialect instead
//    writes this as `typedef void(void) GameFunc;` with the `*` added back
//    at each *use* site (`GameFunc* init`); here the `*` lives in the
//    typedef itself, so a use site just declares `GameFunc init` with no
//    extra star (see obonoCoreShim.h's own DrawFunc for the same pattern).
//  - `title`/`author` are `char*` here, not `int*` - Vircon32 strings are
//    `int[]` (one word per character), standard C strings are `char[]`.

typedef void (*GameFunc)( void );

typedef struct
{
    char* title;
    // Original game's author/credit (e.g. "OBONO", "DANIEL C", "LORANDIL")
    // - shown as "BY <author>" under the menu's thumbnail screenshot.
    char* author;
    GameFunc init;
    GameFunc update;
    // Optional (pass NULL if not needed) - called once when this game
    // resumes after being fully frozen for the quit-confirmation dialog
    // (see main.c's dispatch loop). Most games redraw their whole screen
    // unconditionally every update() call, so freezing/resuming them is
    // transparent - but a game that skips its own redraw entirely on
    // frames where nothing changed (a dirty-flag optimization) needs this
    // hook to force that flag back to true, or its next real update()
    // could also skip drawing and leave the dialog's pixels on screen
    // instead of the game's own content.
    GameFunc onResume;
} Game;

extern int gameCount;

void addGame( char* title, char* author, GameFunc init, GameFunc update, GameFunc onResume );
Game* menu_getGame( int index );
void menu_init();

// draws the menu and handles its own navigation input; returns the game
// just chosen (Fire/A pressed on it) this frame, or -1 if none was
int menu_update();

#endif
