#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// GUNTUS mini (inufuto, UIAPduino+SSD1306 edition, "Cate engine", CH32V003
// RISC-V microcontroller - license "None specified", no LICENSE file in the
// upstream repo) - a Galaga/Xevious-style vertical shooter: steer a fighter
// ship in 4 directions, shoot waves of enemies that fly in along fixed
// courses, form up into rows near the bottom, then peel off individually to
// dive-attack and fire back; clear every enemy to advance to the next of 8
// hand-authored stages, 3 lives, score-only (no hi-score concept upstream at
// all - confirmed by grep, unlike sibling port GUNTUS's own CRACKY).
//
// Ported using gameCracky.c (a sibling Cate-engine game already shipped in
// this cartridge) as the direct structural reference - same author/engine
// lineage (identical AsciiPattern/Frequencies tables, identical VVram/
// CharPattern nibble-interleaved rendering scheme, identical Math.cpp
// Rnd()/Abs()/Sign() helpers). **No hardware-orientation transform of any
// kind is needed** - `gunComposeRawByte()` composes a byte and
// `md_drawColumn(col,page,value)` draws it at its own natural position, no
// mirroring/reversal - matching Cracky's own hard-won conclusion (see that
// file's header) that GUNTUS's own `InitOled()` RightToLeft/BottomToTop
// commands exist purely to correct a real panel-mounting quirk on certain
// physical hardware, with nothing to correct for in software here.
//
// **A second hidden-row staging area, a genuinely different VVram shape
// than Cracky's own**: `VVramHeight = 16 + 2 = 18` (not just 16) - real
// gameplay rendering (`DrawAll()` -> `VVramToVram(VVram + VVramWidth*2)`)
// only ever converts rows 2..17 to real hardware pixels, permanently
// skipping rows 0-1. Traced through `Direction.cpp`'s own `Courses[]` table
// to confirm this is deliberate, not incidental: two of the 8 flight
// courses spawn at row 0 (`{ 12, 0, Direction_Down, ... }`) or exactly the
// bottom visible row 17 (`{ 13, VVramHeight-1, Direction_Up, ... }`) -
// enemies genuinely spawn *above* the visible screen (row 0, invisible)
// and fly down into view, the same "off-screen staging area" idea other
// shooters give a signed/negative Y coordinate, just implemented here as 2
// permanently-hidden VVram rows instead. Reproduced directly:
// `gunComposeRawByte()`'s map-area branch reads `gunVVram[rawPage*2+2]`/
// `[+3]` (a literal `+2` baked into the row lookup) rather than needing any
// runtime row-offset parameter - sprites/stars/enemy-rows all write into
// the full 0-17 row range exactly as upstream does, with rows 0-1 simply
// never making it to `md_drawColumn()`.
//
// **The title screen's own real bitmap logo (`TitleBytes[]`) is drawn via
// a completely different, UNSHIFTED `VVramToVram(VVram)` call than every
// gameplay frame uses (no `+VVramWidth*2`) - a genuinely different row
// addressing for this one screen only, not just cosmetic text.** An
// earlier version of this port "simplified" this real, prominent pixel-
// art wordmark down to small plain text instead (matching an equally
// wrong earlier decision in sibling game Cracky) - **corrected, later in
// the same session, once Cracky's own identical mistake was found and
// fixed first**: `gunTitleBytes[]` (byte-diff-verified against the real
// upstream table) is now drawn directly into `gunVVram` at its own real
// position (row 2, matching upstream's `VVram + VVramWidth*2 + TitleLeft`
// offset), and `gunComposeRawByte()` reads it back out with the correct
// UNSHIFTED row addressing (`rawPage*2`/`+1`, not the gameplay-only
// `rawPage*2+2`/`+3`) whenever `gunFullWidthText` is set - landing the
// logo on real hardware pages 1-2, OR-combined with the "MINI"/
// "INUFUTO 2026"/"START"/"CONTINUE" text drawn into the same real Vram
// text mechanism the status labels use (see `gunComposeRawByte()`'s and
// `gunBeginTitle()`'s own comments for the full derivation, and the later
// "CORRECTED" note further down in this header for the separate, already-
// fixed story of how this area's own real *width* was first modeled
// wrong, then fixed).
//
// **Status text**: GUNTUS's own `PrintStatus()`/`PrintScore()`/
// `PrintRemain()` write real bytes directly to hardware Vram addresses
// (bypassing VVram) at real columns 24-31 (of a genuine 32-char-cell-wide
// row, not just an 8-wide slice - see the "CORRECTED" note below) - the
// exact same address range Cracky's own `crkStatusChar` overlay already
// covers, so the identical grid design (`gunStatusChar`, `gunPrintC`/
// `gunPrintS` writing into it, `gunComposeRawByte()`'s status-area branch
// reading back out of it) was reused directly. `PrintRemain()`'s own
// `Put2C`-drawn 2x2 "extra life"
// ship icon is simplified to a plain digit, matching Cracky's own PrintRemain
// simplification exactly - and, faithfully preserving a genuine upstream
// quirk rather than "fixing" it, nothing is drawn at all (the previous
// frame's own digit just lingers) once `RemainCount` drops to 1 or below,
// since upstream's own `PrintRemain()` only ever writes when
// `RemainCount > 1`. `PrintGameOver()` (page4, char-cell col8, i.e. inside
// the MAP area, not the status area) needed the same kind of overlay-over-
// map trick Cracky's own `crkOverlayActive` mechanism already provides -
// reused directly (`gunOverlayActive`/`gunOverlayText`/`gunOverlayPage`/
// `gunOverlayCol`).
//
// **Sound**: the same real 3-tone-channel software-mixer tracker as
// Cracky's own `Sound.cpp` (`ToneChannel`/`EffectChannel`, a genuine PWM
// synth driven off the SysTick ISR) - not ported at the synthesis level,
// routed straight to `md_playTone(freqHz, durationSeconds)` instead,
// exactly like Cracky. Tempo differs (180 here vs Cracky's own 160), so
// the real per-note-length-unit duration was re-derived from
// `SoundHandler()`'s own formula (`time -= Tempo; if(time<=0) time +=
// 600/2;`): a decrement-event happens once every `300/Tempo` real 60Hz
// ticks, giving `gunNoteFrames(length) = round(length * 300.0/180.0)`
// (~1.667 ticks/unit here, vs Cracky's own 1.875). `Sound_SmallBang()`/
// `Sound_LargeBang()` are genuine NOISE-channel effects (a real LFSR-
// driven `EffectChannel`, not a melody) with no queued-note equivalent to
// port - approximated as a single representative `md_playTone()` call at
// the effect's own real starting frequency (3000Hz/1500Hz) with a short
// fixed duration, the same "derive the real Hz, approximate the envelope"
// treatment Astro Barrier's own hardware-synth port already established
// in this project. Every melody's own note-pair data (`Sound_Fire`/
// `Sound_Up`/`Sound_Start`/`Sound_GameOver`/`StartBGM`'s two simultaneous
// tracks) was extracted via a small Python script (resolving the real
// `NoteLength`/`Scale` enum values), not hand-transcribed - byte-diff-
// verified counts (13/13/23/18/105/227 values respectively) rather than
// trusted on a first eyeballed copy, per this project's own long-standing
// anti-transcription-bug discipline.
//
// **Data**: `Stages[]`/`Formations0-7[]` (75 formations across 8 stages,
// each a `{elementCount,courseIndex,targets[8],types[2]}` C++ struct
// array) were flattened via the same script into parallel flat arrays
// (`gunFormationElementCount`/`gunFormationCourseIndex`/
// `gunFormationTargets`/`gunFormationTypes`, plus a per-stage
// `gunStageFormationStart`/`gunStageFormationCount` window into the one
// combined 75-entry table) - avoids porting a struct-with-a-pointer-to-
// another-struct-array (`Stage::pFormation`), matching this project's own
// established "flatten to plain parallel arrays" precedent. `Courses[]`'s
// own `elements[10]` fields are shorter than 10 in several C++ literals
// (e.g. `{ 12, 3, 8, 9, 12, 10, 15, InvalidElement }`, only 8 explicit
// values) - C++ aggregate-initialization zero-pads the remaining slots,
// *not* with another `InvalidElement`(255) sentinel - reproduced exactly
// (zero-padded, not 255-padded) via the same extraction script, since a
// literal `0` there is itself a *valid* course-element table index (not a
// second "end" marker) - though tracing the real control flow
// (`MoveTeams()`'s own `courceElementIndex` advance, gated by each
// team's members finishing their course and draining to 0) shows this
// padding is essentially unreachable in practice, a defensive index
// clamp was still added before the array read (see `gunMoveTeams()`)
// rather than trusting that reasoning to hold under every possible
// timing.
//
// **AVR-implicit-byte-wraparound reliance, the same bug family already
// documented extensively elsewhere in this project (byte truncation,
// signed-sentinel, logical-vs-arithmetic-shift, etc) - found via careful
// line-by-line reading before ever compiling, not via a crash report**:
// several `byte`-typed position fields (`MovingEnemy::x/y` in `Move()`,
// `EnemyRowLeft` in `MoveEnemyRows()`, `EnemyBullet::x/y` in
// `MoveEnemyBullets()`, `FighterBullet::y` in `MoveFighterBullets()`,
// `Bang`'s own `Show()` helper) rely on a byte genuinely wrapping past 0
// (to 255) or past 255 (to 0) so that a *much smaller* real-hardware range
// check (e.g. `>= RangeX+1` where RangeX is ~22) catches the wrapped value
// as "out of range" and redirects/ends the object - exactly the intended
// "hit the edge of the playfield" detection. Vircon32's plain, non-
// truncating `int`s would instead let these go genuinely negative,
// silently failing every one of those range checks. **Fixed** the same
// way as this project's very first documented bug: an explicit `& 0xFF`
// mask applied right after each such arithmetic step, at every site listed
// above - confirmed via inspection that this reproduces the exact real
// upstream byte value in every case (all of these mask a *plain sum* of
// already-computed operands, so wrapping once at the end vs. per-step,
// as AVR's own byte arithmetic would, gives identical results by modular-
// arithmetic associativity).
//
// **`switch` and the ternary operator avoided proactively throughout**
// (this dialect's own well-documented lack of support for both,
// established by every prior port in this project) - `DecideDirection()`'s
// one `switch(type)` and `MoveMovingEnemies()`'s one `switch(status)`
// both rewritten as `if`/`else` chains; every `a?b:c` in `Print.cpp`/
// `Bang.cpp` rewritten as plain `if`/`else`.
//
// **Two same-function `goto`s used as genuine structured control flow
// (not blocking-loop conversions) in upstream, both rewritten goto-free**:
// `Team.cpp`'s `MoveTeams()` uses `goto nextCourseElement;` to jump into
// the middle of an `if` block from its own `else` branch - rewritten with
// a small `doLookup` boolean flag gating the shared lookup-and-apply logic
// that both paths need, rather than reproducing the jump. `MovingEnemy.cpp`'s
// `MoveMovingEnemies()` uses two: `goto turn;` (from the Align case into
// the Turn case's own label, when a distance check passes) and
// `goto align;` (from the Return case, re-using the Align case's own
// logic every other tick) - both cases' *shared bodies* were pulled out
// into their own functions (`gunTurnLogicMovingEnemy()`/
// `gunAlignMovingEnemy()`), called directly from every dispatch site that
// upstream's own goto target reaches, rather than trying to replicate
// C++'s cross-case label-jump semantics.
//
// **A deliberate, documented deviation from a literal port**: upstream's
// own `DecideDirection()` for `Type_Insistent` declares `sbyte dx, dy;`
// with NO initial value, only assigning them inside an
// `if ((localClock & LongMask) != 0)` block - meaning on the 1-in-4 calls
// where that condition is false, `dx`/`dy` are read as genuinely
// uninitialized stack memory on real AVR hardware (a real, if minor,
// latent bug in the original game, not a deliberate quirk with any
// well-defined "intended" result to preserve). Rather than reproduce
// undefined behavior (impossible to define meaningfully on this platform
// anyway) or silently default to `(0,0)` (a fresh, arbitrary choice with
// no real basis), the port instead reuses the enemy's *own currently-
// stored* `dx`/`dy` (already-persistent struct fields) on that branch -
// the most plausible real interpretation of the evident intent ("mostly
// keep going the same way, only re-target periodically"), documented here
// as a deliberate choice rather than left as an unexamined guess.
//
// =============================================================================
// A later verification pass (this port had only ever been test-compiled,
// never played) - full re-audit against real upstream source, plus live
// Puppeteer testing on an isolated build/server. Found and fixed 3 real
// bugs, the first of which was almost certainly the actual cause of any
// "game state progression" issue reported for this title:
//
// **1) CRITICAL - a genuinely unterminated sound-melody array left the
// game permanently stuck on the GAME OVER screen.** Counting the real
// token list in `Sound.cpp`'s `Sound_GameOver()` directly (not eyeballed)
// showed its `notes[]` array holds exactly 18 bytes (9 real length/note
// pairs) with **no trailing terminator byte** - unlike `Sound_Fire()`/
// `Sound_Up()`/`Sound_Start()`, which all end with an explicit extra `0`.
// This is a genuine bug in the *original* game, not a porting slip: on
// real AVR flash it's harmless-by-luck (`ToneChannel::Next()` just reads
// whatever byte happens to sit right after the array in ROM as if it
// were the next note), but this port's `gunMelodyGameOver` was declared
// `int[18]` with the same 18 real values and nothing after it - reading
// index 18 (`gunMelodyValue(GUN_MELODY_GAMEOVER, 18)`) lands directly in
// the very next declared global, `gunMelodyBgm1[0]` (=18, non-zero), so
// the sequencer's own "did we hit a real 0-length end marker" check never
// fires and `gunSeqPlaying(1)` never goes false - `gunUpdateGameOverJingle()`
// then never calls `gunBeginTitle()`, permanently freezing on GAME OVER
// (misinterpreting BGM1's own note data as an endless stream of more
// GameOver notes). **Confirmed live**: an early Puppeteer soak test
// reliably reached GAME OVER through real play, then sat on that exact
// screen for 10+ real seconds with zero further state change - many times
// longer than the melody's own real ~2.9s duration. **Fixed** by adding
// the missing terminator (`gunMelodyGameOver` extended from 18 to 19
// elements, trailing `0` appended) - reproduces what was clearly the
// intended upstream behavior rather than the real out-of-bounds read.
// Re-verified two ways after rebuilding: (a) a temporary deterministic
// debug hook (forcing `gunRemainCount=0` a few ticks into a fresh game,
// fully removed again afterward - confirmed via a project-wide grep for
// its own marker name) showed a clean GAME OVER -> title cycle completing
// within the expected ~7s window; (b) a second, undoctored soak test
// reaching GAME OVER through genuine random collisions also cleanly
// returned to the title screen. `gunMelodyLength()`'s own (dead-code,
// never actually called - confirmed by grep) GAMEOVER case was also
// updated to report 19 for consistency, though it has no effect on
// behavior either way.
//
// **2) A real out-of-bounds array read/write in the enemy-row attack
// dispatcher, found by careful reading, not a crash report.**
// `gunMoveEnemyRows()`'s attack-row picker decrements `gunNextRow` every
// time it needs to search for a live row to promote to an attacker,
// relying - like upstream's own real `byte nextRow` - on the value
// *wrapping* past 0 to a large number (255 on real AVR) so the following
// `if (nextRow >= EnemyRowCount) nextRow = EnemyRowCount - 1;` catches it
// and resets it. Vircon32's plain, non-wrapping `int` instead lets
// `gunNextRow` go genuinely negative (-1, -2, ...) - which a `>=` check
// against a small positive `EnemyRowCount` can never catch - so once
// every row has been cycled through once, `gunNextRow` keeps counting
// down forever, and `gunStartAttacking(rowIndex)` gets called with a
// **negative index**, reading/writing `gunEnemyRowMemberCount[-1]`,
// `gunEnemyRowType[-1]`, `gunEnemyRowFlags[-1][...]`, etc - a genuine
// out-of-bounds memory access that would eventually corrupt an unrelated
// global or hard-crash with "ERROR: INVALID MEMORY READ/WRITE" the longer
// a level's own attack-row rotation kept running (this is the same "the
// row-formation loop reruns every ~16 ticks for as long as enemies
// remain in the stage" cadence that made the earlier GAME OVER bug so
// easy to hit too - a normal playthrough exercises this path constantly).
// **Fixed** the same way as every other AVR-implicit-byte-wraparound bug
// already documented in this file: an explicit `& 0xFF` mask applied
// right after the decrement (`gunNextRow = ( gunNextRow - 1 ) & 0xFF;`),
// reproducing the real byte wraparound so the existing bounds-reset check
// works as intended. Verified via the same live Puppeteer session: an
// extended ~150-round soak test (repeated movement + firing, cycling
// through multiple full enemy-row rotations) rendered cleanly throughout
// with no crash or corruption, and correctly reached and cleared stage 1
// (STAGE display advancing 1 -> 2) partway through.
//
// **3) A minor, real audio-continuity gap**: `gunStopBgm()` only stopped
// the sequencer state for channels 1/2 (the BGM tracks), not channel 0
// (the Fire/Up SFX channel) - upstream's own `StopBGM()` resets ALL 3
// `ToneChannels`. Since `gunAdvanceSound()` unconditionally advances every
// channel's sequencer every tick regardless of game state, a fire/up cue
// still mid-flight the instant the game ended could re-trigger
// `md_playTone()` a few frames later, bleeding into the GAME OVER jingle.
// **Fixed** by adding `gunStopSeq(0)` alongside the existing
// `gunStopSeq(1)`/`gunStopSeq(2)` calls.
//
// **Extensively checked and confirmed already correct** (not just
// skipped): every data table was re-extracted from the real upstream
// source via a Python script and byte-diffed against the port's own
// arrays, not eyeballed - `gunCharPattern` (286 values) and
// `gunAsciiPattern` (108 values) both matched exactly; the full 75-entry
// `Stages.cpp` formation table (`elementCount`/`courseIndex`/`targets[8]`/
// `types[2]` for every one of the 8 stages' own `Formations0-7[]`)
// matched exactly via an automated diff, including the documented zero-
// vs-255 padding distinction; `Direction.cpp`'s `Directions[]`/
// `CourseElements[]`/`Courses[]` tables (spawn x/y/direction/sallyCount/
// elements for all 8 courses) all matched exactly, including course 6/7's
// own row-0/row-17 "hidden staging area" spawn points. `VVramToVram()`'s
// real byte-composition formula (the `sub==0/1/2/3` 4-way nibble split in
// `gunComposeRawByte()`) was independently re-derived from `Vram.cpp`'s
// own `SendUL()`/`Put2C()` calls and confirmed to match exactly, as did
// the `row0 = rawPage*2+2` "+2 hidden row" offset (re-derived directly
// from `VVramToVram(VVram + VVramWidth*2)`'s own pointer arithmetic, not
// just trusted from the original port's own claim). `Bang.cpp`'s own
// genuine double-offset quirk (`Show()`'s internal `-Size` stacking with
// an already-offset call-site position for the 4 "large" bang corners,
// but not for the single "small" bang case) was traced pixel-by-pixel and
// confirmed the port's `gunBangShowOne()` reproduces the exact same final
// positions in every case. `MoveTeams()`'s goto-based `nextCourseElement`
// control flow, `MoveMovingEnemies()`'s cross-case Align/Turn/Return goto
// chain, `EnemyBullet`'s Bresenham-style fixed-point velocity accumulator
// (confirmed to stay safely within its own bounded envelope regardless of
// numerator, no wraparound risk), `Fighter.cpp`'s crash/revive/blink
// state machine, `FighterBullet`/`Item`'s start/move/hit logic, and the
// title-screen selection/fire-edge-detection were all traced line-by-line
// against upstream and confirmed to match, including several places where
// upstream's own degenerate `CoordShift=0` constants (`CoordMask`,
// `FighterBullet`'s `HalfMask`) collapse conditions to unconditionally-
// true/false and the port correctly preserves that same degeneracy rather
// than "helpfully" adding logic upstream doesn't actually have. The
// `DecideDirection()`/`Type_Insistent` deliberate-deviation reasoning
// (reusing the enemy's own persisted dx/dy instead of upstream's
// genuinely-uninitialized stack read) was re-examined and reconfirmed
// reasonable - it doesn't materially change formation/attack behavior.
// The whole-tick pacing (`GUN_TICK_DIVISOR=3`) was independently
// re-derived from `Timer.cpp`'s own real `kTimerHz=60` SysTick and
// `WaitTimer(3)` call in `Main()`'s loop and confirmed to be an exact
// match (20fps), not an approximation.
//
// **CORRECTED, later in the session: the "CONTINU" truncation above was
// never a real hard limit - it was a self-inflicted design bug, the same
// one found and fixed across nearly every sibling Cate-engine port in
// this batch.** A real user-supplied photo of Cracky (this game's own
// sibling and structural reference) running on actual UIAPduino hardware
// showed "CONTINUE" spelled out in full, "MINI" genuinely present, and
// the whole title layout using real estate far wider than 8 columns.
// Re-reading upstream's real `Status.cpp` directly (both this game's own
// and Cracky's, which are structurally identical - see the two excerpts
// below) confirmed why: `PrintC()`/`PrintS()` write to a real 16-bit Vram
// address spanning the FULL physical screen width - `VramStep=4` real
// pixels/char-cell, 128 real pixels / 4 = a genuine 32-char-cell-wide
// row, not 8. The status labels (SCORE/STAGE/lives) really are confined
// to a narrow slice - `LeftX=24` places them at columns 24-31, the
// rightmost 8 cells - but the *title screen's* own text (MINI at column
// 17, START/CONTINUE at column 9, the "INUFUTO 2026" credit at column
// 12) lives at columns 0-23, well inside what during real gameplay is
// the map/VVram area, using the exact same shared `PrintC()`/`PrintS()`
// call at different column arguments - there is no separate "status-only
// text zone" concept upstream at all, just one shared wide canvas with
// the status labels living in one corner of it.
//
// This port's own `gunStatusChar` was modeled as an 8-column-wide grid
// (`[8][8]`) - correct for the status labels alone (LeftX=24..31 really
// is only 8 cells) - but then the *title screen's* text was also routed
// through that same narrow grid, reusing columns 24-31 that the status
// labels also need. That's the literal, single root cause of "CONTINUE"
// (8 real chars) overflowing an 8-cell grid by one column, "MINI"/"START"
// having nowhere to go without landing on SCORE/STAGE's own columns, and
// the credit line getting shortened/relocated to a wrong page/column
// entirely (see the old, now-corrected `gunBeginTitle()` this replaced -
// it had "INUFUTO" truncated to 7 letters at page 4 col 0, instead of the
// real "INUFUTO 2026" at page 7 col 12).
//
// **Fixed** by widening `gunStatusChar` to `int[8][32]`, updating every
// `gunPrintStatus()`/`gunPrintScore()`/`gunPrintRemain()` column argument
// to upstream's own real `LeftX`-based columns, and adding a
// `gunFullWidthText` flag (true only in `GUN_STATE_TITLE`) that lets
// `gunComposeRawByte()` read the full 32-column range instead of just
// columns 24-31 while the title screen is showing - `gunBeginTitle()` was
// rewritten to place "MINI"/"START"/"CONTINUE" (now the complete 8-letter
// word)/the "INUFUTO 2026" credit line at upstream's own literal columns,
// all genuinely clear of the status labels' own columns 24-31, with
// nothing left over needing to be dropped or truncated. This is the exact
// same fix already applied to gameCracky.c (the reference this port was
// built from in the first place) - see that file's own header comment
// for the full original derivation and the live-hardware-photo evidence
// that prompted it.
//
// **A small, deliberately un-replicated timing nuance, documented rather
// than chased**: upstream's real `Main()` loop reaches a stage-clear via
// `goto stage;`, which - because it jumps directly into the following
// tick's own logic body without an intervening `WaitTimer(3)` pace gate -
// runs one extra, un-paced movement+redraw pass in the same real instant
// as the transition, and skips incrementing `Clock` for that specific
// loop iteration. This port's `gunUpdatePlaying()` (a `return`-based
// state machine, not a `goto`) does still increment `gunClock` on a
// stage-clear tick and does not replicate that extra un-paced pass -
// shifting the movement-gating parity by one tick and delaying the new
// stage's first visible movement by one real paced tick (~50ms) after a
// transition. Confirmed via live testing that stage-clear itself works
// correctly (STAGE display advancing, enemies of the new stage spawning
// and behaving normally) - this is a sub-frame-scale cosmetic pacing
// difference with no effect on scoring/lives/win-loss logic, not chased
// further given the real risk of a larger control-flow restructure for
// an imperceptible gain.
//
// -----------------------------------------------------------------------------
//   SDL3 port note (dialect conversion only): every `TYPE[N] name` array
//   declaration above and below was mechanically rewritten to standard C's
//   `TYPE name[N]` form (and the 2D equivalent), and the five bare-struct-
//   then-array-of-it declarations (GunSprite/GunFighterBullet/
//   GunEnemyBullet/GunBang/GunMovingEnemy) became real `typedef struct`s -
//   no logic, data, or control flow changed. `gunPrintS()`/`gunBeginOverlay()`
//   deliberately keep their upstream `int*`/`int[]` parameter/array types for
//   text (not widened to `char*`/`char[]`, unlike some earlier AVR-lineage
//   ports in this project): every "string" here is built from individual
//   char-literal array elements (`{ 'S', 'C', 'O', 'R', 'E' }`), never a
//   double-quoted string literal assigned into an int pointer - confirmed by
//   grep, matching the same finding already made for sibling game Cracky -
//   so there is no real garbage-bytes-as-ints bug to fix by widening the
//   type here, and doing so anyway would just be unnecessary churn. Redraws
//   unconditionally in every real state (GUN_STATE_TITLE/START_JINGLE/
//   PLAYING/GAMEOVER_JINGLE all end their own update function with a
//   `gunRender()` call), so no `gameGuntus_forceRedraw()` is needed, matching
//   sibling game Cracky's own same finding.
// =============================================================================

