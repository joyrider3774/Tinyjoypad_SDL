// =============================================================================
// RUPTUS mini (inufuto, UIAPduino+SSD1306/CH32V003 edition, license "None
// specified" - GitHub reports no LICENSE file for `UIAPduino_ruptus`) - a
// Xevious/Cabal-flavored top-down shooter: an 8-directionally-rotating
// fighter continuously flies over a scrolling 48x64-cell world, shooting
// down enemy forts (each takes several hits) and dodging/destroying chasing
// enemies and their bullets. Destroy every fort on a stage to advance;
// barriers (destructible walls) block direct paths - shoot either end of a
// barrier to make it vanish. 10 hand-authored stages, 3 lives, real-time
// score, no time limit (unlike this project's own gameCracky.c sibling
// port). Session-only score tracking (upstream has no EEPROM at all - a
// CH32V003 RISC-V microcontroller, not AVR).
//
// Ported directly following gameCracky.c's own already-proven methodology
// (same author/hardware/driver lineage) rather than re-deriving one from
// scratch - see that file's own header comment for the shared reasoning on
// display orientation, the VVram two-level tile system, and the sound
// sequencer shape. This is the largest, most structurally involved port
// from this specific driver lineage so far: an 8-way rotating ship (vs.
// Cracky's 4-way platformer man), 4 concurrent bullet/enemy pools instead
// of Cracky's single monster list, a real bitmap minimap ("Rader"), and a
// genuinely pervasive reliance on real AVR-style `uint8_t`/`byte` wrap-
// around arithmetic for camera-relative coordinate math - see the
// "Byte-wraparound arithmetic" section of this comment for the single
// biggest correctness risk in this whole port and how it was handled.
//
// **No hardware display-orientation transform needed, confirmed via the
// same real `InitOled()` register dump already found safe for Cracky**:
// `RightToLeft`/`BottomToTop` are sent here too, and per Cracky's own
// (user-verified) finding, these compensate for a physical panel-mount
// quirk on real hardware with no equivalent to correct for in software -
// the composed byte is drawn directly at its own (col,page), no mirror,
// no bit-reversal.
//
// **Rendering is three genuinely separate raw-byte regions sharing one
// 128-column x 8-page screen, derived by tracing the real upstream Vram
// addresses each subsystem writes to (not guessed):**
//   - **Map area, columns 0-103 (`VVramWidth`(26)*4)**: the same two-level
//     VVram-glyph-grid + `CharPattern` nibble-composite system as Cracky's
//     own `crkComposeRawByte()` - reproduced identically as
//     `rupComposeMapByte()`. **One genuine, deliberately-preserved upstream
//     quirk**: `VVramToVram()`'s own loop bound is
//     `VVramHeight/2 - 1 = 7`, not 8 - meaning real hardware only ever
//     streams the TOP 7 of 8 possible hardware pages from VVram; the
//     bottom 8px band of the play area (page 7, world rows 14-15 of the
//     16-row-tall viewport) is **never** drawn from the map data at all,
//     permanently black, on every stage, at every camera position (this
//     is not a rare edge case - with `StageHeight`(64) far taller than the
//     16-row viewport, the camera scrolls through this row range
//     constantly during normal play). Preserved faithfully rather than
//     "fixed" (matching this whole project's own "preserve odd-but-real
//     upstream behavior" precedent, and Cracky's own analogous choice for
//     its `Man.x < pMonster->y` comparison quirk) - `rupRender()` simply
//     never composes page 7 for columns < 104, leaving it whatever
//     `md_beginFrame()`'s own clear already left there (black), which is
//     the exact same real-hardware outcome with no extra code needed.
//   - **Status area, columns 104-127, pages 0-3**: `Status.cpp`'s own real
//     Vram addresses (`LeftX=26` chars = column 104, matching where the
//     map area ends exactly) - Score (page 0), Stage number (page 1),
//     Remain/lives (page 3, simplified from upstream's own 2x2 `Put2C`
//     icon to a plain digit, matching Cracky's own established
//     simplification for the identical situation). Page 2 is never
//     written by anything and stays black, matching upstream exactly.
//   - **Rader (minimap) area, columns 104-127, pages 4-7**: a genuinely
//     new subsystem for this driver lineage, not present in Cracky at
//     all - `Rader[]` stores real raw SSD1306 page-bytes directly (no
//     glyph lookup), composited via bit-level `&=`/`|=`/`^=` operations
//     (`DrawFortRader`/`DrawFighterOnRader`) rather than the tile system.
//     Ported as a literal `int[4][24]` byte grid (`rupRader`), streamed
//     straight to `md_drawColumn()` with no CharPattern/AsciiPattern
//     lookup needed at all, since each stored value already IS the final
//     hardware byte.
//
// **The Title() screen repurposes the WHOLE map-area pixel space (columns
// 0-103) as its own custom display** (its own real Vram addresses for
// START/CONTINUE/MINI/INUFUTO 2026 all land well inside the map column
// range, not the status column range) - since there's no game scene to
// show behind it, `rupRender()` OR-combines a `rupComposeMapByte()` pass
// (the real logo bitmap, see below - drawn directly into `rupVVram`) with
// a dedicated `rupComposeTitleByte()` pass (backed by its own
// `rupTitleChar[8][26]` grid, for MINI/START/CONTINUE/the credit line)
// for the whole map region, only while `rupState==RUP_STATE_TITLE`,
// leaving the status/rader areas rendered normally underneath (Title()
// really does call `PrintStatus()`/`ClearRader()` on real hardware too,
// so the score/stage/remain and an empty radar genuinely show on the
// title screen there as well).
//
// **A real bug, found and fixed the same way gameCracky.c's own sibling
// bug was**: upstream's own 80-byte hand-drawn "RUPTUS" pixel logo
// (`TitleBytes[]`, drawn straight into VVram via a `CharPattern`-glyph
// grid, not real text) was originally simplified to plain text "RUPTUS"
// here too, reasoning (incorrectly, per gameCracky.c's own later,
// user-photo-driven correction - see that file's own header comment and
// CLAUDE.md's "A real user-supplied hardware photo overturns Cracky's
// own title-screen design" section) that it was "purely decorative,
// non-gameplay-relevant." It isn't - it's the single largest, most
// prominent element on the whole title screen. **Restored**: the real
// `rupTitleBytes[]` table (byte-diff-verified against upstream via
// script) is now drawn directly into `rupVVram` by `rupBeginTitle()`, at
// upstream's own real position (`TitleLength=5`, `TitleLeft =
// (VVramWidth-4*TitleLength)/2 = 3`, VVram rows 2-5 -> real hardware
// pages 1-2 via `VVramToVram()`'s own row-pair packing), and
// `rupComposeMapByte()` composes it during the title screen the same way
// it already composes ordinary map tiles during gameplay - see
// `rupBeginTitle()`'s own comment for the full derivation. MINI and the
// credit line, previously deliberately repositioned (col10/col7) to suit
// the plain-text substitute's different shape, are now placed at
// upstream's own real, literal columns (18 and 12 respectively) since the
// real logo restores the shape those columns were originally tuned
// against - see `rupBeginTitle()`'s own comment for the exact derivation.
// START/CONTINUE (col `ArrowX+1`=9) were already correct and are
// unchanged.
//
// **Re-audited against gameCracky.c's own later, more serious title-screen
// grid-WIDTH bug fix** (a real user-supplied hardware photo proved
// Cracky's original `crkStatusChar[8][8]` was far too narrow, forcing
// "CONTINUE" to overflow and forcing MINI/the credit line to collide with
// the SCORE/STAGE/TIME status labels - see gameCracky.c's own header
// comment and CLAUDE.md's "A real user-supplied hardware photo overturns
// Cracky's own title-screen design" section for the full story). **This
// file does not share THAT bug and needed no equivalent structural
// fix** (a separate issue from the logo simplification above): unlike
// Cracky's design (one shared narrow grid, toggled between "status" and
// "full width" meaning via a flag), this port was written from the start
// with two entirely separate arrays - `rupStatusChar[4][6]` (bounds-
// checked to upstream's real status-only range, `LeftX=26` through 31)
// and `rupTitleChar[8][26]` (the *whole* real map-column range, 0-25) -
// so title text and status text can never physically collide or need a
// render-mode flag to disambiguate; see this comment's own "out-of-
// bounds-write risk... deliberately NOT repeated here" paragraph above,
// which already explains this was a deliberate design choice made with
// full knowledge of Cracky's narrower original mistake, not a later
// retrofit. Re-verified directly against real upstream columns (traced
// through `Status.cpp`'s `Title()`/`PrintStatus()` and `Vram.h`'s
// `VramStep=4`/`VramRowSize=0x100` constants) and via a fresh isolated
// Puppeteer/WebGL screenshot, before the logo restoration above: MINI/
// `>START`/CONTINUE (all 8 letters, not truncated)/INUFUTO 2026 all
// render fully, at non-overlapping columns, with the SCORE("00")/
// STAGE("I", the font's own correct thin-glyph rendering of digit '1')/
// REMAIN("R2") status text simultaneously visible and undisturbed on the
// right - and the "00"/"I" status rendering was independently cross-
// checked against this repo's own real-hardware photo (`rec.jpg`,
// in-gameplay rather than title, but showing the identical SCORE/STAGE
// glyph shapes at the same screen position), confirming this file's
// status-text placement already matches real hardware exactly.
//
// **A real, if minor, out-of-bounds-write risk found in Cracky's own
// title-screen text and deliberately NOT repeated here**: tracing
// `crkPrintS(6,1,sContinue,8)` (Cracky's own "CONTINUE" call) against its
// `crkStatusChar[8][8]` grid shows the 8th character of an 8-letter word
// starting at column 1 lands at column index 8 - one past that grid's own
// valid 0-7 range. Whether or not this is currently harmless on Cracky's
// own shipped build, this port's own title text also needs to show
// "CONTINUE" - rather than risk repeating an analogous overflow, `rupTitleChar`
// is sized `[8][26]` (a full map-width's worth of char-cells, comfortably
// oversized for every string this port ever writes into it - the longest,
// "INUFUTO 2026", is 12 characters) instead of a tightly-sized 8-wide grid,
// so no title-screen string can ever overflow it regardless of its own
// start column.
//
// **The `Sound.cpp` tone-sequencer system is structurally identical to
// Cracky's own** (3 `ToneChannel`s, the same `Tempo`-driven
// `(600/2)/Tempo` real-tick-cadence formula, the same `NoteLength`/`Scale`
// enum values, the same 40-entry `Frequencies` table byte-for-byte) - this
// game's own `Tempo` is 180, not Cracky's 160, giving
// `rupNoteFrames(length) = round(length * (300.0/180.0))` instead. Ported
// the same way: every `Melody()`/`WaitMelody()` call becomes a
// `rupStartSeq(channel, melodyId)` call into a shared 3-slot frame-stepped
// sequencer (0=one-shot SFX like Fire/Up, 1=jingle/BGM-voice-A, 2=BGM-
// voice-B), each melody resolved by id via `rupMelodyLength()`/
// `rupMelodyValue()` rather than a real pointer, matching Cracky's own
// "resolve by id" precedent. **A genuine EXTRA voice this game has that
// Cracky's didn't**: a real hardware noise/`EffectChannel` used for
// `Sound_SmallBang()`/`Sound_LargeBang()` (a decaying-volume noise burst,
// not a melody) - approximated as one direct `md_playTone(freqHz,
// durationSeconds)` call per bang (3000Hz small / 1500Hz large), with the
// duration derived from the real decay math (`MaxVolume`(63) decrementing
// by 2 every channel-advance tick until 0 = 32 steps, each
// `300/180=1.667` real ticks apart, at 60Hz = `32*1.6667/60 ~= 0.889`
// real seconds) rather than reproducing a true decaying noise waveform
// this engine has no equivalent primitive for - the same "approximate a
// hardware feature with no direct portable equivalent" precedent already
// used elsewhere in this project (UFO's own thruster-hum approximation).
// **Two genuine upstream bugs found and fixed, not reproduced**:
// `Sound_Clear()`'s and `Sound_GameOver()`'s own `static const uint8_t
// notes[]` arrays have NO trailing terminator byte at all (every other
// melody in the file ends with an explicit `0`) - `ToneChannel::Next()`
// would read one element past the end of these two specific arrays once
// the melody finishes playing, a genuine out-of-bounds flash read on real
// hardware (harmless-looking there only because it silently reads
// whatever adjacent byte happens to sit in flash, not a documented
// terminator). Fixed by appending an explicit `0` to both melody tables
// in this port (`rupMelodyClear`/`rupMelodyGameOver`) rather than
// reproducing undefined behavior with no well-defined "faithful" result
// to copy in the first place.
//
// **Byte-wraparound arithmetic - the single biggest correctness risk in
// this whole port, handled far more pervasively than in any prior game in
// this project's own "AVR-implicit-narrow-type" bug family.** Nearly
// every coordinate in this game (`FighterX/Y`, `BaseX/Y`, every bullet/
// enemy/fort/barrier x/y) is a real `byte`(uint8_t)/`sbyte`(int8_t) field
// upstream, and several core algorithms **deliberately, load-bearingly
// rely on real 8-bit unsigned wraparound** to work at all - not just an
// incidental risk to guard against, the way most of this project's other
// byte-truncation fixes have been:
//   - `HitMover()`'s own collision test computes `xDiff`/`yDiff` as
//     `byte` values from a signed difference-plus-offset expression - the
//     classic "unsigned distance trick" (a genuinely far-apart pair wraps
//     to a huge byte value, safely failing the `< range` check, without
//     needing a separate `abs()`/sign check). Ported with an explicit
//     `rupWrapByte()` mask at each `xDiff`/`yDiff` computation - without
//     it, collision detection would silently misfire for any pair of
//     objects whose true difference happens to be negative before adding
//     the size offset (extremely common - this is called for essentially
//     every fighter/bullet/enemy/fort/barrier/item collision check in the
//     game).
//   - `AddX()`/`AddY()` (world-wraparound position stepping) and
//     `OffsetX()`/`OffsetY()` (camera-relative screen positioning) both
//     rely on a genuine double-wrap: the raw byte addition/subtraction
//     wraps through 0-255 first, and *only then* gets re-wrapped into the
//     real game's own `[0,StageWidth)`/`[0,StageHeight)` range if needed. A
//     first draft of this port tried a "simplified" `AddX`/`AddY` that
//     skipped the intermediate byte-wrap (reasoning it'd be behaviorally
//     equivalent for the usual `dx` in `{-1,0,1}` case) - this is
//     correct for THAT case, but `StartEnemy()` also calls
//     `AddX(BaseX, VVramWidth)`/`AddY(BaseY, VVramHeight)` - passing the
//     *camera's own* possibly-"negative" (wrapped-byte) `BaseX`/`BaseY`
//     value as the base, with a much larger `dx`(26)/`dy`(16) - a case
//     where skipping the intermediate wrap gives a genuinely wrong
//     answer (traced by hand: `AddX(243/*=-13*/, 26)` should give `13`,
//     but the simplified version gives `221`). Caught before ever
//     shipping by tracing this exact call site, not by a report - fixed
//     by properly replicating the real double-wrap via an explicit
//     `rupWrapByte()` at each step, matching upstream's literal
//     arithmetic instead of a "simplified but wrong for this one call
//     site" rewrite.
//   - `UpdateBasePosition()`'s own `BaseX = FighterX - VVramWidth/2` is
//     **itself** a deliberate wraparound: when the fighter is near the
//     world's left/top edge, `BaseX`/`BaseY` become large "wrapped-
//     negative" byte values on purpose - `OffsetX()`/`OffsetY()`'s own
//     `if (BaseX >= StageWidth) ...` branch specifically detects this
//     wrapped state to correctly offset on-screen positions near a wrap
//     seam. Ported with an explicit `rupWrapByte()` on the `BaseX`/`BaseY`
//     assignment itself, matching upstream's real semantic exactly rather
//     than trying to represent "camera can be conceptually negative"
//     any other way.
//   - Several `Locate()`-style sprite-visibility functions (Enemy, Item,
//     Bang) compute `x -= HalfSize` on an already-bounds-checked `byte`
//     local, relying on a value like `x=0` wrapping to `255` so a
//     SECOND `if (x >= VVramWidth) ...` bounds check correctly catches
//     "just off the left/top edge" as off-screen. Without the wrap, this
//     port's own plain (non-wrapping) `int` locals would instead compute
//     a genuinely negative `x`, which the same `>=` check would NOT catch
//     - it would instead attempt to write `rupVVram[y][-1]`, a real out-
//     of-bounds array access (this project's own well-documented "ERROR:
//     INVALID MEMORY READ/WRITE" crash class), not just a wrong-looking
//     frame. Fixed the same way, with `rupWrapByte()` at each such site.
//   - `DrawForts()`'s own `x`/`y` locals are declared `sbyte` (SIGNED,
//     unlike every other Locate()-style function in this file, which
//     stays in unsigned `byte` space) - `VPut6CXY()`'s own bound checks
//     (`if (x < -6) return;`) are written expecting a genuinely negative
//     value for a fort mostly off-screen to the left/top, not a large
//     unsigned byte. Ported with a distinct `rupWrapSByte()` helper
//     (masks to 8 bits, then re-interprets values >127 as negative,
//     matching real `int8_t` reinterpretation) applied only at this one
//     call site - traced by hand that using the WRONG helper here
//     (`rupWrapByte`, unsigned) would silently suppress the correct
//     partial-edge-visibility drawing for a fort straddling the visible
//     area's left/top edge (an `xxx>=VVramWidth` early-return would
//     trigger for every case that should have shown 1-5 partially-visible
//     columns/rows instead).
//   - `SubX()`/`SubY()` (used only for `Enemy.cpp`'s own fighter-relative
//     targeting) and `HitBarrier()`'s own `NearBarrier1()`/`Hit()`
//     coordinate comparisons were checked and confirmed to NOT need any
//     wraparound handling - both only ever operate on already-valid,
//     always-non-negative stage coordinates (never a wrapped `BaseX`-
//     style camera value), so a plain `int` port is already correct
//     there. Not every byte-typed value in this file needed the same
//     treatment - each site was individually traced rather than masking
//     everything uniformly out of caution alone.
//
// **Circular module dependencies resolved via restructuring, not forward
// declarations** (this dialect supports forward declarations per this
// project's own `VIRCON32_C_DIALECT.md` section 11 and prior precedent in
// `gameTinyPipe.c`, but the task's own instructions asked for a
// conservative, declare-before-use-only file, matching the more cautious
// option already used elsewhere in this project). Upstream's real
// `Fighter.cpp <-> FighterBullet.cpp <-> Enemy.cpp <-> EnemyBullet.cpp`
// call graph is a genuine 4-way cycle in C++ too (resolved there only via
// header-declared `extern` prototypes) - traced the exact edges and found
// exactly one needed a structural change to become a clean top-to-bottom
// order: upstream's `Fighter::ControlFighter()` calls
// `FighterBullet::StartFighterBullet()` directly, but nothing in
// `FighterBullet`/`Enemy`/`EnemyBullet` ever calls back into
// `ControlFighter` itself - so that one call was moved OUT of
// `rupControlFighter()` into the shared per-tick driver function
// (`rupUpdatePlaying()`), which now calls `rupControlFighter()` then
// separately calls `rupFighterBulletTick(...)` right after, re-checking
// the exact same `rupFighterDyingCount < 0` guard `ControlFighter()`'s
// own early-return already implied - reproducing the identical per-tick
// order/frequency (both still run exactly once per tick, in the same
// relative order, gated by the same condition) without Fighter's own
// code needing to call into FighterBullet's. With that one edge removed,
// the remaining chain (Fighter -> EnemyBullet -> Enemy -> FighterBullet)
// has a genuine acyclic order: `EnemyBullet` only needs Fighter's already-
// defined `rupHitFighter()` plus the `RupEnemy` struct TYPE (not its
// functions); `Enemy` needs Fighter's `rupHitFighter()` and EnemyBullet's
// `rupStartEnemyBullet()` (both already defined by that point);
// `FighterBullet` needs Enemy's `rupHitEnemy()` plus Fort/Barrier's own
// (independently early-defined) hit functions.
//
// **The per-tick pacing model needed no throttle divisor at all - a
// genuine coincidence in the real math, not a design choice.** Unlike
// every other Daniel-C/Sven-B-lineage game in this project (which all
// needed an explicit `_TICK_DIVISOR` to match a real or synthetic target
// rate), this game's own `Main()` do-while loop only calls the real
// `WaitTimer(2)` blocking wait on every *other* iteration
// (`if ((Clock&1)==0)`) - the alternate iterations run with zero
// synchronization delay at all. Averaged over both kinds of iteration,
// that's `(0 + 2 ticks) / 2 iterations = 1 tick/iteration` at the real
// 60Hz `kTimerHz`, i.e. upstream's own real average iteration rate is
// already ~60Hz - matching this engine's own native 60fps almost exactly
// (computed precisely: 16.5ms/iteration average vs. this engine's
// 16.67ms/frame, under 1% off). So `rupClock` simply increments once per
// real engine frame with no gating divisor, and every one of upstream's
// own internal `Clock&N` gates (`&3` for `MoveFighter`/`MoveEnemy`/
// `StartEnemy`, `&7` inside `MoveFighter`/`MoveEnemy`, `&1` inside the
// bullet-movement functions, `&0x3f` inside `StartEnemy`) is preserved
// completely unchanged, reproducing upstream's real relative cadences
// automatically.
//
// **No explicit "lose animation" state exists, unlike Cracky's own 8-step
// `LooseMan()` blink loop** - upstream's real death sequence is just
// `FighterDyingCount` counting down from 30 while every other system
// (enemies, bullets, the fighter's own already-triggered `StartBang()`
// explosion sprite) keeps running completely normally each tick; only
// `MoveFighter()`'s own early-decrement-and-return and `ControlFighter()`'s
// own early-return suppress player control during this window. Ported by
// simply NOT introducing a separate state at all - `rupUpdatePlaying()`
// keeps running its normal per-tick body throughout the death countdown,
// checking `rupFighterDyingCount == 0` every tick (matching upstream's
// own do-while check) to detect the exact tick the countdown finishes.
//
// **No time limit exists at all** (unlike Cracky) - stage-clear is purely
// "every fort destroyed", checked once per tick via `rupFortCount <= 0`,
// matching upstream's own `while (FortCount > 0)` do-while condition
// exactly - so this port needs only 5 states total (TITLE, START_JINGLE,
// PLAYING, GAMEOVER_JINGLE, CLEAR_JINGLE), fewer than Cracky's 8.
//
// Data tables (`AsciiPattern`, `CharPattern`, all 6 sound-effect melodies,
// the 2 BGM voice tables, all 10 stages' fort/barrier/start-position data)
// were byte-diff-extracted via a small Python script directly against the
// real upstream source, not hand-transcribed, matching this project's own
// established anti-transcription-bug discipline (confirmed element counts
// against each table's own expected size before ever pasting anything in
// - e.g. `CharPattern` against `Char_End`(0x95=149)*2=298 bytes).
//
// Ported mechanically from the sibling tinyjoypad_vircon32 project's own
// already-correct, already-verified Vircon32-dialect gameRuptus.c - only
// `int[N] name` -> `int name[N]` (and the 2D/3D forms) array-declaration
// syntax and the 6 bare `struct` tags -> `typedef struct` needed changing.
// Every string-shaped array in this file (rupOverlayText, the various
// `sMini`/`sStart`/`sContinue`/`sCredit`/`sScore`-equivalent/`table[]`
// local buffers) holds individual `'A'`-style char constants, never a
// `"..."` string literal, matching gameCracky.c's own identical finding -
// so none of them needed `int[]`->`char[]` widening; every value still
// round-trips through rupAsciiIndex()'s own int-comparison lookup exactly
// as upstream wrote it. No EEPROM/high-score persistence here, matching
// upstream having none at all (a CH32V003 RISC-V board, not AVR). No
// forceRedraw needed either: every one of this state machine's own
// branches calls rupRender() unconditionally on every real tick it runs
// (including rupUpdatePlaying()'s own "off tick" - md_beginFrame() is
// simply skipped via rupDrawAll()/rupRender() being gated on `(rupClock&1)==0`
// together, exactly matching upstream's own DrawAll()+WaitTimer() gating -
// so there's no "last frame's pixels are still correct, skip redrawing"
// case any *other* code path would need a forceRedraw hook to recover
// from on menu resume).
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define RUP_CHAR_SPACE 0x00
#define RUP_CHAR_FIGHTERBULLET 0x10
#define RUP_CHAR_ENEMYBULLET 0x14
#define RUP_CHAR_BARRIER 0x15
#define RUP_CHAR_BARRIERHEAD 0x17
#define RUP_CHAR_STAR 0x18
#define RUP_CHAR_FIGHTER 0x19
#define RUP_CHAR_ENEMY 0x39
#define RUP_CHAR_SMALLBANG 0x59
#define RUP_CHAR_LARGEBANG 0x5D
#define RUP_CHAR_ITEM 0x6D
#define RUP_CHAR_FORT 0x71
#define RUP_CHAR_END 0x95

