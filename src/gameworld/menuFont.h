#ifndef MENU_FONT_H
#define MENU_FONT_H

#include "machineDependent.h"

// -----------------------------------------------------------------------------
// Small 6x8 one-byte-per-column bitmap font (the classic ssd1306xled font).
//
// NOT used for the menu (see biosFont.h instead - the menu draws with the
// real Vircon32 BIOS font, per direct user request). This table's purpose
// is narrower: it already exists independently three times in the sibling
// tinyjoypad_vircon32 build (gameOroboros.c/gameRunDudeRun.c/
// gameDinoGame.c's own `rddFont`/etc, each page-aligned 8px tall like every
// other game asset there) - kept here as one shared home so that when
// those three games are ported (Phase 4), they can point at this copy
// instead of re-duplicating it a fourth time. Not currently included by
// gamesMain.c - nothing needs it yet.
//
// This exact table (95 printable ASCII chars, ' '..'~', 6 bytes each: 5
// columns of glyph data + 1 blank spacing column, bit0=top/bit7=bottom
// matching every other column byte in this whole project) is byte-for-byte
// identical across all three of those games' own copies.
//
// Text is always drawn page-aligned (y must be a multiple of 8) - matching
// how every other page-based draw call in this project works.
// -----------------------------------------------------------------------------

// `static` throughout this file: header-only, and now potentially
// #include'd by more than one separate translation unit (see
// CLAUDE.md's "Translation-unit boundary" section) now that the game
// world is no longer one stitched-together TU - without it, two TUs
// including this header would each emit an external definition of the
// same array/functions and the link would fail with "multiple
// definition of menuFont"/etc.
static int menuFont[ 570 ] =
{
0,0,0,0,0,0,0,0,0,47,0,0,0,0,7,0,
7,0,0,20,127,20,127,20,0,36,42,127,42,18,0,98,
100,8,19,35,0,54,73,85,34,80,0,0,5,3,0,0,
0,0,28,34,65,0,0,0,65,34,28,0,0,20,8,62,
8,20,0,8,8,62,8,8,0,0,0,160,96,0,0,8,
8,8,8,8,0,0,96,96,0,0,0,32,16,8,4,2,
0,62,81,73,69,62,0,0,66,127,64,0,0,66,97,81,
73,70,0,33,65,69,75,49,0,24,20,18,127,16,0,39,
69,69,69,57,0,60,74,73,73,48,0,1,113,9,5,3,
0,54,73,73,73,54,0,6,73,73,41,30,0,0,54,54,
0,0,0,0,86,54,0,0,0,8,20,34,65,0,0,20,
20,20,20,20,0,0,65,34,20,8,0,2,1,81,9,6,
0,50,73,89,81,62,0,124,18,17,18,124,0,127,73,73,
73,54,0,62,65,65,65,34,0,127,65,65,34,28,0,127,
73,73,73,65,0,127,9,9,9,1,0,62,65,73,73,122,
0,127,8,8,8,127,0,0,65,127,65,0,0,32,64,65,
63,1,0,127,8,20,34,65,0,127,64,64,64,64,0,127,
2,12,2,127,0,127,4,8,16,127,0,62,65,65,65,62,
0,127,9,9,9,6,0,62,65,81,33,94,0,127,9,25,
41,70,0,70,73,73,73,49,0,1,1,127,1,1,0,63,
64,64,64,63,0,31,32,64,32,31,0,63,64,56,64,63,
0,99,20,8,20,99,0,7,8,112,8,7,0,97,81,73,
69,67,0,0,127,65,65,0,0,2,4,8,16,32,0,0,
65,65,127,0,0,4,2,1,2,4,0,64,64,64,64,64,
0,0,1,2,4,0,0,32,84,84,84,120,0,127,72,68,
68,56,0,56,68,68,68,32,0,56,68,68,72,127,0,56,
84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,127,8,4,4,120,0,0,68,125,64,0,0,64,128,132,
125,0,0,127,16,40,68,0,0,0,65,127,64,0,0,124,
4,24,4,120,0,124,8,4,4,120,0,56,68,68,68,56,
0,252,36,36,36,24,0,24,36,36,24,252,0,124,8,4,
4,8,0,72,84,84,84,32,0,4,63,68,64,32,0,60,
64,64,32,124,0,28,32,64,32,28,0,60,64,48,64,60,
0,68,40,16,40,68,0,28,160,160,160,124,0,68,100,84,
76,68,0,8,54,65,65,0,0,0,0,127,0,0,0,0,
65,65,54,8,0,8,4,8,16,8,
};

static int menuFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return menuFont[ ( ch - 32 ) * 6 + col ];
}

// Character width in pixels (5 glyph columns + 1 blank spacing column).
#define MENU_FONT_CHAR_W 6
#define MENU_FONT_CHAR_H 8

static void menuDrawChar( int ch, int x, int y )
{
    int page = y / MENU_FONT_CHAR_H;
    int col;

    for( col = 0; col < MENU_FONT_CHAR_W; col++ )
      md_drawColumn( x + col, page, menuFontByte( ch, col ) );
}

static void menuDrawText( char* text, int x, int y )
{
    int i = 0;

    while( text[ i ] != 0 )
    {
        menuDrawChar( text[ i ], x + i * MENU_FONT_CHAR_W, y );
        i++;
    }
}

// Pixel width of `text` if drawn with menuDrawText() - useful for centering.
static int menuTextWidth( char* text )
{
    int len = 0;

    while( text[ len ] != 0 )
      len++;

    return len * MENU_FONT_CHAR_W;
}

#endif
