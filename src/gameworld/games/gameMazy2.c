#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// MAZY2 mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_mazy2`) - a maze game: find
// the exit hidden somewhere in a multi-floor maze while dodging wandering
// monsters, picking up knives off the floor and throwing them (in whichever
// direction you're currently facing/moving) to destroy monsters that get in
// the way. 8 hand-authored stages (2-5 floors each, connected by paired up/
// down staircases), 3 lives, no persistent hi-score (upstream's own
// `HiScore` tracking is entirely commented out in the real source - not a
// gap this port needs to fill, matches the shipped game exactly).
//
// Same CH32V003+SSD1306 UIAPduino hardware/driver lineage as the sibling
// port `gameCracky.c` (same author, same real 60Hz SysTick frame limiter
// via `Timer.cpp`'s `kTimerHz=60`/`WaitTimer(t)`, same `ScanKeys.h` 4-
// direction+1-button input, same `Oled.cpp`/`Vram.cpp`/`VVram.cpp` display
// stack) - this port follows Cracky's own already-proven methodology
// directly rather than re-deriving it. Only 4 directions + 1 action button
// (`Keys_Button0`), a strict subset of what `tinyJoypadShim.h` already
// exposes - no new shim primitive needed, `isFire2Pressed()` unused.
//
// **No hardware display-orientation transform applied, on purpose** -
// `InitOled()` sends the exact same `OledCmd::RightToLeft`/`BottomToTop`
// register commands Cracky's own `InitOled()` does, and Cracky's own
// session already spent significant effort discovering (via a real
// reference photo) that these settings do NOT need replicating in
// software at all: they exist on real hardware to compensate for a
// physical panel-mounting quirk specific to that module, with nothing
// equivalent to correct for in a software recreation. The composed byte
// is drawn directly at its own `(col,page)` - no column mirror, no page
// reversal, no bit-reversal table.
//
// **Rendering is the same genuine two-level tile system as Cracky's own
// port, ported as a direct structural mirror rather than re-derived into
// a closed form** - `VVram` is a 25x16 logical glyph-index grid; pairs of
// vertically-stacked VVram cells pack into 4 real output bytes via a
// 2-byte-per-glyph `CharPattern` lookup and the same nibble-interleaving
// `crkComposeRawByte()` already established (`mz2ComposeMapByte()` here).
// Two real structural differences from Cracky, both kept faithful to
// upstream rather than simplified away:
//   - Upstream keeps a genuinely separate `VVramBack` (the static per-
//     floor map, rebuilt only on `InitFloor()`, i.e. once per floor entry)
//     and `VVramFront` (a fresh working copy composited with sprites/
//     points every `DrawAll()` call) - kept as the real Back/Front split
//     here (`mz2VVramBack`/`mz2VVramFront`), rather than Cracky's own
//     choice to fully rebuild one combined VVram from scratch every
//     frame. Cracky's map (a flat 12x4 grid) is cheap enough to redo in
//     full every tick; this game's own map render additionally runs a
//     real flood-fill visibility scan (see below) that's genuinely
//     expensive to repeat every frame - keeping upstream's own caching
//     split is the right call here, not a fidelity shortcut.
//   - Both `VVramBack` and `VVramFront` are flat `int[400]` arrays
//     (`VVramWidth*VVramHeight`, matching upstream's own real
//     `VVramOffset(x,y)=y*VVramWidth+x` addressing exactly, via a shared
//     `mz2VOffset()` helper) rather than a 2D array the way Cracky's own
//     `crkVVram[height][width]` is - `InitFloor()`'s own map-render
//     routine and `Title()`'s own logo render are both genuinely
//     stateful pointer-walk algorithms (advance N, back up M, etc, with
//     dozens of non-uniform relative offsets) that are far safer to
//     literally mirror with a linear cursor index than to re-derive as
//     row/col math - "faithfully copy an intricate stateful algorithm's
//     own shape rather than re-derive a closed form", per this project's
//     own established precedent (Frogger's own row-buffer compositing;
//     Cracky's own VVram-packing header comment states the same
//     reasoning for the *packing* half of this same system).
//
// **Status/UI text lives in ONE unified whole-screen overlay grid,
// architecturally simpler than Cracky's own two-tier design** - Cracky
// needed a `crkStatusChar[8][8]` grid scoped ONLY to its rightmost status
// columns *plus* a separate `crkOverlayActive`/`crkOverlayText` mechanism
// for the one GAME OVER/TIME UP message that lands inside the *map*
// area - because this game's own always-on status labels (SCORE/STAGE/
// TIME/lives/held-knife-count) sit at a fixed `LeftX=24` (raw pixel 96+)
// *and* its GAME OVER/TIME UP messages, *and* its title screen's own
// "MINI" text, all land at genuinely different, sometimes-overlapping-
// with-the-map raw columns, this port instead uses ONE `mz2StatusChar
// [8][32]` grid spanning the *entire* screen width (32 4px-wide "text
// cells"), where a per-cell sentinel of -1 means "transparent, show
// the VVram-derived map content here" and any other value (0-26) is a
// real `AsciiPattern` glyph index overriding the map for that one cell.
// `mz2ComposeMapByte()` is consulted only when a cell's own status entry
// is -1. This absorbs Cracky's two separate mechanisms into one, and -
// since `mz2ClearScreen()` (called both entering the title screen and
// entering real gameplay, matching upstream's own two `ClearScreen()`
// call sites) resets the *whole* grid back to -1 - this proactively
// avoids the exact "stale overlay text bleeds into the next screen" bug
// class Cracky's own session found and had to fix via a live user
// report (see Cracky's own `crkInitTrying()` comment) rather than
// needing a report here first.
//
// **One genuine exception, kept OUTSIDE the unified status grid on
// purpose**: `PrintHeldKnives()` draws its 4 knife-icon slots via
// upstream's own `PutU()` (a *single-height* `CharPattern`-tile glyph,
// not an `AsciiPattern` text glyph - a different font table entirely),
// spaced a real 5 real pixels apart (`VramStep(4)+1`) rather than the
// 4px-per-cell grid every other status element uses. Reproducing that
// exact 1px inter-icon gap would need genuine sub-cell positioning this
// port's whole-screen 4px-cell grid can't represent - simplified to
// plain 4px-cell-tight packing instead (a purely cosmetic difference for
// a small decorative inventory row, the same class of "drop a minor
// exact-pixel-spacing detail for a non-gameplay-critical HUD element"
// simplification already used elsewhere in this project), handled by a
// small dedicated `mz2HeldKnifeIcons[4]` array and a direct composer
// branch in `mz2Render()` rather than routed through the ascii-glyph
// status grid at all (the held-knife icon *is* a `CharPattern` glyph,
// not text).
//
// **The maze's own real, working "fog of war" visibility system is
// ported with the SAME converged result, but a cheaper algorithm
// shape**: upstream's own flood-fill (`InitFloor()`'s trailing block)
// restarts its *entire* 40-cell scan from index 0 the instant any single
// cell visited during a pass causes a new cell to become queued
// (`if (changed) goto loop;`, mid-scan, not just at the end of a full
// pass) - a real, if inefficient, algorithm. The *final* set of visible
// cells this converges to is a pure function of maze connectivity from
// the seed cell, independent of *how* the scan is ordered or how often
// it restarts - so `mz2FloodFillVisibility()` instead does the
// standard, cheaper "repeat one full pass over all cells until a pass
// makes no changes" shape, converging to the *identical* final Visible
// set with far fewer redundant re-scans - not a shortcut on the game's
// own real behavior, just a cheaper way to compute the same fixed point.
// The eventual read of `mz2CellMap[idx +/- ColumnCount]` at a maze
// boundary (row 0's own top-neighbor, row 4's own bottom-neighbor, etc)
// is guarded defensively in `mz2SetCellVisible()` even though a direct
// check of every one of the 28 real floors' own raw wall-encoding bytes
// confirmed the outer boundary is always genuinely walled in every stage
// (so the guard is verified-unreachable for the real shipped level data,
// kept anyway as a cheap safety net against any future data edit, matching
// this project's own repeated "preserve behavior, guard the crash" caution
// for boundary reads whose safety depends on data rather than the
// algorithm's own structure).
//
// **`Movable`/`Man`/`Monster`/`Knife` classes flattened to plain structs +
// `mz2`-prefixed free functions taking an explicit pointer** (Man's own
// extra `pDirection` field became a separate `mz2ManDirIndex` global
// rather than a struct field, since nothing else in `Movable` needs it) -
// the same treatment already established across this whole project's
// C++-class ports. The multi-floor `Stage`/`Floor` struct hierarchy
// (a `Stage` holding a `count+pointer` to its own `Floor` array, each
// `Floor` itself holding `count+pointer` fields for its own stairs/
// monsters/knives) was flattened into parallel flat tables addressed by
// a single contiguous "global floor index" spanning all 8 stages'
// combined 28 floors (`mz2StageFloorStart[stage]+floorWithinStage`),
// each with its own `start+count` pair into one shared flat data array
// per resource kind (`mz2AllStairs`/`mz2AllMonsters`/`mz2AllKnives`) -
// the same "flatten struct-with-pointer-members into parallel arrays"
// precedent already established by Cracky's own `Stage` flattening, just
// one level deeper here since this game's own real per-floor structure
// genuinely has variable-length sub-lists nested inside a variable-length
// floor list, not a single fixed list. All data tables (glyph patterns,
// stage/floor/stair/monster/knife tables, sound melody tables) were
// byte-diff-extracted via small Python scripts against the real upstream
// source before ever building, not hand-transcribed - the stair/monster/
// knife arrays specifically use real `(col<<4)|row` / `0x02`-style C
// expressions rather than plain literals, so the first extraction attempt
// (a naive hex/decimal regex) silently mis-extracted every individual
// operand as a separate value instead of evaluating the whole expression
// - caught immediately by comparing the extracted array lengths against
// each floor's own declared `stairCount`/`monsterCount`/`knifeCount`
// (every single array mismatched), fixed by evaluating each comma-
// separated C expression properly before trusting any of it.
//
// **CoordRate=1 in this shipped build** (`CoordShift=0`, matching
// Cracky's own identical setup) makes every literal `((x|y)&CoordMask)==0`
// check upstream performs trivially always-true - kept literal rather
// than simplified away, matching Cracky's own stated "keep #defines/
// expressions exactly as upstream wrote them" preference; the *real*,
// position-varying alignment check (`CoordMod(x)==0`) is kept as an
// actual lookup-table port (`mz2CoordModTable`/`mz2ToGridTable`, both
// literally transcribed 32-entry tables, not re-derived formulas) rather
// than risk a subtly-wrong hand-derived replacement for a table whose
// exact boundary behavior (a `-1` sentinel for an unreachable input) this
// port doesn't need to independently re-verify is safe.
//
// **Sound**: the same real 3-tone-channel software mixer as Cracky's own
// `Sound.cpp` (`Tempo=220` here, not Cracky's 160 - a different tempo
// constant, so `mz2NoteFrames()`'s own derived multiplier differs:
// `SoundHandler()`'s real cadence is `(600/2)/Tempo = 300/220 ~= 1.364`
// real 60Hz ticks per channel-advance, vs Cracky's own `1.875`). Every
// upstream `Sound_*()` call routes to `md_playTone(freqHz, durationSeconds)`
// via the same small frame-stepped, id-resolved sequencer shape already
// established for Cracky (3 independent voices: 0=one-shot SFX, 1=jingle/
// BGM-voice-A, 2=BGM-voice-B - advancing every real engine frame,
// independent of `MZ2_TICK_DIVISOR`). Two of upstream's own `Sound_*()`
// calls (`Sound_Loose`, `Sound_Beep`) are real *blocking* `WaitMelody()`
// calls on real hardware - each gets its own explicit wait-state in this
// port's state machine (`mz2WaitFrames = mz2NoteFrames(1)`, plus the
// bonus-tally state's own extra literal `WaitTimer(3)` folded directly
// into that same wait value - `mz2NoteFrames(1)+3` - since upstream's own
// `Sound_Beep(); WaitTimer(3);` pair, unlike Cracky's own equivalent
// bonus-tally step which has no separate trailing wait at all, genuinely
// adds a second, independent real-time delay after the note finishes).
// `Sound_Hit`/`Sound_Get` are real non-blocking `StartMelody()` calls on
// real hardware and stay non-blocking here too (fire-and-forget
// `mz2StartSeq()` calls with no associated wait state).
//
// **`ChangeFloor()`'s own extra `DrawAll()` call, right before actually
// switching floors, is dropped rather than reproduced** - a deliberate
// simplification, not an oversight. On real hardware this call performs
// a genuine, comparatively slow synchronous I2C transfer that visibly
// updates the physical display for a real, non-negligible slice of wall-
// clock time *before* the very next line of code (re)builds the new
// floor's own map data - so a human eye really does catch that
// intermediate "old floor, man standing on the stair" frame before the
// screen cuts to the new floor. On this engine, drawing more than once
// within a single `update()` call has no such effect: nothing is ever
// actually *presented* to the screen until the frame ends, so an earlier
// draw that gets overwritten by a later one within the same tick is pure
// wasted work, never a second visible frame - the same "multiple renders
// per tick don't produce multiple visible frames" model this whole
// project's other ports already rely on. `mz2ChangeFloor()` just switches
// `mz2CurrentFloor` and calls `mz2InitFloor()` directly.
//
// **A real, faithfully-preserved-not-fixed upstream quirk**: `InitFloor
// ()`'s own per-cell render, for a currently-*invisible* (fogged) maze
// cell, deliberately writes only a 3x3 inner block of blank cells and
// leaves that cell's own top row and left column entirely untouched -
// relying on either a neighboring *visible* cell's own already-drawn
// shared edge, or (if surrounded by other invisible cells too) whatever
// was already sitting in `VVramBack` from a *previous* floor's own
// render into that same buffer position (since `VVramBack` is never
// explicitly zeroed between floor entries, only ever selectively
// overwritten). This is a real, if obscure, potential "ghost fragment
// from a previous floor" artifact that exists on real hardware exactly
// as shipped - not something this port adds or needs to fix, since
// `mz2VVramBack` starts genuinely zero-initialized (`Char_Space`) the
// same way real BSS memory does, matching the very first floor's own
// correct appearance, and any later-floor artifact would be an
// inherited-from-upstream oddity rather than a porting defect.
//
// **Lives-remaining display, a small deliberate improvement over
// Cracky's own equivalent rather than a literal copy of it**: upstream
// draws a real 2x2 `Char_Remain` (`Char_Man`) icon per remaining life
// via `Put2C()`, falling back to an icon+space+digit summary once more
// than 2 lives remain - simplified here, like Cracky's own precedent, to
// plain ascii-text digits instead of baking a decorative icon (matching
// this port's own status-text-only design). Cracky's own equivalent
// simplification left its *common* case (<=2 remaining lives, the only
// case ever actually reachable given `RemainCount` starts at 3 and only
// decreases) rendering nothing but blank spaces - `mz2PrintStatus()`
// instead always shows the real remaining-life count as a single digit
// in both branches, a small, self-consistent, deliberate improvement
// over reproducing what reads as a likely oversight in the sibling port.
//
// **A proactive fix applied from the start, matching an already-found
// sibling bug rather than waiting for a fresh report**: upstream's own
// `Title()` calls `PrintStatus()` directly (showing whatever `Score`/
// `CurrentStage`/`RemainCount`/`StageTime` the *previous* game session
// left behind, since `Main()` only resets `Score`/`CurrentStage`/
// `RemainCount` *after* `Title()` already returns) - the exact same
// shape Cracky's own session found and fixed as a real, live-reported
// bug ("on game over, the time value remains visible on titlescreen").
// `mz2BeginTitle()` resets `mz2StageTime = 0` before calling
// `mz2PrintStatus()`, matching Cracky's own already-user-approved fix,
// applied here proactively rather than needing its own separate report.
//
// **The deferred `mz2FloorChangePending`/`mz2PendingFloor` mechanism**:
// `mz2ChangeFloor()` (and the `mz2InitFloor()` it calls) are defined
// later in this file than `mz2MoveMan()` - this dialect has no forward
// declarations, so a floor change requested from inside `mz2MoveMan()`
// is deferred via these two globals instead of reordering the whole
// render/visibility dependency chain those two functions need.
// Upstream's own `MoveMan()` calls `ChangeFloor()` synchronously, as the
// very last statement it ever executes, and the very next statement in
// upstream's own outer loop is `MoveMonsters()` - so upstream's own
// "floor already changed before Monsters/Knives/DrawAll see it" behavior
// only depends on the call happening *before* those siblings, not on it
// happening synchronously *inside* `MoveMan()` specifically.
// `mz2UpdatePlaying()` applies the deferred change immediately after
// `mz2MoveMan()` returns and strictly before `mz2MoveMonsters()`/
// `mz2UpdatePoints()`/the knife-move loop/`mz2DrawAll()` - reproducing
// the exact same "already on the new floor" visibility for every one of
// those siblings.
// =============================================================================


// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into mz2CharPattern (map tiles)
// -----------------------------------------------------------------------------

#define MZ2_CHAR_SPACE 0x00
#define MZ2_CHAR_WALL 0x10
#define MZ2_CHAR_KNIFE 0x11
#define MZ2_CHAR_KNIFE_LEFT 0x11
#define MZ2_CHAR_KNIFE_RIGHT 0x12
#define MZ2_CHAR_KNIFE_UP 0x13
#define MZ2_CHAR_KNIFE_DOWN 0x14
#define MZ2_CHAR_POINT 0x15
#define MZ2_CHAR_MAN 0x1F
#define MZ2_CHAR_MAN_LEFT 0x1F
#define MZ2_CHAR_MAN_DOWN 0x4F
#define MZ2_CHAR_MAN_LOOSE2 0x5F
#define MZ2_CHAR_MAN_LOOSE3 0x63
#define MZ2_CHAR_CHASER 0x67
#define MZ2_CHAR_DISTURBER 0x6B
#define MZ2_CHAR_STAIRUP 0x6F
#define MZ2_CHAR_STAIRDOWN 0x73
#define MZ2_CHAR_GOAL 0x77

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define MZ2_COORD_SHIFT 0
#define MZ2_COORD_RATE ( 1 << MZ2_COORD_SHIFT )
#define MZ2_COORD_MASK ( MZ2_COORD_RATE - 1 )
#define MZ2_HIT_RANGE ( MZ2_COORD_RATE * 4 / 3 )

#define MZ2_MOVABLE_LIVE 0x80

#define MZ2_DIRECTION_LEFT 0
#define MZ2_DIRECTION_RIGHT 1
#define MZ2_DIRECTION_UP 2
#define MZ2_DIRECTION_DOWN 3

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
} Mz2Movable;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define MZ2_COLUMN_COUNT 8
#define MZ2_ROW_COUNT 5
#define MZ2_STAGE_COUNT 8

#define MZ2_CELL_RIGHT_WALL 0x01
#define MZ2_CELL_BOTTOM_WALL 0x02
#define MZ2_CELL_LEFT_WALL 0x10
#define MZ2_CELL_TOP_WALL 0x20
#define MZ2_CELL_DOWN_STAIR 0x04
#define MZ2_CELL_UP_STAIR 0x08
#define MZ2_CELL_GOAL 0x0c
#define MZ2_CELL_TYPE_MASK 0x0c
#define MZ2_CELL_NEXT_SCAN 0x40
#define MZ2_CELL_VISIBLE 0x80

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define MZ2_VVRAM_WIDTH 25
#define MZ2_VVRAM_HEIGHT 16
#define MZ2_WINDOW_WIDTH 23

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define MZ2_SPRITE_MAN 0
#define MZ2_SPRITE_MONSTER 1
#define MZ2_SPRITE_KNIFE 7
#define MZ2_SPRITE_END 15
#define MZ2_INVALID_PATTERN 255

typedef struct
{
    int x, y, pattern;
} Mz2Sprite;

// -----------------------------------------------------------------------------
//   Status.h
// -----------------------------------------------------------------------------

#define MZ2_LEFT_X 24

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching upstream's own enum exactly.
// -----------------------------------------------------------------------------

#define MZ2_N8 6
#define MZ2_N4 ( MZ2_N8 * 2 )
#define MZ2_N4P ( MZ2_N4 * 3 / 2 )
#define MZ2_N2 ( MZ2_N4 * 2 )

#define MZ2_F2 2
#define MZ2_G2 4
#define MZ2_A2 6
#define MZ2_C3 9
#define MZ2_D3 11
#define MZ2_E3 13
#define MZ2_F3 14
#define MZ2_A3 18
#define MZ2_C4 21
#define MZ2_D4 23
#define MZ2_E4 25
#define MZ2_F4 26
#define MZ2_G4 28
#define MZ2_A4 30
#define MZ2_B4 32
#define MZ2_C5 33
#define MZ2_D5 35
#define MZ2_E5 37
#define MZ2_F5 38

#define MZ2_TEMPO 220

#define MZ2_MELODY_NONE 0
#define MZ2_MELODY_LOOSE 1
#define MZ2_MELODY_HIT 2
#define MZ2_MELODY_BEEP 3
#define MZ2_MELODY_GET 4
#define MZ2_MELODY_START 5
#define MZ2_MELODY_CLEAR 6
#define MZ2_MELODY_GAMEOVER 7
#define MZ2_MELODY_BGM1 8
#define MZ2_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via small Python scripts from the real
//   upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph - byte-for-byte
// identical to the same table already proven in gameCracky.c, kept as its
// own self-contained copy per this project's standing per-game-file
// convention.
int mz2AsciiPattern[108] = {
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

// CharPattern - 123 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int mz2CharPattern[246] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0xa5, 0xa5, 0x62, 0x66, 0x66, 0x26, 0xf0, 0x0e,
    0xf0, 0x07, 0x00, 0x0e, 0x00, 0x03, 0xaa, 0x0e,
    0x23, 0x02, 0x8e, 0x0e, 0x00, 0x03, 0xae, 0x0e,
    0x23, 0x03, 0x2e, 0x0e, 0x23, 0x03, 0x80, 0xf5,
    0x7d, 0x08, 0x10, 0x3c, 0xc3, 0x01, 0x00, 0xf5,
    0xfd, 0x00, 0x90, 0x34, 0x43, 0x05, 0x00, 0x75,
    0x7d, 0x00, 0x00, 0xd0, 0xc1, 0x00, 0x00, 0xf5,
    0x7d, 0x00, 0x10, 0x2d, 0x43, 0x05, 0x80, 0xd7,
    0x5f, 0x08, 0x10, 0x3c, 0xc3, 0x01, 0x00, 0xdf,
    0x5f, 0x00, 0x50, 0x34, 0x43, 0x09, 0x00, 0xd7,
    0x57, 0x00, 0x00, 0x1c, 0x0d, 0x00, 0x00, 0xd7,
    0x5f, 0x00, 0x50, 0x34, 0xd2, 0x01, 0x8c, 0xac,
    0x8a, 0x44, 0x23, 0x53, 0x15, 0x22, 0x8c, 0x8c,
    0x8a, 0x24, 0x23, 0x33, 0x15, 0x06, 0x8c, 0x0c,
    0x08, 0x88, 0x23, 0x13, 0x01, 0x22, 0x8c, 0x8c,
    0x86, 0x44, 0x23, 0x13, 0x15, 0x06, 0x4c, 0xac,
    0x8a, 0x44, 0x13, 0x53, 0x15, 0x22, 0x4c, 0xcc,
    0x8a, 0x06, 0x13, 0x13, 0x15, 0x42, 0x4c, 0x8c,
    0x08, 0x44, 0x13, 0x03, 0x01, 0x11, 0x4c, 0x8c,
    0x8a, 0x06, 0x13, 0x13, 0x16, 0x22, 0x80, 0xc3,
    0x3c, 0x08, 0x10, 0xbe, 0xaf, 0x01, 0x44, 0xa8,
    0xca, 0xc8, 0x22, 0x51, 0x35, 0x32, 0x60, 0x69,
    0x68, 0x69, 0xc0, 0x36, 0x3f, 0xc6, 0xc0, 0x3e,
    0x3f, 0xce, 0xc0, 0x37, 0x3f, 0xc7, 0x1f, 0x11,
    0x99, 0xf1, 0x8f, 0x8b, 0xbb, 0xf8, 0x88, 0xc0,
    0x0c, 0xee, 0xbb, 0xd8, 0xcd, 0xee, 0xf0, 0xff,
    0x7f, 0x0f, 0xf0, 0xff, 0xef, 0x0f,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40) -
