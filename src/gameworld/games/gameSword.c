// =============================================================================
// SWORD mini (inufuto, UIAPduino+CH32V003+SSD1306 "Cate engine" edition,
// repo "SwordWork", license "None specified" - GitHub reports no LICENSE
// file). A top-down action game on a 12-column x 7-row grid: walk around
// with the d-pad, swing a sword with Fire to knock out chasing monsters
// (killing one drops a score-popup and briefly respawns it from its own
// start point after a countdown - monsters are a persistent "wave", not a
// one-shot kill list), dodge vertically-bouncing balls, grab items to
// extend the sword's own reach (thrust distance), and collect every
// treasure box to clear the stage; 3 lives, real (session-only) hi-score.
//
// Same author/engine lineage as this project's own already-shipped
// gameCracky.c (near-identical VVram/Vram/Sound/Math/ScanKeys plumbing,
// down to a shared real upstream typo - see swdDecideDirection() below) -
// this port follows gameCracky.c's own already-proven structural patterns
// throughout: class-flattened C structs + prefixed free functions, a
// frame-stepped state machine replacing every blocking upstream
// `WaitTimer`/`WaitMelody` busy-wait, and the exact same two-level VVram
// (24x16 glyph-index grid) -> raw-byte nibble-packed composite technique.
//
// **No hardware display-orientation transform is needed, matching
// Cracky's own final, hard-won conclusion** (see that file's own header
// comment for the multi-round debugging saga that established this) -
// `swdComposeRawByte(col,page)` is drawn directly at its own (col,page)
// via a plain `md_drawColumn()` loop, no column mirroring, no page
// reversal, no bit-reversal.
//
// **Coordinate model**: `GridCoordShift=1` (`GridCoordRate=2`) means every
// grid cell is exactly 2 "sub-units" wide/tall, and - just like Cracky -
// those sub-units are already expressed directly in VVram-cell-grid units
// (`ColumnWidth=2`/`RowHeight=2` VVram cells per grid cell), so sprite
// positions (`Man.x/y`, `Monster.x/y`, etc) plot directly into
// `swdVVram[y][x]` with no extra scaling. `CoordShift=0` (`CoordRate=1`,
// `CoordMask=0`) is a genuinely degenerate upstream parameterization here
// (every `& CoordMask` check upstream is unconditionally true, and a
// `Clock`-gated `(Clock & CoordMask) == 0` check in both Man.cpp and
// Monster.cpp is therefore *always* true too) - the `& CoordMask`
// comparisons are kept as literal expressions at their real call sites
// (matching this whole project's own "keep a degenerate-but-still-present
// upstream formula as a real expression" precedent, e.g. Cracky's own
// `CRK_COORD_RATE`), but the two `Clock` bookkeeping variables that only
// ever fed those permanently-true checks (Man.cpp's own local `clock`,
// Monster.cpp's own file-scope `Clock`) have no observable effect at all
// and were dropped rather than ported as genuinely inert complexity.
//
// **A real AVR/embedded-`byte`-wraparound-reliance bug, fixed proactively
// before ever compiling** - the same bug family this whole project has
// hit repeatedly (byte truncation, shift wraparound, signed sentinels,
// `int8_t` overflow reliance): both `CanMove()` (Man.cpp) and
// `CanMoveEnemy()` (Enemy.cpp) compute `byte nextColumn = (x >>
// GridCoordShift) + dx;` then only check `nextColumn >= ColumnCount` -
// relying on a *negative* result silently wrapping to a large unsigned
// value (0-1 -> 255 on a real `uint8_t`) to be caught by that same upper-
// bound check. Vircon32 ints don't wrap at 8 bits, so a leftward move at
// column 0 (or an upward move at row 0) would otherwise pass this check
// as a small negative number and read/write out of the intended grid.
// Fixed with an explicit `< 0` guard at both sites (`swdCanMove()`,
// `swdCanMoveMonster()`/`swdCanMoveBall()`). `Movable.cpp`'s own
// `NextCell()` has the identical wraparound-reliant shape but is
// confirmed dead code (declared, never called anywhere in the real
// source) and was dropped entirely rather than ported.
//
// **Sound**: `Sound.cpp` is the exact same real 3-tone-channel software
// mixer as Cracky's own, just with `Tempo=180` instead of Cracky's 160 -
// every `Sound_X()` one-shot effect and the two-part BGM route through
// the identical `[length,note]` byte-pair melody format, and this port
// reuses Cracky's own already-proven 3-voice frame-stepped sequencer
// shape verbatim (0=one-shot SFX, 1=jingle/BGM-voice-A reused, 2=BGM-
// voice-B), just re-derived for this file's own real tempo:
// `SoundHandler()`'s tick math is `time -= Tempo; if (time<=0) { time +=
// 300; advance(); }`, so one melody "length" unit = `300/Tempo` real
// 60Hz ticks - `swdNoteFrames(length) = round(length * 300/180)` =
// `round(length * 1.66667)` (vs. Cracky's own `*1.875` for its
// `Tempo=160`). Every melody's own `[length,note]` byte-pair table was
// byte-diff-extracted via a small Python script from the real upstream
// `Sound.cpp` (resolving its `NoteLength`/`Scale` enum symbols
// numerically first), not hand-transcribed - all 9 melodies (Loose/Hit/
// Up/Attack/Start/Clear/GameOver/Bgm1/Bgm2) checked against the script's
// own element counts before use.
//
// **The periodic "extra pause" in `Main.cpp`'s own tick loop, replicated
// rather than dropped as an odd upstream quirk**: `do { if ((Clock&3)==0)
// {...real logic...; WaitTimer(8);} if ((Clock&15)==0) {WaitTimer(8);}
// ...; ++Clock; } while(...)` - since real logic only ever runs on
// `Clock%4==0` iterations, and 16 is a multiple of 4, every 4th *active*
// logic tick also gets a second, doubled-length pause (no extra state
// change, just idle time) immediately after it. Reproduced with
// `swdActiveTickIndex` (a plain incrementing count of completed active
// ticks, checked `% 4 == 0` - equivalent to `Clock % 16 == 0` restricted
// to the already-`Clock%4==0` moments the real check can ever fire at)
// and, on that condition, seeding `swdTickCounter` at `-SWD_TICK_DIVISOR`
// instead of 0 so the next active tick needs a full 16 real frames
// instead of 8 - the same single-divisor-with-an-occasional-double-wait
// shape, no separate second timer to keep in sync.
//
// **Title screen's own text grid width - fixed after a real user-supplied
// hardware photo of Cracky (this project's other Cate-engine port)
// overturned the whole batch's original title-screen text model.** This
// port originally modeled `swdStatusChar` as an 8-char-wide grid (only
// ever wide enough for upstream's own status labels, SCORE/STAGE/REMAIN,
// which really are confined to upstream's own `LeftX=24`-based columns
// 24-31) and then ALSO tried to cram every piece of Title()'s own text
// ("SWORD", "MINI", "START"/"CONTINUE", the "INUFUTO 2026" credit) into
// that same narrow 8-cell slice - even though upstream's real `Title()`
// writes all of that text via the exact same `PrintS()`/`PrintC()`
// mechanism status text uses, just at genuinely different real columns
// (MINI at col 16, START/CONTINUE at col 9 with the cursor at col 8, the
// credit line at col 12 - see Status.cpp's own `Title()`), spanning the
// real full screen width (0-31 char cells), not a second narrow zone.
// **Fixed** the same way Cracky's own identical bug was fixed: widened
// `swdStatusChar` to `[8][32]`, added `swdFullWidthText` (true only while
// on the title screen) so `swdComposeRawByte()` reads the whole 32-column
// range instead of just the map area, and moved every status-label print
// call (SCORE/STAGE/REMAIN) onto upstream's own real, literal columns
// (LeftX=24-based) instead of a local 0-7 offset - see `swdStatusChar`'s
// own header comment and `swdComposeRawByte()`'s own comment for the full
// mechanism. Every title-screen string (MINI/START/CONTINUE/credit) now
// sits at upstream's own real literal columns, with nothing left needing
// to be trimmed, relocated, or dropped to fit.
//
// **A second, related architectural issue, found afterward and fixed the
// same way as Cracky's own identical fix**: the title's own decorative
// "SWORD" wordmark had initially been left as a plain-text substitute
// (this project's own 26-glyph font, `swdAsciiPattern` - " 0123456789>
// ACEFGIMNORSTUV", note no 'P' - is missing several common letters
// including W and D, so "SWORD" itself lost two of its own five letters
// to blank cells) rather than upstream's real hand-authored VVram bitmap
// logo - reasoned at the time as "purely decorative, not load-bearing."
// That reasoning was wrong, the same way it was wrong for Cracky: it's the
// single biggest, most prominent element on the whole title screen, not a
// throwaway detail, and a real user-supplied hardware photo of Cracky's
// own title screen (see that file's own header comment) is what actually
// established this. **Fixed** by restoring the real bitmap
// (`swdTitleBytes[]`, drawn directly into `swdVVram` by `swdBeginTitle()`)
// and OR-combining it with the status-text layer in
// `swdComposeRawByte()` instead of picking one exclusively - see both of
// those functions' own header comments for the full mechanism, including
// why Sword's own upstream `TitleBytes[]` data is laid out in a genuinely
// different (row-major, not letter-major) order than Cracky's.
// `PrintGameOver()`'s own real "GAME OVER" text (drawn
// directly into the *map* area, columns 32-67) is reproduced with the
// exact same small runtime overlay mechanism Cracky's own
// `PrintTimeUp`/`PrintGameOver` needed for the identical reason (no
// persistent VRAM here to lean on for a one-off direct write - see
// Cracky's own header comment for the fuller rationale) -
// `swdOverlayActive`/`Text`/`Len`/`Page`/`Col`, checked in `swdRender()`
// before falling back to `swdComposeRawByte()` - this overlay's own real
// upstream column (page 4, col 8) was already correct before this fix and
// needed no change.
//
// **The blocking upstream control flow converted to an explicit frame-
// stepped state machine**, the same treatment every port in this project
// needs: SWD_STATE_TITLE (`Title()`'s own key-poll loop), SWD_STATE_
// START_JINGLE (`Sound_Start()`'s real blocking wait, screen already
// showing the freshly-drawn stage), SWD_STATE_PLAYING (the tick-gated
// main loop described above), SWD_STATE_GAMEOVER_WAIT (the real
// `WaitTimer(30)` pause after `ShowMan()`/`DrawAll()`/`StopBGM()`),
// SWD_STATE_GAMEOVER_JINGLE (after `PrintGameOver()`'s overlay is set,
// `Sound_GameOver()`'s own blocking wait), SWD_STATE_CLEAR_WAIT (the
// mirror-image real `WaitTimer(30)` on a stage clear), and SWD_STATE_
// CLEAR_JINGLE (`Sound_Clear()`'s own blocking wait, followed by
// `++CurrentStage; goto stage;`). Unlike Cracky, Sword has no stage timer
// and no separate "lose animation" state at all - losing a life
// (`HitMan()`) is just a temporary-invincibility blink with no position
// reset and no freeze, so gameplay simply continues uninterrupted until
// `RemainCount` reaches 0.
//
// **Man<->Monster is a genuine two-way dependency** (Man's own attack
// swing calls `HitMonsters()`; a live Monster's/Ball's own movement calls
// `HitMan()`) - resolved without any forward declaration by ordering
// function *definitions* so `swdHitMan()` (which only ever touches Man's
// own state, never Monster's) is defined early, well before the Monster/
// Ball modules, while the rest of Man's own functions (`swdMoveMan()` in
// particular, which calls into Monster's `swdHitMonsters()`) are defined
// last, after Monster/Ball/Point/OneUp are all already in scope - a pure
// reordering of this file's own function layout, not a structural change
// from upstream's real Man.cpp/Monster.cpp split.
//
// Data extraction discipline matches this whole project's own established
// "byte-diff extract via script, don't hand-transcribe" practice
// throughout: `AsciiPattern`/`CharPattern` (Chars.cpp) and every one of
// the 8 stages' own start position / monster spawn list / ball spawn
// list / packed 21-byte wall-map (Stages.cpp) were all extracted via a
// small Python script parsing the real source files directly (resolving
// `(N<<4)|M`-style packed-byte expressions programmatically), not
// eyeballed by hand - each table's own element count checked against the
// script's own parse before use.
//
// **A follow-up meticulous verification pass** (this port had only ever
// been test-compiled, never played, before this pass) re-derived every
// data table via a fresh script-based byte-diff against the real upstream
// source (`AsciiPattern`/`CharPattern`/`Frequencies`/all 9 melodies/
// `RndNumbers`/every one of the 8 stages' own start-byte/monster-count/
// ball-count/monster-list/ball-list/21-byte wall-map - all confirmed
// byte-for-byte identical, no re-transcription needed), traced the whole
// state machine and every collision/movement function line-by-line
// against Main.cpp/Man.cpp/Monster.cpp/Ball.cpp/Enemy.cpp/Movable.cpp,
// and found + fixed 3 real bugs:
//
// 1) **A genuine, reachable-in-completely-ordinary-play out-of-bounds
//    array write**, the most severe finding of this pass.
//    `swdDrawSpritesIntoVVram()` was missing the bounds check upstream's
//    own `DrawSprites()` has (`y <= VVramHeight-1 && x <= VVramWidth-1`)
//    before ever writing a sprite's 2x2 block into `swdVVram`. Hand-traced
//    a full attack sequence tick-by-tick (concrete numbers, not just
//    reasoning) and confirmed the SWORD's own position is the one entity
//    in this whole file that ISN'T independently re-validated against the
//    grid boundary on every one of its own sub-steps - it just rides
//    exactly 1 grid cell ahead of Man, inheriting Man's own already-
//    validated per-step movement. Man is allowed to reach the very last
//    row/column while attacking (that's normal, valid movement), and at
//    that exact moment the sword sits far enough beyond it to land
//    outside the real 24x16 VVram grid - reachable with the default
//    starting `thrustCount=1` (no item pickups needed), attacking toward
//    any of the 4 edges from just inside it (e.g. column 10 of 0-11
//    facing right). Confirmed reachable on BOTH the positive side
//    (x/y > 23/15) and the negative side (attacking left/up near
//    column0/row0 drives the sword's own x/y genuinely negative) via the
//    same hand-trace. Upstream's own check only needs an upper bound
//    because a would-be-negative real AVR `byte` coordinate first wraps
//    to a large positive value (the exact same AVR-embedded-byte-
//    wraparound-reliance bug family already fixed once in this same file
//    for `swdCanMove()`/`swdCanMoveMonster()`/`swdCanMoveBall()`) -
//    Vircon32 ints don't wrap, so a literal port of upstream's own
//    upper-bound-only formula would have left the negative case
//    completely unguarded, an even worse out-of-bounds read/write
//    (before the start of the array) than the positive-side overrun.
//    Fixed with an explicit 4-sided range check (`x<0 || x>23 || y<0 ||
//    y>15`) before ever indexing into `swdVVram`. Without this, a sprite
//    write at x=24 would silently corrupt the next VVram row's own
//    column 0 (row-major array aliasing), and a write at y=16 would
//    corrupt whatever global sits right after `swdVVram` in memory - a
//    real, silent memory-corruption bug matching this project's own
//    well-documented "ERROR: INVALID MEMORY READ/WRITE" bug class,
//    reachable by simply attacking near any map edge, not a rare corner
//    case. Verified via an isolated Puppeteer test build: walked to and
//    attacked repeatedly at all 4 edges/both corners of stage 1 (which
//    starts the player right in the bottom-right corner), confirming no
//    crash and no visible rendering corruption anywhere on screen
//    throughout, plus a real game-over reached cleanly afterward (score
//    350, "GAME OVER" overlay rendered correctly) with the whole play
//    session remaining coherent start to finish.
//
// 2) & 3) **Two real "static-local-persists-across-goto" state-
//    progression deviations**, the same bug shape this file's own header
//    already documents once for the (inert) `Clock` variables, just
//    found in two places that DO have real observable effect. Main.cpp's
//    `static sbyte monsterNum = 0;` sits inside the goto-reachable
//    `stage:` block (re-entered on every new stage AND every new game) -
//    but real C++ semantics only run a static local's constant
//    initializer the very first time control ever reaches it; every
//    later `goto stage;` skips straight past that line, leaving
//    `monsterNum` to simply carry over whatever value the previous
//    stage's own monster-movement-throttle loop left it at, rather than
//    genuinely resetting to 0 every stage. Man.cpp's `static bool keyOn;`
//    (inside `MoveMan()`, no initializer at all) has the identical shape
//    - implicitly zero-initialized once at real cold boot, and never
//    touched by `InitMan()` on any subsequent stage, so whether Fire was
//    already held at the moment one stage/game ended genuinely carries
//    into the next one (blocking an instant re-attack if so). An earlier
//    version of this port reset both (`swdMonsterNum=0` in
//    `swdBeginStage()`, `swdManKeyOn=false` in `swdInitMan()`) on every
//    single stage transition, silently diverging from upstream's real
//    one-time-only semantics - a subtle but genuine deviation (a
//    slightly different monster-movement-throttle phase per stage; Fire
//    no longer needing to be released+re-pressed across a stage/game
//    boundary the way it really does upstream). **Fixed** by moving both
//    resets to run only once, in `gameSword_init()` (mirroring the real
//    program's own one-time cold-boot init, matching every other
//    genuinely-once-only reset already there like `swdHiScore=0`) and
//    removing them from `swdBeginStage()`/`swdInitMan()` entirely.
//
// Everything else traced during this pass held up as correct on
// re-derivation: `swdCanMoveMonster()`/`swdCanMoveBall()`'s own monster-
// vs-ball self-exclusion split (matches `CanMoveEnemy()`'s real pointer-
// identity behavior exactly, verified by tracing what a Monster-vs-Ball
// pointer comparison actually resolves to); `swdComposeRawByte()`'s own
// nibble-interleaved byte math (re-derived by hand against
// `VVramToVram()`/`SendUL()` byte-by-byte and confirmed an exact match,
// no mirroring/reversal needed, consistent with Cracky's own established
// conclusion); `swdPrintRemain()`'s icon-to-blank simplification (traced
// every RemainCount transition 1<->2<->3<->4+ by hand and confirmed the
// blank footprint always fully covers whatever a higher RemainCount's
// own display last touched, so no stale digit/icon can ever survive a
// transition); the GAME OVER vs CLEAR path's own different
// StopBgm/ShowMan/DrawAll ordering (a real, harmless upstream difference,
// reproduced faithfully rather than "normalized" to one order); and the
// `HideAllSprites()`/`NextCell()` call-site claims in this file's own
// original header comment (confirmed via a fresh grep: `HideAllSprites`
// really is only ever called from `Title()`, `NextCell` really is dead
// code, never called anywhere). Not independently re-forced this pass:
// a genuine stage-CLEAR (collecting every box on a level) - the state
// machine path for it is a direct structural mirror of the already-
// verified GAME OVER path (same wait/jingle/transition shape, just
// ending in `++CurrentStage; swdBeginStage();` instead of
// `swdBeginTitle();`), so risk is low, but worth a direct check if
// anything looks off.
//
// **Mechanical dialect conversion for this project**: ported directly
// from the sibling tinyjoypad_vircon32 project's own already-correct,
// already-verified gameSword.c (see that project's own CLAUDE.md for the
// full porting/bug-fix history behind every finding documented above) -
// only `int[N] name` -> `int name[N]` (and the 2D form) array-declaration
// syntax and the 6 bare `struct` tags (SwdSprite/SwdMan/SwdMovable/
// SwdMonster/SwdBall/SwdPoint) -> `typedef struct` needed changing. Every
// string-shaped array in this file (swdOverlayText, the various
// sScore/sStage/sMini/sStart/sContinue/sCredit/s local buffers) holds
// individual `'A'`-style char constants, never a `"..."` string literal,
// so - matching Cracky's own identical finding - none of them needed
// `int[]`->`char[]` widening; every value still round-trips through
// swdAsciiIndex()'s own int-comparison lookup exactly as upstream wrote
// it. No EEPROM/high-score persistence here, matching upstream having
// none at all (a CH32V003 RISC-V board, not AVR). No forceRedraw needed
// either: every one of this state machine's own branches calls
// swdRender() unconditionally on every real tick it runs, so there's no
// "last frame's pixels are still correct, skip redrawing" case anywhere
// in this file for a resumed screen to miss.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars/Stage/VVram/Sprite/Sound constants
// -----------------------------------------------------------------------------

