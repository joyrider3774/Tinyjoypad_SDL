// =============================================================================
// AERIAL mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_aerial`) - a side-scrolling
// shooter: fly left/right/up/down over an endlessly-scrolling terrain, shoot
// enemy fighters/ground vehicles/gun forts, dodge their return fire, and
// pick up the occasional extra-life item; 3 lives per game, unlimited
// auto-advancing stages (no win condition, only survival). Ported following
// the same standard-C dialect-conversion recipe already established
// project-wide, from the sibling `tinyjoypad_vircon32` project's own
// already-verified `src/games/gameAerial.c` (same author/engine lineage as
// that project's own `gameCracky.c` - the "cate" engine, CH32V003 RISC-V,
// not AVR - so `avrCompat.h`'s uint8_t-narrowing tricks are irrelevant
// here). See the sibling file's own header comment for the full porting/
// bug-fix history (byte-underflow-as-exit-signal fixes, the OR-combined
// title logo + status text rendering, the flattened per-stage sky/ground
// element tables, etc) - not duplicated here, only the mechanical dialect
// conversion (int[N]->TYPE name[N], text int*/int[]->char*/char[]) was
// applied on top of that already-correct source.
//
// No EEPROM/high-score persistence - upstream has none at all (checked:
// only Score/RemainCount/CurrentStage exist, no persisted best-score of any
// kind). No forceRedraw needed - every state (title/start-jingle/playing/
// gameover-jingle) calls aerRender() unconditionally on every real
// gameAerial_update() tick, same as the sibling project's own conclusion.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into aerCharPattern (map tiles/sprites) /
//   aerAsciiPattern (status text)
// -----------------------------------------------------------------------------

#define AER_CHAR_SPACE 0x00
#define AER_CHAR_GROUND 0x10
#define AER_CHAR_GROUND_UP 0x11
#define AER_CHAR_GROUND_DOWN 0x12
#define AER_CHAR_BULLET 0x13
#define AER_CHAR_FIGHTER 0x17
#define AER_CHAR_FIGHTER_RIGHT 0x17
#define AER_CHAR_FIGHTER_RIGHT_DOWN 0x1B
#define AER_CHAR_FIGHTER_RIGHT_UP 0x1F
#define AER_CHAR_FIGHTER_LEFT 0x23
#define AER_CHAR_TRACK 0x27
#define AER_CHAR_ROCKET 0x2B
#define AER_CHAR_SMALL_BANG 0x2F
#define AER_CHAR_LARGE_BANG 0x33
#define AER_CHAR_ITEM 0x43
#define AER_CHAR_FORT 0x47

// -----------------------------------------------------------------------------
//   Movable.h / Bullet.h
// -----------------------------------------------------------------------------

#define AER_BULLET_SHIFT 1
#define AER_BULLET_RATE 2
#define AER_HI_VELOCITY 50
#define AER_LO_VELOCITY 35
#define AER_LONG_VELOCITY 46
#define AER_SHORT_VELOCITY 19

// -----------------------------------------------------------------------------
//   Stage.h / VVram.h
// -----------------------------------------------------------------------------

#define AER_VVRAM_WIDTH 24
#define AER_VVRAM_HEIGHT 14
#define AER_STAGE_WIDTH 24
#define AER_STAGE_HEIGHT 14
#define AER_MAP_WIDTH 48
#define AER_BACKGROUND_WIDTH 26
#define AER_BOTTOM 12
#define AER_FORT_START_X 86
#define AER_STAGE_COUNT 10
#define AER_SKY_TOTAL 50
#define AER_GROUND_TOTAL 187

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define AER_SPRITE_MYFIGHTER 0
#define AER_SPRITE_ENEMYFIGHTER 1
#define AER_EF_COUNT 4
#define AER_SPRITE_GROUNDENEMY 5
#define AER_GE_COUNT 6
#define AER_SPRITE_MYBULLET 11
#define AER_MB_COUNT 3
#define AER_SPRITE_ENEMYBULLET 14
#define AER_EB_COUNT 4
#define AER_SPRITE_BANG 18
#define AER_BANG_COUNT 6
#define AER_SPRITE_ITEM 24
#define AER_SPRITE_COUNT 25
#define AER_INVALID_CODE 255
#define AER_INVALID_Y 255
#define AER_BANG_INVALID_Y 128

// -----------------------------------------------------------------------------
//   MyFighter.h
// -----------------------------------------------------------------------------

#define AER_MYFIGHTER_INITIAL_X 2
#define AER_MYFIGHTER_INITIAL_Y 2
#define AER_CRASH_TIME 7
#define AER_REVIVE_TIME 31
#define AER_REVIVE_MASK 1

// -----------------------------------------------------------------------------
//   MyBullet.h
// -----------------------------------------------------------------------------

#define AER_MB_SHORT_INTERVAL 2
#define AER_MB_LONG_INTERVAL 8

// -----------------------------------------------------------------------------
//   GroundEnemy.h
// -----------------------------------------------------------------------------

#define AER_GE_TYPE_TRACK 0
#define AER_GE_TYPE_ROCKET 1

// -----------------------------------------------------------------------------
//   Fort.h
// -----------------------------------------------------------------------------

#define AER_FORT_COUNT 6
#define AER_FORT_WIDTH 6
#define AER_FORT_HEIGHT 4
#define AER_FORT_LEFT 6
#define AER_FORT_MAX_LIFE 20
#define AER_FORT_PTS 80
#define AER_FORT_FIRE_MASK 7

// -----------------------------------------------------------------------------
//   Bang.h
// -----------------------------------------------------------------------------

#define AER_BANG_SIZE 1
#define AER_BANG_TIME 2

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching Cracky's own precedent.
// -----------------------------------------------------------------------------

#define AER_N8 6
#define AER_N8P ( AER_N8 * 3 / 2 )
#define AER_N4 ( AER_N8 * 2 )
#define AER_N16 ( AER_N8 / 2 )
#define AER_N2 ( AER_N4 * 2 )

#define AER_E2 1
#define AER_F2 2
#define AER_F2S 3
#define AER_G2 4
#define AER_G2S 5
#define AER_A2 6
#define AER_A2S 7
#define AER_B2 8
#define AER_C3 9
#define AER_C3S 10
#define AER_D3 11
#define AER_D3S 12
#define AER_E3 13
#define AER_F3 14
#define AER_F3S 15
#define AER_G3 16
#define AER_G3S 17
#define AER_A3 18
#define AER_A3S 19
#define AER_B3 20
#define AER_C4 21
#define AER_C4S 22
#define AER_D4 23
#define AER_D4S 24
#define AER_E4 25
#define AER_F4 26
#define AER_F4S 27
#define AER_G4 28
#define AER_G4S 29
#define AER_A4 30
#define AER_A4S 31
#define AER_B4 32
#define AER_C5 33
#define AER_C5S 34
#define AER_D5 35
#define AER_D5S 36
#define AER_E5 37
#define AER_F5 38
#define AER_F5S 39
#define AER_G5 40

#define AER_TEMPO 150

#define AER_MELODY_NONE 0
#define AER_MELODY_FIRE 1
#define AER_MELODY_UP 2
#define AER_MELODY_START 3
#define AER_MELODY_GAMEOVER 4
#define AER_MELODY_BGM1 5
#define AER_MELODY_BGM2 6

// -----------------------------------------------------------------------------
//   State machine / frame pacing
// -----------------------------------------------------------------------------

#define AER_TICK_DIVISOR 2

#define AER_STATE_TITLE 0
#define AER_STATE_START_JINGLE 1
#define AER_STATE_PLAYING 2
#define AER_STATE_GAMEOVER_JINGLE 3

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script, not
//   hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph. Byte-for-byte
// identical to Cracky's own copy of the same table (shared "cate" engine).
int aerAsciiPattern[108] = {
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
};

// CharPattern - 95 tile/sprite glyphs, 2 bytes/glyph (a 4x4 pixel block).
int aerCharPattern[190] = {
    0, 0, 51, 0, 204, 0, 255, 0, 0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204, 0, 255, 51, 255, 204, 255, 255, 255,
    255, 255, 128, 252, 238, 140, 51, 0, 0, 51, 204, 0, 0, 204, 206, 200,
    174, 140, 16, 101, 18, 17, 192, 254, 136, 0, 98, 52, 215, 7, 136, 232,
    223, 39, 16, 51, 97, 3, 200, 234, 140, 236, 17, 33, 86, 1, 128, 122,
    206, 8, 231, 111, 214, 109, 0, 252, 12, 0, 236, 247, 231, 12, 228, 182,
    74, 78, 114, 226, 37, 23, 0, 194, 132, 124, 98, 255, 233, 111, 206, 236,
    7, 136, 221, 54, 63, 1, 100, 219, 123, 226, 0, 54, 17, 226, 151, 171,
    88, 70, 18, 49, 35, 0, 30, 31, 179, 227, 99, 102, 100, 54, 200, 174,
    219, 221, 221, 221, 221, 221, 173, 202, 189, 11, 99, 174, 106, 102, 102, 102,
    102, 102, 166, 106, 166, 11, 0, 48, 255, 63, 119, 255, 63, 255, 47, 0,
    17, 1, 0, 17, 1, 0, 16, 115, 1, 0, 17, 1, 0, 0,
};