// -----------------------------------------------------------------------------
//   Coord.h - degenerate case (CoordShift=0), kept as real expressions
//   rather than resolved to their current literal values, matching
//   gameCracky.c's own established philosophy for this exact situation.
// -----------------------------------------------------------------------------

#define GUN_COORD_SHIFT 0
#define GUN_COORD_RATE ( 1 << GUN_COORD_SHIFT )
#define GUN_COORD_MASK ( GUN_COORD_RATE - 1 )
#define GUN_HALF_MASK ( GUN_COORD_RATE * 2 - 1 )

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define GUN_CHAR_SPACE 0x00
#define GUN_CHAR_STAR 0x10
#define GUN_CHAR_FIGHTER_BULLET 0x11
#define GUN_CHAR_ENEMY_BULLET 0x12
#define GUN_CHAR_ENEMY 0x13
#define GUN_CHAR_FIGHTER 0x73
#define GUN_CHAR_BANG 0x77
#define GUN_CHAR_ITEM 0x8B

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define GUN_MAX_ENEMY_COUNT 6
#define GUN_MAX_FIGHTER_BULLET_COUNT 2
#define GUN_MAX_ENEMY_BULLET_COUNT 3
#define GUN_MAX_BANG_COUNT 4

#define GUN_SPRITE_FIGHTER 0
#define GUN_SPRITE_ENEMY 1
#define GUN_SPRITE_FIGHTER_BULLET 7
#define GUN_SPRITE_ENEMY_BULLET 9
#define GUN_SPRITE_ITEM 13
#define GUN_SPRITE_BANG 14
#define GUN_SPRITE_COUNT 18
#define GUN_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Direction.h / MovingEnemy.h / Team.h / Stage.h constants
// -----------------------------------------------------------------------------

#define GUN_DIR_UP 0
#define GUN_DIR_UPRIGHT 1
#define GUN_DIR_RIGHT 2
#define GUN_DIR_DOWNRIGHT 3
#define GUN_DIR_DOWN 4
#define GUN_DIR_DOWNLEFT 5
#define GUN_DIR_LEFT 6
#define GUN_DIR_UPLEFT 7

#define GUN_DIRECTION_COUNT 4
#define GUN_INVALID_ELEMENT 255
#define GUN_MAX_MEMBER_COUNT 6

#define GUN_ME_NONE 0
#define GUN_ME_STANDBY 1
#define GUN_ME_SALLY 2
#define GUN_ME_ALIGN 3
#define GUN_ME_TURN 4
#define GUN_ME_ATTACK 5
#define GUN_ME_RETURN 6

#define GUN_TYPE_CRASH 0
#define GUN_TYPE_SMART 1
#define GUN_TYPE_INSISTENT 2

#define GUN_STAGE_COUNT 8

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define GUN_VVRAM_WIDTH 24
#define GUN_VVRAM_HEIGHT 18

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale kept as real expressions.
// -----------------------------------------------------------------------------

#define GUN_TEMPO 180

#define GUN_MELODY_NONE 0
#define GUN_MELODY_FIRE 1
#define GUN_MELODY_UP 2
#define GUN_MELODY_START 3
#define GUN_MELODY_GAMEOVER 4
#define GUN_MELODY_BGM1 5
#define GUN_MELODY_BGM2 6

// -----------------------------------------------------------------------------
//   Data tables - script-extracted from the real upstream source, not hand-
//   copied (see header comment).
// -----------------------------------------------------------------------------

