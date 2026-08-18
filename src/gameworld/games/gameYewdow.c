// =============================================================================
// YEWDOW mini (inufuto, UIAPduino+SSD1306/CH32V003 edition, license "None
// specified" - GitHub reports no LICENSE file for `UIAPduino_yewdow`) - a
// puzzle/arcade hybrid: place directional arrows in empty map cells to steer
// autonomously-driving cars, guiding them over all 8 flag "items" scattered
// across a 12-column x 7-row maze to clear the stage. Cars bounce off (and
// destroy) walls, but going off the map edge or driving into a rock ends
// that attempt; a countdown clock also ends the attempt if it reaches zero.
// A separate player character ("Man") walks the same maze to place arrows
// and must dodge wandering monsters and the cars themselves (contact just
// freezes Man briefly, it isn't fatal - only a lost car or the clock running
// out costs a life). 7 hand-authored stages (looped indefinitely, with the
// per-attempt time budget shrinking every full lap), 3 lives.
//
// One of ~22 games from inufuto's own "Cate engine" family (CH32V003
// RISC-V MCU + SSD1306 OLED, UIAPduino hardware) ported into this project -
// a different hardware family from every earlier game here (not AVR, not
// ESP8266/8285), but the same Vircon32-dialect C source shape as every
// other game, so the same standard-C dialect-conversion recipe applies
// unchanged. `gameCracky.c` (this project's own port of the same family's
// reference implementation) documents the shared engine's general shape -
// the two-level VVram/Vram tile rendering, the frame-stepped sound
// sequencer, the confirmed-correct "no hardware display-orientation
// transform needed" finding - all of which applies here unchanged and
// isn't re-derived in this file's own comment.
//
// Mechanically dialect-converted from the sibling `tinyjoypad_vircon32`
// project's own already-correct, already user-verified gameYewdow.c (see
// that project's own CLAUDE.md for the full porting/bug-fix history behind
// every finding documented in that file's own header comment - the missing
// Y/W/D glyphs in the shared 27-glyph ASCII font, the restored real
// `TitleBytes[]` pixel-art logo, the BGM-during-lose-car-animation ordering
// fix, the unsigned-byte-wraparound-reliance fixes in `yewCanMoveTo()`/the
// car movement lookahead, and the deliberate non-fix of the off-map-edge
// loss check's own upstream dead-code quirk - none of that history is
// duplicated here, only linked to). Only `int[N] name` -> `int name[N]`
// (and the 2D form) array-declaration syntax and the 5 bare `struct` tags
// (`YewActor`/`YewMonster`/`YewPoint`/`YewItem`/`YewSprite`) -> `typedef
// struct` needed changing to reach standard C - every string-shaped array
// in this file (`sScore`/`sStage`/`sTime`/`sMini`/`sStart`/`sContinue`/
// `sCredit`/the two `s` locals in `yewPrintGameOver()`/`yewPrintTimeUp()`,
// the `table[]` lookup in `yewAsciiIndex()`) holds individual `'A'`-style
// char constants, never a `"..."` string literal (confirmed by inspection -
// no string literal appears anywhere in this file's own real code, only in
// comments), so none of them needed `int[]`->`char[]` widening - every
// value still round-trips through `yewAsciiIndex()`'s own int-comparison
// lookup exactly as upstream wrote it, matching `gameCracky.c`'s own
// identical finding for its own status-text arrays.
//
// No EEPROM/high-score persistence here, matching upstream having none at
// all (a CH32V003 RISC-V board, not AVR - this project's own `eeprom_*`
// shim/avrCompat widening aren't relevant here the way they are for every
// ATtiny85 port). No `forceRedraw` needed either: every one of this state
// machine's own branches (`yewUpdateTitle`/`yewUpdateStartJingle`/
// `yewUpdatePlaying`/`yewUpdateLoseCarAnim`/`yewUpdateTimeUpWait`/
// `yewUpdateGameOverJingle`/`yewUpdateClearWait`/`yewUpdateClearJingle`/
// `yewUpdateBonusTally`) calls `yewRender()` unconditionally on every real
// tick it runs, so there's no "last frame's pixels are still correct, skip
// redrawing" case anywhere in this file for a resumed screen to miss.
//
// Only 4 directions + 1 action button (`isLeftPressed()`/`isRightPressed()`/
// `isUpPressed()`/`isDownPressed()`/`isFirePressed()`, via
// `tinyJoypadShim.h`) - a strict subset of what that shim already exposes,
// so no new shim primitive was needed (`isFire2Pressed()` goes unused
// here, matching Cracky). Every `Sound.cpp`-originated call routes through
// `md_playTone(freqHz, durationSeconds)` via 3 independent frame-stepped
// sequencer slots, exactly the same non-blocking-sequencer treatment
// `gameCracky.c` already established for this shared engine family - not
// re-derived here.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into yewCharPattern (map/sprite tiles)
// -----------------------------------------------------------------------------

#define YEW_CHAR_SPACE 0
#define YEW_CHAR_LOGO 0
#define YEW_CHAR_FENCE 0x10
#define YEW_CHAR_MAN 0x12
#define YEW_CHAR_MONSTER 0x32
#define YEW_CHAR_CAR 0x52
#define YEW_CHAR_ARROW 0x62
#define YEW_CHAR_ITEM 0x72
#define YEW_CHAR_POINT 0x82
#define YEW_CHAR_WALL 0x92
#define YEW_CHAR_ROCK 0x96
#define YEW_CHAR_FLUSH 0x9A
#define YEW_CHAR_END 0x9E

// -----------------------------------------------------------------------------
//   ScanKeys.h - kept for structural fidelity, though this port reads
//   isLeftPressed()/etc directly rather than building a combined key mask.
// -----------------------------------------------------------------------------

#define YEW_KEYS_LEFT 0x01
#define YEW_KEYS_RIGHT 0x02
#define YEW_KEYS_UP 0x04
#define YEW_KEYS_DOWN 0x08
#define YEW_KEYS_BUTTON0 0x10

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define YEW_COORD_SHIFT 0
#define YEW_COORD_RATE ( 1 << YEW_COORD_SHIFT )
#define YEW_COORD_MASK ( YEW_COORD_RATE - 1 )
#define YEW_MAP_SHIFT ( YEW_COORD_SHIFT + 1 )
#define YEW_MAP_RATE ( YEW_COORD_RATE * 2 )
#define YEW_MAP_MASK ( YEW_MAP_RATE - 1 )
#define YEW_HIT_RANGE ( YEW_COORD_RATE * 4 / 3 )

// -----------------------------------------------------------------------------
//   Actor.h
// -----------------------------------------------------------------------------

#define YEW_ACTOR_SEQ_MASK 0x01
#define YEW_ACTOR_DIRECTION_MASK 0x06
#define YEW_ACTOR_PATTERN_MASK ( YEW_ACTOR_DIRECTION_MASK | YEW_ACTOR_SEQ_MASK )
#define YEW_ACTOR_LIVE 0x08

#define YEW_DIRECTION_LEFT 0
#define YEW_DIRECTION_RIGHT 2
#define YEW_DIRECTION_UP 4
#define YEW_DIRECTION_DOWN 6

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define YEW_MAP_WIDTH 12
#define YEW_MAP_HEIGHT 7
#define YEW_MAP_WIDTH_PER_BYTE 2
#define YEW_MAX_ITEM_COUNT 8

#define YEW_STAGE_WIDTH ( YEW_MAP_WIDTH * 2 )
#define YEW_STAGE_HEIGHT ( YEW_MAP_HEIGHT * 2 )

#define YEW_CELL_SPACE 0x0
#define YEW_CELL_WALL 0x1
#define YEW_CELL_ROCK 0x2
#define YEW_CELL_ARROW 0x4
#define YEW_CELL_ITEM 0x8
// Cell_NullArrow (0xc upstream) confirmed dead by grep - declared, never
// referenced anywhere else in the real source - dropped.

#define YEW_STAGE_COUNT 7
#define YEW_MAX_CAR_COUNT 2
#define YEW_MAX_MONSTER_COUNT 2
#define YEW_CELLMAP_BYTES ( ( YEW_MAP_WIDTH / YEW_MAP_WIDTH_PER_BYTE ) * YEW_MAP_HEIGHT )
#define YEW_RANGE_X ( ( YEW_MAP_WIDTH * 2 - 1 ) * YEW_COORD_RATE )
#define YEW_RANGE_Y ( ( YEW_MAP_HEIGHT * 2 - 1 ) * YEW_COORD_RATE )

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define YEW_VVRAM_WIDTH 24
#define YEW_VVRAM_HEIGHT 16
#define YEW_STAGE_TOP 1

// Title()'s own real title-logo layout constants, kept as the same real
// expressions upstream declares (`constexpr auto TitleLength = 5;`/
// `constexpr auto TitleLeft = (VVramWidth - 4 * TitleLength) / 2;`) rather
// than pre-resolved literals, matching this project's own established
// "#defines should stay exactly as upstream wrote them" preference (see
// gameCracky.c's own header comment for the same rule applied there).
#define YEW_TITLE_LENGTH 5
#define YEW_TITLE_LEFT ( ( YEW_VVRAM_WIDTH - 4 * YEW_TITLE_LENGTH ) / 2 )

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define YEW_SPRITE_CAR 0
#define YEW_SPRITE_MAN 2
#define YEW_SPRITE_MONSTER 3
#define YEW_SPRITE_POINT 5
#define YEW_SPRITE_END 7
#define YEW_INVALID_CODE 0xff

// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

#define YEW_MAN_BUTTON_ON 0x40
#define YEW_MAN_ARROW_ON 0x20
#define YEW_MAN_FLASH 0x80
#define YEW_MAN_FREEZE_TIME ( 20 * YEW_COORD_RATE )

// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

#define YEW_MONSTER_FREEZE 0x10
#define YEW_MONSTER_RETURN 0x20
#define YEW_MONSTER_FREEZE_TIME ( 30 * YEW_COORD_RATE )

// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

#define YEW_INVALID_X 0xff
#define YEW_INVALID_TYPE 0xff

// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

#define YEW_POINT_COUNT 2
#define YEW_POINT_INVALID_Y 0xe0
#define YEW_POINT_TIME 6

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching upstream's own enum exactly.
// -----------------------------------------------------------------------------

#define YEW_N8 6
#define YEW_N8L 8
#define YEW_N8R 4
#define YEW_N8P ( YEW_N8 * 3 / 2 )
#define YEW_N4 ( YEW_N8 * 2 )
#define YEW_N4P ( YEW_N4 * 3 / 2 )
#define YEW_N2 ( YEW_N4 * 2 )
#define YEW_N2P ( YEW_N2 * 3 / 2 )
#define YEW_N1 ( YEW_N2 * 2 )
#define YEW_N16 ( YEW_N8 / 2 )

#define YEW_E2 1
#define YEW_F2 2
#define YEW_F2S 3
#define YEW_G2 4
#define YEW_G2S 5
#define YEW_A2 6
#define YEW_A2S 7
#define YEW_B2 8
#define YEW_C3 9
#define YEW_C3S 10
#define YEW_D3 11
#define YEW_D3S 12
#define YEW_E3 13
#define YEW_F3 14
#define YEW_F3S 15
#define YEW_G3 16
#define YEW_G3S 17
#define YEW_A3 18
#define YEW_A3S 19
#define YEW_B3 20
#define YEW_C4 21
#define YEW_C4S 22
#define YEW_D4 23
#define YEW_D4S 24
#define YEW_E4 25
#define YEW_F4 26
#define YEW_F4S 27
#define YEW_G4 28
#define YEW_G4S 29
#define YEW_A4 30
#define YEW_A4S 31
#define YEW_B4 32
#define YEW_C5 33
#define YEW_C5S 34
#define YEW_D5 35
#define YEW_D5S 36
#define YEW_E5 37
#define YEW_F5 38
#define YEW_F5S 39
#define YEW_G5 40

// Sound sequencer melody ids, resolved by yewMelodyLength()/yewMelodyValue()
// instead of a real pointer-per-channel (this project's own established
// "resolve by id" pattern).
#define YEW_MELODY_NONE 0
#define YEW_MELODY_LOOSE 1
#define YEW_MELODY_BEEP 2
#define YEW_MELODY_HIT 3
#define YEW_MELODY_FREEZE 4
#define YEW_MELODY_START 5
#define YEW_MELODY_CLEAR 6
#define YEW_MELODY_GAMEOVER 7
#define YEW_MELODY_BGM1 8
#define YEW_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Main.cpp
// -----------------------------------------------------------------------------

