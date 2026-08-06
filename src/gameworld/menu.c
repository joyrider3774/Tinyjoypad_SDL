#include "avrCompat.h"
#include "menu.h"
#include "machineDependent.h"
#include "biosFont.h"
#include <string.h>

// Ported from the sibling tinyjoypad_vircon32 build's own menu.c - dialect
// fixes only (array-declaration order, `int[]` text -> `char[]`,
// Vircon32's own BIOS print_at()/clear_screen()/screen_width/
// bios_character_width -> this project's biosDrawText()/md_beginFrame()/
// MD_SCREEN_WIDTH/BIOS_FONT_CHAR_W - see biosFont.h/machineDependent.h),
// no structural changes.

// Bumped 32->48 in the Vircon32 build when Dino Game (its 33rd registered
// game) was silently dropped by addGame()'s own capacity guard below -
// kept at that same headroom here for whenever this project's own port
// count catches up. Bumped again, 48->64, when this project's own count
// reached 50 (41 existing + 9 new games in one batch) - the exact same
// silent-drop risk, just here instead of there this time (addGame()'s own
// guard below would have quietly dropped the last 2 games with no error
// at all, not a crash - the kind of bug that's easy to miss until someone
// notices a game they know they added just isn't in the menu).
#define MAX_GAMES 64

// How many entries fit in the vertical space between the list's start
// (y=140) and the bottom of the 360px-tall screen at 24px/row.
#define GAMES_PER_PAGE 9

// Top of the game list/thumbnail area - the header lines above it
// (title + 2 hint lines) are centered independently of this.
#define LIST_AREA_TOP 140

// Centers a fixed-width-bios-font string horizontally on screen.
int menuCenteredX( char* text )
{
    return ( MD_SCREEN_WIDTH - strlen( text ) * BIOS_FONT_CHAR_W ) / 2;
}

int gameCount = 0;
Game games[ MAX_GAMES ];
int selection = 0;

// `games[]` stays in addGame()'s own registration order always - that's
// also what a launched game's init/update function pointers are looked up
// by (menu_getGame(), called from main()'s dispatch loop), and (once
// thumbnails are wired up in Phase 5) what the thumbnail atlas will be
// keyed by. Alphabetical sorting is purely a *display* concern, so it
// lives in a separate indirection array instead of reordering games[]
// itself: displayOrder[i] holds the original games[] index shown at
// display position i. `selection` walks display positions (0..gameCount-1)
// - every place that used to index games[] directly with `selection` must
// go through displayOrder[selection] instead, or it'll show/launch the
// wrong game the moment the alphabetical order differs from registration
// order.
int displayOrder[ MAX_GAMES ];
bool displayOrderBuilt = false;

bool prevUp = false;
bool prevDown = false;
bool prevFire = false;
bool prevLeft = false;
bool prevRight = false;

void addGame( char* title, char* author, GameFunc init, GameFunc update, GameFunc onResume )
{
    if( gameCount >= MAX_GAMES )
      return;

    games[ gameCount ].title = title;
    games[ gameCount ].author = author;
    games[ gameCount ].init = init;
    games[ gameCount ].update = update;
    games[ gameCount ].onResume = onResume;
    gameCount++;
}

Game* menu_getGame( int index )
{
    return &games[ index ];
}

// Selection sort on displayOrder (by games[].title) - gameCount is always
// a small handful of entries, so O(n^2) costs nothing measurable here.
void menu_buildDisplayOrder()
{
    for( int i = 0; i < gameCount; i++ )
      displayOrder[ i ] = i;

    for( int i = 0; i < gameCount - 1; i++ )
    {
        int best = i;
        for( int j = i + 1; j < gameCount; j++ )
          if( strcmp( games[ displayOrder[ j ] ].title, games[ displayOrder[ best ] ].title ) < 0 )
            best = j;

        if( best != i )
        {
            int tmp = displayOrder[ i ];
            displayOrder[ i ] = displayOrder[ best ];
            displayOrder[ best ] = tmp;
        }
    }

    displayOrderBuilt = true;
}

void menu_init()
{
    prevUp = false;
    prevDown = false;
    prevFire = false;
    prevLeft = false;
    prevRight = false;

    // Built once (addGames() has already run by the time menu_init() is
    // first called, and gameCount/games[] never change afterward) - not
    // redone on every return-to-menu.
    if( !displayOrderBuilt )
      menu_buildDisplayOrder();
}