#define SWD_CHAR_SPACE 0x00
#define SWD_CHAR_FENCE 0x10
#define SWD_CHAR_WALL 0x12
#define SWD_CHAR_SPRITE 0x1E
#define SWD_CHAR_END 0x96

#define SWD_COLUMN_COUNT 12
#define SWD_ROW_COUNT 7
#define SWD_COLUMN_WIDTH 2
#define SWD_ROW_HEIGHT 2
#define SWD_GRID_SHIFT 1
#define SWD_COLUMNS_PER_BYTE 4
#define SWD_MAP_SIZE ( ( SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE ) * SWD_ROW_COUNT )

#define SWD_CELL_SPACE 0x0
#define SWD_CELL_WALL 0x1
#define SWD_CELL_BOX 0x2
#define SWD_CELL_ITEM 0x3

#define SWD_COORD_SHIFT 0
#define SWD_COORD_RATE ( 1 << SWD_COORD_SHIFT )
#define SWD_COORD_MASK ( SWD_COORD_RATE - 1 )
#define SWD_GRID_COORD_SHIFT ( SWD_GRID_SHIFT + SWD_COORD_SHIFT )
#define SWD_GRID_COORD_RATE ( 1 << SWD_GRID_COORD_SHIFT )
#define SWD_GRID_COORD_MASK ( SWD_GRID_COORD_RATE - 1 )
#define SWD_HIT_RANGE ( SWD_COORD_RATE * 4 / 3 )

#define SWD_ACTOR_PATTERN_MASK 0x0f

#define SWD_VVRAM_WIDTH 24
#define SWD_VVRAM_HEIGHT 16
#define SWD_STAGE_TOP 1

#define SWD_SPRITE_POINT 0
#define SWD_SPRITE_BALL 4
#define SWD_SPRITE_MONSTER 8
#define SWD_SPRITE_MAN 12
#define SWD_SPRITE_ONEUP 14
#define SWD_SPRITE_END 15
#define SWD_INVALID_CODE 255

#define SWD_POINT_SPRITE_COUNT 4
#define SWD_BALL_SPRITE_COUNT 4
#define SWD_MONSTER_SPRITE_COUNT 4

#define SWD_PATTERN_MAN 0
#define SWD_PATTERN_MONSTER 16
#define SWD_PATTERN_BALL 24
#define SWD_PATTERN_POINT 0x19
#define SWD_PATTERN_ONEUP 0x1D

#define SWD_DIR_LEFT 0
#define SWD_DIR_RIGHT 1
#define SWD_DIR_UP 2
#define SWD_DIR_DOWN 3

#define SWD_STAGE_COUNT 8
#define SWD_MAX_MONSTER_COUNT 4
#define SWD_MAX_BALL_COUNT 4

#define SWD_MAN_ATTACKING 0x40

#define SWD_MONSTER_AVAILABLE 0x20
#define SWD_MONSTER_START 0x40
#define SWD_MONSTER_LIVE 0x80

#define SWD_BALL_LIVE 0x80

#define SWD_ONEUP_INVALID_Y 0xe0

#define SWD_POINT_MAX_TIME ( 6 << SWD_COORD_SHIFT )

#define SWD_MAX_TIMER_VALUE 50
#define SWD_MIN_TIMER_VALUE 8

// NoteLength/Scale, kept as real expressions (not resolved to their
// current literal values) matching upstream's own Sound.cpp enum, per
// this project's own established "don't silently pre-compute a #define"
// preference (see Cracky's own identical treatment).
#define SWD_N8 6
#define SWD_N8L 8
#define SWD_N8R 4
#define SWD_N8P ( SWD_N8 * 3 / 2 )
#define SWD_N4 ( SWD_N8 * 2 )
#define SWD_N4P ( SWD_N4 * 3 / 2 )
#define SWD_N2 ( SWD_N4 * 2 )
#define SWD_N2P ( SWD_N2 * 3 / 2 )
#define SWD_N1 ( SWD_N2 * 2 )
#define SWD_N16 ( SWD_N8 / 2 )

#define SWD_E2 1
#define SWD_F2 2
#define SWD_F2S 3
#define SWD_G2 4
#define SWD_G2S 5
#define SWD_A2 6
#define SWD_A2S 7
#define SWD_B2 8
#define SWD_C3 9
#define SWD_C3S 10
#define SWD_D3 11
#define SWD_D3S 12
#define SWD_E3 13
#define SWD_F3 14
#define SWD_F3S 15
#define SWD_G3 16
#define SWD_G3S 17
#define SWD_A3 18
#define SWD_A3S 19
#define SWD_B3 20
#define SWD_C4 21
#define SWD_C4S 22
#define SWD_D4 23
#define SWD_D4S 24
#define SWD_E4 25
#define SWD_F4 26
#define SWD_F4S 27
#define SWD_G4 28
#define SWD_G4S 29
#define SWD_A4 30
#define SWD_A4S 31
#define SWD_B4 32
#define SWD_C5 33
#define SWD_C5S 34
#define SWD_D5 35
#define SWD_D5S 36
#define SWD_E5 37
#define SWD_F5 38
#define SWD_F5S 39
#define SWD_G5 40

#define SWD_TEMPO 180