#define YEW_MAX_TIME_DENOM ( 50 / ( 8 / YEW_COORD_RATE ) )
#define YEW_BONUS_RATE 4
#define YEW_TICK_DIVISOR 8

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied. AsciiPattern/CharPattern are
//   confirmed byte-identical to gameCracky.c's own copies (same underlying
//   Cate-engine assets) - kept as this file's own self-contained copy
//   rather than shared, matching this project's own standing "each game
//   keeps its own copy" precedent.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph. The first 108
// values (27 glyphs) are byte-diff-verified identical to upstream's own real
// Chars.cpp table (and to gameCracky.c's own copy of the same shared font) -
// see this file's own header comment.
//
// **A real, serious bug found only via live play, not by static comparison
// against upstream**: this restricted 27-glyph font is missing Y, W, and D -
// EVERY UIAPduino/Cate-engine game in this project's whole `more games/`
// batch shares this exact same restricted glyph set (confirmed by grep
// across all ~21 sibling ports' own upstream Chars.cpp files - they're all
// byte-identical). Upstream's own Status.cpp never actually calls PrintC/
// PrintS with any of those 3 letters for its real UI text (SCORE/STAGE/TIME/
// START/CONTINUE/GAME OVER/TIME UP/INUFUTO 2026/MINI all fit the restricted
// set) - the game's own TITLE WORD, "YEWDOW", is instead drawn via a
// completely separate real pixel-art bitmap (`Status.cpp`'s own
// `TitleBytes[]`, written directly into VVram, never touching this ASCII
// font at all) specifically BECAUSE the restricted font can't spell it.
// This port's own simplification of that bitmap logo into plain text (see
// this file's own header comment) didn't account for that - and the result,
// confirmed via an actual Puppeteer screenshot of the title screen, was
// "YEWDOW" rendering as "_E__O_" (Y/W/D silently falling through
// yewAsciiIndex()'s own not-found case to the blank/space glyph, index 0) -
// a genuine title-screen text-corruption bug, just via a different
// mechanism than the more common "text overwritten by another string" shape
// already found in several sibling ports. **Fixed** by extending the font
// with 3 new hand-drawn glyphs (indices 27-29: D, W, Y), using the same
// 5-row-tall/3-significant-column/1-spacer-column bit-packed encoding as
// every existing glyph (bit0=top row..bit4=bottom row per column byte) -
// D reuses O's own flat-top/flat-bottom/curved-right-edge column values
// (0x11, 0x0e) with O's own rounded left edge (0x0e) swapped for a flat
// solid vertical bar (0x1f, matching every other flat-left-edge letter in
// this font, e.g. A/E/F/P/R); W is a vertically-mirrored M (M's own middle
// column, 0x06, dips from the top; W's own middle column, 0x0c, rises from
// the bottom instead); Y is a short two-stroke fork at the top (0x03 each
// side) merging into a single solid stem for the bottom 3 rows (0x1c).
//
// **Follow-up**: the real root cause turned out to be the plain-text
// title substitute itself, not a font gap that needed patching around -
// once `yewBeginTitle()` was fixed to draw upstream's own real
// `TitleBytes[]` bitmap logo instead (see `yewTitleBytes`'s own comment
// and `yewBeginTitle()`'s own comment for the full story), the title word
// no longer goes through this ASCII font at all, so these 3 glyphs are no
// longer needed for that purpose. Left in place anyway (harmless, already
// verified, and this project's own standing practice doesn't strip a
// small font extension just because its original motivating call site
// went away) in case some future text here ever needs them.
int yewAsciiPattern[120] = {
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x11, 0x1f, 0x00,
    0x00, 0x00, 0x1f, 0x00, 0x1d, 0x15, 0x17, 0x00,
    0x15, 0x15, 0x1f, 0x00, 0x07, 0x04, 0x1f, 0x00,
    0x17, 0x15, 0x1d, 0x00, 0x1f, 0x15, 0x1d, 0x00,
    0x01, 0x1d, 0x03, 0x00, 0x1f, 0x15, 0x1f, 0x00,
    0x17, 0x15, 0x1f, 0x00, 0x1f, 0x0e, 0x04, 0x00,
    0x1e, 0x09, 0x1e, 0x00, 0x0e, 0x11, 0x0a, 0x00,
    0x1f, 0x15, 0x11, 0x00, 0x1f, 0x05, 0x01, 0x00,
    0x0e, 0x11, 0x0d, 0x00, 0x11, 0x1f, 0x11, 0x00,
    0x1f, 0x06, 0x1f, 0x00, 0x1f, 0x01, 0x1e, 0x00,
    0x0e, 0x11, 0x0e, 0x00, 0x1f, 0x05, 0x07, 0x00,
    0x1f, 0x05, 0x1a, 0x00, 0x16, 0x15, 0x0d, 0x00,
    0x01, 0x1f, 0x01, 0x00, 0x1f, 0x10, 0x1f, 0x00,
    0x0f, 0x10, 0x0f, 0x00,
    // Not-upstream glyphs added by this port to fix the "YEWDOW"-can't-be-
    // spelled bug above - see this array's own comment.
    0x1f, 0x11, 0x0e, 0x00,  // D (index 27)
    0x1f, 0x0c, 0x1f, 0x00,  // W (index 28)
    0x03, 0x1c, 0x03, 0x00,  // Y (index 29)
};

// CharPattern - 158 map/sprite-tile glyphs (Char_End=0x9E), 2 bytes/glyph.
int yewCharPattern[316] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x88, 0x88, 0x11, 0x11, 0xa0, 0xbf, 0xef, 0x00,
    0xf8, 0x37, 0xf7, 0x0b, 0xa0, 0xbf, 0xef, 0x00,
    0x32, 0xb7, 0x7f, 0x21, 0x00, 0xfe, 0xfb, 0x0a,
    0xb0, 0x7f, 0x73, 0x8f, 0x00, 0xfe, 0xfb, 0x0a,
    0x12, 0xf7, 0x7b, 0x23, 0xe0, 0xff, 0xef, 0x00,
    0xf1, 0x3f, 0x77, 0x03, 0xe0, 0xff, 0xef, 0x00,
    0x73, 0x37, 0xff, 0x01, 0x00, 0xbe, 0xbf, 0x0e,
    0x30, 0x77, 0xf3, 0x1f, 0x00, 0xbe, 0xbf, 0x0e,
    0x10, 0xff, 0x73, 0x37, 0xa8, 0xaf, 0xef, 0x08,
    0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e, 0xce, 0x00,
    0x32, 0xf7, 0xff, 0x02, 0x80, 0xfe, 0xfa, 0x8a,
    0x00, 0xfb, 0x37, 0x01, 0x00, 0xec, 0xe4, 0x04,
    0x20, 0xff, 0x7f, 0x23, 0xe8, 0xef, 0xef, 0x08,
    0x30, 0xf7, 0x37, 0x00, 0xc0, 0xce, 0xce, 0x00,
    0x71, 0xff, 0x7f, 0x01, 0x80, 0xbe, 0xbe, 0x8e,
    0x00, 0x73, 0x7f, 0x03, 0x00, 0x6c, 0x6c, 0x0c,
    0x10, 0xf7, 0xff, 0x17, 0x00, 0x2c, 0x2e, 0x84,
    0xb7, 0x7b, 0xb7, 0x7b, 0x48, 0xe2, 0xc2, 0x00,
    0xb7, 0x7b, 0xb7, 0x7b, 0xc0, 0x22, 0x22, 0x0c,
    0xd7, 0x57, 0x75, 0x7d, 0xc0, 0x22, 0x22, 0x0c,
    0xf5, 0x67, 0x76, 0x5f, 0x80, 0xec, 0x8a, 0x08,
    0x10, 0x73, 0x15, 0x01, 0x80, 0xa8, 0xce, 0x08,
    0x10, 0x51, 0x37, 0x01, 0x80, 0xec, 0xce, 0x08,
    0x10, 0x70, 0x07, 0x01, 0x80, 0xe0, 0x0e, 0x08,
    0x10, 0x73, 0x37, 0x01, 0x00, 0xaf, 0xa5, 0x05,
    0x80, 0x8f, 0x00, 0x00, 0x00, 0xff, 0x37, 0x01,
    0x80, 0x8f, 0x00, 0x00, 0x00, 0x1f, 0xa1, 0x04,
    0x80, 0x9f, 0x01, 0x00, 0x00, 0xff, 0xce, 0x08,
    0x80, 0x8f, 0x00, 0x00, 0xe4, 0xc0, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x24, 0xcc, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x8c, 0xce, 0xc2, 0x00,
    0x00, 0x03, 0x61, 0x69, 0xa4, 0xc4, 0xc2, 0x00,
    0x21, 0x01, 0x61, 0x69, 0x1f, 0x11, 0x11, 0xf1,
    0x8f, 0x88, 0x88, 0xf8, 0xc0, 0xd6, 0xef, 0x84,
    0xb7, 0xf7, 0xb6, 0x35, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
};