int menu_update()
{
    bool up = md_inputUp();
    bool down = md_inputDown();
    bool fire = md_inputFire();
    bool left = md_inputLeft();
    bool right = md_inputRight();

    if( down && !prevDown )
    {
        selection++;
        if( selection >= gameCount )
          selection = 0;
    }
    if( up && !prevUp )
    {
        selection--;
        if( selection < 0 )
          selection = gameCount - 1;
    }

    int totalPages = ( gameCount + GAMES_PER_PAGE - 1 ) / GAMES_PER_PAGE;

    // LEFT/RIGHT jump a whole page at a time (wrapping past the last/first
    // page), same idea as UP/DOWN moving one entry at a time.
    if( right && !prevRight )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage++;
        if( currentPage >= totalPages )
          currentPage = 0;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }
    if( left && !prevLeft )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage--;
        if( currentPage < 0 )
          currentPage = totalPages - 1;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }

    bool justFired = ( fire && !prevFire );

    prevUp = up;
    prevDown = down;
    prevFire = fire;
    prevLeft = left;
    prevRight = right;

    // ---- draw ----
    md_beginFrame();
    biosDrawText( "TINYJOYPAD FOR SDL", menuCenteredX( "TINYJOYPAD FOR SDL" ), 40 );
    biosDrawText( "UP/DOWN: SELECT     A: PLAY", menuCenteredX( "UP/DOWN: SELECT     A: PLAY" ), 80 );

    int currentPage = selection / GAMES_PER_PAGE;

    if( totalPages > 1 )
    {
        char pageNumText[ 8 ];
        char totalPagesText[ 8 ];
        char pageHintText[ 48 ];
        itoa( currentPage + 1, pageNumText, 10 );
        itoa( totalPages, totalPagesText, 10 );
        strcpy( pageHintText, "LEFT/RIGHT: CHANGE PAGE " );
        strcat( pageHintText, pageNumText );
        strcat( pageHintText, "/" );
        strcat( pageHintText, totalPagesText );
        biosDrawText( pageHintText, menuCenteredX( pageHintText ), 105 );
    }

    int startIndex = currentPage * GAMES_PER_PAGE;

    int y = LIST_AREA_TOP;
    for( int i = 0; i < GAMES_PER_PAGE; i++ )
    {
        int idx = startIndex + i;
        if( idx >= gameCount )
          break;

        // Kept close to the left edge (small margin only) so the right
        // side of the screen stays free for the current game's thumbnail
        // screenshot (see THUMBNAIL_* below).
        int x = 60;
        if( idx == selection )
          x = 40;

        if( idx == selection )
          biosDrawText( ">", x, y );

        // Zero-padded "NN " position number (1-based on the whole
        // alphabetized list, not per-page) prepended to the title.
        char numText[ 8 ];
        itoa( idx + 1, numText, 10 );
        char labelText[ 64 ];
        if( idx + 1 < 10 )
          strcpy( labelText, "0" );
        else
          strcpy( labelText, "" );
        strcat( labelText, numText );
        strcat( labelText, ". " );
        strcat( labelText, games[ displayOrder[ idx ] ].title );

        biosDrawText( labelText, x + 20, y );
        y += 24;
    }

    // Real gameplay screenshot of the currently-selected game, in the
    // margin freed up on the right by keeping the list itself close to
    // the left edge - switches immediately whenever the selection moves,
    // since it's just read straight off `selection` every frame. Centered
    // vertically (as a group with the "BY <author>" line below it) within
    // the list/selection area (LIST_AREA_TOP down to the bottom of the
    // screen) rather than top-aligned with the list. Indexed through
    // displayOrder[] like the title above - the thumbnail atlas is keyed
    // by registration index, not by alphabetical position.
    int selectedGameIndex = displayOrder[ selection ];
    if( selectedGameIndex < md_getThumbnailCount() )
    {
        int authorGapY = 8;
        int blockHeight = MD_THUMBNAIL_HEIGHT + authorGapY + BIOS_FONT_CHAR_H;
        int blockY = LIST_AREA_TOP + ( ( MD_SCREEN_HEIGHT - LIST_AREA_TOP ) - blockHeight ) / 2;

        md_drawGameThumbnail( selectedGameIndex, 340, blockY );

        char authorText[ 32 ];
        strcpy( authorText, "BY " );
        strcat( authorText, games[ selectedGameIndex ].author );
        int authorX = 340 + ( MD_THUMBNAIL_WIDTH - strlen( authorText ) * BIOS_FONT_CHAR_W ) / 2;
        biosDrawText( authorText, authorX, blockY + MD_THUMBNAIL_HEIGHT + authorGapY );
    }

    if( justFired )
      return selectedGameIndex;

    return -1;
}