#define SWD_MELODY_NONE 0
#define SWD_MELODY_LOOSE 1
#define SWD_MELODY_HIT 2
#define SWD_MELODY_UP 3
#define SWD_MELODY_ATTACK 4
#define SWD_MELODY_START 5
#define SWD_MELODY_CLEAR 6
#define SWD_MELODY_GAMEOVER 7
#define SWD_MELODY_BGM1 8
#define SWD_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream Chars.cpp/Sound.cpp/Stages.cpp, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNORSTUV" (26 glyphs, 4 bytes/glyph).
int swdAsciiPattern[104] = {
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
    0x0e, 0x11, 0x0e, 0x00, 0x1f, 0x05, 0x1a, 0x00,
    0x16, 0x15, 0x0d, 0x00, 0x01, 0x1f, 0x01, 0x00,
    0x1f, 0x10, 0x1f, 0x00, 0x0f, 0x10, 0x0f, 0x00,
};

// CharPattern - 150 map/sprite glyphs, 2 bytes/glyph (a 4x4 pixel block).
int swdCharPattern[300] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x88, 0x88, 0x11, 0x11, 0x1f, 0x11, 0x11, 0xf1,
    0x8f, 0x88, 0x88, 0xf8, 0xc4, 0x6a, 0xff, 0xce,
    0x63, 0x0d, 0xdd, 0x66, 0x80, 0x80, 0xec, 0x06,
    0x20, 0x37, 0x05, 0x00, 0x00, 0x15, 0x75, 0x00,
    0x80, 0x0b, 0x40, 0x0c, 0x00, 0x15, 0x75, 0x00,
    0x20, 0xb0, 0x0b, 0x00, 0x50, 0x51, 0x07, 0x00,
    0x81, 0x3b, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x31, 0x33, 0x33, 0x00, 0x57, 0x51, 0x00,
    0xc0, 0x14, 0xa2, 0x08, 0x00, 0x57, 0x51, 0x00,
    0x00, 0xa1, 0x0a, 0x03, 0x00, 0x70, 0x15, 0x05,
    0x00, 0x4c, 0x90, 0x19, 0x00, 0x00, 0x00, 0x00,
    0x33, 0x33, 0x13, 0x00, 0x00, 0x77, 0x77, 0x00,
    0x10, 0x3c, 0x43, 0x03, 0x00, 0x77, 0x77, 0x00,
    0x30, 0x34, 0xc3, 0x01, 0x00, 0x77, 0x77, 0x00,
    0x30, 0x3c, 0xc3, 0x00, 0x00, 0x00, 0x8c, 0x00,
    0x00, 0x00, 0xff, 0x00, 0x00, 0x15, 0x75, 0x00,
    0x10, 0x3c, 0x43, 0x03, 0x00, 0x57, 0x51, 0x00,
    0x30, 0x34, 0xc3, 0x01, 0x00, 0x44, 0xc4, 0x00,
    0x60, 0x09, 0x81, 0x03, 0x00, 0xff, 0x00, 0x00,
    0x00, 0x31, 0x00, 0x00, 0x00, 0x7d, 0x7d, 0x00,
    0x80, 0x28, 0x40, 0x0c, 0x00, 0x7d, 0x7d, 0x00,
    0x20, 0x80, 0x0c, 0x02, 0x00, 0xd7, 0xd7, 0x00,
    0xc0, 0x04, 0x82, 0x08, 0x00, 0xd7, 0xd7, 0x00,
    0x20, 0xc0, 0x08, 0x02, 0x80, 0x77, 0x77, 0x08,
    0x00, 0x0c, 0x40, 0x01, 0x80, 0x77, 0x77, 0x08,
    0x10, 0x04, 0xc0, 0x00, 0x00, 0x7d, 0x7d, 0x08,
    0x00, 0x0c, 0x40, 0x01, 0x80, 0xd7, 0xd7, 0x00,
    0x10, 0x04, 0xc0, 0x00, 0xf6, 0xfa, 0xd6, 0xaf,
    0xf5, 0xd7, 0x6f, 0x5b, 0xe4, 0xc0, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x24, 0xcc, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x0c, 0xce, 0xc2, 0x00,
    0x11, 0x03, 0x61, 0x69, 0xa4, 0xc4, 0xc2, 0x00,
    0x21, 0x01, 0x61, 0x69, 0x1e, 0x1f, 0xb3, 0xc3,
    0x63, 0x66, 0x64, 0x36,
};