// TitleBytes - upstream's own real "AERIAL" title-screen logo bitmap
// (Status.cpp's `Title()`), 6 letters x 4x4 VVram-cell glyph indices each
// (96 values total: A,E,R,I,A,L), byte-diff-verified against the real
// upstream source via a Python extraction script. Every value here is a
// valid index into aerCharPattern[]'s own "logo" range (indices 0-15, the
// first 32 bytes of that table, already confirmed byte-identical to
// upstream's own CharPattern[] logo block) - the exact same shared
// block-pattern palette every other map tile in this game already draws
// through, just reused here to build a big pixel-art wordmark instead of a
// terrain/sprite tile. See aerBeginTitle()'s own comment for why this
// replaces an earlier plain-text "AERIAL" substitute (in the sibling
// project's own porting history), matching Cracky's own aerTitleBytes fix
// exactly.
int aerTitleBytes[96] = {
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x07, 0x05, 0x01, 0x0c, 0x0b, 0x0a, 0x02,
    0x0c, 0x03, 0x00, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x0c, 0x07, 0x05, 0x0b, 0x0c, 0x03, 0x08, 0x0f,
    0x0c, 0x07, 0x0f, 0x02, 0x04, 0x01, 0x04, 0x05,
    0x04, 0x0d, 0x07, 0x01, 0x00, 0x0c, 0x03, 0x00,
    0x00, 0x0c, 0x03, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x03, 0x00, 0x00, 0x0c, 0x03, 0x00, 0x00,
    0x0c, 0x03, 0x00, 0x00, 0x04, 0x05, 0x05, 0x01,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values
// 1-40). Byte-for-byte identical to Cracky's own copy.
int aerFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int aerMelodyFire[13] = { 1, AER_F5, 1, AER_D5S, 1, AER_C5S, 1, AER_B4, 1, AER_A4, 1, AER_G5, 0 };
int aerMelodyUp[13]   = { 1, AER_C4, 1, AER_C4S, 1, AER_D4, 1, AER_F4, 1, AER_A4, 1, AER_C5, 0 };

int aerMelodyStart[25] = {
    AER_N4, AER_G4, AER_N8, AER_G4, AER_N8, AER_C5, AER_N8, AER_E5, AER_N4, AER_D5, AER_N8, AER_C5,
    AER_N8P, AER_D5, AER_N16, AER_D5, AER_N8, AER_D5, AER_N8, AER_E5, AER_N4, AER_D5, AER_N4, 0,
    0,
};

// Fixed: upstream's own notes[] has NO trailing 0 terminator (a genuine
// omission, not a deliberate quirk - see the sibling project's own header
// comment for the full derivation). The trailing 0 here is a fix, not
// present upstream.
int aerMelodyGameOver[27] = {
    AER_N8, AER_F5, AER_N8, AER_F5, AER_N8, AER_E5, AER_N8, AER_E5, AER_N8, AER_D5, AER_N8, AER_D5,
    AER_N8, AER_C5, AER_N8, AER_C5, AER_N8, AER_B4, AER_N8, AER_G4, AER_N8, AER_A4, AER_N8, AER_B4,
    AER_N2, AER_C5, 0,
};

int aerMelodyBgm1[169] = {
    12, 28, 6, 28, 6, 33, 6, 37, 12, 35, 6, 33, 9, 35, 3, 35, 6, 35, 6, 37, 12, 35, 12, 0,
    12, 28, 6, 28, 6, 33, 6, 37, 12, 35, 6, 33, 24, 35, 24, 0,
    12, 28, 6, 28, 6, 33, 6, 37, 12, 35, 6, 33, 9, 35, 3, 35, 6, 35, 6, 37, 12, 35, 12, 0,
    12, 28, 6, 28, 6, 33, 6, 37, 12, 38, 6, 37, 24, 35, 12, 0,
    6, 35, 6, 37, 9, 38, 3, 38, 6, 38, 6, 37, 12, 38, 12, 0,
    9, 37, 3, 37, 6, 37, 6, 35, 12, 37, 12, 0,
    9, 35, 3, 35, 6, 35, 6, 33, 12, 35, 6, 33, 6, 35, 24, 37, 24, 0,
    9, 38, 3, 38, 6, 38, 6, 37, 12, 38, 12, 0,
    9, 37, 3, 37, 6, 37, 6, 35, 12, 37, 12, 0,
    9, 35, 3, 35, 6, 35, 6, 37, 6, 38, 12, 37, 6, 35, 24, 33, 24, 0,
    255,
};

int aerMelodyBgm2[257] = {
    6, 21, 6, 21, 6, 28, 6, 21, 6, 18, 6, 18, 6, 21, 6, 25, 6, 23, 6, 23, 6, 30, 6, 23, 6, 16, 6, 16, 6, 20, 6, 23,
    6, 21, 6, 21, 6, 25, 6, 28, 6, 18, 6, 18, 6, 25, 6, 18, 6, 23, 6, 23, 6, 30, 6, 30, 6, 28, 6, 28, 6, 35, 6, 23,
    6, 21, 6, 21, 6, 28, 6, 21, 6, 18, 6, 18, 6, 25, 6, 21, 6, 23, 6, 23, 6, 26, 6, 30, 6, 28, 6, 28, 6, 20, 6, 23,
    6, 21, 6, 21, 6, 25, 6, 28, 6, 14, 6, 14, 6, 21, 6, 14, 6, 13, 6, 13, 6, 16, 6, 20, 6, 18, 6, 18, 6, 25, 6, 18,
    6, 23, 6, 23, 6, 26, 6, 30, 6, 28, 6, 28, 6, 20, 6, 23, 6, 21, 6, 21, 6, 25, 6, 25, 6, 28, 6, 28, 6, 21, 6, 21,
    6, 23, 6, 23, 6, 26, 6, 26, 6, 30, 6, 30, 6, 23, 6, 23, 6, 18, 6, 18, 6, 21, 6, 21, 6, 25, 6, 25, 6, 21, 6, 25,
    6, 23, 6, 23, 6, 30, 6, 23, 6, 28, 6, 28, 6, 23, 6, 20, 6, 21, 6, 21, 6, 25, 6, 25, 6, 28, 6, 28, 6, 21, 6, 21,
    6, 23, 6, 23, 6, 26, 6, 30, 6, 28, 6, 28, 6, 20, 6, 23, 6, 21, 6, 21, 6, 25, 6, 25, 6, 28, 6, 28, 6, 30, 6, 28,
    255,
};

// Terrain generation (Stage.cpp's own byte-per-stage map data, plus the
// Up/Down transition mini-tiles).
int aerStageBytes[10][12] = {
    { 33, 0, 33, 16, 8, 0, 16, 66, 2, 33, 0, 0 },
    { 64, 38, 9, 9, 144, 36, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 64, 146, 16, 72, 2, 0, 25, 2, 0 },
    { 0, 1, 2, 0, 97, 0, 128, 64, 32, 145, 2, 33 },
    { 1, 146, 16, 0, 128, 64, 0, 144, 32, 1, 0, 32 },
    { 16, 128, 64, 64, 146, 36, 73, 146, 32, 0, 4, 128 },
    { 64, 16, 0, 0, 34, 16, 0, 0, 32, 1, 1, 136 },
    { 16, 4, 0, 8, 4, 10, 0, 81, 136, 64, 10, 0 },
    { 20, 0, 1, 42, 68, 160, 0, 0, 16, 129, 144, 128 },
    { 0, 21, 34, 0, 17, 66, 128, 136, 0, 16, 5, 42 },
};

int aerUpTile[4]   = { AER_CHAR_SPACE, AER_CHAR_GROUND_UP, AER_CHAR_GROUND_UP, AER_CHAR_GROUND };
int aerDownTile[4] = { AER_CHAR_GROUND_DOWN, AER_CHAR_SPACE, AER_CHAR_GROUND, AER_CHAR_GROUND_DOWN };

// Flattened per-stage sky/ground element lists (upstream: 10 separate named
// arrays each, referenced via a real pointer inside each Stage struct) -
// one shared flat array per field plus a start-offset+count table.
int aerSkyX[50] = {
    0, 34, 36, 46, 48, 72, 82, 94, 0, 34, 38, 46, 50, 70, 86, 94,
    0, 30, 52, 66, 88, 94, 0, 28, 56, 64, 92, 94, 0, 24, 92, 94,
    0, 2, 4, 94, 0, 22, 92, 94, 0, 18, 92, 94, 0, 94, 0, 2,
    92, 94,
};
int aerSkyBits[50] = {
    1, 0, 2, 0, 1, 3, 1, 0, 1, 3, 2, 3, 1, 3, 1, 0,
    1, 3, 1, 3, 1, 0, 1, 3, 1, 3, 1, 0, 1, 3, 1, 0,
    1, 3, 2, 0, 1, 3, 1, 0, 1, 3, 1, 0, 1, 0, 1, 3,
    1, 0,
};
int aerStageSkyStart[10] = { 0, 8, 16, 22, 28, 32, 36, 40, 44, 46 };
int aerStageSkyCount[10] = { 8, 8, 6, 6, 4, 4, 4, 4, 2, 4 };

int aerGroundX[187] = {
    6, 10, 12, 24, 40, 42, 48, 68, 82, 86, 90,
    4, 14, 20, 22, 28, 34, 40, 46, 50, 52, 58, 62, 68, 72, 76, 80, 82, 88, 90,
    4, 8, 12, 18, 22, 28, 34, 40, 42, 46, 52, 58, 60, 64, 70, 78, 82, 86, 88, 92,
    4, 6, 14, 18, 22, 26, 30, 34, 42, 46, 48, 52, 56, 60, 64, 70, 74, 82, 86, 90,
    4, 6, 10, 16, 24, 28, 30, 34, 40, 44, 48, 52, 54, 58, 64, 70, 74, 76, 82, 88,
    8, 10, 16, 20, 24, 28, 34, 40, 46, 52, 58, 64, 70, 74, 78, 86, 88,
    4, 10, 14, 16, 22, 26, 28, 30, 34, 38, 42, 52, 58, 62, 64, 70, 76, 78, 82,
    8, 12, 16, 22, 28, 30, 32, 36, 44, 46, 52, 58, 64, 68, 72, 74, 76, 84, 88, 92,
    8, 10, 18, 22, 30, 32, 36, 40, 48, 52, 54, 58, 60, 64, 66, 70, 74, 76, 80, 82, 88, 90,
    2, 6, 14, 18, 22, 26, 28, 34, 38, 42, 48, 52, 64, 66, 70, 74, 84, 86, 94,
};
int aerGroundType[187] = {
    0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0,
    1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1,
    0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1,
    0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1,
    0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1,
};
int aerStageGroundStart[10] = { 0, 11, 30, 50, 70, 90, 107, 126, 146, 168 };
int aerStageGroundCount[10] = { 11, 19, 20, 20, 20, 17, 19, 20, 22, 19 };

// EnemyFighter's own per-type table (x, dx, bulletOffset, pattern, point).
int aerEfTypeX[2] = { AER_STAGE_WIDTH - 1, 0 };
int aerEfTypeDx[2] = { -1, 1 };
int aerEfTypeBulletOffset[2] = { 0, 1 };
int aerEfTypePattern[2] = { AER_CHAR_FIGHTER_LEFT, AER_CHAR_FIGHTER_RIGHT };
int aerEfTypePoint[2] = { 10, 15 };

// GroundEnemy's own per-type table (pattern, point).
int aerGeTypePattern[2] = { AER_CHAR_TRACK, AER_CHAR_ROCKET };
int aerGeTypePoint[2] = { 5, 20 };

// Math.cpp's own 32-entry pseudo-random cycling table - byte-for-byte
// identical to Cracky's own copy (shared "cate" engine).
int aerRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int aerScore;
int aerRemainCount;
int aerCurrentStage;
int aerClock;
int aerTickCounter;
int aerState;
int aerSelection;
bool aerSelectionChanged;
int aerPrevLeft, aerPrevRight, aerPrevUp, aerPrevDown, aerPrevFire;
bool aerPendingContinue;

int aerRndIndex;

// Stage/background - row-major, aerBgIdx(x,y) = y*AER_BACKGROUND_WIDTH+x.
int aerBackground[AER_BACKGROUND_WIDTH * AER_STAGE_HEIGHT];
int aerGroundYs[AER_BACKGROUND_WIDTH];
int aerMapX;
int aerMapBits;
int aerNextGroundY;
int aerMinGroundY;
int aerBgBytePtr;
int aerStageIndex;
int aerSkyElementCount;
int aerSkyElementPtr;
int aerSkyEnemyType;
int aerSkyEnemyBit;
int aerGroundElementCount;
int aerGroundElementPtr;

// VVram / sprites / status text grid.
int aerVVram[AER_VVRAM_HEIGHT][AER_VVRAM_WIDTH];

typedef struct
{
    int x, y, code;
} AerSprite;
AerSprite aerSprites[AER_SPRITE_COUNT];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize=0x100 selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row). See gameCracky.c's own crkStatusChar header comment (sibling
// project) for the full derivation of why this is 32 columns wide, not 8.
int aerStatusChar[8][32];

// Set true only while on the title screen (AER_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its initial
// ClearScreen(), and instead drives the ENTIRE screen (not just the status
// zone) through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range (MINI at col 19, START/CONTINUE
// at col 9, the credit line at col 12 - all inside what during gameplay is
// the map area). When true, aerComposeRawByte() reads aerStatusChar across
// the full width instead of just the narrow status-only zone (cols 24-31).
bool aerFullWidthText;

