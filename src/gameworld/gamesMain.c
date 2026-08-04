// -----------------------------------------------------------------------------
// Top-level "game world" glue: the quit-confirmation dialog and the
// menu<->game dispatch (gamesMain_dispatchFrame()) main.c calls once per
// real frame, plus the small CLI-support surface (gamesMain.h). Every
// shim/menu/games/*.c file is its own separate translation unit now (see
// CLAUDE.md's "Translation-unit boundary" section for why this changed from
// an earlier single-TU-via-#include design) - this file no longer #includes
// any of their .c bodies, only the headers it actually calls into.
//
// Must never #include SDL.h or anything that pulls in <stdint.h> (see
// machineDependent.h's own header comment for why) - still true here even
// though this is no longer the one file every game gets stitched into.
// -----------------------------------------------------------------------------

#include "avrCompat.h"
#include "biosFont.h"
#include "machineDependent.h"
#include "menu.h"
#include "menuGameList.h"

// -----------------------------------------------------------------------------
// Quit-confirmation dialog - ported from the sibling tinyjoypad_vircon32
// build's own portVircon32.c (drawConfirmQuitDialog(), plus main()'s own
// confirmingQuit/confirmSelection state and edge-detection). Lives here
// (the game-world side) rather than in main.c because it only ever calls
// game-world-safe drawing functions (md_drawSolidRect(), biosDrawText()) -
// main.c's own dispatch loop just calls into this file's small surface
// (see gamesMain.h) to keep the platform side focused on control flow.
// -----------------------------------------------------------------------------

bool confirmingQuit = false;
int confirmSelection = 0; // 0 = NO (default - the safer choice), 1 = YES
bool prevConfirmLeft = false;
bool prevConfirmRight = false;
bool prevConfirmFire = false;
bool prevStart = false;

int currentGameIndex = -1;

void drawConfirmQuitDialog()
{
    int boxX = MD_DIALOG_X, boxY = MD_DIALOG_Y, boxW = MD_DIALOG_W, boxH = MD_DIALOG_H;
    int borderThickness = 6;

    md_drawSolidRect( boxX, boxY, boxW, boxH, MD_COLOR_WHITE );
    md_drawSolidRect
    (
        boxX + borderThickness, boxY + borderThickness,
        boxW - ( borderThickness * 2 ), boxH - ( borderThickness * 2 ),
        MD_COLOR_BLACK
    );

    biosDrawText( "CONFIRM", boxX + 125, boxY + 20 );
    biosDrawText( "QUIT TO MENU?", boxX + 95, boxY + 55 );

    int yesX = boxX + 100;
    int noX = boxX + 210;
    int optionsY = boxY + 95;

    if( confirmSelection == 1 )
      biosDrawText( ">", yesX - 15, optionsY );
    else
      biosDrawText( ">", noX - 15, optionsY );

    biosDrawText( "YES", yesX, optionsY );
    biosDrawText( "NO", noX, optionsY );
}