// TitleBytes - upstream's own real "SWORD" title-screen logo bitmap
// (Status.cpp's `Title()`), byte-diff-verified against the real upstream
// source. Every value is a valid index into swdCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table) - the exact same
// shared block-pattern palette every other map tile in this game already
// draws through. **Laid out as a flat 16-wide x 8-tall row-major raster
// across the WHOLE logo block, NOT grouped letter-by-letter** - a
// genuinely different data order than Cracky's own TitleBytes (which is
// letter-major: each letter's own 4x4 block fully written before moving to
// the next letter). Confirmed directly from upstream's own loop shape
// (`repeat(LogoHeight){ repeat(LogoWidth){ pVVram=VPut(...); } ... }` -
// LogoWidth=16 spans all 4 letter-columns before ever dropping to the next
// row) - see swdBeginTitle()'s own comment for the matching write loop and
// why this ordering matters.
int swdTitleBytes[128] = {
    0x0e, 0x05, 0x0b, 0x0c, 0x03, 0x02, 0x0f, 0x08,
    0x07, 0x0b, 0x0c, 0x07, 0x0b, 0x0c, 0x07, 0x0b,
    0x0d, 0x0a, 0x02, 0x0c, 0x03, 0x03, 0x0f, 0x0c,
    0x03, 0x0f, 0x0c, 0x0b, 0x07, 0x0c, 0x03, 0x0f,
    0x0a, 0x00, 0x0f, 0x04, 0x05, 0x05, 0x01, 0x00,
    0x05, 0x01, 0x04, 0x01, 0x05, 0x04, 0x05, 0x01,
    0x04, 0x05, 0x01, 0x0a, 0x00, 0x08, 0x02, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0f, 0x0c, 0x0c, 0x03, 0x08,
    0x0a, 0x00, 0x0a, 0x0a, 0x00, 0x0a, 0x00, 0x0a,
    0x00, 0x00, 0x00, 0x0f, 0x0c, 0x0c, 0x03, 0x0f,
    0x0c, 0x03, 0x0f, 0x0c, 0x03, 0x0f, 0x0e, 0x01,
    0x00, 0x00, 0x00, 0x0f, 0x0e, 0x0e, 0x01, 0x0d,
    0x0e, 0x01, 0x0f, 0x0d, 0x02, 0x0f, 0x04, 0x0b,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int swdFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int swdMelodyLoose[17] = {
    1, SWD_F5, 1, SWD_E5, 1, SWD_D5, 1, SWD_C5, 1, SWD_B4,
    1, SWD_A4, 1, SWD_G4, 1, SWD_F4, 0,
};

int swdMelodyHit[17] = {
    1, SWD_F4, 1, SWD_G4, 1, SWD_A4, 1, SWD_B4, 1, SWD_C5,
    1, SWD_D5, 1, SWD_E5, 1, SWD_F5, 0,
};

int swdMelodyUp[13] = {
    1, SWD_C4, 1, SWD_C4S, 1, SWD_D4, 1, SWD_F4, 1, SWD_A4,
    1, SWD_C5, 0,
};

int swdMelodyAttack[3] = { 1, SWD_A4, 0 };

int swdMelodyStart[17] = {
    SWD_N4, SWD_A4, SWD_N4, SWD_A4, SWD_N8, SWD_A4, SWD_N4, SWD_C5, SWD_N8, SWD_D5,
    SWD_N2, SWD_E5, SWD_N4, 0, SWD_N4, 0, 0,
};

int swdMelodyClear[25] = {
    SWD_N8, SWD_A4, SWD_N8, 0, SWD_N8, SWD_A4, SWD_N8, SWD_G4, SWD_N8, SWD_A4,
    SWD_N4, SWD_C5, SWD_N8, SWD_D5, SWD_N8, 0, SWD_N8, SWD_C5, SWD_N8, 0,
    SWD_N4P, SWD_A4, SWD_N2, 0, 0,
};

int swdMelodyGameOver[25] = {
    SWD_N8, SWD_A4, SWD_N8, SWD_E5, SWD_N8, SWD_D5, SWD_N8, SWD_C5, SWD_N8, SWD_D5,
    SWD_N8, SWD_C5, SWD_N8, SWD_B4, SWD_N4P, SWD_A4, SWD_N8, 0, SWD_N4, SWD_G4,
    SWD_N8, SWD_G4, SWD_N4, SWD_A4, 0,
};

int swdMelodyBgm1[193] = {
    SWD_N4, SWD_A4, SWD_N4, SWD_A4, SWD_N8, SWD_A4, SWD_N4, SWD_C5, SWD_N8, SWD_D5,
    SWD_N2, SWD_E5, SWD_N4, 0, SWD_N8, SWD_E5, SWD_N8, SWD_F5, SWD_N8P, SWD_E5,
    SWD_N8P, SWD_D5, SWD_N8, SWD_C5, SWD_N8P, SWD_D5, SWD_N8P, SWD_C5, SWD_N8, SWD_B4,
    SWD_N8P, SWD_C5, SWD_N8P, SWD_B4, SWD_N8, SWD_A4, SWD_N8P, SWD_B4, SWD_N8P, SWD_A4,
    SWD_N8, SWD_G4, SWD_N4, SWD_A4, SWD_N4, SWD_A4, SWD_N8, SWD_A4, SWD_N4, SWD_C5,
    SWD_N8, SWD_D5, SWD_N2, SWD_E5, SWD_N4, 0, SWD_N8, SWD_E5, SWD_N8, SWD_F5,
    SWD_N8P, SWD_E5, SWD_N8P, SWD_D5, SWD_N8, SWD_C5, SWD_N8P, SWD_D5, SWD_N8P, SWD_C5,
    SWD_N8, SWD_B4, SWD_N8P, SWD_C5, SWD_N8P, SWD_B4, SWD_N8, SWD_A4, SWD_N8P, SWD_B4,
    SWD_N8P, SWD_A4, SWD_N8, SWD_G4, SWD_N8, SWD_A4, SWD_N8, SWD_A4, SWD_N4, SWD_A4,
    SWD_N8, 0, SWD_N8, SWD_G4, SWD_N8, 0, SWD_N8, SWD_A4, SWD_N8, SWD_C5,
    SWD_N8, SWD_C5, SWD_N4, SWD_C5, SWD_N8, 0, SWD_N8, SWD_B4, SWD_N8, 0,
    SWD_N8, SWD_C5, SWD_N8, SWD_D5, SWD_N8, SWD_D5, SWD_N4, SWD_D5, SWD_N8, 0,
    SWD_N8, SWD_C5, SWD_N8, 0, SWD_N8, SWD_D5, SWD_N8P, SWD_E5, SWD_N8P, SWD_D5,
    SWD_N8, SWD_C5, SWD_N8P, SWD_D5, SWD_N8P, SWD_C5, SWD_N8, SWD_B4, SWD_N8, SWD_A4,
    SWD_N8, SWD_A4, SWD_N4, SWD_A4, SWD_N8, 0, SWD_N8, SWD_G4, SWD_N8, 0,
    SWD_N8, SWD_A4, SWD_N8, SWD_C5, SWD_N8, SWD_C5, SWD_N4, SWD_C5, SWD_N8, 0,
    SWD_N8, SWD_B4, SWD_N8, 0, SWD_N8, SWD_C5, SWD_N8, SWD_D5, SWD_N8, SWD_D5,
    SWD_N4, SWD_D5, SWD_N8, 0, SWD_N8, SWD_C5, SWD_N8, 0, SWD_N8, SWD_D5,
    SWD_N8P, SWD_E5, SWD_N8P, SWD_D5, SWD_N8, SWD_C5, SWD_N8P, SWD_D5, SWD_N8P, SWD_C5,
    SWD_N8, SWD_B4, 255,
};

int swdMelodyBgm2[233] = {
    SWD_N4, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2,
    SWD_N8, 0, SWD_N8, SWD_F2, SWD_N4, SWD_C3, SWD_N8, 0, SWD_N8, SWD_C3,
    SWD_N8, 0, SWD_N8, SWD_C3, SWD_N8, 0, SWD_N8, SWD_C3, SWD_N4, SWD_D3,
    SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, 0,
    SWD_N8, SWD_D3, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0,
    SWD_N8, SWD_G2, SWD_N8, 0, SWD_N8, SWD_G2, SWD_N8, 0, SWD_N4, SWD_F2,
    SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0,
    SWD_N8, SWD_F2, SWD_N4, SWD_C3, SWD_N8, 0, SWD_N8, SWD_C3, SWD_N8, 0,
    SWD_N8, SWD_C3, SWD_N8, 0, SWD_N8, SWD_C3, SWD_N4, SWD_D3, SWD_N8, 0,
    SWD_N8, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3,
    SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_G2,
    SWD_N8, 0, SWD_N8, SWD_G2, SWD_N8, 0, SWD_N4, SWD_A2, SWD_N8, 0,
    SWD_N8, SWD_A2, SWD_N8, 0, SWD_N8, SWD_A2, SWD_N8, 0, SWD_N8, SWD_A2,
    SWD_N4, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2,
    SWD_N8, 0, SWD_N8, SWD_F2, SWD_N4, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3,
    SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, SWD_C3,
    SWD_N8, 0, SWD_N8, SWD_C3, SWD_N8, 0, SWD_N8, SWD_B2, SWD_N8, 0,
    SWD_N8, SWD_B2, SWD_N8, 0, SWD_N4, SWD_A2, SWD_N8, 0, SWD_N8, SWD_A2,
    SWD_N8, 0, SWD_N8, SWD_A2, SWD_N8, 0, SWD_N8, SWD_A2, SWD_N4, SWD_F2,
    SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0, SWD_N8, SWD_F2, SWD_N8, 0,
    SWD_N8, SWD_F2, SWD_N4, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, 0,
    SWD_N8, SWD_D3, SWD_N8, 0, SWD_N8, SWD_D3, SWD_N8, SWD_C3, SWD_N8, 0,
    SWD_N8, SWD_C3, SWD_N8, 0, SWD_N8, SWD_B2, SWD_N8, 0, SWD_N8, SWD_B2,
    SWD_N8, 0, 255,
};

// Stage data - each entry's start byte is (column<<4)|row, matching
// upstream's own ToColumn()/ToRow() packing.
int swdStageStart[8] = { 182, 182, 17, 179, 99, 102, 180, 17 };
int swdStageMonsterCount[8] = { 4, 2, 4, 3, 3, 3, 4, 4 };
int swdStageBallCount[8] = { 2, 2, 2, 3, 4, 1, 4, 2 };

int swdStageMonsters[8][4] = {
    { 0, 32, 80, 128 },
    { 1, 113, 0, 0 },
    { 176, 116, 22, 102 },
    { 0, 32, 80, 0 },
    { 3, 179, 102, 0 },
    { 176, 6, 182, 0 },
    { 0, 32, 80, 4 },
    { 176, 97, 22, 102 },
};

int swdStageBalls[8][4] = {
    { 114, 36, 0, 0 },
    { 144, 84, 0, 0 },
    { 83, 165, 0, 0 },
    { 132, 148, 164, 0 },
    { 32, 68, 86, 150 },
    { 131, 0, 0, 0 },
    { 67, 131, 100, 164 },
    { 83, 164, 0, 0 },
};

int swdStageBytes[8][21] = {
    {
        0x00, 0x00, 0xc0, 0x90, 0x59, 0x19,
        0x08, 0x01, 0x10, 0x04, 0x11, 0x11,
        0x00, 0xe2, 0x22, 0x54, 0x54, 0x54,
        0x88, 0x2c, 0x03,
    },
    {
        0x15, 0x96, 0x81, 0x20, 0x25, 0xa1,
        0x14, 0xd1, 0x40, 0x26, 0x51, 0xd4,
        0x24, 0x60, 0x0c, 0x14, 0x11, 0x16,
        0x22, 0xa0, 0x15,
    },
    {
        0x05, 0x03, 0x20, 0x40, 0x44, 0x75,
        0x44, 0x54, 0x01, 0x44, 0x50, 0x90,
        0x64, 0x20, 0x44, 0x44, 0x50, 0x44,
        0x00, 0x82, 0x80,
    },
    {
        0x00, 0x82, 0xc0, 0x18, 0x66, 0x65,
        0x11, 0x15, 0x22, 0xd3, 0x05, 0x15,
        0x21, 0x78, 0x40, 0x10, 0x66, 0x40,
        0x28, 0x85, 0x80,
    },
    {
        0x08, 0x3e, 0x48, 0x4e, 0x55, 0xc2,
        0x44, 0x00, 0x00, 0x08, 0x41, 0x21,
        0x87, 0x90, 0xa0, 0x84, 0x30, 0x11,
        0x4e, 0x40, 0xc1,
    },
    {
        0xad, 0x19, 0x02, 0x58, 0x09, 0x80,
        0x67, 0x55, 0xc5, 0x04, 0x00, 0x80,
        0x46, 0x55, 0x14, 0x24, 0x68, 0x20,
        0xb0, 0x45, 0x19,
    },
    {
        0x00, 0x82, 0xf0, 0x18, 0x86, 0x19,
        0x70, 0xd5, 0x09, 0x64, 0x54, 0x14,
        0x64, 0x44, 0x04, 0x64, 0x44, 0x44,
        0x28, 0x00, 0x82,
    },
    {
        0x55, 0x03, 0x04, 0x00, 0x04, 0x01,
        0x45, 0x11, 0x11, 0x40, 0x91, 0x90,
        0x64, 0x21, 0x45, 0x44, 0x11, 0x01,
        0x00, 0x02, 0x80,
    },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int swdScore;
int swdHiScore;
int swdRemainCount;
int swdCurrentStage;
int swdStageIndex;
int swdBoxCount;
int swdTimerValue;
int swdStageMap[SWD_MAP_SIZE];

int swdVVram[SWD_VVRAM_HEIGHT][SWD_VVRAM_WIDTH];

typedef struct
{
    int x, y, code;
} SwdSprite;
SwdSprite swdSprites[SWD_SPRITE_END];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into swdAsciiPattern (0 = space) per cell.
//
// **This width was widened from an original, wrong `[8][8]` after a real
// user-supplied hardware photo of this "Cate engine" family's own sibling
// game Cracky proved the original narrow model was flatly incorrect for
// every game in this batch, Sword included.** The original design only
// ever modeled upstream's own status-label columns (SCORE/STAGE/REMAIN,
// which upstream's `LeftX=24` constant genuinely does confine to columns
// 24-31, 8 cells) - but Title()'s own text (the "SWORD" logo, "MINI",
// "START"/"CONTINUE", the "INUFUTO 2026" credit) lives at upstream's real
// columns 0-23 (see Status.cpp's own `Title()`: MINI at col 16, START/
// CONTINUE at col 9 with the cursor at col 8, the credit line at col 12),
// using the exact same shared PrintC()/PrintS() mechanism at different
// column arguments - not a separate, narrower grid at all. Cramming all of
// that title-screen text into the same 8-cell-wide status grid (reusing
// columns 24-31 that the status labels ALSO use) is what caused this
// port's own version of the recurring bug pattern found across this
// project's whole Cate-engine batch: title text silently overlapping
// SCORE/STAGE/REMAIN, or getting truncated to fit. See swdComposeRawByte()'s
// own header for how this wider grid actually reaches the screen.
int swdStatusChar[8][32];

// Set true only while on the title screen (SWD_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system for text at all (only
// for the decorative logo bitmap), and instead drives status labels AND
// every title-screen string through the same PrintC()/PrintS() Vram
// mechanism, at real columns spanning the whole 0-31 char-cell range. When
// true, swdComposeRawByte() reads swdStatusChar across the full width
// instead of just the map area, letting the title screen use that same
// wide real estate instead of being artificially confined to a narrow
// status-only zone - matching Cracky's own crkFullWidthText fix exactly.
bool swdFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintGameOver() Vram-direct write - see header comment.
bool swdOverlayActive;
int swdOverlayText[10];
int swdOverlayLen;
int swdOverlayPage;
int swdOverlayCol;

int swdRndIndex;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of SWD_TICK_DIVISOR.
int swdSeqMelody[3];
int swdSeqPos[3];
int swdSeqWait[3];
int swdSeqActive[3];

typedef struct
{
    int x, y, sprite, status, dx, dy, count;
} SwdMan;
SwdMan swdMan;
int swdManDirDx;
int swdManDirDy;
int swdManDirPattern;
bool swdManKeyOn;
int swdThrustCount;
int swdInvincibleCount;

typedef struct
{
    int x, y, sprite;
} SwdMovable;
SwdMovable swdSword;

typedef struct
{
    int x, y, sprite, status, dx, dy, startX, startY, count;
} SwdMonster;
SwdMonster swdMonsters[SWD_MAX_MONSTER_COUNT];
int swdNextMonsterIndex;

typedef struct
{
    int x, y, sprite, status, dx, dy;
} SwdBall;
SwdBall swdBalls[SWD_MAX_BALL_COUNT];

typedef struct
{
    int x, y, sprite, time;
} SwdPoint;
SwdPoint swdPoints[SWD_POINT_SPRITE_COUNT];
int swdPointRate;
int swdPointValues[4] = { 10, 20, 40, 80 };

int swdOneUpX;
int swdOneUpY;
int swdOneUpSprite;
int swdOneUpNextRow;

int swdMonsterDirTable[8] = { -1, 0, 1, 0, 0, -1, 0, 1 };

#define SWD_TICK_DIVISOR 8
int swdTickCounter;
int swdActiveTickIndex;
int swdMonsterNum;

#define SWD_STATE_TITLE 0
#define SWD_STATE_START_JINGLE 1
#define SWD_STATE_PLAYING 2
#define SWD_STATE_GAMEOVER_WAIT 3
#define SWD_STATE_GAMEOVER_JINGLE 4
#define SWD_STATE_CLEAR_WAIT 5
#define SWD_STATE_CLEAR_JINGLE 6
int swdState;
int swdWaitFrames;
int swdSelection;
bool swdSelectionChanged;
bool swdPrevLeft;
bool swdPrevRight;
bool swdPrevUp;
bool swdPrevDown;
bool swdPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int swdRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

int swdRnd()
{
    int r;
    r = swdRndNumbers[ swdRndIndex ];
    swdRndIndex = swdRndIndex + 1;
    if( swdRndIndex >= 32 )
      swdRndIndex = 0;
    return r & 0x0f;
}

int swdAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int swdToColumn( int b )
{
    return b >> 4;
}

int swdToRow( int b )
{
    return b & 0x0f;
}

int swdGetCell( int column, int row )
{
    int idx, shift, b;
    idx = row * ( SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE ) + column / SWD_COLUMNS_PER_BYTE;
    shift = ( column & 3 ) * 2;
    b = swdStageMap[ idx ];
    return ( b >> shift ) & 0x03;
}

void swdSetCell( int column, int row, int cell )
{
    int idx, shift, mask;
    idx = row * ( SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE ) + column / SWD_COLUMNS_PER_BYTE;
    shift = ( column & 3 ) * 2;
    mask = ~( 0x03 << shift );
    swdStageMap[ idx ] = ( swdStageMap[ idx ] & mask ) | ( ( cell & 0x03 ) << shift );
}


// -----------------------------------------------------------------------------
//   Movable/Sprite primitives
// -----------------------------------------------------------------------------

bool swdIsNear( int x1, int y1, int x2, int y2 )
{
    return
        x1 + SWD_HIT_RANGE >= x2 && x2 + SWD_HIT_RANGE >= x1 &&
        y1 + SWD_HIT_RANGE >= y2 && y2 + SWD_HIT_RANGE >= y1;
}

void swdHideSprite( int index )
{
    swdSprites[ index ].code = SWD_INVALID_CODE;
}

void swdHideAllSprites()
{
    int i;
    for( i = 0; i < SWD_SPRITE_END; i = i + 1 )
      swdSprites[ i ].code = SWD_INVALID_CODE;
}

void swdShowSpriteXY( int x, int y, int spriteIndex, int pattern )
{
    swdSprites[ spriteIndex ].x = x;
    swdSprites[ spriteIndex ].y = y + SWD_STAGE_TOP;
    swdSprites[ spriteIndex ].code = ( pattern << 2 ) + SWD_CHAR_SPRITE;
}

void swdDrawSpritesIntoVVram()
{
    // Upstream's own DrawSprites() gates every sprite write behind
    // `y <= VVramHeight-1 && x <= VVramWidth-1` before ever touching VVram -
    // a real, load-bearing safety check, not a redundant one: the sword's
    // own position (Sword.x/y) is NOT independently re-validated against
    // the grid boundary on every one of its own sub-steps the way every
    // other entity's movement is (it just rides 1 grid cell ahead of Man,
    // inheriting Man's own already-validated movement) - Man can validly
    // reach the very last row/column while attacking, at which point the
    // sword sits exactly far enough beyond it to land outside the real
    // VVram grid (confirmed by hand-tracing a full attack sequence toward
    // any of the 4 edges, reachable with the default thrustCount=1, not
    // just after collecting several range-extending items). Upstream's
    // own check only needs an upper bound because a would-be-negative
    // `byte` coordinate first wraps to a large positive value (the same
    // AVR/embedded-byte-wraparound-reliance bug family already fixed for
    // swdCanMove()/swdCanMoveMonster()/swdCanMoveBall() above) - Vircon32
    // ints don't wrap, so a genuinely negative x/y (confirmed reachable
    // too, attacking left/up near column0/row0) needs an explicit lower-
    // bound check as well, not just upstream's literal upper-bound-only
    // formula. Without this, the sword's own errant position silently
    // wrote past the end of a VVram row (corrupting the next row's own
    // leftmost cell) or, worse, past the end of the whole swdVVram array
    // (corrupting whatever global happens to sit right after it) -
    // reachable in completely ordinary play, not a rare corner case.
    int i, x, y, c;
    for( i = SWD_SPRITE_END - 1; i >= 0; i = i - 1 )
    {
        if( swdSprites[ i ].code != SWD_INVALID_CODE )
        {
            x = swdSprites[ i ].x;
            y = swdSprites[ i ].y;
            if( x < 0 || x > SWD_VVRAM_WIDTH - 1 || y < 0 || y > SWD_VVRAM_HEIGHT - 1 )
              continue;
            c = swdSprites[ i ].code;
            swdVVram[ y ][ x ] = c; c = c + 1;
            swdVVram[ y ][ x + 1 ] = c; c = c + 1;
            swdVVram[ y + 1 ][ x ] = c; c = c + 1;
            swdVVram[ y + 1 ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp
// -----------------------------------------------------------------------------

int swdAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNORSTUV" - direct port of PrintC()'s
    // own linear search (only 26 entries, no cost concern doing this live).
    int table[26] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'R', 'S', 'T', 'U', 'V',
    };
    int i;
    for( i = 0; i < 26; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

int swdPrintC( int page, int col, int c )
{
    swdStatusChar[ page ][ col ] = swdAsciiIndex( c );
    return col + 1;
}

int swdPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = swdPrintC( page, col, s[ i ] );
    return col;
}

int swdPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      swdPrintC( page, col, ' ' );
    else
      swdPrintC( page, col, d1 + '0' );
    return swdPrintC( page, col + 1, ( b % 10 ) + '0' );
}

int swdPrintNumber5( int page, int col, int w )
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
          swdPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            swdPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    return swdPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// swdStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void swdPrintScore()
{
    swdPrintNumber5( 1, 26, swdScore );
    swdPrintC( 1, 31, '0' );
}

void swdPrintRemain()
{
    // Upstream draws a real 2x2 Char_Remain icon (Put2C) for each extra
    // life - simplified to plain blanks/digit throughout (matching this
    // whole project's own established "status-text-only lives display"
    // precedent, see Cracky's own identical simplification), while still
    // reproducing the exact same column math (starting from upstream's
    // real LeftX=24 base, and the real trailing blank-fill for
    // RemainCount<3) so no stale digit/icon can ever be left behind from
    // a previous, higher-RemainCount display.
    int i, col;
    col = 24;
    if( swdRemainCount > 1 )
    {
        i = swdRemainCount - 1;
        if( i > 2 )
        {
            swdPrintC( 7, 24, ' ' );
            swdPrintC( 7, 25, ' ' );
            col = 26;
            swdPrintC( 7, col, ' ' );
            col = col + 1;
            swdPrintC( 7, col, i + '0' );
            col = col + 1;
        }
        else
        {
            int j;
            for( j = 0; j < i; j = j + 1 )
            {
                swdPrintC( 7, col, ' ' );
                col = col + 1;
                swdPrintC( 7, col, ' ' );
                col = col + 1;
            }
        }
    }
    if( swdRemainCount < 3 )
    {
        swdPrintC( 7, col, ' ' );
        swdPrintC( 7, col + 1, ' ' );
    }
}

void swdPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    swdPrintS( 0, 24, sScore, 5 );
    swdPrintS( 3, 24, sStage, 5 );
    swdPrintByteNumber2( 3, 30, swdCurrentStage + 1 );
    swdPrintScore();
    swdPrintRemain();
}

void swdBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    swdOverlayActive = true;
    swdOverlayLen = len;
    swdOverlayPage = page;
    swdOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      swdOverlayText[ i ] = s[ i ];
}

void swdPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    swdBeginOverlay( s, 9, 4, 8 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int swdMelodyLength( int id )
{
    if( id == SWD_MELODY_LOOSE ) return 17;
    if( id == SWD_MELODY_HIT ) return 17;
    if( id == SWD_MELODY_UP ) return 13;
    if( id == SWD_MELODY_ATTACK ) return 3;
    if( id == SWD_MELODY_START ) return 17;
    if( id == SWD_MELODY_CLEAR ) return 25;
    if( id == SWD_MELODY_GAMEOVER ) return 25;
    if( id == SWD_MELODY_BGM1 ) return 193;
    if( id == SWD_MELODY_BGM2 ) return 233;
    return 0;
}

int swdMelodyValue( int id, int idx )
{
    if( id == SWD_MELODY_LOOSE ) return swdMelodyLoose[ idx ];
    if( id == SWD_MELODY_HIT ) return swdMelodyHit[ idx ];
    if( id == SWD_MELODY_UP ) return swdMelodyUp[ idx ];
    if( id == SWD_MELODY_ATTACK ) return swdMelodyAttack[ idx ];
    if( id == SWD_MELODY_START ) return swdMelodyStart[ idx ];
    if( id == SWD_MELODY_CLEAR ) return swdMelodyClear[ idx ];
    if( id == SWD_MELODY_GAMEOVER ) return swdMelodyGameOver[ idx ];
    if( id == SWD_MELODY_BGM1 ) return swdMelodyBgm1[ idx ];
    if( id == SWD_MELODY_BGM2 ) return swdMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/SWD_TEMPO(180) = 1.66667 real 60Hz ticks - see header comment.
int swdNoteFrames( int length )
{
    return (int)( length * 1.66666667 + 0.5 );
}

void swdStartSeq( int channel, int melodyId )
{
    swdSeqMelody[ channel ] = melodyId;
    swdSeqPos[ channel ] = 0;
    swdSeqWait[ channel ] = 0;
    swdSeqActive[ channel ] = 1;
}

void swdStopSeq( int channel )
{
    swdSeqActive[ channel ] = 0;
    swdSeqMelody[ channel ] = SWD_MELODY_NONE;
}

bool swdSeqPlaying( int channel )
{
    return swdSeqActive[ channel ] != 0;
}

void swdAdvanceOneSeq( int channel )
{
    int length, note;

    if( swdSeqActive[ channel ] == 0 ) return;

    if( swdSeqWait[ channel ] > 0 )
    {
        swdSeqWait[ channel ] = swdSeqWait[ channel ] - 1;
        return;
    }

    length = swdMelodyValue( swdSeqMelody[ channel ], swdSeqPos[ channel ] );
    if( length == 0 )
    {
        swdStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        swdSeqPos[ channel ] = 0;
        length = swdMelodyValue( swdSeqMelody[ channel ], 0 );
    }
    note = swdMelodyValue( swdSeqMelody[ channel ], swdSeqPos[ channel ] + 1 );
    swdSeqPos[ channel ] = swdSeqPos[ channel ] + 2;
    swdSeqWait[ channel ] = swdNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)swdFrequencies[ note - 1 ], (float)swdSeqWait[ channel ] / 60.0 );
}

void swdAdvanceSound()
{
    swdAdvanceOneSeq( 0 );
    swdAdvanceOneSeq( 1 );
    swdAdvanceOneSeq( 2 );
}

void swdStartSfx( int melodyId )
{
    swdStartSeq( 0, melodyId );
}

void swdStartBgm()
{
    swdStartSeq( 1, SWD_MELODY_BGM1 );
    swdStartSeq( 2, SWD_MELODY_BGM2 );
}

void swdStopBgm()
{
    swdStopSeq( 1 );
    swdStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void swdAddScore( int pts )
{
    swdScore = swdScore + pts;
    if( swdScore > swdHiScore )
      swdHiScore = swdScore;
    swdPrintScore();
}


// -----------------------------------------------------------------------------
//   Man.cpp - swdHitMan() only (the half of Man.cpp that Monster/Ball need),
//   defined early to avoid a forward declaration; the rest of Man.cpp (which
//   itself needs Monster's swdHitMonsters()) is defined near the end of this
//   file, after Monster/Ball/Point/OneUp are already in scope.
// -----------------------------------------------------------------------------

void swdHitMan( int x, int y )
{
    if( swdInvincibleCount != 0 ) return;
    if( swdIsNear( swdMan.x, swdMan.y, x, y ) )
    {
        swdInvincibleCount = swdTimerValue;
        swdHideSprite( swdMan.sprite );
        swdRemainCount = swdRemainCount - 1;
        swdPrintRemain();
        swdStartSfx( SWD_MELODY_LOOSE );
    }
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void swdInitPoints()
{
    int i, sprite;
    sprite = SWD_SPRITE_POINT;
    for( i = 0; i < SWD_POINT_SPRITE_COUNT; i = i + 1 )
    {
        swdPoints[ i ].sprite = sprite;
        swdPoints[ i ].time = 0;
        swdHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void swdStartPoint( int x, int y )
{
    int i;
    swdAddScore( swdPointValues[ swdPointRate ] );
    for( i = 0; i < SWD_POINT_SPRITE_COUNT; i = i + 1 )
    {
        if( swdPoints[ i ].time == 0 )
        {
            swdPoints[ i ].time = SWD_POINT_MAX_TIME;
            swdPoints[ i ].x = x;
            swdPoints[ i ].y = y;
            swdShowSpriteXY( x, y, swdPoints[ i ].sprite, SWD_PATTERN_POINT + swdPointRate );
            swdPointRate = swdPointRate + 1;
            i = SWD_POINT_SPRITE_COUNT;
        }
    }
}

void swdUpdatePoints()
{
    int i;
    for( i = 0; i < SWD_POINT_SPRITE_COUNT; i = i + 1 )
    {
        if( swdPoints[ i ].time != 0 )
        {
            swdPoints[ i ].time = swdPoints[ i ].time - 1;
            if( swdPoints[ i ].time == 0 )
              swdHideSprite( swdPoints[ i ].sprite );
        }
    }
}


// -----------------------------------------------------------------------------
//   OneUp.cpp
// -----------------------------------------------------------------------------

void swdInitOneUp()
{
    swdOneUpY = SWD_ONEUP_INVALID_Y;
    swdOneUpSprite = SWD_SPRITE_ONEUP;
    swdHideSprite( swdOneUpSprite );
    swdOneUpNextRow = 0;
}

void swdStartOneUp()
{
    int row, column;
    if( swdOneUpY == SWD_ONEUP_INVALID_Y && swdRemainCount < 10 && swdRnd() < 5 )
    {
        row = swdOneUpNextRow;
        column = swdRnd() & ( SWD_COLUMN_COUNT - 1 );
        if( swdGetCell( column, row ) == SWD_CELL_SPACE )
        {
            swdOneUpX = column << SWD_GRID_COORD_SHIFT;
            swdOneUpY = row << SWD_GRID_COORD_SHIFT;
            swdShowSpriteXY( swdOneUpX, swdOneUpY, swdOneUpSprite, SWD_PATTERN_ONEUP );
        }
        swdOneUpNextRow = swdOneUpNextRow + 1;
        if( swdOneUpNextRow >= SWD_ROW_COUNT )
          swdOneUpNextRow = 0;
    }
}

void swdHitOneUp( int x, int y )
{
    if( swdIsNear( swdOneUpX, swdOneUpY, x, y ) )
    {
        swdOneUpY = SWD_ONEUP_INVALID_Y;
        swdHideSprite( swdOneUpSprite );
        swdStartSfx( SWD_MELODY_UP );
        swdRemainCount = swdRemainCount + 1;
        swdPrintRemain();
    }
}


// -----------------------------------------------------------------------------
//   Enemy.cpp - CanMoveEnemy()/blocked-by checks shared by Monster and Ball.
// -----------------------------------------------------------------------------

bool swdIsBlockedByMonster( int excludeIndex, int column, int row )
{
    int i, monsterColumn, monsterRow;
    for( i = 0; i < SWD_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( i != excludeIndex )
        {
            if( ( swdMonsters[ i ].status & ( SWD_MONSTER_LIVE | SWD_MONSTER_START ) ) != 0 )
            {
                monsterColumn = swdMonsters[ i ].x >> SWD_GRID_COORD_SHIFT;
                monsterRow = swdMonsters[ i ].y >> SWD_GRID_COORD_SHIFT;
                if( monsterColumn == column && monsterRow == row ) return true;
                if( monsterColumn + swdMonsters[ i ].dx == column && monsterRow + swdMonsters[ i ].dy == row ) return true;
            }
        }
    }
    return false;
}

bool swdIsBlockedByBall( int excludeIndex, int column, int row )
{
    int i, ballColumn, ballRow;
    for( i = 0; i < SWD_MAX_BALL_COUNT; i = i + 1 )
    {
        if( i != excludeIndex )
        {
            if( ( swdBalls[ i ].status & SWD_BALL_LIVE ) != 0 )
            {
                ballColumn = swdBalls[ i ].x >> SWD_GRID_COORD_SHIFT;
                ballRow = swdBalls[ i ].y >> SWD_GRID_COORD_SHIFT;
                if( ballColumn == column && ballRow == row ) return true;
                if( ballColumn + swdBalls[ i ].dx == column && ballRow + swdBalls[ i ].dy == row ) return true;
            }
        }
    }
    return false;
}

bool swdCanMoveMonster( int index, int dx, int dy )
{
    int nextColumn, nextRow, cell;
    nextColumn = ( swdMonsters[ index ].x >> SWD_GRID_COORD_SHIFT ) + dx;
    if( nextColumn < 0 || nextColumn >= SWD_COLUMN_COUNT ) return false;
    nextRow = ( swdMonsters[ index ].y >> SWD_GRID_COORD_SHIFT ) + dy;
    if( nextRow < 0 || nextRow >= SWD_ROW_COUNT ) return false;
    cell = swdGetCell( nextColumn, nextRow );
    if( cell == SWD_CELL_WALL ) return false;
    if( swdIsBlockedByMonster( index, nextColumn, nextRow ) ) return false;
    if( swdIsBlockedByBall( -1, nextColumn, nextRow ) ) return false;
    return true;
}

bool swdCanMoveBall( int index, int dx, int dy )
{
    int nextColumn, nextRow, cell;
    nextColumn = ( swdBalls[ index ].x >> SWD_GRID_COORD_SHIFT ) + dx;
    if( nextColumn < 0 || nextColumn >= SWD_COLUMN_COUNT ) return false;
    nextRow = ( swdBalls[ index ].y >> SWD_GRID_COORD_SHIFT ) + dy;
    if( nextRow < 0 || nextRow >= SWD_ROW_COUNT ) return false;
    cell = swdGetCell( nextColumn, nextRow );
    if( cell == SWD_CELL_WALL ) return false;
    if( swdIsBlockedByMonster( -1, nextColumn, nextRow ) ) return false;
    if( swdIsBlockedByBall( index, nextColumn, nextRow ) ) return false;
    return true;
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void swdShowMonster( int index )
{
    int pattern, seq;
    pattern = swdMonsters[ index ].status & SWD_ACTOR_PATTERN_MASK;
    if( swdMonsters[ index ].dx != 0 || swdMonsters[ index ].dy != 0 )
    {
        seq = ( swdMonsters[ index ].x | swdMonsters[ index ].y ) & 1;
        pattern = pattern + seq;
    }
    swdShowSpriteXY( swdMonsters[ index ].x, swdMonsters[ index ].y, swdMonsters[ index ].sprite, pattern + SWD_PATTERN_MONSTER );
}

void swdDecideDirection( int index )
{
    int directions[4];
    int verticalIdx, horizontalIdx;
    int i, direction, dx, dy;
    int mx, my, px, py, pdx, pdy;

    mx = swdMan.x; my = swdMan.y;
    px = swdMonsters[ index ].x; py = swdMonsters[ index ].y;
    pdx = swdMonsters[ index ].dx; pdy = swdMonsters[ index ].dy;

    verticalIdx = 0;
    horizontalIdx = 0;

    if( swdAbs( mx, px ) > swdAbs( my, py ) )
    {
        if( mx < px )
        {
            if( pdx <= 0 )
            {
                directions[ 0 ] = SWD_DIR_LEFT;
                directions[ 3 ] = SWD_DIR_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = SWD_DIR_RIGHT;
                directions[ 3 ] = SWD_DIR_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pdx >= 0 )
            {
                directions[ 0 ] = SWD_DIR_RIGHT;
                directions[ 3 ] = SWD_DIR_LEFT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = SWD_DIR_LEFT;
                directions[ 3 ] = SWD_DIR_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( my < py && pdy <= 0 ) || pdy < 0 )
        {
            directions[ verticalIdx ] = SWD_DIR_UP;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = SWD_DIR_DOWN;
        }
        else
        {
            directions[ verticalIdx ] = SWD_DIR_DOWN;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = SWD_DIR_UP;
        }
    }
    else
    {
        if( my < py )
        {
            if( pdy <= 0 )
            {
                directions[ 0 ] = SWD_DIR_UP;
                directions[ 3 ] = SWD_DIR_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = SWD_DIR_DOWN;
                directions[ 3 ] = SWD_DIR_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pdy >= 0 )
            {
                directions[ 0 ] = SWD_DIR_DOWN;
                directions[ 3 ] = SWD_DIR_UP;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = SWD_DIR_UP;
                directions[ 3 ] = SWD_DIR_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares Man.x < pMonster->y here too (a real upstream
        // typo, not a transcription slip) - the exact same quirk already
        // found and kept faithful in this project's own Cracky port
        // (shared "Cate engine" boilerplate, same author).
        if( ( mx < py && pdx <= 0 ) || pdx < 0 )
        {
            directions[ horizontalIdx ] = SWD_DIR_LEFT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = SWD_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalIdx ] = SWD_DIR_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = SWD_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        dx = swdMonsterDirTable[ direction * 2 ];
        dy = swdMonsterDirTable[ direction * 2 + 1 ];
        if( swdCanMoveMonster( index, dx, dy ) )
        {
            swdMonsters[ index ].dx = dx;
            swdMonsters[ index ].dy = dy;
            swdMonsters[ index ].status = ( swdMonsters[ index ].status & ~SWD_ACTOR_PATTERN_MASK ) | ( direction << 1 );
            return;
        }
    }
    swdMonsters[ index ].dx = 0;
    swdMonsters[ index ].dy = 0;
}

void swdStartMonster()
{
    int loopCount, status, sprite, attemptIdx, j, inUse;
    for( loopCount = 0; loopCount < SWD_MAX_MONSTER_COUNT; loopCount = loopCount + 1 )
    {
        status = swdMonsters[ swdNextMonsterIndex ].status;
        if( ( status & SWD_MONSTER_AVAILABLE ) != 0 && ( status & ( SWD_MONSTER_START | SWD_MONSTER_LIVE ) ) == 0 )
        {
            sprite = SWD_SPRITE_MONSTER;
            for( attemptIdx = 0; attemptIdx < SWD_MONSTER_SPRITE_COUNT; attemptIdx = attemptIdx + 1 )
            {
                inUse = 0;
                for( j = 0; j < SWD_MAX_MONSTER_COUNT; j = j + 1 )
                {
                    if( ( swdMonsters[ j ].status & ( SWD_MONSTER_START | SWD_MONSTER_LIVE ) ) != 0 && swdMonsters[ j ].sprite == sprite )
                      inUse = 1;
                }
                if( inUse == 0 )
                {
                    swdMonsters[ swdNextMonsterIndex ].x = swdMonsters[ swdNextMonsterIndex ].startX;
                    swdMonsters[ swdNextMonsterIndex ].y = swdMonsters[ swdNextMonsterIndex ].startY;
                    swdMonsters[ swdNextMonsterIndex ].sprite = sprite;
                    swdMonsters[ swdNextMonsterIndex ].status = SWD_MONSTER_START | SWD_MONSTER_AVAILABLE;
                    swdMonsters[ swdNextMonsterIndex ].dx = 0;
                    swdMonsters[ swdNextMonsterIndex ].dy = 0;
                    swdMonsters[ swdNextMonsterIndex ].count = swdTimerValue;
                    return;
                }
                sprite = sprite + 1;
            }
            return;
        }
        swdNextMonsterIndex = swdNextMonsterIndex + 1;
        if( swdNextMonsterIndex >= SWD_MAX_MONSTER_COUNT )
          swdNextMonsterIndex = 0;
    }
}

void swdInitMonsters()
{
    int i, sprite, count;

    sprite = SWD_SPRITE_MONSTER;
    for( i = 0; i < SWD_MONSTER_SPRITE_COUNT; i = i + 1 )
    {
        swdHideSprite( sprite );
        sprite = sprite + 1;
    }

    count = swdStageMonsterCount[ swdStageIndex ];
    for( i = 0; i < count; i = i + 1 )
    {
        int b;
        b = swdStageMonsters[ swdStageIndex ][ i ];
        swdMonsters[ i ].x = swdToColumn( b ) << SWD_GRID_COORD_SHIFT;
        swdMonsters[ i ].y = swdToRow( b ) << SWD_GRID_COORD_SHIFT;
        swdMonsters[ i ].status = SWD_MONSTER_AVAILABLE;
        swdMonsters[ i ].startX = swdMonsters[ i ].x;
        swdMonsters[ i ].startY = swdMonsters[ i ].y;
    }
    for( i = count; i < SWD_MAX_MONSTER_COUNT; i = i + 1 )
      swdMonsters[ i ].status = 0;

    swdNextMonsterIndex = 0;
    for( i = 0; i < SWD_MONSTER_SPRITE_COUNT; i = i + 1 )
      swdStartMonster();

    for( i = 0; i < SWD_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( swdMonsters[ i ].status & SWD_MONSTER_START ) != 0 )
        {
            swdMonsters[ i ].status = swdMonsters[ i ].status | SWD_MONSTER_LIVE;
            swdDecideDirection( i );
            swdShowMonster( i );
        }
    }
}

void swdMoveMonsters()
{
    int i, status;
    for( i = 0; i < SWD_MAX_MONSTER_COUNT; i = i + 1 )
    {
        status = swdMonsters[ i ].status;
        if( ( status & SWD_MONSTER_LIVE ) != 0 )
        {
            if( ( ( swdMonsters[ i ].x | swdMonsters[ i ].y ) & SWD_GRID_COORD_MASK ) == 0 )
            {
                swdDecideDirection( i );
                swdHitMan( swdMonsters[ i ].x, swdMonsters[ i ].y );
            }
            swdMonsters[ i ].x = swdMonsters[ i ].x + swdMonsters[ i ].dx;
            swdMonsters[ i ].y = swdMonsters[ i ].y + swdMonsters[ i ].dy;
            swdShowMonster( i );
            if( ( ( swdMonsters[ i ].x | swdMonsters[ i ].y ) & SWD_COORD_MASK ) == 0 )
              swdHitMan( swdMonsters[ i ].x, swdMonsters[ i ].y );
        }
        else if( ( status & SWD_MONSTER_START ) != 0 )
        {
            int count;
            count = swdMonsters[ i ].count;
            if( count != 0 )
            {
                if( ( count << 2 ) < swdTimerValue )
                {
                    if( ( count & 1 ) == 0 )
                      swdShowMonster( i );
                    else
                      swdHideSprite( swdMonsters[ i ].sprite );
                }
                swdMonsters[ i ].count = count - 1;
            }
            else
            {
                status = status & ~SWD_MONSTER_START;
                status = status | SWD_MONSTER_LIVE;
                swdMonsters[ i ].status = status;
                swdMonsters[ i ].x = swdMonsters[ i ].startX;
                swdMonsters[ i ].y = swdMonsters[ i ].startY;
                swdDecideDirection( i );
                swdShowMonster( i );
            }
        }
    }
}

void swdHitMonsters( int x, int y )
{
    int i;
    for( i = 0; i < SWD_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( swdMonsters[ i ].status & SWD_MONSTER_LIVE ) != 0 )
        {
            if( swdIsNear( swdMonsters[ i ].x, swdMonsters[ i ].y, x, y ) )
            {
                swdMonsters[ i ].status = swdMonsters[ i ].status & ~( SWD_MONSTER_LIVE | SWD_MONSTER_START );
                swdHideSprite( swdMonsters[ i ].sprite );
                swdStartSfx( SWD_MELODY_HIT );
                swdStartPoint( swdMonsters[ i ].x, swdMonsters[ i ].y );
                swdStartMonster();
                swdStartOneUp();
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Ball.cpp
// -----------------------------------------------------------------------------

void swdShowBall( int index )
{
    swdShowSpriteXY( swdBalls[ index ].x, swdBalls[ index ].y, swdBalls[ index ].sprite, SWD_PATTERN_BALL );
}

void swdInitBalls()
{
    int sprite, i, count, b;
    sprite = SWD_SPRITE_BALL;
    count = swdStageBallCount[ swdStageIndex ];
    for( i = 0; i < count; i = i + 1 )
    {
        swdBalls[ i ].sprite = sprite;
        swdBalls[ i ].status = SWD_BALL_LIVE;
        b = swdStageBalls[ swdStageIndex ][ i ];
        swdBalls[ i ].x = swdToColumn( b ) << SWD_GRID_COORD_SHIFT;
        swdBalls[ i ].y = swdToRow( b ) << SWD_GRID_COORD_SHIFT;
        swdBalls[ i ].dx = 0;
        swdBalls[ i ].dy = 1;
        swdShowBall( i );
        sprite = sprite + 1;
    }
    for( i = count; i < SWD_MAX_BALL_COUNT; i = i + 1 )
    {
        swdHideSprite( sprite );
        swdBalls[ i ].status = 0;
        sprite = sprite + 1;
    }
}

void swdMoveBalls()
{
    int i;
    for( i = 0; i < SWD_MAX_BALL_COUNT; i = i + 1 )
    {
        if( ( swdBalls[ i ].status & SWD_BALL_LIVE ) != 0 )
        {
            if( ( ( swdBalls[ i ].x | swdBalls[ i ].y ) & SWD_GRID_COORD_MASK ) == 0 )
            {
                if( !swdCanMoveBall( i, swdBalls[ i ].dx, swdBalls[ i ].dy ) )
                  swdBalls[ i ].dy = -swdBalls[ i ].dy;
                swdHitMan( swdBalls[ i ].x, swdBalls[ i ].y );
            }
            swdBalls[ i ].x = swdBalls[ i ].x + swdBalls[ i ].dx;
            swdBalls[ i ].y = swdBalls[ i ].y + swdBalls[ i ].dy;
            swdShowBall( i );
            if( ( ( swdBalls[ i ].x | swdBalls[ i ].y ) & SWD_COORD_MASK ) == 0 )
              swdHitMan( swdBalls[ i ].x, swdBalls[ i ].y );
        }
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp - the rest (needs Monster's swdHitMonsters(), OneUp's
//   swdHitOneUp(), already both in scope above).
// -----------------------------------------------------------------------------

bool swdCanMove( int dx, int dy )
{
    int nextColumn, nextRow, cell;
    nextColumn = ( swdMan.x >> SWD_GRID_COORD_SHIFT ) + dx;
    if( nextColumn < 0 || nextColumn >= SWD_COLUMN_COUNT ) return false;
    nextRow = ( swdMan.y >> SWD_GRID_COORD_SHIFT ) + dy;
    if( nextRow < 0 || nextRow >= SWD_ROW_COUNT ) return false;
    cell = swdGetCell( nextColumn, nextRow );
    return cell != SWD_CELL_WALL;
}

void swdManShow()
{
    int pattern, seq;
    if( swdInvincibleCount != 0 && ( swdInvincibleCount & 1 ) != 0 )
    {
        swdHideSprite( swdMan.sprite );
        return;
    }
    pattern = swdMan.status & SWD_ACTOR_PATTERN_MASK;
    if( ( swdMan.status & SWD_MAN_ATTACKING ) != 0 )
    {
        swdShowSpriteXY( swdSword.x, swdSword.y, swdSword.sprite, pattern + 3 );
        pattern = pattern + 2;
    }
    else if( swdMan.dx != 0 || swdMan.dy != 0 )
    {
        seq = ( swdMan.x | swdMan.y ) & 1;
        pattern = pattern + seq;
    }
    swdShowSpriteXY( swdMan.x, swdMan.y, swdMan.sprite, pattern + SWD_PATTERN_MAN );
}

void swdInitMan()
{
    swdInvincibleCount = 0;
    swdMan.sprite = SWD_SPRITE_MAN;
    swdMan.status = 0;
    swdMan.dx = 0;
    swdMan.dy = 0;
    swdMan.x = swdToColumn( swdStageStart[ swdStageIndex ] ) << SWD_GRID_COORD_SHIFT;
    swdMan.y = swdToRow( swdStageStart[ swdStageIndex ] ) << SWD_GRID_COORD_SHIFT;
    swdManShow();
    swdSword.sprite = SWD_SPRITE_MAN + 1;
    swdHideSprite( swdSword.sprite );
    swdThrustCount = 1;
    swdManDirDx = -1;
    swdManDirDy = 0;
    swdManDirPattern = 0;
    // swdManKeyOn is deliberately NOT reset here - see gameSword_init()'s
    // own comment for why (upstream's `keyOn` is a static local inside
    // MoveMan() with no initializer, never touched by InitMan() at all).
}

void swdMoveMan()
{
    if( swdInvincibleCount != 0 )
      swdInvincibleCount = swdInvincibleCount - 1;

    if( ( ( swdMan.x | swdMan.y ) & SWD_GRID_COORD_MASK ) == 0 )
    {
        if( ( swdMan.status & SWD_MAN_ATTACKING ) != 0 )
        {
            if( !swdCanMove( swdMan.dx, swdMan.dy ) )
              swdMan.count = 0;
            else
              swdMan.count = swdMan.count - 1;

            if( swdMan.count == 0 )
            {
                swdMan.dx = 0;
                swdMan.dy = 0;
                swdMan.status = swdMan.status & ~SWD_MAN_ATTACKING;
                swdHideSprite( swdSword.sprite );
            }
            if( !isFirePressed() )
              swdManKeyOn = false;
        }
        else
        {
            int dx, dy, pattern;
            bool left, right, up, down, fire;
            dx = 0; dy = 0;
            pattern = swdMan.status & SWD_ACTOR_PATTERN_MASK;
            left = isLeftPressed();
            right = isRightPressed();
            up = isUpPressed();
            down = isDownPressed();
            fire = isFirePressed();

            if( left || right || up || down )
            {
                int newDx, newDy, newPattern;
                if( left )
                {
                    newDx = -1; newDy = 0; newPattern = 0;
                }
                else if( right )
                {
                    newDx = 1; newDy = 0; newPattern = 4;
                }
                else if( up )
                {
                    newDx = 0; newDy = -1; newPattern = 8;
                }
                else
                {
                    newDx = 0; newDy = 1; newPattern = 12;
                }

                dx = newDx;
                dy = newDy;
                if( swdCanMove( dx, dy ) )
                {
                    swdManDirDx = dx;
                    swdManDirDy = dy;
                    swdManDirPattern = newPattern;
                }
                else if( swdCanMove( swdManDirDx, swdManDirDy ) )
                {
                    dx = swdManDirDx;
                    dy = swdManDirDy;
                }
                else
                {
                    swdManDirDx = newDx;
                    swdManDirDy = newDy;
                    swdManDirPattern = newPattern;
                    dx = 0;
                    dy = 0;
                }
                pattern = swdManDirPattern;
            }
            swdMan.dx = dx;
            swdMan.dy = dy;
            swdMan.status = ( swdMan.status & ~SWD_ACTOR_PATTERN_MASK ) | pattern;

            if( fire )
            {
                if( !swdManKeyOn )
                {
                    dx = swdManDirDx;
                    dy = swdManDirDy;
                    if( swdCanMove( dx, dy ) )
                    {
                        swdMan.dx = dx;
                        swdMan.dy = dy;
                        swdMan.status = swdMan.status | SWD_MAN_ATTACKING;
                        swdSword.x = swdMan.x + ( dx << ( SWD_COORD_SHIFT + 1 ) );
                        swdSword.y = swdMan.y + ( dy << ( SWD_COORD_SHIFT + 1 ) );
                        swdMan.count = swdThrustCount;
                        swdPointRate = 0;
                        swdStartSfx( SWD_MELODY_ATTACK );
                        swdManKeyOn = true;
                        swdHitMonsters( swdSword.x, swdSword.y );
                    }
                }
            }
            else
              swdManKeyOn = false;
        }
    }

    swdMan.x = swdMan.x + swdMan.dx;
    swdMan.y = swdMan.y + swdMan.dy;

    if( ( swdMan.status & SWD_MAN_ATTACKING ) != 0 )
    {
        swdSword.x = swdSword.x + swdMan.dx;
        swdSword.y = swdSword.y + swdMan.dy;
        if( ( ( swdSword.x | swdSword.y ) & SWD_COORD_MASK ) == 0 )
          swdHitMonsters( swdSword.x, swdSword.y );

        swdMan.x = swdMan.x + swdMan.dx;
        swdMan.y = swdMan.y + swdMan.dy;
        swdSword.x = swdSword.x + swdMan.dx;
        swdSword.y = swdSword.y + swdMan.dy;
        if( ( ( swdSword.x | swdSword.y ) & SWD_COORD_MASK ) == 0 )
          swdHitMonsters( swdSword.x, swdSword.y );
    }

    swdManShow();

    if( ( ( swdMan.x | swdMan.y ) & SWD_GRID_COORD_MASK ) == 0 )
    {
        int column, row, cell;
        column = swdMan.x >> SWD_GRID_COORD_SHIFT;
        row = swdMan.y >> SWD_GRID_COORD_SHIFT;
        cell = swdGetCell( column, row );
        if( cell == SWD_CELL_ITEM )
        {
            swdSetCell( column, row, SWD_CELL_SPACE );
            swdAddScore( 10 );
            swdStartSfx( SWD_MELODY_UP );
            swdThrustCount = swdThrustCount + 1;
        }
        else if( cell == SWD_CELL_BOX )
        {
            swdSetCell( column, row, SWD_CELL_SPACE );
            swdAddScore( 5 );
            swdBoxCount = swdBoxCount - 1;
            swdStartSfx( SWD_MELODY_HIT );
        }
        swdHitOneUp( swdMan.x, swdMan.y );
    }
}

void swdShowMan()
{
    swdInvincibleCount = 0;
    swdHideSprite( swdSword.sprite );
    swdManShow();
}


// -----------------------------------------------------------------------------
//   InitStage - flattens Stage.cpp's own real stage-selection/unpack/init
//   sequence (stage-index wraparound + TimerValue derivation, then unpacking
//   the packed 2-bit-per-cell wall map into swdStageMap, then re-initializing
//   every entity module for the freshly-selected stage).
// -----------------------------------------------------------------------------

void swdInitStage()
{
    int i, j, row, column, byteIdx, b, sub, cell;

    swdRndIndex = 0;
    swdTimerValue = SWD_MAX_TIMER_VALUE;
    {
        int p, count;
        p = 0;
        count = swdCurrentStage;
        i = 0;
        while( count != 0 )
        {
            p = p + 1;
            i = i + 1;
            if( i >= SWD_STAGE_COUNT )
            {
                p = 0;
                i = 0;
            }
            swdTimerValue = swdTimerValue - 1;
            if( swdTimerValue < SWD_MIN_TIMER_VALUE )
              swdTimerValue = SWD_MIN_TIMER_VALUE;
            count = count - 1;
        }
        swdStageIndex = p;
    }

    // ClearScreen()'s own status-grid-relevant half - prevents whatever the
    // title screen (or a previous stage's own HUD) last left in a status
    // cell from silently persisting into this stage's own SCORE/STAGE/TIME
    // text wherever both land on the same page - the exact same "clear
    // cache/overlay state that doesn't get naturally overwritten" lesson
    // already documented (and fixed) in Cracky's own crkInitTrying().
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        swdStatusChar[ i ][ j ] = 0;
    swdOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in swdUpdateTitle()) - matches swdOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches swdInitStage() without going through that transition first.
    swdFullWidthText = false;
    swdPrintStatus();

    swdBoxCount = 0;
    for( i = 0; i < SWD_MAP_SIZE; i = i + 1 )
      swdStageMap[ i ] = 0;
    row = 0;
    while( row < SWD_ROW_COUNT )
    {
        column = 0;
        for( byteIdx = 0; byteIdx < SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE; byteIdx = byteIdx + 1 )
        {
            b = swdStageBytes[ swdStageIndex ][ row * ( SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE ) + byteIdx ];
            for( sub = 0; sub < SWD_COLUMNS_PER_BYTE; sub = sub + 1 )
            {
                cell = b & 0x03;
                if( cell == SWD_CELL_BOX )
                  swdBoxCount = swdBoxCount + 1;
                swdSetCell( column, row, cell );
                b = b >> 2;
                column = column + 1;
            }
        }
        row = row + 1;
    }

    swdInitMan();
    swdInitMonsters();
    swdInitBalls();
    swdInitPoints();
    swdInitOneUp();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void swdDrawBackGround()
{
    int x, row, colGroup, sub, b, cell;

    for( x = 0; x < SWD_VVRAM_WIDTH; x = x + 1 )
      swdVVram[ 0 ][ x ] = SWD_CHAR_FENCE;

    for( row = 0; row < SWD_ROW_COUNT; row = row + 1 )
    {
        for( colGroup = 0; colGroup < SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE; colGroup = colGroup + 1 )
        {
            b = swdStageMap[ row * ( SWD_COLUMN_COUNT / SWD_COLUMNS_PER_BYTE ) + colGroup ];
            for( sub = 0; sub < SWD_COLUMNS_PER_BYTE; sub = sub + 1 )
            {
                int column, vx, vy, c;
                cell = b & 0x03;
                column = colGroup * SWD_COLUMNS_PER_BYTE + sub;
                vx = column * SWD_COLUMN_WIDTH;
                vy = 1 + row * SWD_ROW_HEIGHT;
                if( cell == SWD_CELL_SPACE )
                {
                    swdVVram[ vy ][ vx ] = SWD_CHAR_SPACE;
                    swdVVram[ vy ][ vx + 1 ] = SWD_CHAR_SPACE;
                    swdVVram[ vy + 1 ][ vx ] = SWD_CHAR_SPACE;
                    swdVVram[ vy + 1 ][ vx + 1 ] = SWD_CHAR_SPACE;
                }
                else
                {
                    c = SWD_CHAR_WALL + ( ( cell - 1 ) << 2 );
                    swdVVram[ vy ][ vx ] = c; c = c + 1;
                    swdVVram[ vy ][ vx + 1 ] = c; c = c + 1;
                    swdVVram[ vy + 1 ][ vx ] = c; c = c + 1;
                    swdVVram[ vy + 1 ][ vx + 1 ] = c;
                }
                b = b >> 2;
            }
        }
    }

    for( x = 0; x < SWD_VVRAM_WIDTH; x = x + 1 )
      swdVVram[ SWD_VVRAM_HEIGHT - 1 ][ x ] = SWD_CHAR_FENCE + 1;
}

void swdDrawAll()
{
    swdDrawBackGround();
    swdDrawSpritesIntoVVram();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly - see
// Cracky's own header comment (this project's other Cate-engine port) for
// the full derivation; the two files' own composite math is byte-for-byte
// identical, only the underlying data tables differ.
//
// **OR-combines mapByte (VVram/map-derived) with textByte (swdStatusChar-
// derived) instead of choosing one exclusively**, matching Cracky's own
// identical fix: while swdFullWidthText is true (the title screen only),
// the real "SWORD" logo bitmap drawn into swdVVram (see swdBeginTitle())
// and the title's own status text (MINI/START/CONTINUE/credit) need to
// coexist on screen at once. Safe by construction, not just by luck: the
// logo's own real footprint (real columns 16-79, hardware pages 0-4) never
// shares a nonzero byte with any title-screen text string - MINI (page 4)
// is the only string sharing a page with the logo at all, and the logo's
// own page-4 content (VVram row 8, the last data row) is entirely blank
// (space) across its whole width, confirmed directly against the data
// table above - every other string (START/CONTINUE/credit) lives on pages
// 5-7, which the logo never touches (it only spans pages 0-4).
int swdComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < SWD_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = swdVVram[ rawPage * 2 ][ mapX ];
        lower = swdVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = swdCharPattern[ upper * 2 + 0 ];
            lowerByte = swdCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = swdCharPattern[ upper * 2 + 0 ];
            lowerByte = swdCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = swdCharPattern[ upper * 2 + 1 ];
            lowerByte = swdCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = swdCharPattern[ upper * 2 + 1 ];
            lowerByte = swdCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !swdFullWidthText && rawCol < SWD_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // swdStatusChar's own full-width indexing directly - no "subtract the
    // map width" local-offset math needed, since rawCol/4 already lands on
    // the correct real column either way (whether this is the
    // swdFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = swdStatusChar[ rawPage ][ charCol ];
            textByte = swdAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void swdRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( swdOverlayActive && page == swdOverlayPage &&
                col >= swdOverlayCol * 4 && col < swdOverlayCol * 4 + swdOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - swdOverlayCol * 4 ) / 4;
                sub = ( col - swdOverlayCol * 4 ) % 4;
                value = swdAsciiPattern[ swdAsciiIndex( swdOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = swdComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void swdBeginStage()
{
    swdInitStage();
    swdDrawAll();
    // swdMonsterNum is deliberately NOT reset here - see gameSword_init()'s
    // own comment for why (upstream's `monsterNum` is a static local with a
    // constant initializer, `static sbyte monsterNum = 0;`, sitting inside
    // Main()'s own goto-reachable `stage:` block - real C++ semantics only
    // ever run that initializer once, the very first time control reaches
    // it, not on every `goto stage;` re-entry).
    swdActiveTickIndex = 0;
    swdTickCounter = 0;
    swdStartSeq( 1, SWD_MELODY_START );
    swdState = SWD_STATE_START_JINGLE;
    swdRender();
}

// **Rewritten after a real user-supplied hardware photo of Cracky (this
// project's other Cate-engine port) proved the previous version of this
// whole batch's title-screen text model was simply wrong.** The earlier
// version believed upstream's own title-screen text collided with the
// SCORE/STAGE/REMAIN status labels and had to be crammed into the same
// narrow 8-cell status grid - now fixed (see swdStatusChar's own header
// comment), this function is rewritten to place everything at upstream's
// real, literal columns, with `swdFullWidthText=true` so
// swdComposeRawByte() renders the full canvas instead of just the narrow
// status zone.
void swdBeginTitle()
{
    int i, j;
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };

    for( i = 0; i < SWD_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < SWD_VVRAM_WIDTH; j = j + 1 )
        swdVVram[ i ][ j ] = SWD_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        swdStatusChar[ i ][ j ] = 0;
    swdOverlayActive = false;
    swdFullWidthText = true;
    swdHideAllSprites();
    swdPrintStatus();

    // **Restored, matching Cracky's own identical fix (the "second
    // architectural issue" found in this Cate-engine batch)**: this is
    // upstream's own real "SWORD" logo bitmap (Status.cpp's `Title()`,
    // `TitleBytes[]`), drawn directly into swdVVram at its own real
    // position - `LogoLeft = (VVramWidth(24) - LogoWidth(16)) / 2 = 4`,
    // `VVram + VVramWidth*1 + LogoLeft`, i.e. VVram row 1, column 4,
    // spanning 16 columns x 8 rows (real hardware pages 0-4). The earlier
    // version of this function replaced this real wordmark with plain
    // small text ("SWORD"), reasoning it was "purely decorative" - the
    // same mistake Cracky's own title screen made and had already fixed
    // (see that file's own header comment for the full story: it's the
    // single biggest, most prominent element on the whole screen, not a
    // throwaway detail).
    //
    // **Data order note**: unlike Cracky's own TitleBytes (letter-major -
    // each letter's own 4x4 block written in full before moving to the
    // next letter), Sword's own upstream loop is a flat, row-major raster
    // across the WHOLE 16-wide logo block (`repeat(LogoHeight){
    // repeat(LogoWidth){ pVVram=VPut(pVVram,*p); ++p; } pVVram +=
    // VVramWidth-LogoWidth; }` - LogoWidth spans all 4 letter-columns
    // before ever dropping to the next row) - confirmed directly from
    // upstream's own real loop shape before choosing this port's own loop
    // structure, not assumed identical to Cracky's just because both games
    // share the same engine. Reproduced with the matching row-major loop
    // below rather than Cracky's own letter-major one.
    //
    // `swdComposeRawByte()` was updated to OR-combine this VVram content
    // with swdStatusChar's own text layer rather than choosing one
    // exclusively - see that function's own header comment for why this is
    // safe (the logo's own real footprint never actually shares a nonzero
    // byte with any title-screen text string, confirmed by checking the
    // data directly rather than assumed).
    {
        int row, col, idx;
        idx = 0;
        for( row = 0; row < 8; row = row + 1 )
          for( col = 0; col < 16; col = col + 1 )
          {
              swdVVram[ 1 + row ][ 4 + col ] = swdTitleBytes[ idx ];
              idx = idx + 1;
          }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 16, START/CONTINUE at col 9 with
    // the cursor at col 8, the credit line at col 12) - all genuinely
    // clear of the status labels' own columns 24-31, so nothing here needs
    // trimming, relocating, or dropping anymore.
    swdPrintS( 4, 16, sMini, 4 );
    swdPrintS( 5, 9, sStart, 5 );
    swdPrintS( 6, 9, sContinue, 8 );
    swdPrintS( 7, 12, sCredit, 12 );

    swdSelection = 0;
    swdSelectionChanged = true;
    swdPrevLeft = false; swdPrevRight = false; swdPrevUp = false; swdPrevDown = false; swdPrevFire = false;
    swdState = SWD_STATE_TITLE;
}

void swdUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !swdPrevLeft ) || ( right && !swdPrevRight ) ||
                ( up && !swdPrevUp ) || ( down && !swdPrevDown ) );
    justFire = ( fire && !swdPrevFire );
    swdPrevLeft = left; swdPrevRight = right; swdPrevUp = up; swdPrevDown = down; swdPrevFire = fire;

    if( swdSelectionChanged )
    {
        swdSelectionChanged = false;
        if( swdSelection == 0 )
          swdPrintC( 5, 8, '>' );
        else
          swdPrintC( 5, 8, ' ' );
        if( swdSelection == 1 )
          swdPrintC( 6, 8, '>' );
        else
          swdPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        swdFullWidthText = false;
        swdScore = 0;
        if( swdSelection == 0 )
          swdCurrentStage = 0;
        swdRemainCount = 3;
        swdBeginStage();
        return;
    }
    if( justDir )
    {
        swdSelection = swdSelection ^ 1;
        swdSelectionChanged = true;
    }
    swdRender();
}

void swdUpdateStartJingle()
{
    if( !swdSeqPlaying( 1 ) )
    {
        swdStartBgm();
        swdState = SWD_STATE_PLAYING;
    }
    swdRender();
}

void swdUpdateGameOverWait()
{
    if( swdWaitFrames > 0 )
    {
        swdWaitFrames = swdWaitFrames - 1;
        swdRender();
        return;
    }
    swdPrintGameOver();
    swdStartSeq( 1, SWD_MELODY_GAMEOVER );
    swdState = SWD_STATE_GAMEOVER_JINGLE;
    swdRender();
}

void swdUpdateGameOverJingle()
{
    if( !swdSeqPlaying( 1 ) )
      swdBeginTitle();
    swdRender();
}

void swdUpdateClearWait()
{
    if( swdWaitFrames > 0 )
    {
        swdWaitFrames = swdWaitFrames - 1;
        swdRender();
        return;
    }
    swdStartSeq( 1, SWD_MELODY_CLEAR );
    swdState = SWD_STATE_CLEAR_JINGLE;
    swdRender();
}

void swdUpdateClearJingle()
{
    if( !swdSeqPlaying( 1 ) )
    {
        swdCurrentStage = swdCurrentStage + 1;
        swdBeginStage();
        return;
    }
    swdRender();
}

void swdUpdatePlaying()
{
    swdTickCounter = swdTickCounter + 1;
    if( swdTickCounter < SWD_TICK_DIVISOR )
    {
        swdRender();
        return;
    }
    swdTickCounter = 0;

    swdMoveMan();
    if( swdMonsterNum >= 0 )
    {
        swdMoveMonsters();
        swdMonsterNum = swdMonsterNum - 7;
    }
    swdMonsterNum = swdMonsterNum + 4;
    swdMoveBalls();
    swdUpdatePoints();
    swdDrawAll();

    if( ( swdActiveTickIndex % 4 ) == 0 )
      swdTickCounter = -SWD_TICK_DIVISOR;
    swdActiveTickIndex = swdActiveTickIndex + 1;

    if( swdRemainCount == 0 )
    {
        swdShowMan();
        swdDrawAll();
        swdStopBgm();
        swdWaitFrames = 30;
        swdState = SWD_STATE_GAMEOVER_WAIT;
        swdRender();
        return;
    }

    if( swdBoxCount == 0 )
    {
        swdStopBgm();
        swdShowMan();
        swdDrawAll();
        swdWaitFrames = 30;
        swdState = SWD_STATE_CLEAR_WAIT;
        swdRender();
        return;
    }

    swdRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameSword_init()
{
    int i;

    swdHiScore = 0;
    swdScore = 0;
    swdCurrentStage = 0;
    swdRemainCount = 3;
    swdRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        swdSeqActive[ i ] = 0;
        swdSeqMelody[ i ] = SWD_MELODY_NONE;
    }
    swdOverlayActive = false;
    swdTickCounter = 0;
    swdActiveTickIndex = 0;

    // swdMonsterNum/swdManKeyOn are reset ONLY here, matching upstream's own
    // real one-time-only initialization: Main.cpp's `static sbyte monsterNum
    // = 0;` sits inside a goto-reachable block (the `stage:` label, re-
    // entered on every new stage/game), but a static local's constant
    // initializer only actually runs the first time control ever reaches
    // it - every later `goto stage;` skips right past it, leaving
    // monsterNum to simply carry over whatever value the previous stage's
    // own move-throttle loop left it at. Man.cpp's `static bool keyOn;`
    // (inside MoveMan(), no initializer at all) has the identical shape -
    // implicitly zero-initialized once, at real cold boot, and never
    // touched by InitMan() on any subsequent stage. Resetting either one
    // on every swdBeginStage()/swdInitMan() call (as an earlier version of
    // this port did) doesn't match that real semantic - it's a subtle but
    // genuine state-progression deviation (a slightly different monster-
    // movement-throttle phase per stage; keyOn resetting to "not held"
    // instead of carrying over whether Fire was already down when a stage/
    // game ended). Resetting them here instead reproduces the real
    // program's one-time cold-boot init, matching every other explicit
    // reset in this function (HiScore/Score/etc are also upstream globals
    // only ever set once, at the very top of Main()).
    swdMonsterNum = 0;
    swdManKeyOn = false;

    swdBeginTitle();
}

void gameSword_update()
{
    swdAdvanceSound();

    if( swdState == SWD_STATE_TITLE )
      swdUpdateTitle();
    else if( swdState == SWD_STATE_START_JINGLE )
      swdUpdateStartJingle();
    else if( swdState == SWD_STATE_PLAYING )
      swdUpdatePlaying();
    else if( swdState == SWD_STATE_GAMEOVER_WAIT )
      swdUpdateGameOverWait();
    else if( swdState == SWD_STATE_GAMEOVER_JINGLE )
      swdUpdateGameOverJingle();
    else if( swdState == SWD_STATE_CLEAR_WAIT )
      swdUpdateClearWait();
    else if( swdState == SWD_STATE_CLEAR_JINGLE )
      swdUpdateClearJingle();
}