bool aerOverlayActive;
int aerOverlayText[10];
int aerOverlayLen;
int aerOverlayPage;
int aerOverlayCol;

// Print.cpp's own file-local statics, hoisted to globals (no static locals
// in this dialect - matching this project's own established rule).
bool aerZeroVisible;
int aerByteValue;
int aerWordValue;

// Sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of AER_TICK_DIVISOR.
int aerSeqMelody[3];
int aerSeqPos[3];
int aerSeqWait[3];
int aerSeqActive[3];

// Entities.
typedef struct
{
    int x, y, sprite, clock;
} AerMovable;
AerMovable aerMyFighter;
int aerMyFighterDy;
int aerMyFighterPattern;
int aerCrashCount;
int aerReviveCount;

AerMovable aerItem;

typedef struct
{
    int x, y, sprite, clock, dx, dy, numeratorX, denominatorX, numeratorY, denominatorY;
} AerBullet;
AerBullet aerMyBullets[AER_MB_COUNT];
int aerMyBulletIntervalCount;
AerBullet aerEnemyBullets[AER_EB_COUNT];

typedef struct
{
    int x, y, sprite, clock, type;
} AerEnemyFighter;
AerEnemyFighter aerEnemyFighters[AER_EF_COUNT];

typedef struct
{
    int x, y, sprite, clock, type;
} AerGroundEnemy;
AerGroundEnemy aerGroundEnemies[AER_GE_COUNT];

typedef struct
{
    int x, y, targetX, life, clock;
} AerFort;
AerFort aerForts[AER_FORT_COUNT];
int aerFortCount;
bool aerFortsMoving;

typedef struct
{
    int x, y, sprite, clock, pattern;
} AerBang;
AerBang aerBangs[AER_BANG_COUNT];


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int aerRnd()
{
    int r;
    r = aerRndNumbers[ aerRndIndex ];
    aerRndIndex = aerRndIndex + 1;
    if( aerRndIndex >= 32 )
      aerRndIndex = 0;
    return r & 0x0f;
}

int aerAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int aerSign( int from, int to )
{
    if( from == to )
      return 0;
    if( from < to )
      return 1;
    return -1;
}


// -----------------------------------------------------------------------------
//   Sound sequencer (Sound.cpp) - see the sibling project's own header
//   comment for the derivation.
// -----------------------------------------------------------------------------

int aerNoteFrames( int length )
{
    return length * 2;
}

void aerStartSeq( int channel, int melodyId )
{
    aerSeqMelody[ channel ] = melodyId;
    aerSeqPos[ channel ] = 0;
    aerSeqWait[ channel ] = 0;
    aerSeqActive[ channel ] = 1;
}

void aerStopSeq( int channel )
{
    aerSeqActive[ channel ] = 0;
    aerSeqMelody[ channel ] = AER_MELODY_NONE;
}

bool aerSeqPlaying( int channel )
{
    return aerSeqActive[ channel ] != 0;
}

int aerMelodyLength( int id )
{
    if( id == AER_MELODY_FIRE ) return 13;
    if( id == AER_MELODY_UP ) return 13;
    if( id == AER_MELODY_START ) return 25;
    if( id == AER_MELODY_GAMEOVER ) return 27;
    if( id == AER_MELODY_BGM1 ) return 169;
    if( id == AER_MELODY_BGM2 ) return 257;
    return 0;
}

int aerMelodyValue( int id, int idx )
{
    if( id == AER_MELODY_FIRE ) return aerMelodyFire[ idx ];
    if( id == AER_MELODY_UP ) return aerMelodyUp[ idx ];
    if( id == AER_MELODY_START ) return aerMelodyStart[ idx ];
    if( id == AER_MELODY_GAMEOVER ) return aerMelodyGameOver[ idx ];
    if( id == AER_MELODY_BGM1 ) return aerMelodyBgm1[ idx ];
    if( id == AER_MELODY_BGM2 ) return aerMelodyBgm2[ idx ];
    return 0;
}

void aerAdvanceOneSeq( int channel )
{
    int length, note;

    if( aerSeqActive[ channel ] == 0 ) return;

    if( aerSeqWait[ channel ] > 0 )
    {
        aerSeqWait[ channel ] = aerSeqWait[ channel ] - 1;
        return;
    }

    length = aerMelodyValue( aerSeqMelody[ channel ], aerSeqPos[ channel ] );
    if( length == 0 )
    {
        aerStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        aerSeqPos[ channel ] = 0;
        length = aerMelodyValue( aerSeqMelody[ channel ], 0 );
    }
    note = aerMelodyValue( aerSeqMelody[ channel ], aerSeqPos[ channel ] + 1 );
    aerSeqPos[ channel ] = aerSeqPos[ channel ] + 2;
    aerSeqWait[ channel ] = aerNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)aerFrequencies[ note - 1 ], (float)aerSeqWait[ channel ] / 60.0 );
}

void aerAdvanceSound()
{
    aerAdvanceOneSeq( 0 );
    aerAdvanceOneSeq( 1 );
    aerAdvanceOneSeq( 2 );
}

void aerSoundFire()
{
    aerStartSeq( 0, AER_MELODY_FIRE );
}

void aerSoundUp()
{
    aerStartSeq( 0, AER_MELODY_UP );
}

// Sound_SmallBang()/Sound_LargeBang() are real Effect-channel noise bursts
// upstream, with a decaying-volume envelope no single md_playTone() call
// can reproduce - approximated as a short, fixed-duration flat tone at a
// representative pitch instead of the real ~1s decay, a deliberate
// simplification rather than a derived value (matches the sibling
// project's own reasoning).
void aerSoundSmallBang()
{
    md_playTone( 3000.0, 0.15 );
}

void aerSoundLargeBang()
{
    md_playTone( 1500.0, 0.25 );
}

void aerStartBgm()
{
    aerStartSeq( 1, AER_MELODY_BGM1 );
    aerStartSeq( 2, AER_MELODY_BGM2 );
}