// AsciiPattern - byte-diff-confirmed identical to gameCracky.c's own
// crkAsciiPattern (same font, same " 0123456789>ACEFGIMNOPRSTUV" set).
int gunAsciiPattern[108] = {
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

// CharPattern - 143 map-tile glyphs, 2 bytes/glyph. Script-extracted and
// count-verified (286 bytes) against Chars.cpp before use.
int gunCharPattern[286] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x20, 0x00, 0x00, 0xf0, 0xf6, 0x6f, 0x57, 0xcb,
    0x5b, 0x07, 0x32, 0x73, 0x33, 0x02, 0xc4, 0xdf,
    0x4f, 0x0c, 0x20, 0x31, 0x17, 0x01, 0xf8, 0xce,
    0x5b, 0x07, 0x70, 0x13, 0x56, 0x07, 0xa0, 0xec,
    0x4f, 0x0c, 0x11, 0x57, 0x17, 0x01, 0x62, 0xfe,
    0x6e, 0x02, 0x57, 0x16, 0x56, 0x07, 0x4c, 0xef,
    0xac, 0x00, 0x11, 0x57, 0x17, 0x01, 0x57, 0xcb,
    0xfe, 0x08, 0x57, 0x16, 0x73, 0x00, 0x4c, 0xdf,
    0xcf, 0x04, 0x11, 0x37, 0x21, 0x00, 0xec, 0xfb,
    0xeb, 0x0c, 0x32, 0x73, 0x33, 0x02, 0xc4, 0xdf,
    0x7f, 0x0e, 0x20, 0x31, 0x17, 0x01, 0xf8, 0xfe,
    0xeb, 0x0c, 0x70, 0x73, 0x36, 0x01, 0xa0, 0xec,
    0x4f, 0x0c, 0x11, 0x57, 0x77, 0x03, 0xea, 0xfe,
    0xee, 0x0a, 0x31, 0x76, 0x36, 0x01, 0x4c, 0xef,
    0xac, 0x00, 0x73, 0x57, 0x17, 0x01, 0xec, 0xfb,
    0xfe, 0x08, 0x31, 0x76, 0x73, 0x00, 0x7e, 0xdf,
    0xcf, 0x04, 0x11, 0x37, 0x21, 0x00, 0xe8, 0xeb,
    0xeb, 0x08, 0x10, 0x73, 0x13, 0x00, 0xe0, 0xde,
    0x4f, 0x0c, 0x76, 0x33, 0x33, 0x00, 0xc8, 0xfe,
    0xea, 0x04, 0x10, 0x73, 0x32, 0x01, 0xf3, 0xee,
    0x6e, 0x08, 0x30, 0x53, 0x17, 0x01, 0xc8, 0xfe,
    0xce, 0x08, 0x30, 0x36, 0x36, 0x00, 0x68, 0xee,
    0xfe, 0x03, 0x11, 0x57, 0x33, 0x00, 0xe4, 0xfa,
    0xce, 0x08, 0x31, 0x72, 0x13, 0x00, 0x4c, 0xdf,
    0xee, 0x00, 0x30, 0x33, 0x73, 0x06, 0xfc, 0xa4,
    0xf4, 0x0c, 0x73, 0x33, 0x73, 0x03, 0xe4, 0xb6,
    0x4a, 0x4e, 0x72, 0xe2, 0x25, 0x17, 0x00, 0xc2,
    0x84, 0x7c, 0x62, 0xff, 0xe9, 0x6f, 0xce, 0xec,
    0x07, 0x88, 0xdd, 0x36, 0x3f, 0x01, 0x64, 0xdb,
    0x7b, 0xe2, 0x00, 0x36, 0x11, 0xe2, 0x97, 0xab,
    0x58, 0x46, 0x12, 0x31, 0x23, 0x00, 0x1e, 0x1f,
    0xb3, 0xe3, 0x63, 0x66, 0x64, 0x36,
};

// TitleBytes - upstream's own real "GUNTUS" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 4x4-VVram-cell chunks (80 values total,
// TitleLength=5) - byte-diff-verified against the real upstream source.
// Every value here is a valid index into gunCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table), the same shared
// block-pattern palette every other map tile in this game already draws
// through - see gunBeginTitle()'s own comment for why this replaces the
// earlier plain-text "GUNTUS" substitute (the 5 stored chunks pack 6 real
// kerned letters, G-U-N-T-U-S, not literally 5 - confirmed by rendering
// the raw bitmap offline before trusting the data).
int gunTitleBytes[80] = {
    0x0e, 0x05, 0x0b, 0x0c,
    0x0f, 0x08, 0x0a, 0x0c,
    0x0f, 0x00, 0x0f, 0x0c,
    0x04, 0x05, 0x01, 0x00,
    0x03, 0x0f, 0x0c, 0x07,
    0x03, 0x0f, 0x0c, 0x03,
    0x03, 0x0f, 0x0c, 0x03,
    0x05, 0x01, 0x04, 0x01,
    0x0b, 0x04, 0x0f, 0x01,
    0x0f, 0x00, 0x0f, 0x00,
    0x0f, 0x00, 0x0f, 0x00,
    0x05, 0x00, 0x05, 0x00,
    0x0f, 0x0c, 0x03, 0x0e,
    0x0f, 0x0c, 0x03, 0x0d,
    0x0f, 0x0c, 0x03, 0x0a,
    0x04, 0x05, 0x00, 0x04,
    0x0d, 0x02, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00,
    0x0c, 0x03, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 - byte-diff-confirmed
// identical to gameCracky.c's own crkFrequencies.
int gunFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

// Sound melodies - script-extracted (NoteLength/Scale enum values
// resolved), count-verified against Sound.cpp before use.
int gunMelodyFire[13] = {
    1, 38, 1, 36, 1, 34, 1, 32, 1, 30, 1, 40,
    0,
};
int gunMelodyUp[13] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30, 1, 33,
    0,
};
int gunMelodyStart[23] = {
    6, 30, 12, 32, 12, 33, 12, 33, 6, 30, 12, 35,
    12, 35, 6, 33, 18, 35, 36, 37, 12, 0, 0,
};
// upstream's own real notes[] array for this melody (Sound_GameOver(), 18
// bytes = 9 length/note pairs) has NO trailing terminator byte, unlike
// Sound_Fire()/Sound_Up()/Sound_Start() which all end with an explicit
// extra 0 - a genuine upstream bug (confirmed by literally counting the
// tokens in the real source), not a porting slip. On real AVR flash this
// is harmless-by-luck (whatever byte happens to sit right after the array
// in ROM either terminates the melody anyway or is simply never audible),
// but on Vircon32 reading past index 17 lands directly in the very next
// declared global, gunMelodyBgm1[0] (=18, non-zero) - so the "still
// playing" check never goes false and gunUpdateGameOverJingle() never
// transitions back to the title screen, misinterpreting BGM1's own data
// as more GameOver notes indefinitely. Confirmed via live Puppeteer
// testing: the GAME OVER screen stayed up indefinitely (many seconds,
// far past the real ~2.9s the melody should take) with no return to
// title. Fixed with an explicit trailing 0 terminator, matching every
// other melody table in this file and reproducing what was clearly the
// intended (if never-quite-written) upstream behavior rather than the
// real out-of-bounds read.
int gunMelodyGameOver[19] = {
    12, 30, 6, 25, 6, 30, 6, 28, 6, 26, 6, 25,
    6, 23, 36, 25, 12, 0, 0,
};
int gunMelodyBgm1[105] = {
    18, 30, 18, 32, 24, 33, 12, 33, 12, 32, 12, 33,
    18, 32, 18, 28, 36, 28, 24, 0, 18, 30, 18, 32,
    24, 33, 12, 33, 12, 32, 12, 33, 18, 40, 18, 35,
    36, 35, 24, 0, 18, 38, 18, 37, 24, 38, 12, 38,
    12, 37, 12, 38, 18, 37, 18, 33, 36, 33, 24, 0,
    6, 30, 6, 30, 6, 32, 12, 33, 12, 33, 6, 33,
    6, 32, 6, 32, 6, 33, 12, 35, 12, 35, 6, 35,
    6, 33, 6, 33, 6, 35, 12, 37, 12, 37, 6, 37,
    6, 38, 12, 38, 18, 37, 12, 0, 255,
};
int gunMelodyBgm2[227] = {
    12, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0,
    6, 10, 12, 11, 6, 0, 6, 11, 6, 0, 6, 11,
    6, 0, 6, 11, 12, 13, 6, 0, 6, 13, 6, 0,
    6, 13, 6, 0, 6, 17, 12, 6, 6, 0, 6, 6,
    6, 0, 6, 6, 6, 0, 6, 6, 12, 6, 6, 0,
    6, 6, 6, 0, 6, 6, 6, 0, 6, 13, 12, 14,
    6, 0, 6, 14, 6, 0, 6, 14, 6, 0, 6, 15,
    12, 16, 6, 0, 6, 16, 6, 0, 6, 16, 6, 0,
    6, 16, 12, 13, 6, 0, 6, 13, 6, 0, 6, 13,
    6, 0, 6, 13, 12, 11, 6, 0, 6, 11, 6, 0,
    6, 11, 6, 0, 6, 7, 12, 16, 6, 0, 6, 16,
    6, 0, 6, 16, 6, 0, 6, 16, 12, 9, 6, 0,
    6, 9, 6, 0, 6, 9, 6, 0, 6, 9, 12, 9,
    6, 0, 6, 9, 6, 0, 6, 9, 6, 0, 6, 9,
    6, 6, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6,
    6, 0, 6, 10, 6, 11, 6, 11, 6, 0, 6, 11,
    6, 0, 6, 11, 6, 0, 6, 17, 6, 6, 6, 6,
    6, 0, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6,
    6, 14, 6, 14, 6, 0, 18, 13, 12, 0, 255,
};

// Star.cpp - fixed decorative star-field positions.
#define GUN_STAR_COUNT 16
#define GUN_STAR_RANGE_Y 32
int gunStarX[16] = { 10, 3, 10, 20, 4, 3, 17, 10, 8, 17, 22, 18, 16, 18, 16, 4 };
int gunStarY[16] = { 20, 8, 15, 7, 26, 13, 18, 24, 7, 5, 26, 24, 27, 2, 21, 19 };

