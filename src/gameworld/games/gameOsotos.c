#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// OSOTOS mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_osotos`) - a Boulder-Dash/
// Sokoban-flavored puzzle-chase: walk a 12-column x 8-row stage collecting
// every star ("item"); press Fire while facing a block to push it - a pushed
// block slides until it hits a wall/other block/edge (destroying it) or
// falls off a ledge onto a monster below (killing the monster and scoring a
// combo-multiplied bonus). A block pushed off the map or crushed against a
// wall "dies" but silently revives at its original position a few ticks
// later ("消えたブロックは元の場所に復活します" - readme.md). Collect every
// star to clear a stage; 8 hand-authored stages, 3 lives, real persistent
// hi-score tracked in-session only (upstream itself has NO EEPROM/hi-score
// tracking at all - its own `HiScore` global and every reference to it are
// commented out in `Main.cpp`; this port simply carries no hi-score either,
// matching upstream's own real shipped behavior rather than adding one).
// Same CH32V003 RISC-V + SSD1306, same real 60Hz SysTick frame limiter
// (`Timer.cpp`'s `kTimerHz=60`, `WaitTimer(t)`) as the sibling `gameCracky.c`
// port this file was built from - see that file's own header for the
// methodology this port follows throughout. Only 4 directions + 1 action
// button (`ScanKeys.h`'s `Keys_Button0`), a strict subset of what
// `tinyJoypadShim.h` already exposes - `isFire2Pressed()` goes unused here.
//
// **No hardware display-orientation transform is applied, on purpose** -
// `crkComposeRawByte(col,page)`'s sibling in this file, `osoComposeRawByte`,
// draws directly at its own `(col,page)` with no mirror/flip/bit-reversal.
// This exact "does this game's `InitOled()` need a software orientation fix"
// question was already investigated at length for `gameCracky.c` (see its
// own header) and settled definitively via a real user-supplied reference
// photo: UIAPduino's own `SegRemap`/`ComScanDec` register writes exist to
// correct a *physical panel-mounting* quirk on that specific breakout
// module, with nothing to correct for in a from-scratch software
// recreation. `Oled.cpp` here uses the identical two commands for the
// identical reason - not re-investigated from scratch, just carried over
// directly from that already-settled finding.
//
// **Rendering is the same two-level VVram/Vram tile system as Cracky, ported
// the same way (a direct structural mirror, not re-derived into a closed
// form)** - `VVram` is a 24-wide x 18-tall logical glyph-index grid
// (`osoVVram`; taller than Cracky's 16 rows - see below for why), packed
// into real SSD1306 bytes via the identical `SendUL()` nibble-interleave
// (`osoComposeRawByte`, copied structurally from `crkComposeRawByte` since
// `CharPattern`'s own 2-bytes-per-glyph layout and the nibble-mixing formula
// are byte-for-byte the same "Cate engine" convention in both games).
// Upstream's own dirty-tracking `Backup[]` buffer (skip resending unchanged
// real I2C bytes) is dropped, matching Cracky's own precedent and this
// project's standing "always redraw the full frame" rule.
//
// **A genuine, load-bearing 1-row vertical crop, found by tracing
// `VVramToVram()`'s own pointer arithmetic rather than assumed**: its scan
// starts at `VVram + VVramWidth` (row 1, not row 0) and each of the 8 real
// hardware pages reads VVram rows `(2*page+1)` and `(2*page+2)` - so VVram's
// own row 0 (the very top of `DrawBackground()`'s own grid, floor 0's own
// "is there a ceiling above" indicator row - always blank in practice, since
// floor 0 is the topmost floor and nothing is ever drawn above it) is never
// actually sent to the display, and one extra row (16, a synthetic all-floor
// row `DrawBackground()` writes right after its 4-floor x 4-row grid, "the
// ground beneath the lowest floor") becomes visible as page 7's own lower
// half instead. `VVramHeight` is declared `16+2` (18) specifically to give
// `DrawSprites()`'s own per-row bound check (`y < VVramHeight`) one row of
// headroom past the last genuinely-visible row (16) without ever indexing
// out of the flat array. Reproduced exactly: `osoComposeRawByte()` reads
// `osoVVram[page*2+1]`/`[page*2+2]` (not `[page*2]`/`[page*2+1]` the way
// Cracky's own, un-cropped VVram does), and `osoDrawSpriteInto2x2()` bound-
// checks against the same `OSO_VVRAM_HEIGHT` (18)/`OSO_VVRAM_WIDTH` (24)
// upstream's own `DrawSprites()` does.
//
// **The title screen's own text layout was rewritten after the exact same
// architectural bug found (via a real user-supplied hardware photo) in the
// sibling `gameCracky.c` was traced into this file too - see that file's
// own header comment and CLAUDE.md's "A real user-supplied hardware photo
// overturns Cracky's own title-screen design" section for the full
// discovery story.** The root cause here is identical: upstream's real
// `PrintC()`/`PrintS()` write to a genuine 32-character-cell-wide Vram row
// (128 real pixels / `VramStep`=4), not an 8-cell one - `Status.cpp`'s own
// `LeftX=24` constant confines SCORE/STAGE/TIME/lives to columns 24-31, but
// every piece of `Title()`'s own text ("MINI" at column 17, "START"/
// "CONTINUE" at column 9 with the cursor at column 8, "INUFUTO 2026" at
// column 12) lives at columns well inside what's the map area during real
// gameplay - the *same* shared `PrintC()`/`PrintS()` mechanism, just at
// different column arguments, not a separate narrow zone. Upstream's own
// "MINI" is additionally drawn relative to the real 80-value logo bitmap's
// own computed position - `TitleLeft + 4*TitleLength - 5` (`TitleLeft=
// OSO_TITLE_LEFT=2`, `TitleLength=OSO_TITLE_LENGTH=5`) - which resolves to
// column 17.
//
// This port's own `osoStatusChar` widened to `int[8][32]` (matching
// upstream's own real 32-column Vram row) - every `osoPrintStatus()`/
// `osoPrintScore()`/`osoPrintTime()` column argument uses upstream's own
// real `LeftX=24`-based columns, a `osoFullWidthText` flag (true only in
// `OSO_STATE_TITLE`) lets `osoComposeRawByte()` read the full 0-31 column
// range instead of just columns 24-31 while the title screen is showing,
// and `osoBeginTitle()` places every element at upstream's own literal
// columns - "MINI" (page 3, col 17), "START"/"CONTINUE" (page 5/6, col 9,
// cursor at col 8 on both, "CONTINUE" spelled out in full, 8 letters), and
// the "INUFUTO 2026" credit line (page 7, col 12) - none of it collides
// with SCORE/STAGE/TIME/lives' own columns 24-31.
//
// The real upstream title wordmark bitmap (`osoTitleBytes[80]`, byte-diff-
// extracted from `Status.cpp`'s own real `TitleBytes[]`, 80 values =
// `OSO_TITLE_LENGTH`(5) letters x 16) is drawn directly into `osoVVram` at
// VVram row 2, starting column `OSO_TITLE_LEFT` (matching upstream's own
// `VVram + VVramWidth*2 + TitleLeft` starting offset exactly), the same
// structural-mirror loop shape as Cracky's own logo. `osoCharPattern[]`'s
// own first 32 bytes (the "logo" glyph range every value in `osoTitleBytes`
// indexes into) are the same shared block-pattern palette every other map
// tile in this game already draws through. `osoComposeRawByte()` OR-
// combines the VVram-derived `mapByte` with the status-text-derived
// `textByte` instead of choosing one exclusively, since the logo (real
// hardware pages 0-2) and the status text (pages 0/3/5/6/7) occupy disjoint
// page ranges by construction - see that function's own comment for the
// full reasoning, including why this is a no-op change for every real-
// gameplay column. No separate cleanup was needed at the title->gameplay
// transition: `osoInitTrying()`/`osoBeginTrying()` already rebuild the
// whole `osoVVram` grid fresh via `osoDrawAll()`/`osoMapToVVram()` every
// real gameplay frame (the same "always redraw the full frame" model this
// whole port already follows), so the logo's own VVram rows are naturally
// overwritten the instant real gameplay begins, with no leftover logo
// pixels bleeding into the map area.
//
// **The blocking upstream control flow rewritten as an explicit frame-
// stepped state machine**, the same treatment as every port in this
// project: OSO_STATE_TITLE, OSO_STATE_START_JINGLE (the blocking
// `Sound_Start()` held before play begins, then `StartBGM()`),
// OSO_STATE_PLAYING, OSO_STATE_LOSE_ANIM (`LooseMan()`'s own 8-step blink-
// and-beep loop), OSO_STATE_GAMEOVER_JINGLE, OSO_STATE_CLEAR_WAIT_A (the
// real `WaitTimer(30)` held before `StopBGM()`), OSO_STATE_CLEAR_WAIT_B (a
// *second*, separate `WaitTimer(10)` held after `StopBGM()` but before the
// clear jingle starts - upstream genuinely has two distinct waits with a
// real state change in between, not one combined wait, so this port keeps
// them as two distinct states rather than merging them), OSO_STATE_
// CLEAR_JINGLE, and OSO_STATE_BONUS_TALLY (the real `while(StageTime>=
// BonusRate){...Sound_Beep();}` bonus-countdown loop, converted to one
// decrement+beep per real tick, matching Cracky's own identical pattern).
//
// **Main()'s own loop structure is genuinely more involved than Cracky's**,
// worth spelling out since it drove a real, deliberate simplification:
// every real "tick" (each `WaitTimer(8)` call, gated behind `Clock&3==0`)
// actually corresponds to *4 raw loop iterations*, only one of which calls
// `WaitTimer()` at all - the other three run back-to-back with zero real
// time elapsing. Within that group, `MoveMan()`/`MoveMonsters()`/
// `UpdateBlocks()`/`UpdatePoints()` run exactly once (gated by `Clock&3==0`),
// but `MoveBlocks()` runs *twice* (gated by the looser `Clock&1==0`, so it
// fires once at the group's own start - immediately before that group's real
// `DrawAll()`+`WaitTimer(8)` - and once more, silently, during the "free"
// iterations right after the wait returns but before the *next* group's own
// processing begins). Tracing the actual coordinate math (`Movable.x/y` are
// genuine sub-cell pixel positions here, unlike Cracky's - `CellCoordMask`
// is 1, not 0 - so a block takes exactly 2 `MoveBlocks()` calls to cross one
// full stage cell) confirms this is deliberate: pushed/falling blocks slide
// at a full cell per real tick, while Man himself (`MoveMan()`, called only
// once per tick) advances only *half* a cell per tick - blocks visibly
// outrun the player once pushed, a real gameplay detail worth preserving
// exactly, not an artifact to "fix". This port reproduces the *effective*
// per-tick outcome (call `osoMoveBlocksOnce()` twice, back-to-back, then
// draw/render once) rather than replicating the precise interleaving with
// the render call sitting in between the two calls - a deliberate,
// documented simplification: the only observable difference is that every
// rendered frame here is a constant one extra half-step "ahead" of where
// upstream's own frame-boundary would sit (upstream's frame N shows
// `2N+1` cumulative block-move-calls applied; this port's frame N shows
// `2N+2`) - an unnoticeable, permanent one-half-cell-early bias in block-
// slide animation, not a growing drift, and not worth the extra state-
// machine complexity a literal reproduction (call the "hidden" second
// `MoveBlocks()` at the *start* of the *next* tick instead of the end of
// this one) would need.
//
// **Sound**: the exact same real 3-tone-channel software mixer/tracker as
// Cracky (`Sound.cpp`, byte-for-byte the same `Tempo=160`/`NoteLength`/
// `Scale` enums, the same `[duration,note]` melody byte-pair format, several
// melodies literally identical between the two games - `Sound_Start`/
// `Sound_Clear`/`Sound_GameOver`/`StartBGM`'s own `notes1`/`notes2` all
// match Cracky's own `CRK_MELODY_START`/`CLEAR`/`GAMEOVER`/`BGM1`/`BGM2`
// tables byte-for-byte, evidently shared boilerplate/composition across
// inufuto's own "Cate engine" games) - so every call here goes straight to
// `md_playTone()` the same way, reusing Cracky's exact `crkNoteFrames()`
// tempo-derivation formula (`length * 1.875`, unchanged since `Tempo=160`
// is identical) and its exact 3-slot frame-stepped sequencer shape
// (0=one-shot SFX, 1=jingle/BGM-voice-A, 2=BGM-voice-B). One real
// per-call-site difference from Cracky, checked directly against upstream
// rather than assumed identical: this game's own channel-0 cues split
// between *blocking* (`WaitMelody`: `Sound_Loose`, `Sound_Beep`) and
// *non-blocking* (`StartMelody`: `Sound_Hit`, and a genuinely new one this
// game has that Cracky doesn't, `Sound_Push`) - each call site here matches
// upstream's own choice exactly (an explicit wait-state for the two
// blocking cues, a bare fire-and-forget `osoStartSeq()` call with no wait
// for the two non-blocking ones), the same site-by-site fidelity Cracky's
// own header already documents for its own channel-0 cues.
//
// **A genuine out-of-bounds-read risk, found by inspection before ever
// compiling, not by a report**: `Point.cpp`'s `StartPoint(x,y,rate)` indexes
// a 4-entry `Values[]={10,20,40,80}` table with `rate`, and `rate` is
// literally `MovingBlock->status & Block_RateMask` - a per-block "how many
// monsters has this exact block already killed on its current slide" combo
// counter, incremented (`++pMovingBlock->status;`) every time it hits
// another monster before ever stopping. Nothing upstream caps this at 3
// (the table's own last valid index) - a single block killing a 5th monster
// mid-slide would read `Values[4]`, off the end of the real table. Harmless
// on real AVR/RISC-V flash (an adjacent-constant-data read), a genuine
// out-of-bounds array read here - the same bug shape this project has found
// and fixed many times elsewhere (e.g. Point.cpp's own upstream sibling
// games). **Fixed** with an explicit `if (rate > 3) rate = 3;` clamp right
// before the table lookup, in `osoStartPoint()`.
//
// **`HitToMan()` (`Man.cpp`) is genuinely dead code** - defined and
// declared, but never called from anywhere else in the whole source tree
// (confirmed via a full-tree grep before dropping it, the same "confirmed
// dead by grep" standard this project applies everywhere) - not ported.
// `Status.cpp`'s own `PrintPerfect()` is dead the same way (declared,
// defined, zero call sites) - also dropped.
//
// Movable coordinates (`x`,`y`) are genuine sub-cell pixel positions here
// (`CellCoordShift=1`, `CellCoordMask=1` - unlike Cracky, where the
// equivalent mask was 0 and coordinates were already whole VVram-grid
// cells) - every `CoordShift`/`CoordRate`/`CoordMask`/`CellShift`/
// `CellCoordShift`/`CellCoordMask`/`MaxTimeDenom` define is kept as the
// real upstream expression rather than silently resolved to its current
// literal value, matching Cracky's own established "#defines stay exactly
// as upstream wrote them" preference - future-proofing against a value
// this file doesn't currently need to exercise, the same reasoning Cracky's
// own header already gives for doing this.
//
// This port carries forward the sibling `tinyjoypad_vircon32` source's own
// current, already-fixed shape verbatim, including its own two whole-block
// relocations (osoAddScoreForward()'s Status/Print/Sound/Score dependency
// chain, and osoDrawAll()'s own render chain, both moved earlier in the
// file to be defined before their first real use, since standard C - like
// the Vircon32 dialect this was ported from - still requires
// definition-before-use with no forward declarations here) and its own
// five independently-verified bug fixes (a title-screen "MINI"/"START"
// collision now resolved by the widened status-text grid, an octave-too-
// high "lose a life" cue, a full-octave-too-low BGM2 bassline, a real
// block-vs-block collision-projection bug in `osoMoveBlocksOnce()` now
// passing the block's own projected dx/dy instead of literal zeros, and a
// guarded out-of-bounds array read in `osoInitTrying()`'s own last-floor
// "lower" byte lookup) - none of that history is re-derived here, just
// mechanically dialect-converted from the sibling project's own real
// source shape, per this port's own CLAUDE.md-documented methodology (see
// `gameCracky.c`'s own header for the shared engine background).
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into osoCharPattern (map tiles) / osoAsciiPattern
//   (status text)
// -----------------------------------------------------------------------------