void aerStopBgm()
{
    aerStopSeq( 1 );
    aerStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Stage.cpp - accessors
// -----------------------------------------------------------------------------

int aerBgIdx( int x, int y )
{
    return y * AER_BACKGROUND_WIDTH + x;
}

int aerGroundY( int x )
{
    return aerGroundYs[ x ];
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void aerHideAllSprites()
{
    int i;
    for( i = 0; i < AER_SPRITE_COUNT; i = i + 1 )
      aerSprites[ i ].code = AER_INVALID_CODE;
}

void aerShowSpriteAt( int spriteIndex, int x, int y, int code )
{
    if( spriteIndex >= AER_SPRITE_COUNT ) return;
    aerSprites[ spriteIndex ].x = x;
    aerSprites[ spriteIndex ].y = y;
    aerSprites[ spriteIndex ].code = code;
}

void aerHideSprite( int index )
{
    if( index >= AER_SPRITE_COUNT ) return;
    aerSprites[ index ].code = AER_INVALID_CODE;
}

// Negative-coordinate guards added beyond upstream's own bounds checks
// (which only ever needed an upper bound, relying on AVR byte-underflow to
// make a "negative" coordinate a huge, safely-rejected positive one - not
// true for this port's plain int arithmetic) - see the sibling project's
// own header comment for the full derivation.
void aerDrawSpritesIntoVVram()
{
    int i;
    for( i = 0; i < AER_SPRITE_COUNT; i = i + 1 )
    {
        if( aerSprites[ i ].code != AER_INVALID_CODE )
        {
            int c, x, y;
            c = aerSprites[ i ].code;
            if( c < AER_CHAR_FIGHTER )
            {
                x = aerSprites[ i ].x >> AER_BULLET_SHIFT;
                if( x < 0 || x >= AER_VVRAM_WIDTH ) continue;
                y = aerSprites[ i ].y >> AER_BULLET_SHIFT;
                if( y < 0 || y >= AER_VVRAM_HEIGHT ) continue;
                aerVVram[ y ][ x ] = c;
            }
            else
            {
                int row, cy;
                x = aerSprites[ i ].x;
                if( x < 0 || x >= AER_VVRAM_WIDTH ) continue;
                y = aerSprites[ i ].y;
                if( y < 0 || y >= AER_VVRAM_HEIGHT ) continue;
                cy = y;
                for( row = 0; row < 2; row = row + 1 )
                {
                    int col, cx, cc;
                    cx = x;
                    cc = c + row * 2;
                    for( col = 0; col < 2; col = col + 1 )
                    {
                        if( cx >= 0 && cx < AER_VVRAM_WIDTH && cy >= 0 && cy < AER_VVRAM_HEIGHT )
                          aerVVram[ cy ][ cx ] = cc;
                        cc = cc + 1;
                        cx = cx + 1;
                    }
                    cy = cy + 1;
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp
// -----------------------------------------------------------------------------

int aerAsciiIndex( int c )
{
    int table[27] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V',
    };
    int i;
    for( i = 0; i < 27; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

// Defensive bounds clamp on col (upstream's own real Vram-based status/
// title-text grid is 32 char-cells wide - see aerStatusChar's own header
// comment) - every placement in this port's own title/status text was
// deliberately chosen to stay within [0,31], but this catches any future
// mistake safely instead of silently corrupting the next row.
int aerPrintC( int page, int col, int c )
{
    if( col >= 0 && col < 32 )
      aerStatusChar[ page ][ col ] = aerAsciiIndex( c );
    return col + 1;
}

int aerPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = aerPrintC( page, col, s[ i ] );
    return col;
}

int aerPrintDigitB( int page, int col, int n )
{
    int c;
    c = aerByteValue / n;
    aerByteValue = aerByteValue % n;
    if( c == 0 )
    {
        if( aerZeroVisible )
          c = '0';
        else
          c = ' ';
    }
    else
    {
        aerZeroVisible = true;
        c = c + '0';
    }
    return aerPrintC( page, col, c );
}

int aerPrintByteNumber2( int page, int col, int b )
{
    aerZeroVisible = false;
    aerByteValue = b;
    col = aerPrintDigitB( page, col, 10 );
    col = aerPrintC( page, col, aerByteValue + '0' );
    return col;
}

int aerPrintDigitW( int page, int col, int n )
{
    int c;
    c = aerWordValue / n;
    aerWordValue = aerWordValue % n;
    if( c == 0 )
    {
        if( aerZeroVisible )
          c = '0';
        else
          c = ' ';
    }
    else
    {
        aerZeroVisible = true;
        c = c + '0';
    }
    return aerPrintC( page, col, c );
}

int aerPrintNumber5( int page, int col, int w )
{
    aerZeroVisible = false;
    aerWordValue = w;
    col = aerPrintDigitW( page, col, 10000 );
    col = aerPrintDigitW( page, col, 1000 );
    col = aerPrintDigitW( page, col, 100 );
    col = aerPrintDigitW( page, col, 10 );
    col = aerPrintC( page, col, aerWordValue + '0' );
    return col;
}

// All column arguments below are real upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2=26, LeftX+6=30), not an arbitrary local 0-7 offset.
void aerPrintScore()
{
    int col;
    col = aerPrintNumber5( 1, 26, aerScore );
    aerPrintC( 1, col, '0' );
}

void aerPrintStage()
{
    aerPrintByteNumber2( 3, 30, aerCurrentStage + 1 );
}

// Simplified from upstream's own 2x2-sprite "Char_Remain" icon (Put2C) to
// a plain status-text digit, matching Cracky's own established
// simplification for the identical shape of upstream code - unlike
// Cracky's own version (which leaves lives<=2 blank), this always shows
// the count directly, a deliberate, slightly more informative choice
// (matches the sibling project's own porting decision).
void aerPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    aerPrintS( 0, 24, sScore, 5 );
    aerPrintS( 3, 24, sStage, 5 );
    if( aerRemainCount > 1 )
    {
        int i;
        i = aerRemainCount - 1;
        aerPrintC( 6, 24, i + '0' );
    }
    aerPrintScore();
    aerPrintStage();
}

void aerBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    aerOverlayActive = true;
    aerOverlayLen = len;
    aerOverlayPage = page;
    aerOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      aerOverlayText[ i ] = s[ i ];
}

void aerPrintGameOverOverlay()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    aerBeginOverlay( s, 9, 4, 8 );
}

// Placed here (not down near Fort, its main caller) so aerHitBulletFort()/
// aerHitBulletEnemyFighter()/aerHitBulletGroundEnemy() - all defined
// further below - can call it without needing a forward declaration.
void aerAddScore( int pts )
{
    aerScore = aerScore + pts;
    aerPrintScore();
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void aerShowBang( int i )
{
    aerShowSpriteAt( aerBangs[ i ].sprite, aerBangs[ i ].x, aerBangs[ i ].y, aerBangs[ i ].pattern );
}

void aerInitBangs()
{
    int sprite, i;
    sprite = AER_SPRITE_BANG;
    for( i = 0; i < AER_BANG_COUNT; i = i + 1 )
    {
        aerBangs[ i ].y = AER_BANG_INVALID_Y;
        aerBangs[ i ].sprite = sprite;
        sprite = sprite + 1;
    }
}

void aerStartBangAt( int x, int y, int pattern )
{
    int i;
    for( i = 0; i < AER_BANG_COUNT; i = i + 1 )
    {
        if( aerBangs[ i ].y != AER_BANG_INVALID_Y ) continue;
        aerBangs[ i ].x = x;
        aerBangs[ i ].y = y;
        aerBangs[ i ].clock = 0;
        aerBangs[ i ].pattern = pattern;
        aerShowBang( i );
        return;
    }
}

void aerStartSmallBang( int x, int y )
{
    aerStartBangAt( x - AER_BANG_SIZE, y - AER_BANG_SIZE, AER_CHAR_SMALL_BANG );
}

void aerStartLargeBang( int x, int y )
{
    aerStartBangAt( x - AER_BANG_SIZE * 2, y - AER_BANG_SIZE * 2, AER_CHAR_LARGE_BANG );
    aerStartBangAt( x, y - AER_BANG_SIZE * 2, AER_CHAR_LARGE_BANG + 4 );
    aerStartBangAt( x - AER_BANG_SIZE * 2, y, AER_CHAR_LARGE_BANG + 8 );
    aerStartBangAt( x, y, AER_CHAR_LARGE_BANG + 12 );
}

void aerUpdateBangs()
{
    int i;
    for( i = 0; i < AER_BANG_COUNT; i = i + 1 )
    {
        if( aerBangs[ i ].y == AER_BANG_INVALID_Y ) continue;
        aerBangs[ i ].clock = aerBangs[ i ].clock + 1;
        if( aerBangs[ i ].clock >= AER_BANG_TIME )
        {
            aerHideSprite( aerBangs[ i ].sprite );
            aerBangs[ i ].y = AER_BANG_INVALID_Y;
        }
        else
          aerShowBang( i );
    }
}


// -----------------------------------------------------------------------------
//   Bullet.cpp - shared physics/state for MyBullet and EnemyBullet.
// -----------------------------------------------------------------------------

void aerEndBullet( AerBullet* pBullet )
{
    pBullet->y = AER_INVALID_Y;
    aerHideSprite( pBullet->sprite );
}

void aerStartBullet( AerBullet* pBullet, int x, int y )
{
    pBullet->x = x;
    pBullet->y = y;
    pBullet->clock = 0;
    pBullet->denominatorX = 0;
    pBullet->denominatorY = 0;
}

// x<0/y<0 exit conditions added ahead of upstream's own >=RangeX/groundY
// checks - a real, reachable AVR-byte-underflow-reliance bug otherwise (a
// leftward/upward bullet leaving the screen never gets recognized as
// out-of-range, and its ever-more-negative x eventually indexes
// aerGroundY() out of bounds) - see the sibling project's own header
// comment for the full derivation.
bool aerMoveBullet( AerBullet* pBullet )
{
    pBullet->denominatorX = pBullet->denominatorX - pBullet->numeratorX;
    while( pBullet->denominatorX < 0 )
    {
        pBullet->x = pBullet->x + pBullet->dx;
        pBullet->denominatorX = pBullet->denominatorX + AER_HI_VELOCITY;
    }
    pBullet->denominatorY = pBullet->denominatorY - pBullet->numeratorY;
    while( pBullet->denominatorY < 0 )
    {
        pBullet->y = pBullet->y + pBullet->dy;
        pBullet->denominatorY = pBullet->denominatorY + AER_HI_VELOCITY;
    }
    if(
        pBullet->x < 0 || pBullet->y < 0 ||
        pBullet->x >= ( AER_STAGE_WIDTH - 1 ) * AER_BULLET_RATE ||
        ( pBullet->y >> AER_BULLET_SHIFT ) >= aerGroundY( pBullet->x >> AER_BULLET_SHIFT )
      )
      return false;
    return true;
}

bool aerHitMyBulletBox( int movableX, int movableY, int bulletX, int bulletY )
{
    int xx, yy;
    xx = movableX << AER_BULLET_SHIFT;
    yy = movableY << AER_BULLET_SHIFT;
    return
        bulletX >= xx && bulletX < xx + 2 * AER_BULLET_RATE &&
        bulletY >= yy && bulletY < yy + 2 * AER_BULLET_RATE;
}


// -----------------------------------------------------------------------------
//   MyFighter.cpp - Init/Show/Start/Crash/hit-tests (movement deferred).
// -----------------------------------------------------------------------------

void aerShowMyFighter()
{
    if(
        aerCrashCount != 0 ||
        ( aerReviveCount != 0 && ( aerMyFighter.clock & AER_REVIVE_MASK ) == 0 )
      )
      aerHideSprite( AER_SPRITE_MYFIGHTER );
    else
      aerShowSpriteAt( AER_SPRITE_MYFIGHTER, aerMyFighter.x, aerMyFighter.y, aerMyFighterPattern );
}

void aerStartMyFighter()
{
    aerMyFighter.x = AER_MYFIGHTER_INITIAL_X;
    aerMyFighter.y = AER_MYFIGHTER_INITIAL_Y;
    aerMyFighterPattern = AER_CHAR_FIGHTER_RIGHT;
    aerMyFighterDy = 0;
    aerCrashCount = 0;
    aerShowMyFighter();
}

void aerInitMyFighter()
{
    aerMyFighter.sprite = AER_SPRITE_MYFIGHTER;
    aerMyFighter.clock = 0;
    aerReviveCount = 0;
    aerStartMyFighter();
}

void aerCrashMyFighter()
{
    aerSoundLargeBang();
    aerStartLargeBang( aerMyFighter.x + 1, aerMyFighter.y + 1 );
    aerHideSprite( AER_SPRITE_MYFIGHTER );
    aerCrashCount = AER_CRASH_TIME;
}

bool aerHitMovableMyFighter( int x, int y )
{
    if(
        aerCrashCount == 0 && aerReviveCount == 0 &&
        x + 1 >= aerMyFighter.x && x < aerMyFighter.x + 1 * 2 &&
        y + 1 >= aerMyFighter.y && y < aerMyFighter.y + 1 * 2
      )
    {
        aerCrashMyFighter();
        return true;
    }
    return false;
}

bool aerHitBulletMyFighter( int x, int y )
{
    if( aerCrashCount == 0 && aerReviveCount == 0 )
    {
        int xx, yy;
        xx = aerMyFighter.x << AER_BULLET_SHIFT;
        yy = aerMyFighter.y << AER_BULLET_SHIFT;
        if( x >= xx + 1 && x < xx + 3 && y >= yy + 0 && y < yy + 3 )
        {
            aerCrashMyFighter();
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Item.cpp - fully self-contained (MoveItem only ever reads aerMyFighter's
//   own fields directly, never calls one of its functions).
// -----------------------------------------------------------------------------

void aerShowItem()
{
    aerShowSpriteAt( aerItem.sprite, aerItem.x, aerItem.y, AER_CHAR_ITEM );
}

void aerEndItem()
{
    aerHideSprite( aerItem.sprite );
    aerItem.y = AER_INVALID_Y;
}

void aerInitItem()
{
    aerItem.y = AER_INVALID_Y;
    aerItem.sprite = AER_SPRITE_ITEM;
}

void aerStartItem( int x, int y )
{
    if( aerItem.y != AER_INVALID_Y ) return;
    aerItem.x = x;
    aerItem.y = y;
    aerShowItem();
}

// x<0 added to the exit check - otherwise a missed item never properly
// ends, and StartItem()'s own "one live item at a time" guard stays
// tripped forever, silently blocking every future item for the rest of
// the game (see the sibling project's own header comment).
void aerMoveItem()
{
    if( aerItem.y != AER_INVALID_Y )
    {
        aerItem.x = aerItem.x - 1;
        if( aerItem.x < 0 || aerItem.x >= AER_VVRAM_WIDTH )
        {
            aerEndItem();
            return;
        }
        if(
            aerItem.x + 1 >= aerMyFighter.x && aerItem.x < aerMyFighter.x + 2 &&
            aerItem.y + 1 >= aerMyFighter.y && aerItem.y < aerMyFighter.y + 2
          )
        {
            aerEndItem();
            aerSoundUp();
            aerRemainCount = aerRemainCount + 1;
            aerPrintStatus();
            return;
        }
        aerShowItem();
    }
}


// -----------------------------------------------------------------------------
//   MyBullet.cpp - Init/Show/StartMyBullet (movement deferred).
// -----------------------------------------------------------------------------

void aerShowMyBullet( int i )
{
    aerShowSpriteAt(
        aerMyBullets[ i ].sprite, aerMyBullets[ i ].x, aerMyBullets[ i ].y,
        AER_CHAR_BULLET + ( aerMyBullets[ i ].x & 1 ) + ( ( aerMyBullets[ i ].y & 1 ) << 1 )
      );
}

void aerInitMyBullets()
{
    int sprite, i;
    sprite = AER_SPRITE_MYBULLET;
    for( i = 0; i < AER_MB_COUNT; i = i + 1 )
    {
        aerMyBullets[ i ].sprite = sprite;
        aerMyBullets[ i ].y = AER_INVALID_Y;
        sprite = sprite + 1;
    }
    aerMyBulletIntervalCount = AER_MB_SHORT_INTERVAL;
}

void aerStartMyBullet( bool on )
{
    int i;
    if( aerMyBulletIntervalCount != 0 )
      aerMyBulletIntervalCount = aerMyBulletIntervalCount - 1;
    if( !on )
    {
        if( aerMyBulletIntervalCount > AER_MB_SHORT_INTERVAL )
          aerMyBulletIntervalCount = AER_MB_SHORT_INTERVAL;
        return;
    }
    if( aerMyBulletIntervalCount != 0 ) return;
    for( i = 0; i < AER_MB_COUNT; i = i + 1 )
    {
        if( aerMyBullets[ i ].y != AER_INVALID_Y ) continue;
        aerSoundFire();
        aerStartBullet( &aerMyBullets[ i ], ( aerMyFighter.x + 1 ) << AER_BULLET_SHIFT, ( aerMyFighter.y + 1 ) << AER_BULLET_SHIFT );
        aerMyBullets[ i ].dx = 1;
        aerMyBullets[ i ].dy = aerMyFighterDy;
        if( aerMyFighterDy != 0 )
        {
            aerMyBullets[ i ].numeratorX = AER_LO_VELOCITY * 4 / 3;
            aerMyBullets[ i ].numeratorY = AER_LO_VELOCITY * 4 / 3;
        }
        else
        {
            aerMyBullets[ i ].numeratorX = AER_HI_VELOCITY * 4 / 3;
            aerMyBullets[ i ].numeratorY = AER_HI_VELOCITY * 4 / 3;
        }
        aerShowMyBullet( i );
        aerMyBulletIntervalCount = AER_MB_LONG_INTERVAL;
        return;
    }
}


// -----------------------------------------------------------------------------
//   EnemyBullet.cpp - Init/Show/StartEnemyBullet (movement deferred).
// -----------------------------------------------------------------------------

void aerShowEnemyBullet( int i )
{
    aerShowSpriteAt(
        aerEnemyBullets[ i ].sprite, aerEnemyBullets[ i ].x, aerEnemyBullets[ i ].y,
        AER_CHAR_BULLET + ( aerEnemyBullets[ i ].x & 1 ) + ( ( aerEnemyBullets[ i ].y & 1 ) << 1 )
      );
}

void aerInitEnemyBullets()
{
    int sprite, i;
    sprite = AER_SPRITE_ENEMYBULLET;
    for( i = 0; i < AER_EB_COUNT; i = i + 1 )
    {
        aerEnemyBullets[ i ].sprite = sprite;
        aerEnemyBullets[ i ].y = AER_INVALID_Y;
        sprite = sprite + 1;
    }
}

AerBullet* aerStartEnemyBullet( int x, int y )
{
    int i;
    for( i = 0; i < AER_EB_COUNT; i = i + 1 )
    {
        if( aerEnemyBullets[ i ].y != AER_INVALID_Y ) continue;
        aerStartBullet( &aerEnemyBullets[ i ], x, y );
        aerShowEnemyBullet( i );
        return &aerEnemyBullets[ i ];
    }
    return NULL;
}


// -----------------------------------------------------------------------------
//   Fort.cpp - Init/Draw/StartForts/HitBulletFort (Fire/MoveForts deferred).
// -----------------------------------------------------------------------------

void aerInitForts()
{
    int i;
    for( i = 0; i < AER_FORT_COUNT; i = i + 1 )
      aerForts[ i ].y = AER_INVALID_Y;
    aerFortCount = 0;
    aerFortsMoving = false;
}

void aerDrawForts()
{
    int i;
    for( i = 0; i < AER_FORT_COUNT; i = i + 1 )
    {
        int x, y, c, row;
        x = aerForts[ i ].x;
        y = aerForts[ i ].y;
        if( y == AER_INVALID_Y || x >= AER_VVRAM_WIDTH ) continue;
        c = AER_CHAR_FORT;
        for( row = 0; row < AER_FORT_HEIGHT; row = row + 1 )
        {
            int col, cx;
            cx = x;
            for( col = 0; col < AER_FORT_WIDTH; col = col + 1 )
            {
                if( cx < AER_VVRAM_WIDTH && ( y + row ) < AER_VVRAM_HEIGHT )
                  aerVVram[ y + row ][ cx ] = c;
                c = c + 1;
                cx = cx + 1;
            }
        }
    }
}

void aerStartForts()
{
    int fortIndex, x, y, remaining;
    aerFortCount = 0;
    remaining = ( aerCurrentStage + 2 ) >> 1;
    if( remaining > AER_FORT_COUNT )
      remaining = AER_FORT_COUNT;
    fortIndex = 0;
    x = AER_VVRAM_WIDTH;
    y = 0;
    while( true )
    {
        int targetX;
        targetX = x - ( AER_VVRAM_WIDTH - AER_FORT_LEFT );
        if( targetX > AER_VVRAM_WIDTH - AER_FORT_WIDTH ) break;
        aerForts[ fortIndex ].x = x;
        aerForts[ fortIndex ].targetX = targetX;
        aerForts[ fortIndex ].y = y;
        aerForts[ fortIndex ].life = AER_FORT_MAX_LIFE;
        aerForts[ fortIndex ].clock = 0;
        aerFortCount = aerFortCount + 1;
        fortIndex = fortIndex + 1;
        y = y + AER_FORT_HEIGHT;
        if( y + AER_FORT_HEIGHT > aerMinGroundY )
        {
            x = x + AER_FORT_WIDTH;
            y = 0;
        }
        remaining = remaining - 1;
        if( remaining == 0 ) break;
    }
    aerFortsMoving = true;
}

// Note: upstream reuses this exact function ("does a bullet-space point
// hit a fort") from MyFighter's own movement too, treating the fighter's
// own position as if it were a bullet - crashing into a fort's turret
// directly damages/destroys it exactly the same as being shot, a real
// upstream design choice preserved faithfully here.
bool aerHitBulletFort( int x, int y )
{
    int xx, yy, i;
    xx = ( x >> AER_BULLET_SHIFT ) - 1;
    yy = ( y + AER_BULLET_RATE / 2 ) >> AER_BULLET_SHIFT;
    for( i = 0; i < AER_FORT_COUNT; i = i + 1 )
    {
        if( aerForts[ i ].y == AER_INVALID_Y ) continue;
        if(
            xx >= aerForts[ i ].x && xx < aerForts[ i ].x + ( AER_FORT_WIDTH - 2 ) &&
            yy >= aerForts[ i ].y && yy < aerForts[ i ].y + AER_FORT_HEIGHT
          )
        {
            aerForts[ i ].life = aerForts[ i ].life - 1;
            if( aerForts[ i ].life == 0 )
            {
                aerSoundLargeBang();
                aerStartLargeBang( aerForts[ i ].x + AER_FORT_WIDTH / 2, aerForts[ i ].y + AER_FORT_HEIGHT / 2 );
                aerForts[ i ].y = AER_INVALID_Y;
                aerFortCount = aerFortCount - 1;
                aerAddScore( AER_FORT_PTS );
            }
            else
            {
                aerSoundSmallBang();
                aerStartSmallBang( ( x >> AER_BULLET_SHIFT ) + 1, ( y >> AER_BULLET_SHIFT ) + 1 );
            }
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   EnemyFighter.cpp - Init/Show/End/StartEnemyFighter/hit-test (movement
//   deferred).
// -----------------------------------------------------------------------------

void aerShowEnemyFighter( int i )
{
    aerShowSpriteAt( aerEnemyFighters[ i ].sprite, aerEnemyFighters[ i ].x, aerEnemyFighters[ i ].y, aerEfTypePattern[ aerEnemyFighters[ i ].type ] );
}

void aerEndEnemyFighter( int i )
{
    aerEnemyFighters[ i ].y = AER_INVALID_Y;
    aerHideSprite( aerEnemyFighters[ i ].sprite );
}

void aerInitEnemyFighters()
{
    int sprite, i;
    sprite = AER_SPRITE_ENEMYFIGHTER;
    for( i = 0; i < AER_EF_COUNT; i = i + 1 )
    {
        aerEnemyFighters[ i ].sprite = sprite;
        aerEnemyFighters[ i ].y = AER_INVALID_Y;
        sprite = sprite + 1;
    }
}

void aerStartEnemyFighter( int type )
{
    int y, i;
    y = aerRnd() & 0x0f;
    if( y >= aerMinGroundY ) return;
    for( i = 0; i < AER_EF_COUNT; i = i + 1 )
    {
        if( aerEnemyFighters[ i ].y != AER_INVALID_Y ) continue;
        aerEnemyFighters[ i ].type = type;
        aerEnemyFighters[ i ].x = aerEfTypeX[ type ];
        aerEnemyFighters[ i ].y = y;
        aerEnemyFighters[ i ].clock = 0;
        aerShowEnemyFighter( i );
        return;
    }
}

bool aerHitBulletEnemyFighter( int x, int y )
{
    int i;
    for( i = 0; i < AER_EF_COUNT; i = i + 1 )
    {
        if( aerEnemyFighters[ i ].y == AER_INVALID_Y ) continue;
        if( aerHitMyBulletBox( aerEnemyFighters[ i ].x, aerEnemyFighters[ i ].y, x, y ) )
        {
            int pts;
            pts = aerEfTypePoint[ aerEnemyFighters[ i ].type ];
            aerSoundSmallBang();
            aerStartSmallBang( aerEnemyFighters[ i ].x + 1, aerEnemyFighters[ i ].y + 1 );
            aerEndEnemyFighter( i );
            aerAddScore( pts );
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   GroundEnemy.cpp - Init/Show/End/StartGroundEnemy/hit-test (movement
//   deferred - needs aerStartItem, already defined above).
// -----------------------------------------------------------------------------

void aerShowGroundEnemy( int i )
{
    aerShowSpriteAt( aerGroundEnemies[ i ].sprite, aerGroundEnemies[ i ].x, aerGroundEnemies[ i ].y, aerGeTypePattern[ aerGroundEnemies[ i ].type & 0x0f ] );
}

void aerEndGroundEnemy( int i )
{
    aerGroundEnemies[ i ].y = AER_INVALID_Y;
    aerHideSprite( aerGroundEnemies[ i ].sprite );
}

void aerInitGroundEnemies()
{
    int sprite, i;
    sprite = AER_SPRITE_GROUNDENEMY;
    for( i = 0; i < AER_GE_COUNT; i = i + 1 )
    {
        aerGroundEnemies[ i ].sprite = sprite;
        aerGroundEnemies[ i ].y = AER_INVALID_Y;
        sprite = sprite + 1;
    }
}

void aerStartGroundEnemy( int y, int type )
{
    int i;
    for( i = 0; i < AER_GE_COUNT; i = i + 1 )
    {
        if( aerGroundEnemies[ i ].y != AER_INVALID_Y ) continue;
        aerGroundEnemies[ i ].type = type;
        aerGroundEnemies[ i ].x = AER_STAGE_WIDTH - 1;
        aerGroundEnemies[ i ].y = y - 2;
        aerGroundEnemies[ i ].clock = 0;
        aerShowGroundEnemy( i );
        return;
    }
}

bool aerHitBulletGroundEnemy( int x, int y )
{
    int i;
    for( i = 0; i < AER_GE_COUNT; i = i + 1 )
    {
        if( aerGroundEnemies[ i ].y == AER_INVALID_Y ) continue;
        if( aerHitMyBulletBox( aerGroundEnemies[ i ].x, aerGroundEnemies[ i ].y, x, y ) )
        {
            int pts;
            if(
                aerRemainCount < 10 &&
                aerGroundEnemies[ i ].type == ( AER_GE_TYPE_ROCKET | 0x80 ) &&
                ( aerRnd() & 1 ) == 0
              )
              aerStartItem( aerGroundEnemies[ i ].x, aerGroundEnemies[ i ].y );
            pts = aerGeTypePoint[ aerGroundEnemies[ i ].type & 0x0f ];
            aerSoundSmallBang();
            aerStartSmallBang( aerGroundEnemies[ i ].x + 1, aerGroundEnemies[ i ].y + 1 );
            aerEndGroundEnemy( i );
            aerAddScore( pts );
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - terrain generation (needs every entity's own Init* function
//   above, for aerInitGameState()).
// -----------------------------------------------------------------------------

void aerFillSpacesAt( int xStart, int count )
{
    int row;
    if( count < 0 ) count = 0;
    for( row = 0; row < count; row = row + 1 )
    {
        aerBackground[ aerBgIdx( xStart + 0, row ) ] = AER_CHAR_SPACE;
        aerBackground[ aerBgIdx( xStart + 1, row ) ] = AER_CHAR_SPACE;
    }
}

void aerFillTileAt( int xStart, int yStart, int* tile )
{
    int r;
    if( yStart < 0 ) yStart = 0;
    for( r = 0; r < 2; r = r + 1 )
    {
        int row;
        row = yStart + r;
        if( row < AER_STAGE_HEIGHT )
        {
            aerBackground[ aerBgIdx( xStart + 0, row ) ] = tile[ r * 2 + 0 ];
            aerBackground[ aerBgIdx( xStart + 1, row ) ] = tile[ r * 2 + 1 ];
        }
    }
}

void aerFillGroundAt( int xStart, int yStart )
{
    int row;
    if( yStart < 0 ) yStart = 0;
    for( row = yStart; row < AER_STAGE_HEIGHT; row = row + 1 )
    {
        aerBackground[ aerBgIdx( xStart + 0, row ) ] = AER_CHAR_GROUND;
        aerBackground[ aerBgIdx( xStart + 1, row ) ] = AER_CHAR_GROUND;
    }
}

// Generates one fresh 2-column-wide tile pair at the far right edge of
// aerBackground (columns AER_STAGE_WIDTH/AER_STAGE_WIDTH+1, the "off-
// screen buffer" columns beyond the visible AER_STAGE_WIDTH-wide map),
// reading one Up/Down/Flat 2-bit code from the current stage's own byte
// table. mode==3 (never actually produced by any of the 10 hand-authored
// stages' own data, confirmed via the byte-diff-verified extraction) is a
// deliberate no-op, matching upstream's own switch with no matching case.
void aerFillTiles()
{
    int mode;
    if( ( aerMapX & 7 ) == 0 )
    {
        aerMapBits = aerStageBytes[ aerStageIndex ][ aerBgBytePtr ];
        aerBgBytePtr = aerBgBytePtr + 1;
    }
    else
      aerMapBits = aerMapBits >> 2;

    mode = aerMapBits & 3;
    if( mode == 1 ) // Up
    {
        aerNextGroundY = aerNextGroundY - 1;
        aerGroundYs[ AER_STAGE_WIDTH ] = aerNextGroundY;
        aerNextGroundY = aerNextGroundY - 1;
        aerGroundYs[ AER_STAGE_WIDTH + 1 ] = aerNextGroundY;
        aerFillSpacesAt( AER_STAGE_WIDTH, aerNextGroundY );
        aerFillTileAt( AER_STAGE_WIDTH, aerNextGroundY, aerUpTile );
        aerFillGroundAt( AER_STAGE_WIDTH, aerNextGroundY + 2 );
    }
    else if( mode == 2 ) // Down
    {
        aerFillSpacesAt( AER_STAGE_WIDTH, aerNextGroundY );
        aerFillTileAt( AER_STAGE_WIDTH, aerNextGroundY, aerDownTile );
        aerGroundYs[ AER_STAGE_WIDTH ] = aerNextGroundY;
        aerNextGroundY = aerNextGroundY + 1;
        aerGroundYs[ AER_STAGE_WIDTH + 1 ] = aerNextGroundY;
        aerNextGroundY = aerNextGroundY + 1;
        aerFillGroundAt( AER_STAGE_WIDTH, aerNextGroundY );
    }
    else if( mode == 0 ) // Flat
    {
        aerGroundYs[ AER_STAGE_WIDTH ] = aerNextGroundY;
        aerGroundYs[ AER_STAGE_WIDTH + 1 ] = aerNextGroundY;
        aerFillSpacesAt( AER_STAGE_WIDTH, aerNextGroundY );
        aerFillGroundAt( AER_STAGE_WIDTH, aerNextGroundY );
    }
}

void aerInitStage()
{
    int i, j;
    i = 0;
    j = 0;
    while( i < aerCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= AER_STAGE_COUNT )
          j = 0;
    }
    aerStageIndex = j;

    aerBgBytePtr = 0;
    aerMapX = 0;
    aerNextGroundY = AER_BOTTOM;
    aerFillTiles();

    aerSkyElementCount = aerStageSkyCount[ aerStageIndex ];
    aerSkyElementPtr = aerStageSkyStart[ aerStageIndex ];
    aerSkyEnemyType = 0;
    aerSkyEnemyBit = 1;
    aerGroundElementCount = aerStageGroundCount[ aerStageIndex ];
    aerGroundElementPtr = aerStageGroundStart[ aerStageIndex ];
}

void aerInitGameState()
{
    int x, y;
    aerRndIndex = 0;
    for( y = 0; y < AER_BOTTOM; y = y + 1 )
    {
        for( x = 0; x < AER_BACKGROUND_WIDTH; x = x + 1 )
          aerBackground[ aerBgIdx( x, y ) ] = AER_CHAR_SPACE;
    }
    for( y = AER_BOTTOM; y < AER_BOTTOM + 2; y = y + 1 )
    {
        for( x = 0; x < AER_BACKGROUND_WIDTH; x = x + 1 )
          aerBackground[ aerBgIdx( x, y ) ] = AER_CHAR_GROUND;
    }
    for( x = 0; x < AER_BACKGROUND_WIDTH; x = x + 1 )
      aerGroundYs[ x ] = AER_BOTTOM;

    aerInitStage();

    // aerPrintStatus() only ever WRITES its own specific label/digit cells
    // (rows 0/1/3/6-col24), it never clears the whole aerStatusChar grid
    // first - so without this, whatever the title screen last left in the
    // OTHER cells (AERIAL/INUFUTO/START/CONTINUE, the title cursor) would
    // silently persist for the entire game, bleeding into the status area
    // right next to the live SCORE/STAGE/lives display. Cleared here,
    // before the first real aerPrintStatus() call of the session (matches
    // the sibling project's own identical fix).
    for( y = 0; y < 8; y = y + 1 )
    {
        for( x = 0; x < 32; x = x + 1 )
          aerStatusChar[ y ][ x ] = 0;
    }
    aerOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in aerUpdateTitle()) - matches aerOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches aerInitGameState() without going through that transition
    // first.
    aerFullWidthText = false;

    aerHideAllSprites();
    aerInitMyFighter();
    aerInitMyBullets();
    aerInitEnemyFighters();
    aerInitEnemyBullets();
    aerInitGroundEnemies();
    aerInitForts();
    aerInitBangs();
    aerInitItem();
}


// -----------------------------------------------------------------------------
//   VVram.cpp
// -----------------------------------------------------------------------------

void aerGroundToVVram()
{
    int y, x;
    for( y = 0; y < AER_VVRAM_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < AER_VVRAM_WIDTH; x = x + 1 )
          aerVVram[ y ][ x ] = aerBackground[ aerBgIdx( x, y ) ];
    }
}

void aerDrawAll()
{
    aerGroundToVVram();
    aerDrawForts();
    aerDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   Group2: per-tick Move* orchestration - every call here only ever
//   references another entity's already-defined Init/Show/Start/hit-test
//   function above it, never another entity's own Move* function (except
//   aerScrollBackground, further below, which needs
//   aerScrollGroundEnemies defined first).
// -----------------------------------------------------------------------------

void aerMoveMyFighter()
{
    bool left, right, up, down;
    if( aerCrashCount != 0 )
    {
        aerCrashCount = aerCrashCount - 1;
        if( aerCrashCount == 0 )
        {
            aerRemainCount = aerRemainCount - 1;
            if( aerRemainCount != 0 )
            {
                aerPrintStatus();
                aerReviveCount = AER_REVIVE_TIME;
                aerStartMyFighter();
            }
        }
        aerMyFighter.clock = aerMyFighter.clock + 1;
        return;
    }

    if( ( aerMyFighter.clock & 1 ) == 0 )
    {
        aerMyFighterDy = 0;
        aerMyFighterPattern = AER_CHAR_FIGHTER_RIGHT;
        left = isLeftPressed();
        right = isRightPressed();
        up = isUpPressed();
        down = isDownPressed();
        if( left && aerMyFighter.x > 0 )
          aerMyFighter.x = aerMyFighter.x - 1;
        if( right && aerMyFighter.x < ( AER_STAGE_WIDTH - 2 ) )
          aerMyFighter.x = aerMyFighter.x + 1;
        if( up && aerMyFighter.y > 0 )
        {
            aerMyFighter.y = aerMyFighter.y - 1;
            aerMyFighterDy = -1;
            aerMyFighterPattern = AER_CHAR_FIGHTER_RIGHT_UP;
        }
        if( down && aerMyFighter.y < ( AER_STAGE_HEIGHT - 2 ) )
        {
            aerMyFighter.y = aerMyFighter.y + 1;
            aerMyFighterDy = 1;
            aerMyFighterPattern = AER_CHAR_FIGHTER_RIGHT_DOWN;
        }
        if( aerMyFighter.y + 2 > aerGroundY( aerMyFighter.x + 1 ) )
          aerCrashMyFighter();
        // Not an else-if - upstream really does check both, and Crash()
        // can genuinely fire twice on one tick (harmless double bang/
        // sound). See aerHitBulletFort's own header comment for why
        // touching a fort here damages it exactly like a bullet would.
        if( aerHitBulletFort( aerMyFighter.x, aerMyFighter.y ) )
          aerCrashMyFighter();
    }
    aerStartMyBullet( isFirePressed() );
    if( aerReviveCount != 0 )
      aerReviveCount = aerReviveCount - 1;
    aerShowMyFighter();
    aerMyFighter.clock = aerMyFighter.clock + 1;
}

// The (clock & 0)==0 gate this call used to have upstream (BulletMask==0
// for this game's BulletShift) is a provable syntactic constant-true,
// simplified away rather than transcribed (see the sibling project's own
// header comment).
void aerMoveMyBullets()
{
    int i;
    for( i = 0; i < AER_MB_COUNT; i = i + 1 )
    {
        if( aerMyBullets[ i ].y == AER_INVALID_Y ) continue;
        if(
            aerMoveBullet( &aerMyBullets[ i ] ) &&
            !(
                aerHitBulletEnemyFighter( aerMyBullets[ i ].x, aerMyBullets[ i ].y ) ||
                aerHitBulletGroundEnemy( aerMyBullets[ i ].x, aerMyBullets[ i ].y ) ||
                aerHitBulletFort( aerMyBullets[ i ].x, aerMyBullets[ i ].y )
              )
          )
        {
            aerShowMyBullet( i );
            aerMyBullets[ i ].clock = aerMyBullets[ i ].clock + 1;
        }
        else
          aerEndBullet( &aerMyBullets[ i ] );
    }
}

// Same simplification as aerMoveMyBullets() above (BulletMask==0).
void aerMoveEnemyBullets()
{
    int i;
    for( i = 0; i < AER_EB_COUNT; i = i + 1 )
    {
        if( aerEnemyBullets[ i ].y == AER_INVALID_Y ) continue;
        if( aerMoveBullet( &aerEnemyBullets[ i ] ) && !aerHitBulletMyFighter( aerEnemyBullets[ i ].x, aerEnemyBullets[ i ].y ) )
        {
            aerShowEnemyBullet( i );
            aerEnemyBullets[ i ].clock = aerEnemyBullets[ i ].clock + 1;
        }
        else
          aerEndBullet( &aerEnemyBullets[ i ] );
    }
}

// x<0 added to the exit check (type0 moves left, starting at the right
// edge, decrementing toward and past 0).
void aerMoveEnemyFighters()
{
    int bottom, i;
    bottom = aerMinGroundY - 4;
    for( i = 0; i < AER_EF_COUNT; i = i + 1 )
    {
        if( aerEnemyFighters[ i ].y == AER_INVALID_Y ) continue;
        if( ( aerEnemyFighters[ i ].clock & 3 ) == 0 )
        {
            int type;
            type = aerEnemyFighters[ i ].type;
            aerEnemyFighters[ i ].x = aerEnemyFighters[ i ].x + aerEfTypeDx[ type ];
            if( aerEnemyFighters[ i ].y > bottom )
              aerEnemyFighters[ i ].y = aerEnemyFighters[ i ].y - 1;
            if(
                aerEnemyFighters[ i ].x < 0 || aerEnemyFighters[ i ].x >= ( AER_STAGE_WIDTH - 1 ) ||
                aerHitMovableMyFighter( aerEnemyFighters[ i ].x, aerEnemyFighters[ i ].y )
              )
            {
                aerEndEnemyFighter( i );
                continue;
            }
            // Upstream re-checks (clock & FireMask)==0 here even though
            // it's already inside the (clock&3)==0 gate above and
            // FireMask is also 3 for this game - a real, harmless
            // upstream redundancy, kept faithfully rather than removed.
            if(
                ( aerEnemyFighters[ i ].clock & 3 ) == 0 &&
                aerEnemyFighters[ i ].y >= 4 &&
                ( aerRnd() << 1 ) < aerCurrentStage + 1 &&
                aerSign( aerEnemyFighters[ i ].x, aerMyFighter.x ) == aerEfTypeDx[ type ]
              )
            {
                AerBullet* pBullet;
                pBullet = aerStartEnemyBullet(
                    ( aerEnemyFighters[ i ].x + aerEfTypeBulletOffset[ type ] ) << AER_BULLET_SHIFT,
                    ( aerEnemyFighters[ i ].y + 1 ) << AER_BULLET_SHIFT
                  );
                if( pBullet != NULL )
                {
                    pBullet->dx = aerEfTypeDx[ type ];
                    if( aerAbs( aerEnemyFighters[ i ].y, aerMyFighter.y ) < 1 )
                    {
                        pBullet->dy = 0;
                        pBullet->numeratorX = AER_HI_VELOCITY * 4 / 3;
                        pBullet->numeratorY = AER_HI_VELOCITY * 4 / 3;
                    }
                    else
                    {
                        pBullet->dy = aerSign( aerEnemyFighters[ i ].y, aerMyFighter.y );
                        pBullet->numeratorX = AER_LONG_VELOCITY * 4 / 3;
                        pBullet->numeratorY = AER_SHORT_VELOCITY * 4 / 3;
                    }
                }
            }
        }
        aerShowEnemyFighter( i );
        aerEnemyFighters[ i ].clock = aerEnemyFighters[ i ].clock + 1;
    }
}

// x>=0 added alongside the existing x<RangeX check (every ground enemy
// scrolls left every call, decrementing toward and past 0).
void aerScrollGroundEnemies()
{
    int i;
    for( i = 0; i < AER_GE_COUNT; i = i + 1 )
    {
        if( aerGroundEnemies[ i ].y == AER_INVALID_Y ) continue;
        aerGroundEnemies[ i ].x = aerGroundEnemies[ i ].x - 1;
        if(
            aerGroundEnemies[ i ].x >= 0 && aerGroundEnemies[ i ].x < ( AER_STAGE_WIDTH - 1 ) &&
            !aerHitMovableMyFighter( aerGroundEnemies[ i ].x, aerGroundEnemies[ i ].y )
          )
          aerShowGroundEnemy( i );
        else
          aerEndGroundEnemy( i );
    }
}

void aerMoveGroundEnemies()
{
    int i;
    for( i = 0; i < AER_GE_COUNT; i = i + 1 )
    {
        if( aerGroundEnemies[ i ].y == AER_INVALID_Y ) continue;
        if( aerGroundEnemies[ i ].type == AER_GE_TYPE_TRACK )
        {
            if(
                ( aerGroundEnemies[ i ].clock & 3 ) == 0 &&
                aerRnd() < aerCurrentStage + 1 &&
                aerMyFighter.x < aerGroundEnemies[ i ].x
              )
            {
                AerBullet* pBullet;
                int x, y;
                x = aerGroundEnemies[ i ].x + 1;
                y = aerGroundEnemies[ i ].y;
                pBullet = aerStartEnemyBullet( x << AER_BULLET_SHIFT, y << AER_BULLET_SHIFT );
                if( pBullet != NULL )
                {
                    int lx, ly;
                    pBullet->dx = -1;
                    pBullet->dy = -1;
                    lx = aerAbs( x, aerMyFighter.x );
                    ly = aerAbs( y, aerMyFighter.y );
                    if( ( lx >> 1 ) <= ly )
                    {
                        pBullet->numeratorX = AER_SHORT_VELOCITY * 4 / 3;
                        pBullet->numeratorY = AER_LONG_VELOCITY * 4 / 3;
                    }
                    else if( ( ly >> 1 ) <= lx )
                    {
                        pBullet->numeratorX = AER_LONG_VELOCITY * 4 / 3;
                        pBullet->numeratorY = AER_SHORT_VELOCITY * 4 / 3;
                    }
                    else
                    {
                        pBullet->numeratorX = AER_LO_VELOCITY * 4 / 3;
                        pBullet->numeratorY = AER_LO_VELOCITY * 4 / 3;
                    }
                }
            }
        }
        else if( aerGroundEnemies[ i ].type == AER_GE_TYPE_ROCKET )
        {
            if(
                aerGroundEnemies[ i ].x - ( aerGroundEnemies[ i ].y >> 1 ) < ( AER_STAGE_WIDTH - 1 ) / 3 ||
                aerRnd() == 0
              )
              aerGroundEnemies[ i ].type = aerGroundEnemies[ i ].type | 0x80;
        }
        else if( aerGroundEnemies[ i ].type == ( AER_GE_TYPE_ROCKET | 0x80 ) )
        {
            // y>=0 added alongside the existing y<RangeY check (a rocket
            // climbing decrements y toward and past 0 once it reaches the
            // top of the screen).
            aerGroundEnemies[ i ].y = aerGroundEnemies[ i ].y - 1;
            if(
                aerGroundEnemies[ i ].y >= 0 && aerGroundEnemies[ i ].y < ( AER_STAGE_HEIGHT - 1 ) &&
                !aerHitMovableMyFighter( aerGroundEnemies[ i ].x, aerGroundEnemies[ i ].y )
              )
              aerShowGroundEnemy( i );
            else
              aerEndGroundEnemy( i );
        }
        aerGroundEnemies[ i ].clock = aerGroundEnemies[ i ].clock + 1;
    }
}

void aerFireFort( int i )
{
    if( ( aerForts[ i ].clock & AER_FORT_FIRE_MASK ) == 0 && aerForts[ i ].x < ( AER_VVRAM_WIDTH - 2 ) )
    {
        int x, y;
        AerBullet* pBullet;
        x = aerForts[ i ].x + 2;
        y = aerForts[ i ].y + 2;
        pBullet = aerStartEnemyBullet( x << AER_BULLET_SHIFT, y << AER_BULLET_SHIFT );
        if( pBullet != NULL )
        {
            pBullet->dx = -1;
            pBullet->dy = aerSign( y, aerMyFighter.y );
            if( pBullet->dy != 0 )
            {
                int lx, ly;
                lx = aerAbs( x, aerMyFighter.x );
                ly = aerAbs( y, aerMyFighter.y );
                if( lx < ly )
                {
                    pBullet->numeratorX = AER_SHORT_VELOCITY * 4 / 3;
                    pBullet->numeratorY = AER_LONG_VELOCITY * 4 / 3;
                }
                else if( lx > ly )
                {
                    pBullet->numeratorX = AER_LONG_VELOCITY * 4 / 3;
                    pBullet->numeratorY = AER_SHORT_VELOCITY * 4 / 3;
                }
                else
                {
                    pBullet->numeratorX = AER_LO_VELOCITY * 4 / 3;
                    pBullet->numeratorY = AER_LO_VELOCITY * 4 / 3;
                }
            }
            else
            {
                pBullet->numeratorX = AER_HI_VELOCITY * 4 / 3;
                pBullet->numeratorY = AER_HI_VELOCITY * 4 / 3;
            }
        }
    }
}

void aerMoveForts()
{
    int i;
    if( aerFortsMoving )
    {
        for( i = 0; i < AER_FORT_COUNT; i = i + 1 )
        {
            if( aerForts[ i ].y == AER_INVALID_Y ) continue;
            if( aerForts[ i ].x > aerForts[ i ].targetX )
              aerForts[ i ].x = aerForts[ i ].x - 1;
            else
              aerFortsMoving = false;
            aerFireFort( i );
        }
        if( aerFortsMoving ) return;
    }
    else
    {
        for( i = 0; i < AER_FORT_COUNT; i = i + 1 )
        {
            if( aerForts[ i ].y == AER_INVALID_Y ) continue;
            aerFireFort( i );
            aerForts[ i ].clock = aerForts[ i ].clock + 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Group3: aerScrollBackground - needs aerScrollGroundEnemies (above).
// -----------------------------------------------------------------------------

void aerScrollBackground()
{
    int x, h, minY, i;
    if( aerFortCount != 0 ) return;

    aerScrollGroundEnemies();

    minY = AER_BOTTOM;
    for( x = 0; x < AER_STAGE_WIDTH; x = x + 1 )
    {
        h = aerGroundYs[ x + 1 ];
        if( h < minY ) minY = h;
        aerGroundYs[ x ] = h;
    }
    aerMinGroundY = minY;

    for( i = 0; i < AER_BACKGROUND_WIDTH * AER_STAGE_HEIGHT - 1; i = i + 1 )
      aerBackground[ i ] = aerBackground[ i + 1 ];

    aerMapX = aerMapX + 1;
    if( aerMapX == AER_FORT_START_X && aerFortCount == 0 )
      aerStartForts();
    if( aerMapX == AER_MAP_WIDTH * 2 )
    {
        aerCurrentStage = aerCurrentStage + 1;
        aerInitStage();
        aerPrintStage();
    }
    else if( ( aerMapX & 1 ) == 0 )
      aerFillTiles();

    while( aerSkyElementCount > 0 && aerSkyX[ aerSkyElementPtr ] < aerMapX )
    {
        aerSkyElementPtr = aerSkyElementPtr + 1;
        aerSkyElementCount = aerSkyElementCount - 1;
    }
    // Defensive clamp (upstream's own equivalent pointer can, in real
    // reachable play, advance one element past a stage's own valid slice;
    // this port's own single shared flat array makes that a genuine
    // out-of-bounds read for the very last stage unless clamped) - see
    // the sibling project's own header comment for the full derivation.
    if( aerSkyElementPtr >= AER_SKY_TOTAL )
      aerSkyElementPtr = AER_SKY_TOTAL - 1;

    if( ( aerRnd() << 1 ) < aerCurrentStage + 2 )
    {
        int typeIdx, bit, found, tries;
        typeIdx = aerSkyEnemyType;
        bit = aerSkyEnemyBit;
        found = 0;
        tries = 0;
        while( tries < 2 && found == 0 )
        {
            bit = bit << 1;
            typeIdx = typeIdx + 1;
            if( typeIdx >= 2 )
            {
                typeIdx = 0;
                bit = 1;
            }
            if( ( bit & aerSkyBits[ aerSkyElementPtr ] ) != 0 )
              found = 1;
            tries = tries + 1;
        }
        aerSkyEnemyType = typeIdx;
        aerSkyEnemyBit = bit;
        if( typeIdx == 0 || typeIdx == 1 )
          aerStartEnemyFighter( typeIdx );
    }

    if( aerGroundElementCount != 0 )
    {
        x = aerGroundX[ aerGroundElementPtr ] + 1;
        if( x == aerMapX )
        {
            aerStartGroundEnemy( aerGroundYs[ AER_STAGE_WIDTH + 1 ], aerGroundType[ aerGroundElementPtr ] );
            aerGroundElementPtr = aerGroundElementPtr + 1;
            aerGroundElementCount = aerGroundElementCount - 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly,
// matching Cracky's own crkComposeRawByte() derivation - rawPage>=7
// (upstream's own genuinely unused 8th hardware page during real gameplay)
// contributes a flat 0 mapByte with no VVram read at all, rather than an
// out-of-bounds one (aerVVram only has 14 rows, enough for pages 0-6).
//
// OR-combines mapByte with textByte instead of choosing one exclusively,
// matching Cracky's own crkComposeRawByte() shape - needed since the title
// screen draws its real "AERIAL" logo bitmap directly into aerVVram (see
// aerBeginTitle()) rather than a plain-text substitute: the logo occupies
// real hardware pages 1-2 only (VVram rows 2-5), while every title-screen
// status-text element (SCORE/STAGE/MINI/START/CONTINUE/credit) is printed
// on other pages - entirely disjoint page ranges, so OR-combining can
// never actually blend two distinct pieces of content together, it just
// lets the logo (mapByte, non-zero only on its own 2 pages) and the text
// (textByte, non-zero only on its own pages) coexist within one composed
// byte instead of one silently excluding the other.
int aerComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < AER_VVRAM_WIDTH * 4 )
    {
        if(
            aerOverlayActive && rawPage == aerOverlayPage &&
            rawCol >= aerOverlayCol * 4 && rawCol < aerOverlayCol * 4 + aerOverlayLen * 4
          )
        {
            int i, sub;
            i = ( rawCol - aerOverlayCol * 4 ) / 4;
            sub = ( rawCol - aerOverlayCol * 4 ) % 4;
            return aerAsciiPattern[ aerAsciiIndex( aerOverlayText[ i ] ) * 4 + sub ];
        }
        if( rawPage < 7 )
        {
            int mapX, sub, upper, lower, upperByte, lowerByte;
            mapX = rawCol / 4;
            sub = rawCol % 4;
            upper = aerVVram[ rawPage * 2 ][ mapX ];
            lower = aerVVram[ rawPage * 2 + 1 ][ mapX ];
            if( sub == 0 )
            {
                upperByte = aerCharPattern[ upper * 2 + 0 ];
                lowerByte = aerCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else if( sub == 1 )
            {
                upperByte = aerCharPattern[ upper * 2 + 0 ];
                lowerByte = aerCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
            else if( sub == 2 )
            {
                upperByte = aerCharPattern[ upper * 2 + 1 ];
                lowerByte = aerCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else
            {
                upperByte = aerCharPattern[ upper * 2 + 1 ];
                lowerByte = aerCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
        }
    }

    if( !aerFullWidthText && rawCol < AER_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // aerStatusChar's own full-width indexing directly - no more "subtract
    // the map width" local-offset math needed, since rawCol/4 already lands
    // on the correct real column either way (whether this is the
    // aerFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = aerStatusChar[ rawPage ][ charCol ];
            textByte = aerAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void aerRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            value = aerComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// Upstream's own real 6-letter, 4x4-VVram-cell-per-letter "AERIAL" pixel-
// art wordmark, drawn via Status.cpp's `Title()`. aerTitleBytes[] is drawn
// directly into aerVVram at VVram rows 2-5 (matching upstream's own
// `VVram + VVramWidth*2` starting offset exactly, with
// TitleLeft=(VVramWidth-4*6)/2=0 for this 6-letter word), and
// aerComposeRawByte() OR-combines this VVram content with aerStatusChar's
// own text layer rather than choosing one exclusively (see that function's
// own comment).
//
// Everything else below is at upstream's own real, literal columns
// (Status.cpp's Title(): MINI at col TitleLeft+4*TitleLength-5=19, START/
// CONTINUE at col ArrowX+1=9 with the cursor at col ArrowX=8, the credit
// line at col 12) - all genuinely clear of the status labels' own columns
// 24-31 once modeled against the real, wide 32-char-cell-per-page Vram
// canvas - see aerStatusChar's own header comment for the full derivation.
void aerBeginTitle()
{
    int i, j;
    for( i = 0; i < AER_VVRAM_HEIGHT; i = i + 1 )
    {
        for( j = 0; j < AER_VVRAM_WIDTH; j = j + 1 )
          aerVVram[ i ][ j ] = AER_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          aerStatusChar[ i ][ j ] = 0;
    }
    aerOverlayActive = false;
    aerFullWidthText = true;
    aerHideAllSprites();

    // Matches upstream's own real behaviour: PrintStatus() here shows
    // whatever Score/RemainCount/CurrentStage the JUST-ENDED game left
    // behind (Score/CurrentStage/RemainCount aren't reset until AFTER a
    // Start/Continue selection is confirmed, below) - a deliberate,
    // faithful quirk, not a bug.
    aerPrintStatus();

    // Upstream's own real 6-glyph "AERIAL" logo bitmap, drawn directly into
    // aerVVram from aerTitleBytes[] at its own real position (VVram rows
    // 2-5, i.e. real hardware pages 1-2).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                aerVVram[ 2 + row ][ ch * 4 + col ] = aerTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns - see
    // this function's own header comment for the derivation. CONTINUE is
    // shown in full (8 letters), and the credit line keeps its real
    // "2026" year.
    {
        char sMini[4] = { 'M', 'I', 'N', 'I' };
        aerPrintS( 3, 19, sMini, 4 );
    }
    {
        char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        aerPrintS( 5, 9, sStart, 5 );
    }
    {
        char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        aerPrintS( 6, 9, sContinue, 8 );
    }
    {
        char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        aerPrintS( 7, 12, sCredit, 12 );
    }

    aerSelection = 0;
    aerSelectionChanged = true;
    aerPrevLeft = 0; aerPrevRight = 0; aerPrevUp = 0; aerPrevDown = 0; aerPrevFire = 0;
    aerState = AER_STATE_TITLE;
}

void aerUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !aerPrevLeft ) || ( right && !aerPrevRight ) ||
                ( up && !aerPrevUp ) || ( down && !aerPrevDown ) );
    justFire = ( fire && !aerPrevFire );
    aerPrevLeft = left; aerPrevRight = right; aerPrevUp = up; aerPrevDown = down; aerPrevFire = fire;

    if( aerSelectionChanged )
    {
        aerSelectionChanged = false;
        // Cursor arrow column matches upstream's own real ArrowX=8 (one
        // column left of START/CONTINUE's own col 9) - see aerBeginTitle()'s
        // own header comment for the full column derivation.
        if( aerSelection == 0 )
          aerPrintC( 5, 8, '>' );
        else
          aerPrintC( 5, 8, ' ' );
        if( aerSelection == 1 )
          aerPrintC( 6, 8, '>' );
        else
          aerPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        aerFullWidthText = false;
        aerPendingContinue = ( aerSelection == 1 );
        aerScore = 0;
        if( !aerPendingContinue )
          aerCurrentStage = 0;
        aerRemainCount = 3;

        aerInitGameState();
        aerPrintStatus();
        aerDrawAll();

        aerStartSeq( 1, AER_MELODY_START );
        aerState = AER_STATE_START_JINGLE;
        aerRender();
        return;
    }
    if( justDir )
    {
        aerSelection = aerSelection ^ 1;
        aerSelectionChanged = true;
    }
    aerRender();
}

void aerUpdateStartJingle()
{
    if( !aerSeqPlaying( 1 ) )
    {
        aerStartBgm();
        aerClock = 0;
        aerTickCounter = 0;
        aerState = AER_STATE_PLAYING;
    }
    aerRender();
}

void aerUpdatePlaying()
{
    aerTickCounter = aerTickCounter + 1;
    if( aerTickCounter < AER_TICK_DIVISOR )
    {
        aerRender();
        return;
    }
    aerTickCounter = 0;

    aerMoveMyBullets();
    if( ( aerClock & 1 ) == 0 )
    {
        aerMoveMyFighter();
        aerMoveEnemyFighters();
        aerMoveEnemyBullets();
    }
    if( ( aerClock & 3 ) == 0 )
      aerMoveGroundEnemies();
    if( ( aerClock & 7 ) == 0 )
    {
        aerUpdateBangs();
        aerMoveItem();
    }
    if( ( aerClock & 15 ) == 0 )
    {
        aerMoveForts();
        aerScrollBackground();
    }

    aerDrawAll();

    if( aerRemainCount == 0 )
    {
        aerStopBgm();
        aerPrintGameOverOverlay();
        aerStartSeq( 1, AER_MELODY_GAMEOVER );
        aerState = AER_STATE_GAMEOVER_JINGLE;
        aerRender();
        return;
    }

    aerClock = aerClock + 1;
    aerRender();
}

void aerUpdateGameOverJingle()
{
    if( !aerSeqPlaying( 1 ) )
      aerBeginTitle();
    else
      aerRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameAerial_init()
{
    int i;

    aerScore = 0;
    aerRemainCount = 3;
    aerCurrentStage = 0;
    aerRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        aerSeqActive[ i ] = 0;
        aerSeqMelody[ i ] = AER_MELODY_NONE;
    }
    aerOverlayActive = false;
    aerTickCounter = 0;

    aerBeginTitle();
}

void gameAerial_update()
{
    aerAdvanceSound();

    if( aerState == AER_STATE_TITLE )
      aerUpdateTitle();
    else if( aerState == AER_STATE_START_JINGLE )
      aerUpdateStartJingle();
    else if( aerState == AER_STATE_PLAYING )
      aerUpdatePlaying();
    else if( aerState == AER_STATE_GAMEOVER_JINGLE )
      aerUpdateGameOverJingle();
}