// -----------------------------------------------------------------------------
//   Sprite.h - reserved slot ranges within the shared rupSprites[] array.
// -----------------------------------------------------------------------------

#define RUP_SPRITE_FIGHTERBULLET 0
#define RUP_SPRITE_FIGHTER 4
#define RUP_SPRITE_ITEM 5
#define RUP_SPRITE_ENEMY 6
#define RUP_SPRITE_ENEMYBULLET 15
#define RUP_SPRITE_BANG 19
#define RUP_SPRITE_COUNT 27

#define RUP_SPRITE_HIDDEN 0xff
#define RUP_OBJ_NONE 0x80

// -----------------------------------------------------------------------------
//   Direction.h
// -----------------------------------------------------------------------------

#define RUP_DIR_UP 0
#define RUP_DIR_UPRIGHT 1
#define RUP_DIR_RIGHT 2
#define RUP_DIR_DOWNRIGHT 3
#define RUP_DIR_DOWN 4
#define RUP_DIR_DOWNLEFT 5
#define RUP_DIR_LEFT 6
#define RUP_DIR_UPLEFT 7

int rupDirectionsDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int rupDirectionsDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
int rupBulletOffsetsDx[8] = { -1, 0, 0, 0, -1, -1, -1, -1 };
int rupBulletOffsetsDy[8] = { -1, -1, -1, 0, 0, 0, -1, -1 };

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define RUP_STAGE_WIDTH 48
#define RUP_STAGE_HEIGHT 64
#define RUP_VVRAM_WIDTH 26
#define RUP_VVRAM_HEIGHT 16
#define RUP_STAGE_COUNT 10
#define RUP_MAX_FORT_COUNT 7
#define RUP_MAX_BARRIER_COUNT 8

// -----------------------------------------------------------------------------
//   Fort.h / Barrier.h / Bang.h flag constants
// -----------------------------------------------------------------------------

#define RUP_FORT_SIZE 6
#define RUP_FORT_HALFSIZE ( RUP_FORT_SIZE / 2 )
#define RUP_FORT_MAX_LIFE 4
#define RUP_FORT_FLAG_LIVE 0x80
#define RUP_FORT_FLAG_VISIBLE 0x10

#define RUP_BARRIER_FLAG_VERTICAL 0x01
#define RUP_BARRIER_FLAG_START_OVERWRAPPED 0x02
#define RUP_BARRIER_FLAG_END_OVERWRAPPED 0x04
#define RUP_BARRIER_FLAG_START_VISIBLE 0x10
#define RUP_BARRIER_FLAG_END_VISIBLE 0x20
#define RUP_BARRIER_FLAG_LINE_VISIBLE 0x40
#define RUP_BARRIER_FLAG_LIVE 0x80

#define RUP_BANG_MAX 8
#define RUP_BANG_HALFSIZE 1
#define RUP_BANG_STATUS_NONE 0x00
#define RUP_BANG_STATUS_SMALL 0x10
#define RUP_BANG_STATUS_LARGE_SMALL 0x20
#define RUP_BANG_STATUS_LARGE_LARGE 0x30
#define RUP_BANG_STATUS_MASK 0xf0
#define RUP_BANG_COUNT_MASK 0x0f

#define RUP_FBULLET_MAX 4
#define RUP_FBULLET_HIVEL 100
#define RUP_FBULLET_LOVEL ( RUP_FBULLET_HIVEL * 70 / 100 )
#define RUP_FBULLET_INTERVAL 20

#define RUP_ENEMY_MAX 3
#define RUP_ENEMY_HIVEL 50
#define RUP_ENEMY_LOVEL ( RUP_ENEMY_HIVEL * 70 / 100 )

#define RUP_EBULLET_MAX 4
#define RUP_EBULLET_HIVEL 28
#define RUP_EBULLET_LOVEL ( RUP_EBULLET_HIVEL * 70 / 100 )
#define RUP_EBULLET_LONGVEL ( RUP_EBULLET_HIVEL * 92 / 100 )
#define RUP_EBULLET_SHORTVEL ( RUP_EBULLET_HIVEL * 38 / 100 )

#define RUP_ITEM_HALFSIZE 1
#define RUP_FIGHTER_HALFSIZE 1

#define RUP_FIXED_X ( RUP_VVRAM_WIDTH / 2 * 8 - 8 )
#define RUP_FIXED_Y ( RUP_VVRAM_HEIGHT / 2 * 8 - 8 )

#define RUP_RADER_WIDTH ( RUP_STAGE_WIDTH / 2 )
#define RUP_RADER_HEIGHT ( RUP_STAGE_HEIGHT / 2 / 8 )

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching upstream.
// -----------------------------------------------------------------------------

#define RUP_N8 6
#define RUP_N8L 8
#define RUP_N8R 4
#define RUP_N8P ( RUP_N8 * 3 / 2 )
#define RUP_N4 ( RUP_N8 * 2 )
#define RUP_N4P ( RUP_N4 * 3 / 2 )
#define RUP_N2 ( RUP_N4 * 2 )
#define RUP_N2P ( RUP_N2 * 3 / 2 )
#define RUP_N1 ( RUP_N2 * 2 )
#define RUP_N16 ( RUP_N8 / 2 )

#define RUP_TEMPO 180

#define RUP_MELODY_NONE 0
#define RUP_MELODY_FIRE 1
#define RUP_MELODY_UP 2
#define RUP_MELODY_START 3
#define RUP_MELODY_CLEAR 4
#define RUP_MELODY_GAMEOVER 5
#define RUP_MELODY_BGM1 6
#define RUP_MELODY_BGM2 7

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via script, not hand-transcribed.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph. Byte-for-byte
// identical to gameCracky.c's own crkAsciiPattern (same upstream author,
// same table) - re-extracted independently anyway rather than assumed.
int rupAsciiPattern[108] = {
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x11, 0x1f, 0x00, 0x00, 0x00,
    0x1f, 0x00, 0x1d, 0x15, 0x17, 0x00, 0x15, 0x15, 0x1f, 0x00,
    0x07, 0x04, 0x1f, 0x00, 0x17, 0x15, 0x1d, 0x00, 0x1f, 0x15,
    0x1d, 0x00, 0x01, 0x1d, 0x03, 0x00, 0x1f, 0x15, 0x1f, 0x00,
    0x17, 0x15, 0x1f, 0x00, 0x1f, 0x0e, 0x04, 0x00, 0x1e, 0x09,
    0x1e, 0x00, 0x0e, 0x11, 0x0a, 0x00, 0x1f, 0x15, 0x11, 0x00,
    0x1f, 0x05, 0x01, 0x00, 0x0e, 0x11, 0x0d, 0x00, 0x11, 0x1f,
    0x11, 0x00, 0x1f, 0x06, 0x1f, 0x00, 0x1f, 0x01, 0x1e, 0x00,
    0x0e, 0x11, 0x0e, 0x00, 0x1f, 0x05, 0x07, 0x00, 0x1f, 0x05,
    0x1a, 0x00, 0x16, 0x15, 0x0d, 0x00, 0x01, 0x1f, 0x01, 0x00,
    0x1f, 0x10, 0x1f, 0x00, 0x0f, 0x10, 0x0f, 0x00,
};