#define OSO_CHAR_SPACE 0x00
#define OSO_CHAR_LOGO 0x00
#define OSO_CHAR_FLOOR 0x10
#define OSO_CHAR_LADDER 0x11
#define OSO_CHAR_LADDER_LEFT 0x11
#define OSO_CHAR_LADDER_RIGHT 0x12
#define OSO_CHAR_MAN 0x13
#define OSO_CHAR_MAN_LEFT 0x13
#define OSO_CHAR_MAN_LEFT0 0x17
#define OSO_CHAR_MAN_LEFT1 0x1B
#define OSO_CHAR_MAN_LEFT2 0x1F
#define OSO_CHAR_MAN_LEFT_PUSH 0x23
#define OSO_CHAR_MAN_RIGHT 0x27
#define OSO_CHAR_MAN_RIGHT0 0x2B
#define OSO_CHAR_MAN_RIGHT1 0x2F
#define OSO_CHAR_MAN_RIGHT2 0x33
#define OSO_CHAR_MAN_RIGHT_PUSH 0x37
#define OSO_CHAR_MAN_UPDOWN 0x3B
#define OSO_CHAR_MAN_UPDOWN0 0x3B
#define OSO_CHAR_MAN_UPDOWN1 0x3F
#define OSO_CHAR_MAN_DOWN_PUSH 0x43
#define OSO_CHAR_MAN_LOOSE0 0x47
#define OSO_CHAR_MAN_LOOSE1 0x4B
#define OSO_CHAR_MAN_LOOSE2 0x4F
#define OSO_CHAR_MONSTER 0x53
#define OSO_CHAR_MONSTERREV 0x73
#define OSO_CHAR_BLOCK 0x83
#define OSO_CHAR_POINT 0xA7
#define OSO_CHAR_ITEM 0xB7
#define OSO_CHAR_END 0xBB

// Man's own normal/pushing sprite-frame base patterns (indices into the
// (pattern<<2)+OSO_CHAR_MAN scheme), kept as real expressions matching
// upstream's own `(Char_X - Char_Man) / 4` macros exactly.
#define OSO_PATTERN_LEFT ( ( OSO_CHAR_MAN_LEFT - OSO_CHAR_MAN ) / 4 )
#define OSO_PATTERN_RIGHT ( ( OSO_CHAR_MAN_RIGHT - OSO_CHAR_MAN ) / 4 )
#define OSO_PATTERN_UPDOWN ( ( OSO_CHAR_MAN_UPDOWN - OSO_CHAR_MAN ) / 4 )
#define OSO_PATTERN_LEFT_PUSH ( ( OSO_CHAR_MAN_LEFT_PUSH - OSO_CHAR_MAN ) / 4 )
#define OSO_PATTERN_RIGHT_PUSH ( ( OSO_CHAR_MAN_RIGHT_PUSH - OSO_CHAR_MAN ) / 4 )
#define OSO_PATTERN_UP_PUSH 0
#define OSO_PATTERN_DOWN_PUSH ( ( OSO_CHAR_MAN_DOWN_PUSH - OSO_CHAR_MAN ) / 4 )

// -----------------------------------------------------------------------------
//   Movable.h / Stage.h
// -----------------------------------------------------------------------------

#define OSO_COORD_SHIFT 0
#define OSO_COORD_RATE ( 1 << OSO_COORD_SHIFT )
#define OSO_COORD_MASK ( OSO_COORD_RATE - 1 )

#define OSO_MOVABLE_LIVE 0x80
#define OSO_MOVABLE_FALL 0x40
#define OSO_MAN_PUSHING 0x20
#define OSO_MONSTER_THROGH 0x20
#define OSO_PATTERN_MASK 0x0f
#define OSO_MONSTER_PATTERN_MASK 0x07

#define OSO_DIR_LEFT 0
#define OSO_DIR_RIGHT 1
#define OSO_DIR_UP 2
#define OSO_DIR_DOWN 3

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
} OsoMovable;

#define OSO_COLUMN_COUNT 12
#define OSO_FLOOR_COUNT 4
#define OSO_ROW_COUNT ( OSO_FLOOR_COUNT * 2 )
#define OSO_CELL_SIZE 2
#define OSO_COLUMN_WIDTH OSO_CELL_SIZE
#define OSO_ROW_HEIGHT OSO_CELL_SIZE
#define OSO_FLOOR_HEIGHT ( OSO_ROW_HEIGHT * 2 )
#define OSO_CELL_SHIFT 1
#define OSO_CELL_MASK ( OSO_COLUMN_WIDTH - 1 )
#define OSO_CELL_COORD_SHIFT ( OSO_CELL_SHIFT + OSO_COORD_SHIFT )
#define OSO_CELL_COORD_MASK ( OSO_CELL_SIZE * OSO_COORD_RATE - 1 )

#define OSO_CELL_DOWN 0x1
#define OSO_CELL_UP 0x2
#define OSO_CELL_CEILING 0x4
#define OSO_CELL_ITEM 0x4
#define OSO_CELL_BLOCK 0x8

#define OSO_COLUMNS_PER_BYTE 4
#define OSO_STAGE_COUNT 8
#define OSO_MAX_BLOCK_COUNT 7
#define OSO_MAX_MONSTER_COUNT 4

#define OSO_HIT_RANGE ( OSO_COORD_RATE * 4 / 3 )

// -----------------------------------------------------------------------------
//   VVram.h / Sprite.h
// -----------------------------------------------------------------------------

#define OSO_VVRAM_WIDTH 24
#define OSO_VVRAM_HEIGHT 18

// Title()'s own real logo placement - upstream's `TitleLength=5`/
// `TitleLeft=(VVramWidth-4*TitleLength)/2` - kept as real expressions
// rather than pre-computed literals, matching this file's own established
// "#defines stay exactly as upstream wrote them" preference.
#define OSO_TITLE_LENGTH 5
#define OSO_TITLE_LEFT ( ( OSO_VVRAM_WIDTH - 4 * OSO_TITLE_LENGTH ) / 2 )

#define OSO_SPRITE_MAN 0
#define OSO_SPRITE_MONSTER 1
#define OSO_SPRITE_BLOCK 5
#define OSO_SPRITE_POINT 9
#define OSO_SPRITE_END 13
#define OSO_MAX_MOVING_BLOCK_COUNT ( OSO_SPRITE_POINT - OSO_SPRITE_BLOCK )
#define OSO_MAX_POINT_COUNT ( OSO_SPRITE_END - OSO_SPRITE_POINT )
#define OSO_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Block.h
// -----------------------------------------------------------------------------

#define OSO_BLOCK_LIVE 0x80
#define OSO_BLOCK_DESTROYING 0x40
#define OSO_BLOCK_RESTARTING 0xc0
#define OSO_BLOCK_SLEEPING 0x20
#define OSO_BLOCK_RATE_MASK 0x0f
#define OSO_INVALID_POSITION 0xff

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching
//   upstream's own enum exactly (same shape as Cracky's own copy).
// -----------------------------------------------------------------------------

#define OSO_N8 6
#define OSO_N8P ( OSO_N8 * 3 / 2 )
#define OSO_N4 ( OSO_N8 * 2 )
#define OSO_N4P ( OSO_N4 * 3 / 2 )
#define OSO_N2 ( OSO_N4 * 2 )
#define OSO_N2P ( OSO_N2 * 3 / 2 )
#define OSO_N1 ( OSO_N2 * 2 )

// F3/G3/A3/B3 (one octave below F4/G4/A4/B4) - StartBGM()'s own real
// notes2[] (osoMelodyBgm2 below) and Sound_Loose() (osoMelodyLoose below)
// both genuinely need octave-3 notes.
#define OSO_F3 14
#define OSO_G3 16
#define OSO_A3 18
#define OSO_B3 20
#define OSO_C4 21
#define OSO_C4S 22
#define OSO_D4 23
#define OSO_E4 25
#define OSO_F4 26
#define OSO_G4 28
#define OSO_A4 30
#define OSO_B4 32
#define OSO_C5 33
#define OSO_D5 35
#define OSO_E5 37
#define OSO_F5 38

#define OSO_TEMPO 160

#define OSO_MELODY_NONE 0
#define OSO_MELODY_LOOSE 1
#define OSO_MELODY_HIT 2
#define OSO_MELODY_BEEP 3
#define OSO_MELODY_PUSH 4
#define OSO_MELODY_START 5
#define OSO_MELODY_CLEAR 6
#define OSO_MELODY_GAMEOVER 7
#define OSO_MELODY_BGM1 8
#define OSO_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph. Confirmed
// byte-identical to Cracky's own copy of this exact same "Cate engine"
// table (same author, shared boilerplate) - extracted independently anyway
// rather than assumed.
int osoAsciiPattern[108] = {
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

// CharPattern - 187 map-tile glyphs (Char_End=0xBB), 2 bytes/glyph.
int osoCharPattern[374] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x33, 0x33, 0xf0, 0xaa, 0xaa, 0x0f,
    0x80, 0xf5, 0x7d, 0x08, 0x10, 0x3c, 0xc3, 0x01,
    0x00, 0xf5, 0xfd, 0x00, 0x90, 0x34, 0x43, 0x05,
    0x00, 0x75, 0x7d, 0x00, 0x00, 0xd0, 0xc1, 0x00,
    0x00, 0xf5, 0x7d, 0x00, 0x10, 0x2d, 0x43, 0x05,
    0x50, 0xdf, 0x07, 0x00, 0x11, 0x3c, 0x43, 0x08,
    0x80, 0xd7, 0x5f, 0x08, 0x10, 0x3c, 0xc3, 0x01,
    0x00, 0xdf, 0x5f, 0x00, 0x50, 0x34, 0x43, 0x09,
    0x00, 0xd7, 0x57, 0x00, 0x00, 0x1c, 0x0d, 0x00,
    0x00, 0xd7, 0x5f, 0x00, 0x50, 0x34, 0xd2, 0x01,
    0x00, 0x70, 0xfd, 0x05, 0x80, 0x34, 0xc3, 0x11,
    0x80, 0xb3, 0xbb, 0x04, 0x00, 0x35, 0xc3, 0x00,
    0x40, 0xbb, 0x3b, 0x08, 0x00, 0x3c, 0x53, 0x00,
    0x00, 0xc4, 0xc4, 0x00, 0xa8, 0x15, 0x51, 0x8a,
    0x4c, 0xac, 0x8a, 0x44, 0x13, 0x53, 0x15, 0x22,
    0x80, 0xc3, 0x3c, 0x08, 0x10, 0xbe, 0xaf, 0x01,
    0x44, 0xa8, 0xca, 0xc8, 0x22, 0x51, 0x35, 0x32,
    0xa8, 0xaf, 0xef, 0x08, 0x10, 0x73, 0xbf, 0x00,
    0x40, 0x4e, 0xce, 0x00, 0x32, 0xf7, 0xff, 0x02,
    0x80, 0xfe, 0xfa, 0x8a, 0x00, 0xfb, 0x37, 0x01,
    0x00, 0xec, 0xe4, 0x04, 0x20, 0xff, 0x7f, 0x23,
    0xe8, 0xef, 0xef, 0x08, 0x30, 0xf7, 0x37, 0x00,
    0xc0, 0xce, 0xce, 0x00, 0x71, 0xff, 0x7f, 0x01,
    0x80, 0xbe, 0xbe, 0x8e, 0x00, 0x73, 0x7f, 0x03,
    0x00, 0x6c, 0x6c, 0x0c, 0x10, 0xf7, 0xff, 0x17,
    0x5e, 0x51, 0x11, 0x0e, 0x21, 0x44, 0xa8, 0x05,
    0xe0, 0x11, 0x15, 0xe5, 0x50, 0x8a, 0x44, 0x12,
    0xe0, 0x11, 0x11, 0xe1, 0x10, 0x42, 0x48, 0x12,
    0x1e, 0x15, 0x15, 0x0e, 0x21, 0x84, 0x24, 0x01,
    0x1e, 0xdd, 0x1d, 0x0e, 0x43, 0x55, 0x45, 0x03,
    0x1c, 0x58, 0x05, 0x0e, 0x42, 0x15, 0x45, 0x02,
    0x00, 0xc8, 0x48, 0x08, 0x43, 0x25, 0x44, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x52, 0x52, 0x25, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x04, 0x04, 0x04,
    0x00, 0xc8, 0x08, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0xcc, 0x0c, 0x00, 0x00, 0x11, 0x01, 0x00,
    0x80, 0x6c, 0x8c, 0x00, 0x00, 0x31, 0x01, 0x00,
    0xc0, 0xee, 0xce, 0x00, 0x10, 0x33, 0x13, 0x00,
    0xe4, 0xc0, 0xc2, 0x00, 0x32, 0x02, 0x61, 0x69,
    0x24, 0xcc, 0xc2, 0x00, 0x32, 0x02, 0x61, 0x69,
    0x8c, 0xce, 0xc2, 0x00, 0x00, 0x03, 0x61, 0x69,
    0xa4, 0xc4, 0xc2, 0x00, 0x21, 0x01, 0x61, 0x69,
    0xc4, 0xfc, 0xcc, 0x04, 0x40, 0x13, 0x43, 0x00,
};