// TitleBytes - upstream's own real "YEWDOW" title-screen logo bitmap
// (Status.cpp's `Title()`), byte-diff-verified against the real upstream
// source via a small script. Unlike Cracky's own 6-letter/24-column-wide
// logo (TitleLength=6, filling the whole VVram width with TitleLeft=0),
// this game's own upstream `TitleLength` is 5, not 6 - 5 groups of 4x4
// VVram cells (80 values total), positioned starting at VVram column
// `TitleLeft = (VVramWidth - 4*TitleLength) / 2 = (24-20)/2 = 2` (derived
// directly from upstream's own compile-time constant, not guessed) rather
// than column 0 - see yewBeginTitle()'s own comment for how this is
// actually drawn. Every value here is a valid index into yewCharPattern[]'s
// own "logo" range (indices 0-15, the first 32 bytes of that table,
// confirmed byte-identical to Cracky's own copy and explicitly labelled
// "// logo" in the real upstream Chars.cpp) - the same shared dithered-
// block palette every other map tile in this game already draws through.
int yewTitleBytes[80] = {
    0x0f, 0x00, 0x0f, 0x00, 0x0d, 0x0a, 0x07, 0x08,
    0x00, 0x0f, 0x00, 0x0c, 0x00, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 0x0b, 0x0c, 0x03,
    0x07, 0x05, 0x0c, 0x03, 0x05, 0x01, 0x04, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x0f, 0x08, 0x07,
    0x03, 0x0f, 0x0c, 0x03, 0x05, 0x01, 0x00, 0x05,
    0x0f, 0x00, 0x00, 0x00, 0x0f, 0x08, 0x07, 0x0b,
    0x0f, 0x0c, 0x03, 0x0f, 0x05, 0x00, 0x05, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x03, 0x03, 0x0f,
    0x0c, 0x03, 0x03, 0x0f, 0x04, 0x05, 0x05, 0x01,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int yewFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int yewMelodyLoose[3] = { 1, YEW_A3, 0 };
int yewMelodyBeep[3] = { 1, YEW_A4, 0 };

int yewMelodyHit[17] = {
    1, YEW_F4, 1, YEW_G4, 1, YEW_A4, 1, YEW_B4, 1, YEW_C5,
    1, YEW_D5, 1, YEW_E5, 1, YEW_F5, 0,
};

int yewMelodyFreeze[17] = {
    1, YEW_F5, 1, YEW_E5, 1, YEW_D5, 1, YEW_C5, 1, YEW_B4,
    1, YEW_A4, 1, YEW_G4, 1, YEW_F4, 0,
};

int yewMelodyStart[23] = {
    YEW_N4, YEW_C5, YEW_N4, YEW_C5, YEW_N4, YEW_G4, YEW_N4, YEW_G4, YEW_N8L, YEW_E5,
    YEW_N8R, YEW_D5, YEW_N8L, YEW_E5, ( YEW_N8R + YEW_N4 ), YEW_D5, YEW_N4, YEW_D5, YEW_N2P, YEW_C5,
    YEW_N4, 0, 0,
};

int yewMelodyClear[27] = {
    YEW_N4, YEW_C4, YEW_N4, YEW_E4, ( YEW_N4 + YEW_N8L ), YEW_G4, YEW_N4, YEW_A4, YEW_N8R, YEW_A4,
    YEW_N8L, YEW_G4, YEW_N8R, YEW_F4, YEW_N8L, YEW_G4, YEW_N8R, YEW_G4, YEW_N8L, YEW_A4,
    YEW_N8R, YEW_B4, YEW_N2P, YEW_C5, YEW_N4, 0, 0,
};

int yewMelodyGameOver[27] = {
    YEW_N8L, YEW_C5, YEW_N8R, YEW_C5, YEW_N8L, YEW_B4, YEW_N8R, YEW_B4, YEW_N8L, YEW_A4,
    YEW_N8R, YEW_A4, YEW_N8L, YEW_G4, YEW_N8R, YEW_E4, YEW_N8L, YEW_G4, YEW_N8R, YEW_G4,
    YEW_N8L, YEW_A4, YEW_N8R, YEW_B4, YEW_N2, YEW_C5, 0,
};

// StartBGM()'s voice A - a real, genuine loop (terminated with the "repeat
// from index 0" sentinel 255, matching upstream's own literal 0xff and
// this project's own established Cracky precedent for the identical
// looping-BGM shape).
int yewMelodyBgm1[131] = {
    YEW_N4, YEW_C5, YEW_N4, YEW_C5, YEW_N4, YEW_G4, YEW_N4, YEW_G4, YEW_N8L, YEW_E5,
    YEW_N8R, YEW_D5, YEW_N8L, YEW_E5, YEW_N2, YEW_D5, YEW_N8R, 0, YEW_N4, YEW_E5,
    YEW_N4, YEW_G5, YEW_N8L, YEW_D5, YEW_N8R, YEW_C5, YEW_N8L, YEW_D5, YEW_N1, YEW_E5,
    YEW_N8R, 0, YEW_N4, YEW_F5, YEW_N4, YEW_F5, YEW_N4, YEW_E5, YEW_N4, YEW_E5,
    YEW_N8L, YEW_D5, YEW_N8R, YEW_D5, YEW_N8L, YEW_D5, YEW_N2, YEW_E5, YEW_N8R, 0,
    YEW_N4, YEW_F5, YEW_N4, YEW_F5, YEW_N4, YEW_E5, YEW_N4, YEW_E5, YEW_N8L, YEW_G5,
    YEW_N8R, YEW_G5, YEW_N8L, YEW_G5, YEW_N2, YEW_D5, YEW_N8R, 0, YEW_N4, YEW_C5,
    YEW_N4, YEW_C5, YEW_N4, YEW_G4, YEW_N4, YEW_G4, YEW_N8L, YEW_E5, YEW_N8R, YEW_D5,
    YEW_N8L, YEW_E5, YEW_N2, YEW_D5, YEW_N8R, 0, YEW_N4, YEW_E5, YEW_N4, YEW_G5,
    YEW_N8L, YEW_D5, YEW_N8R, YEW_C5, YEW_N8L, YEW_D5, YEW_N1, YEW_E5, YEW_N8R, 0,
    YEW_N4, YEW_F5, YEW_N4, YEW_F5, YEW_N4, YEW_E5, YEW_N4, YEW_E5, YEW_N8L, YEW_G5,
    YEW_N8R, YEW_G5, YEW_N8L, YEW_G5, ( YEW_N8R + YEW_N4 ), YEW_E5, YEW_N4, YEW_D5, YEW_N4, YEW_C5,
    YEW_N4, YEW_G4, YEW_N8L, YEW_C5, YEW_N4, YEW_D5, YEW_N1, YEW_C5, YEW_N8R, 0,
    255,
};

// StartBGM()'s voice B - same looping shape as voice A.
int yewMelodyBgm2[229] = {
    YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N4, YEW_C4,
    YEW_N8L, 0, YEW_N8R, YEW_C4, YEW_N8L, 0, YEW_N8R, YEW_C4, YEW_N8L, 0,
    YEW_N8R, YEW_C4, YEW_N8L, YEW_C4, YEW_N8R, YEW_D4, YEW_N8L, YEW_C4, YEW_N8R, YEW_D4S,
    YEW_N8L, YEW_E4, YEW_N8R, YEW_B3, YEW_N8L, YEW_E4, YEW_N8R, 0, YEW_N4, YEW_E4,
    YEW_N8L, 0, YEW_N4, YEW_A3, YEW_N8R, YEW_A3, YEW_N8L, 0, YEW_N8R, YEW_A3,
    YEW_N8L, YEW_A3, YEW_N8R, 0, YEW_N8L, YEW_A3, YEW_N8R, 0, YEW_N8L, YEW_F3,
    YEW_N8R, YEW_C4, YEW_N8L, YEW_F3, YEW_N8R, 0, YEW_N4, YEW_F3, YEW_N8L, 0,
    YEW_N4, YEW_D4, YEW_N8R, YEW_D4, YEW_N8L, 0, YEW_N8R, YEW_D4, YEW_N8L, YEW_D4,
    YEW_N8R, 0, YEW_N8L, YEW_D4, YEW_N8R, 0, YEW_N8L, YEW_F3, YEW_N8R, 0,
    YEW_N8L, YEW_F3, YEW_N8R, 0, YEW_N4, YEW_F3, YEW_N8L, 0, YEW_N4, YEW_G3,
    YEW_N8R, YEW_G3, YEW_N8L, 0, YEW_N8R, YEW_D4, YEW_N8L, YEW_G3, YEW_N8R, YEW_D4,
    YEW_N8L, YEW_G3, YEW_N8R, 0, YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N8L, YEW_C4,
    YEW_N8R, 0, YEW_N4, YEW_C4, YEW_N8L, 0, YEW_N8R, YEW_C4, YEW_N8L, 0,
    YEW_N8R, YEW_C4, YEW_N8L, 0, YEW_N8R, YEW_C4, YEW_N8L, YEW_C4, YEW_N8R, YEW_D4,
    YEW_N8L, YEW_C4, YEW_N8R, YEW_D4S, YEW_N8L, YEW_E4, YEW_N8R, YEW_B3, YEW_N8L, YEW_E4,
    YEW_N8R, 0, YEW_N4, YEW_E4, YEW_N8L, 0, YEW_N4, YEW_A3, YEW_N8R, YEW_A3,
    YEW_N8L, 0, YEW_N8R, YEW_E3, YEW_N8L, YEW_A3, YEW_N8R, YEW_E3, YEW_N8L, YEW_A3,
    YEW_N8R, 0, YEW_N8L, YEW_F3, YEW_N8R, 0, YEW_N8L, YEW_F3, YEW_N8R, 0,
    YEW_N4, YEW_F3, YEW_N8L, 0, YEW_N4, YEW_E3, YEW_N8R, YEW_E3, YEW_N8L, 0,
    YEW_N8R, YEW_E3, YEW_N8L, YEW_E3, YEW_N8R, 0, YEW_N8L, YEW_E3, YEW_N8R, 0,
    YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N4, YEW_G3,
    YEW_N8L, 0, YEW_N4, YEW_C4, YEW_N8R, YEW_C4, YEW_N8L, 0, YEW_N8R, YEW_C4,
    YEW_N8L, YEW_C4, YEW_N8R, 0, YEW_N8L, YEW_C4, YEW_N8R, 0, 255,
};

// Point popup score values, indexed by "match streak rate" (0-3).
int yewPointValues[4] = { 10, 20, 40, 80 };

// Per-stage data, flattened from upstream's own `struct Stage { start,
// items[8], carCount, pCars, monsterCount, pMonsters, bytes[21] }` array +
// separate CarsN/EnemiesN arrays into parallel fixed arrays (avoids porting
// a struct-with-a-real-pointer-member, matching this project's own
// "flatten to plain arrays" precedent already established by Cracky's own
// `crkStageEnemies`).
int yewStageStart[7] = { 0x64, 0xa5, 0x51, 0x42, 0xa3, 0x42, 0x84 };

int yewStageItems[7][8] = {
    { 0x42, 0xa4, 0x23, 0x33, 0x51, 0x62, 0x11, 0x25 },
    { 0x20, 0x91, 0x35, 0x56, 0x44, 0x36, 0x70, 0xa3 },
    { 0x83, 0x56, 0x52, 0x23, 0xb3, 0x84, 0x44, 0xa4 },
    { 0x11, 0x43, 0x73, 0x16, 0x51, 0x61, 0x82, 0x65 },
    { 0xb1, 0xb2, 0x63, 0x85, 0x23, 0x53, 0x21, 0x35 },
    { 0x32, 0x93, 0x95, 0xa5, 0x35, 0x45, 0x43, 0x53 },
    { 0x23, 0x96, 0x40, 0x33, 0x52, 0x82, 0x91, 0x22 },
};

// Each stage's own car/monster start bytes, packed the same
// `( pos << 4 ) | floor` way as yewStageStart, padded to
// YEW_MAX_CAR_COUNT/YEW_MAX_MONSTER_COUNT(2) with unused trailing 0s.
int yewStageCars[7][4] = {
    { 0x14, 0x00, 0x00, 0x00 },
    { 0x55, 0x00, 0x00, 0x00 },
    { 0x91, 0x00, 0x00, 0x00 },
    { 0x71, 0x00, 0x00, 0x00 },
    { 0x94, 0x00, 0x00, 0x00 },
    { 0x23, 0x65, 0x00, 0x00 },
    { 0x44, 0x56, 0x00, 0x00 },
};

int yewStageMonsters[7][4] = {
    { 0xa1, 0x00, 0x00, 0x00 },
    { 0x22, 0x00, 0x00, 0x00 },
    { 0x74, 0x36, 0x00, 0x00 },
    { 0x91, 0x46, 0x00, 0x00 },
    { 0x84, 0x16, 0x00, 0x00 },
    { 0x15, 0x86, 0x00, 0x00 },
    { 0xb4, 0x06, 0x00, 0x00 },
};

int yewStageCarCount[7] = { 1, 1, 1, 1, 1, 2, 2 };
int yewStageMonCount[7] = { 1, 1, 2, 2, 2, 2, 2 };

// Raw per-stage cell map, 2 bits/cell, 4 cells/byte (3 bytes per 12-column
// row x 7 rows = 21 bytes) - unpacked into the runtime 4-bit-per-cell
// yewCellMap by yewUnpackStageBytes() every time a stage begins, matching
// upstream's own InitTrying()-embedded unpack loop exactly.
int yewStageBytes[7][21] = {
    {
        0x55, 0x55, 0x55, 0x01, 0x40, 0x44, 0x51,
        0x40, 0x40, 0x01, 0x00, 0x40, 0x01, 0x40,
        0x44, 0x01, 0x02, 0x40, 0x55, 0x55, 0x55,
    },
    {
        0x01, 0x00, 0x00, 0x40, 0x04, 0x00, 0x01,
        0x00, 0x00, 0x15, 0x14, 0x44, 0x15, 0x04,
        0x44, 0x00, 0x00, 0x40, 0x05, 0x50, 0x51,
    },
    {
        0x00, 0x00, 0x15, 0x44, 0x01, 0x00, 0x40,
        0x00, 0x21, 0x00, 0x10, 0x00, 0x55, 0x18,
        0x00, 0x05, 0x50, 0x04, 0x15, 0x00, 0x00,
    },
    {
        0x55, 0x05, 0x40, 0x01, 0x00, 0x40, 0x01,
        0x00, 0x40, 0x10, 0x08, 0x50, 0x11, 0x44,
        0x01, 0x61, 0x05, 0x00, 0x00, 0x04, 0x91,
    },
    {
        0x50, 0x15, 0x00, 0x44, 0x64, 0x19, 0x14,
        0x04, 0x10, 0x04, 0x00, 0x00, 0x14, 0x00,
        0x50, 0x04, 0x68, 0x54, 0x50, 0x55, 0x55,
    },
    {
        0x51, 0x55, 0x01, 0x91, 0x65, 0x22, 0x11,
        0x00, 0x00, 0x05, 0x00, 0x11, 0x51, 0x54,
        0x19, 0x11, 0x44, 0x01, 0x59, 0x15, 0x94,
    },
    {
        0x01, 0x50, 0x00, 0x09, 0x00, 0x10, 0x01,
        0x20, 0x10, 0x01, 0x42, 0x14, 0x01, 0x00,
        0x10, 0x01, 0x04, 0x40, 0x14, 0x01, 0x51,
    },
};

// -----------------------------------------------------------------------------
//   Structs - flattened composition, see this file's own header comment.
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y, sprite;
    int dx, dy;
    int status;
    int clock;
} YewActor;

