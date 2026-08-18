#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// MIEYEN mini (inufuto, UIAPduino+SSD1306 edition, "License: None specified" -
// GitHub reports no LICENSE file for `UIAPduino_mieyen`) - a maze/hunting
// game: walk a 8x5 grid maze dropping breadcrumbs (dots) behind you, shoot
// the INVISIBLE monsters roaming the maze before they steal every hamburger
// (food) or touch you. The monsters only ever flash visible for a moment -
// when they eat one of your dropped breadcrumbs (silently, no flash), when
// they steal a hamburger (a flash + a "stolen" jingle), when they touch you,
// or while they're playing their multi-step death animation after being
// shot - the rest of the time they're rendered with no sprite at all. Since
// a breadcrumb only ever gets dropped once (on a cell with no existing dot/
// food) and a monster silently erases it in passing, watching which crumbs
// vanish over time is the *only* way to infer an invisible monster's own
// past path (matches the readme.md's own description almost exactly:
// "defeat every invisible monster to clear; the breadcrumbs a monster eats
// disappear, so you can infer its position; losing every hamburger or
// touching a monster ends the game"). 8 hand-authored mazes, 3 lives, real
// persistent hi-score tracked in-session (same CH32V003/RISC-V-not-AVR
// situation as this project's own Cracky port - no EEPROM/avrCompat concerns
// here at all).
//
// Ported directly following `gameCracky.c`'s own already-proven methodology
// (same author/engine lineage - `Vram.cpp`/`VVram.cpp`/`Oled.cpp`/
// `ScanKeys.cpp`/`Timer.cpp` are all confirmed BYTE-FOR-BYTE identical to
// Cracky's own copies via a direct `diff`, only `Sound.cpp`'s Tempo/
// melodies/`Point.cpp`/`Fire.cpp`/`Chars.cpp`/`Stage.cpp`/`Stages.cpp`
// genuinely differ) - reusing every technique that port already proved out,
// not re-deriving them from scratch. Standard mechanical dialect conversion
// applied throughout (`TYPE[N] name` -> `TYPE name[N]`, bare `struct Foo{};`
// -> `typedef struct{} Foo;`) matching this project's own established
// recipe - not called out per-occurrence below since none of it changes any
// game logic.
//
// **No hardware display-orientation transform is applied, matching Cracky's
// own final, user-verified conclusion** - `InitOled()`'s `SegRemap`/
// `ComScanDec` register writes exist to compensate for a real physical
// panel-mounting quirk on the ACTUAL hardware, with nothing analogous to
// correct for in a software recreation. `miyComposeRawByte(col,page)` is
// drawn directly at its own `(col,page)` - no column mirror, no page
// reorder, no bit-reversal, no lookup table.
//
// **Rendering reuses the identical two-level VVram/CharPattern tile system
// Cracky's own header comment already derived in full** - `VVramToVram()`/
// `SendUL()`'s exact nibble-interleave (`(upper&0xf)|(lower<<4)` /
// `(upper>>4)|(lower&0xf0)`), `AsciiPattern`'s own 27-character table (byte-
// diff confirmed identical to Cracky's own copy), and the status-text-area
// split at raw column 96 (`VVramWidth*4`) - all reused verbatim as
// `miyComposeRawByte()`. Unlike Cracky's own `int[16][24] crkVVram` (a real
// 2D array, since Cracky's own `MapToVVram()` only ever used explicit x/y
// loops), Mieyen's own `Stage.cpp` genuinely relies on raw pointer-walking
// arithmetic for its wall-drawing pass (`pVVram += 2`, `pVVram += VVramWidth
// * 3 - ColumnWidth * ColumnCount`, etc) - reproducing that arithmetic
// faithfully was far lower-risk than re-deriving an equivalent nested-loop
// form by hand, so this port uses a genuinely FLAT `int[VVramWidth*
// VVramHeight]` array (`miyVVramBack`/`miyVVramFront`) with an explicit
// `miyVVramIdx(x,y)` helper, matching upstream's own real flat-array layout
// even more literally than Cracky's simplification did - `miyVPut`/
// `miyVPut2C`/`miyVErase2` are direct 1:1 ports of `VPut`/`VPut2C`/
// `VErase2`, taking an explicit `int*` buffer parameter since sometimes the
// target is the persistent "map" layer (`miyVVramBack`, written by
// `miySetCell`/the wall-drawing pass) and sometimes the per-frame composite
// (`miyVVramFront`, written by `miyDrawSprites`/`miyDrawPoints`) - matching
// upstream's own real Back-vs-Front distinction, which this port's
// `miyDrawAll()` (`VVramBackToFront + DrawSprites + DrawPoints`) preserves
// exactly rather than collapsing into one buffer.
//
// **The `GAME OVER` message reuses Cracky's own already-solved overlay
// technique** (`miyOverlayActive`/`miyOverlayText`/`miyOverlayPage`/
// `miyOverlayCol`) - `PrintGameOver()` writes its text directly into real
// Vram bytes bypassing VVram entirely, which persists on real hardware only
// because nothing else re-touches those exact bytes; since this port always
// redraws the full frame from VVram every tick, a literal port would make
// the message flash for one frame and vanish.
//
// **The title screen's own real dithered logo bitmap (`TitleBytes[]`,
// `miyTitleBytes` here) is drawn for real**, matching Cracky's own logo
// treatment - `miyComposeRawByte()` OR-combines the VVram-derived map byte
// with `miyStatusChar`'s own text layer instead of choosing one exclusively,
// since the two occupy disjoint real hardware pages by construction (the
// logo only ever occupies pages 1-2, every piece of title-screen status
// text lives on pages 0/3/5/6/7).
//
// **A genuinely different, more elaborate movement/AI model than Cracky's
// own floor-and-ladder platforming** - a real 8x5 walled maze grid (`Stage.h`'s
// own per-cell `Cell_RightWall`/`Cell_BottomWall` bits, not floors/ladders),
// with the man/monsters/fire bullets all sharing ONE combined
// `struct MiyMovable` (x, y, sprite, status, dx, dy, targetX, targetY, time -
// every field ANY of the three flavors ever needs), matching this project's
// own established "flatten every flavor into one struct + explicit-pointer
// functions" precedent (Tiny Plaque's own `TplaqSprite`) rather than the
// separate-but-compatible-layout inheritance upstream's C++ uses (`struct
// Monster : Movable {...}`, `struct Fire : Movable {...}`). `ShowMovable()`
// (shared by BOTH Man and Monster upstream, a single generic direction-
// pattern-cycling function - simpler than Cracky's own bespoke per-object
// `crkShowMan`/`crkShowMonster`) is reproduced as `miyShowMovable()`.
//
// **A genuinely reachable out-of-bounds VVram write, found and fixed by
// inspection before ever compiling, not by a crash**: `DrawPoints()`
// upstream has NO bounds checking at all (unlike `DrawSprites()`, which
// checks `x < VVramWidth`/`y < VVramHeight` per cell) - its 3-cell-wide
// score-popup strip is placed directly at a just-hit monster's own real X
// coordinate, which can legitimately reach 24 (`ToCoord(7)+2`, the far edge
// of the last grid column) - `24+2=26`, past the real 24-wide VVram row,
// a genuine out-of-bounds array write on this platform (harmless-on-real-
// hardware adjacent-memory scribbling upstream, a real correctness/safety
// concern here). Fixed by clamping the popup's own X in `miyStartPoint()`
// to `MIY_VVRAM_WIDTH-3` (21) - the popup shifts up to 3 cells left of the
// hit position only in this rare far-right-edge case, never silently
// corrupts memory.
//
// **The same signed-`byte`-minus-1-then-compared-unsigned safety concern
// already documented in `ShowSprite()`'s own upstream comment** (`pSprite->x
// = pMovable->x - 1;`, only ever compared against an upper bound
// `x<VVramWidth` on real hardware, where a genuinely-zero `pMovable->x`
// would wrap to 255 and safely fail that same check) - proven, by tracing
// every reachable coordinate through `CanMove()`'s own boundary guards, that
// `pMovable->x` can never actually be 0 for any of Man/Monster/Fire (column0's
// own left-wall check blocks leaving x=1, the minimum grid coordinate) -
// but ported with an explicit `x>=0` lower-bound guard in `miyDrawSprites()`
// anyway (a real, if never-actually-triggered, second half of the same
// bounds check DrawSprites() already needs on this platform, matching this
// project's own standing "guard the crash even if unreachable in practice"
// discipline for this exact bug family), since Vircon32's non-truncating
// `int` can't rely on the same accidental wraparound-then-fail-the-upper-
// bound-check safety net a real AVR `byte` gets for free.
//
// **Two genuinely dead, always-true "throttle" checks, both sharing the
// same root cause, simplified away rather than ported literally**: two
// separate places in upstream (`Monster.cpp`'s own `MoveMonsters()`,
// `Fire.cpp`'s own `MoveFires()`) each gate a small piece of logic behind
// `if ((someLocalStaticClock & CoordMask) == 0)` - but
// `CoordMask = CoordRate-1 = (1<<CoordShift)-1 = 0` here (`CoordShift=0`,
// the same degenerate case Cracky's own `CRK_COORD_MASK` already
// documented), so `anything & 0 == 0` is unconditionally true regardless of
// the clock's own value - both of these local statics are confirmed, by
// direct inspection, to have NO other reader anywhere in their own function
// (write-only otherwise), making both genuinely dead weight rather than
// real gating logic. Dropped entirely; the guarded code now runs
// unconditionally every call, matching the real, always-true upstream
// outcome exactly. (`Man.cpp`'s own `MoveMan()` has a *third*, superficially
// similar-looking `(clock & CoordMask) == 0` check, but it sits inside a
// block that's entirely `//`-commented-out in the real upstream source
// itself - dead code on paper, not merely dead-at-runtime - so there was
// never anything there to port at all.)
// `Man.cpp`'s own fire-repeat cooldown counter
// (`keyCount`, ported as the persistent global `miyManKeyCount`) is the one
// local static that IS load-bearing - and, matching its real C++
// function-local-static lifetime (zero-initialized once at program start,
// never explicitly reset anywhere else in the whole program, not even
// between stages/retries), it is likewise never reset anywhere in this
// port either (relying on Vircon32's own zero-initialized globals to match
// that same "set once, at the very beginning" semantic).
//
// **A real, easy-to-miss dual-timing-rate mechanic, faithfully
// reproduced**: upstream's own play loop has exactly ONE genuine real-time
// throttle (`WaitTimer(8/CoordRate)`, inside the `Clock%4==0` branch that
// also runs `MoveMan()`/`DrawAll()`) - but `MoveFires()` is called
// completely UNCONDITIONALLY on every one of the 4 raw loop iterations per
// "big tick" (the `WaitTimer`-gated period), while `MoveMonsters()` only
// runs on every OTHER big tick (`Clock%8==0`, half as often as `MoveMan()`).
// Net effect: fire bullets travel visibly 4x faster than the player/
// monsters between rendered frames, since 3 of their 4 movement steps per
// big tick happen "invisibly" in between renders with no real-time pause
// between them. Reproduced in `miyUpdatePlaying()` by calling
// `miyMoveFires()` 4 times per gated tick (3 "leftover" calls from the
// tail of the *previous* big tick's own off-ticks, matching their real
// chronological position *before* that tick's own `MoveMan`/`MoveMonsters`,
// then 1 final call after) rather than approximating with a flat "fire is
// just faster" rate - preserves the *exact* interleaving upstream has
// between `MoveMonsters()` and the fire-bullet steps around it. The very
// first big tick after entering PLAYING correctly skips the "3 leftover"
// calls (there is no previous big tick to have left any over), matching
// `Clock`'s own real value of exactly 0 at that point.
//
// **`Sound.cpp` is the same 3-tone-channel tracker as Cracky's own copy,
// just with `Tempo=180` instead of 160 and this game's own melodies** -
// `SoundHandler()`'s own real tempo math re-derived the same way Cracky's
// header comment already explains: a channel advances once every
// `(600/2)/Tempo = 1.667` real 60Hz ticks, giving
// `miyNoteFrames(length) = round(length * 5.0/3.0)`. Reused the identical
// 3-slot sequencer shape (0=one-shot SFX - `Fire`/`Hit`/`Stole`/`Loose`/
// `Bonus` all share it, since none of them ever overlaps another in time;
// 1=jingle/BGM-voice-A; 2=BGM-voice-B), all data byte-diff-extracted via a
// small Python script rather than hand-copied. `Sound_Bonus()`'s own real
// `WaitMelody()` (block until the whole 6-note melody finishes, THEN wait
// 30 more real ticks) needed a genuine 2-phase wait state
// (`miyBonusPlayingSound` then `miyBonusExtraWait`) in `miyUpdateBonusTally()`
// rather than Cracky's own simpler fixed-frame-count approximation (which
// only needed to represent a single note) - dynamically polling
// `!miySeqPlaying(0)` matches `WaitMelody`'s real "wait until actually
// done" semantic exactly regardless of how the 6-note melody's own data
// might change in the future.
//
// **Two real, deliberately-preserved upstream oddities, not "fixed"**:
// (1) `Monster.cpp`'s own `DecideDirection()` has the exact same class of
// cross-axis comparison bug Cracky's own `crkDecideDirection` already found
// and documented (`crkMan.x < pMonster->y` there; here,
// `pMonster->targetX <= pMonster->y`, comparing an X value against a Y
// field) - almost certainly propagated from the same shared Cate-engine
// boilerplate this author (inufuto) reuses across both games. Kept exactly
// as written. (2) The Loose-monster "dying" animation timing genuinely
// varies by 1-4 ticks depending on which direction the monster was facing
// when hit (`status += 2` on a byte whose low 3 bits already only ever
// hold 0/2/4/6, checked against `(status&7)==0` - reaches that condition
// after 4/3/2/1 increments respectively) - reproduced with plain `int`
// arithmetic (no AVR-byte-overflow reliance needed at all here, since the
// counter never needs to exceed 8 before the monster is removed and its
// slot reset).
//
// **Data**: `AsciiPattern`/`Frequencies` reused verbatim (byte-diff
// confirmed identical to Cracky's own already-extracted copies of the same
// shared tables); `CharPattern` (118 glyphs, this game's own wall/fence/
// point/man/monster/fire/dot/food art), `TitleBytes` (the real dithered
// logo bitmap), all 8 stages' `start`/`enemyCount`/`enemies[4]`/`bytes[20]`,
// and all 9 melodies (`Loose`/`Fire`/`Hit`/`Stole`/`Bonus`/`Start`/`Clear`/
// `GameOver`/`Bgm1`/`Bgm2`) were all extracted via a small Python script and
// byte-diff-verified against the real upstream source before ever being
// pasted in, matching this project's own established anti-transcription-bug
// discipline.
//
// No forceRedraw needed - miyRender() unconditionally redraws every real
// hardware column/page every single call, in every state (title, playing,
// every wait/jingle/tally state), matching this project's own established
// "always redraw the full frame" precedent for this whole Cate-engine
// family (see Cracky's own header comment) - there is no dirty-flag/
// partial-redraw path anywhere in this file for a resume hook to need to
// force past.
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define MIY_CHAR_SPACE 0x00
#define MIY_CHAR_WALL 0x10
#define MIY_CHAR_FENCE 0x20
#define MIY_CHAR_POINT 0x21
#define MIY_CHAR_MAN 0x26
#define MIY_CHAR_MAN_LEFT0 0x26
#define MIY_CHAR_MAN_RIGHT0 0x2E
#define MIY_CHAR_MAN_UP0 0x36
#define MIY_CHAR_MAN_DOWN0 0x3E
#define MIY_CHAR_MONSTER 0x46
#define MIY_CHAR_FIREBULLET 0x66
#define MIY_CHAR_FIRE 0x6A
#define MIY_CHAR_DOT 0x6E
#define MIY_CHAR_FOOD 0x72

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define MIY_COORD_SHIFT 0
#define MIY_COORD_RATE ( 1 << MIY_COORD_SHIFT )
#define MIY_COORD_MASK ( MIY_COORD_RATE - 1 )