// TitleBytes - upstream's own real 5-letter title-screen logo bitmap
// (Status.cpp's `Title()`), 4x4 VVram-cell glyph indices per letter (80
// values total: TitleLength=5 * 16), byte-diff-verified via script against
// the real upstream source. Every value is a valid index into
// osoCharPattern[]'s own "logo" range (indices 0-15, the first 32 bytes of
// that table - confirmed byte-identical to Cracky's own copy of the same
// shared "Cate engine" table via the same script) - the same shared
// block-pattern palette every other map tile in this game already draws
// through, reused here to build the title wordmark.
int osoTitleBytes[80] = {
    0x00, 0x08, 0x07, 0x0d, 0x00, 0x0c, 0x03, 0x0c,
    0x00, 0x0c, 0x03, 0x0c, 0x00, 0x00, 0x05, 0x05,
    0x02, 0x0e, 0x05, 0x01, 0x03, 0x04, 0x0d, 0x03,
    0x03, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x0d, 0x0e, 0x05, 0x0b, 0x0c,
    0x0f, 0x00, 0x0f, 0x04, 0x04, 0x05, 0x01, 0x00,
    0x07, 0x01, 0x00, 0x00, 0x03, 0x0e, 0x0d, 0x02,
    0x01, 0x0f, 0x0c, 0x03, 0x00, 0x04, 0x05, 0x00,
    0x0e, 0x05, 0x01, 0x00, 0x04, 0x0d, 0x03, 0x00,
    0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Rnd() pseudo-random table - byte-identical to Cracky's own copy.
int osoRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

// A real transcription bug, found via a byte-diff against the real
// upstream `Sound_Loose()` (`notes[]={1,A3,0}` - A3=18, not A4=30): an
// earlier draft used OSO_A4 here, most likely because OSO_A3 simply
// didn't exist yet as a #define at the time this was written (every
// other melody in this file only ever needed octave-4-and-up notes) -
// played the "lose a life" cue a full octave too high. Fixed to OSO_A3.
int osoMelodyLoose[3] = { 1, OSO_A3, 0 };

int osoMelodyHit[17] = {
    1, OSO_F4, 1, OSO_G4, 1, OSO_A4, 1, OSO_B4, 1, OSO_C5,
    1, OSO_D5, 1, OSO_E5, 1, OSO_F5, 0,
};

int osoMelodyBeep[3] = { 1, OSO_A4, 0 };

int osoMelodyPush[13] = {
    1, OSO_C4, 1, OSO_C4S, 1, OSO_D4, 1, OSO_F4, 1, OSO_A4, 1, OSO_C5, 0,
};

int osoMelodyStart[27] = {
    OSO_N8, 0, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5,
    OSO_N4, OSO_G4, OSO_N4, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_D5, OSO_N8, OSO_C5,
    OSO_N4, OSO_D5, OSO_N4, OSO_E5, OSO_N1, OSO_C5, 0,
};

int osoMelodyClear[22] = {
    OSO_N8, OSO_C4, OSO_N8, OSO_E4, OSO_N8, OSO_G4, OSO_N8, OSO_D4, OSO_N8, OSO_F4,
    OSO_N8, OSO_A4, OSO_N8, OSO_E4, OSO_N8, OSO_G4, OSO_N8, OSO_B4, OSO_N4P, OSO_C5,
    0, 0,
};

int osoMelodyGameOver[21] = {
    OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_G4, OSO_N8, OSO_G4, OSO_N8, OSO_A4,
    OSO_N8, OSO_A4, OSO_N8, OSO_B4, OSO_N8, OSO_B4, OSO_N2P, OSO_C5, OSO_N4, 0,
    0,
};

int osoMelodyBgm1[105] = {
    OSO_N8, 0, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5,
    OSO_N4, OSO_G4, OSO_N4, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_D5, OSO_N8, OSO_C5,
    OSO_N4, OSO_D5, OSO_N4, OSO_E5, OSO_N8, 0, OSO_N8, OSO_C5, OSO_N8, OSO_C5,
    OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N4, OSO_D5, OSO_N4, OSO_F5, OSO_N8, OSO_F5,
    OSO_N8, OSO_E5, OSO_N8, OSO_C5, OSO_N4, OSO_C5, OSO_N4, OSO_D5, OSO_N8, 0,
    OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_C5, OSO_N4, OSO_G4,
    OSO_N4, OSO_C5, OSO_N8, OSO_C5, OSO_N8, OSO_D5, OSO_N8, OSO_C5, OSO_N4, OSO_D5,
    OSO_N4, OSO_E5, OSO_N4, OSO_F5, OSO_N4, OSO_F5, OSO_N4, OSO_E5, OSO_N4, OSO_E5,
    OSO_N4, OSO_D5, OSO_N8, OSO_D5, OSO_N4, OSO_E5, OSO_N8, OSO_E5, OSO_N8, OSO_D5,
    OSO_N8, OSO_C5, OSO_N4, OSO_C5, OSO_N4, OSO_C5, OSO_N8, OSO_D5, OSO_N4, OSO_D5,
    OSO_N4P, OSO_C5, OSO_N2P, 0, 255,
};

// A real, systematic transcription bug, found via a byte-diff against the
// real upstream `StartBGM()`'s own `notes2[]` (Sound.cpp): an earlier
// draft of this table was mechanically copied from Cracky's own
// crkMelodyBgm2 (this file's header comment used to claim BGM2 "matches
// Cracky's own... BGM2 tables byte-for-byte", which turned out to be
// true only for BGM1 - BGM2 is a genuinely different, one-octave-higher
// bassline in OSOTOS's own real source: C4/E4/G4/A3/D4/F3/G3/B3, not
// Cracky's own C3/E3/G3/A2/D3/F2/G2/B2). Every raw numeric pitch literal
// below (9,13,16,6,11,14,18,4,8,2 - Cracky's own octave-3 scale values)
// was exactly 12 semitones (one octave) below the correct OSOTOS value -
// the whole BGM2 voice played a full octave too low for the entire game.
// Fixed by rewriting the table from OSOTOS's own real notes2[] directly,
// using named mnemonics (matching this file's own BGM1 table's style)
// instead of raw numbers, so a future octave slip like this is far
// easier to catch by eye.
int osoMelodyBgm2[105] = {
    OSO_N8, OSO_C4, OSO_N4, 0, OSO_N4P, OSO_E4, OSO_N8, OSO_G4, OSO_N8, 0,
    OSO_N8, OSO_A3, OSO_N4, 0, OSO_N4P, OSO_C4, OSO_N8, OSO_E4, OSO_N8, 0,
    OSO_N8, OSO_D4, OSO_N4, 0, OSO_N4P, OSO_F3, OSO_N8, OSO_A3, OSO_N8, 0,
    OSO_N8, OSO_G3, OSO_N4, 0, OSO_N4P, OSO_B3, OSO_N8, OSO_D4, OSO_N8, 0,
    OSO_N8, OSO_C4, OSO_N4, 0, OSO_N4P, OSO_E4, OSO_N8, OSO_G4, OSO_N8, 0,
    OSO_N8, OSO_A3, OSO_N4, 0, OSO_N4P, OSO_C4, OSO_N8, OSO_E4, OSO_N8, 0,
    OSO_N8, OSO_F3, OSO_N4, 0, OSO_N4P, OSO_A3, OSO_N8, OSO_C4, OSO_N8, 0,
    OSO_N8, OSO_D4, OSO_N4, 0, OSO_N4P, OSO_E4, OSO_N8, OSO_A3, OSO_N8, 0,
    OSO_N8, OSO_F3, OSO_N4, 0, OSO_N8, OSO_F3, OSO_N8, OSO_G3, OSO_N8, 0, OSO_N8, OSO_B3, OSO_N8, OSO_D4,
    OSO_N8, OSO_C4, OSO_N4, 0, OSO_N4P, OSO_E4,
    OSO_N8, OSO_G4, OSO_N8, 0, 255,
};

// Stage data - flattened from upstream's own `Stage{start,blockCount,
// pBlocks,enemyCount,pEnemies,bytes[12]}` struct-with-pointer-members array
// into parallel fixed tables, matching Cracky's own "flatten to plain
// arrays" precedent for the identical upstream shape.
int osoStageStart[8] = {
    ( 1 << 4 ) | 3, ( 4 << 4 ) | 1, ( 6 << 4 ) | 1, ( 8 << 4 ) | 0,
    ( 11 << 4 ) | 3, ( 11 << 4 ) | 0, ( 5 << 4 ) | 0, ( 11 << 4 ) | 0,
};
int osoStageBlockCount[8] = { 4, 5, 7, 5, 4, 3, 4, 6 };
int osoStageEnemyCount[8] = { 2, 2, 2, 2, 2, 2, 4, 2 };

int osoStageBlocks[8][7] = {
    { ( 4 << 4 ) | 0, ( 1 << 4 ) | 2, ( 2 << 4 ) | 3, ( 9 << 4 ) | 3, 0, 0, 0 },
    { ( 4 << 4 ) | 0, ( 8 << 4 ) | 0, ( 6 << 4 ) | 1, ( 1 << 4 ) | 2, ( 9 << 4 ) | 2, 0, 0 },
    { ( 4 << 4 ) | 0, ( 7 << 4 ) | 0, ( 10 << 4 ) | 0, ( 4 << 4 ) | 1, ( 7 << 4 ) | 1, ( 10 << 4 ) | 1, ( 3 << 4 ) | 3 },
    { ( 5 << 4 ) | 0, ( 4 << 4 ) | 1, ( 4 << 4 ) | 2, ( 7 << 4 ) | 2, ( 3 << 4 ) | 3, 0, 0 },
    { ( 8 << 4 ) | 0, ( 8 << 4 ) | 1, ( 3 << 4 ) | 2, ( 6 << 4 ) | 2, 0, 0, 0 },
    { ( 3 << 4 ) | 0, ( 9 << 4 ) | 1, ( 8 << 4 ) | 2, 0, 0, 0, 0 },
    { ( 4 << 4 ) | 0, ( 9 << 4 ) | 0, ( 5 << 4 ) | 1, ( 7 << 4 ) | 1, 0, 0, 0 },
    { ( 4 << 4 ) | 0, ( 7 << 4 ) | 0, ( 5 << 4 ) | 1, ( 8 << 4 ) | 1, ( 1 << 4 ) | 2, ( 6 << 4 ) | 3, 0 },
};

int osoStageEnemies[8][4] = {
    { ( 0 << 4 ) | 0, ( 11 << 4 ) | 2, 0, 0 },
    { ( 8 << 4 ) | 1, ( 0 << 4 ) | 3, 0, 0 },
    { ( 4 << 4 ) | 2, ( 0 << 4 ) | 3, 0, 0 },
    { ( 1 << 4 ) | 3, ( 4 << 4 ) | 3, 0, 0 },
    { ( 1 << 4 ) | 0, ( 6 << 4 ) | 1, 0, 0 },
    { ( 0 << 4 ) | 2, ( 3 << 4 ) | 2, 0, 0 },
    { ( 4 << 4 ) | 2, ( 5 << 4 ) | 2, ( 6 << 4 ) | 2, ( 7 << 4 ) | 2 },
    { ( 2 << 4 ) | 0, ( 5 << 4 ) | 0, 0, 0 },
};

int osoStageBytes[8][12] = {
    { 0x5d, 0x55, 0x1c, 0x75, 0x27, 0x95, 0xc5, 0x58, 0x75, 0x56, 0x59, 0x55 },
    { 0xd5, 0xdd, 0x31, 0x5b, 0x55, 0x41, 0xb7, 0x55, 0x15, 0x5d, 0x67, 0xd5 },
    { 0x3c, 0x55, 0x15, 0x0f, 0x55, 0x95, 0x3c, 0x01, 0x80, 0x55, 0xff, 0xbf },
    { 0xd7, 0x74, 0xd5, 0xd9, 0x15, 0x63, 0x36, 0x4d, 0x6d, 0x56, 0x55, 0x65 },
    { 0xf4, 0x45, 0xd1, 0x55, 0x92, 0x21, 0x75, 0xd6, 0x01, 0xde, 0x55, 0x56 },
    { 0x77, 0xdf, 0x5d, 0x5c, 0xdd, 0x94, 0x51, 0x65, 0xd5, 0xd6, 0x55, 0x59 },
    { 0x00, 0x45, 0x15, 0x00, 0x74, 0x80, 0xfc, 0x55, 0xbf, 0xfe, 0xff, 0xbf },
    { 0x54, 0x75, 0x70, 0xf2, 0x37, 0x35, 0xc6, 0x4f, 0x9d, 0x56, 0xd5, 0x95 },
};

// Point-bonus values, indexed by a per-block "monsters killed on this
// slide so far" combo counter (clamped to 3 - see header comment).
int osoPointValues[4] = { 10, 20, 40, 80 };

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int osoScore;
int osoRemainCount;
int osoCurrentStage;
int osoStageTime;
int osoItemCount;
int osoClock;
int osoMonsterNum;
int osoRndIndex;
int osoStageIndex;
int osoNextCell;

#define OSO_MAX_TIME_DENOM ( 50 / ( 8 / OSO_COORD_RATE ) )
#define OSO_BONUS_RATE 5

int osoVVram[OSO_VVRAM_HEIGHT][OSO_VVRAM_WIDTH];
int osoCellMap[OSO_COLUMN_COUNT * OSO_FLOOR_COUNT];

typedef struct
{
    int x, y, code;
} OsoSprite;
OsoSprite osoSprites[OSO_SPRITE_END];

OsoMovable osoMan;
int osoManDirDx;
int osoManDirDy;
int osoManNormalPattern;
int osoManPushPattern;
int osoManPushingTime;

int osoMonsterCount;
OsoMovable osoMonsters[OSO_MAX_MONSTER_COUNT];
int osoMonsterClock;

typedef struct
{
    int initialByte, column, row, status, count;
} OsoFixedBlock;
OsoFixedBlock osoFixedBlocks[OSO_MAX_BLOCK_COUNT];

typedef struct
{
    int x, y, sprite, status, dx, dy, fixedIdx;
} OsoMovingBlock;
OsoMovingBlock osoMovingBlocks[OSO_MAX_MOVING_BLOCK_COUNT];

typedef struct
{
    int x, y, sprite, status;
} OsoPoint;
OsoPoint osoPoints[OSO_MAX_POINT_COUNT];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into osoAsciiPattern (0 = space) per cell.
int osoStatusChar[8][32];

// Set true only while on the title screen (OSO_STATE_TITLE) - matching
// Cracky's own `crkFullWidthText` exactly. Upstream's real Title() never
// touches the VVram/map system again after its initial ClearScreen(), and
// instead drives the whole screen (not just the status zone) through the
// same PrintC()/PrintS() text mechanism, at real columns spanning the whole
// 0-31 char-cell range. When true, osoComposeRawByte() reads osoStatusChar
// across the full width instead of just columns 24-31.
bool osoFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintTimeUp()/PrintGameOver() Vram-direct writes - see header.
bool osoOverlayActive;
int osoOverlayText[12];
int osoOverlayLen;
int osoOverlayPage;
int osoOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of OSO_TICK_DIVISOR.
int osoSeqMelody[3];
int osoSeqPos[3];
int osoSeqWait[3];
int osoSeqActive[3];

#define OSO_TICK_DIVISOR 8
int osoTickCounter;
int osoTimeDenom;

#define OSO_STATE_TITLE 0
#define OSO_STATE_START_JINGLE 1
#define OSO_STATE_PLAYING 2
#define OSO_STATE_LOSE_ANIM 3
#define OSO_STATE_GAMEOVER_JINGLE 4
#define OSO_STATE_CLEAR_WAIT_A 5
#define OSO_STATE_CLEAR_WAIT_B 6
#define OSO_STATE_CLEAR_JINGLE 7
#define OSO_STATE_BONUS_TALLY 8
int osoState;
int osoWaitFrames;
int osoAnimStep;
int osoSelection;
bool osoSelectionChanged;
int osoPrevLeft;
int osoPrevRight;
int osoPrevUp;
int osoPrevDown;
int osoPrevFire;
bool osoPendingContinue;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int osoRnd()
{
    int r;
    r = osoRndNumbers[ osoRndIndex ];
    osoRndIndex = osoRndIndex + 1;
    if( osoRndIndex >= 32 )
      osoRndIndex = 0;
    return r;
}

int osoAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int osoCellMapPtr( int column, int row )
{
    return ( row / 2 ) * OSO_COLUMN_COUNT + column;
}

int osoGetCell( int column, int row )
{
    int b;
    b = osoCellMap[ osoCellMapPtr( column, row ) ];
    if( ( row & 1 ) != 0 )
      b = b >> 4;
    return b & 0x0f;
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void osoLocateMovable( OsoMovable* pMovable, int b )
{
    pMovable->x = ( b & 0xf0 ) >> ( 3 - OSO_COORD_SHIFT );
    pMovable->y = ( ( ( ( b & 15 ) << 2 ) + OSO_CELL_SIZE ) << OSO_COORD_SHIFT );
}

bool osoIsNearXY( int x, int y, int p2x, int p2y )
{
    return
        x + OSO_COORD_RATE * 2 >= p2x && p2x + OSO_COORD_RATE * 2 >= x &&
        y + OSO_COORD_RATE * 2 >= p2y && p2y + OSO_COORD_RATE * 2 >= y;
}

bool osoIsNear( OsoMovable* p1, OsoMovable* p2 )
{
    return
        p1->x + OSO_HIT_RANGE >= p2->x && p2->x + OSO_HIT_RANGE >= p1->x &&
        p1->y + OSO_HIT_RANGE >= p2->y && p2->y + OSO_HIT_RANGE >= p1->y;
}

void osoMoveMovable( OsoMovable* pMovable )
{
    pMovable->x = pMovable->x + pMovable->dx;
    pMovable->y = pMovable->y + pMovable->dy;
}

bool osoCanMove( OsoMovable* pMovable, int dx, int dy )
{
    int x, y, column, row, nextColumn, nextRow, nextCell;
    osoNextCell = 0;
    x = pMovable->x;
    if( ( x & OSO_CELL_COORD_MASK ) != 0 )
      return dy == 0;
    y = pMovable->y;
    if( ( y & OSO_CELL_COORD_MASK ) != 0 )
      return dx == 0;
    column = x >> OSO_CELL_COORD_SHIFT;
    if( dx < 0 && column == 0 ) return false;
    if( dx > 0 && column >= OSO_COLUMN_COUNT - 1 ) return false;
    row = y >> OSO_CELL_COORD_SHIFT;
    if( dy > 0 && row >= OSO_ROW_COUNT - 1 ) return false;
    if( ( pMovable->status & OSO_MOVABLE_FALL ) == 0 )
    {
        nextColumn = column + dx;
        nextRow = row + dy;
        nextCell = osoGetCell( nextColumn, nextRow );
        if( ( nextCell & OSO_CELL_BLOCK ) != 0 )
        {
            osoNextCell = nextCell;
            return false;
        }
        if( ( nextRow & 1 ) == 0 )
        {
            if( ( nextCell & OSO_CELL_CEILING ) != 0 ) return false;
        }
        if( dy < 0 )
        {
            if( ( nextCell & OSO_CELL_DOWN ) == 0 ) return false;
        }
    }
    return true;
}

bool osoFallMovable( OsoMovable* pMovable )
{
    int column, nextRow, nextCell;
    column = pMovable->x >> OSO_CELL_COORD_SHIFT;
    nextRow = ( pMovable->y >> OSO_CELL_COORD_SHIFT ) + 1;
    if( nextRow >= OSO_ROW_COUNT )
    {
        pMovable->status = pMovable->status & ~OSO_MOVABLE_FALL;
        return false;
    }
    nextCell = osoGetCell( column, nextRow );
    if( ( nextRow & 1 ) != 0 )
      nextCell = nextCell & ~OSO_CELL_ITEM;
    if( nextCell == 0 )
    {
        pMovable->status = pMovable->status | OSO_MOVABLE_FALL;
        return true;
    }
    pMovable->status = pMovable->status & ~OSO_MOVABLE_FALL;
    return false;
}

bool osoInRange( OsoMovable* pMovable, int dx, int dy )
{
    int column, row;
    column = ( pMovable->x >> OSO_CELL_COORD_SHIFT ) + dx;
    if( column >= OSO_COLUMN_COUNT ) return false;
    row = ( pMovable->y >> OSO_CELL_COORD_SHIFT ) + dy;
    return row < OSO_ROW_COUNT;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites into osoSprites[], drawn into osoVVram once per
//   frame by osoDrawSpritesIntoVVram(), matching upstream's own two-step
//   ShowSprite()-then-DrawSprites() split exactly.
// -----------------------------------------------------------------------------

void osoHideAllSprites()
{
    int i;
    for( i = 0; i < OSO_SPRITE_END; i = i + 1 )
      osoSprites[ i ].code = OSO_INVALID_CODE;
}

void osoShowSpriteXY( int sprite, int x, int y, int pattern )
{
    osoSprites[ sprite ].x = x;
    osoSprites[ sprite ].y = y;
    osoSprites[ sprite ].code = pattern;
}

void osoShowSprite( OsoMovable* pMovable, int pattern )
{
    osoShowSpriteXY( pMovable->sprite, pMovable->x, pMovable->y, pattern );
}

void osoHideSprite( int index )
{
    osoSprites[ index ].code = OSO_INVALID_CODE;
}

// Writes 4 consecutive glyph indices (c,c+1,c+2,c+3) into a 2x2 VVram block
// at (x,y), bound-checked per-cell exactly like upstream's own DrawSprites()
// - the only VVram writer in this file that ever needs a bound check, since
// sprite coordinates are player/monster/block-driven, not fixed layout data.
void osoDrawSpriteInto2x2( int x, int y, int c )
{
    int row, col, cc;
    cc = c;
    for( row = 0; row < 2; row = row + 1 )
    {
        if( y + row < OSO_VVRAM_HEIGHT )
        {
            for( col = 0; col < 2; col = col + 1 )
            {
                if( x + col < OSO_VVRAM_WIDTH )
                  osoVVram[ y + row ][ x + col ] = cc;
                cc = cc + 1;
            }
        }
        else
          cc = cc + 2;
    }
}

void osoDrawSpritesIntoVVram()
{
    int i;
    for( i = 0; i < OSO_SPRITE_END; i = i + 1 )
    {
        if( osoSprites[ i ].code != OSO_INVALID_CODE )
          osoDrawSpriteInto2x2( osoSprites[ i ].x, osoSprites[ i ].y, osoSprites[ i ].code );
    }
}


// -----------------------------------------------------------------------------
//   Block.cpp
// -----------------------------------------------------------------------------

void osoBlockLocate( int fb )
{
    int b, column, row;
    b = osoFixedBlocks[ fb ].initialByte;
    column = b >> 4;
    row = ( ( b & 0x0f ) << 1 ) + 1;
    osoFixedBlocks[ fb ].column = column;
    osoFixedBlocks[ fb ].row = row;
    osoCellMap[ osoCellMapPtr( column, row ) ] = osoCellMap[ osoCellMapPtr( column, row ) ] | ( OSO_CELL_BLOCK << 4 );
}

void osoBlockErase( int fb )
{
    int column, row, idx;
    column = osoFixedBlocks[ fb ].column;
    row = osoFixedBlocks[ fb ].row;
    idx = osoCellMapPtr( column, row );
    if( ( row & 1 ) == 0 )
      osoCellMap[ idx ] = osoCellMap[ idx ] & ~OSO_CELL_BLOCK;
    else
      osoCellMap[ idx ] = osoCellMap[ idx ] & ~( OSO_CELL_BLOCK << 4 );
}

// Draws the fixed block's own destroying/restarting animation frame - the
// same "4 consecutive glyph indices in a 2x2 block" shape as every other
// map-tile writer here, at (column<<CellShift, row<<CellShift), unbounded
// (a fixed block's own column/row are always safely on-map by construction).
void osoBlockDrawAnim( int fb, int c )
{
    int x, y, row, col, cc;
    x = osoFixedBlocks[ fb ].column << OSO_CELL_SHIFT;
    y = osoFixedBlocks[ fb ].row << OSO_CELL_SHIFT;
    cc = c;
    for( row = 0; row < 2; row = row + 1 )
    {
        for( col = 0; col < 2; col = col + 1 )
        {
            osoVVram[ y + row ][ x + col ] = cc;
            cc = cc + 1;
        }
    }
}

void osoInitBlocks()
{
    int i, sprite;
    for( i = 0; i < osoStageBlockCount[ osoStageIndex ]; i = i + 1 )
    {
        osoFixedBlocks[ i ].initialByte = osoStageBlocks[ osoStageIndex ][ i ];
        osoBlockLocate( i );
        osoFixedBlocks[ i ].status = OSO_BLOCK_LIVE;
    }
    for( i = osoStageBlockCount[ osoStageIndex ]; i < OSO_MAX_BLOCK_COUNT; i = i + 1 )
    {
        osoFixedBlocks[ i ].initialByte = OSO_INVALID_POSITION;
        osoFixedBlocks[ i ].status = 0;
    }

    sprite = OSO_SPRITE_BLOCK;
    for( i = 0; i < OSO_MAX_MOVING_BLOCK_COUNT; i = i + 1 )
    {
        osoMovingBlocks[ i ].sprite = sprite;
        osoMovingBlocks[ i ].status = 0;
        sprite = sprite + 1;
    }
}

// Start() - claims a free moving-block slot for a fixed block that just
// started sliding; returns the slot index, or -1 if every slot is busy
// (matching upstream's own `nullptr` return, checked by the caller).
int osoMovingBlockStart( int fb )
{
    int i;
    for( i = 0; i < OSO_MAX_MOVING_BLOCK_COUNT; i = i + 1 )
    {
        if( ( osoMovingBlocks[ i ].status & OSO_MOVABLE_LIVE ) == 0 )
        {
            osoMovingBlocks[ i ].x = osoFixedBlocks[ fb ].column << OSO_CELL_COORD_SHIFT;
            osoMovingBlocks[ i ].y = osoFixedBlocks[ fb ].row << OSO_CELL_COORD_SHIFT;
            osoMovingBlocks[ i ].status = OSO_MOVABLE_LIVE;
            osoMovingBlocks[ i ].fixedIdx = fb;
            osoShowSpriteXY( osoMovingBlocks[ i ].sprite, osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y, OSO_CHAR_BLOCK );
            osoBlockErase( fb );
            osoFixedBlocks[ fb ].status = 0;
            return i;
        }
    }
    return -1;
}

bool osoStartMovingBlock( int fb, int dx )
{
    int i;
    i = osoMovingBlockStart( fb );
    if( i >= 0 )
    {
        osoMovingBlocks[ i ].dx = dx;
        osoMovingBlocks[ i ].dy = 0;
        return true;
    }
    return false;
}

bool osoPushBlock( int px, int py, int dx, int dy )
{
    int column, row, i, nextColumn;
    if( ( ( px | py ) & OSO_CELL_COORD_MASK ) != 0 ) return false;
    column = ( px >> OSO_CELL_COORD_SHIFT ) + dx;
    row = ( py >> OSO_CELL_COORD_SHIFT ) + dy;
    if( ( osoGetCell( column, row ) & OSO_CELL_BLOCK ) == 0 ) return false;

    for( i = 0; i < OSO_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( osoFixedBlocks[ i ].status == OSO_BLOCK_LIVE &&
            osoFixedBlocks[ i ].column == column && osoFixedBlocks[ i ].row == row )
        {
            bool destroy;
            destroy = false;
            if( dx != 0 )
            {
                if( column == 0 || column == OSO_COLUMN_COUNT - 1 )
                  destroy = true;
            }
            else
            {
                if( row == OSO_ROW_COUNT - 1 )
                  destroy = true;
            }
            if( !destroy )
            {
                nextColumn = column + dx;
                if( ( osoGetCell( nextColumn, row ) & ( OSO_CELL_BLOCK | OSO_CELL_ITEM ) ) != 0 )
                  destroy = true;
            }
            if( destroy )
            {
                osoFixedBlocks[ i ].status = OSO_BLOCK_DESTROYING;
                osoFixedBlocks[ i ].count = 0;
                return true;
            }
            return osoStartMovingBlock( i, dx );
        }
    }
    return false;
}

void osoUpdateBlocks()
{
    int i, nextRow, nextCell;
    for( i = 0; i < OSO_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( osoFixedBlocks[ i ].status == OSO_BLOCK_DESTROYING )
        {
            osoFixedBlocks[ i ].count = osoFixedBlocks[ i ].count + 1;
            if( osoFixedBlocks[ i ].count >= OSO_COORD_RATE * 4 )
            {
                osoBlockErase( i );
                osoFixedBlocks[ i ].status = OSO_BLOCK_SLEEPING;
                osoFixedBlocks[ i ].count = 0;
            }
        }
        else if( osoFixedBlocks[ i ].status == OSO_BLOCK_LIVE )
        {
            nextRow = osoFixedBlocks[ i ].row + 1;
            if( nextRow < OSO_ROW_COUNT )
            {
                nextCell = osoGetCell( osoFixedBlocks[ i ].column, nextRow );
                if( ( nextCell & ( OSO_CELL_BLOCK | OSO_CELL_CEILING ) ) == 0 &&
                    ( ( nextRow & 1 ) != 0 || ( nextCell & OSO_CELL_UP ) == 0 ) )
                  osoStartMovingBlock( i, 0 );
            }
        }
        else if( osoFixedBlocks[ i ].status == OSO_BLOCK_RESTARTING )
        {
            osoFixedBlocks[ i ].count = osoFixedBlocks[ i ].count + 1;
            if( osoFixedBlocks[ i ].count >= OSO_COORD_RATE * 4 )
              osoFixedBlocks[ i ].status = OSO_BLOCK_LIVE;
        }
        else if( osoFixedBlocks[ i ].status == OSO_BLOCK_SLEEPING )
        {
            osoFixedBlocks[ i ].count = osoFixedBlocks[ i ].count + 1;
            if( osoFixedBlocks[ i ].count >= OSO_COORD_RATE * 4 )
            {
                osoFixedBlocks[ i ].status = OSO_BLOCK_RESTARTING;
                osoFixedBlocks[ i ].count = 0;
                osoBlockLocate( i );
            }
        }
    }
}

// excludeIndex: the calling moving block's own index (skip self-collision),
// or -1 when called for Man (never matches any moving-block index).
bool osoHitToBlock( int x, int y, int dx, int dy, int excludeIndex )
{
    int i, tx, ty;
    tx = x + dx;
    ty = y + dy;
    for( i = 0; i < OSO_MAX_MOVING_BLOCK_COUNT; i = i + 1 )
    {
        if( ( osoMovingBlocks[ i ].status & OSO_MOVABLE_LIVE ) != 0 && i != excludeIndex )
        {
            if( osoIsNearXY( tx, ty, osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y ) )
              return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp / Sound sequencer / Score - moved up from their
//   original position further down in this file (see the sibling
//   tinyjoypad_vircon32 source's own git history / the duplicate-free single
//   copy carried over here), since osoStartPoint() (right after this block)
//   needs osoAddScoreForward() and this dialect has no forward declarations.
//   Everything here only depends on already-declared globals (osoStatusChar,
//   osoOverlay*, osoSeq*, osoMelody* data tables, osoScore, etc) and
//   md_playTone()/md_stopTone(), so it's safe to move as one self-contained
//   block.
// -----------------------------------------------------------------------------

int osoAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct port of PrintC()'s
    // own linear search (only 27 entries, no cost concern doing this live).
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

int osoPrintC( int page, int col, int c )
{
    osoStatusChar[ page ][ col ] = osoAsciiIndex( c );
    return col + 1;
}

int osoPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = osoPrintC( page, col, s[ i ] );
    return col;
}

void osoPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      osoPrintC( page, col, ' ' );
    else
      osoPrintC( page, col, d1 + '0' );
    osoPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void osoPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        osoPrintC( page, col, ' ' );
        if( d2 == 0 )
          osoPrintC( page, col + 1, ' ' );
        else
          osoPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        osoPrintC( page, col, d1 + '0' );
        osoPrintC( page, col + 1, d2 + '0' );
    }
    osoPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void osoPrintNumber5( int page, int col, int w )
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
          osoPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            osoPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    osoPrintC( page, col + 4, rem + '0' );
}

// Column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset.
void osoPrintScore()
{
    osoPrintNumber5( 1, 26, osoScore );
    osoPrintC( 1, 31, '0' );
}

void osoPrintTime()
{
    osoPrintByteNumber3( 5, 29, osoStageTime );
}

void osoPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    osoPrintS( 0, 24, sScore, 5 );
    osoPrintS( 3, 24, sStage, 5 );
    osoPrintByteNumber2( 3, 30, osoCurrentStage + 1 );
    osoPrintS( 5, 24, sTime, 4 );

    if( osoRemainCount > 1 )
    {
        i = osoRemainCount - 1;
        if( i > 2 )
        {
            // upstream draws a real 2x2 Char_Remain icon (Put2C) here, then
            // a space, then the remaining digit - simplified to plain text
            // digits throughout (matching this port's own status-text-only
            // lives display, the same simplification Cracky's own
            // crkPrintStatus already made for the identical upstream shape).
            osoPrintC( 7, 24, ' ' );
            osoPrintC( 7, 25, ' ' );
            osoPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < osoRemainCount - 1; i = i + 1 )
              osoPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    osoPrintScore();
    osoPrintTime();
}

void osoBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    osoOverlayActive = true;
    osoOverlayLen = len;
    osoOverlayPage = page;
    osoOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      osoOverlayText[ i ] = s[ i ];
}

void osoPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    osoBeginOverlay( s, 9, 4, 8 );
}

void osoPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    osoBeginOverlay( s, 7, 4, 9 );
}