// CharPattern - 149 map-tile glyphs (Char_End=0x95), 2 bytes/glyph.
int rupCharPattern[298] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00, 0x00, 0x33,
    0x33, 0x33, 0xcc, 0x33, 0xff, 0x33, 0x00, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0xff, 0xcc, 0x00, 0xff, 0x33, 0xff, 0xcc, 0xff,
    0xff, 0xff, 0x00, 0xf0, 0x40, 0x12, 0x88, 0x88, 0x21, 0x04,
    0xf6, 0x6f, 0x42, 0x42, 0xa0, 0x05, 0x9f, 0xf9, 0x20, 0x00,
    0x80, 0xfe, 0x8e, 0x00, 0x31, 0x71, 0x31, 0x01, 0xcc, 0xec,
    0xee, 0x01, 0x20, 0x71, 0x07, 0x00, 0xa8, 0xef, 0xcc, 0x08,
    0x20, 0x37, 0x11, 0x00, 0xa8, 0xfc, 0x8f, 0x00, 0x11, 0x31,
    0x33, 0x04, 0xe4, 0xfc, 0xec, 0x04, 0x00, 0x73, 0x03, 0x00,
    0x80, 0xff, 0xac, 0x08, 0x34, 0x33, 0x11, 0x01, 0xc8, 0xec,
    0xaf, 0x08, 0x10, 0x31, 0x27, 0x00, 0xe1, 0xee, 0xcc, 0x0c,
    0x00, 0x77, 0x21, 0x00, 0xc8, 0xfa, 0xca, 0x08, 0x13, 0x71,
    0x11, 0x03, 0x24, 0xf7, 0xbd, 0x0f, 0x00, 0x01, 0x35, 0x01,
    0x98, 0xff, 0xca, 0x08, 0x40, 0x77, 0x12, 0x00, 0x00, 0x84,
    0xed, 0x0c, 0x21, 0x77, 0x65, 0x07, 0xce, 0xfc, 0xcc, 0x0e,
    0x10, 0x72, 0x12, 0x00, 0xec, 0x8d, 0x04, 0x00, 0x67, 0x75,
    0x27, 0x01, 0xc8, 0xfa, 0x9f, 0x08, 0x10, 0x72, 0x47, 0x00,
    0xbf, 0xfd, 0x27, 0x04, 0x31, 0x05, 0x01, 0x00, 0xe4, 0xb6,
    0x4a, 0x4e, 0x72, 0xe2, 0x25, 0x17, 0x00, 0xc2, 0x84, 0x7c,
    0x62, 0xff, 0xe9, 0x6f, 0xce, 0xec, 0x07, 0x88, 0xdd, 0x36,
    0x3f, 0x01, 0x64, 0xdb, 0x7b, 0xe2, 0x00, 0x36, 0x11, 0xe2,
    0x97, 0xab, 0x58, 0x46, 0x12, 0x31, 0x23, 0x00, 0x1e, 0x1f,
    0xb3, 0xe3, 0x63, 0x66, 0x64, 0x36, 0xec, 0xff, 0x4e, 0x80,
    0xc8, 0xec, 0xce, 0x8c, 0x08, 0xe4, 0xff, 0xce, 0x50, 0x55,
    0x66, 0x07, 0xa6, 0xcc, 0xcc, 0x6a, 0x70, 0x66, 0x55, 0x05,
    0x10, 0xd9, 0x9d, 0x00, 0xf7, 0xad, 0xda, 0x7f, 0x00, 0xd9,
    0x9d, 0x01, 0x80, 0xb9, 0x9b, 0x00, 0xfe, 0x5b, 0xb5, 0xef,
    0x00, 0xb9, 0x9b, 0x08, 0xa0, 0xaa, 0x66, 0x0e, 0x56, 0x33,
    0x33, 0x65, 0xe0, 0x66, 0xaa, 0x0a, 0x73, 0xff, 0x27, 0x10,
    0x31, 0x73, 0x37, 0x13, 0x01, 0x72, 0xff, 0x37,
};

// TitleBytes - upstream's own real "RUPTUS" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 letters x 4x4 VVram-cell glyph indices each
// (80 values total), byte-diff-verified via script against the real
// upstream source. Every value here is a valid index into rupCharPattern[]'s
// own "logo" range (indices 0-15, the first 32 bytes of that table - the
// exact same shared block-pattern palette gameCracky.c's own crkTitleBytes
// draws through too) - restored here after gameCracky.c's own analogous
// fix (see that file's own header comment and this file's own
// rupBeginTitle() comment for why the earlier plain-text "RUPTUS"
// substitute was wrong).
int rupTitleBytes[80] = {
    0x0f, 0x0d, 0x02, 0x0f,
    0x0f, 0x0e, 0x01, 0x0f,
    0x0f, 0x0c, 0x03, 0x0f,
    0x05, 0x04, 0x01, 0x04,
    0x0c, 0x03, 0x0f, 0x0d,
    0x0c, 0x03, 0x0f, 0x0e,
    0x0c, 0x03, 0x0f, 0x05,
    0x05, 0x00, 0x05, 0x00,
    0x02, 0x0d, 0x07, 0x0c,
    0x03, 0x0c, 0x03, 0x0c,
    0x00, 0x0c, 0x03, 0x0c,
    0x00, 0x04, 0x01, 0x00,
    0x03, 0x0f, 0x08, 0x07,
    0x03, 0x0f, 0x04, 0x0b,
    0x03, 0x0f, 0x08, 0x02,
    0x05, 0x01, 0x00, 0x05,
    0x0b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40) -
// byte-for-byte identical to Cracky's own crkFrequencies (same author/table).
int rupFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int rupMelodyFire[13] = {
    1, 38, 1, 36, 1, 34, 1, 32, 1, 30, 1, 40, 0,
};

int rupMelodyUp[13] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30, 1, 33, 0,
};

int rupMelodyStart[21] = {
    6, 30, 6, 28, 6, 30, 12, 33, 6, 37, 24, 35, 6,
    0, 12, 33, 6, 33, 12, 35, 0,
};

// Sound_Clear upstream has NO trailing terminator byte (a real upstream bug
// - ToneChannel::Next() would read past this array's own end once the
// melody finishes); fixed here by appending an explicit 0 - see header.
int rupMelodyClear[31] = {
    6, 25, 6, 28, 6, 33, 6, 30, 6, 0, 6, 30, 6,
    0, 6, 28, 6, 0, 6, 28, 6, 0, 6, 28, 6, 0,
    6, 28, 12, 30, 0,
};

// Sound_GameOver has the same missing-terminator bug - same fix.
int rupMelodyGameOver[27] = {
    6, 25, 6, 25, 6, 28, 12, 25, 6, 33, 6, 32, 6,
    30, 6, 28, 6, 30, 6, 0, 12, 30, 6, 28, 12, 30,
    0,
};

// rupMelodyBgm1/rupMelodyBgm2 - re-extracted and byte-diff-verified against
// the real upstream Sound.cpp (a small Python script resolving every
// NoteLength/Scale symbolic name to its real numeric value and comparing
// element-by-element), which this port's own original transcription did
// NOT actually match despite the header comment above claiming it did:
//   - rupMelodyBgm1 had 2 wrong values near its very end (index 176: 9
//     instead of the real N4P=18; index 182: 18 instead of the real
//     N2+N8=30) - a small, isolated slip in the trailing "outro" bars.
//   - rupMelodyBgm2 was far more seriously wrong: only 223 of the real
//     235 elements were ever actually typed in (the array's own declared
//     size, [235], was correct - the literal initializer list itself was
//     short by 12 values), and from partway through the very first bar
//     onward almost every value differs from upstream's real note/duration
//     data - this is the exact "Tiny Bomber dropped byte" transcription-
//     error class this project has hit before with large data tables.
//     Regenerated programmatically from the verified extraction rather
//     than hand-patched, matching this project's own established practice
//     for a table this corrupted (SnakeGame85's own load-screen bitmap hit
//     the identical failure mode - a draft written directly rather than
//     reading the extraction script's own output back in first).
int rupMelodyBgm1[185] = {
    6, 30, 6, 30, 6, 33, 6, 30, 6, 33, 12, 30, 6,
    0, 6, 28, 12, 28, 12, 28, 6, 28, 12, 30, 6, 30,
    6, 30, 6, 33, 6, 30, 6, 33, 12, 35, 6, 0, 6,
    37, 12, 37, 12, 37, 6, 35, 12, 37, 6, 30, 6, 30,
    6, 33, 6, 30, 6, 33, 12, 30, 6, 0, 6, 28, 12,
    28, 12, 28, 6, 28, 12, 30, 6, 30, 6, 30, 6, 33,
    6, 30, 6, 33, 12, 35, 6, 0, 6, 37, 12, 37, 12,
    37, 6, 35, 12, 37, 6, 35, 6, 35, 6, 35, 12, 35,
    6, 33, 12, 35, 6, 35, 6, 35, 6, 35, 12, 35, 6,
    33, 12, 35, 6, 30, 6, 30, 6, 30, 12, 30, 6, 28,
    12, 30, 6, 30, 6, 30, 6, 30, 12, 30, 6, 28, 12,
    30, 6, 35, 6, 35, 6, 35, 12, 35, 6, 33, 12, 35,
    6, 35, 6, 35, 6, 35, 12, 35, 6, 33, 12, 35, 6,
    37, 6, 37, 12, 37, 6, 35, 18, 37, 6, 38, 12, 38,
    30, 37, 255,
};

int rupMelodyBgm2[235] = {
    12, 2, 6, 0, 6, 2, 6, 0, 6, 2, 6, 0, 6,
    2, 12, 9, 6, 0, 6, 9, 6, 0, 6, 9, 6, 0,
    6, 9, 12, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6,
    0, 6, 11, 12, 1, 6, 0, 6, 1, 6, 0, 6, 1,
    6, 0, 6, 1, 12, 2, 6, 0, 6, 2, 6, 0, 6,
    2, 6, 0, 6, 2, 12, 9, 6, 0, 6, 9, 6, 0,
    6, 9, 6, 0, 6, 9, 12, 11, 6, 0, 6, 11, 6,
    0, 6, 11, 6, 0, 6, 11, 12, 1, 6, 0, 6, 1,
    6, 0, 6, 1, 6, 0, 6, 1, 6, 11, 6, 0, 6,
    11, 6, 0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11,
    6, 0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6,
    0, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0,
    6, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0, 6,
    6, 6, 0, 6, 6, 6, 0, 6, 11, 6, 0, 6, 11,
    6, 0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6,
    0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6, 0,
    6, 6, 6, 0, 6, 6, 6, 0, 6, 1, 6, 0, 6,
    1, 6, 0, 6, 9, 6, 8, 6, 0, 24, 6, 6, 0,
    255,
};

// Stage data - flattened from upstream's own StageDef+Position/BarrierDef
// arrays into parallel fixed-size tables (7=MaxFortCount, 8=MaxBarrierCount
// slots per stage, unused trailing slots zero-padded), matching this
// project's own "flatten a struct-with-a-real-pointer-member into plain
// arrays" precedent (e.g. gameCracky.c's own crkStageEnemies). All values
// are raw upstream units - the real ">>1" halving InitForts/InitBarriers/
// InitFighter perform is applied at init time, not pre-computed here.
int rupStageStartX[10] = { 48, 48, 48, 48, 48, 48, 48, 48, 48, 0 };
int rupStageStartY[10] = { 64, 64, 64, 64, 64, 64, 64, 64, 64, 48 };
int rupStageFortCount[10] = { 3, 4, 5, 7, 5, 6, 6, 5, 4, 4 };
int rupStageBarrierCount[10] = { 2, 8, 5, 5, 5, 4, 6, 7, 7, 8 };