// byte-for-byte identical to the same table already proven in gameCracky.c.
int mz2Frequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int mz2MelodyLoose[3] = { 1, 18, 0 };
int mz2MelodyHit[17] = { 1, 26, 1, 28, 1, 30, 1, 32, 1, 33, 1, 35, 1, 37, 1, 38, 0 };
int mz2MelodyBeep[3] = { 1, 30, 0 };
int mz2MelodyGet[3] = { 1, 30, 0 };
int mz2MelodyStart[11] = { 6, 21, 6, 28, 6, 25, 6, 28, 24, 33, 0 };
int mz2MelodyClear[21] = {
    6, 21, 6, 25, 6, 28, 6, 23, 6, 26,
    6, 30, 6, 25, 6, 28, 6, 32, 18, 33,
    0,
};
int mz2MelodyGameOver[23] = {
    6, 33, 6, 28, 6, 25, 6, 33, 6, 32,
    6, 28, 6, 25, 6, 32, 12, 30, 12, 32,
    24, 33, 0,
};
int mz2MelodyBgm1[89] = {
    6, 23, 6, 26, 6, 28, 6, 30, 6, 30,
    6, 30, 6, 30, 6, 30, 6, 30, 6, 30,
    6, 30, 6, 28, 6, 28, 6, 28, 6, 28,
    6, 28, 6, 28, 6, 30, 6, 28, 24, 26,
    24, 28, 30, 30, 6, 23, 6, 26, 6, 28,
    6, 30, 6, 30, 6, 30, 6, 30, 6, 30,
    6, 30, 6, 30, 6, 30, 6, 28, 6, 28,
    6, 28, 6, 28, 6, 28, 6, 28, 6, 30,
    6, 28, 24, 26, 24, 28, 30, 26, 255,
};
int mz2MelodyBgm2[41] = {
    18, 0, 18, 2, 18, 2, 12, 2, 18, 9,
    18, 9, 12, 9, 24, 2, 24, 4, 30, 6,
    18, 0, 18, 6, 18, 6, 12, 6, 18, 4,
    18, 4, 12, 4, 24, 2, 24, 9, 30, 2,
    255,
};

// Title-screen "MAZY2" logo bitmap - 5 chars x 4x4 VVram-cell glyph indices.
int mz2TitleBytes[80] = {
    0x0c, 0x07, 0x07, 0x0b, 0x0c, 0x03, 0x03, 0x0f,
    0x0c, 0x03, 0x03, 0x0f, 0x04, 0x01, 0x01, 0x05,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x04, 0x05, 0x0d, 0x07, 0x00, 0x08, 0x07, 0x00,
    0x08, 0x07, 0x00, 0x00, 0x04, 0x05, 0x05, 0x05,
    0x0c, 0x03, 0x0c, 0x03, 0x04, 0x0b, 0x0e, 0x01,
    0x00, 0x0c, 0x03, 0x00, 0x00, 0x04, 0x01, 0x00,
    0x08, 0x07, 0x05, 0x0b, 0x00, 0x08, 0x0e, 0x07,
    0x08, 0x0f, 0x05, 0x00, 0x04, 0x05, 0x05, 0x05,
};

// Stage/floor data - flattened from upstream's own nested `Stage{floorCount,
// pFloors}` / `Floor{bytes[10],stairCount,pStairs,monsterCount,pMonsters,
// knifeCount,pKnives}` struct hierarchy into parallel flat arrays addressed
// by one contiguous "global floor index" spanning all 8 stages' combined 28
// floors (mz2StageFloorStart[stage]+floorWithinStage), each with its own
// start+count pair into a shared flat resource array - see header comment.
int mz2StageFloorStart[8] = { 0, 2, 4, 7, 10, 14, 18, 23 };
int mz2StageFloorCount[8] = { 2, 2, 3, 3, 4, 4, 5, 5 };
int mz2StageStart[8] = { 0, 116, 4, 4, 0, 115, 0, 52 };
int mz2StageGoal[8] = { 116, 17, 96, 112, 115, 4, 97, 35 };

int mz2FloorBytes[28][10] = {
    { 153, 96, 96, 87, 179, 222, 140, 202, 238, 234 },
    { 162, 224, 133, 105, 117, 102, 198, 197, 234, 238 },
    { 200, 233, 103, 68, 204, 121, 177, 214, 171, 235 },
    { 194, 80, 165, 69, 117, 245, 145, 91, 175, 239 },
    { 152, 122, 102, 104, 184, 99, 154, 216, 234, 235 },
    { 88, 241, 165, 103, 49, 122, 165, 237, 234, 235 },
    { 152, 100, 105, 105, 153, 231, 150, 104, 234, 250 },
    { 41, 198, 152, 229, 113, 233, 177, 69, 235, 251 },
    { 58, 107, 204, 218, 162, 231, 113, 97, 186, 239 },
    { 165, 110, 154, 70, 49, 249, 113, 74, 250, 250 },
    { 146, 72, 172, 91, 170, 254, 161, 225, 174, 234 },
    { 10, 97, 89, 87, 58, 123, 161, 230, 239, 234 },
    { 21, 234, 37, 105, 107, 122, 225, 104, 171, 238 },
    { 74, 100, 156, 75, 164, 249, 161, 98, 171, 238 },
    { 129, 115, 21, 233, 229, 106, 101, 213, 187, 234 },
    { 161, 238, 25, 98, 110, 249, 230, 224, 170, 235 },
    { 232, 230, 42, 67, 92, 253, 21, 210, 186, 235 },
    { 136, 203, 21, 97, 230, 251, 198, 81, 170, 238 },
    { 34, 98, 221, 221, 60, 100, 98, 71, 170, 251 },
    { 17, 81, 221, 221, 197, 204, 197, 204, 238, 238 },
    { 161, 234, 197, 204, 229, 238, 197, 204, 238, 238 },
    { 162, 234, 204, 196, 230, 109, 204, 69, 238, 254 },
    { 168, 106, 204, 92, 228, 102, 105, 204, 238, 238 },
    { 72, 202, 21, 114, 230, 227, 172, 105, 234, 235 },
    { 154, 232, 197, 106, 138, 213, 86, 106, 234, 234 },
    { 168, 102, 171, 114, 161, 233, 105, 117, 234, 235 },
    { 168, 76, 137, 118, 229, 200, 172, 230, 171, 235 },
    { 168, 202, 134, 118, 152, 226, 156, 234, 174, 234 },
};

int mz2FloorStairStart[28] = {
    0, 2, 2, 7, 7, 9, 13, 13, 18, 26,
    26, 28, 30, 32, 32, 37, 41, 45, 45, 50,
    57, 62, 66, 66, 68, 70, 72, 75,
};
int mz2FloorStairCount[28] = {
    2, 0, 5, 0, 2, 4, 0, 5, 8, 0,
    2, 2, 2, 0, 5, 4, 4, 0, 5, 7,
    5, 4, 0, 2, 2, 2, 3, 0,
};
int mz2AllStairs[75] = {
    114, 52, 112, 1, 115, 4, 68, 48, 84, 96,
    18, 68, 116, 113, 66, 114, 52, 84, 0, 32,
    80, 65, 50, 3, 67, 100, 114, 3, 112, 4,
    3, 100, 0, 80, 112, 36, 116, 96, 2, 66,
    35, 64, 112, 114, 116, 17, 49, 81, 113, 18,
    0, 50, 82, 114, 52, 84, 116, 112, 49, 113,
    51, 83, 50, 82, 20, 116, 112, 68, 114, 4,
    1, 68, 97, 20, 116,
};

int mz2FloorMonsterStart[28] = {
    0, 4, 6, 8, 11, 15, 18, 22, 24, 28,
    29, 34, 35, 37, 39, 43, 46, 50, 53, 56,
    58, 59, 63, 64, 66, 69, 73, 76,
};
int mz2FloorMonsterCount[28] = {
    4, 2, 2, 3, 4, 3, 4, 2, 4, 1,
    5, 1, 2, 2, 4, 3, 4, 3, 3, 2,
    1, 4, 1, 2, 3, 4, 3, 4,
};
int mz2AllMonsters[160] = {
    64, 2, 112, 1, 97, 2, 99, 1, 20, 0,
    84, 2, 0, 1, 82, 2, 33, 2, 49, 2,
    113, 0, 32, 1, 80, 1, 81, 2, 68, 2,
    64, 2, 82, 2, 52, 2, 49, 1, 66, 2,
    51, 2, 4, 2, 50, 1, 82, 2, 64, 2,
    17, 2, 4, 2, 36, 2, 66, 0, 96, 1,
    65, 2, 68, 2, 84, 2, 100, 2, 99, 2,
    34, 2, 68, 1, 48, 1, 34, 2, 81, 1,
    2, 2, 3, 2, 83, 2, 16, 2, 32, 1,
    83, 0, 32, 2, 18, 2, 67, 0, 68, 0,
    81, 2, 2, 2, 68, 2, 67, 2, 52, 2,
    84, 2, 2, 2, 19, 2, 1, 2, 0, 2,
    65, 2, 18, 2, 99, 2, 48, 0, 96, 1,
    99, 2, 48, 2, 65, 2, 34, 2, 0, 2,
    65, 0, 18, 2, 116, 0, 65, 2, 83, 2,
    4, 2, 32, 2, 34, 2, 19, 2, 115, 2,
};

int mz2FloorKnifeStart[28] = {
    0, 2, 4, 6, 6, 8, 9, 10, 12, 12,
    13, 14, 15, 16, 17, 18, 19, 20, 21, 21,
    21, 22, 22, 23, 24, 24, 26, 26,
};
int mz2FloorKnifeCount[28] = {
    2, 2, 2, 0, 2, 1, 1, 2, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    1, 0, 1, 1, 0, 2, 0, 1,
};
int mz2AllKnives[27] = {
    16, 20, 96, 2, 80, 51, 34, 52, 97, 100,
    68, 116, 114, 82, 97, 98, 114, 35, 3, 48,
    65, 115, 84, 84, 96, 83, 50,
};


// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int mz2Score;
int mz2RemainCount;
int mz2CurrentStage;
int mz2StageIndex;
int mz2StageTime;
int mz2InitialTime;
int mz2HeldKnifeCount;
int mz2TimeDenom;
int mz2MonsterClock;

#define MZ2_MAX_TIME_DENOM ( 60 / ( 8 / MZ2_COORD_RATE ) )
#define MZ2_BONUS_RATE 2
#define MZ2_TICK_DIVISOR ( 8 / MZ2_COORD_RATE )
int mz2TickCounter;

int mz2CellMap[400];   // MZ2_ROW_COUNT * MZ2_COLUMN_COUNT reserved, only the
                        // first 40 entries ever used - see mz2CellIndex().
int mz2GoalX, mz2GoalY;
bool mz2Reached;

int mz2VVramBack[400];   // MZ2_VVRAM_WIDTH * MZ2_VVRAM_HEIGHT, flat, matching
int mz2VVramFront[400];  // upstream's own VVramOffset(x,y)=y*W+x addressing.

Mz2Sprite mz2Sprites[MZ2_SPRITE_END];

Mz2Movable mz2Man;
int mz2ManDirIndex;
bool mz2ManKeyOn;

#define MZ2_MAX_MONSTER_COUNT 16
#define MZ2_MAX_MONSTER_COUNT_OF_FLOOR 5
#define MZ2_MONSTER_VISIBLE 0x08
#define MZ2_MONSTER_FLOOR_MASK 0x70
#define MZ2_MONSTER_TYPE_MASK 0x03
#define MZ2_MONSTER_FLOOR_SHIFT 4
#define MZ2_TYPE_HORIZONTAL 0
#define MZ2_TYPE_VERTICAL 1
#define MZ2_TYPE_CHASER 2
Mz2Movable mz2Monsters[MZ2_MAX_MONSTER_COUNT];
int mz2CurrentFloor;
// mz2ChangeFloor() (and the mz2InitFloor() it calls) are defined later in
// this file than mz2MoveMan() - this dialect has no forward declarations,
// so a floor change requested from inside mz2MoveMan() is deferred via
// these two globals instead of reordering the whole render/visibility
// dependency chain those two functions need; mz2UpdatePlaying() (defined
// after both) checks and actually applies it right after calling
// mz2MoveMan(), same real-tick timing as a direct call would have had.
bool mz2FloorChangePending;
int mz2PendingFloor;

#define MZ2_MAX_KNIFE_COUNT 4
#define MZ2_KNIFE_VISIBLE 0x08
#define MZ2_KNIFE_MOVING 0x04
#define MZ2_KNIFE_FLOOR_MASK 0x70
#define MZ2_KNIFE_DIR_MASK 0x03
#define MZ2_KNIFE_FLOOR_SHIFT 4
Mz2Movable mz2Knives[MZ2_MAX_KNIFE_COUNT];
int mz2HeldKnifeIcons[4];   // MZ2_CHAR_KNIFE_LEFT or -1 (blank) per slot

#define MZ2_POINT_MAX_COUNT 4
#define MZ2_POINT_MAX_TIME 6
typedef struct { int x, y, c, time; } Mz2Point;
Mz2Point mz2Points[MZ2_POINT_MAX_COUNT];
int mz2PointRate;
int mz2PointValues[4] = { 10, 20, 40, 80 };

// whole-screen status/UI text overlay - see header comment. -1 = transparent
// (show the VVram-derived map content), 0-26 = an mz2AsciiPattern glyph index.
int mz2StatusChar[8][32];

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of MZ2_TICK_DIVISOR.
int mz2SeqMelody[3];
int mz2SeqPos[3];
int mz2SeqWait[3];
int mz2SeqActive[3];