int osoMelodyLength( int id )
{
    if( id == OSO_MELODY_LOOSE ) return 3;
    if( id == OSO_MELODY_HIT ) return 17;
    if( id == OSO_MELODY_BEEP ) return 3;
    if( id == OSO_MELODY_PUSH ) return 13;
    if( id == OSO_MELODY_START ) return 27;
    if( id == OSO_MELODY_CLEAR ) return 22;
    if( id == OSO_MELODY_GAMEOVER ) return 21;
    if( id == OSO_MELODY_BGM1 ) return 105;
    if( id == OSO_MELODY_BGM2 ) return 105;
    return 0;
}

int osoMelodyValue( int id, int idx )
{
    if( id == OSO_MELODY_LOOSE ) return osoMelodyLoose[ idx ];
    if( id == OSO_MELODY_HIT ) return osoMelodyHit[ idx ];
    if( id == OSO_MELODY_BEEP ) return osoMelodyBeep[ idx ];
    if( id == OSO_MELODY_PUSH ) return osoMelodyPush[ idx ];
    if( id == OSO_MELODY_START ) return osoMelodyStart[ idx ];
    if( id == OSO_MELODY_CLEAR ) return osoMelodyClear[ idx ];
    if( id == OSO_MELODY_GAMEOVER ) return osoMelodyGameOver[ idx ];
    if( id == OSO_MELODY_BGM1 ) return osoMelodyBgm1[ idx ];
    if( id == OSO_MELODY_BGM2 ) return osoMelodyBgm2[ idx ];
    return 0;
}