#define MIY_MOVABLE_LIVE 0x80
#define MIY_MOVABLE_LOOSE 0x20
#define MIY_MOVABLE_PATTERN 0x07

#define MIY_DIR_LEFT 0
#define MIY_DIR_RIGHT 1
#define MIY_DIR_UP 2
#define MIY_DIR_DOWN 3

#define MIY_HIT_RANGE ( MIY_COORD_RATE * 4 / 3 )

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
    int targetX, targetY;
    int time;
} MiyMovable;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define MIY_COLUMN_COUNT 8
#define MIY_ROW_COUNT 5
#define MIY_CELL_SIZE 3
#define MIY_COLUMN_WIDTH MIY_CELL_SIZE
#define MIY_ROW_HEIGHT MIY_CELL_SIZE
#define MIY_COLUMNS_PER_BYTE 2

#define MIY_CELL_RIGHTWALL 0x01
#define MIY_CELL_BOTTOMWALL 0x02
#define MIY_CELL_DOT 0x04
#define MIY_CELL_FOOD 0x08

#define MIY_WALL_LEFT 0x01
#define MIY_WALL_RIGHT 0x02
#define MIY_WALL_TOP 0x04
#define MIY_WALL_BOTTOM 0x08
#define MIY_WALL_ALL 0x0f

#define MIY_STAGE_COUNT 8
#define MIY_MAX_MONSTER_COUNT 4
#define MIY_CELLMAP_BYTES ( ( MIY_COLUMN_COUNT / MIY_COLUMNS_PER_BYTE ) * MIY_ROW_COUNT )

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define MIY_VVRAM_WIDTH 24
#define MIY_VVRAM_HEIGHT 16

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define MIY_SPRITE_MAN 0
#define MIY_SPRITE_MONSTER 1
#define MIY_SPRITE_FIRE 5
#define MIY_SPRITE_END 9
#define MIY_INVALID_PATTERN 255

typedef struct
{
    int x, y;
    int pattern;
} MiySprite;

// -----------------------------------------------------------------------------
//   Fire.h / Point.h
// -----------------------------------------------------------------------------

#define MIY_FIRE_MOVING 0x40
#define MIY_FIRE_POINTMASK 0x30
#define MIY_MAX_FIRE_COUNT ( MIY_SPRITE_END - MIY_SPRITE_FIRE )

#define MIY_MONSTER_VISIBLE 0x40

#define MIY_POINT_MAX_COUNT 4
#define MIY_POINT_MAX_TIME 6

typedef struct
{
    int x, y, c, time;
} MiyPoint;

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching Cracky's own precedent exactly.
// -----------------------------------------------------------------------------

#define MIY_N8 6
#define MIY_N4 ( MIY_N8 * 2 )
#define MIY_N4P ( MIY_N4 * 3 / 2 )
#define MIY_N2 ( MIY_N4 * 2 )
#define MIY_N2P ( MIY_N2 * 3 / 2 )
#define MIY_N1 ( MIY_N2 * 2 )

#define MIY_E2 1
#define MIY_F2 2
#define MIY_G2 4
#define MIY_A2 6
#define MIY_B2 8
#define MIY_C3 9
#define MIY_D3 11
#define MIY_E3 13
#define MIY_F3 14
#define MIY_G3 16
#define MIY_A3 18
#define MIY_B3 20
#define MIY_C4 21
#define MIY_C4S 22
#define MIY_D4 23
#define MIY_E4 25
#define MIY_F4 26
#define MIY_F4S 27
#define MIY_G4 28
#define MIY_G4S 29
#define MIY_A4 30
#define MIY_A4S 31
#define MIY_B4 32
#define MIY_C5 33
#define MIY_D5 35
#define MIY_E5 37
#define MIY_F5 38
#define MIY_G5 40

#define MIY_TEMPO 180

#define MIY_MELODY_NONE 0
#define MIY_MELODY_LOOSE 1
#define MIY_MELODY_FIRE 2
#define MIY_MELODY_HIT 3
#define MIY_MELODY_STOLE 4
#define MIY_MELODY_BONUS 5
#define MIY_MELODY_START 6
#define MIY_MELODY_CLEAR 7
#define MIY_MELODY_GAMEOVER 8
#define MIY_MELODY_BGM1 9
#define MIY_MELODY_BGM2 10

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph - byte-diff
// confirmed identical to Cracky's own copy of this same shared table.
int miyAsciiPattern[108] = {
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

// miyTitleBytes - upstream's own real title-screen logo bitmap
// (`Status.cpp`'s `Title()`), 5 letters/tiles x 4x4 VVram-cell glyph indices
// each (80 values total), byte-diff-verified against the real upstream
// source. Every value here is a valid index into miyCharPattern[]'s own
// "logo" range (indices 0-15, the first 32 bytes of that table - confirmed
// byte-identical to Cracky's own copy of the same shared palette) - the
// exact same dithered block-pattern palette used to build Cracky's own
// "CRACKY" wordmark, just reused here for this game's own 5-tile logo. See
// miyBeginTitle()'s own header comment for how this reaches the screen.
int miyTitleBytes[80] = {
    0x00, 0x0c, 0x0b, 0x08,
    0x00, 0x0c, 0x07, 0x07,
    0x00, 0x0c, 0x03, 0x00,
    0x00, 0x04, 0x01, 0x00,
    0x0f, 0x08, 0x02, 0x00,
    0x0f, 0x08, 0x02, 0x0e,
    0x0f, 0x0c, 0x03, 0x0f,
    0x05, 0x04, 0x01, 0x04,
    0x00, 0x00, 0x00, 0x00,
    0x0d, 0x02, 0x0f, 0x0c,
    0x05, 0x01, 0x04, 0x0d,
    0x05, 0x00, 0x05, 0x05,
    0x00, 0x00, 0x00, 0x00,
    0x03, 0x0e, 0x0d, 0x02,
    0x03, 0x0f, 0x05, 0x01,
    0x00, 0x04, 0x05, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0x0f, 0x0d, 0x02, 0x00,
    0x0f, 0x0c, 0x03, 0x00,
    0x05, 0x04, 0x01, 0x00,
};

// CharPattern - 118 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int miyCharPattern[236] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x0f, 0x00, 0x00, 0xf0, 0x0f, 0xf0,
    0x11, 0x11, 0x1e, 0x11, 0x11, 0xe1, 0x1e, 0xe1,
    0x88, 0x88, 0x87, 0x88, 0x88, 0x78, 0x87, 0x78,
    0x99, 0x99, 0x96, 0x99, 0x99, 0x69, 0x96, 0x69,
    0x05, 0x00, 0x00, 0x0f, 0xd9, 0x0a, 0x47, 0x0f,
    0xaf, 0x0f, 0x9f, 0x0f, 0x20, 0xe6, 0xf7, 0x45,
    0x80, 0x19, 0xa5, 0x00, 0x20, 0xe6, 0xf7, 0x45,
    0x20, 0x91, 0x29, 0x04, 0x54, 0x7f, 0x6e, 0x02,
    0x00, 0x5a, 0x91, 0x08, 0x54, 0x7f, 0x6e, 0x02,
    0x40, 0x92, 0x19, 0x02, 0x54, 0xff, 0x4e, 0x04,
    0x10, 0x0c, 0x34, 0x00, 0x54, 0xff, 0x4e, 0x04,
    0x30, 0x04, 0x1c, 0x00, 0xc4, 0xf6, 0xd7, 0x04,
    0x00, 0x1d, 0x25, 0x00, 0xc4, 0xf6, 0xd7, 0x04,
    0x20, 0x15, 0x0d, 0x00, 0xa8, 0xaf, 0xef, 0x08,
    0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e, 0xce, 0x00,
    0x32, 0xf7, 0xff, 0x02, 0x80, 0xfe, 0xfa, 0x8a,
    0x00, 0xfb, 0x37, 0x01, 0x00, 0xec, 0xe4, 0x04,
    0x20, 0xff, 0x7f, 0x23, 0xe8, 0xef, 0xef, 0x08,
    0x30, 0xf7, 0x37, 0x00, 0xc0, 0xce, 0xce, 0x00,
    0x71, 0xff, 0x7f, 0x01, 0x80, 0xbe, 0xbe, 0x8e,
    0x00, 0x73, 0x7f, 0x03, 0x00, 0x6c, 0x6c, 0x0c,
    0x10, 0xf7, 0xff, 0x17, 0x00, 0x48, 0x4c, 0x00,
    0x30, 0x54, 0x12, 0x00, 0x00, 0x68, 0x08, 0x00,
    0x70, 0xa8, 0x78, 0x00, 0x00, 0x80, 0x08, 0x00,
    0x00, 0x10, 0x01, 0x00, 0x64, 0x75, 0x73, 0x46,
    0xd4, 0xdd, 0xdd, 0x4d,
};