#define MZ2_STATE_TITLE 0
#define MZ2_STATE_START_JINGLE 1
#define MZ2_STATE_PLAYING 2
#define MZ2_STATE_LOSE_ANIM 3
#define MZ2_STATE_GAMEOVER_JINGLE 4
#define MZ2_STATE_CLEAR_WAIT 5
#define MZ2_STATE_CLEAR_JINGLE 6
#define MZ2_STATE_BONUS_TALLY 7
int mz2State;
int mz2WaitFrames;
int mz2AnimStep;
int mz2TitleSelection;
bool mz2TitleSelectionChanged;
int mz2PrevLeft, mz2PrevRight, mz2PrevUp, mz2PrevDown, mz2PrevFire;

// direction lookup shared by Man movement and Knife throwing - index-aligned
// with MZ2_DIRECTION_LEFT/RIGHT/UP/DOWN (0-3), matching upstream's own
// shared Direction_Left/Right/Up/Down enum used by both Man's and Knife's
// own separate Direction tables.
int mz2DirDx[4] = { -1, 1, 0, 0 };
int mz2DirDy[4] = { 0, 0, -1, 1 };
int mz2ManPatternBase[4] = { 0, 4, 8, 12 };

int mz2CoordModTable[32] = {
    2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0,
};
int mz2ToGridTable[32] = {
    -1, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
};

// cell-type icon lookup, matching upstream's own Chars[] table (indexed by
// `cell & MZ2_CELL_TYPE_MASK`, which yields 0/4/8/12 - a direct base offset
// into this table, not a scaled index).
int mz2CellTypeIcons[16] = {
    MZ2_CHAR_SPACE, MZ2_CHAR_SPACE, MZ2_CHAR_SPACE, MZ2_CHAR_SPACE,
    MZ2_CHAR_STAIRUP+0, MZ2_CHAR_STAIRUP+1, MZ2_CHAR_STAIRUP+2, MZ2_CHAR_STAIRUP+3,
    MZ2_CHAR_STAIRDOWN+0, MZ2_CHAR_STAIRDOWN+1, MZ2_CHAR_STAIRDOWN+2, MZ2_CHAR_STAIRDOWN+3,
    MZ2_CHAR_GOAL+0, MZ2_CHAR_GOAL+1, MZ2_CHAR_GOAL+2, MZ2_CHAR_GOAL+3,
};


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int mz2Abs( int a, int b )
{
    if( a < b ) return b - a;
    return a - b;
}

int mz2Sign( int from, int to )
{
    if( from == to ) return 0;
    if( from < to ) return 1;
    return -1;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - coordinate helpers
// -----------------------------------------------------------------------------

int mz2CoordMod( int a )
{
    return mz2CoordModTable[ a ];
}

int mz2ToGrid( int a )
{
    return mz2ToGridTable[ a ];
}

int mz2ToCoord( int a )
{
    return 1 + ( a << 1 ) + a;
}

int mz2CellIndex( int column, int row )
{
    return row * MZ2_COLUMN_COUNT + column;
}

int mz2VOffset( int x, int y )
{
    return y * MZ2_VVRAM_WIDTH + x;
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void mz2LocateMovable( Mz2Movable* pMovable, int b )
{
    int column, row;
    column = b >> 4;
    row = b & 0x0f;
    pMovable->x = mz2ToCoord( column ) << MZ2_COORD_SHIFT;
    pMovable->y = mz2ToCoord( row ) << MZ2_COORD_SHIFT;
}

void mz2MoveMovable( Mz2Movable* pMovable )
{
    pMovable->x = pMovable->x + pMovable->dx;
    pMovable->y = pMovable->y + pMovable->dy;
}

bool mz2CanMove( Mz2Movable* pMovable, int dx, int dy )
{
    int x, y;
    x = pMovable->x;
    y = pMovable->y;
    if( dy == 0 )
    {
        int yMod, xMod;
        yMod = mz2CoordMod( y );
        if( yMod != 0 ) return false;
        xMod = mz2CoordMod( x );
        if( dx < 0 )
        {
            if( xMod == 0 )
            {
                int column, row;
                column = mz2ToGrid( x );
                if( column == 0 ) return false;
                row = mz2ToGrid( y );
                if( ( mz2CellMap[ mz2CellIndex(column,row) ] & MZ2_CELL_LEFT_WALL ) != 0 ) return false;
            }
            return true;
        }
        else
        {
            if( xMod == 0 )
            {
                int column, row;
                column = mz2ToGrid( x );
                if( column >= MZ2_COLUMN_COUNT - 1 ) return false;
                row = mz2ToGrid( y );
                if( ( mz2CellMap[ mz2CellIndex(column,row) ] & MZ2_CELL_RIGHT_WALL ) != 0 ) return false;
            }
            return true;
        }
    }
    else if( dx == 0 )
    {
        int xMod, yMod;
        xMod = mz2CoordMod( x );
        if( xMod != 0 ) return false;
        yMod = mz2CoordMod( y );
        if( dy < 0 )
        {
            if( yMod == 0 )
            {
                int row, column;
                row = mz2ToGrid( y );
                if( row == 0 ) return false;
                column = mz2ToGrid( x );
                if( ( mz2CellMap[ mz2CellIndex(column,row) ] & MZ2_CELL_TOP_WALL ) != 0 ) return false;
            }
            return true;
        }
        else
        {
            if( yMod == 0 )
            {
                int row, column;
                row = mz2ToGrid( y );
                if( row >= MZ2_ROW_COUNT - 1 ) return false;
                column = mz2ToGrid( x );
                if( ( mz2CellMap[ mz2CellIndex(column,row) ] & MZ2_CELL_BOTTOM_WALL ) != 0 ) return false;
            }
            return true;
        }
    }
    return true;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void mz2HideAllSprites()
{
    int i;
    for( i = 0; i < MZ2_SPRITE_END; i = i + 1 )
      mz2Sprites[ i ].pattern = MZ2_INVALID_PATTERN;
}

void mz2ShowSprite( Mz2Movable* pMovable, int pattern )
{
    mz2Sprites[ pMovable->sprite ].x = pMovable->x;
    mz2Sprites[ pMovable->sprite ].y = pMovable->y;
    mz2Sprites[ pMovable->sprite ].pattern = pattern;
}

void mz2HideSprite( int index )
{
    mz2Sprites[ index ].pattern = MZ2_INVALID_PATTERN;
}

void mz2DrawSprites()
{
    int i;
    for( i = 0; i < MZ2_SPRITE_END; i = i + 1 )
    {
        if( mz2Sprites[i].pattern != MZ2_INVALID_PATTERN )
        {
            int idx, c;
            idx = mz2VOffset( mz2Sprites[i].x, mz2Sprites[i].y );
            c = mz2Sprites[i].pattern;
            if( c < MZ2_CHAR_MAN )
              mz2VVramFront[ idx ] = c;
            else
            {
                int row;
                for( row = 0; row < 2; row = row + 1 )
                {
                    int col;
                    for( col = 0; col < 2; col = col + 1 )
                    {
                        mz2VVramFront[ idx ] = c;
                        idx = idx + 1;
                        c = c + 1;
                    }
                    idx = idx + MZ2_VVRAM_WIDTH - 2;
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Print.cpp / Vram.cpp
// -----------------------------------------------------------------------------

int mz2AsciiIndex( int c )
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

int mz2PrintC( int page, int col, int c )
{
    mz2StatusChar[ page ][ col ] = mz2AsciiIndex( c );
    return col + 1;
}

int mz2PrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = mz2PrintC( page, col, s[ i ] );
    return col;
}

void mz2PrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      mz2PrintC( page, col, ' ' );
    else
      mz2PrintC( page, col, d1 + '0' );
    mz2PrintC( page, col + 1, ( b % 10 ) + '0' );
}

void mz2PrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        mz2PrintC( page, col, ' ' );
        if( d2 == 0 )
          mz2PrintC( page, col + 1, ' ' );
        else
          mz2PrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        mz2PrintC( page, col, d1 + '0' );
        mz2PrintC( page, col + 1, d2 + '0' );
    }
    mz2PrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void mz2PrintNumber5( int page, int col, int w )
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
          mz2PrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            mz2PrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    mz2PrintC( page, col + 4, rem + '0' );
}

void mz2ClearStatusChar()
{
    int page, col;
    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 32; col = col + 1 )
          mz2StatusChar[ page ][ col ] = -1;
    }
}

void mz2ClearScreen()
{
    int i;
    for( i = 0; i < MZ2_VVRAM_WIDTH * MZ2_VVRAM_HEIGHT; i = i + 1 )
      mz2VVramFront[ i ] = MZ2_CHAR_SPACE;
    mz2ClearStatusChar();
}


// -----------------------------------------------------------------------------
//   Status.cpp
// -----------------------------------------------------------------------------

void mz2PrintScore()
{
    mz2PrintNumber5( 1, MZ2_LEFT_X + 2, mz2Score );
    mz2PrintC( 1, MZ2_LEFT_X + 7, '0' );
}

void mz2PrintTime()
{
    mz2PrintByteNumber3( 5, MZ2_LEFT_X + 5, mz2StageTime );
}

void mz2PrintHeldKnives()
{
    int i;
    for( i = 0; i < 4; i = i + 1 )
    {
        if( i < mz2HeldKnifeCount )
          mz2HeldKnifeIcons[ i ] = MZ2_CHAR_KNIFE_LEFT;
        else
          mz2HeldKnifeIcons[ i ] = -1;
    }
}

void mz2PrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };

    mz2PrintS( 0, MZ2_LEFT_X, sScore, 5 );
    mz2PrintS( 3, MZ2_LEFT_X, sStage, 5 );
    mz2PrintByteNumber2( 3, MZ2_LEFT_X + 6, mz2CurrentStage + 1 );
    mz2PrintS( 5, MZ2_LEFT_X, sTime, 4 );

    if( mz2RemainCount > 1 )
    {
        // Upstream draws a real 2x2 Char_Remain (Char_Man) icon per
        // remaining life via Put2C, falling back to an icon+digit summary
        // once more than 2 remain - simplified throughout to a single
        // plain digit (matching Cracky's own established "status-text-
        // only lives display" simplification), always showing the real
        // count rather than reproducing Cracky's own blank-space
        // placeholder for its own low-count branch - see header comment.
        int rc;
        rc = mz2RemainCount - 1;
        mz2PrintC( 7, MZ2_LEFT_X, ' ' );
        mz2PrintC( 7, MZ2_LEFT_X + 1, ' ' );
        mz2PrintC( 7, MZ2_LEFT_X + 2, rc + '0' );
    }
    else
    {
        mz2PrintC( 7, MZ2_LEFT_X, ' ' );
        mz2PrintC( 7, MZ2_LEFT_X + 1, ' ' );
        mz2PrintC( 7, MZ2_LEFT_X + 2, ' ' );
    }

    mz2PrintScore();
    mz2PrintTime();
    mz2PrintHeldKnives();
}

void mz2PrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    mz2PrintS( 4, 8, s, 9 );
}