// Direction.cpp
int gunDirections[8][2] = {
    { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
    { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 },
};
int gunCourseElementDirs[16][4] = {
    { 2, 3, 4, 5 }, { 6, 7, 0, 1 }, { 6, 5, 4, 3 }, { 2, 1, 0, 7 },
    { 2, 3, 3, 4 }, { 4, 5, 5, 6 }, { 6, 7, 7, 0 }, { 0, 1, 1, 2 },
    { 6, 5, 5, 4 }, { 4, 3, 3, 2 }, { 2, 1, 1, 0 }, { 0, 7, 7, 6 },
    { 2, 2, 2, 2 }, { 6, 6, 6, 6 }, { 4, 4, 4, 4 }, { 0, 0, 0, 0 },
};
// gunCourseElements - script-extracted; upstream's own C++ aggregate-init
// zero-pads several rows shorter than 10 explicit values (NOT a second
// InvalidElement sentinel) - reproduced exactly, see header comment.
int gunCourseX[8] = { 0, 22, 0, 22, 0, 22, 12, 13 };
int gunCourseY[8] = { 2, 2, 6, 6, 10, 10, 0, 17 };
int gunCourseDirection[8] = { 2, 6, 2, 6, 2, 6, 4, 0 };
int gunCourseSallyCount[8] = { 4, 4, 4, 4, 4, 4, 2, 4 };
int gunCourseElements[8][10] = {
    { 12, 4, 5, 1, 3, 8, 9, 10, 7, 255 },
    { 13, 8, 9, 3, 1, 4, 5, 6, 11, 255 },
    { 12, 3, 8, 9, 12, 10, 15, 255, 0, 0 },
    { 13, 1, 4, 5, 13, 6, 15, 255, 0, 0 },
    { 12, 3, 2, 12, 10, 15, 255, 0, 0, 0 },
    { 13, 1, 0, 13, 6, 15, 255, 0, 0, 0 },
    { 14, 14, 255, 0, 0, 0, 0, 0, 0, 0 },
    { 15, 15, 255, 0, 0, 0, 0, 0, 0, 0 },
};

// Stages.cpp - flattened; script-extracted from Formations0-7[]/Stages[].
int gunFormationElementCount[75] = {
    4, 4, 4, 4, 4, 2, 2, 4, 2, 1, 1, 2, 2, 1, 4, 5,
    4, 2, 2, 2, 1, 4, 5, 3, 2, 3, 2, 2, 3, 4, 4, 2,
    2, 2, 2, 2, 3, 4, 2, 3, 2, 2, 3, 4, 2, 4, 2, 2,
    2, 2, 2, 2, 2, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 2, 5, 5, 5, 5, 5, 5, 5, 5,
};
int gunFormationCourseIndex[75] = {
    0, 1, 2, 3, 3, 2, 1, 0, 3, 2, 1, 0, 3, 1, 2, 4,
    5, 3, 3, 1, 1, 2, 4, 5, 0, 2, 5, 3, 2, 6, 4, 3,
    1, 5, 4, 0, 2, 7, 3, 2, 3, 1, 2, 6, 0, 4, 5, 1,
    5, 7, 6, 2, 0, 3, 2, 1, 0, 1, 5, 7, 6, 2, 0, 3,
    2, 1, 0, 3, 7, 5, 1, 2, 4, 0, 2,
};
int gunFormationTargets[75][8] = {
    { 0x04, 0x20, 0x24, 0x25, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x12, 0x14, 0x21, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x10, 0x11, 0x15, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x13, 0x22, 0x23, 0x00, 0x00, 0x00, 0x00 },
    { 0x14, 0x15, 0x24, 0x25, 0x00, 0x00, 0x00, 0x00 },
    { 0x13, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x10, 0x11, 0x20, 0x21, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x11, 0x14, 0x15, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x10, 0x23, 0x24, 0x25, 0x00, 0x00, 0x00 },
    { 0x01, 0x20, 0x21, 0x22, 0x00, 0x00, 0x00, 0x00 },
    { 0x13, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x07, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x06, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x12, 0x15, 0x16, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x11, 0x24, 0x25, 0x26, 0x00, 0x00, 0x00 },
    { 0x02, 0x22, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x32, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x23, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x16, 0x22, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x13, 0x14, 0x15, 0x00, 0x00, 0x00, 0x00 },
    { 0x11, 0x26, 0x34, 0x35, 0x00, 0x00, 0x00, 0x00 },
    { 0x07, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x06, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x32, 0x33, 0x34, 0x35, 0x00, 0x00, 0x00, 0x00 },
    { 0x25, 0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x22, 0x23, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x21, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x17, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x16, 0x20, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x13, 0x14, 0x15, 0x00, 0x00, 0x00, 0x00 },
    { 0x10, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x05, 0x11, 0x24, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x35, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x34, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x33, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x32, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x31, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x30, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x24, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x20, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x07, 0x17, 0x27, 0x37, 0x47, 0x00, 0x00, 0x00 },
    { 0x06, 0x16, 0x26, 0x36, 0x46, 0x00, 0x00, 0x00 },
    { 0x05, 0x15, 0x25, 0x35, 0x45, 0x00, 0x00, 0x00 },
    { 0x04, 0x14, 0x24, 0x34, 0x44, 0x00, 0x00, 0x00 },
    { 0x03, 0x13, 0x23, 0x33, 0x43, 0x00, 0x00, 0x00 },
    { 0x02, 0x12, 0x22, 0x32, 0x42, 0x00, 0x00, 0x00 },
    { 0x01, 0x11, 0x21, 0x31, 0x41, 0x00, 0x00, 0x00 },
    { 0x00, 0x10, 0x20, 0x30, 0x40, 0x00, 0x00, 0x00 },
};
int gunFormationTypes[75][2] = {
    { 0x02, 0x00 }, { 0x16, 0x00 }, { 0x56, 0x00 }, { 0x06, 0x00 },
    { 0x05, 0x00 }, { 0x01, 0x00 }, { 0x01, 0x00 }, { 0x05, 0x00 },
    { 0x0a, 0x00 }, { 0x02, 0x00 }, { 0x02, 0x00 }, { 0x0a, 0x00 },
    { 0x05, 0x00 }, { 0x02, 0x00 }, { 0x56, 0x00 }, { 0x06, 0x00 },
    { 0x02, 0x00 }, { 0x05, 0x00 }, { 0x02, 0x00 }, { 0x06, 0x00 },
    { 0x02, 0x00 }, { 0x56, 0x00 }, { 0x06, 0x00 }, { 0x02, 0x00 },
    { 0x06, 0x00 }, { 0x02, 0x00 }, { 0x00, 0x00 }, { 0x05, 0x00 },
    { 0x15, 0x00 }, { 0x55, 0x00 }, { 0x05, 0x00 }, { 0x06, 0x00 },
    { 0x06, 0x00 }, { 0x0a, 0x00 }, { 0x0a, 0x00 }, { 0x06, 0x00 },
    { 0x16, 0x00 }, { 0x00, 0x00 }, { 0x01, 0x00 }, { 0x05, 0x00 },
    { 0x05, 0x00 }, { 0x02, 0x00 }, { 0x16, 0x00 }, { 0xaa, 0x00 },
    { 0x02, 0x00 }, { 0x6a, 0x00 }, { 0x0a, 0x00 }, { 0x00, 0x00 },
    { 0x00, 0x00 }, { 0x00, 0x00 }, { 0x00, 0x00 }, { 0x00, 0x00 },
    { 0x00, 0x00 }, { 0x05, 0x00 }, { 0x01, 0x00 }, { 0x01, 0x00 },
    { 0x05, 0x00 }, { 0x02, 0x00 }, { 0x02, 0x00 }, { 0x02, 0x00 },
    { 0x02, 0x00 }, { 0x02, 0x00 }, { 0x02, 0x00 }, { 0x0a, 0x00 },
    { 0x02, 0x00 }, { 0x02, 0x00 }, { 0x0a, 0x00 }, { 0x1a, 0x00 },
    { 0x1a, 0x00 }, { 0x1a, 0x00 }, { 0x1a, 0x00 }, { 0x1a, 0x00 },
    { 0x1a, 0x00 }, { 0x1a, 0x00 }, { 0x1a, 0x00 },
};
int gunStageMin[8] = { 1, 1, 1, 0, 0, 0, 1, 0 };
int gunStageMax[8] = { 6, 6, 6, 7, 7, 7, 6, 7 };
int gunStageRowCount[8] = { 3, 3, 3, 3, 4, 4, 5, 5 };
int gunStageFormationCount[8] = { 4, 8, 5, 9, 11, 10, 20, 8 };
int gunStageFormationStart[8] = { 0, 4, 12, 17, 26, 37, 47, 67 };

// EnemyRow.cpp - fixed attack-turn column pick order.
#define GUN_ENEMYROW_MAX_COUNT 5
#define GUN_ENEMYROW_COLUMN_COUNT 12
#define GUN_TOP 2
int gunEnemyRowColumns[12] = { 4, 1, 0, 11, 5, 10, 9, 2, 8, 6, 3, 7 };
int gunEnemyRowPoints[3] = { 1, 2, 5 };

// MovingEnemy.cpp - kill-score-by-type.
int gunMovingEnemyPoints[3] = { 2, 4, 10 };

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int gunScore;
int gunRemainCount;
int gunCurrentStage;
int gunStageIndex;
int gunEnemyCount;
int gunRndIndex;

int gunVVram[GUN_VVRAM_HEIGHT][GUN_VVRAM_WIDTH];

typedef struct
{
    int x, y, code;
} GunSprite;
GunSprite gunSprites[GUN_SPRITE_COUNT];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells
// per row) - widened from an original, wrong `[8][8]` once a real
// hardware photo of sibling game Cracky proved the narrow model was
// flatly incorrect; see this file's own header comment for the full
// derivation. Same shape/fix as gameCracky.c's own crkStatusChar.
int gunStatusChar[8][32];

// Set true only while on the title screen (GUN_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system for its own text at
// all (only its decorative bitmap logo does), and instead drives the
// ENTIRE screen through the same PrintC()/PrintS() text mechanism, at
// real columns spanning the whole 0-31 char-cell range (MINI/START/
// CONTINUE/the credit line all live at columns 0-23, well inside what
// during gameplay is the map area). When true, gunComposeRawByte() reads
// gunStatusChar across the full width instead of just columns 24-31.
bool gunFullWidthText;

// message overlay burned directly over the map area (GAME OVER).
bool gunOverlayActive;
int gunOverlayText[10];
int gunOverlayLen;
int gunOverlayPage;
int gunOverlayCol;

// sound sequencer - 3 independent voices, advance every real engine frame.
int gunSeqMelody[3];
int gunSeqPos[3];
int gunSeqWait[3];
int gunSeqActive[3];

// Star.cpp
int gunStarYOffset;

// Fighter.cpp
#define GUN_FIGHTER_INITIAL_X ( ( GUN_VVRAM_WIDTH / 2 - 1 ) * GUN_COORD_RATE )
#define GUN_FIGHTER_INITIAL_Y ( ( GUN_VVRAM_HEIGHT - 1 - 1 ) * GUN_COORD_RATE )
#define GUN_FIGHTER_CRASH_RANGE ( 16 * GUN_COORD_RATE )
#define GUN_FIGHTER_REVIVE_TIME ( 31 * GUN_COORD_RATE )
int gunFighterX, gunFighterY;
int gunCrashCount;
int gunReviveCount;

// FighterBullet.cpp
#define GUN_FB_COUNT ( GUN_SPRITE_ENEMY_BULLET - GUN_SPRITE_FIGHTER_BULLET )
#define GUN_FB_RANGE ( GUN_VVRAM_HEIGHT * GUN_COORD_RATE )
#define GUN_FB_SHORT_INTERVAL GUN_COORD_RATE
#define GUN_FB_LONG_INTERVAL ( GUN_FB_SHORT_INTERVAL * 4 )
#define GUN_FB_HALF_MASK ( GUN_COORD_MASK >> 1 )
typedef struct
{
    int x, y, sprite, clock;
} GunFighterBullet;
GunFighterBullet gunFighterBullets[GUN_FB_COUNT];
int gunFbIntervalCount;

// EnemyBullet.cpp
#define GUN_EB_RANGE_X ( GUN_VVRAM_WIDTH * GUN_COORD_RATE - 1 )
#define GUN_EB_RANGE_Y ( GUN_VVRAM_HEIGHT * GUN_COORD_RATE )
#define GUN_EB_HI_VELOCITY 100
#define GUN_EB_LO_VELOCITY ( GUN_EB_HI_VELOCITY * 70 / 100 )
#define GUN_EB_LONG_VELOCITY ( GUN_EB_HI_VELOCITY * 92 / 100 )
#define GUN_EB_SHORT_VELOCITY ( GUN_EB_HI_VELOCITY * 38 / 100 )
typedef struct
{
    int x, y, sprite, dx, dy, clock, numeratorX, denominatorX, numeratorY, denominatorY;
} GunEnemyBullet;
GunEnemyBullet gunEnemyBullets[GUN_MAX_ENEMY_BULLET_COUNT];

// Item.cpp
#define GUN_ITEM_RANGE ( GUN_VVRAM_HEIGHT * GUN_COORD_RATE )
int gunItemX, gunItemY;

// Bang.cpp
#define GUN_BANG_SIZE GUN_COORD_RATE
#define GUN_BANG_STATUS_NONE 0x00
#define GUN_BANG_STATUS_SMALL 0x10
#define GUN_BANG_STATUS_LARGE_SMALL 0x20
#define GUN_BANG_STATUS_LARGE_LARGE 0x30
#define GUN_BANG_RANGE_X ( GUN_VVRAM_WIDTH * GUN_COORD_RATE - 1 )
#define GUN_BANG_RANGE_Y ( GUN_VVRAM_HEIGHT * GUN_COORD_RATE )
typedef struct
{
    int x, y, status;
} GunBang;
GunBang gunBangs[GUN_MAX_BANG_COUNT];

// MovingEnemy.cpp
#define GUN_ME_RANGE_X ( ( GUN_VVRAM_WIDTH - 2 ) * GUN_COORD_RATE )
#define GUN_ME_RANGE_Y ( ( GUN_VVRAM_HEIGHT - 2 ) * GUN_COORD_RATE )
#define GUN_FIRE_MASK ( GUN_COORD_RATE * 2 - 1 )
#define GUN_TURN_MASK ( GUN_COORD_RATE * 2 - 1 )
#define GUN_LONG_MASK ( GUN_COORD_RATE * 4 - 1 )
typedef struct
{
    int status, x, y, sprite, type, dx, dy, direction, target, bulletCount;
} GunMovingEnemy;
GunMovingEnemy gunMovingEnemies[GUN_MAX_ENEMY_COUNT];
int gunFreeEnemyCount;
int gunMeClock;

// EnemyRow.cpp
#define GUN_ENEMYROW_MIN_INITIAL_X ( ( GUN_VVRAM_WIDTH - 8 * 2 ) / 2 )
#define GUN_ENEMYROW_RANGE_X ( GUN_VVRAM_WIDTH * GUN_COORD_RATE )
int gunEnemyRowMemberCount[GUN_ENEMYROW_MAX_COUNT];
int gunEnemyRowType[GUN_ENEMYROW_MAX_COUNT];
int gunEnemyRowFlags[GUN_ENEMYROW_MAX_COUNT][2];
int gunEnemyRowCount;
int gunEnemyRowLeft, gunEnemyRowWidth;
int gunEnemyRowDirection;
int gunNextRow, gunNextColumn;

// Team.cpp
int gunTeamMemberCount[2];
int gunTeamFormation[2];
int gunTeamCourse[2];
int gunTeamNextMember[2];
int gunTeamMembers[2][GUN_MAX_MEMBER_COUNT];
int gunTeamSallyCount[2];
int gunTeamDirectionIndex[2];
int gunTeamCourseElementIndex[2];
int gunTeamCourseElement[2];
int gunTeamDirections[2][GUN_MAX_MEMBER_COUNT * 2];
int gunCurFormation;
int gunFormationCount;
int gunTeamClock;

// state machine
#define GUN_STATE_TITLE 0
#define GUN_STATE_START_JINGLE 1
#define GUN_STATE_PLAYING 2
#define GUN_STATE_GAMEOVER_JINGLE 3
int gunState;
#define GUN_TICK_DIVISOR 3
int gunTickCounter;
int gunClock;
int gunClearTime;
int gunSelection;
bool gunSelectionChanged;
int gunPrevLeft, gunPrevRight, gunPrevUp, gunPrevDown, gunPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int gunRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

int gunRnd()
{
    int r;
    r = gunRndNumbers[ gunRndIndex ];
    gunRndIndex = gunRndIndex + 1;
    if( gunRndIndex >= 32 )
      gunRndIndex = 0;
    return r & 0x0f;
}

int gunAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int gunSign( int from, int to )
{
    if( from == to )
      return 0;
    if( from < to )
      return 1;
    return -1;
}


// -----------------------------------------------------------------------------
//   Direction.cpp
// -----------------------------------------------------------------------------

int gunToDirection( int dx, int dy )
{
    if( dx == 0 )
    {
        if( dy < 0 ) return GUN_DIR_UP;
        return GUN_DIR_DOWN;
    }
    if( dx < 0 )
    {
        if( dy == 0 ) return GUN_DIR_LEFT;
        if( dy < 0 ) return GUN_DIR_UPLEFT;
        return GUN_DIR_DOWNLEFT;
    }
    if( dy == 0 ) return GUN_DIR_RIGHT;
    if( dy < 0 ) return GUN_DIR_UPRIGHT;
    return GUN_DIR_DOWNRIGHT;
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status-text overlay grid, matching
//   gameCracky.c's own crkStatusChar mechanism exactly.
// -----------------------------------------------------------------------------

int gunAsciiIndex( int c )
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

int gunPrintC( int page, int col, int c )
{
    gunStatusChar[ page ][ col ] = gunAsciiIndex( c );
    return col + 1;
}

int gunPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = gunPrintC( page, col, s[ i ] );
    return col;
}

void gunPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      gunPrintC( page, col, ' ' );
    else
      gunPrintC( page, col, d1 + '0' );
    gunPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void gunPrintNumber5( int page, int col, int w )
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
          gunPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            gunPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    gunPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// gunStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void gunPrintScore()
{
    gunPrintNumber5( 1, 26, gunScore );
    gunPrintC( 1, 31, '0' );
}

void gunPrintRemain()
{
    // upstream does nothing at all once RemainCount<=1 (a real, minor
    // upstream quirk - whatever was last drawn here just lingers) -
    // preserved faithfully rather than "fixed" with an explicit blank.
    if( gunRemainCount > 1 )
    {
        int i;
        i = gunRemainCount - 1;
        // upstream draws a real 2x2 Char_Remain icon (Put2C) here, i
        // times - simplified to plain digit text throughout (matching
        // gameCracky.c's own PrintRemain simplification).
        gunPrintC( 7, 24, ' ' );
        gunPrintC( 7, 25, ' ' );
        gunPrintC( 7, 26, i + '0' );
    }
}

void gunPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    gunPrintS( 0, 24, sScore, 5 );
    gunPrintS( 3, 24, sStage, 5 );
    gunPrintByteNumber2( 3, 30, gunCurrentStage + 1 );
    gunPrintScore();
    gunPrintRemain();
}

void gunAddScore( int pts )
{
    gunScore = gunScore + pts;
    gunPrintScore();
}

void gunBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    gunOverlayActive = true;
    gunOverlayLen = len;
    gunOverlayPage = page;
    gunOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      gunOverlayText[ i ] = s[ i ];
}

void gunPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    gunBeginOverlay( s, 9, 4, 8 );
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void gunHideAllSprites()
{
    int i;
    for( i = 0; i < GUN_SPRITE_COUNT; i = i + 1 )
      gunSprites[ i ].code = GUN_INVALID_CODE;
}

void gunShowSprite( int index, int x, int y, int code )
{
    gunSprites[ index ].x = x;
    gunSprites[ index ].y = y;
    gunSprites[ index ].code = code;
}

void gunHideSprite( int index )
{
    gunSprites[ index ].code = GUN_INVALID_CODE;
}

void gunDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < GUN_SPRITE_COUNT; i = i + 1 )
    {
        if( gunSprites[ i ].code != GUN_INVALID_CODE )
        {
            x = gunSprites[ i ].x;
            y = gunSprites[ i ].y;
            c = gunSprites[ i ].code;
            gunVVram[ y ][ x ] = c;
            if( c >= GUN_CHAR_ENEMY )
            {
                c = c + 1;
                gunVVram[ y ][ x + 1 ] = c;
                if( y < GUN_VVRAM_HEIGHT - 1 )
                {
                    c = c + 1;
                    gunVVram[ y + 1 ][ x ] = c;
                    c = c + 1;
                    gunVVram[ y + 1 ][ x + 1 ] = c;
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   VVram.cpp
// -----------------------------------------------------------------------------

void gunVPut2C( int row, int col, int c )
{
    gunVVram[ row ][ col ] = c;
    gunVVram[ row ][ col + 1 ] = c + 1;
    gunVVram[ row + 1 ][ col ] = c + 2;
    gunVVram[ row + 1 ][ col + 1 ] = c + 3;
}


// -----------------------------------------------------------------------------
//   Star.cpp
// -----------------------------------------------------------------------------

void gunMoveStars()
{
    gunStarYOffset = gunStarYOffset + 1;
}

void gunDrawStars()
{
    int i, x, y;
    for( i = 0; i < GUN_STAR_COUNT; i = i + 1 )
    {
        x = gunStarX[ i ];
        y = ( gunStarY[ i ] + gunStarYOffset ) & ( GUN_STAR_RANGE_Y - 1 );
        if( y < GUN_VVRAM_HEIGHT )
          gunVVram[ y ][ x ] = GUN_CHAR_STAR;
    }
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void gunInitBangs()
{
    int i;
    for( i = 0; i < GUN_MAX_BANG_COUNT; i = i + 1 )
      gunBangs[ i ].status = GUN_BANG_STATUS_NONE;
}

void gunStartBang( int x, int y, bool large )
{
    int i;
    for( i = 0; i < GUN_MAX_BANG_COUNT; i = i + 1 )
    {
        if( ( gunBangs[ i ].status & 0xf0 ) != GUN_BANG_STATUS_NONE ) continue;
        gunBangs[ i ].x = x;
        gunBangs[ i ].y = y;
        if( large )
          gunBangs[ i ].status = GUN_BANG_STATUS_LARGE_SMALL;
        else
          gunBangs[ i ].status = GUN_BANG_STATUS_SMALL;
        return;
    }
}

int gunBangShowOne( int x, int y, int sprite, int pattern )
{
    int bx, by;
    if( sprite < GUN_SPRITE_COUNT )
    {
        bx = ( x - GUN_BANG_SIZE ) & 0xFF;
        by = ( y - GUN_BANG_SIZE ) & 0xFF;
        if( bx < GUN_BANG_RANGE_X && by < GUN_BANG_RANGE_Y )
        {
            gunShowSprite( sprite, bx, by, pattern );
            return sprite + 1;
        }
    }
    return sprite;
}

void gunUpdateBangs()
{
    int sprite, i;
    sprite = GUN_SPRITE_BANG;
    for( i = 0; i < GUN_MAX_BANG_COUNT; i = i + 1 )
    {
        int mode, count;
        mode = gunBangs[ i ].status & 0xf0;
        if( mode == GUN_BANG_STATUS_NONE ) continue;
        count = gunBangs[ i ].status & 0x0f;

        if( mode == GUN_BANG_STATUS_LARGE_LARGE )
        {
            sprite = gunBangShowOne( gunBangs[i].x - GUN_BANG_SIZE, gunBangs[i].y - GUN_BANG_SIZE, sprite, GUN_CHAR_BANG + 4 );
            sprite = gunBangShowOne( gunBangs[i].x + GUN_BANG_SIZE, gunBangs[i].y - GUN_BANG_SIZE, sprite, GUN_CHAR_BANG + 8 );
            sprite = gunBangShowOne( gunBangs[i].x - GUN_BANG_SIZE, gunBangs[i].y + GUN_BANG_SIZE, sprite, GUN_CHAR_BANG + 12 );
            sprite = gunBangShowOne( gunBangs[i].x + GUN_BANG_SIZE, gunBangs[i].y + GUN_BANG_SIZE, sprite, GUN_CHAR_BANG + 16 );
        }
        else
          sprite = gunBangShowOne( gunBangs[i].x, gunBangs[i].y, sprite, GUN_CHAR_BANG );

        count = count + 1;
        if( count >= 4 )
        {
            if( mode == GUN_BANG_STATUS_LARGE_SMALL )
              gunBangs[ i ].status = GUN_BANG_STATUS_LARGE_LARGE;
            else
              gunBangs[ i ].status = GUN_BANG_STATUS_NONE;
        }
        else
          gunBangs[ i ].status = mode | count;
    }
    while( sprite < GUN_SPRITE_COUNT )
    {
        gunHideSprite( sprite );
        sprite = sprite + 1;
    }
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int gunMelodyLength( int id )
{
    if( id == GUN_MELODY_FIRE ) return 13;
    if( id == GUN_MELODY_UP ) return 13;
    if( id == GUN_MELODY_START ) return 23;
    if( id == GUN_MELODY_GAMEOVER ) return 19;
    if( id == GUN_MELODY_BGM1 ) return 105;
    if( id == GUN_MELODY_BGM2 ) return 227;
    return 0;
}

int gunMelodyValue( int id, int idx )
{
    if( id == GUN_MELODY_FIRE ) return gunMelodyFire[ idx ];
    if( id == GUN_MELODY_UP ) return gunMelodyUp[ idx ];
    if( id == GUN_MELODY_START ) return gunMelodyStart[ idx ];
    if( id == GUN_MELODY_GAMEOVER ) return gunMelodyGameOver[ idx ];
    if( id == GUN_MELODY_BGM1 ) return gunMelodyBgm1[ idx ];
    if( id == GUN_MELODY_BGM2 ) return gunMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a decrement-event happens once every
// (600/2)/GUN_TEMPO = 1.667 real 60Hz ticks - see header comment.
int gunNoteFrames( int length )
{
    return (int)( length * 300.0 / 180.0 + 0.5 );
}

void gunStartSeq( int channel, int melodyId )
{
    gunSeqMelody[ channel ] = melodyId;
    gunSeqPos[ channel ] = 0;
    gunSeqWait[ channel ] = 0;
    gunSeqActive[ channel ] = 1;
}

void gunStopSeq( int channel )
{
    gunSeqActive[ channel ] = 0;
    gunSeqMelody[ channel ] = GUN_MELODY_NONE;
}

bool gunSeqPlaying( int channel )
{
    return gunSeqActive[ channel ] != 0;
}

void gunAdvanceOneSeq( int channel )
{
    int length, note;

    if( gunSeqActive[ channel ] == 0 ) return;

    if( gunSeqWait[ channel ] > 0 )
    {
        gunSeqWait[ channel ] = gunSeqWait[ channel ] - 1;
        return;
    }

    length = gunMelodyValue( gunSeqMelody[ channel ], gunSeqPos[ channel ] );
    if( length == 0 )
    {
        gunStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        gunSeqPos[ channel ] = 0;
        length = gunMelodyValue( gunSeqMelody[ channel ], 0 );
    }
    note = gunMelodyValue( gunSeqMelody[ channel ], gunSeqPos[ channel ] + 1 );
    gunSeqPos[ channel ] = gunSeqPos[ channel ] + 2;
    gunSeqWait[ channel ] = gunNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)gunFrequencies[ note - 1 ], (float)gunSeqWait[ channel ] / 60.0 );
}

void gunAdvanceSound()
{
    gunAdvanceOneSeq( 0 );
    gunAdvanceOneSeq( 1 );
    gunAdvanceOneSeq( 2 );
}

void gunSoundFire()
{
    gunStartSeq( 0, GUN_MELODY_FIRE );
}

void gunSoundUp()
{
    gunStartSeq( 0, GUN_MELODY_UP );
}

// Sound_SmallBang()/Sound_LargeBang() are genuine noise-channel effects
// upstream (a real LFSR, not a melody) - approximated as one representative
// tone at the effect's own real starting frequency, matching Astro
// Barrier's own established "derive Hz, approximate envelope" precedent
// for a hardware synth effect this shim's simple md_playTone() can't
// otherwise reproduce.
void gunSoundSmallBang()
{
    md_playTone( 3000.0, 0.15 );
}

void gunSoundLargeBang()
{
    md_playTone( 1500.0, 0.2 );
}

void gunStartBgm()
{
    gunStartSeq( 1, GUN_MELODY_BGM1 );
    gunStartSeq( 2, GUN_MELODY_BGM2 );
}

void gunStopBgm()
{
    // upstream's own StopBGM() resets ALL 3 ToneChannels (including channel
    // 0, the Fire/Up SFX channel), not just the 2 BGM channels - matching
    // that exactly rather than leaving channel 0's sequencer state armed
    // (which would otherwise keep advancing via gunAdvanceSound() every
    // tick regardless of game state, and could re-trigger md_playTone() on
    // top of the GAME OVER jingle a few frames later if a fire/up sound was
    // still mid-flight the instant the game ended).
    gunStopSeq( 0 );
    gunStopSeq( 1 );
    gunStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Enemy.cpp
// -----------------------------------------------------------------------------

bool gunHitBulletEnemy( int bulletX, int bulletY, int enemyX, int enemyY )
{
    return
        bulletX + GUN_COORD_RATE >= enemyX &&
        enemyX + GUN_COORD_RATE >= bulletX &&
        bulletY + GUN_COORD_RATE * 3 / 4 >= enemyY &&
        enemyY + GUN_COORD_RATE * 7 / 4 >= bulletY;
}


// -----------------------------------------------------------------------------
//   EnemyRow.cpp - part 1 (no Fighter/MovingEnemy dependency)
// -----------------------------------------------------------------------------

int gunFixedEnemyY( int target )
{
    return ( ( target & 0xf0 ) >> ( 3 - GUN_COORD_SHIFT ) ) + GUN_TOP * GUN_COORD_RATE;
}

int gunFixedEnemyX( int target )
{
    return ( ( target & 0x0f ) << ( GUN_COORD_SHIFT + 1 ) ) + gunEnemyRowLeft;
}

void gunInitEnemyRows()
{
    int i;
    for( i = 0; i < GUN_ENEMYROW_MAX_COUNT; i = i + 1 )
    {
        gunEnemyRowMemberCount[ i ] = 0;
        gunEnemyRowFlags[ i ][ 0 ] = 0;
        gunEnemyRowFlags[ i ][ 1 ] = 0;
    }
    gunEnemyRowCount = gunStageRowCount[ gunStageIndex ];
    gunEnemyRowLeft = ( GUN_ENEMYROW_MIN_INITIAL_X + ( gunStageMin[ gunStageIndex ] << 1 ) ) << GUN_COORD_SHIFT;
    gunEnemyRowWidth = ( ( gunStageMax[ gunStageIndex ] - gunStageMin[ gunStageIndex ] + 1 ) << 1 ) << GUN_COORD_SHIFT;
    gunEnemyRowDirection = -1;
    gunNextRow = gunEnemyRowCount - 1;
    gunNextColumn = 0;
}

void gunAddEnemyRowMember( int target, int type )
{
    int rowIdx, column;
    rowIdx = target >> 4;
    gunEnemyRowMemberCount[ rowIdx ] = gunEnemyRowMemberCount[ rowIdx ] + 1;
    gunEnemyRowType[ rowIdx ] = type;
    column = target & 0x0f;
    gunEnemyRowFlags[ rowIdx ][ column >> 3 ] = gunEnemyRowFlags[ rowIdx ][ column >> 3 ] | ( 1 << ( column & 7 ) );
}

void gunDrawEnemyRows()
{
    int leftCoord, rowIdx;
    leftCoord = gunEnemyRowLeft;
    for( rowIdx = 0; rowIdx < GUN_ENEMYROW_MAX_COUNT; rowIdx = rowIdx + 1 )
    {
        int n;
        n = gunEnemyRowMemberCount[ rowIdx ];
        if( n != 0 )
        {
            int row, mask, bits, cType, byteIdx, colIdx, col;
            row = GUN_TOP + rowIdx * 2;
            cType = ( gunEnemyRowType[ rowIdx ] << 5 ) + GUN_CHAR_ENEMY;
            col = leftCoord;
            byteIdx = 0;
            bits = gunEnemyRowFlags[ rowIdx ][ 0 ];
            mask = 1;
            for( colIdx = 0; colIdx < GUN_ENEMYROW_COLUMN_COUNT; colIdx = colIdx + 1 )
            {
                if( ( bits & mask ) != 0 )
                {
                    gunVPut2C( row, col, cType );
                    n = n - 1;
                }
                col = col + 2;
                mask = mask << 1;
                if( mask == 0x100 )
                {
                    byteIdx = byteIdx + 1;
                    bits = gunEnemyRowFlags[ rowIdx ][ byteIdx ];
                    mask = 1;
                }
                if( n == 0 ) break;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   DrawAll (Vram.cpp) - populates gunVVram only; the real hardware push
//   happens in gunRender(), matching gameCracky.c's own split.
// -----------------------------------------------------------------------------

void gunDrawAll()
{
    int r, c;
    for( r = 0; r < GUN_VVRAM_HEIGHT; r = r + 1 )
      for( c = 0; c < GUN_VVRAM_WIDTH; c = c + 1 )
        gunVVram[ r ][ c ] = GUN_CHAR_SPACE;
    gunDrawStars();
    gunDrawEnemyRows();
    gunDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 1 (needs Sprite module only)
// -----------------------------------------------------------------------------

void gunInitMovingEnemies()
{
    int i, sprite;
    sprite = GUN_SPRITE_ENEMY;
    for( i = 0; i < GUN_MAX_ENEMY_COUNT; i = i + 1 )
    {
        gunMovingEnemies[ i ].status = GUN_ME_NONE;
        gunMovingEnemies[ i ].sprite = sprite;
        sprite = sprite + 1;
    }
    gunFreeEnemyCount = GUN_MAX_ENEMY_COUNT;
    gunMeClock = 0;
}

int gunStartMovingEnemy( int status, int type )
{
    int i;
    for( i = 0; i < GUN_MAX_ENEMY_COUNT; i = i + 1 )
    {
        if( gunMovingEnemies[ i ].status == GUN_ME_NONE )
        {
            gunMovingEnemies[ i ].status = status;
            gunMovingEnemies[ i ].type = type;
            gunMovingEnemies[ i ].bulletCount = gunCurrentStage + 1;
            gunFreeEnemyCount = gunFreeEnemyCount - 1;
            return i;
        }
    }
    return -1;
}

void gunShowMovingEnemy( int idx )
{
    int type, pattern;
    type = gunMovingEnemies[ idx ].type;
    pattern = GUN_CHAR_ENEMY + ( ( ( type << 3 ) | gunMovingEnemies[ idx ].direction ) << 2 );
    gunShowSprite( gunMovingEnemies[ idx ].sprite, gunMovingEnemies[ idx ].x, gunMovingEnemies[ idx ].y, pattern );
}

void gunSetMovingEnemyDirection( int idx, int direction )
{
    gunMovingEnemies[ idx ].dx = gunDirections[ direction ][ 0 ];
    gunMovingEnemies[ idx ].dy = gunDirections[ direction ][ 1 ];
    gunMovingEnemies[ idx ].direction = direction;
}

void gunEndMovingEnemy( int idx )
{
    gunMovingEnemies[ idx ].status = GUN_ME_NONE;
    gunHideSprite( gunMovingEnemies[ idx ].sprite );
    gunFreeEnemyCount = gunFreeEnemyCount + 1;
}


// -----------------------------------------------------------------------------
//   EnemyBullet.cpp - part 1 (init + start; move deferred, needs Fighter)
// -----------------------------------------------------------------------------

void gunInitEnemyBullets()
{
    int i, sprite;
    sprite = GUN_SPRITE_ENEMY_BULLET;
    for( i = 0; i < GUN_MAX_ENEMY_BULLET_COUNT; i = i + 1 )
    {
        gunEnemyBullets[ i ].sprite = sprite;
        gunEnemyBullets[ i ].y = 255;
        sprite = sprite + 1;
    }
}

bool gunStartEnemyBullet( int x, int y )
{
    int i;
    for( i = 0; i < GUN_MAX_ENEMY_BULLET_COUNT; i = i + 1 )
    {
        if( gunEnemyBullets[ i ].y < GUN_EB_RANGE_Y ) continue;

        gunEnemyBullets[ i ].dx = gunSign( x, gunFighterX );
        gunEnemyBullets[ i ].dy = gunSign( y, gunFighterY );
        if( gunEnemyBullets[ i ].dx != 0 && gunEnemyBullets[ i ].dy != 0 )
        {
            int lx, ly;
            lx = gunAbs( x, gunFighterX );
            ly = gunAbs( y, gunFighterY );
            if( lx < ly )
            {
                gunEnemyBullets[ i ].numeratorX = GUN_EB_SHORT_VELOCITY;
                gunEnemyBullets[ i ].numeratorY = GUN_EB_LONG_VELOCITY;
            }
            else if( lx > ly )
            {
                gunEnemyBullets[ i ].numeratorX = GUN_EB_LONG_VELOCITY;
                gunEnemyBullets[ i ].numeratorY = GUN_EB_SHORT_VELOCITY;
            }
            else
            {
                gunEnemyBullets[ i ].numeratorX = GUN_EB_LO_VELOCITY;
                gunEnemyBullets[ i ].numeratorY = GUN_EB_LO_VELOCITY;
            }
        }
        else
        {
            gunEnemyBullets[ i ].numeratorX = GUN_EB_HI_VELOCITY;
            gunEnemyBullets[ i ].numeratorY = GUN_EB_HI_VELOCITY;
        }
        gunEnemyBullets[ i ].x = x;
        gunEnemyBullets[ i ].y = y;
        gunEnemyBullets[ i ].clock = 0;
        gunEnemyBullets[ i ].denominatorX = 0;
        gunEnemyBullets[ i ].denominatorY = 0;
        gunShowSprite( gunEnemyBullets[ i ].sprite, x, y, GUN_CHAR_ENEMY_BULLET );
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   FighterBullet.cpp - part 1 (init + start; move deferred)
// -----------------------------------------------------------------------------

void gunInitFighterBullets()
{
    int i, sprite;
    sprite = GUN_SPRITE_FIGHTER_BULLET;
    for( i = 0; i < GUN_FB_COUNT; i = i + 1 )
    {
        gunFighterBullets[ i ].sprite = sprite;
        gunFighterBullets[ i ].y = 255;
        sprite = sprite + 1;
    }
    gunFbIntervalCount = GUN_FB_SHORT_INTERVAL;
}

void gunStartFighterBullet( bool on )
{
    int i;
    if( gunFbIntervalCount != 0 )
      gunFbIntervalCount = gunFbIntervalCount - 1;
    if( !on )
    {
        if( gunFbIntervalCount > GUN_FB_SHORT_INTERVAL )
          gunFbIntervalCount = GUN_FB_SHORT_INTERVAL;
        return;
    }
    if( gunFbIntervalCount != 0 ) return;
    for( i = 0; i < GUN_FB_COUNT; i = i + 1 )
    {
        if( gunFighterBullets[ i ].y < GUN_FB_RANGE ) continue;
        gunSoundFire();
        gunFighterBullets[ i ].x = gunFighterX;
        gunFighterBullets[ i ].y = gunFighterY;
        gunFighterBullets[ i ].clock = 0;
        gunShowSprite( gunFighterBullets[ i ].sprite, gunFighterBullets[i].x, gunFighterBullets[i].y, GUN_CHAR_FIGHTER_BULLET );
        gunFbIntervalCount = GUN_FB_LONG_INTERVAL;
        return;
    }
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 2 (Fire/Turn/DecideDirection)
// -----------------------------------------------------------------------------

void gunFireMovingEnemy( int idx )
{
    if( ( gunMeClock & GUN_FIRE_MASK ) == 0 && gunMovingEnemies[ idx ].bulletCount > 0 )
    {
        int threshold;
        threshold = gunCurrentStage + 2;
        if( gunMovingEnemies[ idx ].status < GUN_ME_ATTACK )
          threshold = threshold >> 1;
        if( gunRnd() < threshold )
        {
            if( gunStartEnemyBullet( gunMovingEnemies[ idx ].x, gunMovingEnemies[ idx ].y ) )
              gunMovingEnemies[ idx ].bulletCount = gunMovingEnemies[ idx ].bulletCount - 1;
        }
    }
}

bool gunTurnMovingEnemy( int idx, int direction )
{
    int diff;
    if( direction == gunMovingEnemies[ idx ].direction ) return true;

    diff = ( direction - gunMovingEnemies[ idx ].direction ) & 7;
    if( diff <= 4 )
      direction = ( gunMovingEnemies[ idx ].direction + 1 ) & 7;
    else
      direction = ( gunMovingEnemies[ idx ].direction - 1 ) & 7;
    gunSetMovingEnemyDirection( idx, direction );
    return false;
}

void gunDecideDirection( int idx )
{
    int dx, dy, direction;
    dx = 0;
    dy = 0;
    if( gunMovingEnemies[ idx ].type == GUN_TYPE_CRASH )
    {
        dx = gunSign( gunMovingEnemies[ idx ].x, gunFighterX );
        dy = 1;
    }
    else if( gunMovingEnemies[ idx ].type == GUN_TYPE_SMART )
    {
        dx = gunSign( gunMovingEnemies[ idx ].x, gunFighterX );
        dy = gunSign( gunMovingEnemies[ idx ].y, gunFighterY );
    }
    else if( gunMovingEnemies[ idx ].type == GUN_TYPE_INSISTENT )
    {
        if( ( gunMeClock & GUN_LONG_MASK ) != 0 )
        {
            int x, y;
            x = ( gunRnd() & 0x0f ) << ( 1 + GUN_COORD_SHIFT );
            y = ( ( gunRnd() & 0x0f ) + 4 ) << GUN_COORD_SHIFT;
            dy = gunSign( gunMovingEnemies[ idx ].y, y );
            dx = gunSign( gunMovingEnemies[ idx ].x, x );
        }
        else
        {
            // upstream leaves dx/dy uninitialized here (real stack
            // garbage on AVR) - deliberately kept deterministic instead,
            // see header comment.
            dx = gunMovingEnemies[ idx ].dx;
            dy = gunMovingEnemies[ idx ].dy;
        }
    }
    direction = gunToDirection( dx, dy );
    if( !gunTurnMovingEnemy( idx, direction ) )
      gunSetMovingEnemyDirection( idx, direction );
}


// -----------------------------------------------------------------------------
//   Team.cpp - part 1 (pure array search, no MovingEnemy call-out needed)
// -----------------------------------------------------------------------------

void gunRemoveTeamMember( int enemyIdx )
{
    int t, i;
    for( t = 0; t < 2; t = t + 1 )
    {
        if( gunTeamMemberCount[ t ] != 0 )
        {
            for( i = 0; i < GUN_MAX_MEMBER_COUNT; i = i + 1 )
            {
                if( gunTeamMembers[ t ][ i ] == enemyIdx )
                {
                    gunTeamMembers[ t ][ i ] = -1;
                    gunTeamMemberCount[ t ] = gunTeamMemberCount[ t ] - 1;
                    return;
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 3 (Destroy)
// -----------------------------------------------------------------------------

void gunDestroyMovingEnemy( int idx )
{
    if( gunMovingEnemies[ idx ].status < GUN_ME_ALIGN )
      gunRemoveTeamMember( idx );
    gunSoundSmallBang();
    gunStartBang( gunMovingEnemies[ idx ].x + GUN_COORD_RATE, gunMovingEnemies[ idx ].y + GUN_COORD_RATE, false );
    gunEndMovingEnemy( idx );
    gunEnemyCount = gunEnemyCount - 1;
    gunAddScore( gunMovingEnemyPoints[ gunMovingEnemies[ idx ].type ] );
}


// -----------------------------------------------------------------------------
//   Fighter.cpp
// -----------------------------------------------------------------------------

void gunShowFighterSprite()
{
    gunShowSprite( GUN_SPRITE_FIGHTER, gunFighterX, gunFighterY, GUN_CHAR_FIGHTER );
}

void gunHideFighterSprite()
{
    gunHideSprite( GUN_SPRITE_FIGHTER );
}

void gunInitFighter()
{
    gunFighterX = GUN_FIGHTER_INITIAL_X;
    gunFighterY = GUN_FIGHTER_INITIAL_Y;
    gunShowFighterSprite();
    gunCrashCount = 0;
    gunReviveCount = 0;
}

void gunCrashFighter()
{
    gunHideFighterSprite();
    gunSoundLargeBang();
    gunStartBang( gunFighterX + GUN_COORD_RATE, gunFighterY + GUN_COORD_RATE, true );
    gunCrashCount = 5;
    gunPrintStatus();
}

bool gunHitBulletFighter( int x, int y )
{
    if(
        gunCrashCount == 0 && gunReviveCount == 0 &&
        x + GUN_COORD_RATE / 4 >= gunFighterX &&
        gunFighterX + GUN_COORD_RATE * 7 / 4 >= x &&
        y + GUN_COORD_RATE / 4 >= gunFighterY &&
        gunFighterY + GUN_COORD_RATE * 7 / 4 >= y
    )
    {
        gunCrashFighter();
        return true;
    }
    return false;
}

bool gunHitEnemyFighter( int x, int y )
{
    if(
        gunCrashCount == 0 && gunReviveCount == 0 &&
        x + GUN_COORD_RATE * 6 / 4 >= gunFighterX &&
        gunFighterX + GUN_COORD_RATE * 6 / 4 >= x &&
        y + GUN_COORD_RATE / 6 >= gunFighterY &&
        gunFighterY + GUN_COORD_RATE * 6 / 4 >= y
    )
    {
        gunCrashFighter();
        return true;
    }
    return false;
}

void gunMoveFighter()
{
    bool left, right, up, down, fire;
    if( gunCrashCount >= 1 )
    {
        gunCrashCount = gunCrashCount + 1;
        if( gunCrashCount >= GUN_FIGHTER_CRASH_RANGE )
        {
            gunRemainCount = gunRemainCount - 1;
            gunFighterX = GUN_FIGHTER_INITIAL_X;
            gunFighterY = GUN_FIGHTER_INITIAL_Y;
            gunCrashCount = 0;
            gunReviveCount = GUN_FIGHTER_REVIVE_TIME;
            gunPrintRemain();
        }
        return;
    }

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    if( left && gunFighterX > 0 ) gunFighterX = gunFighterX - 1;
    if( right && gunFighterX < ( GUN_VVRAM_WIDTH - 2 ) * GUN_COORD_RATE ) gunFighterX = gunFighterX + 1;
    if( up && gunFighterY > 0 ) gunFighterY = gunFighterY - 1;
    if( down && gunFighterY < ( GUN_VVRAM_HEIGHT - 2 ) * GUN_COORD_RATE ) gunFighterY = gunFighterY + 1;

    if( gunReviveCount > 0 )
    {
        gunReviveCount = gunReviveCount - 1;
        if( ( ( gunReviveCount >> GUN_COORD_SHIFT ) & 1 ) != 0 )
          gunShowFighterSprite();
        else
          gunHideFighterSprite();
    }
    else
      gunShowFighterSprite();

    gunStartFighterBullet( fire );
}


// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

void gunInitItem()
{
    gunItemY = 255;
}

void gunShowItem()
{
    gunShowSprite( GUN_SPRITE_ITEM, gunItemX, gunItemY, GUN_CHAR_ITEM );
}

void gunHideItem()
{
    gunHideSprite( GUN_SPRITE_ITEM );
    gunItemY = 255;
}

void gunStartItem( int x, int y )
{
    if( gunItemY < GUN_ITEM_RANGE ) return;
    gunItemX = x;
    gunItemY = y;
    gunShowItem();
}

void gunMoveItem()
{
    if( gunItemY < GUN_ITEM_RANGE )
    {
        gunItemY = gunItemY + 1;
        if( gunItemY >= GUN_ITEM_RANGE )
        {
            gunHideItem();
            return;
        }
        if(
            gunItemX + GUN_COORD_RATE >= gunFighterX &&
            gunFighterX + GUN_COORD_RATE >= gunItemX &&
            gunItemY + GUN_COORD_RATE >= gunFighterY &&
            gunFighterY + GUN_COORD_RATE >= gunItemY
        )
        {
            gunHideItem();
            gunSoundUp();
            gunRemainCount = gunRemainCount + 1;
            gunPrintStatus();
            return;
        }
        gunShowItem();
    }
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 4 (Move, needs Fighter+Destroy)
// -----------------------------------------------------------------------------

bool gunMoveMovingEnemyPosition( int idx )
{
    int newX, newY;
    bool inRange;
    inRange = true;

    newX = ( gunMovingEnemies[ idx ].x + gunMovingEnemies[ idx ].dx ) & 0xFF;
    if( newX >= GUN_ME_RANGE_X + 1 )
    {
        if( gunMovingEnemies[ idx ].x < GUN_COORD_RATE * 2 )
          newX = 0;
        else
          newX = GUN_ME_RANGE_X;
        inRange = false;
    }
    gunMovingEnemies[ idx ].x = newX;

    newY = ( gunMovingEnemies[ idx ].y + gunMovingEnemies[ idx ].dy ) & 0xFF;
    if( newY >= GUN_ME_RANGE_Y + 1 )
    {
        if( gunMovingEnemies[ idx ].y < GUN_COORD_RATE * 2 )
          newY = 0;
        else
          newY = GUN_ME_RANGE_Y;
        inRange = false;
    }
    gunMovingEnemies[ idx ].y = newY;

    gunShowMovingEnemy( idx );
    if( gunHitEnemyFighter( gunMovingEnemies[ idx ].x, gunMovingEnemies[ idx ].y ) )
    {
        gunDestroyMovingEnemy( idx );
        inRange = true;
    }
    return inRange;
}


// -----------------------------------------------------------------------------
//   EnemyRow.cpp - part 2 (Destroy/StartAttacking/Move/Hit - needs Fighter,
//   MovingEnemy start/show)
// -----------------------------------------------------------------------------

void gunEnemyRowDestroy( int rowIdx, int x, int y )
{
    gunSoundSmallBang();
    gunStartBang( x + GUN_COORD_RATE, y + GUN_COORD_RATE, false );
    gunEnemyCount = gunEnemyCount - 1;
    gunEnemyRowMemberCount[ rowIdx ] = gunEnemyRowMemberCount[ rowIdx ] - 1;
    gunAddScore( gunEnemyRowPoints[ gunEnemyRowType[ rowIdx ] ] );
}

bool gunStartAttacking( int rowIdx )
{
    if( gunEnemyRowMemberCount[ rowIdx ] != 0 )
    {
        int columnIndex, byteIndex, bit;
        columnIndex = gunEnemyRowColumns[ gunNextColumn ];
        gunNextColumn = gunNextColumn + 1;
        if( gunNextColumn >= GUN_ENEMYROW_COLUMN_COUNT )
          gunNextColumn = 0;
        byteIndex = columnIndex >> 3;
        bit = 1 << ( columnIndex & 7 );
        if( ( gunEnemyRowFlags[ rowIdx ][ byteIndex ] & bit ) != 0 )
        {
            int enemyIdx;
            enemyIdx = gunStartMovingEnemy( GUN_ME_ATTACK, gunEnemyRowType[ rowIdx ] );
            if( enemyIdx != -1 )
            {
                gunMovingEnemies[ enemyIdx ].x = gunEnemyRowLeft + ( columnIndex << 1 );
                gunMovingEnemies[ enemyIdx ].y = ( rowIdx << 1 ) + GUN_TOP * GUN_COORD_RATE;
                gunMovingEnemies[ enemyIdx ].target = ( rowIdx << 4 ) | columnIndex;
                gunMovingEnemies[ enemyIdx ].bulletCount = gunCurrentStage + 1;
                gunEnemyRowFlags[ rowIdx ][ byteIndex ] = gunEnemyRowFlags[ rowIdx ][ byteIndex ] & ~bit;
                gunEnemyRowMemberCount[ rowIdx ] = gunEnemyRowMemberCount[ rowIdx ] - 1;
                gunShowMovingEnemy( enemyIdx );
                return true;
            }
        }
    }
    return false;
}

void gunMoveEnemyRows()
{
    int oldCoord, newX, newCoord;
    oldCoord = gunEnemyRowLeft;
    newX = ( gunEnemyRowLeft + gunEnemyRowDirection ) & 0xFF;
    if( newX >= GUN_ENEMYROW_RANGE_X || newX + gunEnemyRowWidth >= GUN_ENEMYROW_RANGE_X )
    {
        gunEnemyRowDirection = -gunEnemyRowDirection;
        newX = ( gunEnemyRowLeft + gunEnemyRowDirection ) & 0xFF;
    }
    gunEnemyRowLeft = newX;
    newCoord = gunEnemyRowLeft;
    if( newCoord != oldCoord )
    {
        if( gunFormationCount == 0 && gunFreeEnemyCount != 0 )
        {
            int r;
            for( r = 0; r < GUN_ENEMYROW_MAX_COUNT; r = r + 1 )
            {
                int rowIndex;
                rowIndex = gunNextRow;
                gunNextRow = ( gunNextRow - 1 ) & 0xFF;
                if( gunNextRow >= gunEnemyRowCount )
                  gunNextRow = gunEnemyRowCount - 1;
                if( gunStartAttacking( rowIndex ) ) break;
            }
        }
    }
    {
        int y, rowIdx;
        bool hitFound;
        y = GUN_TOP * GUN_COORD_RATE;
        hitFound = false;
        for( rowIdx = 0; rowIdx < GUN_ENEMYROW_MAX_COUNT; rowIdx = rowIdx + 1 )
        {
            if( !hitFound && gunEnemyRowMemberCount[ rowIdx ] != 0 )
            {
                int x, mask, bits, byteIdx, colIdx;
                x = gunEnemyRowLeft;
                byteIdx = 0;
                bits = gunEnemyRowFlags[ rowIdx ][ 0 ];
                mask = 1;
                for( colIdx = 0; colIdx < GUN_ENEMYROW_COLUMN_COUNT && !hitFound; colIdx = colIdx + 1 )
                {
                    if( ( bits & mask ) != 0 )
                    {
                        if( gunHitEnemyFighter( x, y ) )
                        {
                            bits = bits & ~mask;
                            gunEnemyRowFlags[ rowIdx ][ byteIdx ] = bits;
                            gunEnemyRowDestroy( rowIdx, x, y );
                            hitFound = true;
                        }
                    }
                    if( !hitFound )
                    {
                        mask = mask << 1;
                        if( mask == 0x100 )
                        {
                            byteIdx = byteIdx + 1;
                            bits = gunEnemyRowFlags[ rowIdx ][ byteIdx ];
                            mask = 1;
                        }
                        x = x + GUN_COORD_RATE * 2;
                    }
                }
            }
            y = y + GUN_COORD_RATE * 2;
        }
    }
}

bool gunHitEnemyRows( int bulletX, int bulletY )
{
    int y, rowIdx;
    y = GUN_TOP * GUN_COORD_RATE;
    for( rowIdx = 0; rowIdx < GUN_ENEMYROW_MAX_COUNT; rowIdx = rowIdx + 1 )
    {
        if( gunEnemyRowMemberCount[ rowIdx ] != 0 )
        {
            int enemyX, mask, bits, byteIdx, colIdx;
            enemyX = gunEnemyRowLeft;
            byteIdx = 0;
            bits = gunEnemyRowFlags[ rowIdx ][ 0 ];
            mask = 1;
            for( colIdx = 0; colIdx < GUN_ENEMYROW_COLUMN_COUNT; colIdx = colIdx + 1 )
            {
                if( ( bits & mask ) != 0 )
                {
                    if( gunHitBulletEnemy( bulletX, bulletY, enemyX, y ) )
                    {
                        bits = bits & ~mask;
                        gunEnemyRowFlags[ rowIdx ][ byteIdx ] = bits;
                        gunEnemyRowDestroy( rowIdx, enemyX, y );
                        return true;
                    }
                }
                mask = mask << 1;
                if( mask == 0x100 )
                {
                    byteIdx = byteIdx + 1;
                    bits = gunEnemyRowFlags[ rowIdx ][ byteIdx ];
                    mask = 1;
                }
                enemyX = enemyX + GUN_COORD_RATE * 2;
            }
        }
        y = y + GUN_COORD_RATE * 2;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 5 (Turn logic / Align / dispatch, needs EnemyRow)
// -----------------------------------------------------------------------------

bool gunTurnLogicMovingEnemy( int idx )
{
    if( gunTurnMovingEnemy( idx, GUN_DIR_UP ) )
    {
        gunAddEnemyRowMember( gunMovingEnemies[ idx ].target, gunMovingEnemies[ idx ].type );
        gunEndMovingEnemy( idx );
        return true;
    }
    {
        int targetX, targetY;
        targetX = gunFixedEnemyX( gunMovingEnemies[ idx ].target );
        gunMovingEnemies[ idx ].dx = gunSign( gunMovingEnemies[ idx ].x, targetX );
        targetY = gunFixedEnemyY( gunMovingEnemies[ idx ].target );
        gunMovingEnemies[ idx ].dy = gunSign( gunMovingEnemies[ idx ].y, targetY );
    }
    return false;
}

void gunAlignMovingEnemy( int idx )
{
    bool skipMove;
    skipMove = false;
    if( ( gunMeClock & GUN_COORD_MASK ) == 0 )
    {
        int targetX, targetY;
        targetX = gunFixedEnemyX( gunMovingEnemies[ idx ].target );
        targetY = gunFixedEnemyY( gunMovingEnemies[ idx ].target );
        if( gunAbs( gunMovingEnemies[ idx ].x, targetX ) < GUN_COORD_RATE &&
            gunAbs( gunMovingEnemies[ idx ].y, targetY ) < GUN_COORD_RATE )
        {
            gunMovingEnemies[ idx ].status = GUN_ME_TURN;
            skipMove = gunTurnLogicMovingEnemy( idx );
        }
        else
        {
            gunMovingEnemies[ idx ].dx = gunSign( gunMovingEnemies[ idx ].x, targetX );
            gunMovingEnemies[ idx ].dy = gunSign( gunMovingEnemies[ idx ].y, targetY );
            gunMovingEnemies[ idx ].direction = gunToDirection( gunMovingEnemies[ idx ].dx, gunMovingEnemies[ idx ].dy );
        }
    }
    if( !skipMove )
      gunMoveMovingEnemyPosition( idx );
}

void gunProcessTurnState( int idx )
{
    bool skipMove;
    skipMove = false;
    if( ( gunMeClock & GUN_COORD_MASK ) == 0 )
      skipMove = gunTurnLogicMovingEnemy( idx );
    if( !skipMove )
      gunMoveMovingEnemyPosition( idx );
}

void gunMoveMovingEnemies()
{
    int i;
    for( i = 0; i < GUN_MAX_ENEMY_COUNT; i = i + 1 )
    {
        if( gunMovingEnemies[ i ].status == GUN_ME_ALIGN )
          gunAlignMovingEnemy( i );
        else if( gunMovingEnemies[ i ].status == GUN_ME_SALLY )
        {
            gunFireMovingEnemy( i );
            gunMoveMovingEnemyPosition( i );
        }
        else if( gunMovingEnemies[ i ].status == GUN_ME_TURN )
          gunProcessTurnState( i );
        else if( gunMovingEnemies[ i ].status == GUN_ME_ATTACK )
        {
            if( ( gunMeClock & 1 ) == 0 )
            {
                if( ( gunMeClock & GUN_TURN_MASK ) == 0 )
                  gunDecideDirection( i );
                gunFireMovingEnemy( i );
                if( gunMovingEnemies[ i ].bulletCount == 0 )
                  gunMovingEnemies[ i ].status = GUN_ME_RETURN;
                if( !gunMoveMovingEnemyPosition( i ) )
                  gunMovingEnemies[ i ].status = GUN_ME_RETURN;
            }
        }
        else if( gunMovingEnemies[ i ].status == GUN_ME_RETURN )
        {
            if( ( gunMeClock & 1 ) == 0 )
              gunAlignMovingEnemy( i );
        }
    }
    gunMeClock = gunMeClock + 1;
}


// -----------------------------------------------------------------------------
//   MovingEnemy.cpp - part 6 (bullet collision, needs Item)
// -----------------------------------------------------------------------------

bool gunHitBulletMovingEnemy( int x, int y )
{
    int i;
    for( i = 0; i < GUN_MAX_ENEMY_COUNT; i = i + 1 )
    {
        if( gunMovingEnemies[ i ].status >= GUN_ME_SALLY )
        {
            if( gunHitBulletEnemy( x, y, gunMovingEnemies[ i ].x, gunMovingEnemies[ i ].y ) )
            {
                if( gunMovingEnemies[ i ].type == 2 &&
                    gunMovingEnemies[ i ].status == GUN_ME_ATTACK &&
                    y < ( GUN_VVRAM_HEIGHT / 2 ) * GUN_COORD_RATE &&
                    gunRemainCount < 10 &&
                    gunRnd() < 6 )
                  gunStartItem( gunMovingEnemies[ i ].x, gunMovingEnemies[ i ].y );
                gunDestroyMovingEnemy( i );
                return true;
            }
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   FighterBullet.cpp - part 2 (Move, needs MovingEnemy+EnemyRow hit checks)
// -----------------------------------------------------------------------------

bool gunFighterBulletHit( int idx )
{
    if( ( gunFighterBullets[ idx ].y & GUN_FB_HALF_MASK ) != 0 ) return false;
    if( gunHitBulletMovingEnemy( gunFighterBullets[ idx ].x, gunFighterBullets[ idx ].y ) ) return true;
    if( gunHitEnemyRows( gunFighterBullets[ idx ].x, gunFighterBullets[ idx ].y ) ) return true;
    return false;
}

void gunMoveFighterBullets()
{
    int i;
    for( i = 0; i < GUN_FB_COUNT; i = i + 1 )
    {
        if( gunFighterBullets[ i ].y >= GUN_FB_RANGE ) continue;
        gunFighterBullets[ i ].y = ( gunFighterBullets[ i ].y - 1 ) & 0xFF;
        if( gunFighterBullets[ i ].y >= GUN_FB_RANGE ||
            ( ( gunFighterBullets[ i ].clock & GUN_COORD_MASK ) == 0 && gunFighterBulletHit( i ) ) )
        {
            gunHideSprite( gunFighterBullets[ i ].sprite );
            gunFighterBullets[ i ].y = 255;
        }
        else
        {
            gunShowSprite( gunFighterBullets[ i ].sprite, gunFighterBullets[i].x, gunFighterBullets[i].y, GUN_CHAR_FIGHTER_BULLET );
            gunFighterBullets[ i ].clock = gunFighterBullets[ i ].clock + 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   EnemyBullet.cpp - part 2 (Move, needs Fighter hit check)
// -----------------------------------------------------------------------------

void gunMoveEnemyBullets()
{
    int i;
    for( i = 0; i < GUN_MAX_ENEMY_BULLET_COUNT; i = i + 1 )
    {
        if( gunEnemyBullets[ i ].y >= GUN_EB_RANGE_Y ) continue;

        gunEnemyBullets[ i ].denominatorX = gunEnemyBullets[ i ].denominatorX - gunEnemyBullets[ i ].numeratorX;
        if( gunEnemyBullets[ i ].denominatorX < 0 )
        {
            gunEnemyBullets[ i ].x = ( gunEnemyBullets[ i ].x + gunEnemyBullets[ i ].dx ) & 0xFF;
            gunEnemyBullets[ i ].denominatorX = gunEnemyBullets[ i ].denominatorX + GUN_EB_HI_VELOCITY;
        }
        gunEnemyBullets[ i ].denominatorY = gunEnemyBullets[ i ].denominatorY - gunEnemyBullets[ i ].numeratorY;
        if( gunEnemyBullets[ i ].denominatorY < 0 )
        {
            gunEnemyBullets[ i ].y = ( gunEnemyBullets[ i ].y + gunEnemyBullets[ i ].dy ) & 0xFF;
            gunEnemyBullets[ i ].denominatorY = gunEnemyBullets[ i ].denominatorY + GUN_EB_HI_VELOCITY;
        }
        if( gunEnemyBullets[ i ].x >= GUN_EB_RANGE_X || gunEnemyBullets[ i ].y >= GUN_EB_RANGE_Y ||
            ( ( gunEnemyBullets[ i ].clock & GUN_COORD_MASK ) == 0 && gunHitBulletFighter( gunEnemyBullets[i].x, gunEnemyBullets[i].y ) ) )
        {
            gunHideSprite( gunEnemyBullets[ i ].sprite );
            gunEnemyBullets[ i ].y = 255;
        }
        else
        {
            gunShowSprite( gunEnemyBullets[ i ].sprite, gunEnemyBullets[i].x, gunEnemyBullets[i].y, GUN_CHAR_ENEMY_BULLET );
            gunEnemyBullets[ i ].clock = gunEnemyBullets[ i ].clock + 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Team.cpp - part 2 (StartTeam / MoveTeams, needs MovingEnemy start/show)
// -----------------------------------------------------------------------------

int gunFormationTypeAt( int formIdx, int elemIdx )
{
    int typeByte;
    typeByte = gunFormationTypes[ formIdx ][ elemIdx >> 2 ];
    typeByte = typeByte >> ( ( elemIdx & 3 ) * 2 );
    return typeByte & 3;
}

void gunStartTeam()
{
    int t, formIdx, courseIdx, elementCount, i, memberIdx;
    if( gunFormationCount == 0 ) return;
    formIdx = gunCurFormation;
    elementCount = gunFormationElementCount[ formIdx ];
    if( gunFreeEnemyCount < elementCount ) return;
    for( t = 0; t < 2; t = t + 1 )
    {
        if( gunTeamMemberCount[ t ] == 0 )
        {
            courseIdx = gunFormationCourseIndex[ formIdx ];
            gunTeamFormation[ t ] = formIdx;
            gunTeamCourse[ t ] = courseIdx;
            gunTeamSallyCount[ t ] = gunCourseSallyCount[ courseIdx ];
            gunTeamDirections[ t ][ 0 ] = gunCourseDirection[ courseIdx ];
            gunTeamCourseElementIndex[ t ] = 0;
            gunTeamDirectionIndex[ t ] = 0;

            for( i = 0; i < elementCount; i = i + 1 )
            {
                memberIdx = gunStartMovingEnemy( GUN_ME_STANDBY, gunFormationTypeAt( formIdx, i ) );
                if( memberIdx == -1 )
                {
                    gunTeamMembers[ t ][ i ] = -1;
                    continue;
                }
                gunEnemyCount = gunEnemyCount + 1;
                gunMovingEnemies[ memberIdx ].target = gunFormationTargets[ formIdx ][ i ];
                gunTeamMembers[ t ][ i ] = memberIdx;
                gunTeamMemberCount[ t ] = gunTeamMemberCount[ t ] + 1;
            }
            for( i = elementCount; i < GUN_MAX_MEMBER_COUNT; i = i + 1 )
              gunTeamMembers[ t ][ i ] = -1;
            gunTeamNextMember[ t ] = 0;

            gunCurFormation = gunCurFormation + 1;
            gunFormationCount = gunFormationCount - 1;
            return;
        }
    }
}

void gunMoveTeams()
{
    int t;
    if( ( gunTeamClock & GUN_COORD_MASK ) == 0 )
    {
        for( t = 0; t < 2; t = t + 1 )
        {
            if( gunTeamMemberCount[ t ] != 0 )
            {
                if( ( gunTeamClock & GUN_HALF_MASK ) == 0 &&
                    gunTeamNextMember[ t ] < gunFormationElementCount[ gunTeamFormation[ t ] ] )
                {
                    int memberIdx;
                    memberIdx = gunTeamMembers[ t ][ gunTeamNextMember[ t ] ];
                    if( memberIdx != -1 )
                    {
                        gunMovingEnemies[ memberIdx ].x = gunCourseX[ gunTeamCourse[ t ] ];
                        gunMovingEnemies[ memberIdx ].y = gunCourseY[ gunTeamCourse[ t ] ];
                        gunMovingEnemies[ memberIdx ].status = GUN_ME_SALLY;
                        gunShowMovingEnemy( memberIdx );
                    }
                    gunTeamNextMember[ t ] = gunTeamNextMember[ t ] + 1;
                }
                {
                    int i;
                    for( i = 0; i < GUN_MAX_MEMBER_COUNT; i = i + 1 )
                    {
                        int memberIdx, direction;
                        memberIdx = gunTeamMembers[ t ][ i ];
                        if( memberIdx != -1 && gunMovingEnemies[ memberIdx ].status == GUN_ME_SALLY )
                        {
                            direction = gunTeamDirections[ t ][ i * 2 ];
                            if( direction == GUN_INVALID_ELEMENT )
                            {
                                gunMovingEnemies[ memberIdx ].status = GUN_ME_ALIGN;
                                gunTeamMemberCount[ t ] = gunTeamMemberCount[ t ] - 1;
                                gunTeamMembers[ t ][ i ] = -1;
                            }
                            else
                              gunSetMovingEnemyDirection( memberIdx, direction );
                        }
                    }
                }
                {
                    int j;
                    for( j = GUN_MAX_MEMBER_COUNT * 2 - 1; j >= 1; j = j - 1 )
                      gunTeamDirections[ t ][ j ] = gunTeamDirections[ t ][ j - 1 ];
                }
                {
                    bool doLookup;
                    int element;
                    doLookup = false;
                    if( gunTeamSallyCount[ t ] > 0 )
                    {
                        gunTeamSallyCount[ t ] = gunTeamSallyCount[ t ] - 1;
                        gunTeamCourseElementIndex[ t ] = 0;
                        doLookup = true;
                    }
                    else
                    {
                        gunTeamDirectionIndex[ t ] = gunTeamDirectionIndex[ t ] + 1;
                        if( gunTeamDirectionIndex[ t ] >= GUN_DIRECTION_COUNT )
                        {
                            gunTeamCourseElementIndex[ t ] = gunTeamCourseElementIndex[ t ] + 1;
                            doLookup = true;
                        }
                    }
                    if( doLookup )
                    {
                        if( gunTeamCourseElementIndex[ t ] > 9 )
                          gunTeamCourseElementIndex[ t ] = 9;
                        element = gunCourseElements[ gunTeamCourse[ t ] ][ gunTeamCourseElementIndex[ t ] ];
                        if( element == GUN_INVALID_ELEMENT )
                          gunTeamCourseElement[ t ] = -1;
                        else
                        {
                            gunTeamCourseElement[ t ] = element;
                            gunTeamDirectionIndex[ t ] = 0;
                        }
                    }
                    if( gunTeamCourseElement[ t ] == -1 )
                      gunTeamDirections[ t ][ 0 ] = GUN_INVALID_ELEMENT;
                    else
                      gunTeamDirections[ t ][ 0 ] = gunCourseElementDirs[ gunTeamCourseElement[ t ] ][ gunTeamDirectionIndex[ t ] ];
                }
            }
        }
    }
    gunTeamClock = gunTeamClock + 1;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

void gunInitStageIndex()
{
    int i, j;
    i = 0;
    j = 0;
    while( i < gunCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= GUN_STAGE_COUNT )
          j = 0;
    }
    gunStageIndex = j;
}

void gunInitTeams()
{
    int t;
    for( t = 0; t < 2; t = t + 1 )
      gunTeamMemberCount[ t ] = 0;
    gunCurFormation = gunStageFormationStart[ gunStageIndex ];
    gunFormationCount = gunStageFormationCount[ gunStageIndex ];
    gunTeamClock = 0;
}

void gunInitStage()
{
    gunInitStageIndex();
    gunInitMovingEnemies();
    gunInitEnemyRows();
    gunInitTeams();
    gunStartTeam();
    gunDrawAll();
}

void gunInitPlaying()
{
    int i, j;
    gunRndIndex = 0;
    // Clears the status-text grid - Title() leaves GUNTUS/INUFUTO/START/
    // CONTINUE text sitting in gunStatusChar, which nothing else would
    // ever naturally overwrite (matching gameCracky.c's own established
    // "stale status-text grid" lesson).
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        gunStatusChar[ i ][ j ] = 0;
    gunOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in gunUpdateTitle()) - matches gameCracky.c's own
    // crkInitTrying()/crkFullWidthText belt-and-suspenders reset, in case
    // any future call site ever reaches gunInitPlaying() without going
    // through that transition first.
    gunFullWidthText = false;
    gunInitBangs();
    gunInitItem();
    gunInitFighter();
    gunInitFighterBullets();
    gunInitEnemyBullets();
    gunPrintStatus();
    gunInitEnemyRows();
}


// -----------------------------------------------------------------------------
//   Rendering - no hardware-orientation transform, see header comment.
// -----------------------------------------------------------------------------

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly,
// matching gameCracky.c's own crkComposeRawByte() - see that file's header
// for the full derivation. Reads gunVVram at rawPage*2+2/+3 by default
// (the shifted addressing every *gameplay* frame uses, since DrawAll()'s
// own real call is `VVramToVram(VVram + VVramWidth*2)` - see this file's
// own "second hidden-row staging area" header note), but the TITLE screen
// is a genuine exception: upstream's own `Title()` draws its logo bitmap
// via a completely DIFFERENT, UNSHIFTED `VVramToVram(VVram)` call (no
// `+VVramWidth*2`) - so while `gunFullWidthText` is set, this reads
// gunVVram at the unshifted rawPage*2/+1 instead, matching upstream's own
// real addressing for that one screen. Both mapByte and textByte are
// computed and OR-combined (not chosen exclusively) whenever
// gunFullWidthText is set, mirroring gameCracky.c's own fix exactly - the
// logo (rows 2-5, landing on real hardware pages 1-2 under the unshifted
// addressing - see gunBeginTitle()'s own comment) and every status-text
// element (SCORE/MINI/START/CONTINUE/credit, pages 0/1/3/5/6/7) never
// actually share a (col,page) cell, so this can never blend two real,
// distinct pieces of content together - it just lets whichever one is
// actually present at a given (col,page) show through, instead of one
// silently excluding the other the way the original text-only substitute
// did.
int gunComposeRawByte( int rawCol, int rawPage )
{
    int mapByte;

    mapByte = 0;
    if( rawCol < GUN_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte, row0, row1;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        if( gunFullWidthText )
          row0 = rawPage * 2;
        else
          row0 = rawPage * 2 + 2;
        row1 = row0 + 1;
        upper = gunVVram[ row0 ][ mapX ];
        lower = gunVVram[ row1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = gunCharPattern[ upper * 2 + 0 ];
            lowerByte = gunCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = gunCharPattern[ upper * 2 + 0 ];
            lowerByte = gunCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = gunCharPattern[ upper * 2 + 1 ];
            lowerByte = gunCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = gunCharPattern[ upper * 2 + 1 ];
            lowerByte = gunCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !gunFullWidthText && rawCol < GUN_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // gunStatusChar's own full-width indexing directly.
    {
        int charCol, sub, c, textByte;
        textByte = 0;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = gunStatusChar[ rawPage ][ charCol ];
            textByte = gunAsciiPattern[ c * 4 + sub ];
        }
        return mapByte | textByte;
    }
}

void gunRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( gunOverlayActive && page == gunOverlayPage &&
                col >= gunOverlayCol * 4 && col < gunOverlayCol * 4 + gunOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - gunOverlayCol * 4 ) / 4;
                sub = ( col - gunOverlayCol * 4 ) % 4;
                value = gunAsciiPattern[ gunAsciiIndex( gunOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = gunComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after the same real user-supplied hardware photo (of sibling
// game Cracky) that overturned Cracky's own equivalent function proved
// this port's earlier version was simply wrong too.** The earlier version
// believed upstream's own title-screen text collided with the SCORE/
// STAGE/lives status labels and had to be trimmed/relocated/dropped to
// fit - "CONTINUE" was truncated to "CONTINU", and the credit line was
// shortened to bare "INUFUTO" and moved to the wrong page/column. Re-
// reading upstream's real `Status.cpp` (`Title()`) line by line shows
// this diagnosis was backwards: none of that text ever collides with
// anything upstream, because upstream's own Vram address space is a
// genuinely wide 32-char-cell-per-page canvas (see gunStatusChar's own
// header comment) - the status labels occupy only columns 24-31
// (upstream's own `LeftX=24`), and every piece of title-screen text sits
// at columns 0-23, well clear of them. The ROOT problem was this port's
// own `gunStatusChar` being modeled as an 8-column-wide grid in the first
// place - now fixed there, this function is rewritten to place everything
// at upstream's real, literal columns, with `gunFullWidthText=true` so
// gunComposeRawByte() renders the full canvas instead of just the narrow
// status zone.
//
// **A second architectural bug, found and fixed later in the same
// session, once gameCracky.c's own equivalent function was corrected
// first**: upstream's real bitmap title logo (`TitleBytes[]`, drawn
// directly into VVram via a literal 80-value table) had been "simplified"
// to plain small text here, reasoning it was "purely decorative, non-
// gameplay-relevant" - the same incorrect judgment call gameCracky.c's
// own title screen made and then reverted (see that file's own header
// for the full story). It's not decorative filler - it's the single
// largest, most prominent element on the whole title screen, and the
// small ASCII font used as a substitute is missing several letters (no
// L/H/Y/W/D/K) that a real game title could easily need. **Fixed** by
// drawing `gunTitleBytes[]` directly into `gunVVram` at its own real
// position (VVram rows 2-5, upstream's own `VVram + VVramWidth*2 +
// TitleLeft` starting offset, TitleLeft=2) instead of printing text -
// `gunComposeRawByte()` was updated to OR-combine this VVram content with
// `gunStatusChar`'s own text layer (see that function's own header for
// why this is safe) rather than choosing one exclusively.
void gunBeginTitle()
{
    int i, j;

    for( i = 0; i < GUN_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < GUN_VVRAM_WIDTH; j = j + 1 )
        gunVVram[ i ][ j ] = GUN_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        gunStatusChar[ i ][ j ] = 0;
    gunOverlayActive = false;
    gunFullWidthText = true;
    gunHideAllSprites();
    gunPrintStatus();

    // Upstream's own real "GUNTUS" logo bitmap (`gunTitleBytes[]`, see its
    // own header comment) - 5 stored 4x4-VVram-cell chunks packing the 6
    // real kerned letters G-U-N-T-U-S, drawn at VVram row 2 (upstream's
    // own `VVram + VVramWidth*2 + TitleLeft` offset), column TitleLeft=2
    // (`(VVramWidth - 4*TitleLength)/2 = (24-20)/2 = 2`). Rendered via
    // `gunComposeRawByte()`'s own UNSHIFTED row addressing while
    // `gunFullWidthText` is set (row2/3 -> page1, row4/5 -> page2) -
    // matching upstream's own real Title()-only `VVramToVram(VVram)` call,
    // genuinely different from the shifted addressing every gameplay
    // frame uses (see that function's own header comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                gunVVram[ 2 + row ][ 2 + ch * 4 + col ] = gunTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col TitleLeft+4*TitleLength-5=17,
    // START/CONTINUE at col ArrowX+1=9 with the cursor at col ArrowX=8,
    // the credit line at col 12) - all genuinely clear of the status
    // labels' own columns 24-31, so nothing here needs trimming,
    // relocating, or dropping anymore.
    {
        int sMini[4] = { 'M', 'I', 'N', 'I' };
        gunPrintS( 3, 17, sMini, 4 );
    }
    {
        int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        gunPrintS( 7, 12, sCredit, 12 );
    }
    {
        int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        gunPrintS( 5, 9, sStart, 5 );
    }
    {
        int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        gunPrintS( 6, 9, sContinue, 8 );
    }

    gunSelection = 0;
    gunSelectionChanged = true;
    gunPrevLeft = 0; gunPrevRight = 0; gunPrevUp = 0; gunPrevDown = 0; gunPrevFire = 0;
    gunState = GUN_STATE_TITLE;
}

void gunUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !gunPrevLeft ) || ( right && !gunPrevRight ) ||
                ( up && !gunPrevUp ) || ( down && !gunPrevDown ) );
    justFire = ( fire && !gunPrevFire );
    gunPrevLeft = left; gunPrevRight = right; gunPrevUp = up; gunPrevDown = down; gunPrevFire = fire;

    if( gunSelectionChanged )
    {
        gunSelectionChanged = false;
        if( gunSelection == 0 )
          gunPrintC( 5, 8, '>' );
        else
          gunPrintC( 5, 8, ' ' );
        if( gunSelection == 1 )
          gunPrintC( 6, 8, '>' );
        else
          gunPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        gunFullWidthText = false;
        gunEnemyCount = 0;
        gunScore = 0;
        if( gunSelection == 0 )
          gunCurrentStage = 0;
        gunRemainCount = 3;
        gunInitPlaying();
        gunDrawAll();
        gunStartSeq( 1, GUN_MELODY_START );
        gunState = GUN_STATE_START_JINGLE;
        gunRender();
        return;
    }
    if( justDir )
    {
        gunSelection = gunSelection ^ 1;
        gunSelectionChanged = true;
    }
    gunRender();
}

void gunUpdateStartJingle()
{
    if( !gunSeqPlaying( 1 ) )
    {
        gunStartBgm();
        gunClock = 0;
        gunClearTime = 150 * GUN_COORD_RATE / 8;
        gunInitStage();
        gunTickCounter = 0;
        gunState = GUN_STATE_PLAYING;
    }
    gunRender();
}

void gunUpdatePlaying()
{
    gunTickCounter = gunTickCounter + 1;
    if( gunTickCounter < GUN_TICK_DIVISOR )
    {
        gunRender();
        return;
    }
    gunTickCounter = 0;

    if( ( gunClock & 0x01 ) == 0 )
    {
        gunMoveFighter();
        gunMoveEnemyBullets();
        gunMoveTeams();
        gunMoveMovingEnemies();
    }
    if( ( gunClock & 0x03 ) == 0 )
    {
        gunMoveItem();
        gunMoveStars();
    }
    if( ( gunClock & 0x0f ) == 0 )
    {
        gunStartTeam();
        gunMoveEnemyRows();
    }
    gunUpdateBangs();
    gunDrawAll();
    gunMoveFighterBullets();

    if( gunRemainCount == 0 )
    {
        gunStopBgm();
        gunPrintGameOver();
        gunStartSeq( 1, GUN_MELODY_GAMEOVER );
        gunState = GUN_STATE_GAMEOVER_JINGLE;
        gunRender();
        return;
    }
    if( gunEnemyCount == 0 )
    {
        if( gunClearTime == 0 )
        {
            gunCurrentStage = gunCurrentStage + 1;
            gunPrintStatus();
            gunClearTime = 150 * GUN_COORD_RATE / 8;
            gunInitStage();
        }
        else
          gunClearTime = gunClearTime - 1;
    }
    gunClock = gunClock + 1;
    gunRender();
}

void gunUpdateGameOverJingle()
{
    if( !gunSeqPlaying( 1 ) )
      gunBeginTitle();
    else
      gunRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameGuntus_init()
{
    int i;

    gunScore = 0;
    gunCurrentStage = 0;
    gunRemainCount = 3;
    gunRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        gunSeqActive[ i ] = 0;
        gunSeqMelody[ i ] = GUN_MELODY_NONE;
    }
    gunOverlayActive = false;
    gunTickCounter = 0;

    gunBeginTitle();
}

void gameGuntus_update()
{
    gunAdvanceSound();

    if( gunState == GUN_STATE_TITLE )
      gunUpdateTitle();
    else if( gunState == GUN_STATE_START_JINGLE )
      gunUpdateStartJingle();
    else if( gunState == GUN_STATE_PLAYING )
      gunUpdatePlaying();
    else if( gunState == GUN_STATE_GAMEOVER_JINGLE )
      gunUpdateGameOverJingle();
}