int osoNoteFrames( int length )
{
    return (int)( length * 1.875 + 0.5 );
}

void osoStartSeq( int channel, int melodyId )
{
    osoSeqMelody[ channel ] = melodyId;
    osoSeqPos[ channel ] = 0;
    osoSeqWait[ channel ] = 0;
    osoSeqActive[ channel ] = 1;
}

void osoStopSeq( int channel )
{
    osoSeqActive[ channel ] = 0;
    osoSeqMelody[ channel ] = OSO_MELODY_NONE;
}

bool osoSeqPlaying( int channel )
{
    return osoSeqActive[ channel ] != 0;
}

// Same 39-entry E2..G5 equal-tempered table Cracky's own Sound.cpp uses,
// resolved by scale-value directly (1-based, matching the Scale enum).
int osoFrequencyForNote( int note )
{
    int freqs[39] = {
        82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
        147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
        262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
        466, 494, 523, 554, 587, 622, 659, 698, 740,
    };
    return freqs[ note - 1 ];
}

void osoAdvanceOneSeq( int channel )
{
    int length, note;

    if( osoSeqActive[ channel ] == 0 ) return;

    if( osoSeqWait[ channel ] > 0 )
    {
        osoSeqWait[ channel ] = osoSeqWait[ channel ] - 1;
        return;
    }

    length = osoMelodyValue( osoSeqMelody[ channel ], osoSeqPos[ channel ] );
    if( length == 0 )
    {
        osoStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        osoSeqPos[ channel ] = 0;
        length = osoMelodyValue( osoSeqMelody[ channel ], 0 );
    }
    note = osoMelodyValue( osoSeqMelody[ channel ], osoSeqPos[ channel ] + 1 );
    osoSeqPos[ channel ] = osoSeqPos[ channel ] + 2;
    osoSeqWait[ channel ] = osoNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)osoFrequencyForNote( note ), (float)osoSeqWait[ channel ] / 60.0 );
}

void osoAdvanceSound()
{
    osoAdvanceOneSeq( 0 );
    osoAdvanceOneSeq( 1 );
    osoAdvanceOneSeq( 2 );
}

void osoStartBgm()
{
    osoStartSeq( 1, OSO_MELODY_BGM1 );
    osoStartSeq( 2, OSO_MELODY_BGM2 );
}

void osoStopBgm()
{
    osoStopSeq( 1 );
    osoStopSeq( 2 );
    md_stopTone();
}

void osoAddScoreForward( int pts )
{
    osoScore = osoScore + pts;
    osoPrintScore();
}


// HitMonster() - kills the first monster near (x,y), if any. Forward-used by
// osoMoveBlocksOnce() below; Monster state itself is declared further down,
// so this takes explicit monster-array access via the already-declared
// osoMonsters[]/osoMonsterCount globals (both declared above already).
bool osoHitMonster( int x, int y )
{
    int i;
    OsoMovable probe;
    probe.x = x; probe.y = y;
    for( i = 0; i < osoMonsterCount; i = i + 1 )
    {
        if( ( osoMonsters[ i ].status & OSO_MOVABLE_LIVE ) == 0 ) continue;
        if( osoIsNear( &probe, &osoMonsters[ i ] ) )
        {
            osoMonsters[ i ].status = osoMonsters[ i ].status & ~OSO_MOVABLE_LIVE;
            osoHideSprite( osoMonsters[ i ].sprite );
            return true;
        }
    }
    return false;
}

void osoStartPoint( int x, int y, int rate )
{
    int i, clampedRate;
    clampedRate = rate;
    if( clampedRate > 3 ) clampedRate = 3; // see header comment - a real OOB guard
    osoAddScoreForward( osoPointValues[ clampedRate ] );
    for( i = 0; i < OSO_MAX_POINT_COUNT; i = i + 1 )
    {
        if( ( osoPoints[ i ].status & OSO_MOVABLE_LIVE ) != 0 ) continue;
        osoPoints[ i ].status = OSO_MOVABLE_LIVE | ( 6 << OSO_COORD_SHIFT );
        osoPoints[ i ].x = x;
        osoPoints[ i ].y = y;
        osoShowSpriteXY( osoPoints[ i ].sprite, x, y, OSO_CHAR_POINT + ( clampedRate << 2 ) );
        return;
    }
}