typedef struct
{
    int x, y, sprite;
    int dx, dy;
    int status;
    int clock;
    int startX, startY;
} YewMonster;

typedef struct
{
    int x, y, sprite;
    int clock;
} YewPoint;

typedef struct
{
    int x, y;
} YewItem;

typedef struct
{
    int x, y, code;
} YewSprite;

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int yewScore;
int yewRemainCount;
int yewCurrentStage;
int yewStageTime;
int yewMaxTime;
int yewStageIndex;
int yewClock;
int yewMonsterNum;
int yewTimeDenom;

int yewCellMap[YEW_CELLMAP_BYTES];
int yewVVram[YEW_VVRAM_HEIGHT][YEW_VVRAM_WIDTH];

YewSprite yewSprites[YEW_SPRITE_END];

YewActor yewMan;
int yewArrowX;
int yewArrowY;

YewActor yewCars[YEW_MAX_CAR_COUNT];
int yewLostCarIndex;

YewMonster yewMonsters[YEW_MAX_MONSTER_COUNT];

YewItem yewItems[YEW_MAX_ITEM_COUNT];
int yewItemCount;
int yewLastType;
int yewRate;
int yewItemClock;

YewPoint yewPoints[YEW_POINT_COUNT];

// status-text + title/overlay text grid - see this file's own header
// comment for why this covers the WHOLE screen width, not just the status
// column the way gameCracky.c's own crkStatusChar does.
int yewTextChar[8][32];
bool yewTextActive[8][32];

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of YEW_TICK_DIVISOR.
int yewSeqMelody[3];
int yewSeqPos[3];
int yewSeqWait[3];
int yewSeqActive[3];

#define YEW_STATE_TITLE 0
#define YEW_STATE_START_JINGLE 1
#define YEW_STATE_PLAYING 2
#define YEW_STATE_LOSE_CAR_ANIM 3
#define YEW_STATE_TIME_UP_WAIT 4
#define YEW_STATE_GAMEOVER_JINGLE 5
#define YEW_STATE_CLEAR_WAIT 6
#define YEW_STATE_CLEAR_JINGLE 7
#define YEW_STATE_BONUS_TALLY 8
int yewState;
int yewWaitFrames;
int yewAnimStep;
int yewSelection;
bool yewSelectionChanged;
bool yewPrevLeft;
bool yewPrevRight;
bool yewPrevUp;
bool yewPrevDown;
bool yewPrevFire;

int yewTickCounter;


// -----------------------------------------------------------------------------
//   Math.cpp - upstream's own Rnd()/RndIndex are entirely commented out/dead
//   (this game uses no randomness anywhere) - only Abs() is real.
// -----------------------------------------------------------------------------

int yewAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Sound sequencer - defined early (right after Math), since yewHitMan()
//   below (needed by both the Car and Monster sections much later in this
//   file) already needs to fire the "freeze" melody, and this dialect
//   requires strict top-to-bottom define-before-use ordering with no
//   forward declarations. None of these functions have any dependency on
//   Movable/Actor/Sprite/etc, only on the melody data tables already
//   defined above and md_playTone()/md_stopTone() (external).
// -----------------------------------------------------------------------------

int yewMelodyLength( int id )
{
    if( id == YEW_MELODY_LOOSE ) return 3;
    if( id == YEW_MELODY_BEEP ) return 3;
    if( id == YEW_MELODY_HIT ) return 17;
    if( id == YEW_MELODY_FREEZE ) return 17;
    if( id == YEW_MELODY_START ) return 23;
    if( id == YEW_MELODY_CLEAR ) return 27;
    if( id == YEW_MELODY_GAMEOVER ) return 27;
    if( id == YEW_MELODY_BGM1 ) return 131;
    if( id == YEW_MELODY_BGM2 ) return 229;
    return 0;
}

int yewMelodyValue( int id, int idx )
{
    if( id == YEW_MELODY_LOOSE ) return yewMelodyLoose[ idx ];
    if( id == YEW_MELODY_BEEP ) return yewMelodyBeep[ idx ];
    if( id == YEW_MELODY_HIT ) return yewMelodyHit[ idx ];
    if( id == YEW_MELODY_FREEZE ) return yewMelodyFreeze[ idx ];
    if( id == YEW_MELODY_START ) return yewMelodyStart[ idx ];
    if( id == YEW_MELODY_CLEAR ) return yewMelodyClear[ idx ];
    if( id == YEW_MELODY_GAMEOVER ) return yewMelodyGameOver[ idx ];
    if( id == YEW_MELODY_BGM1 ) return yewMelodyBgm1[ idx ];
    if( id == YEW_MELODY_BGM2 ) return yewMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/YEW_TEMPO(170) = 1.7647 real 60Hz ticks - see header comment.
int yewNoteFrames( int length )
{
    return (int)( length * ( 300.0 / 170.0 ) + 0.5 );
}

void yewStartSeq( int channel, int melodyId )
{
    yewSeqMelody[ channel ] = melodyId;
    yewSeqPos[ channel ] = 0;
    yewSeqWait[ channel ] = 0;
    yewSeqActive[ channel ] = 1;
}

void yewStopSeq( int channel )
{
    yewSeqActive[ channel ] = 0;
    yewSeqMelody[ channel ] = YEW_MELODY_NONE;
}

bool yewSeqPlaying( int channel )
{
    return yewSeqActive[ channel ] != 0;
}

void yewAdvanceOneSeq( int channel )
{
    int length, note;

    if( yewSeqActive[ channel ] == 0 ) return;

    if( yewSeqWait[ channel ] > 0 )
    {
        yewSeqWait[ channel ] = yewSeqWait[ channel ] - 1;
        return;
    }

    length = yewMelodyValue( yewSeqMelody[ channel ], yewSeqPos[ channel ] );
    if( length == 0 )
    {
        yewStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        yewSeqPos[ channel ] = 0;
        length = yewMelodyValue( yewSeqMelody[ channel ], 0 );
    }
    note = yewMelodyValue( yewSeqMelody[ channel ], yewSeqPos[ channel ] + 1 );
    yewSeqPos[ channel ] = yewSeqPos[ channel ] + 2;
    yewSeqWait[ channel ] = yewNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)yewFrequencies[ note - 1 ], (float)yewSeqWait[ channel ] / 60.0 );
}

void yewAdvanceSound()
{
    yewAdvanceOneSeq( 0 );
    yewAdvanceOneSeq( 1 );
    yewAdvanceOneSeq( 2 );
}

void yewStartBgm()
{
    yewStartSeq( 1, YEW_MELODY_BGM1 );
    yewStartSeq( 2, YEW_MELODY_BGM2 );
}