// The whole top-level menu<->game dispatch, called once per real frame by
// main.c - matches the Vircon32 build's own main() loop body exactly
// (state transitions only; md_updateAudio()/obonoCoreShimUpdateSound()/
// md_endFrame() stay in main.c itself, called unconditionally afterward
// every frame regardless of which branch below ran).
void gamesMain_dispatchFrame()
{
    bool start = md_inputStart();
    bool justStarted = ( start && !prevStart );
    prevStart = start;

    if( confirmingQuit )
    {
        bool left = md_inputLeft();
        bool right = md_inputRight();
        bool fire = md_inputFire();
        bool justLeft = ( left && !prevConfirmLeft );
        bool justRight = ( right && !prevConfirmRight );
        bool justFire = ( fire && !prevConfirmFire );
        prevConfirmLeft = left;
        prevConfirmRight = right;
        prevConfirmFire = fire;

        if( justLeft || justRight )
          confirmSelection = 1 - confirmSelection;

        if( justFire )
        {
            // The same physical press that just confirmed this dialog must
            // not also register as a fresh press once we're back in the
            // menu (instantly launching whatever's highlighted) or back in
            // gameplay (an unwanted in-game action the instant it resumes)
            // - md_inputFire() is the single shared gate every fire-read
            // goes through, so arming it here covers both destinations.
            md_armInputFireGate();
            if( confirmSelection == 1 )
            {
                md_stopTone();
                currentGameIndex = -1;
                menu_init();
            }
            else if( menu_getGame( currentGameIndex )->onResume != NULL )
              menu_getGame( currentGameIndex )->onResume();
            confirmingQuit = false;
        }
        else if( justStarted )
        {
            // pressing Start again cancels, same as selecting NO
            if( menu_getGame( currentGameIndex )->onResume != NULL )
              menu_getGame( currentGameIndex )->onResume();
            confirmingQuit = false;
        }

        drawConfirmQuitDialog();
    }
    else if( currentGameIndex != -1 && justStarted )
    {
        confirmingQuit = true;
        confirmSelection = 0;
        // Arm against whatever Left/Right/Fire happen to already be held
        // at this exact moment, the same reasoning as
        // md_armInputFireGate() - otherwise a leftover press from gameplay
        // could immediately register as a dialog input.
        prevConfirmLeft = md_inputLeft();
        prevConfirmRight = md_inputRight();
        prevConfirmFire = md_inputFire();
        drawConfirmQuitDialog();
    }
    else if( currentGameIndex == -1 )
    {
        int chosen = menu_update();
        if( chosen != -1 )
        {
            currentGameIndex = chosen;
            md_armInputFireGate();
            menu_getGame( chosen )->init();
        }
    }
    else
    {
        menu_getGame( currentGameIndex )->update();
    }

    // Both evaluated last, not first, so they reflect THIS frame's fully
    // updated state (every state transition above, including the dialog
    // opening/closing this very frame, already happened by the time these
    // run) - matters most for md_setDialogShowing(), so the very first
    // frame the dialog opens is already correct, not just the frames
    // after.
    //
    // md_setInGame() stays a plain currentGameIndex != -1 - deliberately
    // NOT also gated on !confirmingQuit the way an earlier revision of
    // this function had it: that made the SDL ports' own glow/CRT/pixel-
    // grid presentation effects, gated on it in their own md_endFrame(),
    // turn off for the dialog's entire duration - which also blanked them
    // from the still-visible frozen game screen behind/around the dialog
    // box, not just the box itself. md_setDialogShowing() (new) is the
    // correct, more surgical tool for that instead: it tells the platform
    // side which exact sub-rect (MD_DIALOG_X/Y/W/H) to keep effect-free,
    // so the rest of the frozen screen still gets the effects the same
    // way it did the instant before the dialog opened.
    md_setInGame( currentGameIndex != -1 );
    md_setDialogShowing( confirmingQuit );
}

void gamesMain_init()
{
    addGames();
    menu_init();
}

// -----------------------------------------------------------------------------
// CLI-support surface - see gamesMain.h for what each of these is for.
// -----------------------------------------------------------------------------

int gamesMain_getGameCount()
{
    return gameCount;
}

char* gamesMain_getGameTitle( int idx )
{
    if( idx < 0 || idx >= gameCount )
      return NULL;

    return menu_getGame( idx )->title;
}

static int gamesMainStrcmpNoCase( char* a, char* b )
{
    while( *a && *b )
    {
        int ca = *a, cb = *b;
        if( ca >= 'a' && ca <= 'z' ) ca -= 32;
        if( cb >= 'a' && cb <= 'z' ) cb -= 32;
        if( ca != cb )
          return ca - cb;
        a++;
        b++;
    }
    return *a - *b;
}

int gamesMain_findGameByTitle( char* title )
{
    for( int i = 0; i < gameCount; i++ )
      if( gamesMainStrcmpNoCase( menu_getGame( i )->title, title ) == 0 )
        return i;

    return -1;
}

void gamesMain_launchGameDirect( int idx )
{
    if( idx < 0 || idx >= gameCount )
      return;

    currentGameIndex = idx;
    md_armInputFireGate();
    menu_getGame( idx )->init();
}

void gamesMain_drawFpsOverlay( float fps )
{
    int whole = (int)fps;
    int frac = (int)( ( fps - (float)whole ) * 100.0f + 0.5f );
    if( frac >= 100 )
    {
        frac -= 100;
        whole++;
    }

    char wholeText[ 8 ];
    char fracText[ 8 ];
    itoa( whole, wholeText, 10 );
    itoa( frac, fracText, 10 );

    char fpsText[ 24 ];
    strcpy( fpsText, "FPS " );
    strcat( fpsText, wholeText );
    strcat( fpsText, "." );
    if( frac < 10 )
      strcat( fpsText, "0" );
    strcat( fpsText, fracText );

    int textW = biosTextWidth( fpsText );
    int rectW = textW + 8;
    int rectH = BIOS_FONT_CHAR_H + 4;
    md_drawSolidRect( 0, 0, rectW, rectH, MD_COLOR_BLACK );
    biosDrawText( fpsText, 4, 2 );

    // See machineDependent.h's own md_setFpsOverlayShowing() comment - this
    // is what keeps the readout itself crisp/unblurred on top of the SDL
    // ports' own presentation effects, the same way md_setDialogShowing()
    // already does for the quit-confirmation dialog box.
    md_setFpsOverlayShowing( true, rectW, rectH );
}