void osoMoveBlocksOnce()
{
    int i, column, row, lowerCell, nextColumn, nextRow, nextCell;
    bool canMove, stop;
    for( i = 0; i < OSO_MAX_MOVING_BLOCK_COUNT; i = i + 1 )
    {
        if( ( osoMovingBlocks[ i ].status & OSO_MOVABLE_LIVE ) == 0 ) continue;
        canMove = true;
        stop = false;
        if( ( ( osoMovingBlocks[ i ].x | osoMovingBlocks[ i ].y ) & OSO_CELL_COORD_MASK ) == 0 )
        {
            column = osoMovingBlocks[ i ].x >> OSO_CELL_COORD_SHIFT;
            row = osoMovingBlocks[ i ].y >> OSO_CELL_COORD_SHIFT;
            if( row < OSO_ROW_COUNT - 1 )
            {
                lowerCell = osoGetCell( column, row + 1 );
                if( ( lowerCell & ( OSO_CELL_CEILING | OSO_CELL_UP | OSO_CELL_BLOCK ) ) == 0 )
                {
                    osoMovingBlocks[ i ].dx = 0;
                    osoMovingBlocks[ i ].dy = 1;
                }
            }
            nextColumn = column + osoMovingBlocks[ i ].dx;
            nextRow = row + osoMovingBlocks[ i ].dy;
            // Real bug, found via a direct user report on the sibling
            // Vircon32 project ("push the lower left block... it ends up
            // at right part of screen"), confirmed against upstream's own
            // MoveBlocks() (Block.cpp): `byte nextColumn = column +
            // pMovingBlock->dx;` there is a `byte` (unsigned), so a block
            // sliding left off column 0 computes nextColumn as a genuine
            // unsigned wraparound (0 + -1 = 255), which the existing
            // `nextColumn >= ColumnCount` check already catches, correctly
            // settling the block right at the edge - the same unsigned-
            // byte-boundary reliance already documented extensively
            // elsewhere in this project (e.g. Lift's own CanMoveTo() fix).
            // This port's `nextColumn` is a plain, non-wrapping signed int,
            // so it just stays -1 and never satisfies that upper-bound-only
            // check - the block keeps "moving" one column further left
            // every tick, forever, with its own on-screen column going
            // increasingly negative. Once that negative column feeds into
            // the sprite/VVram positioning math elsewhere (which has no
            // equivalent negative guard of its own, since upstream never
            // needed one here), it wraps back into a large positive
            // on-screen position - the reported "ends up on the right side
            // of the screen". Unlike Lift's own fix (a single boundary
            // already reachable from either direction), this game's own
            // osoPushBlock() already guards the *first* push against a
            // block already sitting at column 0/ColumnCount-1 (destroying
            // it instead) - this exact gap is reachable only once a block
            // is already sliding continuously, on the tick it would cross
            // the boundary. Fixed with an explicit lower-bound check
            // alongside the existing upper bound, reproducing upstream's
            // real two-sided boundary behavior directly rather than
            // relying on wraparound. Ported directly from the sibling
            // project's own identical fix.
            if( nextColumn < 0 || nextColumn >= OSO_COLUMN_COUNT || nextRow < 0 || nextRow >= OSO_ROW_COUNT )
              stop = true;
            else
            {
                nextCell = osoGetCell( nextColumn, nextRow );
                if( ( nextCell & ( OSO_CELL_ITEM | OSO_CELL_BLOCK ) ) != 0 )
                  stop = true;
                else if( ( nextRow & 1 ) == 0 && ( nextCell & OSO_CELL_UP ) != 0 )
                  stop = true;
                // A real bug found via a careful line-by-line trace against
                // upstream's own MoveBlocks(): `HitToBlock(pMovingBlock)`
                // there computes `x=pMovable->x+pMovable->dx; y=pMovable->y+
                // pMovable->dy` - i.e. it projects the block's own NEXT
                // position (using its real dx/dy, here always dx=0/dy=1
                // whenever this branch can run, since the gravity check
                // just above sets both together) forward before checking
                // for a collision with another already-live block. Passing
                // the block's own dx/dy here (not literal 0,0) matches
                // upstream exactly.
                else if( osoMovingBlocks[ i ].dy != 0 &&
                         osoHitToBlock( osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y,
                                         osoMovingBlocks[ i ].dx, osoMovingBlocks[ i ].dy, i ) )
                  canMove = false;
            }
            if( stop )
            {
                int idx;
                osoHideSprite( osoMovingBlocks[ i ].sprite );
                osoMovingBlocks[ i ].status = 0;
                osoFixedBlocks[ osoMovingBlocks[ i ].fixedIdx ].column = column;
                osoFixedBlocks[ osoMovingBlocks[ i ].fixedIdx ].row = row;
                osoFixedBlocks[ osoMovingBlocks[ i ].fixedIdx ].status = OSO_BLOCK_LIVE;
                idx = osoCellMapPtr( column, row );
                if( ( row & 1 ) == 0 )
                  osoCellMap[ idx ] = osoCellMap[ idx ] | OSO_CELL_BLOCK;
                else
                  osoCellMap[ idx ] = osoCellMap[ idx ] | ( OSO_CELL_BLOCK << 4 );
                continue;
            }
        }
        if( canMove )
        {
            osoMovingBlocks[ i ].x = osoMovingBlocks[ i ].x + osoMovingBlocks[ i ].dx;
            osoMovingBlocks[ i ].y = osoMovingBlocks[ i ].y + osoMovingBlocks[ i ].dy;
        }
        osoShowSpriteXY( osoMovingBlocks[ i ].sprite, osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y, OSO_CHAR_BLOCK );
        if( ( ( osoMovingBlocks[ i ].x | osoMovingBlocks[ i ].y ) & OSO_CELL_MASK ) == 0 )
        {
            if( osoHitMonster( osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y ) )
            {
                osoStartPoint( osoMovingBlocks[ i ].x, osoMovingBlocks[ i ].y, osoMovingBlocks[ i ].status & OSO_BLOCK_RATE_MASK );
                osoMovingBlocks[ i ].status = osoMovingBlocks[ i ].status + 1;
                osoStartSeq( 0, OSO_MELODY_HIT );
            }
        }
    }
}