void yewStopBgm()
{
    yewStopSeq( 1 );
    yewStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Stage.cpp - cell access (runtime CellMap is 4 bits/cell, 2 cells/byte -
//   a DIFFERENT packing than yewStageBytes' own 2-bits/cell raw storage,
//   see yewUnpackStageBytes() below for the conversion between the two).
// -----------------------------------------------------------------------------

int yewCellPtr( int x, int y )
{
    return ( y * ( YEW_MAP_WIDTH / YEW_MAP_WIDTH_PER_BYTE ) ) + ( x / YEW_MAP_WIDTH_PER_BYTE );
}

int yewGetCell( int x, int y )
{
    int b;
    b = yewCellMap[ yewCellPtr( x, y ) ];
    return ( b >> ( ( x % YEW_MAP_WIDTH_PER_BYTE ) * 4 ) ) & 0x0f;
}

void yewSetCell( int x, int y, int cell )
{
    int idx, shift;
    idx = yewCellPtr( x, y );
    shift = ( x % YEW_MAP_WIDTH_PER_BYTE ) * 4;
    yewCellMap[ idx ] = ( yewCellMap[ idx ] & ~( 0x0f << shift ) ) | ( ( cell & 0x0f ) << shift );
}

void yewEraseCell( int x, int y )
{
    yewSetCell( x, y, YEW_CELL_SPACE );
}


// -----------------------------------------------------------------------------
//   Movable.cpp / Actor.cpp - shared primitives, taking explicit x/y (or
//   individual field pointers for the ones that mutate more than one field)
//   rather than a typed struct pointer, so they work identically for both
//   YewActor's and YewMonster's own fields - see this file's own header
//   comment.
// -----------------------------------------------------------------------------

void yewLocateXY( int b, int* outX, int* outY )
{
    *outX = ( b >> 3 ) & 0xfe;
    *outY = ( b & 15 ) << 1;
}

bool yewIsNear( int x1, int y1, int x2, int y2 )
{
    return
        x1 + YEW_HIT_RANGE >= x2 && x2 + YEW_HIT_RANGE >= x1 &&
        y1 + YEW_HIT_RANGE >= y2 && y2 + YEW_HIT_RANGE >= y1;
}

bool yewIsOnCellGrid( int x, int y )
{
    return ( ( x | y ) & YEW_MAP_MASK ) == 0;
}

bool yewIsOnCoordGrid( int x, int y )
{
    // YEW_COORD_MASK is always 0 here (YEW_COORD_SHIFT=0 makes upstream's
    // own generic sub-cell-precision parameterization degenerate, the same
    // finding already documented in gameCracky.c's own header comment) -
    // this always evaluates true, kept as the real formula rather than
    // resolved to a literal `true`, matching this project's own
    // "#defines/formulas stay exactly as upstream wrote them" preference.
    return ( ( x | y ) & YEW_COORD_MASK ) == 0;
}

void yewMoveActor( int* pX, int* pY, int* pStatus, int dx, int dy )
{
    int seq;
    *pX = *pX + dx;
    *pY = *pY + dy;
    seq = ( *pX + *pY ) & 1;
    *pStatus = ( *pStatus & ~YEW_ACTOR_SEQ_MASK ) | seq;
}

int yewDirectionElements[8] = { -1, 0, 1, 0, 0, -1, 0, 1 };

void yewSetDirection( int* pStatus, int* pDx, int* pDy, int direction )
{
    *pStatus = ( *pStatus & ~YEW_ACTOR_DIRECTION_MASK ) | direction;
    *pDx = yewDirectionElements[ direction ];
    *pDy = yewDirectionElements[ direction + 1 ];
}

bool yewCanMoveTo( int x, int y )
{
    int cell;
    // Upstream's own x/y here are `byte` (uint8_t) and rely on unsigned
    // wraparound (a negative offset wraps to a large positive value,
    // safely failing the `>= MapWidth`/`>= MapHeight` check below) - this
    // port's plain, non-wrapping `int`s need an explicit lower-bound check
    // instead, the same fix shape already established project-wide for
    // this exact bug class - see this file's own header comment.
    if( x < 0 || y < 0 || x >= YEW_MAP_WIDTH || y >= YEW_MAP_HEIGHT )
      return false;
    cell = yewGetCell( x, y );
    return cell != YEW_CELL_WALL && cell != YEW_CELL_ROCK;
}

bool yewCanMove( int x, int y, int direction )
{
    int dx, dy, mapX, mapY;
    dx = yewDirectionElements[ direction ];
    dy = yewDirectionElements[ direction + 1 ];
    mapX = ( x >> YEW_MAP_SHIFT ) + dx;
    mapY = ( y >> YEW_MAP_SHIFT ) + dy;
    return yewCanMoveTo( mapX, mapY );
}

// HitMan() - checks whether a movable (a car or a monster) is near Man and,
// if so, freezes Man briefly. Declared early (needs only yewMan + yewIsNear
// + the freeze SFX melody id) since it's shared by both the Car and
// Monster sections below, and Man's own MoveMan() also needs Monster's
// HitManMonsters() - see the dependency-order note in this file's own
// header comment.
bool yewHitMan( int x, int y )
{
    if( ( yewMan.status & YEW_ACTOR_LIVE ) != 0 && yewIsNear( x, y, yewMan.x, yewMan.y ) )
    {
        yewMan.status = yewMan.status & ~YEW_ACTOR_LIVE;
        yewMan.clock = YEW_MAN_FREEZE_TIME;
        yewStartSeq( 0, YEW_MELODY_FREEZE );
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp / VVram.cpp - composites directly into yewVVram, matching
//   upstream (Movable x/y are already VVram-grid-cell coordinates, no
//   scaling needed - the same finding already made for Cracky's own
//   Movable coordinates).
// -----------------------------------------------------------------------------

void yewHideAllSprites()
{
    int i;
    for( i = 0; i < YEW_SPRITE_END; i = i + 1 )
      yewSprites[ i ].code = YEW_INVALID_CODE;
}

void yewHideSprite( int index )
{
    yewSprites[ index ].code = YEW_INVALID_CODE;
}

void yewShowSprite( int spriteSlot, int x, int y, int code )
{
    yewSprites[ spriteSlot ].x = x;
    yewSprites[ spriteSlot ].y = y + YEW_STAGE_TOP;
    yewSprites[ spriteSlot ].code = code;
}

void yewPutTileIntoVVram( int x, int y, int c )
{
    yewVVram[ y ][ x ] = c; c = c + 1;
    yewVVram[ y ][ x + 1 ] = c; c = c + 1;
    yewVVram[ y + 1 ][ x ] = c; c = c + 1;
    yewVVram[ y + 1 ][ x + 1 ] = c;
}

void yewEraseTileInVVram( int x, int y )
{
    yewVVram[ y ][ x ] = YEW_CHAR_SPACE;
    yewVVram[ y ][ x + 1 ] = YEW_CHAR_SPACE;
    yewVVram[ y + 1 ][ x ] = YEW_CHAR_SPACE;
    yewVVram[ y + 1 ][ x + 1 ] = YEW_CHAR_SPACE;
}

void yewDrawSprites()
{
    int s;
    for( s = 0; s < YEW_SPRITE_END; s = s + 1 )
    {
        if( yewSprites[ s ].code != YEW_INVALID_CODE )
        {
            int x, y, c, dy;
            x = yewSprites[ s ].x;
            y = yewSprites[ s ].y;
            c = yewSprites[ s ].code;
            for( dy = 0; dy < 2; dy = dy + 1 )
            {
                // A car lost off the left/top edge can have a genuinely
                // negative x/y at the moment its own loss animation shows
                // it (upstream's own equivalent bounds check relies on
                // byte wraparound here too - same fix shape as
                // yewCanMoveTo() above, explicit `>=0` guards instead).
                if( y + dy >= 0 && y + dy < YEW_VVRAM_HEIGHT )
                {
                    int dx;
                    for( dx = 0; dx < 2; dx = dx + 1 )
                    {
                        if( x + dx >= 0 && x + dx < YEW_VVRAM_WIDTH )
                          yewVVram[ y + dy ][ x + dx ] = c + dx;
                    }
                }
                c = c + 2;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status text + title/overlay text, all routed
//   through the unified yewTextChar/yewTextActive grid - see this file's
//   own header comment for why this differs from Cracky's own design.
// -----------------------------------------------------------------------------

int yewAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" (upstream's own 27-entry
    // linear search, direct port of PrintC()) plus 'D','W','Y' appended at
    // indices 27-29 - this port's own addition, fixing the missing-glyph
    // bug documented on yewAsciiPattern's own comment above (without these,
    // this game's own title word "YEWDOW" can't be spelled at all).
    int table[30] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V',
        'D', 'W', 'Y',
    };
    int i;
    for( i = 0; i < 30; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

int yewPrintC( int page, int col, int c )
{
    yewTextChar[ page ][ col ] = yewAsciiIndex( c );
    yewTextActive[ page ][ col ] = true;
    return col + 1;
}

int yewPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = yewPrintC( page, col, s[ i ] );
    return col;
}

void yewPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      yewPrintC( page, col, ' ' );
    else
      yewPrintC( page, col, d1 + '0' );
    yewPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void yewPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        yewPrintC( page, col, ' ' );
        if( d2 == 0 )
          yewPrintC( page, col + 1, ' ' );
        else
          yewPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        yewPrintC( page, col, d1 + '0' );
        yewPrintC( page, col + 1, d2 + '0' );
    }
    yewPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

int yewPrintNumber5( int page, int col, int w )
{
    int i, d, rem, div;
    bool zeroVisible;
    rem = w;
    div = 10000;
    zeroVisible = false;
    for( i = 0; i < 4; i = i + 1 )
    {
        d = rem / div;
        rem = rem % div;
        if( d == 0 && !zeroVisible )
          yewPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            yewPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    yewPrintC( page, col + 4, rem + '0' );
    return col + 5;
}

// Clears every map-region (char columns 0-23) text-overlay cell - called
// whenever a screen that used the overlay (the title screen, or a brief
// GAME OVER/TIME UP message) is being left, so the underlying map/blank
// background shows through again. Status columns (24-31) are never
// touched here - once yewPrintStatus() first marks them active, they stay
// active for the rest of the session.
void yewResetTextOverlays()
{
    int page, col;
    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < YEW_VVRAM_WIDTH; col = col + 1 )
          yewTextActive[ page ][ col ] = false;
    }
}

void yewPrintScore()
{
    int col;
    col = yewPrintNumber5( 1, 26, yewScore );
    yewPrintC( 1, col, '0' );
}

void yewPrintTime()
{
    yewPrintByteNumber3( 5, 29, yewStageTime );
}

void yewPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    yewPrintS( 0, 24, sScore, 5 );
    yewPrintS( 3, 24, sStage, 5 );
    yewPrintByteNumber2( 3, 30, yewCurrentStage + 1 );
    yewPrintS( 5, 24, sTime, 4 );

    // upstream draws a real 2x2 Char_Remain (Char_Man) map-tile icon here
    // via Put2C, repeated once per remaining life beyond the current one -
    // this port's status-text overlay only supports the ASCII font (not
    // the sprite/map-tile font), so simplified to a plain digit count,
    // matching this project's own established gameCracky.c precedent for
    // the identical situation.
    if( yewRemainCount > 1 )
    {
        i = yewRemainCount - 1;
        yewPrintC( 7, 27, i + '0' );
    }
    else
      yewPrintC( 7, 27, ' ' );

    yewPrintScore();
    yewPrintTime();
}

void yewPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    yewPrintS( 4, 8, s, 9 );
}

void yewPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    yewPrintS( 4, 9, s, 7 );
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void yewAddScore( int pts )
{
    yewScore = yewScore + pts;
    yewPrintScore();
}


// -----------------------------------------------------------------------------
//   Point.cpp - transient "+N" score popups.
// -----------------------------------------------------------------------------

void yewInitPoints()
{
    int i, sprite;
    sprite = YEW_SPRITE_POINT;
    for( i = 0; i < YEW_POINT_COUNT; i = i + 1 )
    {
        yewPoints[ i ].y = YEW_POINT_INVALID_Y;
        yewPoints[ i ].sprite = sprite;
        yewHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void yewStartPoint( int x, int y, int rate )
{
    int i;
    if( rate >= 4 ) rate = 3;
    yewAddScore( yewPointValues[ rate ] );
    for( i = 0; i < YEW_POINT_COUNT; i = i + 1 )
    {
        if( yewPoints[ i ].y == YEW_POINT_INVALID_Y )
        {
            yewPoints[ i ].x = x;
            yewPoints[ i ].y = y;
            yewPoints[ i ].clock = YEW_POINT_TIME << YEW_COORD_SHIFT;
            yewShowSprite( yewPoints[ i ].sprite, x, y, ( rate << 2 ) + YEW_CHAR_POINT );
            return;
        }
    }
}

void yewUpdatePoints()
{
    int i;
    for( i = 0; i < YEW_POINT_COUNT; i = i + 1 )
    {
        if( yewPoints[ i ].y != YEW_POINT_INVALID_Y )
        {
            if( yewPoints[ i ].clock == 0 )
            {
                yewPoints[ i ].y = YEW_POINT_INVALID_Y;
                yewHideSprite( yewPoints[ i ].sprite );
            }
            else
              yewPoints[ i ].clock = yewPoints[ i ].clock - 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Item.cpp - the 8 "flag" items cars must drive over to clear a stage.
// -----------------------------------------------------------------------------

void yewInitItems()
{
    int i;
    for( i = 0; i < YEW_MAX_ITEM_COUNT; i = i + 1 )
    {
        int b, x, y;
        b = yewStageItems[ yewStageIndex ][ i ];
        x = ( b >> 3 ) & 0xfe;
        y = ( b & 15 ) << 1;
        yewItems[ i ].x = x;
        yewItems[ i ].y = y;
        yewSetCell( x >> 1, y >> 1, YEW_CELL_ITEM );
    }
    yewItemCount = YEW_MAX_ITEM_COUNT;
    yewLastType = YEW_INVALID_TYPE;
    yewRate = 0;
}

void yewHitItems( int x, int y )
{
    int cell;
    x = x >> YEW_MAP_SHIFT;
    y = y >> YEW_MAP_SHIFT;
    cell = yewGetCell( x, y );
    if( cell == YEW_CELL_ITEM )
    {
        int index, i;
        index = 0;
        for( i = 0; i < YEW_MAX_ITEM_COUNT; i = i + 1 )
        {
            if( yewItems[ i ].x != YEW_INVALID_X && ( yewItems[ i ].x >> 1 ) == x && ( yewItems[ i ].y >> 1 ) == y )
            {
                int type;
                type = index >> 1;
                if( yewLastType != YEW_INVALID_TYPE )
                {
                    if( type == yewLastType )
                    {
                        yewLastType = YEW_INVALID_TYPE;
                        yewRate = yewRate + 1;
                    }
                    else
                    {
                        yewLastType = type;
                        yewRate = 0;
                    }
                }
                else
                  yewLastType = type;
                yewStartPoint( yewItems[ i ].x, yewItems[ i ].y, yewRate );
                yewEraseCell( x, y );
                yewItems[ i ].x = YEW_INVALID_X;
                yewItemCount = yewItemCount - 1;
                yewStartSeq( 0, YEW_MELODY_HIT );
            }
            index = index + 1;
        }
    }
}

void yewDrawItems()
{
    int i, index;
    index = 0;
    for( i = 0; i < YEW_MAX_ITEM_COUNT; i = i + 1 )
    {
        int type;
        type = index >> 1;
        if( yewItems[ i ].x != YEW_INVALID_X )
        {
            if( type != yewLastType || ( yewItemClock & YEW_COORD_RATE ) != 0 )
            {
                int c;
                c = ( ( index & 6 ) << 1 ) + YEW_CHAR_ITEM;
                yewPutTileIntoVVram( yewItems[ i ].x, yewItems[ i ].y + YEW_STAGE_TOP, c );
            }
        }
        index = index + 1;
    }
    yewItemClock = yewItemClock + 1;
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void yewShowMonster( YewMonster* pMonster )
{
    int pattern, status;
    status = pMonster->status;
    pattern = ( ( status & YEW_ACTOR_PATTERN_MASK ) << 2 ) + YEW_CHAR_MONSTER;
    yewShowSprite( pMonster->sprite, pMonster->x, pMonster->y, pattern );
}

void yewDecideDirection( YewMonster* pMonster )
{
    int directions[4];
    int verticalIdx, horizontalIdx;
    int i, direction;
    int targetX, targetY;

    if( ( yewMan.status & YEW_ACTOR_LIVE ) != 0 && ( pMonster->status & YEW_MONSTER_RETURN ) == 0 )
    {
        targetX = yewMan.x;
        targetY = yewMan.y;
    }
    else
    {
        targetX = pMonster->startX;
        targetY = pMonster->startY;
    }

    if( yewAbs( targetX, pMonster->x ) > yewAbs( targetY, pMonster->y ) )
    {
        if( targetX < pMonster->x )
        {
            if( pMonster->dx <= 0 )
            {
                directions[ 0 ] = YEW_DIRECTION_LEFT;
                directions[ 3 ] = YEW_DIRECTION_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = YEW_DIRECTION_RIGHT;
                directions[ 3 ] = YEW_DIRECTION_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                directions[ 0 ] = YEW_DIRECTION_RIGHT;
                directions[ 3 ] = YEW_DIRECTION_LEFT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = YEW_DIRECTION_LEFT;
                directions[ 3 ] = YEW_DIRECTION_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( targetY < pMonster->y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            directions[ verticalIdx ] = YEW_DIRECTION_UP;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = YEW_DIRECTION_DOWN;
        }
        else
        {
            directions[ verticalIdx ] = YEW_DIRECTION_DOWN;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = YEW_DIRECTION_UP;
        }
    }
    else
    {
        if( targetY < pMonster->y )
        {
            if( pMonster->dy <= 0 )
            {
                directions[ 0 ] = YEW_DIRECTION_UP;
                directions[ 3 ] = YEW_DIRECTION_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = YEW_DIRECTION_DOWN;
                directions[ 3 ] = YEW_DIRECTION_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                directions[ 0 ] = YEW_DIRECTION_DOWN;
                directions[ 3 ] = YEW_DIRECTION_UP;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = YEW_DIRECTION_UP;
                directions[ 3 ] = YEW_DIRECTION_DOWN;
                horizontalIdx = 0;
            }
        }
        // Upstream compares `targetX < pMonster->_._.y` here too (X
        // against Y) - the same real upstream quirk already found and
        // faithfully preserved in this project's own Cracky port (see
        // gameCracky.c's own crkDecideDirection), not a transcription slip.
        if( ( targetX < pMonster->y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            directions[ horizontalIdx ] = YEW_DIRECTION_LEFT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = YEW_DIRECTION_RIGHT;
        }
        else
        {
            directions[ horizontalIdx ] = YEW_DIRECTION_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = YEW_DIRECTION_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        if( yewCanMove( pMonster->x, pMonster->y, direction ) )
        {
            yewSetDirection( &pMonster->status, &pMonster->dx, &pMonster->dy, direction );
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void yewInitMonsters()
{
    int i, sprite, count;
    count = yewStageMonCount[ yewStageIndex ];
    sprite = YEW_SPRITE_MONSTER;
    for( i = 0; i < count; i = i + 1 )
    {
        int x, y;
        yewMonsters[ i ].status = YEW_ACTOR_LIVE;
        yewMonsters[ i ].sprite = sprite;
        yewMonsters[ i ].dx = 0;
        yewMonsters[ i ].dy = 0;
        yewLocateXY( yewStageMonsters[ yewStageIndex ][ i ], &x, &y );
        yewMonsters[ i ].x = x;
        yewMonsters[ i ].y = y;
        yewDecideDirection( &yewMonsters[ i ] );
        yewShowMonster( &yewMonsters[ i ] );
        yewMonsters[ i ].startX = yewMonsters[ i ].x;
        yewMonsters[ i ].startY = yewMonsters[ i ].y;
        sprite = sprite + 1;
    }
    for( i = count; i < YEW_MAX_MONSTER_COUNT; i = i + 1 )
    {
        yewMonsters[ i ].status = 0;
        yewMonsters[ i ].sprite = sprite;
        yewHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void yewMoveMonsters()
{
    int i;
    for( i = 0; i < YEW_MAX_MONSTER_COUNT; i = i + 1 )
    {
        YewMonster* pMonster;
        int status;
        pMonster = &yewMonsters[ i ];
        status = pMonster->status;
        if( ( status & YEW_ACTOR_LIVE ) != 0 )
        {
            if( ( status & YEW_MONSTER_FREEZE ) == 0 )
            {
                if( yewIsOnCellGrid( pMonster->x, pMonster->y ) )
                  yewDecideDirection( pMonster );
                yewMoveActor( &pMonster->x, &pMonster->y, &pMonster->status, pMonster->dx, pMonster->dy );
                yewShowMonster( pMonster );
                if( yewIsOnCoordGrid( pMonster->x, pMonster->y ) )
                {
                    if( yewHitMan( pMonster->x, pMonster->y ) )
                      pMonster->status = pMonster->status | YEW_MONSTER_RETURN;
                    else if( ( pMonster->status & YEW_MONSTER_RETURN ) != 0 &&
                             pMonster->x == pMonster->startX && pMonster->y == pMonster->startY )
                      pMonster->status = pMonster->status & ~YEW_MONSTER_RETURN;
                }
            }
            else
            {
                if( ( pMonster->clock & YEW_COORD_MASK ) == 0 )
                {
                    int patterns[4] = {
                        YEW_CHAR_MONSTER + 0 * 4, YEW_CHAR_MONSTER + 4 * 4,
                        YEW_CHAR_MONSTER + 2 * 4, YEW_CHAR_MONSTER + 6 * 4,
                    };
                    int pattern;
                    pattern = patterns[ ( pMonster->clock >> YEW_COORD_SHIFT ) & 3 ];
                    yewShowSprite( pMonster->sprite, pMonster->x, pMonster->y, pattern );
                }
                pMonster->clock = pMonster->clock - 1;
                if( pMonster->clock == 0 )
                {
                    pMonster->status = pMonster->status & ~YEW_MONSTER_FREEZE;
                    yewShowMonster( pMonster );
                }
            }
        }
    }
}

void yewHitCarMonsters( int x, int y )
{
    int i;
    for( i = 0; i < YEW_MAX_MONSTER_COUNT; i = i + 1 )
    {
        YewMonster* pMonster;
        pMonster = &yewMonsters[ i ];
        if( ( pMonster->status & YEW_ACTOR_LIVE ) != 0 &&
            ( pMonster->status & YEW_MONSTER_FREEZE ) == 0 &&
            yewIsNear( x, y, pMonster->x, pMonster->y ) )
        {
            pMonster->status = pMonster->status | YEW_MONSTER_FREEZE;
            pMonster->clock = YEW_MONSTER_FREEZE_TIME;
            yewStartPoint( pMonster->x, pMonster->y, 2 );
            yewStartSeq( 0, YEW_MELODY_HIT );
        }
    }
}

void yewHitManMonsters()
{
    int i;
    for( i = 0; i < YEW_MAX_MONSTER_COUNT; i = i + 1 )
    {
        YewMonster* pMonster;
        pMonster = &yewMonsters[ i ];
        if( ( pMonster->status & YEW_ACTOR_LIVE ) != 0 &&
            ( pMonster->status & YEW_MONSTER_FREEZE ) == 0 &&
            yewHitMan( pMonster->x, pMonster->y ) )
          pMonster->status = pMonster->status | YEW_MONSTER_RETURN;
    }
}


// -----------------------------------------------------------------------------
//   Car.cpp - autonomously-driving cars, redirected by player-placed arrows.
// -----------------------------------------------------------------------------

void yewShowCar( YewActor* pCar )
{
    int pattern, status;
    status = pCar->status;
    pattern = ( ( status & YEW_ACTOR_DIRECTION_MASK ) << 1 ) + YEW_CHAR_CAR;
    yewShowSprite( pCar->sprite, pCar->x, pCar->y, pattern );
}

void yewInitCars()
{
    int i, sprite, count;
    count = yewStageCarCount[ yewStageIndex ];
    sprite = YEW_SPRITE_CAR;
    for( i = 0; i < count; i = i + 1 )
    {
        int x, y;
        yewCars[ i ].status = YEW_ACTOR_LIVE;
        yewCars[ i ].sprite = sprite;
        yewLocateXY( yewStageCars[ yewStageIndex ][ i ], &x, &y );
        yewCars[ i ].x = x;
        yewCars[ i ].y = y;
        if( yewCars[ i ].x < YEW_STAGE_WIDTH / 2 * YEW_COORD_RATE )
          yewSetDirection( &yewCars[ i ].status, &yewCars[ i ].dx, &yewCars[ i ].dy, YEW_DIRECTION_RIGHT );
        else
          yewSetDirection( &yewCars[ i ].status, &yewCars[ i ].dx, &yewCars[ i ].dy, YEW_DIRECTION_LEFT );
        yewShowCar( &yewCars[ i ] );
        sprite = sprite + 1;
    }
    for( i = count; i < YEW_MAX_CAR_COUNT; i = i + 1 )
    {
        yewCars[ i ].status = 0;
        yewCars[ i ].sprite = sprite;
        yewHideSprite( sprite );
        sprite = sprite + 1;
    }
    yewLostCarIndex = -1;
}

void yewMoveCars()
{
    int carIdx;
    for( carIdx = 0; carIdx < YEW_MAX_CAR_COUNT; carIdx = carIdx + 1 )
    {
        YewActor* pCar;
        pCar = &yewCars[ carIdx ];
        if( ( pCar->status & YEW_ACTOR_LIVE ) == 0 ) continue;

        if( yewIsOnCellGrid( pCar->x, pCar->y ) )
        {
            int x, y;
            x = pCar->x >> YEW_MAP_SHIFT;
            y = pCar->y >> YEW_MAP_SHIFT;

            if( x < YEW_MAP_WIDTH && y < YEW_MAP_HEIGHT )
            {
                int cell;
                cell = yewGetCell( x, y );
                if( ( cell & 0xc ) == YEW_CELL_ARROW )
                {
                    int direction;
                    direction = ( cell & 3 ) << 1;
                    yewSetDirection( &pCar->status, &pCar->dx, &pCar->dy, direction );
                    yewEraseCell( x, y );
                    yewAddScore( 2 );
                    yewShowCar( pCar );
                }
            }

            {
                bool keepTurning;
                keepTurning = true;
                while( keepTurning )
                {
                    int nextX, nextY;
                    keepTurning = false;
                    nextX = x + pCar->dx;
                    nextY = y + pCar->dy;
                    // Same unsigned-wraparound-reliance fix as
                    // yewCanMoveTo() - see this file's own header comment.
                    if( nextX >= 0 && nextY >= 0 && nextX < YEW_MAP_WIDTH && nextY < YEW_MAP_HEIGHT )
                    {
                        int cell;
                        cell = yewGetCell( nextX, nextY );
                        if( cell == YEW_CELL_WALL )
                        {
                            int direction;
                            direction = pCar->status & YEW_ACTOR_DIRECTION_MASK;
                            yewSetDirection( &pCar->status, &pCar->dx, &pCar->dy, direction ^ 2 );
                            yewEraseCell( nextX, nextY );
                            yewAddScore( 1 );
                            yewShowCar( pCar );
                            keepTurning = true;
                        }
                        else if( cell == YEW_CELL_ROCK )
                        {
                            pCar->status = pCar->status & ~YEW_ACTOR_LIVE;
                            yewLostCarIndex = carIdx;
                            return;
                        }
                    }
                }
            }
        }

        yewMoveActor( &pCar->x, &pCar->y, &pCar->status, pCar->dx, pCar->dy );
        if( yewIsOnCellGrid( pCar->x, pCar->y ) )
          yewHitItems( pCar->x, pCar->y );
        // upstream's own Car_OverLeft flag is set here too but never read
        // anywhere else in the real source (confirmed by grep) - dropped.

        if( pCar->x == -YEW_COORD_RATE || pCar->x == YEW_RANGE_X ||
            pCar->y == -YEW_COORD_RATE || pCar->y == YEW_RANGE_Y )
        {
            pCar->status = pCar->status & ~YEW_ACTOR_LIVE;
            yewLostCarIndex = carIdx;
            return;
        }

        if( ( pCar->status & YEW_ACTOR_LIVE ) != 0 )
        {
            yewShowCar( pCar );
            if( yewIsOnCoordGrid( pCar->x, pCar->y ) )
            {
                yewHitMan( pCar->x, pCar->y );
                yewHitCarMonsters( pCar->x, pCar->y );
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp - the player character.
// -----------------------------------------------------------------------------

void yewShowMan()
{
    int status, pattern;
    status = yewMan.status;
    if( ( status & YEW_MAN_FLASH ) != 0 )
    {
        yewShowSprite( yewMan.sprite, yewMan.x, yewMan.y, YEW_CHAR_FLUSH );
        yewMan.status = yewMan.status & ~YEW_MAN_FLASH;
        return;
    }
    pattern = ( ( status & YEW_ACTOR_PATTERN_MASK ) << 2 ) + YEW_CHAR_MAN;
    yewShowSprite( yewMan.sprite, yewMan.x, yewMan.y, pattern );
}

void yewInitMan()
{
    int x, y;
    yewMan.sprite = YEW_SPRITE_MAN;
    yewMan.status = YEW_ACTOR_LIVE | YEW_DIRECTION_RIGHT;
    yewMan.dx = 0;
    yewMan.dy = 0;
    yewLocateXY( yewStageStart[ yewStageIndex ], &x, &y );
    yewMan.x = x;
    yewMan.y = y;
    yewShowMan();
}

void yewMoveMan()
{
    if( ( yewMan.status & YEW_ACTOR_LIVE ) != 0 )
    {
        int key;
        key = 0;
        if( isLeftPressed() ) key = key | YEW_KEYS_LEFT;
        if( isRightPressed() ) key = key | YEW_KEYS_RIGHT;
        if( isUpPressed() ) key = key | YEW_KEYS_UP;
        if( isDownPressed() ) key = key | YEW_KEYS_DOWN;
        if( isFirePressed() ) key = key | YEW_KEYS_BUTTON0;

        if( yewIsOnCellGrid( yewMan.x, yewMan.y ) )
        {
            int keyCodes[4] = { YEW_KEYS_LEFT, YEW_KEYS_RIGHT, YEW_KEYS_UP, YEW_KEYS_DOWN };
            int newDirection, k, moved;
            newDirection = 0;
            moved = 0;
            for( k = 0; k < 4; k = k + 1 )
            {
                if( ( key & keyCodes[ k ] ) != 0 )
                {
                    if( yewCanMove( yewMan.x, yewMan.y, newDirection ) )
                    {
                        yewSetDirection( &yewMan.status, &yewMan.dx, &yewMan.dy, newDirection );
                        moved = 1;
                        break;
                    }
                    else
                    {
                        int oldDirection;
                        oldDirection = yewMan.status & YEW_ACTOR_DIRECTION_MASK;
                        if( yewCanMove( yewMan.x, yewMan.y, oldDirection ) )
                        {
                            moved = 1;
                            break;
                        }
                    }
                }
                newDirection = newDirection + 2;
            }
            if( moved == 0 )
            {
                yewMan.dx = 0;
                yewMan.dy = 0;
            }

            if( ( key & YEW_KEYS_BUTTON0 ) != 0 && ( yewMan.status & ( YEW_MAN_BUTTON_ON | YEW_MAN_ARROW_ON ) ) == 0 )
            {
                int x, y;
                yewMan.status = yewMan.status | YEW_MAN_BUTTON_ON;
                x = yewMan.x >> YEW_COORD_SHIFT;
                y = yewMan.y >> YEW_COORD_SHIFT;
                if( yewGetCell( x >> 1, y >> 1 ) == YEW_CELL_SPACE )
                {
                    yewArrowX = x;
                    yewArrowY = y;
                    yewMan.status = yewMan.status | YEW_MAN_ARROW_ON | YEW_MAN_FLASH;
                    yewStartSeq( 0, YEW_MELODY_BEEP );
                }
            }
        }
        else if( ( yewMan.status & YEW_MAN_ARROW_ON ) != 0 )
        {
            int direction;
            yewMan.status = yewMan.status & ~YEW_MAN_ARROW_ON;
            direction = yewMan.status & YEW_ACTOR_DIRECTION_MASK;
            yewSetCell( yewArrowX >> 1, yewArrowY >> 1, ( direction >> 1 ) | YEW_CELL_ARROW );
        }

        if( ( key & YEW_KEYS_BUTTON0 ) == 0 )
          yewMan.status = yewMan.status & ~YEW_MAN_BUTTON_ON;

        yewMoveActor( &yewMan.x, &yewMan.y, &yewMan.status, yewMan.dx, yewMan.dy );
        yewShowMan();

        if( yewIsOnCoordGrid( yewMan.x, yewMan.y ) )
          yewHitManMonsters();
    }
    else
    {
        if( ( yewMan.clock & YEW_COORD_MASK ) == 0 )
        {
            int patterns[4] = {
                YEW_CHAR_MAN + 0 * 4, YEW_CHAR_MAN + 4 * 4,
                YEW_CHAR_MAN + 2 * 4, YEW_CHAR_MAN + 6 * 4,
            };
            int pattern;
            pattern = patterns[ ( yewMan.clock >> YEW_COORD_SHIFT ) & 3 ];
            yewShowSprite( yewMan.sprite, yewMan.x, yewMan.y, pattern );
        }
        yewMan.clock = yewMan.clock - 1;
        if( yewMan.clock == 0 )
        {
            yewMan.status = yewMan.status | YEW_ACTOR_LIVE;
            yewShowMan();
        }
    }
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void yewMapToVVram()
{
    int col, floor, colGroup, sub, b, x, y;

    for( col = 0; col < YEW_VVRAM_WIDTH; col = col + 1 )
      yewVVram[ 0 ][ col ] = YEW_CHAR_FENCE;

    for( floor = 0; floor < YEW_MAP_HEIGHT; floor = floor + 1 )
    {
        for( colGroup = 0; colGroup < YEW_MAP_WIDTH / YEW_MAP_WIDTH_PER_BYTE; colGroup = colGroup + 1 )
        {
            b = yewCellMap[ floor * ( YEW_MAP_WIDTH / YEW_MAP_WIDTH_PER_BYTE ) + colGroup ];
            for( sub = 0; sub < YEW_MAP_WIDTH_PER_BYTE; sub = sub + 1 )
            {
                int cellByte, column;
                cellByte = b & 0x0f;
                column = colGroup * YEW_MAP_WIDTH_PER_BYTE + sub;
                x = column * 2;
                y = floor * 2 + YEW_STAGE_TOP;
                if( cellByte == YEW_CELL_WALL )
                  yewPutTileIntoVVram( x, y, YEW_CHAR_WALL );
                else if( cellByte == YEW_CELL_ROCK )
                  yewPutTileIntoVVram( x, y, YEW_CHAR_ROCK );
                else if( ( cellByte & 0x0c ) == YEW_CELL_ARROW )
                  yewPutTileIntoVVram( x, y, YEW_CHAR_ARROW + ( ( cellByte << 2 ) & 0x0c ) );
                else
                  yewEraseTileInVVram( x, y );
                b = b >> 4;
            }
        }
    }

    for( col = 0; col < YEW_VVRAM_WIDTH; col = col + 1 )
      yewVVram[ YEW_VVRAM_HEIGHT - 1 ][ col ] = YEW_CHAR_FENCE + 1;
}

void yewDrawAll()
{
    yewMapToVVram();
    yewDrawItems();
    yewDrawSprites();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly,
// reusing the same technique already proven correct in gameCracky.c's own
// crkComposeRawByte() (both games share byte-identical VVram/Chars/Vram
// source - see this file's own header comment). No hardware orientation
// transform - drawn directly at its own (col,page).
int yewComposeRawByte( int rawCol, int rawPage )
{
    int mapX, sub, upper, lower, upperByte, lowerByte;
    mapX = rawCol / 4;
    sub = rawCol % 4;
    upper = yewVVram[ rawPage * 2 ][ mapX ];
    lower = yewVVram[ rawPage * 2 + 1 ][ mapX ];
    if( sub == 0 )
    {
        upperByte = yewCharPattern[ upper * 2 + 0 ];
        lowerByte = yewCharPattern[ lower * 2 + 0 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    if( sub == 1 )
    {
        upperByte = yewCharPattern[ upper * 2 + 0 ];
        lowerByte = yewCharPattern[ lower * 2 + 0 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    if( sub == 2 )
    {
        upperByte = yewCharPattern[ upper * 2 + 1 ];
        lowerByte = yewCharPattern[ lower * 2 + 1 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    upperByte = yewCharPattern[ upper * 2 + 1 ];
    lowerByte = yewCharPattern[ lower * 2 + 1 ];
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

void yewRender()
{
    int page, col, value, charCol;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            charCol = col / 4;
            if( yewTextActive[ page ][ charCol ] )
            {
                int sub, c;
                sub = col % 4;
                c = yewTextChar[ page ][ charCol ];
                value = yewAsciiPattern[ c * 4 + sub ];
            }
            else if( charCol < YEW_VVRAM_WIDTH )
              value = yewComposeRawByte( col, page );
            else
              value = 0;
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - stage progression / init.
// -----------------------------------------------------------------------------

void yewInitStage()
{
    // upstream re-walks from scratch every call, decrementing yewMaxTime by
    // 5 (floor 25) every time a full lap through all 7 stages completes -
    // ported as the same re-walk-from-zero loop rather than persisted
    // running state, matching upstream's own real (if unusual) structure
    // exactly.
    int i, j;
    yewMaxTime = 60;
    i = 0;
    j = 0;
    while( i < yewCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= YEW_STAGE_COUNT )
        {
            j = 0;
            if( yewMaxTime >= 30 )
              yewMaxTime = yewMaxTime - 5;
        }
    }
    yewStageIndex = j;
}

void yewUnpackStageBytes()
{
    // Converts yewStageBytes' own 2-bit-per-cell raw storage (4 cells/byte)
    // into yewCellMap's own runtime 4-bit-per-cell storage (2 cells/byte) -
    // a direct structural mirror of InitTrying()'s own nested unpack loop,
    // not simplified, since getting the nibble order subtly wrong would
    // corrupt every map cell.
    int row, group, sub, srcIdx, destIdx;
    srcIdx = 0;
    destIdx = 0;
    for( row = 0; row < YEW_MAP_HEIGHT; row = row + 1 )
    {
        for( group = 0; group < YEW_MAP_WIDTH / 4; group = group + 1 )
        {
            int source;
            source = yewStageBytes[ yewStageIndex ][ srcIdx ];
            srcIdx = srcIdx + 1;
            for( sub = 0; sub < 2; sub = sub + 1 )
            {
                int destination, j, b;
                destination = 0;
                for( j = 0; j < 2; j = j + 1 )
                {
                    b = source & 3;
                    destination = ( destination >> 4 ) | ( ( b & 0x0f ) << 4 );
                    source = source >> 2;
                }
                yewCellMap[ destIdx ] = destination;
                destIdx = destIdx + 1;
            }
        }
    }
}

void yewInitTrying()
{
    int i;
    yewStageTime = yewMaxTime;
    i = yewStageMonCount[ yewStageIndex ];
    do { yewStageTime = yewStageTime + 30; i = i - 1; } while( i != 0 );
    i = yewStageCarCount[ yewStageIndex ];
    do { yewStageTime = yewStageTime - 10; i = i - 1; } while( i != 0 );

    yewHideAllSprites();
    yewResetTextOverlays();
    yewPrintStatus();

    yewUnpackStageBytes();

    yewInitMan();
    yewInitCars();
    yewInitMonsters();
    yewInitItems();
    yewInitPoints();
    yewDrawAll();
}


// -----------------------------------------------------------------------------
//   State machine - replaces upstream's goto-chained Main() around one big
//   do-while (see this file's own header comment for the full mapping).
// -----------------------------------------------------------------------------

void yewBeginTitle()
{
    int r, c;
    for( r = 0; r < YEW_VVRAM_HEIGHT; r = r + 1 )
    {
        for( c = 0; c < YEW_VVRAM_WIDTH; c = c + 1 )
          yewVVram[ r ][ c ] = YEW_CHAR_SPACE;
    }

    yewResetTextOverlays();
    yewHideAllSprites();

    // Reset StageTime to 0 so a stale prior countdown doesn't linger on
    // the title screen's own status display - matches gameCracky.c's own
    // identical fix for the identical situation.
    yewStageTime = 0;
    yewPrintStatus();

    // **Restored, matching gameCracky.c's own identical fix for the same
    // underlying mistake** (see this file's own header comment, and
    // yewTitleBytes' own comment, for the full story): this used to be a
    // plain-text "YEWDOW" substitute, reasoned as "purely decorative" -
    // wrong, it's upstream's own real pixel-art title wordmark, meant to
    // be the single biggest, most prominent element on the whole screen.
    // Drawn directly into yewVVram from yewTitleBytes[], reproducing
    // Title()'s own real nested-loop write order exactly: for each of
    // YEW_TITLE_LENGTH(5) letter-groups (NOT 6 - this game's own upstream
    // TitleLength differs from Cracky's, see yewTitleBytes' own comment),
    // 4 rows of 4 columns each, starting at VVram row 2 / column
    // YEW_TITLE_LEFT(2) - i.e. real hardware pages 1-2, VVram columns
    // 2-21. This footprint sits entirely within map-region columns
    // (charCol 0-23) and never overlaps any status-text column (24-31),
    // and no text-overlay cell is marked active anywhere in this
    // footprint (the old yewPrintS() call for "YEWDOW" is gone, not just
    // its own text moved elsewhere) - so yewRender()'s own per-cell
    // yewTextActive check naturally falls through to the normal
    // yewComposeRawByte()/VVram-map path for these cells, with no OR-
    // combine or other render-function change needed at all (unlike
    // Cracky's own fix, which needed a real OR-combine specifically
    // because its status-text grid is unconditionally active across the
    // FULL screen width while on the title screen - this game's own
    // yewTextActive is already a genuine per-cell selector, see this
    // file's own header comment for why that's a strictly more general
    // design that was never actually broken here).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < YEW_TITLE_LENGTH; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                yewVVram[ 2 + row ][ YEW_TITLE_LEFT + ch * 4 + col ] = yewTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Every column argument below now matches upstream's own real, literal
    // Status.cpp columns exactly (re-derived directly from the source
    // while restoring the logo above, not left at this port's own earlier,
    // arbitrary placement) - MINI at `TitleLeft + 4*TitleLength - 5` = 17
    // (previously wrongly placed at column 9), START/CONTINUE at
    // `ArrowX + 1` = 9 with the cursor at `ArrowX` = 8 (previously
    // columns 2/1), and the credit line at column 12 (previously column
    // 6). None of these collide with the logo bitmap above (pages 1-2)
    // or with each other - MINI is page 3, START/CONTINUE/cursor are
    // pages 5-6, the credit line is page 7.
    {
        int sMini[4] = { 'M', 'I', 'N', 'I' };
        yewPrintS( 3, YEW_TITLE_LEFT + 4 * YEW_TITLE_LENGTH - 5, sMini, 4 );
    }
    {
        int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        yewPrintS( 5, 9, sStart, 5 );
        yewPrintS( 6, 9, sContinue, 8 );
    }
    {
        int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        yewPrintS( 7, 12, sCredit, 12 );
    }

    yewSelection = 0;
    yewSelectionChanged = true;
    yewPrevLeft = false; yewPrevRight = false; yewPrevUp = false; yewPrevDown = false; yewPrevFire = false;
    yewState = YEW_STATE_TITLE;
}

void yewUpdateTitle()
{
    bool left, right, up, down, fire, justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( left && !yewPrevLeft ) || ( right && !yewPrevRight ) ||
              ( up && !yewPrevUp ) || ( down && !yewPrevDown );
    justFire = fire && !yewPrevFire;
    yewPrevLeft = left; yewPrevRight = right; yewPrevUp = up; yewPrevDown = down; yewPrevFire = fire;

    if( yewSelectionChanged )
    {
        // Cursor column fixed to match upstream's own real `ArrowX = 8`
        // (previously column 1) - see yewBeginTitle()'s own comment for
        // the full re-derivation of every title-screen column position.
        yewSelectionChanged = false;
        if( yewSelection == 0 ) yewPrintC( 5, 8, '>' ); else yewPrintC( 5, 8, ' ' );
        if( yewSelection == 1 ) yewPrintC( 6, 8, '>' ); else yewPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        yewScore = 0;
        if( yewSelection == 0 ) yewCurrentStage = 0;
        yewRemainCount = 3;
        yewInitStage();
        yewInitTrying();
        yewClock = 0;
        yewMonsterNum = 0;
        yewTimeDenom = YEW_MAX_TIME_DENOM;
        yewTickCounter = 0;
        yewStartSeq( 1, YEW_MELODY_START );
        yewState = YEW_STATE_START_JINGLE;
        yewRender();
        return;
    }
    if( justDir )
    {
        yewSelection = yewSelection ^ 1;
        yewSelectionChanged = true;
    }
    yewRender();
}

void yewUpdateStartJingle()
{
    if( !yewSeqPlaying( 1 ) )
    {
        yewStartBgm();
        yewState = YEW_STATE_PLAYING;
    }
    yewRender();
}

void yewBeginTimeUpWait()
{
    yewWaitFrames = 120;
    yewState = YEW_STATE_TIME_UP_WAIT;
}

// A genuine bug found via this port's own verification pass, fixed here:
// upstream's `if (pLostCar != nullptr) { LooseCar(); goto lose; }` runs the
// entire 8-step blink-and-beep LooseCar() animation BEFORE ever reaching the
// `lose:` label's own `StopBGM()` call - so on real hardware, the background
// music keeps playing (mixed with the 8 short "Sound_Loose" beeps) all the
// way through the car-lost animation, and only cuts out once it finishes and
// control reaches yewBeginLoseSequence() (this file's own `lose:` mirror,
// which already calls yewStopBgm() at the correct point). An earlier version
// of this function called yewStopBgm() here too, silencing the BGM the
// instant a car was lost - before the blink animation even started - instead
// of leaving it playing underneath the whole animation the way upstream
// does. Removed; yewBeginLoseSequence()'s own yewStopBgm() call (reached
// once yewAnimStep hits 8) is the only place BGM should stop for this path.
void yewBeginLoseCarAnim()
{
    yewAnimStep = 0;
    yewWaitFrames = 0;
    yewState = YEW_STATE_LOSE_CAR_ANIM;
}

// Shared landing point for both "a car was lost" and "the clock ran out" -
// matches upstream's own shared `lose:` goto-label exactly.
void yewBeginLoseSequence()
{
    yewStopBgm();
    yewRemainCount = yewRemainCount - 1;
    if( yewRemainCount > 0 )
    {
        yewInitTrying();
        yewClock = 0;
        yewMonsterNum = 0;
        yewTimeDenom = YEW_MAX_TIME_DENOM;
        yewTickCounter = 0;
        yewStartSeq( 1, YEW_MELODY_START );
        yewState = YEW_STATE_START_JINGLE;
    }
    else
    {
        yewPrintGameOver();
        yewStartSeq( 1, YEW_MELODY_GAMEOVER );
        yewState = YEW_STATE_GAMEOVER_JINGLE;
    }
}

void yewUpdateTimeUpWait()
{
    if( yewWaitFrames > 0 )
    {
        yewWaitFrames = yewWaitFrames - 1;
        yewRender();
        return;
    }
    yewBeginLoseSequence();
    yewRender();
}

void yewUpdateLoseCarAnim()
{
    int patterns[4] = {
        YEW_CHAR_CAR + 1 * 4, YEW_CHAR_CAR + 2 * 4, YEW_CHAR_CAR + 0 * 4, YEW_CHAR_CAR + 3 * 4,
    };

    if( yewWaitFrames > 0 )
    {
        yewWaitFrames = yewWaitFrames - 1;
        yewRender();
        return;
    }

    yewShowSprite( yewCars[ yewLostCarIndex ].sprite, yewCars[ yewLostCarIndex ].x, yewCars[ yewLostCarIndex ].y, patterns[ yewAnimStep & 3 ] );
    yewDrawAll();
    yewStartSeq( 0, YEW_MELODY_LOOSE );
    yewAnimStep = yewAnimStep + 1;
    yewWaitFrames = yewNoteFrames( 1 );

    if( yewAnimStep >= 8 )
      yewBeginLoseSequence();

    yewRender();
}

void yewUpdateGameOverJingle()
{
    if( !yewSeqPlaying( 1 ) )
      yewBeginTitle();
    else
      yewRender();
}

void yewBeginClearWait()
{
    // BGM deliberately keeps playing during this wait, matching upstream's
    // own real `WaitTimer(30); StopBGM(); ...` ordering - see this file's
    // own header comment.
    yewWaitFrames = 30;
    yewState = YEW_STATE_CLEAR_WAIT;
}

void yewUpdateClearWait()
{
    if( yewWaitFrames > 0 )
    {
        yewWaitFrames = yewWaitFrames - 1;
        yewRender();
        return;
    }
    yewStopBgm();
    yewStartSeq( 1, YEW_MELODY_CLEAR );
    yewState = YEW_STATE_CLEAR_JINGLE;
    yewRender();
}

void yewUpdateClearJingle()
{
    if( !yewSeqPlaying( 1 ) )
    {
        yewWaitFrames = 0;
        yewState = YEW_STATE_BONUS_TALLY;
    }
    yewRender();
}

void yewUpdateBonusTally()
{
    if( yewWaitFrames > 0 )
    {
        yewWaitFrames = yewWaitFrames - 1;
        yewRender();
        return;
    }

    if( yewStageTime >= YEW_BONUS_RATE )
    {
        yewAddScore( 2 );
        yewStageTime = yewStageTime - YEW_BONUS_RATE;
        yewPrintTime();
        yewStartSeq( 0, YEW_MELODY_BEEP );
        yewWaitFrames = yewNoteFrames( 1 );
        yewRender();
        return;
    }

    yewStageTime = 0;
    yewPrintStatus();
    yewCurrentStage = yewCurrentStage + 1;
    yewInitStage();
    yewInitTrying();
    yewClock = 0;
    yewMonsterNum = 0;
    yewTimeDenom = YEW_MAX_TIME_DENOM;
    yewTickCounter = 0;
    yewStartSeq( 1, YEW_MELODY_START );
    yewState = YEW_STATE_START_JINGLE;
    yewRender();
}

void yewUpdatePlaying()
{
    yewTickCounter = yewTickCounter + 1;
    if( yewTickCounter < YEW_TICK_DIVISOR )
    {
        yewRender();
        return;
    }
    yewTickCounter = 0;

    yewUpdatePoints();
    yewMoveMan();
    if( yewMonsterNum >= 0 )
    {
        yewMoveMonsters();
        yewMonsterNum = yewMonsterNum - 10;
    }
    yewMonsterNum = yewMonsterNum + 3;
    // Cars move once every other real logic tick (see this file's own
    // header discussion of upstream's own `Clock&3`/`Clock&7` gating,
    // which - once traced through fully - reduces to exactly this simple
    // alternating-parity check for real-time purposes).
    if( ( yewClock & 1 ) == 0 )
      yewMoveCars();
    yewClock = yewClock + 1;

    yewDrawAll();

    yewTimeDenom = yewTimeDenom - 1;
    if( yewTimeDenom == 0 )
    {
        yewStageTime = yewStageTime - 1;
        yewTimeDenom = YEW_MAX_TIME_DENOM;
        yewPrintTime();
        if( yewStageTime == 0 )
        {
            yewPrintTimeUp();
            yewRender();
            yewBeginTimeUpWait();
            return;
        }
    }

    if( yewLostCarIndex >= 0 )
    {
        yewRender();
        yewBeginLoseCarAnim();
        return;
    }

    if( yewItemCount == 0 )
    {
        yewRender();
        yewBeginClearWait();
        return;
    }

    yewRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameYewdow_init()
{
    int i;

    yewScore = 0;
    yewCurrentStage = 0;
    yewRemainCount = 3;
    yewStageTime = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        yewSeqActive[ i ] = 0;
        yewSeqMelody[ i ] = YEW_MELODY_NONE;
    }
    yewTickCounter = 0;

    yewBeginTitle();
}

void gameYewdow_update()
{
    yewAdvanceSound();

    if( yewState == YEW_STATE_TITLE )
      yewUpdateTitle();
    else if( yewState == YEW_STATE_START_JINGLE )
      yewUpdateStartJingle();
    else if( yewState == YEW_STATE_PLAYING )
      yewUpdatePlaying();
    else if( yewState == YEW_STATE_LOSE_CAR_ANIM )
      yewUpdateLoseCarAnim();
    else if( yewState == YEW_STATE_TIME_UP_WAIT )
      yewUpdateTimeUpWait();
    else if( yewState == YEW_STATE_GAMEOVER_JINGLE )
      yewUpdateGameOverJingle();
    else if( yewState == YEW_STATE_CLEAR_WAIT )
      yewUpdateClearWait();
    else if( yewState == YEW_STATE_CLEAR_JINGLE )
      yewUpdateClearJingle();
    else if( yewState == YEW_STATE_BONUS_TALLY )
      yewUpdateBonusTally();
}