void mz2PrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    mz2PrintS( 4, 9, s, 7 );
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void mz2AddScore( int pts )
{
    mz2Score = mz2Score + pts;
    mz2PrintScore();
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void mz2InitPoints()
{
    int i;
    for( i = 0; i < MZ2_POINT_MAX_COUNT; i = i + 1 )
      mz2Points[ i ].time = 0;
}

void mz2StartPoint( int x, int y )
{
    int i;
    mz2AddScore( mz2PointValues[ mz2PointRate ] );
    for( i = 0; i < MZ2_POINT_MAX_COUNT; i = i + 1 )
    {
        if( mz2Points[i].time == 0 )
        {
            mz2Points[i].time = MZ2_POINT_MAX_TIME << MZ2_COORD_SHIFT;
            if( x > MZ2_WINDOW_WIDTH - 3 )
              x = MZ2_WINDOW_WIDTH - 3;
            mz2Points[i].x = x;
            mz2Points[i].y = y;
            mz2Points[i].c = MZ2_CHAR_POINT + ( mz2PointRate << 1 );
            if( mz2PointRate < 4 - 1 )
              mz2PointRate = mz2PointRate + 1;
            return;
        }
    }
}

void mz2UpdatePoints()
{
    int i;
    for( i = 0; i < MZ2_POINT_MAX_COUNT; i = i + 1 )
    {
        if( mz2Points[i].time != 0 )
          mz2Points[i].time = mz2Points[i].time - 1;
    }
}

int mz2PointPutC( int cursor, int c )
{
    int i;
    for( i = 0; i < 2; i = i + 1 )
    {
        mz2VVramFront[ cursor ] = c;
        cursor = cursor + 1;
        cursor = cursor + MZ2_VVRAM_WIDTH - 1;
        c = c + 1;
    }
    return cursor + 1 - MZ2_VVRAM_WIDTH * 2;
}

void mz2DrawPoints()
{
    int i;
    for( i = 0; i < MZ2_POINT_MAX_COUNT; i = i + 1 )
    {
        if( mz2Points[i].time != 0 )
        {
            int cursor, j;
            cursor = mz2VOffset( mz2Points[i].x, mz2Points[i].y );
            cursor = mz2PointPutC( cursor, mz2Points[i].c );
            for( j = 0; j < 2; j = j + 1 )
              cursor = mz2PointPutC( cursor, MZ2_CHAR_POINT + 4*2 );
        }
    }
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int mz2MelodyLength( int id )
{
    if( id == MZ2_MELODY_LOOSE ) return 3;
    if( id == MZ2_MELODY_HIT ) return 17;
    if( id == MZ2_MELODY_BEEP ) return 3;
    if( id == MZ2_MELODY_GET ) return 3;
    if( id == MZ2_MELODY_START ) return 11;
    if( id == MZ2_MELODY_CLEAR ) return 21;
    if( id == MZ2_MELODY_GAMEOVER ) return 23;
    if( id == MZ2_MELODY_BGM1 ) return 89;
    if( id == MZ2_MELODY_BGM2 ) return 41;
    return 0;
}

int mz2MelodyValue( int id, int idx )
{
    if( id == MZ2_MELODY_LOOSE ) return mz2MelodyLoose[ idx ];
    if( id == MZ2_MELODY_HIT ) return mz2MelodyHit[ idx ];
    if( id == MZ2_MELODY_BEEP ) return mz2MelodyBeep[ idx ];
    if( id == MZ2_MELODY_GET ) return mz2MelodyGet[ idx ];
    if( id == MZ2_MELODY_START ) return mz2MelodyStart[ idx ];
    if( id == MZ2_MELODY_CLEAR ) return mz2MelodyClear[ idx ];
    if( id == MZ2_MELODY_GAMEOVER ) return mz2MelodyGameOver[ idx ];
    if( id == MZ2_MELODY_BGM1 ) return mz2MelodyBgm1[ idx ];
    if( id == MZ2_MELODY_BGM2 ) return mz2MelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/MZ2_TEMPO real 60Hz ticks - see header comment.
int mz2NoteFrames( int length )
{
    return (int)( length * ( 300.0 / MZ2_TEMPO ) + 0.5 );
}

void mz2StartSeq( int channel, int melodyId )
{
    mz2SeqMelody[ channel ] = melodyId;
    mz2SeqPos[ channel ] = 0;
    mz2SeqWait[ channel ] = 0;
    mz2SeqActive[ channel ] = 1;
}

void mz2StopSeq( int channel )
{
    mz2SeqActive[ channel ] = 0;
    mz2SeqMelody[ channel ] = MZ2_MELODY_NONE;
}

bool mz2SeqPlaying( int channel )
{
    return mz2SeqActive[ channel ] != 0;
}

void mz2AdvanceOneSeq( int channel )
{
    int length, note;

    if( mz2SeqActive[ channel ] == 0 ) return;

    if( mz2SeqWait[ channel ] > 0 )
    {
        mz2SeqWait[ channel ] = mz2SeqWait[ channel ] - 1;
        return;
    }

    length = mz2MelodyValue( mz2SeqMelody[ channel ], mz2SeqPos[ channel ] );
    if( length == 0 )
    {
        mz2StopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        mz2SeqPos[ channel ] = 0;
        length = mz2MelodyValue( mz2SeqMelody[ channel ], 0 );
    }
    note = mz2MelodyValue( mz2SeqMelody[ channel ], mz2SeqPos[ channel ] + 1 );
    mz2SeqPos[ channel ] = mz2SeqPos[ channel ] + 2;
    mz2SeqWait[ channel ] = mz2NoteFrames( length );
    if( note != 0 )
      md_playTone( (float)mz2Frequencies[ note - 1 ], (float)mz2SeqWait[ channel ] / 60.0 );
}

void mz2AdvanceSound()
{
    mz2AdvanceOneSeq( 0 );
    mz2AdvanceOneSeq( 1 );
    mz2AdvanceOneSeq( 2 );
}

void mz2StartBgm()
{
    mz2StartSeq( 1, MZ2_MELODY_BGM1 );
    mz2StartSeq( 2, MZ2_MELODY_BGM2 );
}

void mz2StopBgm()
{
    mz2StopSeq( 1 );
    mz2StopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   DrawAll - composites sprites/points onto the per-floor static map,
//   matching upstream's own VVramBackToFront()+DrawSprites()+DrawPoints().
// -----------------------------------------------------------------------------

void mz2DrawAll()
{
    int i;
    for( i = 0; i < MZ2_VVRAM_WIDTH * MZ2_VVRAM_HEIGHT; i = i + 1 )
      mz2VVramFront[ i ] = mz2VVramBack[ i ];
    mz2DrawSprites();
    mz2DrawPoints();
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void mz2UpdateMonsterVisibility( Mz2Movable* pMonster )
{
    int cell;
    cell = mz2CellMap[ mz2CellIndex( mz2ToGrid(pMonster->x), mz2ToGrid(pMonster->y) ) ];
    if( ( cell & MZ2_CELL_VISIBLE ) != 0 )
      pMonster->status = pMonster->status | MZ2_MONSTER_VISIBLE;
    else
      pMonster->status = pMonster->status & ~MZ2_MONSTER_VISIBLE;
}

void mz2ShowMonster( Mz2Movable* pMonster )
{
    int status, pattern;
    status = pMonster->status;
    if( ( status & MZ2_MONSTER_VISIBLE ) != 0 )
    {
        if( ( status & MZ2_MONSTER_TYPE_MASK ) == MZ2_TYPE_CHASER )
          pattern = MZ2_CHAR_CHASER;
        else
          pattern = MZ2_CHAR_DISTURBER;
        mz2ShowSprite( pMonster, pattern );
    }
    else
      mz2HideSprite( pMonster->sprite );
}

void mz2InitMonsters()
{
    int monsterI, floor, gfi, floorCount;
    monsterI = 0;
    floor = 0;
    floorCount = mz2StageFloorCount[ mz2StageIndex ];
    gfi = mz2StageFloorStart[ mz2StageIndex ];
    while( floor < floorCount )
    {
        int count, start, k, sprite;
        sprite = MZ2_SPRITE_MONSTER;
        count = mz2FloorMonsterCount[ gfi ];
        start = mz2FloorMonsterStart[ gfi ];
        k = 0;
        while( k < count )
        {
            int position, type;
            mz2Monsters[ monsterI ].sprite = sprite;
            sprite = sprite + 1;
            position = mz2AllMonsters[ ( start + k ) * 2 ];
            type = mz2AllMonsters[ ( start + k ) * 2 + 1 ];
            mz2LocateMovable( &mz2Monsters[ monsterI ], position );
            mz2Monsters[ monsterI ].status = MZ2_MOVABLE_LIVE | type | ( floor << MZ2_MONSTER_FLOOR_SHIFT );
            monsterI = monsterI + 1;
            k = k + 1;
        }
        floor = floor + 1;
        gfi = gfi + 1;
    }
    while( monsterI < MZ2_MAX_MONSTER_COUNT )
    {
        mz2Monsters[ monsterI ].status = 0;
        monsterI = monsterI + 1;
    }
}

void mz2ShowMonsters()
{
    int sprite, i, floorFilter;
    sprite = MZ2_SPRITE_MONSTER;
    for( i = 0; i < MZ2_MAX_MONSTER_COUNT_OF_FLOOR; i = i + 1 )
    {
        mz2HideSprite( sprite );
        sprite = sprite + 1;
    }
    floorFilter = ( mz2CurrentFloor << MZ2_MONSTER_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE;
    for( i = 0; i < MZ2_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( mz2Monsters[i].status & ( MZ2_MOVABLE_LIVE | MZ2_MONSTER_FLOOR_MASK ) ) == floorFilter )
        {
            mz2UpdateMonsterVisibility( &mz2Monsters[i] );
            mz2ShowMonster( &mz2Monsters[i] );
        }
    }
}

bool mz2IsNearNext( int monsterIdx, int dx, int dy )
{
    int x, y, floorFilter, i;
    x = mz2Monsters[monsterIdx].x + dx;
    y = mz2Monsters[monsterIdx].y + dy;
    floorFilter = ( mz2CurrentFloor << MZ2_MONSTER_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE;
    for( i = 0; i < MZ2_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( i != monsterIdx &&
            ( mz2Monsters[i].status & ( MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE | MZ2_MONSTER_FLOOR_MASK ) ) == floorFilter )
        {
            int ox, oy;
            ox = mz2Monsters[i].x;
            oy = mz2Monsters[i].y;
            if( x + MZ2_COORD_RATE*2-1 >= ox && ox + MZ2_COORD_RATE*2-1 >= x &&
                y + MZ2_COORD_RATE*2-1 >= oy && oy + MZ2_COORD_RATE*2-1 >= y )
              return true;
        }
    }
    return false;
}

void mz2MoveMonsters()
{
    int floorFilter, i;
    floorFilter = ( mz2CurrentFloor << MZ2_MONSTER_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE;
    for( i = 0; i < MZ2_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = mz2Monsters[i].status;
        if( ( status & ( MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE | MZ2_MONSTER_FLOOR_MASK ) ) == floorFilter )
        {
            if( ( ( mz2Monsters[i].x | mz2Monsters[i].y ) & MZ2_COORD_MASK ) == 0 )
            {
                int dx, dy, type;
                dx = 0; dy = 0;
                type = status & MZ2_MONSTER_TYPE_MASK;
                if( type == MZ2_TYPE_HORIZONTAL )
                  dx = mz2Sign( mz2Monsters[i].x, mz2Man.x );
                else if( type == MZ2_TYPE_VERTICAL )
                  dy = mz2Sign( mz2Monsters[i].y, mz2Man.y );
                else
                {
                    dx = mz2Sign( mz2Monsters[i].x, mz2Man.x );
                    if( dx == 0 || !mz2CanMove( &mz2Monsters[i], dx, dy ) )
                    {
                        dx = 0;
                        dy = mz2Sign( mz2Monsters[i].y, mz2Man.y );
                    }
                }
                if( !mz2CanMove( &mz2Monsters[i], dx, dy ) || mz2IsNearNext( i, dx, dy ) )
                {
                    dx = 0;
                    dy = 0;
                }
                mz2Monsters[i].dx = dx;
                mz2Monsters[i].dy = dy;
            }
            if( ( mz2MonsterClock & 1 ) == 0 || ( status & MZ2_MONSTER_TYPE_MASK ) != MZ2_TYPE_CHASER )
            {
                mz2MoveMovable( &mz2Monsters[i] );
                mz2ShowMonster( &mz2Monsters[i] );
                if( ( ( mz2Monsters[i].x | mz2Monsters[i].y ) & MZ2_COORD_MASK ) == 0 &&
                    mz2Man.x + MZ2_HIT_RANGE >= mz2Monsters[i].x && mz2Monsters[i].x + MZ2_HIT_RANGE >= mz2Man.x &&
                    mz2Man.y + MZ2_HIT_RANGE >= mz2Monsters[i].y && mz2Monsters[i].y + MZ2_HIT_RANGE >= mz2Man.y )
                {
                    mz2Man.status = mz2Man.status & ~MZ2_MOVABLE_LIVE;
                }
            }
        }
    }
    mz2MonsterClock = mz2MonsterClock + 1;
}

bool mz2HitMonsters( Mz2Movable* pKnife )
{
    int floorFilter, i;
    floorFilter = ( mz2CurrentFloor << MZ2_MONSTER_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE;
    for( i = 0; i < MZ2_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( mz2Monsters[i].status & ( MZ2_MOVABLE_LIVE | MZ2_MONSTER_VISIBLE | MZ2_MONSTER_FLOOR_MASK ) ) == floorFilter )
        {
            if( pKnife->x + MZ2_HIT_RANGE/2 >= mz2Monsters[i].x && mz2Monsters[i].x + MZ2_HIT_RANGE >= pKnife->x &&
                pKnife->y + MZ2_HIT_RANGE/2 >= mz2Monsters[i].y && mz2Monsters[i].y + MZ2_HIT_RANGE >= pKnife->y )
            {
                mz2StartSeq( 0, MZ2_MELODY_HIT );
                mz2Monsters[i].status = mz2Monsters[i].status & ~MZ2_MOVABLE_LIVE;
                mz2StartPoint( mz2Monsters[i].x, mz2Monsters[i].y );
                mz2HideSprite( mz2Monsters[i].sprite );
                return true;
            }
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Knife.cpp
// -----------------------------------------------------------------------------

int mz2DirectionToGoal( Mz2Movable* pKnife )
{
    int x, y;
    x = pKnife->x >> MZ2_COORD_SHIFT;
    y = pKnife->y >> MZ2_COORD_SHIFT;
    if( mz2Abs( mz2GoalX, x ) >= mz2Abs( mz2GoalY, y ) )
    {
        if( mz2GoalX < x ) return MZ2_DIRECTION_LEFT;
        return MZ2_DIRECTION_RIGHT;
    }
    else
    {
        if( mz2GoalY < y ) return MZ2_DIRECTION_UP;
        return MZ2_DIRECTION_DOWN;
    }
}

void mz2UpdateKnifeVisibility( Mz2Movable* pKnife )
{
    int cell;
    cell = mz2CellMap[ mz2CellIndex( mz2ToGrid(pKnife->x), mz2ToGrid(pKnife->y) ) ];
    if( ( cell & MZ2_CELL_VISIBLE ) != 0 )
      pKnife->status = pKnife->status | MZ2_KNIFE_VISIBLE;
    else
      pKnife->status = pKnife->status & ~MZ2_KNIFE_VISIBLE;
}

void mz2ShowKnife( Mz2Movable* pKnife )
{
    int status;
    status = pKnife->status;
    if( ( status & ( MZ2_KNIFE_VISIBLE | MZ2_KNIFE_MOVING ) ) != 0 )
    {
        int pattern;
        pattern = MZ2_CHAR_KNIFE + ( status & MZ2_KNIFE_DIR_MASK );
        mz2ShowSprite( pKnife, pattern );
    }
    else
      mz2HideSprite( pKnife->sprite );
}

void mz2InitKnives()
{
    int knifeI, floor, gfi, floorCount, sprite;
    knifeI = 0;
    floor = 0;
    floorCount = mz2StageFloorCount[ mz2StageIndex ];
    gfi = mz2StageFloorStart[ mz2StageIndex ];
    sprite = MZ2_SPRITE_KNIFE;
    while( floor < floorCount )
    {
        int count, start, k;
        count = mz2FloorKnifeCount[ gfi ];
        start = mz2FloorKnifeStart[ gfi ];
        k = 0;
        while( k < count )
        {
            int position;
            mz2Knives[ knifeI ].sprite = sprite;
            sprite = sprite + 1;
            position = mz2AllKnives[ start + k ];
            mz2LocateMovable( &mz2Knives[ knifeI ], position );
            mz2Knives[ knifeI ].status = MZ2_MOVABLE_LIVE | ( floor << MZ2_KNIFE_FLOOR_SHIFT ) | mz2DirectionToGoal( &mz2Knives[knifeI] );
            knifeI = knifeI + 1;
            k = k + 1;
        }
        floor = floor + 1;
        gfi = gfi + 1;
    }
    while( knifeI < MZ2_MAX_KNIFE_COUNT )
    {
        mz2Knives[ knifeI ].status = 0;
        knifeI = knifeI + 1;
    }
    mz2HeldKnifeCount = 0;
}

void mz2ShowKnives()
{
    int sprite, i, floorFilter;
    sprite = MZ2_SPRITE_KNIFE;
    for( i = 0; i < MZ2_MAX_KNIFE_COUNT; i = i + 1 )
    {
        mz2HideSprite( sprite );
        sprite = sprite + 1;
    }
    floorFilter = ( mz2CurrentFloor << MZ2_KNIFE_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE;
    for( i = 0; i < MZ2_MAX_KNIFE_COUNT; i = i + 1 )
    {
        if( ( mz2Knives[i].status & ( MZ2_MOVABLE_LIVE | MZ2_KNIFE_FLOOR_MASK ) ) == floorFilter )
        {
            mz2UpdateKnifeVisibility( &mz2Knives[i] );
            mz2ShowKnife( &mz2Knives[i] );
        }
    }
}

void mz2PickupKnife()
{
    int floorFilter, manX, manY, i;
    floorFilter = ( mz2CurrentFloor << MZ2_KNIFE_FLOOR_SHIFT ) | MZ2_MOVABLE_LIVE | MZ2_KNIFE_VISIBLE;
    manX = mz2Man.x >> MZ2_COORD_SHIFT;
    manY = mz2Man.y >> MZ2_COORD_SHIFT;
    for( i = 0; i < MZ2_MAX_KNIFE_COUNT; i = i + 1 )
    {
        int status;
        status = mz2Knives[i].status;
        if( ( status & ( MZ2_MOVABLE_LIVE | MZ2_KNIFE_FLOOR_MASK | MZ2_KNIFE_VISIBLE | MZ2_KNIFE_MOVING ) ) == floorFilter )
        {
            int x;
            x = mz2Knives[i].x >> MZ2_COORD_SHIFT;
            if( manX + 1 >= x && x >= manX )
            {
                int y;
                y = mz2Knives[i].y >> MZ2_COORD_SHIFT;
                if( manY + 1 >= y && y >= manY )
                {
                    mz2HeldKnifeCount = mz2HeldKnifeCount + 1;
                    mz2Knives[i].status = status & ~( MZ2_MOVABLE_LIVE | MZ2_KNIFE_VISIBLE );
                    mz2StartSeq( 0, MZ2_MELODY_GET );
                    mz2HideSprite( mz2Knives[i].sprite );
                    mz2PrintHeldKnives();
                    mz2PointRate = 0;
                }
            }
        }
    }
}

// Flattened from upstream's own goto-chained Hit() - determines whether the
// knife's own leading edge is blocked by a wall (in the direction it's
// currently flying), or has hit a monster; either way, resets it to a
// stationary, ground-level, goal-pointing knife. Note `blocked ||
// mz2HitMonsters(pKnife)` relies on short-circuit evaluation to skip the
// monster-hit check entirely when a wall was already hit, matching
// upstream's own `goto stop` (which jumps PAST the `if (HitMonsters(...))`
// check) rather than always evaluating both.
void mz2KnifeHit( int knifeIdx )
{
    Mz2Movable* pKnife;
    int x, y;
    pKnife = &mz2Knives[ knifeIdx ];
    x = pKnife->x;
    y = pKnife->y;
    if( ( ( x | y ) & MZ2_COORD_MASK ) == 0 )
    {
        int cell, dx;
        bool blocked;
        cell = mz2CellMap[ mz2CellIndex( mz2ToGrid(x), mz2ToGrid(y) ) ];
        dx = pKnife->dx;
        blocked = false;
        if( dx != 0 )
        {
            if( dx > 0 )
            {
                if( mz2CoordMod(x) == 1 && ( cell & MZ2_CELL_RIGHT_WALL ) != 0 ) blocked = true;
            }
            else
            {
                if( mz2CoordMod(x) == 0 && ( cell & MZ2_CELL_LEFT_WALL ) != 0 ) blocked = true;
            }
        }
        else
        {
            int dy;
            dy = pKnife->dy;
            if( dy > 0 )
            {
                if( mz2CoordMod(y) == 1 && ( cell & MZ2_CELL_BOTTOM_WALL ) != 0 ) blocked = true;
            }
            else
            {
                if( mz2CoordMod(y) == 0 && ( cell & MZ2_CELL_TOP_WALL ) != 0 ) blocked = true;
            }
        }
        if( blocked || mz2HitMonsters( pKnife ) )
        {
            pKnife->status = MZ2_MOVABLE_LIVE | MZ2_KNIFE_VISIBLE | ( mz2CurrentFloor << MZ2_KNIFE_FLOOR_SHIFT ) | mz2DirectionToGoal( pKnife );
        }
    }
}

void mz2StartKnife()
{
    int i;
    if( mz2HeldKnifeCount == 0 ) return;
    for( i = 0; i < MZ2_MAX_KNIFE_COUNT; i = i + 1 )
    {
        if( ( mz2Knives[i].status & MZ2_MOVABLE_LIVE ) == 0 )
        {
            int direction, x, y;
            direction = ( mz2Man.status >> 2 ) & MZ2_KNIFE_DIR_MASK;
            mz2Knives[i].dx = mz2DirDx[ direction ];
            x = mz2Man.x;
            if( mz2Knives[i].dx > 0 ) x = x + MZ2_COORD_RATE;
            mz2Knives[i].x = x;
            mz2Knives[i].dy = mz2DirDy[ direction ];
            y = mz2Man.y;
            if( mz2Knives[i].dy > 0 ) y = y + MZ2_COORD_RATE;
            mz2Knives[i].y = y;
            mz2Knives[i].status = MZ2_MOVABLE_LIVE | MZ2_KNIFE_MOVING | direction;
            mz2HeldKnifeCount = mz2HeldKnifeCount - 1;
            mz2StartSeq( 0, MZ2_MELODY_GET );
            mz2PrintHeldKnives();
            mz2KnifeHit( i );
            mz2ShowKnife( &mz2Knives[i] );
            return;
        }
    }
}

void mz2MoveKnives()
{
    int i;
    for( i = 0; i < MZ2_MAX_KNIFE_COUNT; i = i + 1 )
    {
        if( ( mz2Knives[i].status & ( MZ2_MOVABLE_LIVE | MZ2_KNIFE_MOVING ) ) == ( MZ2_MOVABLE_LIVE | MZ2_KNIFE_MOVING ) )
        {
            mz2MoveMovable( &mz2Knives[i] );
            mz2KnifeHit( i );
            mz2ShowKnife( &mz2Knives[i] );
        }
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

#define MZ2_PATTERN_MASK 0x0f

void mz2ShowMan()
{
    int pattern, seq;
    pattern = mz2Man.status & MZ2_PATTERN_MASK;
    if( mz2Man.dx != 0 || mz2Man.dy != 0 )
    {
        seq = ( ( mz2Man.x + mz2Man.y ) >> MZ2_COORD_SHIFT ) & 3;
        if( seq == 3 ) seq = 1;
        pattern = pattern + seq + 1;
    }
    mz2ShowSprite( &mz2Man, MZ2_CHAR_MAN + ( pattern << 2 ) );
}

void mz2InitMan()
{
    mz2Man.sprite = MZ2_SPRITE_MAN;
    mz2Man.status = MZ2_MOVABLE_LIVE;
    mz2Man.dx = 0;
    mz2Man.dy = 0;
    mz2ManDirIndex = MZ2_DIRECTION_LEFT;
    mz2LocateMovable( &mz2Man, mz2StageStart[ mz2StageIndex ] );
    mz2ShowMan();
}

void mz2MoveMan()
{
    int dx, dy, pattern;
    bool moved;
    int i;

    dx = 0; dy = 0; pattern = mz2Man.status & MZ2_PATTERN_MASK;

    if( ( ( mz2Man.x | mz2Man.y ) & MZ2_COORD_MASK ) == 0 )
    {
        bool left, right, up, down, fire;
        left = isLeftPressed(); right = isRightPressed();
        up = isUpPressed(); down = isDownPressed();
        fire = isFirePressed();

        moved = false;

        if( left || right || up || down )
        {
            for( i = 0; i < 4 && !moved; i = i + 1 )
            {
                bool pressed;
                if( i == MZ2_DIRECTION_LEFT ) pressed = left;
                else if( i == MZ2_DIRECTION_RIGHT ) pressed = right;
                else if( i == MZ2_DIRECTION_UP ) pressed = up;
                else pressed = down;

                if( pressed )
                {
                    if( mz2CanMove( &mz2Man, mz2DirDx[i], mz2DirDy[i] ) )
                    {
                        mz2ManDirIndex = i;
                        moved = true;
                    }
                    else if( mz2CanMove( &mz2Man, mz2DirDx[mz2ManDirIndex], mz2DirDy[mz2ManDirIndex] ) )
                      moved = true;
                }
            }
        }

        if( moved )
        {
            dx = mz2DirDx[ mz2ManDirIndex ];
            dy = mz2DirDy[ mz2ManDirIndex ];
            pattern = mz2ManPatternBase[ mz2ManDirIndex ];
        }

        mz2Man.dx = dx;
        mz2Man.dy = dy;
        mz2Man.status = ( mz2Man.status & ~MZ2_PATTERN_MASK ) | pattern;

        if( fire )
        {
            if( !mz2ManKeyOn )
            {
                mz2StartKnife();
                mz2ManKeyOn = true;
            }
        }
        else
          mz2ManKeyOn = false;
    }

    mz2MoveMovable( &mz2Man );
    mz2ShowMan();

    if( ( ( mz2Man.x | mz2Man.y ) & MZ2_COORD_MASK ) == 0 )
    {
        mz2PickupKnife();
        if( ( mz2Man.dx != 0 || mz2Man.dy != 0 ) &&
            mz2CoordMod( mz2Man.x ) == 0 && mz2CoordMod( mz2Man.y ) == 0 )
        {
            int cellType;
            cellType = mz2CellMap[ mz2CellIndex( mz2ToGrid(mz2Man.x), mz2ToGrid(mz2Man.y) ) ] & MZ2_CELL_TYPE_MASK;
            if( cellType == MZ2_CELL_DOWN_STAIR )
            {
                mz2FloorChangePending = true;
                mz2PendingFloor = mz2CurrentFloor + 1;
            }
            else if( cellType == MZ2_CELL_UP_STAIR )
            {
                mz2FloorChangePending = true;
                mz2PendingFloor = mz2CurrentFloor - 1;
            }
            else if( cellType == MZ2_CELL_GOAL )
              mz2Reached = true;
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - floor init/flood-fill/render, and ChangeFloor
// -----------------------------------------------------------------------------

bool mz2SetCellVisible( int idx )
{
    int cell;
    // defensive bound - verified unnecessary against every one of the 28
    // real shipped floors' own wall data (the outer maze boundary is
    // always genuinely walled), kept as a cheap safety net - see header.
    if( idx < 0 || idx >= MZ2_COLUMN_COUNT * MZ2_ROW_COUNT ) return false;
    cell = mz2CellMap[ idx ];
    if( ( cell & MZ2_CELL_VISIBLE ) == 0 )
    {
        mz2CellMap[ idx ] = cell | MZ2_CELL_NEXT_SCAN;
        return true;
    }
    return false;
}

// Converges to the same final Visible set as upstream's own restart-on-any-
// change flood-fill, via a cheaper "repeat a full pass until no changes"
// shape - see header comment.
void mz2FloodFillVisibility()
{
    bool changed;
    int idx;
    do
    {
        changed = false;
        for( idx = 0; idx < MZ2_COLUMN_COUNT * MZ2_ROW_COUNT; idx = idx + 1 )
        {
            int cell;
            cell = mz2CellMap[ idx ];
            if( ( cell & MZ2_CELL_NEXT_SCAN ) != 0 )
            {
                cell = cell | MZ2_CELL_VISIBLE;
                cell = cell & ~MZ2_CELL_NEXT_SCAN;
                mz2CellMap[ idx ] = cell;
                changed = true;
                if( ( cell & MZ2_CELL_TOP_WALL ) == 0 )
                  mz2SetCellVisible( idx - MZ2_COLUMN_COUNT );
                if( ( cell & MZ2_CELL_BOTTOM_WALL ) == 0 )
                  mz2SetCellVisible( idx + MZ2_COLUMN_COUNT );
                if( ( cell & MZ2_CELL_LEFT_WALL ) == 0 )
                  mz2SetCellVisible( idx - 1 );
                if( ( cell & MZ2_CELL_RIGHT_WALL ) == 0 )
                  mz2SetCellVisible( idx + 1 );
            }
        }
    } while( changed );
}

// Direct structural mirror of upstream's own pointer-walk map render - see
// header comment for why a literal cursor-based port was chosen over a
// re-derived row/col closed form.
void mz2RenderFloorIntoBack()
{
    int cursor, row, col, i, mapIdx;

    cursor = 0;
    for( i = 0; i < 1 + 3 * MZ2_COLUMN_COUNT; i = i + 1 )
    {
        mz2VVramBack[ cursor ] = MZ2_CHAR_SPACE;
        cursor = cursor + 1;
    }
    cursor = 0;

    mapIdx = 0;
    for( row = 0; row < MZ2_ROW_COUNT; row = row + 1 )
    {
        for( i = 0; i < 4; i = i + 1 )
        {
            mz2VVramBack[ cursor ] = MZ2_CHAR_SPACE;
            cursor = cursor + 1;
            cursor = cursor + MZ2_VVRAM_WIDTH - 1;
        }
        cursor = cursor - MZ2_VVRAM_WIDTH * 4;

        for( col = 0; col < MZ2_COLUMN_COUNT; col = col + 1 )
        {
            int cell;
            cell = mz2CellMap[ mapIdx ];
            if( ( cell & MZ2_CELL_VISIBLE ) != 0 )
            {
                int c, iconBase, j;

                mz2VVramBack[ cursor ] = MZ2_CHAR_WALL; cursor = cursor + 1;
                if( ( cell & MZ2_CELL_TOP_WALL ) != 0 ) c = MZ2_CHAR_WALL; else c = MZ2_CHAR_SPACE;
                for( j = 0; j < 2; j = j + 1 ) { mz2VVramBack[ cursor ] = c; cursor = cursor + 1; }
                mz2VVramBack[ cursor ] = MZ2_CHAR_WALL; cursor = cursor + 1;
                cursor = cursor + MZ2_VVRAM_WIDTH - 4;

                if( ( cell & MZ2_CELL_LEFT_WALL ) != 0 ) c = MZ2_CHAR_WALL; else c = MZ2_CHAR_SPACE;
                for( j = 0; j < 2; j = j + 1 )
                {
                    mz2VVramBack[ cursor ] = c;
                    cursor = cursor + 1;
                    cursor = cursor + MZ2_VVRAM_WIDTH - 1;
                }
                cursor = cursor + 1 - MZ2_VVRAM_WIDTH * 2;

                iconBase = cell & MZ2_CELL_TYPE_MASK;
                for( j = 0; j < 2; j = j + 1 )
                {
                    int k;
                    for( k = 0; k < 2; k = k + 1 )
                    {
                        mz2VVramBack[ cursor ] = mz2CellTypeIcons[ iconBase + j*2 + k ];
                        cursor = cursor + 1;
                    }
                    cursor = cursor + MZ2_VVRAM_WIDTH - 2;
                }
                cursor = cursor + 2 - MZ2_VVRAM_WIDTH * 2;

                if( ( cell & MZ2_CELL_RIGHT_WALL ) != 0 ) c = MZ2_CHAR_WALL; else c = MZ2_CHAR_SPACE;
                for( j = 0; j < 2; j = j + 1 )
                {
                    mz2VVramBack[ cursor ] = c;
                    cursor = cursor + 1;
                    cursor = cursor + MZ2_VVRAM_WIDTH - 1;
                }
                cursor = cursor - 3;

                mz2VVramBack[ cursor ] = MZ2_CHAR_WALL; cursor = cursor + 1;
                if( ( cell & MZ2_CELL_BOTTOM_WALL ) != 0 ) c = MZ2_CHAR_WALL; else c = MZ2_CHAR_SPACE;
                for( j = 0; j < 2; j = j + 1 ) { mz2VVramBack[ cursor ] = c; cursor = cursor + 1; }
                mz2VVramBack[ cursor ] = MZ2_CHAR_WALL; cursor = cursor + 1;
                cursor = cursor - MZ2_VVRAM_WIDTH * 3 - 1;
            }
            else
            {
                int j;
                cursor = cursor + MZ2_VVRAM_WIDTH + 1;
                for( j = 0; j < 3; j = j + 1 )
                {
                    int k;
                    for( k = 0; k < 3; k = k + 1 )
                    {
                        mz2VVramBack[ cursor ] = MZ2_CHAR_SPACE;
                        cursor = cursor + 1;
                    }
                    cursor = cursor + MZ2_VVRAM_WIDTH - 3;
                }
                cursor = cursor + 2 - MZ2_VVRAM_WIDTH * 4;
            }
            mapIdx = mapIdx + 1;
        }
        cursor = cursor + MZ2_VVRAM_WIDTH * 3 - 24;
    }
}

void mz2InitFloor()
{
    int floorGlobalIdx, byteIdx, row, col, mapIdx;

    floorGlobalIdx = mz2StageFloorStart[ mz2StageIndex ] + mz2CurrentFloor;

    // Build CellMap from this floor's own 10 wall-encoding bytes (2 bytes
    // per row, 4 cells per byte, 2 bits/cell: bit0=RightWall, bit1=
    // BottomWall - LeftWall/TopWall are derived from the already-written
    // neighboring cell, so adjacent cells share one wall instead of
    // storing it twice).
    byteIdx = 0;
    mapIdx = 0;
    for( row = 0; row < MZ2_ROW_COUNT; row = row + 1 )
    {
        col = 0;
        while( col < MZ2_COLUMN_COUNT )
        {
            int source, sub;
            source = mz2FloorBytes[ floorGlobalIdx ][ byteIdx ];
            byteIdx = byteIdx + 1;
            for( sub = 0; sub < 4; sub = sub + 1 )
            {
                int cell;
                if( col == MZ2_COLUMN_COUNT ) break;
                cell = source & 3;
                source = source >> 2;
                if( col == 0 || ( mz2CellMap[ mapIdx - 1 ] & MZ2_CELL_RIGHT_WALL ) != 0 )
                  cell = cell | MZ2_CELL_LEFT_WALL;
                if( row == 0 || ( mz2CellMap[ mapIdx - MZ2_COLUMN_COUNT ] & MZ2_CELL_BOTTOM_WALL ) != 0 )
                  cell = cell | MZ2_CELL_TOP_WALL;
                mz2CellMap[ mapIdx ] = cell;
                mapIdx = mapIdx + 1;
                col = col + 1;
            }
        }
    }

    // this floor's own down-stairs
    {
        int stairCount, stairStart, i;
        stairCount = mz2FloorStairCount[ floorGlobalIdx ];
        stairStart = mz2FloorStairStart[ floorGlobalIdx ];
        for( i = 0; i < stairCount; i = i + 1 )
        {
            int b, sc, sr, idx;
            b = mz2AllStairs[ stairStart + i ];
            sc = b >> 4;
            sr = b & 0x0f;
            idx = mz2CellIndex( sc, sr );
            mz2CellMap[ idx ] = mz2CellMap[ idx ] | MZ2_CELL_DOWN_STAIR;
        }
    }

    // the floor above's own down-stairs become this floor's own up-stairs,
    // at the same column/row position.
    if( mz2CurrentFloor > 0 )
    {
        int prevGlobalIdx, stairCount, stairStart, i;
        prevGlobalIdx = floorGlobalIdx - 1;
        stairCount = mz2FloorStairCount[ prevGlobalIdx ];
        stairStart = mz2FloorStairStart[ prevGlobalIdx ];
        for( i = 0; i < stairCount; i = i + 1 )
        {
            int b, sc, sr, idx;
            b = mz2AllStairs[ stairStart + i ];
            sc = b >> 4;
            sr = b & 0x0f;
            idx = mz2CellIndex( sc, sr );
            mz2CellMap[ idx ] = mz2CellMap[ idx ] | MZ2_CELL_UP_STAIR;
        }
    }

    if( mz2CurrentFloor == 0 )
    {
        int b, gc, gr, idx;
        b = mz2StageGoal[ mz2StageIndex ];
        gc = b >> 4;
        gr = b & 0x0f;
        idx = mz2CellIndex( gc, gr );
        mz2CellMap[ idx ] = mz2CellMap[ idx ] | MZ2_CELL_GOAL;
        mz2GoalX = mz2ToCoord( gc );
        mz2GoalY = mz2ToCoord( gr );
    }

    {
        int startCol, startRow, idx;
        startCol = mz2ToGrid( mz2Man.x );
        startRow = mz2ToGrid( mz2Man.y );
        idx = mz2CellIndex( startCol, startRow );
        mz2CellMap[ idx ] = mz2CellMap[ idx ] | MZ2_CELL_NEXT_SCAN;
    }

    mz2FloodFillVisibility();
    mz2RenderFloorIntoBack();

    mz2ShowMonsters();
    mz2ShowKnives();
}

void mz2ChangeFloor( int floor )
{
    // upstream's own extra DrawAll() call here is dropped - see header
    // comment (this engine only ever presents the last draw before a
    // frame ends, so an intermediate draw within the same tick would
    // never actually be visible here).
    mz2CurrentFloor = floor;
    mz2InitFloor();
}

void mz2InitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never actually stops the player from continuing past stage 8),
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching its own structure exactly.
    int i, j;
    mz2InitialTime = 30;
    i = 0; j = 0;
    while( i < mz2CurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= MZ2_STAGE_COUNT )
        {
            j = 0;
            if( mz2InitialTime > 10 )
              mz2InitialTime = mz2InitialTime - 3;
        }
    }
    mz2StageIndex = j;

    {
        int floorCount, gfiBase, fi;
        floorCount = mz2StageFloorCount[ mz2StageIndex ];
        gfiBase = mz2StageFloorStart[ mz2StageIndex ];
        for( fi = 0; fi < floorCount; fi = fi + 1 )
        {
            mz2InitialTime = mz2InitialTime + ( mz2FloorStairCount[ gfiBase + fi ] * 5 );
            mz2InitialTime = mz2InitialTime + 15;
        }
    }
}

void mz2InitTrying()
{
    mz2StageTime = mz2InitialTime;
    mz2InitMonsters();
    mz2InitKnives();
    mz2InitPoints();
    mz2HideAllSprites();
    mz2ClearScreen();
    mz2PrintStatus();
    mz2CurrentFloor = 0;
    mz2Reached = false;
    mz2InitMan();
    mz2InitFloor();
    mz2DrawAll();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int mz2ComposeHeldKnifeByte( int col, int iconIdx )
{
    int glyph, sub, upperByte;
    glyph = mz2HeldKnifeIcons[ iconIdx ];
    if( glyph == -1 ) return 0;
    sub = col % 4;
    if( sub < 2 )
      upperByte = mz2CharPattern[ glyph*2 + 0 ];
    else
      upperByte = mz2CharPattern[ glyph*2 + 1 ];
    if( ( sub & 1 ) == 0 )
      return upperByte & 0x0f;
    return upperByte >> 4;
}

int mz2ComposeMapByte( int col, int page )
{
    int mapX, sub, upper, lower, upperByte, lowerByte;
    mapX = 1 + ( col / 4 );
    sub = col % 4;
    upper = mz2VVramFront[ mz2VOffset( mapX, page*2 ) ];
    lower = mz2VVramFront[ mz2VOffset( mapX, page*2+1 ) ];
    if( sub == 0 )
    {
        upperByte = mz2CharPattern[ upper*2 + 0 ];
        lowerByte = mz2CharPattern[ lower*2 + 0 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    if( sub == 1 )
    {
        upperByte = mz2CharPattern[ upper*2 + 0 ];
        lowerByte = mz2CharPattern[ lower*2 + 0 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    if( sub == 2 )
    {
        upperByte = mz2CharPattern[ upper*2 + 1 ];
        lowerByte = mz2CharPattern[ lower*2 + 1 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    upperByte = mz2CharPattern[ upper*2 + 1 ];
    lowerByte = mz2CharPattern[ lower*2 + 1 ];
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

void mz2Render()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            int col4;
            col4 = col / 4;
            if( col4 < 32 && mz2StatusChar[ page ][ col4 ] != -1 )
            {
                int sub;
                sub = col % 4;
                value = mz2AsciiPattern[ mz2StatusChar[page][col4]*4 + sub ];
            }
            else if( page == 6 && col4 >= MZ2_LEFT_X && col4 < MZ2_LEFT_X + 4 )
              value = mz2ComposeHeldKnifeByte( col, col4 - MZ2_LEFT_X );
            else if( col < MZ2_WINDOW_WIDTH * 4 )
              value = mz2ComposeMapByte( col, page );
            else
              value = 0;
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void mz2BeginTitle()
{
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    int i;

    for( i = 0; i < MZ2_VVRAM_WIDTH * MZ2_VVRAM_HEIGHT; i = i + 1 )
      mz2VVramFront[ i ] = MZ2_CHAR_SPACE;
    mz2ClearStatusChar();
    mz2HideAllSprites();

    // proactive fix matching Cracky's own already-user-approved sibling
    // fix ("on game over, the time value remains visible on titlescreen")
    // - see header comment.
    mz2StageTime = 0;
    mz2PrintStatus();

    {
        int titleLeft, cursor, ti, row, col, p;
        titleLeft = ( MZ2_VVRAM_WIDTH - 4*5 ) / 2;
        cursor = mz2VOffset( titleLeft, 2 );
        p = 0;
        for( ti = 0; ti < 5; ti = ti + 1 )
        {
            for( row = 0; row < 4; row = row + 1 )
            {
                for( col = 0; col < 4; col = col + 1 )
                {
                    mz2VVramFront[ cursor ] = mz2TitleBytes[ p ];
                    cursor = cursor + 1;
                    p = p + 1;
                }
                cursor = cursor + MZ2_VVRAM_WIDTH - 4;
            }
            cursor = cursor + 4 - MZ2_VVRAM_WIDTH*4;
        }
        mz2PrintS( 3, titleLeft + 4*5 - 5, sMini, 4 );
    }

    mz2PrintS( 7, 12, sCredit, 12 );

    {
        int arrowX;
        arrowX = 8;
        mz2PrintS( 5, arrowX + 1, sStart, 5 );
        mz2PrintS( 6, arrowX + 1, sContinue, 8 );
    }

    mz2TitleSelection = 0;
    mz2TitleSelectionChanged = true;
    mz2PrevLeft = 0; mz2PrevRight = 0; mz2PrevUp = 0; mz2PrevDown = 0; mz2PrevFire = 0;
    mz2State = MZ2_STATE_TITLE;
}

void mz2UpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !mz2PrevLeft ) || ( right && !mz2PrevRight ) ||
                ( up && !mz2PrevUp ) || ( down && !mz2PrevDown ) );
    justFire = ( fire && !mz2PrevFire );
    mz2PrevLeft = left; mz2PrevRight = right; mz2PrevUp = up; mz2PrevDown = down; mz2PrevFire = fire;

    if( mz2TitleSelectionChanged )
    {
        int arrowX;
        arrowX = 8;
        mz2TitleSelectionChanged = false;
        if( mz2TitleSelection == 0 )
          mz2PrintC( 5, arrowX, '>' );
        else
          mz2PrintC( 5, arrowX, ' ' );
        if( mz2TitleSelection == 1 )
          mz2PrintC( 6, arrowX, '>' );
        else
          mz2PrintC( 6, arrowX, ' ' );
    }

    if( justFire )
    {
        bool pendingContinue;
        pendingContinue = ( mz2TitleSelection == 1 );
        mz2Score = 0;
        if( !pendingContinue )
          mz2CurrentStage = 0;
        mz2RemainCount = 3;
        mz2InitStage();
        mz2InitTrying();
        mz2DrawAll();
        mz2StartSeq( 1, MZ2_MELODY_START );
        mz2State = MZ2_STATE_START_JINGLE;
        mz2Render();
        return;
    }
    if( justDir )
    {
        mz2TitleSelection = mz2TitleSelection ^ 1;
        mz2TitleSelectionChanged = true;
    }
    mz2Render();
}

void mz2UpdateStartJingle()
{
    if( !mz2SeqPlaying( 1 ) )
    {
        mz2StartBgm();
        mz2TickCounter = 0;
        mz2TimeDenom = MZ2_MAX_TIME_DENOM;
        mz2State = MZ2_STATE_PLAYING;
    }
    mz2Render();
}

void mz2BeginLose()
{
    mz2StopBgm();
    mz2AnimStep = 0;
    mz2WaitFrames = 0;
    mz2State = MZ2_STATE_LOSE_ANIM;
}

void mz2UpdateLoseAnim()
{
    int patterns[4] = { MZ2_CHAR_MAN_LEFT, MZ2_CHAR_MAN_DOWN, MZ2_CHAR_MAN_LOOSE2, MZ2_CHAR_MAN_LOOSE3 };

    if( mz2WaitFrames > 0 )
    {
        mz2WaitFrames = mz2WaitFrames - 1;
        mz2Render();
        return;
    }

    mz2ShowSprite( &mz2Man, patterns[ mz2AnimStep & 3 ] );
    mz2DrawAll();
    mz2StartSeq( 0, MZ2_MELODY_LOOSE );
    mz2AnimStep = mz2AnimStep + 1;
    mz2WaitFrames = mz2NoteFrames( 1 );

    if( mz2AnimStep >= 8 )
    {
        mz2RemainCount = mz2RemainCount - 1;
        if( mz2RemainCount > 0 )
        {
            mz2InitTrying();
            mz2DrawAll();
            mz2StartSeq( 1, MZ2_MELODY_START );
            mz2State = MZ2_STATE_START_JINGLE;
        }
        else
        {
            mz2PrintGameOver();
            mz2StartSeq( 1, MZ2_MELODY_GAMEOVER );
            mz2State = MZ2_STATE_GAMEOVER_JINGLE;
        }
    }
    mz2Render();
}

void mz2UpdateGameOverJingle()
{
    if( !mz2SeqPlaying( 1 ) )
      mz2BeginTitle();
    else
      mz2Render();
}

void mz2BeginClearWait()
{
    mz2StopBgm();
    mz2WaitFrames = 10;
    mz2State = MZ2_STATE_CLEAR_WAIT;
}

void mz2UpdateClearWait()
{
    if( mz2WaitFrames > 0 )
    {
        mz2WaitFrames = mz2WaitFrames - 1;
        mz2Render();
        return;
    }
    mz2StartSeq( 1, MZ2_MELODY_CLEAR );
    mz2State = MZ2_STATE_CLEAR_JINGLE;
    mz2Render();
}

void mz2UpdateClearJingle()
{
    if( !mz2SeqPlaying( 1 ) )
    {
        mz2WaitFrames = 0;
        mz2State = MZ2_STATE_BONUS_TALLY;
    }
    mz2Render();
}

void mz2UpdateBonusTally()
{
    if( mz2WaitFrames > 0 )
    {
        mz2WaitFrames = mz2WaitFrames - 1;
        mz2Render();
        return;
    }

    if( mz2StageTime >= MZ2_BONUS_RATE )
    {
        mz2AddScore( 3 );
        mz2StageTime = mz2StageTime - MZ2_BONUS_RATE;
        mz2PrintTime();
        mz2StartSeq( 0, MZ2_MELODY_BEEP );
        // Sound_Beep() (blocking, mz2NoteFrames(1) ticks) + a real,
        // separate WaitTimer(3) upstream fires right after it - see header
        // comment (unlike Cracky's own bonus-tally step, which has no
        // second wait).
        mz2WaitFrames = mz2NoteFrames( 1 ) + 3;
        mz2Render();
        return;
    }

    mz2StageTime = 0;
    mz2PrintStatus();
    mz2CurrentStage = mz2CurrentStage + 1;
    mz2InitStage();
    mz2InitTrying();
    mz2DrawAll();
    mz2StartSeq( 1, MZ2_MELODY_START );
    mz2State = MZ2_STATE_START_JINGLE;
    mz2Render();
}

void mz2UpdatePlaying()
{
    int i;

    mz2TickCounter = mz2TickCounter + 1;
    if( mz2TickCounter < MZ2_TICK_DIVISOR )
    {
        mz2Render();
        return;
    }
    mz2TickCounter = 0;

    mz2MoveMan();
    if( mz2FloorChangePending )
    {
        mz2FloorChangePending = false;
        mz2ChangeFloor( mz2PendingFloor );
    }
    mz2MoveMonsters();
    mz2UpdatePoints();

    mz2TimeDenom = mz2TimeDenom - 1;
    if( mz2TimeDenom == 0 )
    {
        mz2StageTime = mz2StageTime - 1;
        mz2TimeDenom = MZ2_MAX_TIME_DENOM;
        mz2PrintTime();
        if( mz2StageTime == 0 )
        {
            mz2PrintTimeUp();
            mz2DrawAll();
            mz2Render();
            mz2BeginLose();
            return;
        }
    }

    // matches upstream's own real "the knife moves 4 grid-units for every
    // 1 the player/monsters move" ratio, since upstream's own MoveKnives()
    // runs on every one of the 4 real sub-iterations between two drawn
    // frames, while MoveMan/MoveMonsters only run on the first - see
    // header comment (this is condensed into one logic tick here, since
    // only the LAST of those 4 knife positions is ever actually drawn
    // upstream too).
    for( i = 0; i < 4; i = i + 1 )
      mz2MoveKnives();

    mz2DrawAll();

    if( ( mz2Man.status & MZ2_MOVABLE_LIVE ) == 0 )
    {
        mz2Render();
        mz2BeginLose();
        return;
    }

    if( mz2Reached )
    {
        mz2Render();
        mz2BeginClearWait();
        return;
    }

    mz2Render();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameMazy2_init()
{
    int i;

    mz2CurrentStage = 0;
    mz2RemainCount = 3;
    mz2StageTime = 0;
    mz2Score = 0;
    mz2HeldKnifeCount = 0;
    mz2MonsterClock = 0;
    mz2ManKeyOn = false;

    for( i = 0; i < 3; i = i + 1 )
    {
        mz2SeqActive[ i ] = 0;
        mz2SeqMelody[ i ] = MZ2_MELODY_NONE;
    }
    mz2TickCounter = 0;

    mz2BeginTitle();
}

void gameMazy2_update()
{
    mz2AdvanceSound();

    if( mz2State == MZ2_STATE_TITLE )
      mz2UpdateTitle();
    else if( mz2State == MZ2_STATE_START_JINGLE )
      mz2UpdateStartJingle();
    else if( mz2State == MZ2_STATE_PLAYING )
      mz2UpdatePlaying();
    else if( mz2State == MZ2_STATE_LOSE_ANIM )
      mz2UpdateLoseAnim();
    else if( mz2State == MZ2_STATE_GAMEOVER_JINGLE )
      mz2UpdateGameOverJingle();
    else if( mz2State == MZ2_STATE_CLEAR_WAIT )
      mz2UpdateClearWait();
    else if( mz2State == MZ2_STATE_CLEAR_JINGLE )
      mz2UpdateClearJingle();
    else if( mz2State == MZ2_STATE_BONUS_TALLY )
      mz2UpdateBonusTally();
}