void osoDrawBlocksAnim()
{
    int i, index;
    for( i = 0; i < OSO_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( osoFixedBlocks[ i ].status == OSO_BLOCK_DESTROYING )
        {
            index = osoFixedBlocks[ i ].count >> OSO_COORD_SHIFT;
            osoBlockDrawAnim( i, OSO_CHAR_BLOCK + 4 + ( index << 2 ) );
        }
        else if( osoFixedBlocks[ i ].status == OSO_BLOCK_RESTARTING )
        {
            index = osoFixedBlocks[ i ].count >> OSO_COORD_SHIFT;
            osoBlockDrawAnim( i, OSO_CHAR_BLOCK + 20 + ( index << 2 ) );
        }
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void osoManShow()
{
    int pattern, seq;
    if( ( osoMan.status & OSO_MAN_PUSHING ) != 0 )
      pattern = osoManPushPattern;
    else
    {
        pattern = osoMan.status & OSO_PATTERN_MASK;
        if( ( osoMan.status & OSO_MOVABLE_FALL ) == 0 )
        {
            if( osoMan.dy != 0 )
            {
                seq = ( osoMan.y >> OSO_COORD_SHIFT ) & 1;
                pattern = pattern + seq;
            }
            else if( osoMan.dx != 0 )
            {
                seq = ( osoMan.x >> OSO_COORD_SHIFT ) & 3;
                if( seq == 3 ) seq = 1;
                pattern = pattern + seq + 1;
            }
        }
    }
    osoShowSprite( &osoMan, ( pattern << 2 ) + OSO_CHAR_MAN );
}

void osoInitMan()
{
    osoMan.sprite = OSO_SPRITE_MAN;
    osoMan.status = OSO_MOVABLE_LIVE | OSO_PATTERN_LEFT;
    osoMan.dx = 0;
    osoMan.dy = 0;
    osoManDirDx = -1;
    osoManDirDy = 0;
    osoManNormalPattern = OSO_PATTERN_LEFT;
    osoManPushPattern = OSO_PATTERN_LEFT_PUSH;
    osoLocateMovable( &osoMan, osoStageStart[ osoStageIndex ] );
    osoManShow();
}

void osoMoveMan()
{
    int dx, dy, pattern, column, row, cell, idx;

    if( ( osoMan.status & OSO_MAN_PUSHING ) != 0 )
    {
        if( osoManPushingTime >= OSO_COORD_RATE )
          osoMan.status = osoMan.status & ~OSO_MAN_PUSHING;
        else
          osoManPushingTime = osoManPushingTime + 1;
    }

    if( ( ( osoMan.x | osoMan.y ) & OSO_COORD_MASK ) == 0 )
    {
        bool left, right, up, down, fire, hasDir;
        dx = 0; dy = 0;
        pattern = osoMan.status & OSO_PATTERN_MASK;
        if( ( osoMan.status & OSO_MOVABLE_FALL ) == 0 )
        {
            left = isLeftPressed();
            right = isRightPressed();
            up = isUpPressed();
            down = isDownPressed();
            hasDir = left || right || up || down;

            if( hasDir )
            {
                int wantDx, wantDy, wantNormal, wantPush;
                if( left )      { wantDx = -1; wantDy = 0;  wantNormal = OSO_PATTERN_LEFT;    wantPush = OSO_PATTERN_LEFT_PUSH; }
                else if( right ){ wantDx = 1;  wantDy = 0;  wantNormal = OSO_PATTERN_RIGHT;   wantPush = OSO_PATTERN_RIGHT_PUSH; }
                else if( up )   { wantDx = 0;  wantDy = -1; wantNormal = OSO_PATTERN_UPDOWN;  wantPush = OSO_PATTERN_UP_PUSH; }
                else            { wantDx = 0;  wantDy = 1;  wantNormal = OSO_PATTERN_UPDOWN;  wantPush = OSO_PATTERN_DOWN_PUSH; }

                if( osoCanMove( &osoMan, wantDx, wantDy ) )
                {
                    dx = wantDx; dy = wantDy; pattern = wantNormal;
                    osoManDirDx = wantDx; osoManDirDy = wantDy;
                    osoManNormalPattern = wantNormal; osoManPushPattern = wantPush;
                }
                else if( osoNextCell != 0 && ( ( osoMan.x | osoMan.y ) & OSO_CELL_COORD_MASK ) == 0 )
                {
                    // matches upstream's own "goto stop" - blocked by a
                    // block right ahead, cancel movement but still face the
                    // just-pressed direction (needed so Fire knows which
                    // way to push).
                    dx = 0; dy = 0;
                    osoManDirDx = wantDx; osoManDirDy = wantDy;
                    if( wantDx != 0 ) pattern = wantNormal;
                    osoManNormalPattern = wantNormal; osoManPushPattern = wantPush;
                }
                else if( osoCanMove( &osoMan, osoManDirDx, osoManDirDy ) )
                {
                    dx = osoManDirDx; dy = osoManDirDy; pattern = osoManNormalPattern;
                }
                else
                {
                    dx = 0; dy = 0;
                    osoManDirDx = wantDx; osoManDirDy = wantDy;
                    if( wantDx != 0 ) pattern = wantNormal;
                    osoManNormalPattern = wantNormal; osoManPushPattern = wantPush;
                }
            }

            osoMan.dx = dx;
            osoMan.dy = dy;
            osoMan.status = ( osoMan.status & ~OSO_PATTERN_MASK ) | pattern;

            fire = isFirePressed();
            if( fire && ( osoMan.status & OSO_MAN_PUSHING ) == 0 )
            {
                if( osoPushBlock( osoMan.x, osoMan.y, osoManDirDx, osoManDirDy ) )
                {
                    osoStartSeq( 0, OSO_MELODY_PUSH );
                    osoMan.status = osoMan.status | OSO_MAN_PUSHING;
                    osoManPushingTime = 0;
                }
            }
        }
        if( osoHitToBlock( osoMan.x, osoMan.y, osoMan.dx, osoMan.dy, -1 ) )
        {
            osoMan.dx = 0;
            osoMan.dy = 0;
        }
    }

    osoMoveMovable( &osoMan );
    if( ( ( osoMan.x | osoMan.y ) & OSO_CELL_COORD_MASK ) == 0 )
    {
        column = osoMan.x >> OSO_CELL_COORD_SHIFT;
        row = osoMan.y >> OSO_CELL_COORD_SHIFT;
        if( ( row & 1 ) != 0 )
        {
            idx = osoCellMapPtr( column, row );
            cell = osoCellMap[ idx ];
            if( ( cell & ( OSO_CELL_ITEM << 4 ) ) != 0 )
            {
                osoCellMap[ idx ] = cell & ~( OSO_CELL_ITEM << 4 );
                osoItemCount = osoItemCount - 1;
                osoAddScoreForward( 5 );
                osoStartSeq( 0, OSO_MELODY_HIT );
            }
        }
        if( osoFallMovable( &osoMan ) )
        {
            osoMan.dy = 1;
            osoMan.dx = 0;
            osoManDirDx = 0; osoManDirDy = 1;
            osoManNormalPattern = OSO_PATTERN_UPDOWN;
            osoManPushPattern = OSO_PATTERN_DOWN_PUSH;
        }
    }
    osoManShow();
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

int osoMonsterDirDx[4] = { -1, 1, 0, 0 };
int osoMonsterDirDy[4] = { 0, 0, -1, 1 };

void osoMonsterShow( OsoMovable* pMonster )
{
    int status, index, pattern, seq;
    status = pMonster->status;
    index = status & OSO_MONSTER_PATTERN_MASK;
    if( ( status & OSO_MONSTER_THROGH ) != 0 )
      pattern = ( index << 2 ) + OSO_CHAR_MONSTERREV;
    else
    {
        seq = ( ( pMonster->x + pMonster->y ) >> OSO_COORD_SHIFT ) & 1;
        pattern = ( ( ( index << 1 ) + seq ) << 2 ) + OSO_CHAR_MONSTER;
    }
    osoShowSprite( pMonster, pattern );
}

void osoDecideDirection( OsoMovable* pMonster )
{
    int directions[4];
    int verticalIdx, horizontalIdx;
    int i, direction, dx, dy;
    bool throughable, can;

    if( osoAbs( osoMan.x, pMonster->x ) > osoAbs( osoMan.y, pMonster->y ) )
    {
        if( osoMan.x < pMonster->x )
        {
            if( pMonster->dx <= 0 )
            {
                directions[ 0 ] = OSO_DIR_LEFT;
                directions[ 3 ] = OSO_DIR_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = OSO_DIR_RIGHT;
                directions[ 3 ] = OSO_DIR_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                directions[ 0 ] = OSO_DIR_RIGHT;
                directions[ 3 ] = OSO_DIR_LEFT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = OSO_DIR_LEFT;
                directions[ 3 ] = OSO_DIR_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( osoMan.y < pMonster->y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            directions[ verticalIdx ] = OSO_DIR_UP;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = OSO_DIR_DOWN;
        }
        else
        {
            directions[ verticalIdx ] = OSO_DIR_DOWN;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = OSO_DIR_UP;
        }
    }
    else
    {
        if( osoMan.y < pMonster->y )
        {
            if( pMonster->dy <= 0 )
            {
                directions[ 0 ] = OSO_DIR_UP;
                directions[ 3 ] = OSO_DIR_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = OSO_DIR_DOWN;
                directions[ 3 ] = OSO_DIR_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                directions[ 0 ] = OSO_DIR_DOWN;
                directions[ 3 ] = OSO_DIR_UP;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = OSO_DIR_UP;
                directions[ 3 ] = OSO_DIR_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares `Man.x < pMonster->y` here too (a real upstream
        // quirk, preserved exactly - matching Cracky's own identical, also-
        // preserved quirk in this exact spot, likely shared boilerplate).
        if( ( osoMan.x < pMonster->y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            directions[ horizontalIdx ] = OSO_DIR_LEFT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = OSO_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalIdx ] = OSO_DIR_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = OSO_DIR_LEFT;
        }
    }

    throughable = ( pMonster->status & OSO_MONSTER_THROGH ) != 0;
    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        dx = osoMonsterDirDx[ direction ];
        dy = osoMonsterDirDy[ direction ];
        can = osoCanMove( pMonster, dx, dy );
        if( can )
        {
            if( throughable && i == 0 )
              pMonster->status = pMonster->status & ~OSO_MONSTER_THROGH;
        }
        else if( throughable )
          can = osoInRange( pMonster, dx, dy );
        if( can )
        {
            pMonster->dx = dx;
            pMonster->dy = dy;
            pMonster->status = ( pMonster->status & ~OSO_MONSTER_PATTERN_MASK ) | direction;
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void osoInitMonsters()
{
    int i, sprite;
    osoMonsterCount = osoStageEnemyCount[ osoStageIndex ];
    sprite = OSO_SPRITE_MONSTER;
    for( i = 0; i < osoMonsterCount; i = i + 1 )
    {
        osoMonsters[ i ].status = OSO_MOVABLE_LIVE;
        osoMonsters[ i ].sprite = sprite;
        osoMonsters[ i ].dx = 0;
        osoMonsters[ i ].dy = 0;
        osoLocateMovable( &osoMonsters[ i ], osoStageEnemies[ osoStageIndex ][ i ] );
        osoDecideDirection( &osoMonsters[ i ] );
        osoMonsterShow( &osoMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = osoMonsterCount; i < OSO_MAX_MONSTER_COUNT; i = i + 1 )
    {
        osoMonsters[ i ].status = 0;
        osoMonsters[ i ].sprite = sprite;
        osoHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void osoMoveMonsters()
{
    int i;
    osoMonsterClock = osoMonsterClock + 1;

    for( i = 0; i < OSO_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( osoMonsters[ i ].status & OSO_MOVABLE_LIVE ) == 0 ) continue;

        if( ( osoMonsters[ i ].status & OSO_MOVABLE_FALL ) == 0 )
        {
            if( ( ( osoMonsters[ i ].x | osoMonsters[ i ].y ) & OSO_CELL_COORD_MASK ) == 0 )
            {
                if( ( osoRnd() << 1 ) <= osoCurrentStage )
                  osoMonsters[ i ].status = osoMonsters[ i ].status | OSO_MONSTER_THROGH;
                osoDecideDirection( &osoMonsters[ i ] );
            }
        }
        if( ( osoMonsterClock & 1 ) == 0 || ( osoMonsters[ i ].status & OSO_MONSTER_THROGH ) == 0 )
          osoMoveMovable( &osoMonsters[ i ] );

        if( osoIsNear( &osoMonsters[ i ], &osoMan ) )
          osoMan.status = osoMan.status & ~OSO_MOVABLE_LIVE;

        if( ( ( osoMonsters[ i ].x | osoMonsters[ i ].y ) & OSO_CELL_COORD_MASK ) == 0 &&
            ( osoMonsters[ i ].status & OSO_MONSTER_THROGH ) == 0 )
        {
            if( osoFallMovable( &osoMonsters[ i ] ) )
            {
                osoMonsters[ i ].dy = 1;
                osoMonsters[ i ].dx = 0;
            }
        }
        osoMonsterShow( &osoMonsters[ i ] );
    }
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void osoInitPoints()
{
    int i, sprite;
    sprite = OSO_SPRITE_POINT;
    for( i = 0; i < OSO_MAX_POINT_COUNT; i = i + 1 )
    {
        osoPoints[ i ].status = 0;
        osoPoints[ i ].sprite = sprite;
        osoHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void osoUpdatePoints()
{
    int i, status, time;
    for( i = 0; i < OSO_MAX_POINT_COUNT; i = i + 1 )
    {
        status = osoPoints[ i ].status;
        if( ( status & OSO_MOVABLE_LIVE ) == 0 ) continue;
        time = status & ~OSO_MOVABLE_LIVE;
        if( time == 0 )
        {
            osoPoints[ i ].status = 0;
            osoHideSprite( osoPoints[ i ].sprite );
        }
        else
        {
            time = time - 1;
            osoPoints[ i ].status = OSO_MOVABLE_LIVE | time;
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage()/InitTrying()
// -----------------------------------------------------------------------------

void osoInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never actually stops the player from continuing beyond the last
    // stage) - preserved via the same wrap loop upstream uses instead of a
    // plain modulo, matching Cracky's own identical precedent.
    int i, j;
    i = 0; j = 0;
    while( i < osoCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= OSO_STAGE_COUNT )
          j = 0;
    }
    osoStageIndex = j;
}

// Moved up from their original position (right after this point in the
// sibling tinyjoypad_vircon32 source's own earlier draft) - osoInitTrying()
// below needs osoDrawAll() and this dialect has no forward declarations.
// All 3 only depend on osoVVram/osoCellMap globals and
// osoDrawBlocksAnim()/osoDrawSpritesIntoVVram(), both already defined
// above.

// Writes glyph indices c0,c1 (top row) / c2,c3 (bottom row) into a 2x2
// VVram block at (x,y) - shared by every DrawBackground() branch below
// (VPut2C/VPut2S/VErase2 upstream all share this exact iteration shape,
// only differing in which 4 values they pass).
void osoWriteBlock2x2( int x, int y, int c0, int c1, int c2, int c3 )
{
    osoVVram[ y ][ x ] = c0;
    osoVVram[ y ][ x + 1 ] = c1;
    osoVVram[ y + 1 ][ x ] = c2;
    osoVVram[ y + 1 ][ x + 1 ] = c3;
}

void osoDrawBackground()
{
    int floor, col, cell, x, y, i;

    for( floor = 0; floor < OSO_FLOOR_COUNT; floor = floor + 1 )
    {
        for( col = 0; col < OSO_COLUMN_COUNT; col = col + 1 )
        {
            cell = osoCellMap[ floor * OSO_COLUMN_COUNT + col ];
            x = col * OSO_COLUMN_WIDTH;
            y = floor * OSO_FLOOR_HEIGHT;

            // low nibble - top half of the floor's own 4-row block
            if( ( cell & OSO_CELL_BLOCK ) != 0 )
              osoWriteBlock2x2( x, y, OSO_CHAR_BLOCK, OSO_CHAR_BLOCK + 1, OSO_CHAR_BLOCK + 2, OSO_CHAR_BLOCK + 3 );
            else if( ( cell & OSO_CELL_CEILING ) != 0 )
              osoWriteBlock2x2( x, y, OSO_CHAR_FLOOR, OSO_CHAR_FLOOR, OSO_CHAR_SPACE, OSO_CHAR_SPACE );
            else if( ( cell & ( OSO_CELL_DOWN | OSO_CELL_UP ) ) != 0 )
              osoWriteBlock2x2( x, y, OSO_CHAR_LADDER_LEFT, OSO_CHAR_LADDER_RIGHT, OSO_CHAR_LADDER_LEFT, OSO_CHAR_LADDER_RIGHT );
            else
              osoWriteBlock2x2( x, y, OSO_CHAR_SPACE, OSO_CHAR_SPACE, OSO_CHAR_SPACE, OSO_CHAR_SPACE );

            // high nibble - bottom half
            cell = cell >> 4;
            if( ( cell & OSO_CELL_BLOCK ) != 0 )
              osoWriteBlock2x2( x, y + 2, OSO_CHAR_BLOCK, OSO_CHAR_BLOCK + 1, OSO_CHAR_BLOCK + 2, OSO_CHAR_BLOCK + 3 );
            else if( ( cell & OSO_CELL_ITEM ) != 0 )
              osoWriteBlock2x2( x, y + 2, OSO_CHAR_ITEM, OSO_CHAR_ITEM + 1, OSO_CHAR_ITEM + 2, OSO_CHAR_ITEM + 3 );
            else if( ( cell & ( OSO_CELL_DOWN | OSO_CELL_UP ) ) != 0 )
              osoWriteBlock2x2( x, y + 2, OSO_CHAR_LADDER_LEFT, OSO_CHAR_LADDER_RIGHT, OSO_CHAR_LADDER_LEFT, OSO_CHAR_LADDER_RIGHT );
            else
              osoWriteBlock2x2( x, y + 2, OSO_CHAR_SPACE, OSO_CHAR_SPACE, OSO_CHAR_SPACE, OSO_CHAR_SPACE );
        }
    }

    // one extra solid-floor row beneath the lowest floor (upstream's own
    // trailing `repeat(ColumnWidth*ColumnCount){*pVVram++=Char_Floor;}`)
    for( i = 0; i < OSO_COLUMN_WIDTH * OSO_COLUMN_COUNT; i = i + 1 )
      osoVVram[ OSO_FLOOR_COUNT * OSO_FLOOR_HEIGHT ][ i ] = OSO_CHAR_FLOOR;
}

void osoDrawAll()
{
    osoDrawBackground();
    osoDrawBlocksAnim();
    osoDrawSpritesIntoVVram();
}

void osoInitTrying()
{
    int floor, colGroup, upper, lower, bits, cellIdx, i;

    osoRndIndex = 0;
    osoItemCount = 0;
    for( i = 0; i < OSO_COLUMN_COUNT * OSO_FLOOR_COUNT; i = i + 1 )
      osoCellMap[ i ] = 0;

    for( floor = 0; floor < OSO_FLOOR_COUNT; floor = floor + 1 )
    {
        for( colGroup = 0; colGroup < OSO_COLUMN_COUNT / OSO_COLUMNS_PER_BYTE; colGroup = colGroup + 1 )
        {
            int sub;
            upper = osoStageBytes[ osoStageIndex ][ floor * ( OSO_COLUMN_COUNT / OSO_COLUMNS_PER_BYTE ) + colGroup ];
            // A genuine out-of-bounds array read, found by tracing the real
            // index math against osoStageBytes[8][12]'s own declared size:
            // upstream's identical pointer-walking loop (InitTrying(),
            // Stage.cpp) reads one "lower" group of 3 bytes past the real
            // 12-byte bytes[] array on the very last floor (floor==3) too -
            // harmless there (adjacent static flash data, and the read
            // value is never actually used, since every branch below that
            // consults `lower` is itself gated behind `floor <
            // OSO_FLOOR_COUNT-1`, which is false on floor 3) but a real
            // out-of-bounds array access here. Guarded the read itself
            // instead of just trusting the value stays unused - matches
            // upstream's real behavior exactly (lower is simply never
            // examined on the last floor) without ever touching memory
            // outside the array.
            if( floor < OSO_FLOOR_COUNT - 1 )
              lower = osoStageBytes[ osoStageIndex ][ floor * ( OSO_COLUMN_COUNT / OSO_COLUMNS_PER_BYTE ) + colGroup + ( OSO_COLUMN_COUNT / OSO_COLUMNS_PER_BYTE ) ];
            else
              lower = 0;
            for( sub = 0; sub < OSO_COLUMNS_PER_BYTE; sub = sub + 1 )
            {
                bits = upper & 3;
                cellIdx = floor * OSO_COLUMN_COUNT + ( colGroup * OSO_COLUMNS_PER_BYTE + sub );

                if( bits == 1 && floor < OSO_FLOOR_COUNT - 1 ) // Floor
                  osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] = osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] | OSO_CELL_CEILING;
                if( bits == 2 ) // Ladder
                {
                    osoCellMap[ cellIdx ] = osoCellMap[ cellIdx ] | ( OSO_CELL_UP | OSO_CELL_DOWN ) | ( OSO_CELL_UP << 4 );
                    if( floor < OSO_FLOOR_COUNT - 1 )
                      osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] = osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] | OSO_CELL_CEILING;
                }
                if( floor < OSO_FLOOR_COUNT - 1 && ( lower & 3 ) == 2 ) // Ladder (from below)
                {
                    osoCellMap[ cellIdx ] = osoCellMap[ cellIdx ] | ( OSO_CELL_DOWN << 4 );
                    osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] = osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] & ~OSO_CELL_CEILING;
                }
                if( bits == 3 ) // Item
                {
                    osoCellMap[ cellIdx ] = osoCellMap[ cellIdx ] | ( OSO_CELL_ITEM << 4 );
                    if( floor < OSO_FLOOR_COUNT - 1 )
                      osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] = osoCellMap[ cellIdx + OSO_COLUMN_COUNT ] | OSO_CELL_CEILING;
                    osoItemCount = osoItemCount + 1;
                }

                upper = upper >> 2;
                lower = lower >> 2;
            }
        }
    }

    osoStageTime = 40;
    i = osoItemCount;
    do
    {
        osoStageTime = osoStageTime + 8;
        if( osoStageTime > 240 ) break;
        i = i - 1;
    } while( i != 0 );

    osoHideAllSprites();
    // ClearScreen() upstream also blanks the raw hardware VRAM directly -
    // matched here by clearing osoVVram itself (every real frame is already
    // redrawn fully from it via osoDrawAll() -> osoRender(), so there's no
    // separate "hardware clear" step to reproduce), plus the status-text
    // grid and the message overlay - the same "clear cache/overlay state
    // that doesn't get naturally overwritten" lesson Cracky's own
    // crkInitTrying() comment already documents for the identical shape.
    for( floor = 0; floor < OSO_VVRAM_HEIGHT; floor = floor + 1 )
    {
        int col;
        for( col = 0; col < OSO_VVRAM_WIDTH; col = col + 1 )
          osoVVram[ floor ][ col ] = OSO_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          osoStatusChar[ i ][ j ] = 0;
    }
    osoOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in osoUpdateTitle()) - matches osoOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches osoInitTrying() without going through that transition first
    // (matching Cracky's own crkInitTrying() identical defensive reset).
    osoFullWidthText = false;

    osoPrintStatus();
    osoInitBlocks();
    osoInitMan();
    osoInitMonsters();
    osoInitPoints();
    osoDrawAll();
}


// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly, the
// same formula as Cracky's own crkComposeRawByte() - see that file's header
// and this one's own for the derivation. The one real difference from
// Cracky: rawPage*2+1/+2 here, not rawPage*2/+1 - see this file's own
// header comment on the genuine 1-row vertical crop this game's VVram has
// that Cracky's doesn't. No hardware-orientation transform is applied - see
// header.
//
// **OR-combines mapByte with textByte instead of choosing one exclusively**,
// matching the same fix applied to the sibling `crkComposeRawByte()`: on
// the title screen (osoFullWidthText) the logo occupies real hardware pages
// 0-2 only (VVram rows 2-5), while every status-text element (SCORE/MINI/
// START/CONTINUE/credit) is printed on pages 0/3/5/6/7 - entirely disjoint
// page ranges, so ORing the two can never actually blend two distinct
// pieces of content together; it just lets both coexist in one composed
// byte instead of one silently excluding the other. During real gameplay
// (!osoFullWidthText) this is a no-op behavior change: mapByte is only ever
// computed for rawCol<96 (and textByte is 0 there, since charCol<24 always
// resolves to an osoStatusChar cell that gameplay never writes), and
// textByte is only ever nonzero for rawCol>=96 (where mapByte is never
// computed, staying 0) - so the OR-combine reduces to exactly the same
// single-value result the old if/else already produced for every gameplay
// column.
int osoComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < OSO_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = osoVVram[ rawPage * 2 + 1 ][ mapX ];
        lower = osoVVram[ rawPage * 2 + 2 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = osoCharPattern[ upper * 2 + 0 ];
            lowerByte = osoCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = osoCharPattern[ upper * 2 + 0 ];
            lowerByte = osoCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = osoCharPattern[ upper * 2 + 1 ];
            lowerByte = osoCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = osoCharPattern[ upper * 2 + 1 ];
            lowerByte = osoCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !osoFullWidthText && rawCol < OSO_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // osoStatusChar's own full-width indexing directly - no more "subtract
    // the map width" local-offset math needed, since rawCol/4 already
    // lands on the correct real column either way (whether this is the
    // osoFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = osoStatusChar[ rawPage ][ charCol ];
            textByte = osoAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void osoRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( osoOverlayActive && page == osoOverlayPage &&
                col >= osoOverlayCol * 4 && col < osoOverlayCol * 4 + osoOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - osoOverlayCol * 4 ) / 4;
                sub = ( col - osoOverlayCol * 4 ) % 4;
                value = osoAsciiPattern[ osoAsciiIndex( osoOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = osoComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// Places every title-screen element at upstream's real, literal columns
// (see this file's own header comment for the discovery story shared with
// the sibling gameCracky.c): "MINI" at col 17, "START"/"CONTINUE" at col 9
// with the cursor at col 8, the credit line at col 12 - all genuinely clear
// of the status labels' own columns 24-31, so nothing here needs trimming,
// relocating, or dropping.
void osoBeginTitle()
{
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int i;

    for( i = 0; i < OSO_VVRAM_HEIGHT; i = i + 1 )
    {
        int j;
        for( j = 0; j < OSO_VVRAM_WIDTH; j = j + 1 )
          osoVVram[ i ][ j ] = OSO_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          osoStatusChar[ i ][ j ] = 0;
    }
    osoOverlayActive = false;
    osoFullWidthText = true;
    osoHideAllSprites();
    // A real bug found via direct reasoning about Cracky's own already-
    // documented sibling fix ("on game over, the time value remains
    // visible on titlescreen"): osoPrintStatus() below redraws TIME from
    // whatever osoStageTime last held during real gameplay - reset here so
    // the title screen always shows a fresh "TIME 000" instead of a stale
    // leftover value, matching Cracky's own fix for the identical shape.
    osoStageTime = 0;
    osoPrintStatus();

    // Upstream's own real 5-glyph title logo bitmap, drawn directly into
    // osoVVram from osoTitleBytes[] at its own real position (VVram row 2,
    // starting column OSO_TITLE_LEFT - matching `Status.cpp`'s `Title()`'s
    // own `VVram + VVramWidth*2 + TitleLeft` starting offset exactly).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < OSO_TITLE_LENGTH; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                osoVVram[ 2 + row ][ OSO_TITLE_LEFT + ch * 4 + col ] = osoTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 17 - derived from
    // `TitleLeft + 4*TitleLength - 5` relative to the logo bitmap's own
    // position - START/CONTINUE at col 9 with the cursor at col 8, the
    // credit line at col 12).
    osoPrintS( 3, 17, sMini, 4 );
    osoPrintS( 5, 9, sStart, 5 );
    osoPrintS( 6, 9, sContinue, 8 );
    osoPrintS( 7, 12, sCredit, 12 );

    osoSelection = 0;
    osoSelectionChanged = true;
    osoPrevLeft = 0; osoPrevRight = 0; osoPrevUp = 0; osoPrevDown = 0; osoPrevFire = 0;
    osoState = OSO_STATE_TITLE;
}

void osoUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !osoPrevLeft ) || ( right && !osoPrevRight ) ||
                ( up && !osoPrevUp ) || ( down && !osoPrevDown ) );
    justFire = ( fire && !osoPrevFire );
    osoPrevLeft = left; osoPrevRight = right; osoPrevUp = up; osoPrevDown = down; osoPrevFire = fire;

    if( osoSelectionChanged )
    {
        osoSelectionChanged = false;
        if( osoSelection == 0 )
          osoPrintC( 5, 8, '>' );
        else
          osoPrintC( 5, 8, ' ' );
        if( osoSelection == 1 )
          osoPrintC( 6, 8, '>' );
        else
          osoPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        osoFullWidthText = false;
        osoPendingContinue = ( osoSelection == 1 );
        osoScore = 0;
        if( !osoPendingContinue )
          osoCurrentStage = 0;
        osoRemainCount = 3;
        osoInitStage();
        osoInitTrying();
        osoDrawAll();
        osoStartSeq( 1, OSO_MELODY_START );
        osoClock = 0;
        osoMonsterNum = 0;
        osoTimeDenom = OSO_MAX_TIME_DENOM;
        osoTickCounter = 0;
        osoState = OSO_STATE_START_JINGLE;
        osoRender();
        return;
    }
    if( justDir )
    {
        osoSelection = osoSelection ^ 1;
        osoSelectionChanged = true;
    }
    osoRender();
}

void osoUpdateStartJingle()
{
    if( !osoSeqPlaying( 1 ) )
    {
        osoStartBgm();
        osoState = OSO_STATE_PLAYING;
    }
    osoRender();
}

void osoBeginLose()
{
    osoStopBgm();
    osoAnimStep = 0;
    osoWaitFrames = 0;
    osoState = OSO_STATE_LOSE_ANIM;
}

void osoUpdateLoseAnim()
{
    int patterns[4] = { OSO_CHAR_MAN_LEFT, OSO_CHAR_MAN_LOOSE0, OSO_CHAR_MAN_LOOSE1, OSO_CHAR_MAN_LOOSE2 };

    if( osoWaitFrames > 0 )
    {
        osoWaitFrames = osoWaitFrames - 1;
        osoRender();
        return;
    }

    osoShowSprite( &osoMan, patterns[ osoAnimStep & 3 ] );
    osoDrawAll();
    osoStartSeq( 0, OSO_MELODY_LOOSE );
    osoAnimStep = osoAnimStep + 1;
    osoWaitFrames = osoNoteFrames( 1 );

    if( osoAnimStep >= 8 )
    {
        osoRemainCount = osoRemainCount - 1;
        if( osoRemainCount > 0 )
        {
            osoInitTrying();
            osoDrawAll();
            osoOverlayActive = false;
            osoStartSeq( 1, OSO_MELODY_START );
            osoClock = 0;
            osoMonsterNum = 0;
            osoTimeDenom = OSO_MAX_TIME_DENOM;
            osoTickCounter = 0;
            osoState = OSO_STATE_START_JINGLE;
        }
        else
        {
            osoPrintGameOver();
            osoStartSeq( 1, OSO_MELODY_GAMEOVER );
            osoState = OSO_STATE_GAMEOVER_JINGLE;
        }
    }
    osoRender();
}

void osoUpdateGameOverJingle()
{
    if( !osoSeqPlaying( 1 ) )
      osoBeginTitle();
    else
      osoRender();
}

void osoBeginClearWaitA()
{
    osoWaitFrames = 30;
    osoState = OSO_STATE_CLEAR_WAIT_A;
}

void osoUpdateClearWaitA()
{
    if( osoWaitFrames > 0 )
    {
        osoWaitFrames = osoWaitFrames - 1;
        osoRender();
        return;
    }
    osoStopBgm();
    osoWaitFrames = 10;
    osoState = OSO_STATE_CLEAR_WAIT_B;
    osoRender();
}

void osoUpdateClearWaitB()
{
    if( osoWaitFrames > 0 )
    {
        osoWaitFrames = osoWaitFrames - 1;
        osoRender();
        return;
    }
    osoStartSeq( 1, OSO_MELODY_CLEAR );
    osoState = OSO_STATE_CLEAR_JINGLE;
    osoRender();
}

void osoUpdateClearJingle()
{
    if( !osoSeqPlaying( 1 ) )
    {
        osoWaitFrames = 0;
        osoState = OSO_STATE_BONUS_TALLY;
    }
    osoRender();
}

void osoUpdateBonusTally()
{
    if( osoWaitFrames > 0 )
    {
        osoWaitFrames = osoWaitFrames - 1;
        osoRender();
        return;
    }

    if( osoStageTime >= OSO_BONUS_RATE )
    {
        osoAddScoreForward( 5 );
        osoStageTime = osoStageTime - OSO_BONUS_RATE;
        osoPrintTime();
        osoStartSeq( 0, OSO_MELODY_BEEP );
        osoWaitFrames = osoNoteFrames( 1 );
        osoRender();
        return;
    }

    osoStageTime = 0;
    osoPrintStatus();
    osoCurrentStage = osoCurrentStage + 1;
    osoInitStage();
    osoInitTrying();
    osoDrawAll();
    osoStartSeq( 1, OSO_MELODY_START );
    osoClock = 0;
    osoMonsterNum = 0;
    osoTimeDenom = OSO_MAX_TIME_DENOM;
    osoTickCounter = 0;
    osoState = OSO_STATE_START_JINGLE;
    osoRender();
}

void osoUpdatePlaying()
{
    osoTickCounter = osoTickCounter + 1;
    if( osoTickCounter < OSO_TICK_DIVISOR )
    {
        osoRender();
        return;
    }
    osoTickCounter = 0;

    osoMoveMan();
    if( osoMonsterNum >= 0 )
    {
        osoMoveMonsters();
        osoMonsterNum = osoMonsterNum - 10;
    }
    osoMonsterNum = osoMonsterNum + 6;

    osoTimeDenom = osoTimeDenom - 1;
    if( osoTimeDenom == 0 )
    {
        osoStageTime = osoStageTime - 1;
        osoTimeDenom = OSO_MAX_TIME_DENOM;
        osoPrintTime();
        if( osoStageTime == 0 )
        {
            // Matches upstream's own `goto lose` here exactly: UpdateBlocks/
            // UpdatePoints/MoveBlocks/the group's own DrawAll are all
            // skipped, relying (like upstream relies on real VRAM
            // persistence) on the previous tick's already-drawn frame
            // staying visible - see header comment.
            osoPrintTimeUp();
            osoRender();
            osoBeginLose();
            return;
        }
    }

    osoUpdateBlocks();
    osoUpdatePoints();
    // Two MoveBlocks() calls per real tick, back-to-back - see header
    // comment for why this collapses upstream's own "one visible call plus
    // one silent call between ticks" shape into a single tick-boundary pair.
    osoMoveBlocksOnce();
    osoMoveBlocksOnce();

    osoDrawAll();

    if( ( osoMan.status & OSO_MOVABLE_LIVE ) == 0 )
    {
        osoRender();
        osoBeginLose();
        return;
    }

    if( osoItemCount == 0 )
    {
        osoRender();
        osoBeginClearWaitA();
        return;
    }

    osoRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameOsotos_init()
{
    int i;

    osoScore = 0;
    osoCurrentStage = 0;
    osoRemainCount = 3;
    osoStageTime = 0;
    osoRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        osoSeqActive[ i ] = 0;
        osoSeqMelody[ i ] = OSO_MELODY_NONE;
    }
    osoOverlayActive = false;
    osoTickCounter = 0;
    osoMonsterClock = 0;

    osoBeginTitle();
}

void gameOsotos_update()
{
    osoAdvanceSound();

    if( osoState == OSO_STATE_TITLE )
      osoUpdateTitle();
    else if( osoState == OSO_STATE_START_JINGLE )
      osoUpdateStartJingle();
    else if( osoState == OSO_STATE_PLAYING )
      osoUpdatePlaying();
    else if( osoState == OSO_STATE_LOSE_ANIM )
      osoUpdateLoseAnim();
    else if( osoState == OSO_STATE_GAMEOVER_JINGLE )
      osoUpdateGameOverJingle();
    else if( osoState == OSO_STATE_CLEAR_WAIT_A )
      osoUpdateClearWaitA();
    else if( osoState == OSO_STATE_CLEAR_WAIT_B )
      osoUpdateClearWaitB();
    else if( osoState == OSO_STATE_CLEAR_JINGLE )
      osoUpdateClearJingle();
    else if( osoState == OSO_STATE_BONUS_TALLY )
      osoUpdateBonusTally();
}