// Standard equal-tempered note frequencies, E2..G5 - byte-diff confirmed
// identical to Cracky's own copy of this same shared table.
int miyFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int miyPointValues[4] = { 10, 20, 40, 80 };

int miyMelodyLoose[3] = { 1, MIY_A3, 0 };
int miyMelodyFire[9] = { 1, MIY_C5, 1, MIY_A4S, 1, MIY_G4S, 1, MIY_F4S, 0 };
int miyMelodyHit[17] = {
    1, MIY_F4, 1, MIY_G4, 1, MIY_A4, 1, MIY_B4, 1, MIY_C5,
    1, MIY_D5, 1, MIY_E5, 1, MIY_F5, 0,
};
int miyMelodyStole[17] = {
    1, MIY_F5, 1, MIY_E5, 1, MIY_D5, 1, MIY_C5, 1, MIY_B4,
    1, MIY_A4, 1, MIY_G4, 1, MIY_F4, 0,
};
int miyMelodyBonus[13] = {
    1, MIY_C4, 1, MIY_C4S, 1, MIY_D4, 1, MIY_F4, 1, MIY_A4,
    1, MIY_C5, 0,
};
int miyMelodyStart[23] = {
    MIY_N4, MIY_C4, MIY_N4, MIY_E4, MIY_N8, MIY_G4, MIY_N4, MIY_E4, MIY_N4, MIY_F4,
    MIY_N8, MIY_F4, MIY_N4, MIY_A4, MIY_N8, MIY_C5, MIY_N4P, MIY_A4, MIY_N2P, MIY_C5,
    MIY_N4, 0, 0,
};
int miyMelodyClear[29] = {
    MIY_N8, MIY_A4, MIY_N8, MIY_A4, MIY_N8, MIY_G4, MIY_N8, MIY_F4, MIY_N8, MIY_G4,
    MIY_N4, MIY_A4, MIY_N4, MIY_B4, MIY_N8, MIY_B4, MIY_N8, MIY_A4, MIY_N8, MIY_G4,
    MIY_N8, MIY_A4, MIY_N4, MIY_B4, MIY_N8 + MIY_N2, MIY_C5, MIY_N2, 0, 0,
};
int miyMelodyGameOver[21] = {
    MIY_N8, MIY_C5, MIY_N8, MIY_F4, MIY_N8, MIY_A4, MIY_N8, MIY_E4, MIY_N8, MIY_G4,
    MIY_N8, MIY_A4, MIY_N8, MIY_B4, MIY_N8, MIY_C5, MIY_N2P, MIY_C5, MIY_N4, 0,
    0,
};
int miyMelodyBgm1[119] = {
    MIY_N4, MIY_C4, MIY_N4, MIY_G4, MIY_N8, MIY_C4, MIY_N4, MIY_G4, MIY_N4, MIY_A4,
    MIY_N8, MIY_A4, MIY_N8, MIY_G4, MIY_N8, MIY_G4, MIY_N8, MIY_F4, MIY_N8, MIY_F4,
    MIY_N8, MIY_E4, MIY_N8, MIY_E4, MIY_N4, MIY_D4, MIY_N4, MIY_D4, MIY_N8, MIY_D4,
    MIY_N4, MIY_E4, MIY_N4P, MIY_D4, MIY_N2P, 0, MIY_N4, MIY_C4, MIY_N4, MIY_G4,
    MIY_N8, MIY_C4, MIY_N4, MIY_G4, MIY_N4, MIY_A4, MIY_N8, MIY_A4, MIY_N8, MIY_G4,
    MIY_N8, MIY_G4, MIY_N8, MIY_F4, MIY_N8, MIY_F4, MIY_N8, MIY_E4, MIY_N8, MIY_E4,
    MIY_N4, MIY_F4, MIY_N4, MIY_F4, MIY_N8, MIY_F4, MIY_N4, MIY_A4, MIY_N4P, MIY_G4,
    MIY_N2P, 0, MIY_N8, MIY_E4, MIY_N8, MIY_E4, MIY_N8, MIY_E4, MIY_N4, MIY_E4,
    MIY_N8, MIY_E4, MIY_N4, MIY_A4, MIY_N8, MIY_D4, MIY_N8, MIY_D4, MIY_N8, MIY_D4,
    MIY_N4, MIY_D4, MIY_N8, MIY_D4, MIY_N4, MIY_G4, MIY_N8, 0, MIY_N8, MIY_A4,
    MIY_N8, 0, MIY_N8, MIY_G4, MIY_N8, 0, MIY_N8, MIY_F4, MIY_N8, 0,
    MIY_N8, MIY_E4, MIY_N4, MIY_D4, MIY_N4, MIY_E4, MIY_N2, MIY_C4, 255,
};
int miyMelodyBgm2[133] = {
    MIY_N8, MIY_C4, MIY_N4, 0, MIY_N4P, MIY_E4, MIY_N8, MIY_G4, MIY_N8, 0,
    MIY_N8, MIY_A3, MIY_N4, 0, MIY_N4P, MIY_C4, MIY_N8, MIY_E4, MIY_N8, 0,
    MIY_N8, MIY_D4, MIY_N4, 0, MIY_N4P, MIY_F4, MIY_N8, MIY_A3, MIY_N8, 0,
    MIY_N8, MIY_G3, MIY_N4, 0, MIY_N4P, MIY_B3, MIY_N8, MIY_D4, MIY_N8, 0,
    MIY_N8, MIY_C4, MIY_N4, 0, MIY_N4P, MIY_E4, MIY_N8, MIY_G4, MIY_N8, 0,
    MIY_N8, MIY_A3, MIY_N4, 0, MIY_N4P, MIY_C4, MIY_N8, MIY_E4, MIY_N8, 0,
    MIY_N8, MIY_F4, MIY_N4, 0, MIY_N8, MIY_F4, MIY_N8, MIY_G3, MIY_N4, 0,
    MIY_N8, MIY_G3, MIY_N8, MIY_C4, MIY_N4, 0, MIY_N4P, MIY_E4, MIY_N8, MIY_G4,
    MIY_N8, 0, MIY_N8, MIY_C4, MIY_N4, 0, MIY_N8, MIY_C4, MIY_N8, MIY_A3,
    MIY_N4, 0, MIY_N8, MIY_A3, MIY_N8, MIY_D4, MIY_N4, 0, MIY_N8, MIY_D4,
    MIY_N8, MIY_G3, MIY_N4, 0, MIY_N8, MIY_G3, MIY_N8, 0, MIY_N8, MIY_F3,
    MIY_N8, 0, MIY_N8, MIY_F3, MIY_N8, 0, MIY_N8, MIY_G3, MIY_N8, 0,
    MIY_N8, MIY_G3, MIY_N8, MIY_C4, MIY_N4, 0, MIY_N4P, MIY_E4, MIY_N8, MIY_G4,
    MIY_N8, 0, 255,
};