int rupStageForts[10][7][2] = {
    { { 56, 40 }, { 24, 56 }, { 40, 88 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
    { { 40, 24 }, { 88, 24 }, { 24, 88 }, { 72, 88 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
    { { 24, 24 }, { 88, 40 }, { 24, 56 }, { 24, 104 }, { 88, 104 }, { 0, 0 }, { 0, 0 } },
    { { 40, 24 }, { 88, 24 }, { 24, 56 }, { 72, 56 }, { 88, 88 }, { 40, 104 }, { 72, 104 } },
    { { 24, 24 }, { 40, 24 }, { 72, 24 }, { 88, 24 }, { 40, 104 }, { 0, 0 }, { 0, 0 } },
    { { 24, 24 }, { 72, 24 }, { 24, 56 }, { 72, 56 }, { 24, 104 }, { 72, 104 }, { 0, 0 } },
    { { 40, 24 }, { 56, 56 }, { 24, 88 }, { 88, 88 }, { 56, 104 }, { 40, 120 }, { 0, 0 } },
    { { 72, 24 }, { 24, 40 }, { 24, 88 }, { 24, 104 }, { 72, 104 }, { 0, 0 }, { 0, 0 } },
    { { 24, 24 }, { 72, 24 }, { 40, 104 }, { 72, 104 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
    { { 40, 24 }, { 72, 56 }, { 40, 72 }, { 72, 88 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
};

// rupStageBarriers[stage][slot][0=x,1=y,2=length,3=flags]
int rupStageBarriers[10][8][4] = {
    { { 32, 80, 16, 0 }, { 32, 48, 16, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 80, 16, 16, 0 }, { 80, 32, 16, 0 }, { 16, 80, 16, 0 }, { 16, 96, 16, 0 }, { 32, 16, 16, 1 }, { 48, 16, 16, 1 }, { 64, 80, 16, 1 }, { 80, 80, 16, 1 } },
    { { 16, 32, 32, 0 }, { 16, 48, 16, 4 }, { 16, 64, 16, 4 }, { 32, 48, 16, 1 }, { 80, 80, 32, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 16, 16, 32, 0 }, { 16, 32, 32, 0 }, { 32, 48, 48, 0 }, { 32, 96, 48, 0 }, { 32, 112, 48, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 16, 48, 80, 0 }, { 16, 96, 32, 0 }, { 64, 96, 32, 0 }, { 32, 32, 80, 1 }, { 80, 32, 80, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 16, 16, 96, 1 }, { 32, 16, 96, 1 }, { 64, 16, 96, 1 }, { 80, 16, 96, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 32, 32, 32, 0 }, { 16, 80, 64, 0 }, { 48, 96, 16, 0 }, { 16, 112, 32, 0 }, { 32, 16, 80, 1 }, { 80, 16, 80, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    { { 16, 16, 64, 0 }, { 16, 32, 64, 0 }, { 16, 64, 32, 0 }, { 64, 64, 16, 0 }, { 16, 80, 32, 0 }, { 64, 80, 16, 0 }, { 16, 96, 64, 0 }, { 0, 0, 0, 0 } },
    { { 16, 16, 32, 0 }, { 64, 16, 16, 0 }, { 48, 48, 16, 0 }, { 32, 80, 16, 2 }, { 16, 112, 32, 0 }, { 64, 112, 16, 0 }, { 32, 80, 16, 1 }, { 0, 0, 0, 0 } },
    { { 16, 32, 64, 0 }, { 16, 48, 64, 0 }, { 16, 64, 64, 0 }, { 16, 96, 64, 4 }, { 16, 112, 64, 4 }, { 32, 16, 96, 1 }, { 48, 16, 96, 1 }, { 80, 96, 16, 1 } },
};

// Star.cpp's own fixed 8-star field (byte-for-byte from upstream, the
// remaining ~22 commented-out entries in the original table are dead).
int rupStars[8][2] = {
    { 81, 19 }, { 23, 56 }, { 10, 43 }, { 115, 25 },
    { 100, 36 }, { 37, 88 }, { 80, 60 }, { 54, 9 },
};

// Enemy.cpp's own 16-entry pseudo-random cycling table (Rnd()).
int rupRndNumbers[16] = { 11, 8, 9, 4, 4, 12, 0, 12, 13, 11, 0, 6, 13, 12, 5, 15 };

// -----------------------------------------------------------------------------
//   Struct definitions
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y, dx, dy, direction, code, sprite, numerator, denominator;
} RupFighterBullet;

typedef struct
{
    int x, y, dx, dy, code, sprite;
    int numeratorX, denominatorX, numeratorY, denominatorY;
} RupEnemyBullet;

typedef struct
{
    int x, y, direction, sprite, numerator, denominator, bulletCount;
} RupEnemy;

typedef struct
{
    int x, y, life, flags;
} RupFort;

typedef struct
{
    int startX, startY, endX, endY, flags;
} RupBarrier;

typedef struct
{
    int x, y, status;
} RupBang;

typedef struct
{
    int x, y, code;
} RupSprite;

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int rupScore;
int rupCurrentStage;
int rupRemainCount;
int rupClock;
int rupStageIndex;

int rupVVram[RUP_VVRAM_HEIGHT][RUP_VVRAM_WIDTH];
int rupStatusChar[4][6];
int rupTitleChar[8][RUP_VVRAM_WIDTH];
int rupRader[RUP_RADER_HEIGHT][RUP_RADER_WIDTH];

RupSprite rupSprites[RUP_SPRITE_COUNT];

int rupBaseX, rupBaseY;

int rupFighterX, rupFighterY;
int rupFighterDx, rupFighterDy;
int rupFighterDirection;
int rupFighterNumerator, rupFighterDenominator;
int rupFighterDyingCount;

RupFighterBullet rupFighterBullets[RUP_FBULLET_MAX];
int rupFighterBulletIntervalCount;

RupEnemy rupEnemies[RUP_ENEMY_MAX];
int rupEnemyCount;
int rupEnemyRndIndex;

RupEnemyBullet rupEnemyBullets[RUP_EBULLET_MAX];

RupFort rupForts[RUP_MAX_FORT_COUNT];
int rupFortCount;

RupBarrier rupBarriers[RUP_MAX_BARRIER_COUNT];

RupBang rupBangs[RUP_BANG_MAX];

int rupItemX, rupItemY;

int rupSeqMelody[3];
int rupSeqPos[3];
int rupSeqWait[3];
int rupSeqActive[3];

bool rupOverlayActive;
int rupOverlayText[10];
int rupOverlayLen;
int rupOverlayPage;
int rupOverlayCol;

#define RUP_STATE_TITLE 0
#define RUP_STATE_START_JINGLE 1
#define RUP_STATE_PLAYING 2
#define RUP_STATE_GAMEOVER_JINGLE 3
#define RUP_STATE_CLEAR_JINGLE 4
int rupState;
int rupSelection;
bool rupSelectionChanged;
int rupPrevLeft, rupPrevRight, rupPrevUp, rupPrevDown, rupPrevFire;
bool rupPendingContinue;


// -----------------------------------------------------------------------------
//   Byte-wraparound helpers - see this file's own header comment for why
//   these are load-bearing (not just defensive) throughout this port.
// -----------------------------------------------------------------------------

int rupWrapByte( int v )
{
    return v & 0xFF;
}

int rupWrapSByte( int v )
{
    v = v & 0xFF;
    if( v > 127 )
      v = v - 256;
    return v;
}


// -----------------------------------------------------------------------------
//   Math.cpp (Enemy.cpp's own Rnd())
// -----------------------------------------------------------------------------

int rupRnd()
{
    int r;
    r = rupRndNumbers[ rupEnemyRndIndex ];
    rupEnemyRndIndex = rupEnemyRndIndex + 1;
    if( rupEnemyRndIndex >= 16 )
      rupEnemyRndIndex = 0;
    return r;
}


// -----------------------------------------------------------------------------
//   Mover.cpp
// -----------------------------------------------------------------------------

bool rupHitMover( int x1, int y1, int size1, int x2, int y2, int size2 )
{
    int xDiff, yDiff, range;
    if( size2 != 0 )
    {
        if( size1 != 0 )
        {
            int minDistance;
            minDistance = size1 + size2 - 1;
            xDiff = rupWrapByte( x2 - x1 + minDistance );
            yDiff = rupWrapByte( y2 - y1 + minDistance );
            range = ( minDistance << 1 ) + 1;
        }
        else
        {
            xDiff = rupWrapByte( x1 - x2 + size2 );
            yDiff = rupWrapByte( y1 - y2 + size2 );
            range = ( size2 << 1 );
        }
    }
    else
    {
        if( size1 != 0 )
        {
            xDiff = rupWrapByte( x2 - x1 + size1 );
            yDiff = rupWrapByte( y2 - y1 + size1 );
            range = ( size1 << 1 );
        }
        else
          return x1 == x2 && y1 == y2;
    }
    return xDiff < range && yDiff < range;
}


// -----------------------------------------------------------------------------
//   Direction.cpp
// -----------------------------------------------------------------------------

int rupNewDirection( int old, int target )
{
    int diff;
    if( target == old ) return old;
    diff = ( target - old ) & 7;
    if( diff <= 4 )
      return ( old + 1 ) & 7;
    return ( old - 1 ) & 7;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - coordinate helpers (InitStage/InitTrying come later, once
//   every module they depend on is defined).
// -----------------------------------------------------------------------------

int rupAddX( int x, int dx )
{
    x = rupWrapByte( x + dx );
    if( x >= RUP_STAGE_WIDTH )
    {
        if( dx < 0 )
          x = rupWrapByte( x + RUP_STAGE_WIDTH );
        else
          x = rupWrapByte( x - RUP_STAGE_WIDTH );
    }
    return x;
}

int rupAddY( int y, int dy )
{
    y = rupWrapByte( y + dy );
    if( y >= RUP_STAGE_HEIGHT )
    {
        if( dy < 0 )
          y = rupWrapByte( y + RUP_STAGE_HEIGHT );
        else
          y = rupWrapByte( y - RUP_STAGE_HEIGHT );
    }
    return y;
}

// Upstream's real return type is `sbyte SubX(byte x1, byte x2)` - every
// return path implicitly truncates/reinterprets its int expression to a
// real int8_t at the return boundary (a genuine C++ truncation point, same
// class as a function parameter or a narrow-typed local assignment). This
// is a no-op for the common case (x1,x2 both small in-range stage
// coordinates), but x2 (or y2) can genuinely be a raw, unwrapped "wrapped-
// negative" byte value - StartEnemy() assigns `x = BaseX`/`y = BaseY`
// directly (not through AddX/AddY) for 2 of its 4 spawn-direction
// branches, so a freshly-spawned enemy's own x/y can legitimately be e.g.
// 243 (representing -13) - and MoveEnemy() feeds exactly that value into
// SubX/SubY every tick for fighter-relative targeting. Without the final
// rupWrapSByte(), a large `d` (e.g. 238) computed from such an input
// produces a plain, un-truncated `d-StageWidth`/`StageWidth-d` that can be
// wildly out of the real sbyte's reinterpreted range and even land with
// the WRONG SIGN relative to what upstream's real int8_t conversion would
// give - directly flipping which way an enemy decides to move relative to
// the fighter. Fixed by wrapping every return path, matching upstream's
// real per-return truncation exactly (a no-op for already-small values).
int rupSubX( int x1, int x2 )
{
    int d;
    if( x1 > x2 )
    {
        d = x1 - x2;
        if( d < RUP_STAGE_WIDTH / 2 )
          return rupWrapSByte( d );
        return rupWrapSByte( d - RUP_STAGE_WIDTH );
    }
    d = x2 - x1;
    if( d < RUP_STAGE_WIDTH / 2 )
      return rupWrapSByte( -d );
    return rupWrapSByte( RUP_STAGE_WIDTH - d );
}

int rupSubY( int y1, int y2 )
{
    int d;
    if( y1 > y2 )
    {
        d = y1 - y2;
        if( d < RUP_STAGE_HEIGHT / 2 )
          return rupWrapSByte( d );
        return rupWrapSByte( d - RUP_STAGE_HEIGHT );
    }
    d = y2 - y1;
    if( d < RUP_STAGE_HEIGHT / 2 )
      return rupWrapSByte( -d );
    return rupWrapSByte( RUP_STAGE_HEIGHT - d );
}

int rupOffsetX( int x )
{
    if( rupBaseX >= RUP_STAGE_WIDTH )
    {
        if( x >= RUP_STAGE_WIDTH / 2 )
          x = rupWrapByte( x - RUP_STAGE_WIDTH );
    }
    else if( rupBaseX + RUP_VVRAM_WIDTH >= RUP_STAGE_WIDTH )
    {
        if( x < RUP_STAGE_WIDTH / 2 )
          x = rupWrapByte( x + RUP_STAGE_WIDTH );
    }
    return rupWrapByte( x - rupBaseX );
}

int rupOffsetY( int y )
{
    if( rupBaseY >= RUP_STAGE_HEIGHT )
    {
        if( y >= RUP_STAGE_HEIGHT / 2 )
          y = rupWrapByte( y - RUP_STAGE_HEIGHT );
    }
    else if( rupBaseY + RUP_VVRAM_HEIGHT >= RUP_STAGE_HEIGHT )
    {
        if( y < RUP_STAGE_HEIGHT / 2 )
          y = rupWrapByte( y + RUP_STAGE_HEIGHT );
    }
    return rupWrapByte( y - rupBaseY );
}

void rupUpdateBasePosition()
{
    int x, y;
    x = rupWrapByte( rupFighterX - RUP_VVRAM_WIDTH / 2 );
    y = rupWrapByte( rupFighterY - RUP_VVRAM_HEIGHT / 2 );
    if( x != rupBaseX || y != rupBaseY )
    {
        rupBaseX = x;
        rupBaseY = y;
    }
}


// -----------------------------------------------------------------------------
//   VVram.cpp
// -----------------------------------------------------------------------------

void rupClearVVram()
{
    int y, x;
    for( y = 0; y < RUP_VVRAM_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < RUP_VVRAM_WIDTH; x = x + 1 )
          rupVVram[ y ][ x ] = RUP_CHAR_SPACE;
    }
}

void rupVPutXY( int x, int y, int c )
{
    if( x < 0 ) return;
    if( x >= RUP_VVRAM_WIDTH ) return;
    if( y < 0 ) return;
    if( y >= RUP_VVRAM_HEIGHT ) return;
    rupVVram[ y ][ x ] = c;
}

void rupVPut6CXY( int x, int y, int c )
{
    int i, j, cc, xi, yj;
    if( x < -6 ) return;
    if( x >= RUP_VVRAM_WIDTH ) return;
    if( y < -6 ) return;
    if( y >= RUP_VVRAM_HEIGHT ) return;
    cc = c;
    yj = y;
    for( j = 0; j < 6; j = j + 1 )
    {
        xi = x;
        for( i = 0; i < 6; i = i + 1 )
        {
            if( xi >= 0 && xi < RUP_VVRAM_WIDTH && yj >= 0 && yj < RUP_VVRAM_HEIGHT )
              rupVVram[ yj ][ xi ] = cc;
            cc = cc + 1;
            xi = xi + 1;
        }
        yj = yj + 1;
    }
}

// HLine/VLine - upstream does no bounds checking of its own (relies on every
// call site pre-clamping start/count against VVramWidth/Height, which
// rupDrawBarriers() below does) - a defensive per-cell bound check is added
// anyway, cheap insurance against a mistaken call ever writing out of range.
void rupHLine( int x, int y, int count )
{
    int i, xi;
    for( i = 0; i < count; i = i + 1 )
    {
        xi = x + i;
        if( xi >= 0 && xi < RUP_VVRAM_WIDTH && y >= 0 && y < RUP_VVRAM_HEIGHT )
          rupVVram[ y ][ xi ] = RUP_CHAR_BARRIER;
    }
}

void rupVLine( int x, int y, int count )
{
    int i, yi;
    for( i = 0; i < count; i = i + 1 )
    {
        yi = y + i;
        if( x >= 0 && x < RUP_VVRAM_WIDTH && yi >= 0 && yi < RUP_VVRAM_HEIGHT )
          rupVVram[ yi ][ x ] = RUP_CHAR_BARRIER + 1;
    }
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void rupHideAllSprites()
{
    int i;
    for( i = 0; i < RUP_SPRITE_COUNT; i = i + 1 )
      rupSprites[ i ].code = RUP_SPRITE_HIDDEN;
}

void rupShowSprite( int index, int x, int y, int code )
{
    rupSprites[ index ].x = x;
    rupSprites[ index ].y = y;
    rupSprites[ index ].code = code;
}

void rupHideSprite( int index )
{
    rupSprites[ index ].code = RUP_SPRITE_HIDDEN;
}

void rupDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < RUP_SPRITE_COUNT; i = i + 1 )
    {
        if( rupSprites[ i ].code != RUP_SPRITE_HIDDEN )
        {
            x = rupSprites[ i ].x;
            y = rupSprites[ i ].y;
            c = rupSprites[ i ].code;
            rupVVram[ y ][ x ] = c;
            if( c >= RUP_CHAR_FIGHTER )
            {
                c = c + 1; rupVVram[ y ][ x + 1 ] = c;
                c = c + 1; rupVVram[ y + 1 ][ x ] = c;
                c = c + 1; rupVVram[ y + 1 ][ x + 1 ] = c;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Rader.cpp - a raw hardware page-byte minimap, no glyph lookup involved.
// -----------------------------------------------------------------------------

void rupClearRader()
{
    int r, c;
    for( r = 0; r < RUP_RADER_HEIGHT; r = r + 1 )
    {
        for( c = 0; c < RUP_RADER_WIDTH; c = c + 1 )
          rupRader[ r ][ c ] = 0xff;
    }
}

void rupDrawFortRader( int x, int y, bool visible )
{
    int row, col, i, bits;
    x = rupWrapByte( x );
    y = rupWrapByte( y );
    x = x / 2;
    y = y / 2;
    row = y / 8;
    col = x;
    if( row < 0 || row >= RUP_RADER_HEIGHT ) return;
    if( visible )
    {
        if( ( y & 4 ) == 0 ) bits = 0xf8; else bits = 0x8f;
        for( i = 0; i < 3; i = i + 1 )
        {
            if( col + i >= 0 && col + i < RUP_RADER_WIDTH )
              rupRader[ row ][ col + i ] = rupRader[ row ][ col + i ] & bits;
        }
    }
    else
    {
        if( ( y & 4 ) == 0 ) bits = 0x07; else bits = 0x70;
        for( i = 0; i < 3; i = i + 1 )
        {
            if( col + i >= 0 && col + i < RUP_RADER_WIDTH )
              rupRader[ row ][ col + i ] = rupRader[ row ][ col + i ] | bits;
        }
    }
}

void rupDrawFighterOnRader()
{
    int x, y, row, col, bits;
    x = ( ( rupFighterX - 1 ) / 2 ) & 0x1f;
    while( x >= RUP_RADER_WIDTH )
      x = x - RUP_RADER_WIDTH;
    y = ( ( rupFighterY - 1 ) / 2 ) & 0x1f;
    row = y / 8;
    col = x;
    bits = 1 << ( y & 7 );
    if( row >= 0 && row < RUP_RADER_HEIGHT && col >= 0 && col < RUP_RADER_WIDTH )
      rupRader[ row ][ col ] = rupRader[ row ][ col ] ^ bits;
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - writes into rupStatusChar (the real status
//   column area, cols 104-127, pages 0-3: score/stage/remain).
// -----------------------------------------------------------------------------

int rupAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - same table as Vram.cpp's
    // own PrintC() linear search.
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

int rupPrintC( int page, int col, int c )
{
    if( page >= 0 && page < 4 && col >= 0 && col < 6 )
      rupStatusChar[ page ][ col ] = rupAsciiIndex( c );
    return col + 1;
}

int rupPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = rupPrintC( page, col, s[ i ] );
    return col;
}

int rupPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
{
    int c;
    c = value / n;
    if( c == 0 )
    {
        if( zeroVisible ) c = '0'; else c = ' ';
    }
    else
      c = c + '0';
    return rupPrintC( page, col, c );
}

void rupPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      rupPrintC( page, col, ' ' );
    else
      rupPrintC( page, col, d1 + '0' );
    rupPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void rupPrintNumber5( int page, int col, int w )
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
          rupPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            rupPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    rupPrintC( page, col + 4, rem + '0' );
}

void rupPrintScore()
{
    rupPrintNumber5( 0, 0, rupScore );
    rupPrintC( 0, 5, '0' );
}

void rupPrintRemain()
{
    // Simplified from upstream's own 2x2 Put2C icon-per-extra-life display
    // (which also has a real quirk where 2-3 extra lives print as blanks,
    // see this file's own header comment for the Cracky-precedent reasoning
    // behind not reproducing that inconsistency) to a single plain digit
    // showing the extra-life count, whenever there is at least one.
    int i, j;
    for( j = 0; j < 6; j = j + 1 )
      rupPrintC( 3, j, ' ' );
    if( rupRemainCount > 1 )
    {
        i = rupRemainCount - 1;
        rupPrintC( 3, 0, 'R' );
        rupPrintC( 3, 1, i + '0' );
    }
}

void rupPrintStatus()
{
    rupPrintByteNumber2( 1, 4, rupCurrentStage + 1 );
    rupPrintScore();
    rupPrintRemain();
}

void rupBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    rupOverlayActive = true;
    rupOverlayLen = len;
    rupOverlayPage = page;
    rupOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      rupOverlayText[ i ] = s[ i ];
}

void rupPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    rupBeginOverlay( s, 9, 4, 8 );
}

void rupTitlePrintC( int page, int col, int c )
{
    if( page >= 0 && page < 8 && col >= 0 && col < RUP_VVRAM_WIDTH )
      rupTitleChar[ page ][ col ] = rupAsciiIndex( c );
}

void rupTitlePrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
    {
        rupTitlePrintC( page, col, s[ i ] );
        col = col + 1;
    }
}


// -----------------------------------------------------------------------------
//   Sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
//   2=BGM-B), each advancing every real engine frame - matching
//   gameCracky.c's own crkStartSeq/crkAdvanceOneSeq shape, just with this
//   game's own real Tempo(180)-derived note-frame formula. Sound_SmallBang/
//   Sound_LargeBang's real hardware noise channel has no equivalent
//   sequencer slot - see this file's own header comment for how those two
//   are approximated instead.
// -----------------------------------------------------------------------------

int rupMelodyLength( int id )
{
    if( id == RUP_MELODY_FIRE ) return 13;
    if( id == RUP_MELODY_UP ) return 13;
    if( id == RUP_MELODY_START ) return 21;
    if( id == RUP_MELODY_CLEAR ) return 31;
    if( id == RUP_MELODY_GAMEOVER ) return 27;
    if( id == RUP_MELODY_BGM1 ) return 185;
    if( id == RUP_MELODY_BGM2 ) return 235;
    return 0;
}

int rupMelodyValue( int id, int idx )
{
    if( id == RUP_MELODY_FIRE ) return rupMelodyFire[ idx ];
    if( id == RUP_MELODY_UP ) return rupMelodyUp[ idx ];
    if( id == RUP_MELODY_START ) return rupMelodyStart[ idx ];
    if( id == RUP_MELODY_CLEAR ) return rupMelodyClear[ idx ];
    if( id == RUP_MELODY_GAMEOVER ) return rupMelodyGameOver[ idx ];
    if( id == RUP_MELODY_BGM1 ) return rupMelodyBgm1[ idx ];
    if( id == RUP_MELODY_BGM2 ) return rupMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/RUP_TEMPO real 60Hz ticks - see header comment.
int rupNoteFrames( int length )
{
    return (int)( length * ( 300.0 / (float)RUP_TEMPO ) + 0.5 );
}

void rupStartSeq( int channel, int melodyId )
{
    rupSeqMelody[ channel ] = melodyId;
    rupSeqPos[ channel ] = 0;
    rupSeqWait[ channel ] = 0;
    rupSeqActive[ channel ] = 1;
}

void rupStopSeq( int channel )
{
    rupSeqActive[ channel ] = 0;
    rupSeqMelody[ channel ] = RUP_MELODY_NONE;
}

bool rupSeqPlaying( int channel )
{
    return rupSeqActive[ channel ] != 0;
}

void rupAdvanceOneSeq( int channel )
{
    int length, note;
    if( rupSeqActive[ channel ] == 0 ) return;
    if( rupSeqWait[ channel ] > 0 )
    {
        rupSeqWait[ channel ] = rupSeqWait[ channel ] - 1;
        return;
    }
    length = rupMelodyValue( rupSeqMelody[ channel ], rupSeqPos[ channel ] );
    if( length == 0 )
    {
        rupStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        rupSeqPos[ channel ] = 0;
        length = rupMelodyValue( rupSeqMelody[ channel ], 0 );
    }
    note = rupMelodyValue( rupSeqMelody[ channel ], rupSeqPos[ channel ] + 1 );
    rupSeqPos[ channel ] = rupSeqPos[ channel ] + 2;
    rupSeqWait[ channel ] = rupNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)rupFrequencies[ note - 1 ], (float)rupSeqWait[ channel ] / 60.0 );
}

void rupAdvanceSound()
{
    rupAdvanceOneSeq( 0 );
    rupAdvanceOneSeq( 1 );
    rupAdvanceOneSeq( 2 );
}

void rupStartBgm()
{
    rupStartSeq( 1, RUP_MELODY_BGM1 );
    rupStartSeq( 2, RUP_MELODY_BGM2 );
}

void rupStopBgm()
{
    rupStopSeq( 1 );
    rupStopSeq( 2 );
    md_stopTone();
}

void rupSoundFire()
{
    rupStartSeq( 0, RUP_MELODY_FIRE );
}

void rupSoundUp()
{
    rupStartSeq( 0, RUP_MELODY_UP );
}

// 32 real decay steps (MaxVolume(63) decrementing by 2 each channel-advance
// tick until 0), each RUP_TEMPO-derived tick apart - see header comment.
#define RUP_BANG_DURATION ( 32.0 * ( 300.0 / (float)RUP_TEMPO ) / 60.0 )

void rupSoundSmallBang()
{
    md_playTone( 3000.0, RUP_BANG_DURATION );
}

void rupSoundLargeBang()
{
    md_playTone( 1500.0, RUP_BANG_DURATION );
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void rupAddScore( int pts )
{
    rupScore = rupScore + pts;
    rupPrintScore();
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void rupInitBangs()
{
    int i;
    for( i = 0; i < RUP_BANG_MAX; i = i + 1 )
      rupBangs[ i ].status = RUP_BANG_STATUS_NONE;
}

void rupStartBang( int x, int y, bool large )
{
    int i;
    for( i = 0; i < RUP_BANG_MAX; i = i + 1 )
    {
        if( ( rupBangs[ i ].status & 0xf0 ) != RUP_BANG_STATUS_NONE ) continue;
        rupBangs[ i ].x = x;
        rupBangs[ i ].y = y;
        if( large )
          rupBangs[ i ].status = RUP_BANG_STATUS_LARGE_SMALL;
        else
          rupBangs[ i ].status = RUP_BANG_STATUS_SMALL;
        return;
    }
}

int rupBangLocate( int x, int y, int sprite, int code )
{
    if( sprite >= RUP_SPRITE_COUNT ) return sprite;
    x = rupOffsetX( x );
    if( x >= RUP_VVRAM_WIDTH ) return sprite;
    x = rupWrapByte( x - RUP_BANG_HALFSIZE );
    if( x >= RUP_VVRAM_WIDTH ) return sprite;
    y = rupOffsetY( y );
    if( y >= RUP_VVRAM_HEIGHT ) return sprite;
    y = rupWrapByte( y - RUP_BANG_HALFSIZE );
    if( y >= RUP_VVRAM_HEIGHT ) return sprite;
    rupShowSprite( sprite, x, y, code );
    return sprite + 1;
}

void rupUpdateBangs()
{
    int sprite, i, mode, count;
    sprite = RUP_SPRITE_BANG;
    for( i = 0; i < RUP_BANG_MAX; i = i + 1 )
    {
        mode = rupBangs[ i ].status & 0xf0;
        if( mode == RUP_BANG_STATUS_NONE ) continue;
        count = rupBangs[ i ].status & 0x0f;
        if( mode == RUP_BANG_STATUS_LARGE_LARGE )
        {
            // rupBangLocate()'s own x/y params match upstream's real
            // `byte x, byte y` parameters, which implicitly truncate
            // whatever expression is passed at the call boundary - a real
            // C++ truncation point. rupBangs[i].x/y can be 0 (or, via a
            // raw wrapped-BaseX/BaseY-style coordinate, up to 255), so
            // `x - HalfSize`/`x + HalfSize` can go genuinely negative or
            // past 255 here; passing that raw (unwrapped) value straight
            // through instead changes which branch rupOffsetX()'s own
            // internal `>= StageWidth/2` checks take, silently showing or
            // hiding an explosion corner in the wrong place. Wrapped here
            // to match upstream's real per-call-site truncation exactly.
            sprite = rupBangLocate( rupWrapByte( rupBangs[ i ].x - RUP_BANG_HALFSIZE ), rupWrapByte( rupBangs[ i ].y - RUP_BANG_HALFSIZE ), sprite, RUP_CHAR_LARGEBANG + 0 );
            sprite = rupBangLocate( rupWrapByte( rupBangs[ i ].x + RUP_BANG_HALFSIZE ), rupWrapByte( rupBangs[ i ].y - RUP_BANG_HALFSIZE ), sprite, RUP_CHAR_LARGEBANG + 4 );
            sprite = rupBangLocate( rupWrapByte( rupBangs[ i ].x - RUP_BANG_HALFSIZE ), rupWrapByte( rupBangs[ i ].y + RUP_BANG_HALFSIZE ), sprite, RUP_CHAR_LARGEBANG + 8 );
            sprite = rupBangLocate( rupWrapByte( rupBangs[ i ].x + RUP_BANG_HALFSIZE ), rupWrapByte( rupBangs[ i ].y + RUP_BANG_HALFSIZE ), sprite, RUP_CHAR_LARGEBANG + 12 );
        }
        else
          sprite = rupBangLocate( rupBangs[ i ].x, rupBangs[ i ].y, sprite, RUP_CHAR_SMALLBANG );

        count = count + 1;
        if( count >= 8 )
        {
            if( mode == RUP_BANG_STATUS_LARGE_SMALL )
              rupBangs[ i ].status = RUP_BANG_STATUS_LARGE_LARGE;
            else
              rupBangs[ i ].status = RUP_BANG_STATUS_NONE;
        }
        else
          rupBangs[ i ].status = mode | count;
    }
    while( sprite < RUP_SPRITE_COUNT )
    {
        rupHideSprite( sprite );
        sprite = sprite + 1;
    }
}


// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

void rupInitItem()
{
    rupItemX = RUP_OBJ_NONE;
}

void rupStartItem( int x, int y )
{
    if( rupItemX != RUP_OBJ_NONE ) return;
    rupItemX = x;
    rupItemY = y;
}

void rupDrawItem()
{
    int x, y;
    if( rupItemX == RUP_OBJ_NONE ) return;
    x = rupOffsetX( rupItemX );
    if( x >= RUP_VVRAM_WIDTH ) { rupHideSprite( RUP_SPRITE_ITEM ); return; }
    y = rupOffsetY( rupItemY );
    if( y >= RUP_VVRAM_HEIGHT ) { rupHideSprite( RUP_SPRITE_ITEM ); return; }
    x = rupWrapByte( x - RUP_ITEM_HALFSIZE );
    y = rupWrapByte( y - RUP_ITEM_HALFSIZE );
    if( x < RUP_VVRAM_WIDTH - RUP_ITEM_HALFSIZE * 2 && y < RUP_VVRAM_HEIGHT - RUP_ITEM_HALFSIZE * 2 )
      rupShowSprite( RUP_SPRITE_ITEM, x, y, RUP_CHAR_ITEM );
    else
      rupHideSprite( RUP_SPRITE_ITEM );
}

bool rupHitItem( int x, int y, int size )
{
    if( rupItemX == RUP_OBJ_NONE ) return false;
    if( rupHitMover( rupItemX, rupItemY, RUP_ITEM_HALFSIZE, x, y, size ) )
    {
        rupItemX = RUP_OBJ_NONE;
        rupHideSprite( RUP_SPRITE_ITEM );
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Fort.cpp
// -----------------------------------------------------------------------------

void rupInitForts( int stageIndex )
{
    int i;
    rupFortCount = rupStageFortCount[ stageIndex ];
    for( i = 0; i < rupFortCount; i = i + 1 )
    {
        rupForts[ i ].x = rupStageForts[ stageIndex ][ i ][ 0 ] >> 1;
        rupForts[ i ].y = rupStageForts[ stageIndex ][ i ][ 1 ] >> 1;
        rupForts[ i ].life = RUP_FORT_MAX_LIFE;
        rupForts[ i ].flags = RUP_FORT_FLAG_LIVE;
    }
    for( i = rupFortCount; i < RUP_MAX_FORT_COUNT; i = i + 1 )
      rupForts[ i ].flags = 0;
}

void rupDrawForts()
{
    int i, x, y;
    for( i = 0; i < RUP_MAX_FORT_COUNT; i = i + 1 )
    {
        if( ( rupForts[ i ].flags & RUP_FORT_FLAG_LIVE ) == 0 ) continue;
        if( rupForts[ i ].life <= 0 )
        {
            rupForts[ i ].life = rupForts[ i ].life - 1;
            if( rupForts[ i ].life < -12 )
            {
                rupForts[ i ].flags = 0;
                rupFortCount = rupFortCount - 1;
            }
            continue;
        }
        x = rupWrapSByte( rupOffsetX( rupForts[ i ].x ) - RUP_FORT_HALFSIZE );
        y = rupWrapSByte( rupOffsetY( rupForts[ i ].y ) - RUP_FORT_HALFSIZE );
        rupVPut6CXY( x, y, RUP_CHAR_FORT );
        rupForts[ i ].flags = rupForts[ i ].flags | RUP_FORT_FLAG_VISIBLE;
    }
}

RupFort* rupNearFort( int x, int y, int size )
{
    int i;
    for( i = 0; i < RUP_MAX_FORT_COUNT; i = i + 1 )
    {
        if( ( rupForts[ i ].flags & ( RUP_FORT_FLAG_LIVE | RUP_FORT_FLAG_VISIBLE ) ) == 0 || rupForts[ i ].life <= 0 ) continue;
        if( rupHitMover( rupForts[ i ].x, rupForts[ i ].y, RUP_FORT_HALFSIZE, x, y, size ) )
          return &rupForts[ i ];
    }
    return NULL;
}

bool rupHitFort( int x, int y, int size )
{
    RupFort* pFort;
    pFort = rupNearFort( x, y, size );
    if( pFort != NULL )
    {
        pFort->life = pFort->life - 1;
        if( pFort->life == 0 )
        {
            rupSoundLargeBang();
            rupStartBang( pFort->x, pFort->y, true );
            rupAddScore( 50 );
            rupDrawFighterOnRader();
            rupDrawFortRader( pFort->x - RUP_FORT_HALFSIZE, pFort->y - RUP_FORT_HALFSIZE, false );
            rupDrawFighterOnRader();
        }
        else
        {
            if( size == 0 ) { x = x + 1; y = y + 1; }
            rupSoundSmallBang();
            rupStartBang( x, y, false );
        }
        return true;
    }
    return false;
}

void rupDrawFortsOnRader()
{
    int i;
    for( i = 0; i < RUP_MAX_FORT_COUNT; i = i + 1 )
    {
        if( ( rupForts[ i ].flags & ( RUP_FORT_FLAG_LIVE | RUP_FORT_FLAG_VISIBLE ) ) == 0 || rupForts[ i ].life <= 0 ) continue;
        rupDrawFortRader( rupForts[ i ].x - RUP_FORT_HALFSIZE, rupForts[ i ].y - RUP_FORT_HALFSIZE, true );
    }
}


// -----------------------------------------------------------------------------
//   Barrier.cpp
// -----------------------------------------------------------------------------

void rupInitBarriers( int stageIndex )
{
    int i, count, bx, by, blen, bflags;
    count = rupStageBarrierCount[ stageIndex ];
    for( i = 0; i < count; i = i + 1 )
    {
        bx = rupStageBarriers[ stageIndex ][ i ][ 0 ] >> 1;
        by = rupStageBarriers[ stageIndex ][ i ][ 1 ] >> 1;
        blen = rupStageBarriers[ stageIndex ][ i ][ 2 ];
        bflags = rupStageBarriers[ stageIndex ][ i ][ 3 ];
        rupBarriers[ i ].startX = bx;
        rupBarriers[ i ].startY = by;
        rupBarriers[ i ].flags = bflags | RUP_BARRIER_FLAG_LIVE;
        if( ( bflags & RUP_BARRIER_FLAG_VERTICAL ) != 0 )
        {
            rupBarriers[ i ].endX = bx;
            rupBarriers[ i ].endY = by + ( blen >> 1 );
        }
        else
        {
            rupBarriers[ i ].endX = bx + ( blen >> 1 );
            rupBarriers[ i ].endY = by;
        }
    }
    for( i = count; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
      rupBarriers[ i ].flags = 0;
}

void rupDrawBarriers()
{
    int i, startX, startY, endX, endY;
    for( i = 0; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
    {
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_LIVE ) == 0 ) continue;
        rupBarriers[ i ].flags = rupBarriers[ i ].flags & ~( RUP_BARRIER_FLAG_START_VISIBLE | RUP_BARRIER_FLAG_END_VISIBLE | RUP_BARRIER_FLAG_LINE_VISIBLE );

        startX = rupOffsetX( rupBarriers[ i ].startX );
        startY = rupOffsetY( rupBarriers[ i ].startY );
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_START_OVERWRAPPED ) == 0 && startX < RUP_VVRAM_WIDTH && startY < RUP_VVRAM_HEIGHT )
        {
            rupBarriers[ i ].flags = rupBarriers[ i ].flags | RUP_BARRIER_FLAG_START_VISIBLE;
            rupVVram[ startY ][ startX ] = RUP_CHAR_BARRIERHEAD;
        }

        endX = rupOffsetX( rupBarriers[ i ].endX );
        endY = rupOffsetY( rupBarriers[ i ].endY );
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_END_OVERWRAPPED ) == 0 && endX < RUP_VVRAM_WIDTH && endY < RUP_VVRAM_HEIGHT )
        {
            rupBarriers[ i ].flags = rupBarriers[ i ].flags | RUP_BARRIER_FLAG_END_VISIBLE;
            rupVVram[ endY ][ endX ] = RUP_CHAR_BARRIERHEAD;
        }

        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_VERTICAL ) != 0 )
        {
            startY = startY + 1;
            if( startX < RUP_VVRAM_WIDTH )
            {
                if( startY < RUP_VVRAM_HEIGHT || endY < RUP_VVRAM_HEIGHT ||
                    ( rupBarriers[ i ].startY < rupBaseY && rupBarriers[ i ].endY >= rupBaseY ) )
                {
                    if( startY < RUP_VVRAM_HEIGHT && endY < RUP_VVRAM_HEIGHT && startY > endY )
                    {
                        rupVLine( startX, 0, endY );
                        rupVLine( startX, startY, RUP_VVRAM_HEIGHT - startY );
                    }
                    else
                    {
                        if( startY >= RUP_VVRAM_HEIGHT ) startY = 0;
                        if( endY > RUP_VVRAM_HEIGHT ) endY = RUP_VVRAM_HEIGHT;
                        rupVLine( startX, startY, endY - startY );
                    }
                    rupBarriers[ i ].flags = rupBarriers[ i ].flags | RUP_BARRIER_FLAG_LINE_VISIBLE;
                }
            }
        }
        else
        {
            startX = startX + 1;
            if( startY < RUP_VVRAM_HEIGHT )
            {
                if( startX < RUP_VVRAM_WIDTH || endX < RUP_VVRAM_WIDTH ||
                    ( rupBarriers[ i ].startX < rupBaseX && rupBarriers[ i ].endX >= rupBaseX ) )
                {
                    if( startX < RUP_VVRAM_WIDTH && endX < RUP_VVRAM_WIDTH && startX > endX )
                    {
                        rupHLine( 0, startY, endX );
                        rupHLine( startX, startY, RUP_VVRAM_WIDTH - startX );
                    }
                    else
                    {
                        if( startX >= RUP_VVRAM_WIDTH ) startX = 0;
                        if( endX > RUP_VVRAM_WIDTH ) endX = RUP_VVRAM_WIDTH;
                        rupHLine( startX, startY, endX - startX );
                    }
                    rupBarriers[ i ].flags = rupBarriers[ i ].flags | RUP_BARRIER_FLAG_LINE_VISIBLE;
                }
            }
        }
    }
}

void rupBarrierDestroyOverwrapped( int x, int y )
{
    int i;
    for( i = 0; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
    {
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_LIVE ) == 0 ) continue;
        if( ( rupBarriers[ i ].startX == x && rupBarriers[ i ].startY == y ) ||
            ( rupBarriers[ i ].endX == x && rupBarriers[ i ].endY == y ) )
          rupBarriers[ i ].flags = 0;
    }
}

void rupBarrierShowOverwrapped( int x, int y )
{
    int i;
    for( i = 0; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
    {
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_LIVE ) == 0 ) continue;
        if( rupBarriers[ i ].startX == x && rupBarriers[ i ].startY == y )
          rupBarriers[ i ].flags = rupBarriers[ i ].flags & ~RUP_BARRIER_FLAG_START_OVERWRAPPED;
        if( rupBarriers[ i ].endX == x && rupBarriers[ i ].endY == y )
          rupBarriers[ i ].flags = rupBarriers[ i ].flags & ~RUP_BARRIER_FLAG_END_OVERWRAPPED;
    }
}

bool rupBarrierHit( int idx, int thisX, int thisY, int x, int y, int size )
{
    if( rupHitMover( thisX, thisY, 0, x, y, size ) )
    {
        rupStartBang( thisX, thisY, false );
        rupSoundSmallBang();
        rupBarriers[ idx ].flags = 0;
        rupBarrierDestroyOverwrapped( thisX, thisY );
        rupAddScore( 5 );
        return true;
    }
    return false;
}

bool rupBarrierNear1( int idx, int x, int y, int size )
{
    // matches upstream's own NearBarrier1(), whose `size` param is likewise
    // never actually used in the body - self-assigned to silence the
    // unused-parameter warning (this dialect has no (void)param; idiom).
    size = size;
    if( ( rupBarriers[ idx ].flags & RUP_BARRIER_FLAG_LINE_VISIBLE ) != 0 )
    {
        if( ( rupBarriers[ idx ].flags & RUP_BARRIER_FLAG_VERTICAL ) != 0 )
        {
            if( x == rupBarriers[ idx ].startX && y > rupBarriers[ idx ].startY && y < rupBarriers[ idx ].endY )
              return true;
        }
        else
        {
            if( y == rupBarriers[ idx ].startY && x > rupBarriers[ idx ].startX && x < rupBarriers[ idx ].endX )
              return true;
        }
    }
    return false;
}

bool rupNearBarrier( int x, int y, int size )
{
    int i;
    for( i = 0; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
    {
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_LIVE ) == 0 ) continue;
        if( rupBarrierNear1( i, x, y, size ) ) return true;
    }
    return false;
}

bool rupHitBarrier( int x, int y, int size )
{
    int i;
    for( i = 0; i < RUP_MAX_BARRIER_COUNT; i = i + 1 )
    {
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_LIVE ) == 0 ) continue;
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_START_VISIBLE ) != 0 )
        {
            if( rupBarrierHit( i, rupBarriers[ i ].startX, rupBarriers[ i ].startY, x, y, size ) )
            {
                rupBarrierShowOverwrapped( rupBarriers[ i ].endX, rupBarriers[ i ].endY );
                return true;
            }
        }
        if( ( rupBarriers[ i ].flags & RUP_BARRIER_FLAG_END_VISIBLE ) != 0 )
        {
            if( rupBarrierHit( i, rupBarriers[ i ].endX, rupBarriers[ i ].endY, x, y, size ) )
            {
                rupBarrierShowOverwrapped( rupBarriers[ i ].startX, rupBarriers[ i ].startY );
                return true;
            }
        }
        if( rupBarrierNear1( i, x, y, size ) ) return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Star.cpp
// -----------------------------------------------------------------------------

void rupDrawStars()
{
    int rangeY, rangeX, baseX, baseY, i, x, y;
    rangeY = RUP_VVRAM_HEIGHT * 2;
    rangeX = RUP_VVRAM_WIDTH * 2;
    baseX = rupFighterX;
    baseY = rupFighterY;
    while( baseY > rangeY )
      baseY = baseY - rangeY;
    for( i = 0; i < 8; i = i + 1 )
    {
        x = rupWrapByte( rupStars[ i ][ 0 ] - baseX );
        while( x >= rangeX )
          x = x - rangeX;
        y = rupWrapByte( rupStars[ i ][ 1 ] + rangeY - baseY );
        while( y >= rangeY )
          y = y - rangeY;
        x = x >> 1;
        y = y >> 1;
        rupVPutXY( x, y, RUP_CHAR_STAR );
    }
}


// -----------------------------------------------------------------------------
//   Fighter.cpp - the fire-button handling that upstream's own
//   ControlFighter() calls directly (StartFighterBullet) is deliberately
//   NOT called from rupControlFighter() here - see this file's own header
//   comment ("Circular module dependencies") for why that one call was
//   moved out to the shared per-tick driver instead.
// -----------------------------------------------------------------------------

void rupShowFighter()
{
    rupShowSprite( RUP_SPRITE_FIGHTER, RUP_FIXED_X >> 3, RUP_FIXED_Y >> 3, RUP_CHAR_FIGHTER + ( rupFighterDirection << 2 ) );
}

void rupInitFighter( int x, int y )
{
    rupFighterX = x >> 1;
    rupFighterY = y >> 1;
    rupFighterDirection = 0;
    rupFighterNumerator = 100;
    rupFighterDenominator = 0;
    rupFighterDyingCount = -1;
    rupUpdateBasePosition();
    rupShowFighter();
}

void rupFighterDestroy()
{
    rupSoundLargeBang();
    rupStartBang( rupFighterX, rupFighterY, true );
    rupHideSprite( RUP_SPRITE_FIGHTER );
    rupFighterDyingCount = 30;
}

bool rupFighterHitCheck()
{
    if( rupHitBarrier( rupFighterX, rupFighterY, RUP_FIGHTER_HALFSIZE ) )
    {
        rupFighterDestroy();
        return true;
    }
    if( rupHitFort( rupFighterX, rupFighterY, RUP_FIGHTER_HALFSIZE ) )
    {
        rupFighterDestroy();
        return true;
    }
    if( rupHitItem( rupFighterX, rupFighterY, RUP_FIGHTER_HALFSIZE ) )
    {
        rupSoundUp();
        rupRemainCount = rupRemainCount + 1;
        rupPrintRemain();
    }
    return false;
}

void rupControlFighter()
{
    bool left, right, up, down;
    int keyDirection;

    if( rupFighterDyingCount >= 0 ) return;
    left = isLeftPressed();
    right = isRightPressed();
    up = isUpPressed();
    down = isDownPressed();

    if( up )
    {
        if( right ) keyDirection = RUP_DIR_UPRIGHT;
        else if( left ) keyDirection = RUP_DIR_UPLEFT;
        else keyDirection = RUP_DIR_UP;
    }
    else if( down )
    {
        if( right ) keyDirection = RUP_DIR_DOWNRIGHT;
        else if( left ) keyDirection = RUP_DIR_DOWNLEFT;
        else keyDirection = RUP_DIR_DOWN;
    }
    else if( right )
      keyDirection = RUP_DIR_RIGHT;
    else if( left )
      keyDirection = RUP_DIR_LEFT;
    else
      keyDirection = rupFighterDirection;

    rupFighterDirection = rupNewDirection( rupFighterDirection, keyDirection );
    if( ( rupFighterDirection & 1 ) == 0 )
      rupFighterNumerator = 100;
    else
      rupFighterNumerator = 70;
    rupShowFighter();
}

void rupMoveFighter()
{
    int dx, dy;
    if( rupFighterDyingCount >= 0 )
    {
        rupFighterDyingCount = rupFighterDyingCount - 1;
        return;
    }
    if( ( rupClock & 7 ) != 0 ) return;
    rupFighterDenominator = rupFighterDenominator - rupFighterNumerator;
    if( rupFighterDenominator < 0 )
    {
        rupDrawFighterOnRader();
        dx = rupDirectionsDx[ rupFighterDirection ];
        dy = rupDirectionsDy[ rupFighterDirection ];
        rupFighterDx = dx;
        rupFighterDy = dy;
        rupFighterX = rupAddX( rupFighterX, dx );
        rupFighterY = rupAddY( rupFighterY, dy );
        if( rupFighterHitCheck() ) return;
        rupUpdateBasePosition();
        rupShowFighter();
        rupDrawFighterOnRader();
        rupFighterDenominator = rupFighterDenominator + 100;
    }
}

bool rupHitFighter( int x, int y, int size )
{
    if( rupFighterDyingCount >= 0 ) return false;
    if( rupHitMover( rupFighterX, rupFighterY, RUP_FIGHTER_HALFSIZE, x, y, size ) )
    {
        rupFighterDestroy();
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   EnemyBullet.cpp - defined before Enemy.cpp (which calls
//   rupStartEnemyBullet()); only needs Fighter's already-defined
//   rupHitFighter() plus the RupEnemy struct TYPE (already declared),
//   not any of Enemy.cpp's own functions - see header comment.
// -----------------------------------------------------------------------------

int rupEnemyBulletLocate( RupEnemyBullet* pBullet )
{
    int x, y;
    x = rupOffsetX( pBullet->x );
    if( x >= RUP_VVRAM_WIDTH ) { rupHideSprite( pBullet->sprite ); pBullet->sprite = RUP_OBJ_NONE; return 0; }
    y = rupOffsetY( pBullet->y );
    if( y >= RUP_VVRAM_HEIGHT ) { rupHideSprite( pBullet->sprite ); pBullet->sprite = RUP_OBJ_NONE; return 0; }
    rupShowSprite( pBullet->sprite, x, y, RUP_CHAR_ENEMYBULLET );
    return 1;
}

void rupInitEnemyBullets()
{
    int i;
    for( i = 0; i < RUP_EBULLET_MAX; i = i + 1 )
      rupEnemyBullets[ i ].sprite = RUP_OBJ_NONE;
}

bool rupStartEnemyBullet( RupEnemy* pEnemy )
{
    int i, sprite, rnd, enemyDx, enemyDy;
    sprite = RUP_SPRITE_ENEMYBULLET;
    for( i = 0; i < RUP_EBULLET_MAX; i = i + 1 )
    {
        if( rupEnemyBullets[ i ].sprite == RUP_OBJ_NONE )
        {
            rnd = rupRnd() & 3;
            if( rnd == 3 ) rnd = 0;
            enemyDx = rupDirectionsDx[ pEnemy->direction ];
            enemyDy = rupDirectionsDy[ pEnemy->direction ];
            if( enemyDx != 0 )
            {
                if( enemyDy != 0 )
                {
                    rupEnemyBullets[ i ].dx = enemyDx;
                    rupEnemyBullets[ i ].dy = enemyDy;
                    if( rnd == 1 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_SHORTVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_LONGVEL; }
                    else if( rnd == 2 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_LONGVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_SHORTVEL; }
                    else { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_LOVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_LOVEL; }
                }
                else
                {
                    rupEnemyBullets[ i ].dx = enemyDx;
                    if( rnd == 1 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_LONGVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_SHORTVEL; rupEnemyBullets[ i ].dy = -1; }
                    else if( rnd == 2 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_LONGVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_SHORTVEL; rupEnemyBullets[ i ].dy = 1; }
                    else { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_HIVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_HIVEL; rupEnemyBullets[ i ].dy = 0; }
                }
            }
            else
            {
                rupEnemyBullets[ i ].dy = enemyDy;
                if( rnd == 1 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_SHORTVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_LONGVEL; rupEnemyBullets[ i ].dx = -1; }
                else if( rnd == 2 ) { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_SHORTVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_LONGVEL; rupEnemyBullets[ i ].dx = 1; }
                else { rupEnemyBullets[ i ].numeratorX = RUP_EBULLET_HIVEL; rupEnemyBullets[ i ].numeratorY = RUP_EBULLET_HIVEL; rupEnemyBullets[ i ].dx = 0; }
            }
            rupEnemyBullets[ i ].x = rupWrapByte( pEnemy->x + enemyDx );
            rupEnemyBullets[ i ].y = rupWrapByte( pEnemy->y + enemyDy );
            rupEnemyBullets[ i ].sprite = sprite;
            rupEnemyBullets[ i ].denominatorX = 0;
            rupEnemyBullets[ i ].denominatorY = 0;
            return rupEnemyBulletLocate( &rupEnemyBullets[ i ] ) != 0;
        }
        sprite = sprite + 1;
    }
    return false;
}

bool rupEnemyBulletHit( RupEnemyBullet* pBullet )
{
    return rupHitFighter( pBullet->x, pBullet->y, 0 );
}

void rupMoveEnemyBullets()
{
    int i;
    RupEnemyBullet* b;
    for( i = 0; i < RUP_EBULLET_MAX; i = i + 1 )
    {
        b = &rupEnemyBullets[ i ];
        if( b->sprite == RUP_OBJ_NONE ) continue;
        if( ( rupClock & 1 ) == 0 )
        {
            b->denominatorX = b->denominatorX - b->numeratorX;
            if( b->denominatorX < 0 )
            {
                b->x = rupAddX( b->x, b->dx );
                b->denominatorX = b->denominatorX + 100;
            }
            b->denominatorY = b->denominatorY - b->numeratorY;
            if( b->denominatorY < 0 )
            {
                b->y = rupAddY( b->y, b->dy );
                b->denominatorY = b->denominatorY + 100;
            }
        }
        if( rupEnemyBulletHit( b ) )
        {
            rupHideSprite( b->sprite );
            b->sprite = RUP_OBJ_NONE;
            continue;
        }
        rupEnemyBulletLocate( b );
    }
}


// -----------------------------------------------------------------------------
//   Enemy.cpp - needs Fighter's rupHitFighter() and EnemyBullet's
//   rupStartEnemyBullet(), both already defined above.
// -----------------------------------------------------------------------------

void rupInitEnemies()
{
    // Upstream's own InitEnemies() also resets RndIndex = 0 - called via
    // InitTrying() on EVERY new stage and every life-retry, not just once
    // at cartridge boot. Missing this made rupEnemyRndIndex just keep
    // incrementing across the whole session instead of restarting the
    // 16-entry Rnd() cycle at the same point every stage/retry, matching
    // upstream's real enemy-spawn-position/direction cadence.
    int i;
    rupEnemyCount = RUP_ENEMY_MAX;
    for( i = 0; i < RUP_ENEMY_MAX; i = i + 1 )
      rupEnemies[ i ].sprite = RUP_OBJ_NONE;
    rupEnemyRndIndex = 0;
}

void rupEnemyLocate( RupEnemy* pEnemy )
{
    int x, y;
    x = rupOffsetX( pEnemy->x );
    if( x + RUP_VVRAM_WIDTH / 4 >= RUP_VVRAM_WIDTH + RUP_VVRAM_WIDTH / 2 )
    {
        rupHideSprite( pEnemy->sprite );
        pEnemy->sprite = RUP_OBJ_NONE;
        return;
    }
    y = rupOffsetY( pEnemy->y );
    if( y + RUP_VVRAM_WIDTH / 4 >= RUP_VVRAM_HEIGHT + RUP_VVRAM_WIDTH / 2 )
    {
        rupHideSprite( pEnemy->sprite );
        pEnemy->sprite = RUP_OBJ_NONE;
        return;
    }
    x = rupWrapByte( x - 1 );
    y = rupWrapByte( y - 1 );
    if( x < RUP_VVRAM_WIDTH - 2 && y < RUP_VVRAM_HEIGHT - 2 )
      rupShowSprite( pEnemy->sprite, x, y, RUP_CHAR_ENEMY + ( pEnemy->direction << 2 ) );
    else
      rupHideSprite( pEnemy->sprite );
}

void rupStartEnemy()
{
    int i, sprite, x, y, direction, rnd;
    if( ( rupClock & 0x3f ) != 0 ) return;
    if( ( rupRnd() << 2 ) >= rupCurrentStage + 1 ) return;

    sprite = RUP_SPRITE_ENEMY;
    for( i = 0; i < RUP_ENEMY_MAX; i = i + 1 )
    {
        if( rupEnemies[ i ].sprite != RUP_OBJ_NONE ) { sprite = sprite + 1; continue; }

        rnd = ( rupWrapSByte( rupRnd() ) - 8 ) >> 1;
        if( rupFighterDy < 0 )
        {
            x = rupAddX( rupFighterX, rnd );
            y = rupBaseY;
            direction = RUP_DIR_DOWN;
        }
        else if( rupFighterDy > 0 )
        {
            x = rupAddX( rupFighterX, rnd );
            y = rupAddY( rupBaseY, RUP_VVRAM_HEIGHT );
            direction = RUP_DIR_UP;
        }
        else if( rupFighterDx < 0 )
        {
            x = rupBaseX;
            y = rupAddY( rupFighterY, rnd );
            direction = RUP_DIR_RIGHT;
        }
        else
        {
            x = rupAddX( rupBaseX, RUP_VVRAM_WIDTH );
            y = rupAddY( rupFighterY, rnd );
            direction = RUP_DIR_LEFT;
        }
        if( rupNearFort( x, y, 5 / 2 ) != NULL || rupNearBarrier( x, y, 5 / 2 ) ) return;

        rupEnemies[ i ].x = x;
        rupEnemies[ i ].y = y;
        rupEnemies[ i ].direction = direction;
        rupEnemies[ i ].sprite = sprite;
        rupEnemies[ i ].numerator = RUP_ENEMY_HIVEL;
        rupEnemies[ i ].denominator = 0;
        rupEnemies[ i ].bulletCount = 0;
        rupEnemyLocate( &rupEnemies[ i ] );
        return;
    }
}

void rupEnemyDestroy( RupEnemy* pEnemy )
{
    rupSoundSmallBang();
    rupStartBang( pEnemy->x, pEnemy->y, false );
    rupHideSprite( pEnemy->sprite );
    pEnemy->sprite = RUP_OBJ_NONE;
    rupAddScore( 10 );
    if( ( rupRnd() & 7 ) == 0 )
      rupStartItem( pEnemy->x, pEnemy->y );
}

bool rupEnemyHitCheck( RupEnemy* pEnemy )
{
    if( rupHitFighter( pEnemy->x, pEnemy->y, 0 ) )
    {
        rupEnemyDestroy( pEnemy );
        return true;
    }
    return false;
}

void rupMoveEnemy()
{
    int i, dx, dy, direction, attacking;
    RupEnemy* pEnemy;
    for( i = 0; i < RUP_ENEMY_MAX; i = i + 1 )
    {
        pEnemy = &rupEnemies[ i ];
        if( pEnemy->sprite == RUP_OBJ_NONE ) continue;
        if( ( rupClock & 7 ) == 0 )
        {
            pEnemy->denominator = pEnemy->denominator - pEnemy->numerator;
            if( pEnemy->denominator >= 0 )
            {
                rupEnemyLocate( pEnemy );
                continue;
            }
            if( rupEnemyHitCheck( pEnemy ) ) continue;
            dx = rupDirectionsDx[ pEnemy->direction ];
            dy = rupDirectionsDy[ pEnemy->direction ];
            pEnemy->x = rupAddX( pEnemy->x, dx );
            pEnemy->y = rupAddY( pEnemy->y, dy );
            pEnemy->denominator = pEnemy->denominator + 100;
        }
        attacking = pEnemy->bulletCount <= rupCurrentStage;

        // rupClock & 3 == 0 whenever this whole function is even called
        // (gated by rupUpdatePlaying() below), so rupClock is always a
        // multiple of 4, and (rupClock&1)==0 is always true here in
        // practice - kept as a literal, harmless check matching upstream's
        // own exact structure rather than assumed dead and dropped.
        dx = 0; dy = 0; direction = pEnemy->direction;
        if( ( rupClock & 1 ) == 0 )
        {
            dx = rupSubX( rupFighterX, pEnemy->x );
            dy = rupSubY( rupFighterY, pEnemy->y );
            if( !attacking ) { dx = -dx; dy = -dy; }
            if( dx < 0 )
            {
                if( dy < 0 ) direction = RUP_DIR_UPLEFT;
                else if( dy == 0 ) direction = RUP_DIR_LEFT;
                else direction = RUP_DIR_DOWNLEFT;
            }
            else if( dx == 0 )
            {
                if( dy < 0 ) direction = RUP_DIR_UP;
                else direction = RUP_DIR_DOWN;
            }
            else
            {
                if( dy < 0 ) direction = RUP_DIR_UPRIGHT;
                else if( dy == 0 ) direction = RUP_DIR_RIGHT;
                else direction = RUP_DIR_DOWNRIGHT;
            }
        }
        pEnemy->direction = rupNewDirection( pEnemy->direction, direction );
        if( ( direction & 1 ) == 0 )
          pEnemy->numerator = RUP_ENEMY_HIVEL;
        else
          pEnemy->numerator = RUP_ENEMY_LOVEL;

        if( attacking && ( rupRnd() << 1 ) <= rupCurrentStage )
        {
            if( rupStartEnemyBullet( pEnemy ) )
              pEnemy->bulletCount = pEnemy->bulletCount + 1;
        }
        if( rupEnemyHitCheck( pEnemy ) ) continue;
        rupEnemyLocate( pEnemy );
    }
}

bool rupHitEnemy( int x, int y, int size )
{
    int i;
    for( i = 0; i < RUP_ENEMY_MAX; i = i + 1 )
    {
        if( rupEnemies[ i ].sprite == RUP_OBJ_NONE ) continue;
        if( rupHitMover( rupEnemies[ i ].x, rupEnemies[ i ].y, 1, x, y, size ) )
        {
            rupEnemyDestroy( &rupEnemies[ i ] );
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   FighterBullet.cpp - needs Enemy's rupHitEnemy() plus Fort/Barrier's
//   own already-defined hit functions.
// -----------------------------------------------------------------------------

void rupInitFighterBullets()
{
    int i;
    for( i = 0; i < RUP_FBULLET_MAX; i = i + 1 )
      rupFighterBullets[ i ].sprite = RUP_OBJ_NONE;
    rupFighterBulletIntervalCount = RUP_FBULLET_INTERVAL;
}

void rupFighterBulletLocate( RupFighterBullet* pBullet )
{
    int x, y;
    x = rupOffsetX( pBullet->x );
    if( x >= RUP_VVRAM_WIDTH ) { rupHideSprite( pBullet->sprite ); pBullet->sprite = RUP_OBJ_NONE; return; }
    y = rupOffsetY( pBullet->y );
    if( y >= RUP_VVRAM_HEIGHT ) { rupHideSprite( pBullet->sprite ); pBullet->sprite = RUP_OBJ_NONE; return; }
    rupShowSprite( pBullet->sprite, x, y, pBullet->code );
}

bool rupFighterBulletFire( int direction )
{
    int i, sprite, dx, dy, bdx, bdy;
    direction = direction & 7;
    dx = rupDirectionsDx[ direction ];
    dy = rupDirectionsDy[ direction ];
    bdx = rupBulletOffsetsDx[ direction ];
    bdy = rupBulletOffsetsDy[ direction ];

    sprite = RUP_SPRITE_FIGHTERBULLET;
    for( i = 0; i < RUP_FBULLET_MAX; i = i + 1 )
    {
        if( rupFighterBullets[ i ].sprite == RUP_OBJ_NONE )
        {
            rupSoundFire();
            rupFighterBullets[ i ].x = rupWrapByte( rupFighterX + bdx );
            rupFighterBullets[ i ].y = rupWrapByte( rupFighterY + bdy );
            rupFighterBullets[ i ].dx = dx;
            rupFighterBullets[ i ].dy = dy;
            rupFighterBullets[ i ].direction = direction;
            rupFighterBullets[ i ].code = RUP_CHAR_FIGHTERBULLET + ( direction & 3 );
            rupFighterBullets[ i ].sprite = sprite;
            if( ( direction & 1 ) == 0 )
              rupFighterBullets[ i ].numerator = RUP_FBULLET_HIVEL;
            else
              rupFighterBullets[ i ].numerator = RUP_FBULLET_LOVEL;
            rupFighterBullets[ i ].denominator = 0;
            rupFighterBulletLocate( &rupFighterBullets[ i ] );
            return true;
        }
        sprite = sprite + 1;
    }
    return false;
}

void rupFighterBulletTick( bool on )
{
    if( rupFighterBulletIntervalCount != 0 )
    {
        rupFighterBulletIntervalCount = rupFighterBulletIntervalCount - 1;
        return;
    }
    if( !on ) return;
    rupFighterBulletIntervalCount = RUP_FBULLET_INTERVAL;
    rupFighterBulletFire( rupFighterDirection );
}

bool rupFighterBulletHit( RupFighterBullet* pBullet )
{
    int x, y, dx, dy;
    dx = rupBulletOffsetsDx[ pBullet->direction ];
    dy = rupBulletOffsetsDy[ pBullet->direction ];
    x = rupWrapByte( pBullet->x - dx );
    y = rupWrapByte( pBullet->y - dy );
    if( rupHitEnemy( pBullet->x, pBullet->y, 0 ) ) return true;
    if( rupHitBarrier( x, y, 1 ) ) return true;
    if( rupHitFort( x, y, 1 ) ) return true;
    return false;
}

void rupMoveFighterBullets()
{
    int i;
    bool doHit;
    RupFighterBullet* b;
    for( i = 0; i < RUP_FBULLET_MAX; i = i + 1 )
    {
        b = &rupFighterBullets[ i ];
        if( b->sprite == RUP_OBJ_NONE ) continue;
        if( rupHitEnemy( b->x, b->y, 0 ) )
        {
            rupHideSprite( b->sprite );
            b->sprite = RUP_OBJ_NONE;
            continue;
        }
        doHit = true;
        if( ( rupClock & 1 ) == 0 )
        {
            b->denominator = b->denominator - b->numerator;
            if( b->denominator >= 0 )
              doHit = false;
            else
            {
                b->x = rupAddX( b->x, b->dx );
                b->y = rupAddY( b->y, b->dy );
                b->denominator = b->denominator + 100;
            }
        }
        if( doHit )
        {
            if( rupFighterBulletHit( b ) )
            {
                rupHideSprite( b->sprite );
                b->sprite = RUP_OBJ_NONE;
                continue;
            }
        }
        rupFighterBulletLocate( b );
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage/InitTrying, now that every module they depend on
//   (Fort/Barrier/Fighter/Enemy/EnemyBullet/FighterBullet/Bang/Item/Sprite)
//   is defined.
// -----------------------------------------------------------------------------

void rupInitStage()
{
    // upstream cycles through StageDefs[] repeatedly past CurrentStage=9
    // (the game never stops the player continuing past stage 10) -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching gameCracky.c's own crkInitStage() precedent.
    int i, j;
    i = 0;
    j = 0;
    while( i < rupCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= RUP_STAGE_COUNT )
          j = 0;
    }
    rupStageIndex = j;
    rupInitForts( rupStageIndex );
    rupInitBarriers( rupStageIndex );
}

void rupInitTrying()
{
    rupHideAllSprites();
    rupInitFighter( rupStageStartX[ rupStageIndex ], rupStageStartY[ rupStageIndex ] );
    rupInitEnemies();
    rupInitFighterBullets();
    rupInitEnemyBullets();
    rupInitBangs();
    rupInitItem();
}


// -----------------------------------------------------------------------------
//   Rendering - see this file's own header comment for the full layout
//   derivation (map area cols 0-103, status area cols 104-127 pages 0-3,
//   Rader area cols 104-127 pages 4-7).
// -----------------------------------------------------------------------------

void rupDrawAll()
{
    rupClearVVram();
    rupDrawStars();
    rupDrawForts();
    rupDrawBarriers();
    rupDrawSpritesIntoVVram();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly,
// matching gameCracky.c's own crkComposeRawByte() derivation (same
// CharPatternSize=2 upstream constant, same formula).
int rupComposeMapByte( int col, int page )
{
    int mapX, sub, upper, lower, upperByte, lowerByte;
    mapX = col / 4;
    sub = col % 4;
    upper = rupVVram[ page * 2 ][ mapX ];
    lower = rupVVram[ page * 2 + 1 ][ mapX ];
    if( sub == 0 )
    {
        upperByte = rupCharPattern[ upper * 2 + 0 ];
        lowerByte = rupCharPattern[ lower * 2 + 0 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    if( sub == 1 )
    {
        upperByte = rupCharPattern[ upper * 2 + 0 ];
        lowerByte = rupCharPattern[ lower * 2 + 0 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    if( sub == 2 )
    {
        upperByte = rupCharPattern[ upper * 2 + 1 ];
        lowerByte = rupCharPattern[ lower * 2 + 1 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    upperByte = rupCharPattern[ upper * 2 + 1 ];
    lowerByte = rupCharPattern[ lower * 2 + 1 ];
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

int rupComposeStatusByte( int col, int page )
{
    int charCol, sub, c;
    charCol = ( col - RUP_VVRAM_WIDTH * 4 ) / 4;
    sub = ( col - RUP_VVRAM_WIDTH * 4 ) % 4;
    c = rupStatusChar[ page ][ charCol ];
    return rupAsciiPattern[ c * 4 + sub ];
}

int rupComposeTitleByte( int col, int page )
{
    int charCol, sub, c;
    charCol = col / 4;
    sub = col % 4;
    c = rupTitleChar[ page ][ charCol ];
    return rupAsciiPattern[ c * 4 + sub ];
}

void rupRender()
{
    int page, col, value, i, sub;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( col < RUP_VVRAM_WIDTH * 4 )
            {
                if( rupState == RUP_STATE_TITLE )
                  // OR-combine the logo (rupVVram, drawn by rupBeginTitle()
                  // via rupComposeMapByte()) with the MINI/START/CONTINUE/
                  // credit text (rupTitleChar, via rupComposeTitleByte()) -
                  // see rupBeginTitle()'s own comment for why this is safe.
                  value = rupComposeMapByte( col, page ) | rupComposeTitleByte( col, page );
                else if( rupOverlayActive && page == rupOverlayPage &&
                         col >= rupOverlayCol * 4 && col < rupOverlayCol * 4 + rupOverlayLen * 4 )
                {
                    i = ( col - rupOverlayCol * 4 ) / 4;
                    sub = ( col - rupOverlayCol * 4 ) % 4;
                    value = rupAsciiPattern[ rupAsciiIndex( rupOverlayText[ i ] ) * 4 + sub ];
                }
                else if( page < 7 )
                  value = rupComposeMapByte( col, page );
                else
                  value = 0;
            }
            else
            {
                if( page < 4 )
                  value = rupComposeStatusByte( col, page );
                else
                  value = rupRader[ page - 4 ][ col - RUP_VVRAM_WIDTH * 4 ];
            }
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine - see this file's own header comment for why only 5
//   states are needed here (no separate lose-animation state, no time
//   limit, unlike gameCracky.c's own 8).
// -----------------------------------------------------------------------------

void rupBeginTrying()
{
    rupInitTrying();
    // ClearScreen() upstream - the status grid needs an explicit reset
    // here (not just left to whatever a previous stage/title screen wrote)
    // since rupPrintStatus() below only ever WRITES its own specific
    // cells, it never clears the whole grid first - matching the exact
    // "stale status text persisting into gameplay" lesson gameCracky.c's
    // own crkInitTrying() already documents for the identical situation.
    int i, j;
    for( i = 0; i < 4; i = i + 1 )
    {
        for( j = 0; j < 6; j = j + 1 )
          rupStatusChar[ i ][ j ] = 0;
    }
    rupOverlayActive = false;

    rupPrintStatus();
    rupClearRader();
    rupDrawFortsOnRader();
    rupDrawFighterOnRader();
    rupDrawAll();
    rupStartSeq( 1, RUP_MELODY_START );
    rupClock = 0;
    rupState = RUP_STATE_START_JINGLE;
}

void rupBeginTitle()
{
    int i, j;
    // Cleared here (not just left over from gameplay) since the real
    // logo bitmap below is now drawn directly into rupVVram, and
    // rupRender()'s own TITLE branch composes from it - matching
    // gameCracky.c's own crkBeginTitle() clearing crkVVram for the exact
    // same reason (see that file's own header comment). Before this fix,
    // the map area during RUP_STATE_TITLE never read rupVVram at all
    // (rupComposeTitleByte() alone, from rupTitleChar), so a stale
    // rupVVram was harmless - it no longer is now that the two layers
    // are OR-combined every title-screen frame (see rupRender()).
    rupClearVVram();
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < RUP_VVRAM_WIDTH; j = j + 1 )
          rupTitleChar[ i ][ j ] = 0;
    }
    for( i = 0; i < 4; i = i + 1 )
    {
        for( j = 0; j < 6; j = j + 1 )
          rupStatusChar[ i ][ j ] = 0;
    }
    rupOverlayActive = false;
    rupHideAllSprites();
    rupClearRader();
    rupPrintStatus();

    // **Restored, after gameCracky.c's own sibling fix proved the earlier
    // "simplify the title logo to plain text" reasoning was wrong for
    // that game too** (see gameCracky.c's own header comment and
    // CLAUDE.md's "A real user-supplied hardware photo overturns Cracky's
    // own title-screen design" section) - this is upstream's own real
    // 80-byte hand-drawn "RUPTUS" logo bitmap (Status.cpp's `Title()`,
    // `TitleBytes[]`/`rupTitleBytes` above), drawn directly into rupVVram
    // at its own real position: `TitleLength=5` 4x4-VVram-cell glyphs,
    // starting at `TitleLeft = (VVramWidth - 4*TitleLength) / 2 =
    // (26-20)/2 = 3` (upstream's own real `constexpr` formula, computed
    // here rather than pre-resolved, matching this file's own established
    // "keep formulas as real expressions" preference), rows 2-5 (VVram
    // rows -> real hardware pages 1-2 via VVramToVram()'s own row-pair
    // packing - see rupComposeMapByte()). It's the single biggest, most
    // prominent element on the whole title screen, not a throwaway
    // decorative detail - the earlier text substitute was a real
    // regression, not a harmless simplification.
    //
    // rupRender()'s own TITLE branch now OR-combines rupComposeMapByte()
    // (this logo) with rupComposeTitleByte() (the MINI/START/CONTINUE/
    // credit text below, still driven by rupTitleChar) - safe because the
    // two occupy entirely disjoint real hardware pages (logo: pages 1-2
    // only; text: pages 3/5/6/7 only, confirmed by each string's own
    // column argument below), the exact same reasoning already
    // established for Cracky's own crkComposeRawByte() OR-combine.
    {
        int ch, row, col, idx, titleLeft;
        titleLeft = ( RUP_VVRAM_WIDTH - 4 * 5 ) / 2;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                rupVVram[ 2 + row ][ titleLeft + ch * 4 + col ] = rupTitleBytes[ idx ];
                idx = idx + 1;
            }
    }

    // Now that the real logo (matching upstream's own actual shape) is
    // restored, MINI and the credit line are placed at upstream's own
    // real, literal columns too - directly re-derived from Status.cpp's
    // own Vram-address formulas rather than the earlier port's guessed
    // "suit the new logo" positions (col10/col7), which were only ever
    // needed because the plain-text substitute had a different shape than
    // the real logo those columns were tuned against:
    //   - MINI: page 3, col = TitleLeft + 4*TitleLength - 5 = 3+20-5 = 18
    //     (upstream: `Vram + VramRowSize*3 + (TitleLeft+4*TitleLength-5)*VramStep`)
    //   - credit "INUFUTO 2026": page 7, col 12 (upstream's own literal
    //     constant - unchanged, it was already correct)
    // START/CONTINUE (page 5/6, col `ArrowX+1`=9) and the selection arrow
    // (col `ArrowX`=8, in rupUpdateTitle() below) were already at their
    // real upstream columns and are unchanged.
    {
        int sMini[4] = { 'M', 'I', 'N', 'I' };
        int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        rupTitlePrintS( 3, 18, sMini, 4 );
        rupTitlePrintS( 5, 9, sStart, 5 );
        rupTitlePrintS( 6, 9, sContinue, 8 );
        rupTitlePrintS( 7, 12, sCredit, 12 );
    }

    rupSelection = 0;
    rupSelectionChanged = true;
    rupPrevLeft = 0; rupPrevRight = 0; rupPrevUp = 0; rupPrevDown = 0; rupPrevFire = 0;
    rupState = RUP_STATE_TITLE;
}

void rupUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !rupPrevLeft ) || ( right && !rupPrevRight ) ||
                ( up && !rupPrevUp ) || ( down && !rupPrevDown ) );
    justFire = ( fire && !rupPrevFire );
    rupPrevLeft = left; rupPrevRight = right; rupPrevUp = up; rupPrevDown = down; rupPrevFire = fire;

    if( rupSelectionChanged )
    {
        rupSelectionChanged = false;
        if( rupSelection == 0 ) rupTitlePrintC( 5, 8, '>' ); else rupTitlePrintC( 5, 8, ' ' );
        if( rupSelection == 1 ) rupTitlePrintC( 6, 8, '>' ); else rupTitlePrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        rupPendingContinue = ( rupSelection == 1 );
        rupScore = 0;
        if( !rupPendingContinue )
          rupCurrentStage = 0;
        rupRemainCount = 3;
        rupInitStage();
        rupBeginTrying();
        rupRender();
        return;
    }
    if( justDir )
    {
        rupSelection = rupSelection ^ 1;
        rupSelectionChanged = true;
    }
    rupRender();
}

void rupUpdateStartJingle()
{
    if( !rupSeqPlaying( 1 ) )
    {
        rupStartBgm();
        rupState = RUP_STATE_PLAYING;
    }
    rupRender();
}

void rupUpdateGameOverJingle()
{
    if( !rupSeqPlaying( 1 ) )
      rupBeginTitle();
    else
      rupRender();
}

void rupUpdateClearJingle()
{
    if( !rupSeqPlaying( 1 ) )
    {
        rupPrintStatus();
        rupCurrentStage = rupCurrentStage + 1;
        rupInitStage();
        rupBeginTrying();
    }
    rupRender();
}

void rupUpdatePlaying()
{
    bool fireHeld;

    rupControlFighter();
    // The one call this port deliberately moved OUT of rupControlFighter()
    // itself - see this file's own header comment ("Circular module
    // dependencies") - re-checking the exact same guard ControlFighter()'s
    // own early-return already implied, so the fire cooldown genuinely
    // pauses while dying, matching upstream.
    if( rupFighterDyingCount < 0 )
    {
        fireHeld = isFirePressed();
        rupFighterBulletTick( fireHeld );
    }

    if( ( rupClock & 3 ) == 0 )
    {
        rupMoveFighter();
        rupMoveEnemy();
        rupStartEnemy();
    }
    rupMoveFighterBullets();
    rupMoveEnemyBullets();
    rupDrawItem();

    if( rupFighterDyingCount == 0 )
    {
        rupStopBgm();
        rupRemainCount = rupRemainCount - 1;
        if( rupRemainCount != 0 )
        {
            rupBeginTrying();
            rupRender();
            return;
        }
        rupPrintGameOver();
        rupStartSeq( 1, RUP_MELODY_GAMEOVER );
        rupState = RUP_STATE_GAMEOVER_JINGLE;
        rupRender();
        return;
    }

    rupUpdateBangs();
    // Upstream only calls DrawAll()+WaitTimer(2) (the redraw) on every
    // OTHER do-while iteration (`if ((Clock&1)==0)`) - gameplay logic
    // itself (everything above this point: movement, bullets, collision)
    // still runs every tick, only the redraw is halved. This matters for
    // more than just smoothness: rupDrawForts() (called from rupDrawAll())
    // embeds a real state mutation - a destroyed fort's own post-death
    // `life--` countdown down to -13 before it's actually removed from
    // rupFortCount/flags - so calling rupDrawAll() unconditionally every
    // tick (as an earlier version of this function did) advanced that
    // countdown roughly 2x faster than upstream, making forts vanish (and,
    // for the last fort in a stage, the stage-clear transition itself
    // trigger) noticeably sooner than intended. Gated the same way
    // upstream gates it, fixing both the timing and the redraw rate at
    // once - md_beginFrame() is simply skipped on the "off" tick, so the
    // previous frame's pixels persist, the same "skip the whole render
    // call" pattern already used throughout this project's other ports.
    if( ( rupClock & 1 ) == 0 )
    {
        rupDrawAll();
        rupRender();
    }
    rupClock = rupClock + 1;

    if( rupFortCount <= 0 )
    {
        rupStopBgm();
        rupStartSeq( 1, RUP_MELODY_CLEAR );
        rupState = RUP_STATE_CLEAR_JINGLE;
        rupRender();
        return;
    }
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameRuptus_init()
{
    int i;

    rupScore = 0;
    rupCurrentStage = 0;
    rupRemainCount = 3;
    rupEnemyRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        rupSeqActive[ i ] = 0;
        rupSeqMelody[ i ] = RUP_MELODY_NONE;
    }
    rupOverlayActive = false;
    rupClock = 0;

    rupBeginTitle();
}

void gameRuptus_update()
{
    rupAdvanceSound();

    if( rupState == RUP_STATE_TITLE )
      rupUpdateTitle();
    else if( rupState == RUP_STATE_START_JINGLE )
      rupUpdateStartJingle();
    else if( rupState == RUP_STATE_PLAYING )
      rupUpdatePlaying();
    else if( rupState == RUP_STATE_GAMEOVER_JINGLE )
      rupUpdateGameOverJingle();
    else if( rupState == RUP_STATE_CLEAR_JINGLE )
      rupUpdateClearJingle();
}