// Stage data - flattened from upstream's own `struct Stage { start,
// enemyCount, pEnemies, bytes[20] }` + separate Enemies0-7 arrays into
// parallel fixed arrays, matching Cracky's own precedent for the same
// upstream `struct-with-a-real-pointer-member` shape. Enemy lists padded
// with 0 out to MIY_MAX_MONSTER_COUNT(4).
int miyStageStart[8] = { 0x32, 0x00, 0x40, 0x14, 0x44, 0x42, 0x41, 0x32 };
int miyStageEnemyCount[8] = { 2, 2, 2, 3, 3, 3, 4, 4 };
int miyStageEnemies[8][4] = {
    { 0x00, 0x70, 0x00, 0x00 },
    { 0x70, 0x04, 0x00, 0x00 },
    { 0x50, 0x04, 0x00, 0x00 },
    { 0x00, 0x52, 0x54, 0x00 },
    { 0x40, 0x62, 0x04, 0x00 },
    { 0x00, 0x70, 0x04, 0x00 },
    { 0x00, 0x70, 0x04, 0x74 },
    { 0x00, 0x30, 0x51, 0x04 },
};
int miyStageBytes[8][20] = {
    {
        0x51, 0x45, 0x76, 0x15, 0x24, 0x45, 0x66, 0x56, 0x67, 0x66,
        0x6a, 0x56, 0x6a, 0x66, 0x66, 0x56, 0x66, 0x66, 0x66, 0x7a,
    },
    {
        0x95, 0x45, 0x67, 0x34, 0x64, 0x66, 0x06, 0x52, 0x66, 0x66,
        0x68, 0x56, 0x64, 0x66, 0x24, 0x12, 0x62, 0x26, 0x36, 0x3a,
    },
    {
        0x59, 0xa5, 0x25, 0x56, 0x64, 0x64, 0xa6, 0x76, 0x45, 0x66,
        0x26, 0x5b, 0x02, 0x26, 0x62, 0x16, 0x22, 0x66, 0x62, 0x76,
    },
    {
        0x41, 0x65, 0x45, 0xb6, 0x76, 0x66, 0x46, 0x52, 0x68, 0x66,
        0x26, 0x56, 0x05, 0xa2, 0x66, 0x56, 0x6a, 0x22, 0x26, 0x36,
    },
    {
        0xa5, 0x55, 0x51, 0x55, 0x66, 0x44, 0x46, 0x76, 0x2a, 0x22,
        0x66, 0x52, 0x0a, 0x62, 0x66, 0x56, 0x22, 0x22, 0x26, 0xb2,
    },
    {
        0xa1, 0x64, 0x89, 0x15, 0x28, 0x80, 0xb6, 0x30, 0x68, 0x66,
        0x66, 0x56, 0x2a, 0x2a, 0x22, 0x12, 0x22, 0x22, 0x22, 0x32,
    },
    {
        0x40, 0x44, 0x48, 0x14, 0x44, 0x44, 0x44, 0x54, 0x48, 0x84,
        0x48, 0x94, 0x44, 0x44, 0x44, 0x54, 0x62, 0x66, 0x6a, 0x36,
    },
    {
        0x22, 0x19, 0x20, 0xb2, 0xa0, 0x2a, 0x21, 0x10, 0x28, 0x68,
        0x02, 0x12, 0x0a, 0x2a, 0x22, 0x12, 0x22, 0x22, 0x22, 0xb2,
    },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int miyScore;
int miyHiScore;
int miyRemainCount;
int miyCurrentStage;
int miyFoodCount;
int miyStageIndex;
int miyFireTime;

int miyDirDx[4] = { -1, 1, 0, 0 };
int miyDirDy[4] = { 0, 0, -1, 1 };
int miyDirPattern[4] = { MIY_DIR_LEFT * 2, MIY_DIR_RIGHT * 2, MIY_DIR_UP * 2, MIY_DIR_DOWN * 2 };

int miyVVramBack[MIY_VVRAM_WIDTH * MIY_VVRAM_HEIGHT];
int miyVVramFront[MIY_VVRAM_WIDTH * MIY_VVRAM_HEIGHT];
int miyCellMap[MIY_CELLMAP_BYTES];

MiySprite miySprites[MIY_SPRITE_END];

MiyMovable miyMan;
int miyManDirection;
int miyManKeyCount;

int miyMonsterCount;
MiyMovable miyMonsters[MIY_MAX_MONSTER_COUNT];

MiyMovable miyFires[MIY_MAX_FIRE_COUNT];

MiyPoint miyPoints[MIY_POINT_MAX_COUNT];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into miyAsciiPattern (0 = space) per cell.
//
// This width matches upstream's own real Vram address space - the status
// labels (SCORE/STAGE/FOOD/remain) occupy only columns 24-31 (upstream's
// own `LeftX=24` constant), but the title screen's own text ("INUFUTO
// 2026", "START"/"CONTINUE") lives at upstream's real columns 8-23, well
// to the LEFT of the status zone, using the exact same shared PrintC()/
// PrintS() mechanism at different column arguments - not a separate,
// narrower grid at all. See `miyComposeRawByte()`'s own header for how this
// wide grid actually reaches the screen.
int miyStatusChar[8][32];

// Set true only while on the title screen (MIY_STATE_TITLE) - upstream's
// real Title() writes its own real logo bitmap into VVramFront once, then
// drives most of the rest of the screen through the same PrintC()/PrintS()
// text mechanism, at real columns spanning the whole 0-31 char-cell range
// (MINI/START/CONTINUE/the credit line all live at columns 8-23, well
// inside what during gameplay is the map area). When true,
// miyComposeRawByte() reads miyStatusChar across the full width instead of
// just columns 24-31, letting the title screen use that same wide real
// estate instead of being artificially confined to the narrow status-only
// zone. This does NOT disable the VVram/map read (see
// miyComposeRawByte()'s own header) - the two are OR-combined, since the
// logo bitmap and every status-text element occupy disjoint real hardware
// pages.
bool miyFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintGameOver() Vram-direct write - see header.
bool miyOverlayActive;
int miyOverlayText[10];
int miyOverlayLen;
int miyOverlayPage;
int miyOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of MIY_TICK_DIVISOR.
int miySeqMelody[3];
int miySeqPos[3];
int miySeqWait[3];
int miySeqActive[3];

#define MIY_TICK_DIVISOR 8
int miyTickCounter;
bool miyFirstBigTick;
int miyMonsterMoveTurn;

int miyBonusExtraWait;
bool miyBonusPlayingSound;

#define MIY_STATE_TITLE 0
#define MIY_STATE_START_JINGLE 1
#define MIY_STATE_PLAYING 2
#define MIY_STATE_PRE_LOSE_WAIT 3
#define MIY_STATE_LOSE_ANIM 4
#define MIY_STATE_GAMEOVER_JINGLE 5
#define MIY_STATE_CLEAR_WAIT 6
#define MIY_STATE_CLEAR_JINGLE 7
#define MIY_STATE_BONUS_TALLY 8
int miyState;
int miyWaitFrames;
int miyAnimStep;
int miySelection;
bool miySelectionChanged;
int miyPrevLeft;
int miyPrevRight;
int miyPrevUp;
int miyPrevDown;
int miyPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int miyAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   VVram.cpp / Vram.cpp core helpers - operate on an explicit buffer
//   pointer since some writes target the persistent "Back" map layer and
//   some target the per-frame "Front" composite, matching upstream exactly.
// -----------------------------------------------------------------------------

int miyVVramIdx( int x, int y )
{
    return y * MIY_VVRAM_WIDTH + x;
}

int miyVPut( int* buf, int idx, int c )
{
    buf[ idx ] = c;
    return idx + 1;
}

int miyVPut2C( int* buf, int idx, int c )
{
    int i, j, cur, startIdx;
    startIdx = idx;
    cur = idx;
    for( i = 0; i < 2; i = i + 1 )
    {
        for( j = 0; j < 2; j = j + 1 )
        {
            cur = miyVPut( buf, cur, c );
            c = c + 1;
        }
        cur = cur + MIY_VVRAM_WIDTH - 2;
    }
    return startIdx + 2;
}

int miyVErase2( int* buf, int idx )
{
    int i, j, cur, startIdx;
    startIdx = idx;
    cur = idx;
    for( i = 0; i < 2; i = i + 1 )
    {
        for( j = 0; j < 2; j = j + 1 )
          cur = miyVPut( buf, cur, MIY_CHAR_SPACE );
        cur = cur + MIY_VVRAM_WIDTH - 2;
    }
    return startIdx + 2;
}

void miyClearVVramBuffers()
{
    int idx;
    for( idx = 0; idx < MIY_VVRAM_WIDTH * MIY_VVRAM_HEIGHT; idx = idx + 1 )
    {
        miyVVramBack[ idx ] = MIY_CHAR_SPACE;
        miyVVramFront[ idx ] = MIY_CHAR_SPACE;
    }
}

void miyClearStatusChar()
{
    int i, j;
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          miyStatusChar[ i ][ j ] = 0;
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - grid helpers
// -----------------------------------------------------------------------------

int miyCoordMod( int a )
{
    int mods[32] = {
        2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2,
        0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0,
    };
    return mods[ a >> MIY_COORD_SHIFT ];
}

int miyToGrid( int a )
{
    int grids[32] = {
        -1, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4,
        5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
    };
    return grids[ a >> MIY_COORD_SHIFT ];
}

int miyToCoord( int a )
{
    return 1 + ( a << 1 ) + a;
}

int miyCellMapPtr( int column, int row )
{
    return ( row * MIY_COLUMN_COUNT + column ) / MIY_COLUMNS_PER_BYTE;
}

int miyGetCell( int column, int row )
{
    int b;
    b = miyCellMap[ miyCellMapPtr( column, row ) ];
    if( ( column & 1 ) != 0 )
      return b >> 4;
    return b & 0x0f;
}

void miySetCell( int column, int row, int cell )
{
    int idx, idx2;
    idx = miyCellMapPtr( column, row );
    if( ( column & 1 ) != 0 )
      miyCellMap[ idx ] = ( miyCellMap[ idx ] & 0x0f ) | ( cell << 4 );
    else
      miyCellMap[ idx ] = ( miyCellMap[ idx ] & 0xf0 ) | ( cell & 0x0f );

    idx2 = miyVVramIdx( miyToCoord( column ) - 1, miyToCoord( row ) );
    if( ( cell & MIY_CELL_DOT ) != 0 )
      miyVPut2C( miyVVramBack, idx2, MIY_CHAR_DOT );
    else if( ( cell & MIY_CELL_FOOD ) != 0 )
      miyVPut2C( miyVVramBack, idx2, MIY_CHAR_FOOD );
    else
      miyVErase2( miyVVramBack, idx2 );
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void miyHideAllSprites()
{
    int i;
    for( i = 0; i < MIY_SPRITE_END; i = i + 1 )
      miySprites[ i ].pattern = MIY_INVALID_PATTERN;
}

void miyShowSprite( MiyMovable* pMovable, int pattern )
{
    miySprites[ pMovable->sprite ].x = pMovable->x - 1;
    miySprites[ pMovable->sprite ].y = pMovable->y;
    miySprites[ pMovable->sprite ].pattern = pattern;
}

void miyHideSprite( int index )
{
    miySprites[ index ].pattern = MIY_INVALID_PATTERN;
}

void miyDrawSprites()
{
    int s;
    for( s = 0; s < MIY_SPRITE_END; s = s + 1 )
    {
        if( miySprites[ s ].pattern != MIY_INVALID_PATTERN )
        {
            int x, y, c, row;
            x = miySprites[ s ].x;
            y = miySprites[ s ].y;
            c = miySprites[ s ].pattern;
            for( row = 0; row < 2; row = row + 1 )
            {
                if( y < MIY_VVRAM_HEIGHT )
                {
                    int col, cx;
                    cx = x;
                    for( col = 0; col < 2; col = col + 1 )
                    {
                        if( cx >= 0 && cx < MIY_VVRAM_WIDTH )
                          miyVVramFront[ miyVVramIdx( cx, y ) ] = c;
                        c = c + 1;
                        cx = cx + 1;
                    }
                }
                else
                  c = c + 2;
                y = y + 1;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void miyLocateMovable( MiyMovable* pMovable, int b )
{
    int column, row;
    column = b >> 4;
    row = b & 0x0f;
    pMovable->x = miyToCoord( column ) << MIY_COORD_SHIFT;
    pMovable->y = miyToCoord( row ) << MIY_COORD_SHIFT;
}

void miyShowMovable( MiyMovable* pMovable, int pattern )
{
    int seq;
    seq = ( ( pMovable->x + pMovable->y ) >> MIY_COORD_SHIFT ) & 1;
    seq = seq + ( pMovable->status & MIY_MOVABLE_PATTERN );
    pattern = pattern + ( seq << 2 );
    miyShowSprite( pMovable, pattern );
}

void miySetDirection( MiyMovable* pMovable, int dirIndex )
{
    pMovable->dx = miyDirDx[ dirIndex ];
    pMovable->dy = miyDirDy[ dirIndex ];
    pMovable->status = ( pMovable->status & ~MIY_MOVABLE_PATTERN ) | miyDirPattern[ dirIndex ];
}

bool miyCanMove( MiyMovable* pMovable, int dx, int dy )
{
    int x, y;
    x = pMovable->x;
    y = pMovable->y;
    if( dy == 0 )
    {
        int yMod, xMod;
        yMod = miyCoordMod( y );
        if( yMod != 0 ) return false;
        xMod = miyCoordMod( x );
        if( dx < 0 )
        {
            if( xMod == 0 )
            {
                int column, row;
                column = miyToGrid( x );
                if( column == 0 ) return false;
                row = miyToGrid( y );
                if( ( miyGetCell( column - 1, row ) & MIY_CELL_RIGHTWALL ) != 0 ) return false;
            }
            return true;
        }
        else
        {
            if( xMod == 0 )
            {
                int column, row;
                column = miyToGrid( x );
                if( column >= MIY_COLUMN_COUNT - 1 ) return false;
                row = miyToGrid( y );
                if( ( miyGetCell( column, row ) & MIY_CELL_RIGHTWALL ) != 0 ) return false;
            }
            return true;
        }
    }
    else if( dx == 0 )
    {
        int xMod, yMod;
        xMod = miyCoordMod( x );
        if( xMod != 0 ) return false;
        yMod = miyCoordMod( y );
        if( dy < 0 )
        {
            if( yMod == 0 )
            {
                int row, column;
                row = miyToGrid( y );
                if( row == 0 ) return false;
                column = miyToGrid( x );
                if( ( miyGetCell( column, row - 1 ) & MIY_CELL_BOTTOMWALL ) != 0 ) return false;
            }
            return true;
        }
        else
        {
            if( yMod == 0 )
            {
                int row, column;
                row = miyToGrid( y );
                if( row >= MIY_ROW_COUNT - 1 ) return false;
                column = miyToGrid( x );
                if( ( miyGetCell( column, row ) & MIY_CELL_BOTTOMWALL ) != 0 ) return false;
            }
            return true;
        }
    }
    return false;
}

void miyMoveMovable( MiyMovable* pMovable )
{
    pMovable->x = pMovable->x + pMovable->dx;
    pMovable->y = pMovable->y + pMovable->dy;
}

bool miyIsOnGrid( MiyMovable* pMovable )
{
    if( ( ( pMovable->x | pMovable->y ) & MIY_COORD_MASK ) != 0 ) return false;
    if( miyCoordMod( pMovable->x ) != 0 ) return false;
    if( miyCoordMod( pMovable->y ) != 0 ) return false;
    return true;
}

bool miyIsNear( MiyMovable* p1, MiyMovable* p2 )
{
    return
        p1->x + MIY_HIT_RANGE >= p2->x && p2->x + MIY_HIT_RANGE >= p1->x &&
        p1->y + MIY_HIT_RANGE >= p2->y && p2->y + MIY_HIT_RANGE >= p1->y;
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp
// -----------------------------------------------------------------------------

int miyAsciiIndex( int c )
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

int miyPrintC( int page, int col, int c )
{
    miyStatusChar[ page ][ col ] = miyAsciiIndex( c );
    return col + 1;
}

int miyPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = miyPrintC( page, col, s[ i ] );
    return col;
}

int miyPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      miyPrintC( page, col, ' ' );
    else
      miyPrintC( page, col, d1 + '0' );
    miyPrintC( page, col + 1, ( b % 10 ) + '0' );
    return col + 2;
}

void miyPrintNumber5( int page, int col, int w )
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
          miyPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            miyPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    miyPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+6 etc), not an arbitrary local 0-7 offset.
void miyPrintScore()
{
    miyPrintNumber5( 1, 26, miyScore );
    miyPrintC( 1, 31, '0' );
}

void miyPrintFoodCount()
{
    miyPrintByteNumber2( 5, 26, miyFoodCount );
}

void miyPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };

    miyPrintS( 0, 24, sScore, 5 );
    miyPrintS( 3, 24, sStage, 5 );
    miyPrintByteNumber2( 3, 30, miyCurrentStage + 1 );

    // upstream also draws a real Char_Food icon here (Put2C) - can't be
    // reproduced through the ASCII-only status-text grid this port's
    // status area supports (the exact same limitation Cracky's own lives-
    // icon simplification already hit) - dropped; miyPrintFoodCount()'s
    // own digits are the only remaining representation of the food count.

    // Upstream's own remain-lives display (`Char_Remain = Char_Man`, drawn
    // via `Put2C` - a real 2x2-cell icon, `RemainCount-1` times, i.e. one
    // little-man icon per life still in reserve) can't be reproduced
    // through this port's ASCII-only status grid either - same limitation
    // as the food icon above, and the same one Cracky's own identical
    // `PrintStatus()` translation already hit. Since miyRemainCount only
    // ever holds 1-3 in real play (reset to 3 on a new game, decremented on
    // death), only the `else` branch below ever actually fires - the `i>2`
    // branch is kept anyway, matching Cracky's own structure and upstream's
    // own real dead-in-practice fallback, rather than assuming it can never
    // matter.
    if( miyRemainCount > 1 )
    {
        int i;
        i = miyRemainCount - 1;
        if( i > 2 )
        {
            miyPrintC( 7, 24, ' ' );
            miyPrintC( 7, 25, ' ' );
            miyPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < miyRemainCount - 1; i = i + 1 )
              miyPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    miyPrintScore();
    miyPrintFoodCount();
}

void miyBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    miyOverlayActive = true;
    miyOverlayLen = len;
    miyOverlayPage = page;
    miyOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      miyOverlayText[ i ] = s[ i ];
}

void miyPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    miyBeginOverlay( s, 9, 4, 8 );
}

void miyAddScore( int pts )
{
    miyScore = miyScore + pts;
    if( miyScore > miyHiScore )
      miyHiScore = miyScore;
    miyPrintScore();
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int miyMelodyLength( int id )
{
    if( id == MIY_MELODY_LOOSE ) return 3;
    if( id == MIY_MELODY_FIRE ) return 9;
    if( id == MIY_MELODY_HIT ) return 17;
    if( id == MIY_MELODY_STOLE ) return 17;
    if( id == MIY_MELODY_BONUS ) return 13;
    if( id == MIY_MELODY_START ) return 23;
    if( id == MIY_MELODY_CLEAR ) return 29;
    if( id == MIY_MELODY_GAMEOVER ) return 21;
    if( id == MIY_MELODY_BGM1 ) return 119;
    if( id == MIY_MELODY_BGM2 ) return 133;
    return 0;
}

int miyMelodyValue( int id, int idx )
{
    if( id == MIY_MELODY_LOOSE ) return miyMelodyLoose[ idx ];
    if( id == MIY_MELODY_FIRE ) return miyMelodyFire[ idx ];
    if( id == MIY_MELODY_HIT ) return miyMelodyHit[ idx ];
    if( id == MIY_MELODY_STOLE ) return miyMelodyStole[ idx ];
    if( id == MIY_MELODY_BONUS ) return miyMelodyBonus[ idx ];
    if( id == MIY_MELODY_START ) return miyMelodyStart[ idx ];
    if( id == MIY_MELODY_CLEAR ) return miyMelodyClear[ idx ];
    if( id == MIY_MELODY_GAMEOVER ) return miyMelodyGameOver[ idx ];
    if( id == MIY_MELODY_BGM1 ) return miyMelodyBgm1[ idx ];
    if( id == MIY_MELODY_BGM2 ) return miyMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/MIY_TEMPO = 1.6667 real 60Hz ticks - see header comment.
int miyNoteFrames( int length )
{
    return (int)( (float)length * 5.0 / 3.0 + 0.5 );
}

void miyStartSeq( int channel, int melodyId )
{
    miySeqMelody[ channel ] = melodyId;
    miySeqPos[ channel ] = 0;
    miySeqWait[ channel ] = 0;
    miySeqActive[ channel ] = 1;
}

void miyStopSeq( int channel )
{
    miySeqActive[ channel ] = 0;
    miySeqMelody[ channel ] = MIY_MELODY_NONE;
}

bool miySeqPlaying( int channel )
{
    return miySeqActive[ channel ] != 0;
}

void miyAdvanceOneSeq( int channel )
{
    int length, note;

    if( miySeqActive[ channel ] == 0 ) return;

    if( miySeqWait[ channel ] > 0 )
    {
        miySeqWait[ channel ] = miySeqWait[ channel ] - 1;
        return;
    }

    length = miyMelodyValue( miySeqMelody[ channel ], miySeqPos[ channel ] );
    if( length == 0 )
    {
        miyStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        miySeqPos[ channel ] = 0;
        length = miyMelodyValue( miySeqMelody[ channel ], 0 );
    }
    note = miyMelodyValue( miySeqMelody[ channel ], miySeqPos[ channel ] + 1 );
    miySeqPos[ channel ] = miySeqPos[ channel ] + 2;
    miySeqWait[ channel ] = miyNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)miyFrequencies[ note - 1 ], (float)miySeqWait[ channel ] / 60.0 );
}

void miyAdvanceSound()
{
    miyAdvanceOneSeq( 0 );
    miyAdvanceOneSeq( 1 );
    miyAdvanceOneSeq( 2 );
}

void miyStartBgm()
{
    miyStartSeq( 1, MIY_MELODY_BGM1 );
    miyStartSeq( 2, MIY_MELODY_BGM2 );
}

void miyStopBgm()
{
    miyStopSeq( 1 );
    miyStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void miyInitPoints()
{
    int i;
    for( i = 0; i < MIY_POINT_MAX_COUNT; i = i + 1 )
      miyPoints[ i ].time = 0;
}

void miyStartPoint( int x, int y, int rate )
{
    int i;
    miyAddScore( miyPointValues[ rate ] );
    // upstream's own DrawPoints() has NO bounds checking at all (unlike
    // DrawSprites()) and x here can legitimately reach 24 (the far edge of
    // the last grid column) - a 3-cell-wide popup starting there would
    // write past the real 24-wide VVram row. Clamped here, at the source,
    // rather than guessed at in DrawPoints() itself - see header comment.
    if( x > MIY_VVRAM_WIDTH - 3 )
      x = MIY_VVRAM_WIDTH - 3;
    for( i = 0; i < MIY_POINT_MAX_COUNT; i = i + 1 )
    {
        if( miyPoints[ i ].time == 0 )
        {
            miyPoints[ i ].time = MIY_POINT_MAX_TIME << MIY_COORD_SHIFT;
            miyPoints[ i ].x = x;
            miyPoints[ i ].y = y - 1;
            miyPoints[ i ].c = MIY_CHAR_POINT + rate;
            return;
        }
    }
}

void miyUpdatePoints()
{
    int i;
    for( i = 0; i < MIY_POINT_MAX_COUNT; i = i + 1 )
    {
        if( miyPoints[ i ].time != 0 )
          miyPoints[ i ].time = miyPoints[ i ].time - 1;
    }
}

void miyDrawPoints()
{
    int i;
    for( i = 0; i < MIY_POINT_MAX_COUNT; i = i + 1 )
    {
        if( miyPoints[ i ].time != 0 )
        {
            int idx;
            idx = miyVVramIdx( miyPoints[ i ].x, miyPoints[ i ].y );
            idx = miyVPut( miyVVramFront, idx, miyPoints[ i ].c );
            idx = miyVPut( miyVVramFront, idx, MIY_CHAR_POINT + 4 );
            miyVPut( miyVVramFront, idx, MIY_CHAR_POINT + 4 );
        }
    }
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void miyDrawAll()
{
    int idx;
    for( idx = 0; idx < MIY_VVRAM_WIDTH * MIY_VVRAM_HEIGHT; idx = idx + 1 )
      miyVVramFront[ idx ] = miyVVramBack[ idx ];
    miyDrawSprites();
    miyDrawPoints();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly -
// same derivation as Cracky's own crkComposeRawByte(). rawCol/rawPage are
// in upstream's own (unmirrored) GDDRAM coordinate space - no hardware
// remap is applied anywhere, see header.
//
// OR-combines mapByte with textByte instead of choosing one exclusively -
// safe because the two occupy disjoint real hardware pages by construction:
// the logo bitmap only ever occupies pages 1-2 (miyBeginTitle() writes it
// at VVram rows 2-5), while every piece of title-screen status text
// (SCORE/STAGE/MINI/START/CONTINUE/credit) lives on pages 0/3/5/6/7 - and
// during normal gameplay, miyStatusChar is never written at charCol < 24 at
// all (miyPrintStatus()'s own real columns start at upstream's `LeftX=24`),
// so textByte for the map area is always 0 there, making
// mapByte|textByte == mapByte outside the title screen exactly as before.
int miyComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < MIY_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = miyVVramFront[ miyVVramIdx( mapX, rawPage * 2 ) ];
        lower = miyVVramFront[ miyVVramIdx( mapX, rawPage * 2 + 1 ) ];
        if( sub == 0 )
        {
            upperByte = miyCharPattern[ upper * 2 + 0 ];
            lowerByte = miyCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = miyCharPattern[ upper * 2 + 0 ];
            lowerByte = miyCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = miyCharPattern[ upper * 2 + 1 ];
            lowerByte = miyCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = miyCharPattern[ upper * 2 + 1 ];
            lowerByte = miyCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !miyFullWidthText && rawCol < MIY_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // miyStatusChar's own full-width indexing directly - no more "subtract
    // the map width" local-offset math needed, since rawCol/4 already lands
    // on the correct real column either way (whether this is the
    // miyFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = miyStatusChar[ rawPage ][ charCol ];
            textByte = miyAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void miyRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( miyOverlayActive && page == miyOverlayPage &&
                col >= miyOverlayCol * 4 && col < miyOverlayCol * 4 + miyOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - miyOverlayCol * 4 ) / 4;
                sub = ( col - miyOverlayCol * 4 ) % 4;
                value = miyAsciiPattern[ miyAsciiIndex( miyOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = miyComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void miyUpdateMonsterTargets()
{
    int cellIdx, cell, column, row, i;

    if( miyFoodCount == 0 ) return;

    cellIdx = 0;
    cell = miyCellMap[ cellIdx ];
    column = 0;
    row = 0;

    for( i = 0; i < MIY_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = miyMonsters[ i ].status;
        if( ( status & MIY_MOVABLE_LIVE ) != 0 && ( status & MIY_MOVABLE_LOOSE ) == 0 )
        {
            bool done;
            done = false;
            while( !done )
            {
                if( ( cell & MIY_CELL_FOOD ) != 0 )
                {
                    miyMonsters[ i ].targetX = miyToCoord( column ) << MIY_COORD_SHIFT;
                    miyMonsters[ i ].targetY = miyToCoord( row ) << MIY_COORD_SHIFT;
                    done = true;
                }
                column = column + 1;
                if( column >= MIY_COLUMN_COUNT )
                {
                    column = 0;
                    cellIdx = cellIdx + 1;
                    row = row + 1;
                    if( row >= MIY_ROW_COUNT )
                    {
                        row = 0;
                        cellIdx = 0;
                    }
                    cell = miyCellMap[ cellIdx ];
                }
                else if( ( column & 1 ) == 0 )
                {
                    cellIdx = cellIdx + 1;
                    cell = miyCellMap[ cellIdx ];
                }
                else
                  cell = cell >> 4;
            }
        }
    }
}

void miyMonsterShow( MiyMovable* pMonster )
{
    if( ( pMonster->status & MIY_MONSTER_VISIBLE ) != 0 )
      miyShowMovable( pMonster, MIY_CHAR_MONSTER );
    else
      miyHideSprite( pMonster->sprite );
}

void miyDecideDirection( MiyMovable* pMonster )
{
    int dirs[4];
    int p1idx, p2idx;
    int i;

    if( miyAbs( pMonster->targetX, pMonster->x ) > miyAbs( pMonster->targetY, pMonster->y ) )
    {
        if( pMonster->targetX < pMonster->x )
        {
            int dx;
            dx = pMonster->dx;
            if( dx == 0 )
            {
                dirs[ 0 ] = MIY_DIR_LEFT;
                p1idx = 1;
                dirs[ 2 ] = MIY_DIR_RIGHT;
                p2idx = 3;
            }
            else if( dx < 0 )
            {
                dirs[ 0 ] = MIY_DIR_LEFT;
                p1idx = 1;
                p2idx = 2;
                dirs[ 3 ] = MIY_DIR_RIGHT;
            }
            else
            {
                p1idx = 0;
                p2idx = 1;
                dirs[ 2 ] = MIY_DIR_RIGHT;
                dirs[ 3 ] = MIY_DIR_LEFT;
            }
        }
        else
        {
            int dx;
            dx = pMonster->dx;
            if( dx == 0 )
            {
                dirs[ 0 ] = MIY_DIR_RIGHT;
                p1idx = 1;
                dirs[ 2 ] = MIY_DIR_LEFT;
                p2idx = 3;
            }
            else if( dx < 0 )
            {
                p1idx = 0;
                p2idx = 1;
                dirs[ 2 ] = MIY_DIR_LEFT;
                dirs[ 3 ] = MIY_DIR_RIGHT;
            }
            else
            {
                dirs[ 0 ] = MIY_DIR_RIGHT;
                p1idx = 1;
                p2idx = 2;
                dirs[ 3 ] = MIY_DIR_LEFT;
            }
        }
        if( pMonster->targetY <= pMonster->y && pMonster->dy <= 0 )
        {
            dirs[ p1idx ] = MIY_DIR_UP;
            dirs[ p2idx ] = MIY_DIR_DOWN;
        }
        else
        {
            dirs[ p1idx ] = MIY_DIR_DOWN;
            dirs[ p2idx ] = MIY_DIR_UP;
        }
    }
    else
    {
        if( pMonster->targetY < pMonster->y )
        {
            int dy;
            dy = pMonster->dy;
            if( dy == 0 )
            {
                dirs[ 0 ] = MIY_DIR_UP;
                p1idx = 1;
                dirs[ 2 ] = MIY_DIR_DOWN;
                p2idx = 3;
            }
            else if( dy < 0 )
            {
                dirs[ 0 ] = MIY_DIR_UP;
                p1idx = 1;
                p2idx = 2;
                dirs[ 3 ] = MIY_DIR_DOWN;
            }
            else
            {
                p1idx = 0;
                p2idx = 1;
                dirs[ 2 ] = MIY_DIR_DOWN;
                dirs[ 3 ] = MIY_DIR_UP;
            }
        }
        else
        {
            int dy;
            dy = pMonster->dy;
            if( dy == 0 )
            {
                dirs[ 0 ] = MIY_DIR_DOWN;
                p1idx = 1;
                dirs[ 2 ] = MIY_DIR_UP;
                p2idx = 3;
            }
            else if( dy < 0 )
            {
                p1idx = 0;
                p2idx = 1;
                dirs[ 2 ] = MIY_DIR_UP;
                dirs[ 3 ] = MIY_DIR_DOWN;
            }
            else
            {
                dirs[ 0 ] = MIY_DIR_DOWN;
                p1idx = 1;
                p2idx = 2;
                dirs[ 3 ] = MIY_DIR_UP;
            }
        }
        // upstream compares `pMonster->targetX <= pMonster->y` here (an X
        // value against a Y field) - a genuine upstream cross-axis-
        // comparison oddity, the same class of faithfully-preserved quirk
        // already found and documented in this project's own Cracky port
        // (its own crkDecideDirection has the near-identical
        // `crkMan.x < pMonster->y` shape) - kept exactly as written, not
        // "corrected". See header comment.
        if( pMonster->targetX <= pMonster->y && pMonster->dx <= 0 )
        {
            dirs[ p1idx ] = MIY_DIR_LEFT;
            dirs[ p2idx ] = MIY_DIR_RIGHT;
        }
        else
        {
            dirs[ p1idx ] = MIY_DIR_RIGHT;
            dirs[ p2idx ] = MIY_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        int dir;
        dir = dirs[ i ];
        if( miyCanMove( pMonster, miyDirDx[ dir ], miyDirDy[ dir ] ) )
        {
            miySetDirection( pMonster, dir );
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void miyInitMonsters()
{
    int i, sprite, count;
    count = miyStageEnemyCount[ miyStageIndex ];
    sprite = MIY_SPRITE_MONSTER;
    for( i = 0; i < count; i = i + 1 )
    {
        miyMonsters[ i ].status = MIY_MOVABLE_LIVE | MIY_MONSTER_VISIBLE;
        miyMonsters[ i ].sprite = sprite;
        sprite = sprite + 1;
    }
    for( i = count; i < MIY_MAX_MONSTER_COUNT; i = i + 1 )
    {
        miyMonsters[ i ].status = 0;
        miyMonsters[ i ].sprite = sprite;
        miyHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void miyStartMonsters()
{
    int i;
    miyUpdateMonsterTargets();
    miyMonsterCount = 0;
    for( i = 0; i < MIY_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = miyMonsters[ i ].status;
        if( ( status & MIY_MOVABLE_LIVE ) != 0 )
        {
            miyMonsters[ i ].status = status | MIY_MONSTER_VISIBLE;
            miyMonsters[ i ].dx = 0;
            miyMonsters[ i ].dy = 0;
            miyLocateMovable( &miyMonsters[ i ], miyStageEnemies[ miyStageIndex ][ i ] );
            miyDecideDirection( &miyMonsters[ i ] );
            miyMonsterShow( &miyMonsters[ i ] );
            miyMonsterCount = miyMonsterCount + 1;
        }
    }
}

void miyMoveMonsters()
{
    int i;
    for( i = 0; i < MIY_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = miyMonsters[ i ].status;
        if( ( status & MIY_MOVABLE_LIVE ) != 0 )
        {
            bool skip;
            skip = false;
            if( ( status & MIY_MOVABLE_LOOSE ) == 0 )
            {
                miyMoveMovable( &miyMonsters[ i ] );
                if( miyIsOnGrid( &miyMonsters[ i ] ) )
                {
                    int column, row, cell;
                    miyMonsters[ i ].status = miyMonsters[ i ].status & ~MIY_MONSTER_VISIBLE;
                    column = miyToGrid( miyMonsters[ i ].x );
                    row = miyToGrid( miyMonsters[ i ].y );
                    cell = miyGetCell( column, row );
                    if( ( cell & MIY_CELL_DOT ) != 0 )
                      miySetCell( column, row, cell & ~MIY_CELL_DOT );
                    else if( ( cell & MIY_CELL_FOOD ) != 0 )
                    {
                        miyMonsters[ i ].status = miyMonsters[ i ].status | MIY_MONSTER_VISIBLE;
                        miySetCell( column, row, cell & ~MIY_CELL_FOOD );
                        miyFoodCount = miyFoodCount - 1;
                        miyPrintFoodCount();
                        miyStartSeq( 0, MIY_MELODY_STOLE );
                        miyUpdateMonsterTargets();
                    }
                    miyDecideDirection( &miyMonsters[ i ] );
                }
                if( ( miyMan.status & MIY_MOVABLE_LOOSE ) == 0 && miyIsNear( &miyMonsters[ i ], &miyMan ) )
                {
                    miyMan.status = miyMan.status & ~MIY_MOVABLE_LIVE;
                    miyMonsters[ i ].status = miyMonsters[ i ].status | MIY_MONSTER_VISIBLE;
                }
            }
            else
            {
                // upstream's own `(clock & CoordMask) == 0` gate around
                // this branch is provably always true when CoordRate==1
                // (as it is here) - simplified away, see header.
                status = status + 2;
                if( ( status & MIY_MOVABLE_PATTERN ) == 0 )
                {
                    miyMonsters[ i ].status = status & ~MIY_MOVABLE_LIVE;
                    miyHideSprite( miyMonsters[ i ].sprite );
                    miyMonsterCount = miyMonsterCount - 1;
                    skip = true;
                }
                else
                  miyMonsters[ i ].status = status;
            }
            if( !skip )
              miyMonsterShow( &miyMonsters[ i ] );
        }
    }
}

MiyMovable* miyHitMonster( MiyMovable* pMovable )
{
    int i;
    for( i = 0; i < MIY_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = miyMonsters[ i ].status;
        if( ( status & MIY_MOVABLE_LIVE ) != 0 && ( status & MIY_MOVABLE_LOOSE ) == 0 )
        {
            if( miyIsNear( pMovable, &miyMonsters[ i ] ) )
            {
                miyMonsters[ i ].status = ( status & MIY_MOVABLE_PATTERN ) | ( MIY_MOVABLE_LIVE | MIY_MONSTER_VISIBLE | MIY_MOVABLE_LOOSE );
                miyMonsterShow( &miyMonsters[ i ] );
                miyUpdateMonsterTargets();
                return &miyMonsters[ i ];
            }
        }
    }
    return NULL;
}


// -----------------------------------------------------------------------------
//   Fire.cpp
// -----------------------------------------------------------------------------

void miyInitFires()
{
    int i, sprite;
    sprite = MIY_SPRITE_FIRE;
    for( i = 0; i < MIY_MAX_FIRE_COUNT; i = i + 1 )
    {
        miyFires[ i ].status = 0;
        miyFires[ i ].sprite = sprite;
        sprite = sprite + 1;
    }
}

void miyStartFire()
{
    int i;
    for( i = 0; i < MIY_MAX_FIRE_COUNT; i = i + 1 )
    {
        if( ( miyFires[ i ].status & MIY_MOVABLE_LIVE ) == 0 )
        {
            miyFires[ i ].status = MIY_MOVABLE_LIVE | MIY_FIRE_MOVING;
            miyFires[ i ].x = miyMan.x;
            miyFires[ i ].y = miyMan.y;
            miyFires[ i ].dx = miyDirDx[ miyManDirection ];
            miyFires[ i ].dy = miyDirDy[ miyManDirection ];
            miyFires[ i ].time = miyFireTime;
            miyStartSeq( 0, MIY_MELODY_FIRE );
            return;
        }
    }
}

bool miyFireHit( MiyMovable* pFire )
{
    MiyMovable* pMonster;
    pMonster = miyHitMonster( pFire );
    if( pMonster != NULL )
    {
        int status, rate;
        status = pFire->status;
        rate = status & MIY_FIRE_POINTMASK;
        miyStartPoint( pMonster->x, pMonster->y, rate >> 4 );
        pFire->status = ( status & ~MIY_FIRE_POINTMASK ) | ( ( rate + 0x10 ) & MIY_FIRE_POINTMASK );
        miyStartSeq( 0, MIY_MELODY_HIT );
        return true;
    }
    return false;
}

void miyMoveFires()
{
    int i;
    for( i = 0; i < MIY_MAX_FIRE_COUNT; i = i + 1 )
    {
        int status;
        status = miyFires[ i ].status;
        if( ( status & MIY_MOVABLE_LIVE ) != 0 )
        {
            int pattern;
            bool alive;
            alive = true;

            if( ( status & MIY_FIRE_MOVING ) != 0 )
            {
                miyFires[ i ].time = miyFires[ i ].time - 1;
                if( miyFires[ i ].time == 0 || !miyCanMove( &miyFires[ i ], miyFires[ i ].dx, miyFires[ i ].dy ) )
                {
                    miyFires[ i ].status = miyFires[ i ].status & ~MIY_FIRE_MOVING;
                    miyFires[ i ].time = miyFireTime << 2;
                    pattern = MIY_CHAR_FIRE;
                }
                else
                {
                    miyMoveMovable( &miyFires[ i ] );
                    pattern = MIY_CHAR_FIREBULLET;
                }
            }
            else
            {
                // upstream's own `(Clock & CoordMask) == 0` gate around
                // this decrement is likewise always true here - see
                // header comment, same simplification as MoveMonsters().
                miyFires[ i ].time = miyFires[ i ].time - 1;
                if( miyFires[ i ].time == 0 )
                {
                    miyFires[ i ].status = miyFires[ i ].status & ~MIY_MOVABLE_LIVE;
                    miyHideSprite( miyFires[ i ].sprite );
                    alive = false;
                }
                pattern = MIY_CHAR_FIRE;
            }

            if( alive )
            {
                miyShowSprite( &miyFires[ i ], pattern );
                miyFireHit( &miyFires[ i ] );
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

#define MIY_FIRE_INTERVAL ( MIY_COORD_RATE * 4 - 1 )

void miyManShow()
{
    miyShowMovable( &miyMan, MIY_CHAR_MAN );
}

void miyInitMan()
{
    miyMan.sprite = MIY_SPRITE_MAN;
    miyMan.status = MIY_MOVABLE_LIVE;
    miyMan.dx = 0;
    miyMan.dy = 0;
    miyManDirection = MIY_DIR_LEFT;
    miyLocateMovable( &miyMan, miyStageStart[ miyStageIndex ] );
    miyManShow();
}

void miyMoveMan()
{
    if( ( ( miyMan.x | miyMan.y ) & MIY_COORD_MASK ) == 0 )
    {
        bool pressed[4];
        bool moved;
        int i;

        pressed[ 0 ] = isLeftPressed();
        pressed[ 1 ] = isRightPressed();
        pressed[ 2 ] = isUpPressed();
        pressed[ 3 ] = isDownPressed();

        moved = false;
        if( pressed[ 0 ] || pressed[ 1 ] || pressed[ 2 ] || pressed[ 3 ] )
        {
            for( i = 0; i < 4; i = i + 1 )
            {
                if( pressed[ i ] )
                {
                    if( miyCanMove( &miyMan, miyDirDx[ i ], miyDirDy[ i ] ) )
                    {
                        miyManDirection = i;
                        moved = true;
                        break;
                    }
                    else if( miyCanMove( &miyMan, miyDirDx[ miyManDirection ], miyDirDy[ miyManDirection ] ) )
                    {
                        moved = true;
                        break;
                    }
                }
            }
        }

        if( moved )
          miySetDirection( &miyMan, miyManDirection );
        else
        {
            miyMan.dx = 0;
            miyMan.dy = 0;
        }

        if( isFirePressed() )
        {
            if( miyManKeyCount == 0 )
            {
                miyStartFire();
                miyManKeyCount = MIY_FIRE_INTERVAL;
            }
            else
              miyManKeyCount = miyManKeyCount - 1;
        }
        else
          miyManKeyCount = 0;
    }

    miyMoveMovable( &miyMan );
    if( miyIsOnGrid( &miyMan ) )
    {
        int column, row, cell;
        column = miyToGrid( miyMan.x );
        row = miyToGrid( miyMan.y );
        cell = miyGetCell( column, row );
        if( ( cell & ( MIY_CELL_DOT | MIY_CELL_FOOD ) ) == 0 )
        {
            miySetCell( column, row, cell | MIY_CELL_DOT );
            miyAddScore( 1 );
        }
    }
    miyManShow();
}


// -----------------------------------------------------------------------------
//   Stage.cpp - level setup (InitStage/InitTrying)
// -----------------------------------------------------------------------------

void miyInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never actually stops the player from continuing indefinitely),
    // shrinking FireTime (a real difficulty ramp - shorter-ranged shots)
    // by 1 every time the whole 8-stage cycle wraps, clamped at
    // MIY_ROW_HEIGHT*2(6) - matching upstream's own wrap loop exactly
    // instead of a plain modulo.
    int i, j;
    miyFireTime = MIY_ROW_HEIGHT * MIY_ROW_COUNT;
    i = 0;
    j = 0;
    while( i < miyCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= MIY_STAGE_COUNT )
        {
            j = 0;
            miyFireTime = miyFireTime - 1;
            if( miyFireTime < MIY_ROW_HEIGHT * 2 )
              miyFireTime = MIY_ROW_HEIGHT * 2;
        }
    }
    miyStageIndex = j;
    miyInitMonsters();
}

void miyInitTrying()
{
    int r, c;

    miyHideAllSprites();
    // ClearScreen()'s own real-hardware-write behavior isn't needed here -
    // every frame is already redrawn fully from miyVVramFront via
    // miyRender(), matching this project's own standing "always redraw,
    // don't replicate a VRAM-persistence partial-redraw trick" precedent
    // (see Cracky's own header comment for the identical reasoning).
    miyClearVVramBuffers();
    miyClearStatusChar();
    miyOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in miyUpdateTitle()) - matches miyOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches miyInitTrying() without going through that transition first
    // (matching Cracky's own identical defensive reset).
    miyFullWidthText = false;

    miyPrintStatus();

    {
        int idx;
        for( idx = 0; idx < MIY_CELLMAP_BYTES; idx = idx + 1 )
          miyCellMap[ idx ] = miyStageBytes[ miyStageIndex ][ idx ];
    }

    // --- wall-drawing pass: a direct, literal translation of upstream's
    // own raw pointer-walking algorithm (see header comment for why this
    // was ported as flat-index arithmetic rather than re-derived as a
    // fresh nested x/y loop). ---
    {
        int pv;

        pv = 0;
        {
            int n;
            for( n = 0; n < MIY_CELL_SIZE * MIY_COLUMN_COUNT - 1; n = n + 1 )
              pv = miyVPut( miyVVramBack, pv, MIY_CHAR_WALL | MIY_WALL_ALL );
        }
        pv = pv + 1;

        {
            int cellIdx;
            cellIdx = 0;
            for( r = 0; r < MIY_ROW_COUNT; r = r + 1 )
            {
                int column;
                column = 0;
                for( c = 0; c < MIY_COLUMN_COUNT / MIY_COLUMNS_PER_BYTE; c = c + 1 )
                {
                    int cell, sub;
                    cell = miyCellMap[ cellIdx ];
                    cellIdx = cellIdx + 1;
                    for( sub = 0; sub < MIY_COLUMNS_PER_BYTE; sub = sub + 1 )
                    {
                        if( ( cell & MIY_CELL_BOTTOMWALL ) != 0 )
                        {
                            miyVPut( miyVVramBack, pv + MIY_VVRAM_WIDTH * 2, MIY_CHAR_WALL | MIY_WALL_ALL );
                            miyVPut( miyVVramBack, pv + MIY_VVRAM_WIDTH * 2 + 1, MIY_CHAR_WALL | MIY_WALL_ALL );
                        }
                        pv = pv + 2;
                        if( column < MIY_COLUMN_COUNT - 1 && ( cell & MIY_CELL_RIGHTWALL ) != 0 )
                        {
                            miyVPut( miyVVramBack, pv, MIY_CHAR_WALL | MIY_WALL_ALL );
                            miyVPut( miyVVramBack, pv + MIY_VVRAM_WIDTH, MIY_CHAR_WALL | MIY_WALL_ALL );
                        }
                        if( column < MIY_COLUMN_COUNT - 1 )
                          miyVPut( miyVVramBack, pv + MIY_VVRAM_WIDTH * 2, MIY_CHAR_WALL | MIY_WALL_ALL );
                        pv = pv + 1;
                        cell = cell >> 4;
                        column = column + 1;
                    }
                }
                pv = pv + MIY_VVRAM_WIDTH * 3 - MIY_CELL_SIZE * MIY_COLUMN_COUNT;
            }
        }

        // --- fence-carving pass: clear each wall block's own Left/Right/
        // Top/Bottom sub-bit whenever the neighboring cell in that
        // direction is genuinely empty space, producing the visual
        // "only draw edges bordering open space" fence look. ---
        {
            int y;
            for( y = 0; y < MIY_VVRAM_HEIGHT; y = y + 1 )
            {
                int x;
                for( x = 0; x < MIY_VVRAM_WIDTH - 1; x = x + 1 )
                {
                    int vi, cval;
                    vi = miyVVramIdx( x, y );
                    cval = miyVVramBack[ vi ];
                    if( x == 0 || miyVVramBack[ vi - 1 ] != MIY_CHAR_SPACE )
                      cval = cval & ~MIY_WALL_LEFT;
                    if( x == MIY_VVRAM_WIDTH - 2 || miyVVramBack[ vi + 1 ] != MIY_CHAR_SPACE )
                      cval = cval & ~MIY_WALL_RIGHT;
                    if( y > 0 && miyVVramBack[ vi - MIY_VVRAM_WIDTH ] != MIY_CHAR_SPACE )
                      cval = cval & ~MIY_WALL_TOP;
                    if( y < MIY_VVRAM_HEIGHT - 1 && miyVVramBack[ vi + MIY_VVRAM_WIDTH ] != MIY_CHAR_SPACE )
                      cval = cval & ~MIY_WALL_BOTTOM;
                    miyVVramBack[ vi ] = cval;
                }
                miyVVramBack[ miyVVramIdx( MIY_VVRAM_WIDTH - 1, y ) ] = MIY_CHAR_FENCE;
            }
        }

        // --- dot/food placement pass ---
        {
            int cellIdx, pv2;
            miyFoodCount = 0;
            pv2 = MIY_VVRAM_WIDTH;
            cellIdx = 0;
            for( r = 0; r < MIY_ROW_COUNT; r = r + 1 )
            {
                for( c = 0; c < MIY_COLUMN_COUNT / MIY_COLUMNS_PER_BYTE; c = c + 1 )
                {
                    int cell, sub;
                    cell = miyCellMap[ cellIdx ];
                    cellIdx = cellIdx + 1;
                    for( sub = 0; sub < MIY_COLUMNS_PER_BYTE; sub = sub + 1 )
                    {
                        if( ( cell & MIY_CELL_FOOD ) != 0 )
                        {
                            miyVPut2C( miyVVramBack, pv2, MIY_CHAR_FOOD );
                            miyFoodCount = miyFoodCount + 1;
                        }
                        else if( ( cell & MIY_CELL_DOT ) != 0 )
                          miyVPut2C( miyVVramBack, pv2, MIY_CHAR_DOT );
                        pv2 = pv2 + MIY_CELL_SIZE;
                        cell = cell >> 4;
                    }
                }
                pv2 = pv2 + MIY_VVRAM_WIDTH * 2;
            }
            miyPrintFoodCount();
        }
    }

    miyInitMan();
    miyStartMonsters();
    miyInitFires();
    miyInitPoints();
    miyDrawAll();
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void miyBeginTitle()
{
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    char sInufuto[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };

    miyClearVVramBuffers();
    miyClearStatusChar();
    miyOverlayActive = false;
    miyFullWidthText = true;
    miyHideAllSprites();
    miyPrintStatus();

    // Draws upstream's own real 5-tile dithered logo bitmap (`TitleBytes[]`)
    // into VVramFront at `VVramWidth*2 + TitleLeft` - VVram row 2, column
    // `TitleLeft = (VVramWidth - 4*TitleLength)/2 = (24 - 4*5)/2 = 2` - i.e.
    // real hardware pages 1-2 (VVram rows 2-5), exactly like Cracky's own
    // logo. `miyComposeRawByte()` OR-combines this VVram content with
    // miyStatusChar's own text layer instead of choosing one exclusively -
    // see that function's own header for why this is safe (the logo and
    // every status-text element occupy disjoint pages).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                miyVVramFront[ miyVVramIdx( 2 + ch * 4 + col, 2 + row ) ] = miyTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // "MINI" subtitle - upstream's `Status.cpp`'s `Title()` draws this at
    // real literal column `TitleLeft + 4*TitleLength - 5 = 2 + 20 - 5 = 17`,
    // page 3 (matching the game's own real title, "Mieyen mini" per its own
    // readme.md).
    miyPrintS( 3, 17, sMini, 4 );
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): the "INUFUTO 2026" credit at col 12,
    // START/CONTINUE at col 9 with the cursor at col 8).
    miyPrintS( 7, 12, sInufuto, 12 );

    miyPrintS( 5, 9, sStart, 5 );
    miyPrintS( 6, 9, sContinue, 8 );
    miySelection = 0;
    miySelectionChanged = true;
    miyPrevLeft = 0; miyPrevRight = 0; miyPrevUp = 0; miyPrevDown = 0; miyPrevFire = 0;
    miyState = MIY_STATE_TITLE;
}

// Moved up from immediately after miyUpdateTitle() - it needs both of
// these (the fire-press "start a fresh/continued game" transition), and
// this dialect has no forward declarations. Both only depend on
// miyInitStage()/miyInitTrying(), already defined above. Preserving this
// placement (rather than moving the two functions back below
// miyUpdateTitle()) matches the current, already-fixed shape of the real
// upstream-derived source this port is translated from.
void miyBeginTrying()
{
    miyInitTrying();
    miyFirstBigTick = true;
    miyMonsterMoveTurn = 0;
    miyTickCounter = 0;
    miyStartSeq( 1, MIY_MELODY_START );
    miyState = MIY_STATE_START_JINGLE;
}

void miyBeginStage()
{
    miyInitStage();
    miyBeginTrying();
}

void miyUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !miyPrevLeft ) || ( right && !miyPrevRight ) ||
                ( up && !miyPrevUp ) || ( down && !miyPrevDown ) );
    justFire = ( fire && !miyPrevFire );
    miyPrevLeft = left; miyPrevRight = right; miyPrevUp = up; miyPrevDown = down; miyPrevFire = fire;

    if( miySelectionChanged )
    {
        miySelectionChanged = false;
        if( miySelection == 0 )
          miyPrintC( 5, 8, '>' );
        else
          miyPrintC( 5, 8, ' ' );
        if( miySelection == 1 )
          miyPrintC( 6, 8, '>' );
        else
          miyPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        bool continuing;
        miyFullWidthText = false;
        continuing = ( miySelection == 1 );
        miyScore = 0;
        if( !continuing )
          miyCurrentStage = 0;
        miyRemainCount = 3;
        miyBeginStage();
        miyRender();
        return;
    }
    if( justDir )
    {
        miySelection = miySelection ^ 1;
        miySelectionChanged = true;
    }
    miyRender();
}

void miyUpdateStartJingle()
{
    if( !miySeqPlaying( 1 ) )
    {
        miyStartBgm();
        miyState = MIY_STATE_PLAYING;
    }
    miyRender();
}

void miyBeginLoseWait()
{
    miyWaitFrames = 30;
    miyState = MIY_STATE_PRE_LOSE_WAIT;
}

void miyBeginLose()
{
    miyStopBgm();
    miyAnimStep = 0;
    miyWaitFrames = 0;
    miyState = MIY_STATE_LOSE_ANIM;
}

void miyUpdatePreLoseWait()
{
    if( miyWaitFrames > 0 )
    {
        miyWaitFrames = miyWaitFrames - 1;
        miyRender();
        return;
    }
    miyBeginLose();
    miyRender();
}

void miyUpdateLoseAnim()
{
    int patterns[4] = { MIY_CHAR_MAN_LEFT0, MIY_CHAR_MAN_RIGHT0, MIY_CHAR_MAN_UP0, MIY_CHAR_MAN_DOWN0 };

    if( miyWaitFrames > 0 )
    {
        miyWaitFrames = miyWaitFrames - 1;
        miyRender();
        return;
    }

    miyShowSprite( &miyMan, patterns[ miyAnimStep & 3 ] );
    miyDrawAll();
    miyStartSeq( 0, MIY_MELODY_LOOSE );
    miyAnimStep = miyAnimStep + 1;
    miyWaitFrames = miyNoteFrames( 1 );

    if( miyAnimStep >= 8 )
    {
        miyRemainCount = miyRemainCount - 1;
        if( miyRemainCount > 0 )
          miyBeginTrying();
        else
        {
            miyPrintGameOver();
            miyStartSeq( 1, MIY_MELODY_GAMEOVER );
            miyState = MIY_STATE_GAMEOVER_JINGLE;
        }
    }
    miyRender();
}

void miyUpdateGameOverJingle()
{
    if( !miySeqPlaying( 1 ) )
      miyBeginTitle();
    else
      miyRender();
}

void miyBeginClearWait()
{
    miyStopBgm();
    miyWaitFrames = 30;
    miyState = MIY_STATE_CLEAR_WAIT;
}

void miyUpdateClearWait()
{
    if( miyWaitFrames > 0 )
    {
        miyWaitFrames = miyWaitFrames - 1;
        miyRender();
        return;
    }
    miyStartSeq( 1, MIY_MELODY_CLEAR );
    miyState = MIY_STATE_CLEAR_JINGLE;
    miyRender();
}

void miyBeginBonusTally()
{
    miyBonusExtraWait = 0;
    miyBonusPlayingSound = false;
    miyState = MIY_STATE_BONUS_TALLY;
}

void miyUpdateClearJingle()
{
    if( !miySeqPlaying( 1 ) )
      miyBeginBonusTally();
    miyRender();
}

void miyUpdateBonusTally()
{
    if( miyBonusExtraWait > 0 )
    {
        miyBonusExtraWait = miyBonusExtraWait - 1;
        miyRender();
        return;
    }
    if( miyBonusPlayingSound )
    {
        if( miySeqPlaying( 0 ) )
        {
            miyRender();
            return;
        }
        miyBonusPlayingSound = false;
        miyBonusExtraWait = 30;
        miyRender();
        return;
    }
    if( miyFoodCount > 0 )
    {
        miyFoodCount = miyFoodCount - 1;
        miyAddScore( 50 );
        miyPrintStatus();
        miyStartSeq( 0, MIY_MELODY_BONUS );
        miyBonusPlayingSound = true;
        miyRender();
        return;
    }
    miyCurrentStage = miyCurrentStage + 1;
    miyBeginStage();
    miyRender();
}

void miyUpdatePlaying()
{
    miyTickCounter = miyTickCounter + 1;
    if( miyTickCounter < MIY_TICK_DIVISOR )
    {
        miyRender();
        return;
    }
    miyTickCounter = 0;

    // MoveFires() runs 4x per real WaitTimer(8)-gated "big tick" upstream
    // (once unconditionally per raw loop iteration, 4 of which happen
    // between one render and the next) while MoveMan()/MoveMonsters() run
    // only once (or once every other tick, for monsters) - see header
    // comment for the full derivation. The 3 "leftover" calls below
    // reproduce the tail end of the *previous* big tick's own off-ticks,
    // chronologically before this tick's own MoveMan/MoveMonsters/final
    // MoveFires - skipped only on the very first tick after entering
    // PLAYING, matching Clock's own real value of exactly 0 there.
    if( !miyFirstBigTick )
    {
        miyMoveFires();
        miyMoveFires();
        miyMoveFires();
    }
    miyFirstBigTick = false;

    miyMoveMan();
    miyUpdatePoints();

    if( miyMonsterMoveTurn == 0 )
      miyMoveMonsters();
    miyMonsterMoveTurn = miyMonsterMoveTurn ^ 1;

    miyMoveFires();

    miyDrawAll();

    // FoodCount==0 (every hamburger stolen) takes priority over Man dying,
    // exactly matching upstream's own real check order (a `goto lose`
    // reached via the FoodCount branch skips the do-while's own trailing
    // condition check entirely, so a simultaneous "last food stolen AND
    // Man collided with a monster on the same tick" always resolves as
    // the FoodCount-triggered loss, never the win/MonsterCount check).
    if( miyFoodCount == 0 )
    {
        miyRender();
        miyBeginLoseWait();
        return;
    }
    if( ( miyMan.status & MIY_MOVABLE_LIVE ) == 0 )
    {
        miyRender();
        miyBeginLose();
        return;
    }
    if( miyMonsterCount == 0 )
    {
        miyRender();
        miyBeginClearWait();
        return;
    }

    miyRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameMieyen_init()
{
    miyHiScore = 0;
    miyScore = 0;
    miyCurrentStage = 0;
    miyRemainCount = 3;
    miyFoodCount = 0;

    {
        int i;
        for( i = 0; i < 3; i = i + 1 )
        {
            miySeqActive[ i ] = 0;
            miySeqMelody[ i ] = MIY_MELODY_NONE;
        }
    }
    miyOverlayActive = false;
    miyTickCounter = 0;
    // miyManKeyCount is deliberately left untouched here - matching
    // upstream's own real C++ function-local-static lifetime for this
    // exact variable (zero-initialized once at program start, never reset
    // anywhere else even across retries/stage transitions) - see header.

    miyBeginTitle();
}

void gameMieyen_update()
{
    miyAdvanceSound();

    if( miyState == MIY_STATE_TITLE )
      miyUpdateTitle();
    else if( miyState == MIY_STATE_START_JINGLE )
      miyUpdateStartJingle();
    else if( miyState == MIY_STATE_PLAYING )
      miyUpdatePlaying();
    else if( miyState == MIY_STATE_PRE_LOSE_WAIT )
      miyUpdatePreLoseWait();
    else if( miyState == MIY_STATE_LOSE_ANIM )
      miyUpdateLoseAnim();
    else if( miyState == MIY_STATE_GAMEOVER_JINGLE )
      miyUpdateGameOverJingle();
    else if( miyState == MIY_STATE_CLEAR_WAIT )
      miyUpdateClearWait();
    else if( miyState == MIY_STATE_CLEAR_JINGLE )
      miyUpdateClearJingle();
    else if( miyState == MIY_STATE_BONUS_TALLY )
      miyUpdateBonusTally();
}
