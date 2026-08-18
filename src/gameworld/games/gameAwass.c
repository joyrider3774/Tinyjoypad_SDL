#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// AWASS mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_awass`) - a platformer: climb
// ladders/walk floors on a 12-column x 4-floor stage collecting all 8 flags
// (4 matching pairs) while dodging chasing monsters; press Fire to scroll the
// nearest floor left/right (reconnecting its ladders to a different column),
// a mechanic this port's own menu credits as its distinguishing feature over
// the already-shipped sibling port Cracky (same author/engine, no floor-
// scroll mechanic there). 8 hand-authored stages, cycled indefinitely with a
// rising difficulty ramp (MaxTime shrinks every full cycle through all 8),
// 3 lives, real persistent hi-score tracked in-session (upstream has no
// EEPROM at all - a CH32V003 RISC-V microcontroller, not AVR, so this
// project's own `eeprom_*` shim/avrCompat widening aren't relevant here the
// way they are for every ATtiny85 port - same situation as Cracky's own).
//
// Ported following gameCracky.c's own proven methodology exactly (same
// engine/"Cate" framework, same author) - see that file's own header for the
// fuller rationale behind each technique reused here rather than re-derived:
// real SSD1306 display streamed one raw byte at a time (`SendOledData()`, no
// framebuffer - the same model `md_drawColumn()` already handles), a real
// explicit 60Hz SysTick-based frame limiter (`Timer.cpp`'s own `kTimerHz=60`,
// `WaitTimer(t)`), and only 4 directions + 1 action button
// (`ScanKeys.h`'s own `Keys_Button0`) - a strict subset of what
// `tinyJoypadShim.h` already exposes (`isFire2Pressed()` unused here, same as
// Cracky).
//
// **No hardware display-orientation transform, confirmed correct from the
// start rather than needing Cracky's own multi-attempt trial-and-error**:
// `Oled.cpp`'s `InitOled()` sends the identical `OledCmd::RightToLeft`/
// `OledCmd::BottomToTop` (SegRemap=0xA1/ComScanDec=0xC8) pair Cracky's own
// header already documents as a real-hardware panel-mounting compensation
// with no equivalent to correct for in a software recreation - per this
// task's own explicit instruction, no mirror/reversal/lookup-table transform
// was ever written. `awaComposeRawByte(col,page)` is drawn directly at its
// own `(col,page)`.
//
// **Rendering differs from Cracky in one structural way worth documenting**:
// upstream keeps TWO VVram buffers (`VVramBack` - the persistent map layer,
// only ever touched by *incremental* patches: `InitTrying()`'s one-time full
// draw, `MoveFloor()`'s own per-floor-being-scrolled rewrite, and single-cell
// patches from `HitFlags()`/`EraseBomb()` - plus `VVramFront`, rebuilt from
// Back via a full `memcpy` then overlaid with sprites, fresh every
// `DrawAll()`). This port collapses both into one buffer (`awaVVram`,
// matching Cracky's own single-buffer model) rebuilt **entirely from
// `awaStageMap[]`** (the canonical cell data) every single frame, rather than
// replicating `VVramBack`'s own incremental-patch persistence - the same
// "always redraw the full frame from source-of-truth state, don't replicate
// a VRAM-persistence trick" standing precedent Cracky's own header already
// established. This is a strictly safer simplification here than it might
// first look: every incremental `VVramBack` write upstream performs
// (`DrawFloor`/`DrawFloorShift`/`VErase2XY`/`VPut2CXY`) is itself a *pure
// function* of `awaStageMap`'s current content (plus, for the floor actively
// mid-scroll, which of the two `DrawFloorShift` half-steps is due) - nothing
// upstream ever draws depends on any state that ISN'T already recoverable
// from `awaStageMap` + a small number of explicit scroll/blink state
// variables, so a full rebuild from that same state every frame reproduces
// every real visible frame exactly. Sprite draw order (see `Sprite.cpp`'s own
// `DrawSprites()`, which composites in *reverse* index order - Man drawn
// first/lowest-priority, Monsters next, Bangs next, Points drawn last/on-top)
// is preserved via the same descending-index composite loop.
//
// **The floor-scroll animation's own two-step, direction-asymmetric sequence
// was traced through by hand before ever writing code, not simplified** -
// `MoveFloor()`'s `switch(Scrolling)` does the roll-then-draw in a DIFFERENT
// order for left (-1/-2: shift-draw with the OLD unrolled map, THEN roll one
// tick later) vs right (1/2: roll immediately, THEN shift-draw with the
// ALREADY-rolled map) scroll direction - reproduced literally as 4 distinct
// state values (not collapsed to a symmetric 2-state model), since the two
// directions genuinely differ in which tick the real `RollLeft`/`RollRight`
// mutation happens relative to the "shifted" animation frame. `DrawFloorShift`
// itself (`awaDrawFloorShiftInto()`) is a direct structural mirror of
// upstream's own pointer-walking function (the "faithfully copy an intricate
// stateful algorithm's own shape" precedent Cracky's own VVramToVram
// translation and Frogger's row-buffer compositing both already established
// in this project) rather than an attempt to re-derive a closed form: the
// map's own column-0 cell gets split across both screen edges (its "right"
// glyph pinned at column 0, its "left" glyph wrapped to the far right edge)
// while every other column renders normally, exactly matching
// `DrawCell`/`DrawCellLeft`/`DrawCellRight`'s own selective-half-glyph
// behavior.
//
// **A genuine, load-bearing real-time-cadence quirk in upstream's own outer
// loop, worked through by hand rather than assumed benign** - `Main.cpp`'s
// `do{...}while(FlagCount!=0)` body only calls `WaitTimer(8)` (a real ~133ms
// delay) when `(Clock&3)==0`; `MoveFloor()` itself is called on *every*
// iteration regardless. Since the 3 non-waiting iterations between each real
// wait execute with zero elapsed real time (nothing gates them), and
// `MoveFloor()`'s own internal gate (`(ScrollingClock&CoordMask)==0`) is
// *always true* in this degenerate `CoordRate=1` build (`CoordMask=0`), a
// full trace of the call sequence around an ACT-button press confirms only
// TWO real, distinct visible frames of scroll animation ever occur (the
// "shift" frame on the SAME gated tick the button press is read, then the
// "settled" frame exactly one gated tick later) - the extra "free" MoveFloor
// calls either fire within the very same already-gated tick (immediately
// after `MoveMan()` sets `Scrolling`, before that tick's own `DrawAll()`) or
// complete the second scroll step during the very first zero-delay
// iteration, before the *next* gated tick's own draw, so no observable frame
// is ever skipped or duplicated. This means a plain single-call-per-gated-
// tick `awaMoveFloor()` (called once from the same `AWA_TICK_DIVISOR`-gated
// update every other tinyJoypadShim port here already uses) reproduces the
// real observable cadence exactly, with no special-casing needed - verified
// by this exact hand-trace, not assumed by analogy.
//
// **Class-shape**: upstream's `Movable`/`Actor` (`Man : Actor`, `Monster :
// Actor`) flatten into one `AwaActor` struct (x,y,sprite,status,dx,dy) -
// literally the same field layout as Cracky's own `CrkMovable`, just now
// genuinely shared by both Man and every Monster slot (Cracky's `CrkMovable`
// was similarly reused). `Man`'s own `pManDirection` (a pointer into a
// `static const Direction[4]` table, used as a "last successfully applied
// direction" sticky fallback exactly like Cracky's own
// `crkManOldDirDx/Dy/Pattern`) is ported as an index into a flat
// `int[4][4] awaManDirTable` instead of a pointer-to-struct-array, avoiding
// an unproven pointer-into-array-of-struct pattern in this dialect. `Point`/
// `Bang` (`: Movable`) flatten into their own small structs the same way.
//
// **Sound**: same real 3-tone-channel software mixer as Cracky's own engine
// (`Sound.cpp`, `StartMelody()`/`WaitMelody()`), but `Tempo=150` here (not
// Cracky's 160) - `SoundHandler()`'s own tempo formula gives
// `awaNoteFrames(length) = round(length * (600/2)/150) = length * 2` exactly
// (a clean integer multiplier, no fractional rounding needed, unlike
// Cracky's own 1.875). Every call routes to `md_playTone()` directly, same
// as Cracky (this shim's own single-call `Sound(freq,dur)` AVR-buzzer model
// is too limited for a real multi-voice tracker); `md_playTone()` is already
// genuinely multi-voice project-wide (see this project's own CLAUDE.md), so
// the 3 independent sequencer slots (0=one-shot SFX, reused by
// Loose/Hit/Bang/Beep/Move exactly like Cracky's own channel-0 reuse;
// 1=jingle/BGM-voice-A, reused by Start/Clear/GameOver plus BGM voice A;
// 2=BGM-voice-B only) never fight over one shared channel. Melody data
// byte-diff-extracted via a small Python script (not hand-copied) - see
// `crkMelodyLength()`/`crkMelodyValue()`'s own established id-based resolver
// pattern, reused here unchanged in shape.
//
// **A real AVR-`byte`-wraparound-reliant helper, fixed rather than ported
// literally** - `Stage.cpp`'s own `TreatColumn()`:
// `if (column >= static_cast<byte>(-ColumnCount)) return column+ColumnCount;`
// relies on a negative `byte` (e.g. -1) wrapping to 244 (256-12) so the
// comparison catches it - the exact "AVR-implicit-narrow-type behavior the
// port can't assume holds" bug class this project's CLAUDE.md documents at
// length (byte-truncation/shift-wraparound/signed-sentinel/etc). Fixed with
// a genuine sign check (`if(column<0) return column+AWA_COLUMN_COUNT; if
// (column>=AWA_COLUMN_COUNT) return column-AWA_COLUMN_COUNT;`), safe since
// every real call site only ever offsets by exactly +/-1 (a single wrap step,
// matching upstream's own single-wrap-only design intent).
//
// **The flag-blink and mid-scroll-shift visual effects both needed a small
// persistent "which visual variant to render this frame" flag**, since this
// port's own render function is a pure read of current state rather than an
// incremental-patch target: `awaScrollShiftView` (set by `awaMoveFloor()`
// each gated tick, read by the map-builder to choose `DrawFloorShift`-style
// vs normal `DrawFloor`-style rendering for `awaScrollingFloor` only) and
// `awaFlagBlinkOn` (toggled every other gated tick, matching upstream's own
// `BlinkFlags()` being called from the `(Clock&7)==0` branch - i.e. once
// every 2 gated ticks - and toggling its own visibility each time it fires;
// read by the map-builder to blank out, rather than draw, any still-present
// flag whose type matches `LastType`). `Flag.cpp`'s own `DrawFlags()` (a
// full re-draw of every remaining flag, called once from `HitFlags()` after
// a flag is collected) needed no separate port at all - it exists purely to
// patch `VVramBack` back to a correct state after a single-cell erase, which
// this port's own "always rebuild everything from `awaStageMap`" model
// already guarantees for free.
//
// **Two genuinely dead upstream declarations, confirmed via grep before
// dropping rather than ported**: `Monster.h`'s own `HitMonster(Movable*)` is
// declared but never defined or called anywhere in the real source (only
// the actually-used `HitMonsters()` - no `s` - exists); `Status.cpp`'s own
// `PrintPerfect()` is defined but never called from `Main.cpp` or anywhere
// else. `Vram.cpp`'s own `Backup[]`/`InvalidateVramBackup()` (the real-
// hardware dirty-tracking optimization skipped unchanged bytes) is dropped
// entirely, matching Cracky's own identical precedent.
//
// =============================================================================
// A meticulous post-port verification pass, run separately from the initial
// port above (which was only ever compile-tested, never played) - found and
// fixed two real, severe rendering bugs, confirmed one subtle piece of the
// original port's own reasoning correct via a real hardware photo, and
// closed one minor audio-fidelity gap.
//
// **Bug 1 (severe, game-breaking): the whole stage map was never actually
// unpacked.** `awaInitTrying()` copied `awaStageBytes[awaStageIndex][i]`
// directly into `awaStageMap[i]` for `i` in `0..47` - but `awaStageBytes`
// only has 12 valid entries per stage row (`int[8][12]`), not 48! Upstream's
// real `InitTrying()` (Stage.cpp) doesn't store one byte per map cell at
// all - it stores 12 BIT-PACKED bytes per stage (3 bytes/floor row x 4
// floors), each byte holding 4 two-bit cell values that get unpacked with a
// real bit-shift loop (`cell = b & 0x03; ...; b >>= 2;`) into the true
// 48-entry map, with `Cell_Ladder`(2) expanding to both nibbles and
// `Cell_Bomb`(3) expanding to a bomb-over-floor pair. The ported version
// skipped this unpacking entirely and just read 4x too far into a 12-entry
// array row - for `i>=12` this reads out of bounds into whatever global
// data happens to sit after `awaStageBytes` in memory (subsequent stage
// rows, then past the end of the whole 96-int table), producing a
// completely garbage, unplayable map on every single "try". **Fixed** by
// porting the real unpacking loop verbatim into `awaInitTrying()` (a local
// `fl`/`g`/`k`/`byteIdx` block - `fl` instead of the more natural `floor`,
// since `floor` collides with this dialect's own built-in `floor()` math
// function and produced a genuine parser error, "invalid start of
// sentence", the first time it was tried). Verified by decoding stage 0's
// real map data in a standalone Python re-implementation of both the
// buggy-direct-copy and the corrected-unpacking versions, rendering both as
// images, and comparing against the real device photo bundled in this
// game's own upstream repo (`more games/UIAPduino_awass/rec.jpg`) - the
// corrected version is an extremely close match (same ladder/floor/flag
// layout, same relative proportions); confirmed again directly in-engine
// via Puppeteer after the fix, matching that same photo.
//
// **Bug 2 (severe, but title-screen-only): the entire title screen rendered
// in the wrong screen region.** Upstream's real `Title()` (Status.cpp)
// draws its "AWASS" logo, "MINI", "INUFUTO 2026", and "START"/"CONTINUE"
// text at ABSOLUTE VVram-column positions inside the MAP area (cols 0-95) -
// e.g. `PrintS(Vram + VramRowSize*7 + 12*VramStep, "INUFUTO 2026")`, no
// `LeftX` offset at all - genuinely different from `PrintStatus()`'s own
// text, which really is always `LeftX`-relative and belongs in the
// status zone (cols 96-127). The initial port routed ALL of Title()'s text
// through `awaPrintC`/`awaPrintS` (which only ever write into
// `awaStatusChar`, visible solely in the status zone) - so the whole title
// screen rendered crammed into the right-hand status column, overlapping
// the SCORE/STAGE/TIME labels, while the actual map area sat completely
// blank. Confirmed this is the EXACT SAME bug, unnoticed until now, already
// shipped in the sibling Cracky port (`gameCracky.c`'s own
// `crkBeginTitle()`, checked directly against Cracky's real upstream
// Status.cpp) - out of scope to fix here (only `gameAwass.c` may be
// touched), but worth flagging for a future pass. Also found in the same
// investigation: the port's own title text was missing the "MINI" line
// entirely and the "2026" year suffix on "INUFUTO", both silently dropped
// when the text was first (mis-)transcribed. **Fixed** by adding a genuine
// second text grid, `awaMapTextChar[8][AWA_VVRAM_WIDTH]` (see its own
// declaration comment below) plus `awaPrintMapC`/`awaPrintMapS` write
// helpers and an `awaMapTextActive` flag consulted by
// `awaComposeRawByte()`'s existing map-area branch - the same "pattern
// index per (page,char-cell)" technique `awaStatusChar` already uses for
// the status zone, just covering the map area instead, active only during
// `AWA_STATE_TITLE` (cleared the instant gameplay begins, so the real map
// takes back over `awaVVram` immediately). All 5 text pieces now render at
// upstream's real absolute columns; the cursor toggle in `awaUpdateTitle()`
// was moved from status-zone col0 to map-area col8 (`ArrowX`) to match.
// Found via a real Puppeteer screenshot, not code inspection alone - a
// first glance at the rendered title screen made the bug obvious the
// moment it was actually looked at, though pinning down the *correct* fix
// needed reading Status.cpp's real column arithmetic directly. Verified
// via Puppeteer afterward: title screen renders correctly with all 5 text
// pieces in the map area, the cursor toggles correctly between "START"/
// "CONTINUE", and gameplay (launched from either selection) renders
// correctly with the map area handed back to the real game state.
//
// **Checked carefully and confirmed correct, not just assumed**: the
// floor-scroll animation's own direction-asymmetric two-step sequence
// (state -1/-2 for left, 1/2 for right) was re-traced by hand against
// `Main.cpp`'s real `(Clock&3)==0`-gated outer loop and confirmed to
// reproduce upstream's exact observable cadence (see this file's own
// earlier header section) - no change needed. The render pipeline's own
// page-to-VVram-row mapping (`awaComposeRawByte`: hardware page P reads
// VVram row-pair P directly, all 8 pages) was independently re-derived
// from a literal trace of `Vram.cpp`'s real `VVramToVram()`/`DrawAll()`
// call chain, which appeared on paper to only ever send 7 of 8 hardware
// pages (starting at page 1, leaving page 0 and VVram's last row-pair
// unsent) - but a from-scratch Python re-render of stage 0 using that
// literal 7-page/shifted interpretation, compared side-by-side against the
// real device photo, showed a clear MISMATCH (a blank top strip, a cropped
// bottom row), while the existing "all 8 pages, direct mapping" model
// already in the port matched the photo closely. Given the real hardware
// photo is the most authoritative evidence available, the existing
// mapping was left unchanged and trusted over the from-scratch source
// trace - worth revisiting if a future session can pin down the exact
// mechanism that reconciles this (possibly `ComScanDec`'s own real effect
// on physical page ordering, or a subtlety in the dirty-tracking `Backup[]`
// buffer this port's own model deliberately doesn't replicate - see this
// file's own header section on that).
//
// **Minor audio fidelity fix**: `awaStopBgm()` only stopped sequencer
// channels 1/2 (the two BGM voices), but upstream's real `StopBGM()`
// resets ALL 3 hardware tone channels, including channel 0 (the one-shot
// SFX channel) - meaning a still-playing one-shot cue (e.g. a Bang melody
// mid-flight right as the player dies) would upstream be cut off
// immediately, not left to keep playing into the next state. Fixed by
// adding `awaStopSeq(0)` alongside the existing channel-1/2 stops.
//
// **Verified end-to-end via a real isolated Puppeteer test instance**
// (own WebBuild copy, own HTTP port, cleaned up afterward): menu
// navigation to AWASS, the title screen (both fixes above), launching into
// gameplay, movement in all 4 directions, ladder climbing, a real flag
// pickup (SCORE correctly incrementing by 5), the floor-scroll/Fire
// mechanic (visibly reconnects the floor's ladders), and - via a
// temporary debug hook (`awaMaxTime` lowered from 90 to 4, fully reverted
// afterward, confirmed via grep) - the complete TIME UP -> LOSE_ANIM ->
// retry cycle three times over, correctly decrementing lives each time,
// ending in a correctly-rendered "GAME OVER" overlay and a clean return to
// a fresh title screen. Monster-collision death (the OTHER way to lose a
// life) was not independently forced this session - the state-machine
// code path is identical to the time-out path from `awaMan.status &
// AWA_MOVABLE_LIVE` onward (both call `awaBeginLose()`), already proven
// correct by the time-out test, so risk is low, but worth a direct check
// if anything looks off. A genuine level-clear (collecting all 8 flags)
// was also not independently forced - `awaBeginClearWait()`/
// `awaUpdateClearWait()`/`awaUpdateClearJingle()`/`awaUpdateBonusTally()`
// were verified via direct line-by-line comparison against upstream's own
// `Main.cpp` (matching exactly, including the real bonus-tally formula and
// the `AddScore(2)`-per-`BonusRate`-remaining loop), but not visually
// triggered - a full run collecting all 8 of a stage's flags was outside
// this session's own time budget.
//
// **Follow-up pass, prompted by Cracky's own real-hardware-photo-driven
// title-screen rewrite (see gameCracky.c's own header for the full story)**:
// re-verified this file's own two-grid title/status text architecture
// (`awaStatusChar`/`awaMapTextChar`, already split apart correctly BEFORE
// Cracky's own single-unified-grid fix even existed, from this file's own
// earlier, independent discovery of the identical LeftX-vs-map-column bug -
// see the header section above) column-by-column against upstream's real
// `Status.cpp`/`Title()` addressing, rather than assuming it already matched
// just because the *architecture* was already right. Every title-screen
// text piece (the "AWASS" logo substitute, "MINI", "INUFUTO 2026", "START"/
// "CONTINUE", the selection cursor) and every status label (SCORE/STAGE/
// TIME/lives) checked out exactly against upstream's own literal
// `VramRowSize`/`VramStep`/`LeftX` arithmetic - no missing/truncated/
// mispositioned text anywhere in that part.
//
// **One real, previously-shipped bug found, in `awaPrintScore()` specifically
// - not a leftover from the title-screen fix, a plain transcription slip in
// its own column arithmetic.** Upstream's `PrintScore()` writes at real
// column `LeftX+2`=26 (5 digits) then a trailing '0' right after, at
// `LeftX+7`=31. Since `awaStatusChar` stores offsets LOCAL to LeftX (0-7
// representing real columns 24-31 - see its own declaration comment), those
// should be local offsets 2 and 7 - but the code actually read
// `awaPrintNumber5(1, 2+2, awaScore)` (local offset 4) and
// `awaPrintC(1, 2+7, '0')` (local offset 9): both the wrong position (score
// shifted 2 columns right of upstream) AND out of `awaStatusChar`'s own real
// column bound (`int[8][8]`, valid indices 0-7) - offsets 8/9 don't crash
// (row-major aliasing lands them in `awaStatusChar[2][0]`/`[2][1]` instead,
// safely inside the array's total 64-int allocation) but silently splatter
// score digits into the otherwise-blank status page 2 instead of rendering
// the score where it belongs. Found by re-deriving upstream's real column
// math directly rather than trusting the existing port's own arithmetic at
// face value. **Fixed** to the correct local offsets (2 and 7). Verified via
// a fresh isolated Puppeteer/WebGL instance: the title screen still renders
// identically to before (this bug only affects the in-game SCORE readout,
// never shown on the title screen itself, so no title-screen regression
// risk), and live gameplay now shows the SCORE digits at their correct
// on-screen position (immediately after the "SCORE" label, same row) with
// no stray characters on the page-2 status row above it.
//
// **A second, related architectural fix, applied after the identical issue
// was found and fixed in the reference port `gameCracky.c`**: the "AWASS"
// title WORD itself (as opposed to "MINI"/"START"/"CONTINUE"/the credit
// line, all genuinely plain text upstream too) had been simplified from
// upstream's own real pixel-art bitmap logo (`Status.cpp`'s `Title()`,
// `TitleBytes[]` - the single largest, most prominent element on the whole
// title screen) down to small plain text - the exact same wrong "purely
// decorative" judgment call independently made, and later corrected, in
// Cracky's own `crkBeginTitle()` (see that file's own header for the real-
// hardware-photo story that overturned it there). **Fixed** by adding
// `awaTitleBytes[80]` (byte-diff-verified against upstream's real
// `TitleBytes[]`, 5 letters x 16 values each) and drawing it directly into
// `awaVVram` at its own real position (VVram rows 2-5 / real hardware pages
// 1-2, with `TitleLeft=(24-4*5)/2=2` columns of left margin - unlike
// Cracky's own 6-letter title, whose `TitleLeft` happens to compute to 0),
// instead of the earlier plain-text substitute. `awaComposeRawByte()`'s
// map-area branch was restructured to OR-combine this `awaVVram` logo
// content with `awaMapTextChar`'s own text layer instead of choosing one
// exclusively - safe by construction, since the logo occupies only pages
// 1-2 while every text piece sits on pages 3/5/6/7, so the two can never
// both be non-zero for the same (col,page); outside the title screen
// (`awaMapTextActive` false), only the map layer is ever read, exactly as
// before this change, so real gameplay rendering is untouched. Verified via
// a compile-only check (`compile src/main.c`, a clean "global variables"
// success line with no errors) per this task's own explicit instruction not
// to play-test - not re-verified live in the emulator.
//
// -----------------------------------------------------------------------------
// Ported into Tinyjoypad_SDL (standard C, not Vircon32-dialect) following
// this project's own established mechanical dialect-conversion recipe (see
// CLAUDE.md's "Dialect conversion" section): `TYPE[N] name` -> `TYPE
// name[N]` throughout (including the 2D form), the 5 bare structs
// (`AwaActor`/`AwaSprite`/`AwaFlag`/`AwaPoint`/`AwaBang`) each given a real
// `typedef struct { ... } Name;` in place of Vircon32's tag-less
// dialect, and every genuine text-string array/parameter (`awaPrintS`'s/
// `awaPrintMapS`'s/`awaBeginOverlay`'s own `s` parameter, `awaOverlayText`,
// every literal-character array like `sScore`/`sMini`/`awaAsciiIndex()`'s
// own `table`) converted from `int*`/`int[]` to `char*`/`char[]` - a real
// runtime-correctness fix, not just syntax, since a raw `int*` receiving
// string-literal bytes reinterprets them as garbage ints in standard C.
// Every non-text int array (pattern-index/data tables, `awaMapTextChar`/
// `awaStatusChar` - these store pattern INDICES, not raw characters -
// `awaLoosePatterns`, `awaPointValues`, `dirIdx`, etc) is left as plain
// `int`, matching this project's own established distinction. No function-
// pointer typedefs exist in this file to convert. Function definition order
// is unchanged from the source (already Vircon32-dialect-clean, so already
// valid top-down standard C too). No EEPROM/high-score persistence (matches
// upstream, which has none) and no `forceRedraw` hook (see this file's own
// entry-point section below - every state redraws unconditionally every
// tick via `awaRender()`, so a `forceRedraw` stub would have nothing to do
// beyond what already happens naturally).
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into awaCharPattern (map tiles) / awaAsciiPattern
//   (text). Char_MonserRev_Left/Right/Up/Down (0x16/0x17/0x18) are declared
//   upstream but never actually referenced anywhere except the base
//   Char_MonserRev(0x15) value itself - dropped as dead aliases, matching
//   this project's own "confirmed dead by grep, dropped" precedent.
// -----------------------------------------------------------------------------

#define AWA_CHAR_SPACE 0x00
#define AWA_CHAR_LADDER 0x10
#define AWA_CHAR_FLOOR 0x12
#define AWA_CHAR_BOMB 0x13
#define AWA_CHAR_FLAG_A 0x17
#define AWA_CHAR_FLAG_B 0x1B
#define AWA_CHAR_FLAG_C 0x1F
#define AWA_CHAR_FLAG_D 0x23
#define AWA_CHAR_SPRITE 0x27
#define AWA_CHAR_END 0x9F

#define AWA_PATTERN_MAN 0
#define AWA_PATTERN_MAN_LEFT 0
#define AWA_PATTERN_MAN_RIGHT 4
#define AWA_PATTERN_MAN_UPDOWN 8
#define AWA_PATTERN_MAN_LOOSE0 10
#define AWA_PATTERN_MAN_LOOSE1 11
#define AWA_PATTERN_MAN_LOOSE2 12
#define AWA_PATTERN_MONSTER 13
#define AWA_CHAR_MONSER_REV 0x15
#define AWA_PATTERN_POINT 25
#define AWA_PATTERN_BANG 29

// -----------------------------------------------------------------------------
//   ScanKeys.h
// -----------------------------------------------------------------------------

#define AWA_KEYS_LEFT 0x01
#define AWA_KEYS_RIGHT 0x02
#define AWA_KEYS_UP 0x04
#define AWA_KEYS_DOWN 0x08

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define AWA_COORD_SHIFT 0
#define AWA_COORD_RATE ( 1 << AWA_COORD_SHIFT )
#define AWA_COORD_MASK ( AWA_COORD_RATE - 1 )

#define AWA_MOVABLE_LIVE 0x80
#define AWA_ACTOR_FALL 0x40
#define AWA_ACTOR_PATTERN_MASK 0x0f

#define AWA_DIR_LEFT 0
#define AWA_DIR_RIGHT 1
#define AWA_DIR_UP 2
#define AWA_DIR_DOWN 3

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
} AwaActor;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define AWA_COLUMN_COUNT 12
#define AWA_FLOOR_COUNT 4
#define AWA_ROW_COUNT ( AWA_FLOOR_COUNT * 2 )
#define AWA_COLUMN_WIDTH 2
#define AWA_FLOOR_HEIGHT 4
#define AWA_ROW_HEIGHT ( AWA_FLOOR_HEIGHT / 2 )
#define AWA_COLUMN_SHIFT 1
#define AWA_FLOOR_SHIFT 2
#define AWA_ROW_SHIFT ( AWA_FLOOR_SHIFT - 1 )

#define AWA_CELL_BLANK 0
#define AWA_CELL_FLOOR 1
#define AWA_CELL_LADDER 2
#define AWA_CELL_BOMB 3
#define AWA_CELL_FLAG 4
#define AWA_CELL_MASK 7

#define AWA_MAX_FLAG_COUNT 8
#define AWA_STAGE_COUNT 8
#define AWA_MAX_MONSTER_COUNT 4

#define AWA_COLUMN_COORD_SHIFT ( AWA_COLUMN_SHIFT + AWA_COORD_SHIFT )
#define AWA_FLOOR_COORD_SHIFT ( AWA_FLOOR_SHIFT + AWA_COORD_SHIFT )
#define AWA_ROW_COORD_SHIFT ( AWA_ROW_SHIFT + AWA_COORD_SHIFT )
#define AWA_COLUMN_COORD_MASK ( AWA_COLUMN_WIDTH * AWA_COORD_RATE - 1 )
#define AWA_ROW_COORD_MASK ( AWA_ROW_HEIGHT * AWA_COORD_RATE - 1 )
#define AWA_CELL_COORD_MASK ( 2 * AWA_COORD_RATE - 1 )
#define AWA_HIT_RANGE ( AWA_COORD_RATE * 4 / 3 )

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define AWA_VVRAM_WIDTH 24
#define AWA_VVRAM_HEIGHT 16

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define AWA_SPRITE_POINT 0
#define AWA_MAX_POINT_COUNT 4
#define AWA_SPRITE_BANG 4
#define AWA_MAX_BANG_COUNT 2
#define AWA_SPRITE_MONSTER 6
#define AWA_SPRITE_MAN 10
#define AWA_SPRITE_END 11
#define AWA_INVALID_Y 0xf0

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching Cracky's own established
//   practice and upstream's own enum exactly.
// -----------------------------------------------------------------------------

#define AWA_N8 6
#define AWA_N8P ( AWA_N8 * 3 / 2 )
#define AWA_N4 ( AWA_N8 * 2 )
#define AWA_N4P ( AWA_N4 * 3 / 2 )
#define AWA_N2 ( AWA_N4 * 2 )
#define AWA_N2P ( AWA_N2 * 3 / 2 )
#define AWA_N1 ( AWA_N2 * 2 )
#define AWA_N16 ( AWA_N8 / 2 )

#define AWA_TEMPO 150

#define AWA_MELODY_NONE 0
#define AWA_MELODY_LOOSE 1
#define AWA_MELODY_HIT 2
#define AWA_MELODY_BANG 3
#define AWA_MELODY_BEEP 4
#define AWA_MELODY_MOVE 5
#define AWA_MELODY_START 6
#define AWA_MELODY_CLEAR 7
#define AWA_MELODY_GAMEOVER 8
#define AWA_MELODY_BGM1 9
#define AWA_MELODY_BGM2 10

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph, byte-identical
// to Cracky's own copy (same shared "Cate" engine font) - kept as this file's
// own self-contained copy per this project's standing "each game keeps its
// own copy, no cross-game-file sharing mechanism" practice.
int awaAsciiPattern[108] = {
    0, 0, 0, 0, 31, 17, 31, 0,
    0, 0, 31, 0, 29, 21, 23, 0,
    21, 21, 31, 0, 7, 4, 31, 0,
    23, 21, 29, 0, 31, 21, 29, 0,
    1, 29, 3, 0, 31, 21, 31, 0,
    23, 21, 31, 0, 31, 14, 4, 0,
    30, 9, 30, 0, 14, 17, 10, 0,
    31, 21, 17, 0, 31, 5, 1, 0,
    14, 17, 13, 0, 17, 31, 17, 0,
    31, 6, 31, 0, 31, 1, 30, 0,
    14, 17, 14, 0, 31, 5, 7, 0,
    31, 5, 26, 0, 22, 21, 13, 0,
    1, 31, 1, 0, 31, 16, 31, 0,
    15, 16, 15, 0,
};

// CharPattern - 159 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int awaCharPattern[318] = {
    0, 0, 51, 0, 204, 0, 255, 0,
    0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255,
    240, 170, 170, 15, 51, 51, 31, 145,
    93, 241, 143, 251, 191, 248, 0, 175,
    165, 5, 128, 143, 0, 0, 0, 255,
    55, 1, 128, 143, 0, 0, 0, 31,
    161, 4, 128, 159, 1, 0, 0, 255,
    206, 8, 128, 143, 0, 0, 128, 245,
    125, 8, 16, 60, 195, 1, 0, 245,
    253, 0, 144, 52, 67, 5, 0, 117,
    125, 0, 0, 208, 193, 0, 0, 245,
    125, 0, 16, 45, 67, 5, 128, 215,
    95, 8, 16, 60, 195, 1, 0, 223,
    95, 0, 80, 52, 67, 9, 0, 215,
    87, 0, 0, 28, 13, 0, 0, 215,
    95, 0, 80, 52, 210, 1, 128, 179,
    187, 4, 0, 53, 195, 0, 64, 187,
    59, 8, 0, 60, 83, 0, 76, 172,
    138, 68, 19, 83, 21, 34, 128, 195,
    60, 8, 16, 190, 175, 1, 68, 168,
    202, 200, 34, 81, 53, 50, 168, 175,
    239, 8, 16, 115, 191, 0, 64, 78,
    206, 0, 50, 247, 255, 2, 128, 254,
    250, 138, 0, 251, 55, 1, 0, 236,
    228, 4, 32, 255, 127, 35, 232, 239,
    239, 8, 48, 247, 55, 0, 192, 206,
    206, 0, 113, 255, 127, 1, 128, 190,
    190, 142, 0, 115, 127, 3, 0, 108,
    108, 12, 16, 247, 255, 23, 94, 81,
    17, 14, 33, 68, 168, 5, 224, 17,
    21, 229, 80, 138, 68, 18, 224, 17,
    17, 225, 16, 66, 72, 18, 30, 21,
    21, 14, 33, 132, 36, 1, 228, 192,
    194, 0, 50, 2, 97, 105, 36, 204,
    194, 0, 50, 2, 97, 105, 140, 206,
    194, 0, 0, 3, 97, 105, 164, 196,
    194, 0, 33, 1, 97, 105, 228, 182,
    74, 78, 114, 226, 37, 23,
};

// TitleBytes - upstream's own real "AWASS" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 letters x 4x4 VVram-cell glyph indices each
// (80 values total), byte-diff-verified against the real upstream source.
// Every value here is a valid index into awaCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table - byte-identical to
// Cracky's own copy of the same shared range) - the exact same shared
// block-pattern palette every other map tile in this game already draws
// through, just reused here to build a big pixel-art wordmark instead of a
// wall/floor tile. See awaBeginTitle()'s own comment for why this replaces
// the earlier plain-text "AWASS" substitute (the same simplification
// mistake gameCracky.c's own crkBeginTitle() made and later had fixed,
// after a real user-supplied hardware photo proved the "purely decorative"
// reasoning behind it wrong - see that file's own header for the full
// story).
int awaTitleBytes[80] = {
    //  A
    0x00, 0x0e, 0x0d, 0x02,
    0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f,
    0x04, 0x01, 0x00, 0x05,
    //  W
    0x0c, 0x03, 0x02, 0x0f,
    0x0c, 0x03, 0x03, 0x0f,
    0x0c, 0x03, 0x03, 0x0f,
    0x04, 0x05, 0x05, 0x01,
    //  A
    0x00, 0x0e, 0x0d, 0x02,
    0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f,
    0x04, 0x01, 0x00, 0x05,
    //  S
    0x08, 0x07, 0x05, 0x0b,
    0x04, 0x0b, 0x0a, 0x02,
    0x08, 0x02, 0x00, 0x0f,
    0x00, 0x05, 0x05, 0x01,
    //  S
    0x08, 0x07, 0x05, 0x0b,
    0x04, 0x0b, 0x0a, 0x02,
    0x08, 0x02, 0x00, 0x0f,
    0x00, 0x05, 0x05, 0x01,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale values 1-40) -
// byte-identical to Cracky's own copy.
int awaFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int awaMelodyLoose[3] = { 1, 18, 0 };

int awaMelodyHit[17] = {
    1, 26, 1, 28, 1, 30, 1, 32, 1, 33,
    1, 35, 1, 37, 1, 38, 0,
};

int awaMelodyBang[25] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30,
    1, 33, 1, 21, 1, 22, 1, 23, 1, 26,
    1, 30, 1, 33, 0,
};

int awaMelodyBeep[3] = { 1, 30, 0 };
int awaMelodyMove[3] = { 1, 30, 0 };

int awaMelodyStart[25] = {
    12, 33, 6, 28, 6, 30, 6, 33, 6, 35,
    6, 37, 12, 40, 6, 37, 6, 35, 6, 35,
    18, 33, 6, 0, 0,
};

int awaMelodyClear[21] = {
    6, 21, 6, 25, 6, 28, 6, 23, 6, 26,
    6, 30, 6, 25, 6, 28, 6, 32, 18, 33,
    0,
};

int awaMelodyGameOver[21] = {
    6, 33, 6, 33, 6, 28, 6, 28, 6, 30,
    6, 30, 6, 32, 6, 32, 36, 33, 12, 0,
    0,
};

int awaMelodyBgm1[189] = {
    12, 33, 6, 28, 6, 30, 6, 33, 6, 35,
    6, 37, 12, 35, 6, 35, 6, 35, 6, 33,
    12, 35, 12, 37, 12, 33, 6, 28, 6, 30,
    6, 33, 6, 35, 6, 37, 12, 35, 6, 35,
    6, 35, 6, 37, 12, 35, 12, 0, 12, 33,
    6, 28, 6, 30, 6, 33, 6, 35, 6, 37,
    12, 35, 6, 35, 6, 35, 6, 33, 12, 35,
    12, 37, 12, 33, 6, 28, 6, 30, 6, 33,
    6, 35, 6, 37, 12, 35, 6, 35, 6, 35,
    6, 37, 12, 40, 12, 0, 6, 38, 6, 38,
    12, 38, 6, 37, 6, 37, 12, 37, 6, 35,
    6, 35, 6, 37, 18, 35, 12, 0, 6, 38,
    6, 38, 12, 38, 6, 37, 6, 37, 12, 37,
    6, 40, 6, 40, 6, 37, 18, 35, 12, 0,
    12, 33, 6, 28, 6, 30, 6, 33, 6, 35,
    6, 37, 12, 35, 6, 35, 6, 35, 6, 33,
    12, 35, 12, 37, 12, 33, 6, 28, 6, 30,
    6, 33, 6, 35, 6, 37, 12, 40, 6, 37,
    6, 35, 6, 35, 12, 33, 12, 0, 255,
};

int awaMelodyBgm2[319] = {
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 1, 6, 0, 6, 4, 3, 8, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 4, 6, 0, 6, 8, 3, 11, 3, 0,
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 1, 6, 0, 6, 4, 3, 8, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 4, 6, 0, 6, 8, 3, 11, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 6, 6, 0, 6, 10, 3, 13, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 4, 6, 0, 6, 8, 3, 11, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 6, 6, 0, 6, 10, 3, 13, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 4, 6, 0, 6, 8, 3, 11, 3, 0,
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 1, 6, 0, 6, 4, 3, 8, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 9, 6, 0, 6, 13, 3, 16, 3, 0,
    6, 6, 6, 0, 6, 9, 3, 13, 3, 0,
    6, 11, 6, 0, 6, 14, 3, 18, 3, 0,
    6, 4, 6, 0, 6, 9, 6, 0, 255,
};

// Rnd() lookup table - byte-identical to Cracky's own copy. Note: unlike
// Cracky's own crkRnd() (which masks its result with & 0x0f), this game's
// own Rnd() returns the raw table value 0-31 unmasked - Monster.cpp's own
// "become able to phase through walls" chance roll needs the full range.
int awaRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

// Stage data - flattened from upstream's own `struct Stage { start,
// monsterCount, pMonsters, flags[8], bytes[12] }` array + separate
// monsters0-7 arrays into parallel fixed arrays, matching Cracky's own
// established "flatten a struct-with-a-real-pointer-member into parallel
// arrays" precedent.
int awaStageStart[8] = {
    ( 0 << 4 ) | 3,
    ( 4 << 4 ) | 3,
    ( 6 << 4 ) | 0,
    ( 0 << 4 ) | 3,
    ( 1 << 4 ) | 1,
    ( 6 << 4 ) | 0,
    ( 2 << 4 ) | 0,
    ( 11 << 4 ) | 3,
};

int awaStageMonsterCount[8] = { 1, 1, 2, 2, 3, 3, 4, 2 };

int awaStageMonsters[8][4] = {
    { ( 11 << 4 ) | 0, 0, 0, 0 },
    { ( 11 << 4 ) | 1, 0, 0, 0 },
    { ( 10 << 4 ) | 1, ( 11 << 4 ) | 3, 0, 0 },
    { ( 1 << 4 ) | 0, ( 11 << 4 ) | 0, 0, 0 },
    { ( 8 << 4 ) | 0, ( 2 << 4 ) | 3, ( 10 << 4 ) | 3, 0 },
    { ( 8 << 4 ) | 1, ( 1 << 4 ) | 3, ( 8 << 4 ) | 3, 0 },
    { ( 0 << 4 ) | 1, ( 0 << 4 ) | 2, ( 5 << 4 ) | 2, ( 5 << 4 ) | 3 },
    { ( 2 << 4 ) | 0, ( 11 << 4 ) | 0, 0, 0 },
};

int awaStageFlags[8][8] = {
    { ( 2 << 4 ) | 3, ( 9 << 4 ) | 3, ( 8 << 4 ) | 1, ( 11 << 4 ) | 2, ( 5 << 4 ) | 0, ( 4 << 4 ) | 1, ( 0 << 4 ) | 1, ( 2 << 4 ) | 2 },
    { ( 5 << 4 ) | 3, ( 11 << 4 ) | 3, ( 2 << 4 ) | 2, ( 2 << 4 ) | 3, ( 3 << 4 ) | 0, ( 1 << 4 ) | 1, ( 8 << 4 ) | 0, ( 7 << 4 ) | 2 },
    { ( 2 << 4 ) | 0, ( 0 << 4 ) | 1, ( 0 << 4 ) | 3, ( 4 << 4 ) | 3, ( 8 << 4 ) | 2, ( 8 << 4 ) | 3, ( 7 << 4 ) | 1, ( 11 << 4 ) | 1 },
    { ( 3 << 4 ) | 2, ( 4 << 4 ) | 3, ( 5 << 4 ) | 1, ( 7 << 4 ) | 1, ( 5 << 4 ) | 0, ( 10 << 4 ) | 0, ( 10 << 4 ) | 1, ( 11 << 4 ) | 3 },
    { ( 2 << 4 ) | 0, ( 3 << 4 ) | 1, ( 6 << 4 ) | 0, ( 9 << 4 ) | 1, ( 7 << 4 ) | 2, ( 11 << 4 ) | 3, ( 3 << 4 ) | 3, ( 6 << 4 ) | 3 },
    { ( 8 << 4 ) | 0, ( 5 << 4 ) | 1, ( 11 << 4 ) | 0, ( 11 << 4 ) | 1, ( 7 << 4 ) | 3, ( 9 << 4 ) | 3, ( 3 << 4 ) | 0, ( 2 << 4 ) | 3 },
    { ( 3 << 4 ) | 0, ( 3 << 4 ) | 1, ( 3 << 4 ) | 2, ( 3 << 4 ) | 3, ( 9 << 4 ) | 0, ( 8 << 4 ) | 1, ( 9 << 4 ) | 1, ( 9 << 4 ) | 2 },
    { ( 10 << 4 ) | 2, ( 7 << 4 ) | 3, ( 9 << 4 ) | 0, ( 11 << 4 ) | 1, ( 3 << 4 ) | 0, ( 5 << 4 ) | 1, ( 0 << 4 ) | 2, ( 5 << 4 ) | 2 },
};

int awaStageBytes[8][12] = {
    { 100, 85, 101, 85, 73, 9, 25, 87, 97, 85, 86, 37 },
    { 89, 148, 81, 149, 117, 89, 86, 94, 21, 85, 101, 89 },
    { 145, 213, 176, 117, 121, 86, 100, 82, 129, 149, 85, 85 },
    { 54, 54, 81, 89, 85, 210, 86, 86, 87, 149, 217, 109 },
    { 144, 89, 205, 102, 71, 151, 156, 97, 227, 86, 91, 87 },
    { 66, 86, 73, 217, 103, 81, 44, 91, 100, 149, 109, 149 },
    { 87, 0, 148, 77, 5, 165, 77, 7, 133, 85, 85, 150 },
    { 85, 91, 85, 104, 101, 91, 89, 21, 153, 229, 102, 90 },
};

// CellChars - 4 glyph indices (top-left,top-right,bottom-left,bottom-right)
// per Cell_Mask(0-7) value, matching upstream's own static CellChars table.
int awaCellChars[8][4] = {
    { AWA_CHAR_SPACE, AWA_CHAR_SPACE, AWA_CHAR_SPACE, AWA_CHAR_SPACE },
    { AWA_CHAR_FLOOR, AWA_CHAR_FLOOR, AWA_CHAR_SPACE, AWA_CHAR_SPACE },
    { AWA_CHAR_LADDER, AWA_CHAR_LADDER + 1, AWA_CHAR_LADDER, AWA_CHAR_LADDER + 1 },
    { AWA_CHAR_BOMB, AWA_CHAR_BOMB + 1, AWA_CHAR_BOMB + 2, AWA_CHAR_BOMB + 3 },
    { AWA_CHAR_FLAG_A, AWA_CHAR_FLAG_A + 1, AWA_CHAR_FLAG_A + 2, AWA_CHAR_FLAG_A + 3 },
    { AWA_CHAR_FLAG_B, AWA_CHAR_FLAG_B + 1, AWA_CHAR_FLAG_B + 2, AWA_CHAR_FLAG_B + 3 },
    { AWA_CHAR_FLAG_C, AWA_CHAR_FLAG_C + 1, AWA_CHAR_FLAG_C + 2, AWA_CHAR_FLAG_C + 3 },
    { AWA_CHAR_FLAG_D, AWA_CHAR_FLAG_D + 1, AWA_CHAR_FLAG_D + 2, AWA_CHAR_FLAG_D + 3 },
};

// Man's own direction table (key bit, dx, dy, pattern) - ported as a flat
// array indexed by direction id instead of upstream's own pointer-into-
// array-of-struct (`pManDirection`), avoiding an unproven pattern.
int awaManDirTable[4][4] = {
    { AWA_KEYS_LEFT, -1, 0, AWA_PATTERN_MAN_LEFT },
    { AWA_KEYS_RIGHT, 1, 0, AWA_PATTERN_MAN_RIGHT },
    { AWA_KEYS_UP, 0, -1, AWA_PATTERN_MAN_UPDOWN },
    { AWA_KEYS_DOWN, 0, 1, AWA_PATTERN_MAN_UPDOWN },
};

// Monster's own direction table (dx,dy) indexed by AWA_DIR_LEFT/RIGHT/UP/DOWN.
int awaMonsterDirTable[4][2] = {
    { -1, 0 },
    { 1, 0 },
    { 0, -1 },
    { 0, 1 },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int awaScore;
int awaHiScore;
int awaRemainCount;
int awaCurrentStage;
int awaStageTime;
int awaMaxTime;
int awaStageIndex;
int awaClock;

#define AWA_MAX_TIME_DENOM ( 50 / ( 8 / AWA_COORD_RATE ) )
#define AWA_BONUS_RATE 2

int awaStageMap[AWA_COLUMN_COUNT * AWA_FLOOR_COUNT];

int awaScrolling;
int awaScrollingFloor;
int awaScrollingClock;
bool awaScrollShiftView;

int awaVVram[AWA_VVRAM_HEIGHT][AWA_VVRAM_WIDTH];

typedef struct
{
    int x, y, c;
} AwaSprite;
AwaSprite awaSprites[AWA_SPRITE_END];

AwaActor awaMan;
int awaManDirIndex;
int awaManLastDx;
bool awaManKeyOn;

int awaMonsterCount;
AwaActor awaMonsters[AWA_MAX_MONSTER_COUNT];
int awaNextCell;

typedef struct
{
    int column, floor;
} AwaFlag;
AwaFlag awaFlags[AWA_MAX_FLAG_COUNT];
int awaFlagCount;
int awaLastType;
int awaFlagRate;
#define AWA_INVALID_COLUMN 0xff
#define AWA_INVALID_TYPE 0xff
// Toggled once every time upstream's own (Clock&7)==0 cadence fires (i.e.
// BlinkFlags()'s own real call cadence) - equivalent to that function's own
// internal static `clock` parity check, since it only ever toggles once per
// call at a fixed cadence. No separate blink-specific clock counter is
// needed (awaClock itself already tracks the real cadence upstream checks).
bool awaFlagBlinkOn;

typedef struct
{
    int x, y, sprite, time;
} AwaPoint;
#define AWA_POINT_MAX_TIME 6
AwaPoint awaPoints[AWA_MAX_POINT_COUNT];

typedef struct
{
    int x, y, sprite, time;
} AwaBang;
#define AWA_BANG_MAX_TIME 8
AwaBang awaBangs[AWA_MAX_BANG_COUNT];

int awaRndIndex;

// status-text grid (columns 96-127, 8 char-cells wide x 8 pages tall) - a
// pattern index into awaAsciiPattern (0 = space) per cell, same technique as
// Cracky's own crkStatusChar.
int awaStatusChar[8][8];

// title-screen text grid (columns 0-95, the MAP area, 24 char-cells wide x 8
// pages tall) - a pattern index into awaAsciiPattern, same technique as
// awaStatusChar above but covering the map area instead of the status zone.
// Needed because upstream's own Title() draws its logo/"MINI"/"INUFUTO 2026"/
// "START"/"CONTINUE" text at ABSOLUTE VVram-column positions inside the map
// area (Status.cpp: `PrintS(Vram + VramRowSize*N + ABSOLUTE_COL*VramStep,
// ...)`, no LeftX offset at all) - unlike PrintStatus()'s own text, which is
// always LeftX(24)-relative and genuinely belongs in the status zone. A
// first draft of this port (matching Cracky's own identical, apparently
// never-noticed bug in its own crkBeginTitle()) routed ALL of Title()'s text
// through awaPrintC/awaPrintS - which only ever renders into awaStatusChar,
// visible solely in the cols96-127 status zone - so the whole title screen
// rendered wrong: crammed into the right-hand status column (overwriting/
// beside the SCORE/STAGE/TIME labels) instead of the map area upstream
// actually places it in, with the map area itself left entirely blank.
// Found via a real Puppeteer screenshot during this port's own verification
// pass, not by code inspection alone. Only active during AWA_STATE_TITLE
// (awaMapTextActive) - gameplay's own real map rendering (awaVVram) takes
// over for cols<96 whenever this flag is false.
int awaMapTextChar[8][AWA_VVRAM_WIDTH];
bool awaMapTextActive;

// message overlay burned directly over the map area, matching upstream's own
// Vram-direct PrintGameOver()/PrintTimeUp() writes - see this file's own
// header comment and Cracky's identical precedent. Holds real characters
// (copied from a char* string via awaBeginOverlay()), not pattern indices,
// so it's char[], not int[] - see the "text-string" dialect-conversion rule
// this file's own top-of-file conversion note documents.
bool awaOverlayActive;
char awaOverlayText[10];
int awaOverlayLen;
int awaOverlayPage;
int awaOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of AWA_TICK_DIVISOR.
int awaSeqMelody[3];
int awaSeqPos[3];
int awaSeqWait[3];
int awaSeqActive[3];

#define AWA_TICK_DIVISOR 8
int awaTickCounter;

#define AWA_STATE_TITLE 0
#define AWA_STATE_START_JINGLE 1
#define AWA_STATE_PLAYING 2
#define AWA_STATE_LOSE_ANIM 3
#define AWA_STATE_GAMEOVER_JINGLE 4
#define AWA_STATE_CLEAR_WAIT 5
#define AWA_STATE_CLEAR_JINGLE 6
#define AWA_STATE_BONUS_TALLY 7
int awaState;
int awaWaitFrames;
int awaAnimStep;
int awaSelection;
bool awaSelectionChanged;
int awaPrevLeft;
int awaPrevRight;
int awaPrevUp;
int awaPrevDown;
int awaPrevFire;
bool awaPendingContinue;
int awaMonsterNum;
int awaTimeDenom;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int awaRnd()
{
    int r;
    r = awaRndNumbers[ awaRndIndex ];
    awaRndIndex = awaRndIndex + 1;
    if( awaRndIndex >= 32 )
      awaRndIndex = 0;
    return r;
}

int awaAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

// Fixed-column-wraparound helper. Upstream's own TreatColumn() relies on a
// negative `byte` implicitly wrapping to 244 (256-12) to catch the -1 case -
// the same AVR-implicit-narrow-type bug class this project's CLAUDE.md
// documents at length. Every real call site only ever offsets by exactly
// +/-1 (a single wrap step), so a plain sign check is sufficient and safe.
int awaTreatColumn( int column )
{
    if( column < 0 )
      return column + AWA_COLUMN_COUNT;
    if( column >= AWA_COLUMN_COUNT )
      return column - AWA_COLUMN_COUNT;
    return column;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int awaToColumn( int b )
{
    return b >> 4;
}

int awaToFloor( int b )
{
    return b & 0x0f;
}

int awaMapPtr( int column, int floor )
{
    return ( floor * AWA_COLUMN_COUNT ) + column;
}

int awaGetCell( int column, int row )
{
    int b;
    b = awaStageMap[ awaMapPtr( column, row >> 1 ) ];
    if( ( row & 1 ) == 0 )
    {
        b = b >> 4;
        if( ( b & AWA_CELL_FLAG ) != 0 )
          b = AWA_CELL_BLANK;
    }
    else
      b = b & 0x0f;
    return b;
}

void awaInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7, and
    // ramps difficulty (shrinking MaxTime) once per full 8-stage cycle -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching Cracky's own crkInitStage precedent for this shape.
    int i, j;
    awaMaxTime = 90;
    i = 0;
    j = 0;
    while( i < awaCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= AWA_STAGE_COUNT )
        {
            j = 0;
            if( awaMaxTime >= 60 )
              awaMaxTime = awaMaxTime - 10;
        }
    }
    while( j != 0 && awaMaxTime < 200 )
    {
        awaMaxTime = awaMaxTime + 10;
        j = j - 1;
    }
    awaStageIndex = j;

    // InitFlags() - copy this stage's own flag position table into awaFlags
    // (column/floor only; PutFlags() below stamps them into awaStageMap and
    // computes awaFlagCount).
    {
        int k;
        for( k = 0; k < AWA_MAX_FLAG_COUNT; k = k + 1 )
        {
            int b;
            b = awaStageFlags[ awaStageIndex ][ k ];
            awaFlags[ k ].column = awaToColumn( b );
            awaFlags[ k ].floor = awaToFloor( b );
        }
        awaLastType = AWA_INVALID_TYPE;
        awaFlagRate = 0;
    }
}

void awaPutFlags()
{
    int index;
    awaFlagCount = 0;
    for( index = 0; index < AWA_MAX_FLAG_COUNT; index = index + 1 )
    {
        int b;
        b = awaStageFlags[ awaStageIndex ][ index ];
        if( awaFlags[ index ].column < AWA_COLUMN_COUNT )
        {
            int idx;
            awaFlags[ index ].column = awaToColumn( b );
            awaFlags[ index ].floor = awaToFloor( b );
            idx = awaMapPtr( awaFlags[ index ].column, awaFlags[ index ].floor );
            awaStageMap[ idx ] = awaStageMap[ idx ] | ( ( ( index << 3 ) & 0x30 ) | ( AWA_CELL_FLAG << 4 ) );
            awaFlagCount = awaFlagCount + 1;
        }
    }
}

void awaRollRight( int floor )
{
    int base, i, cell;
    base = awaMapPtr( 0, floor );
    cell = awaStageMap[ base + AWA_COLUMN_COUNT - 1 ];
    for( i = AWA_COLUMN_COUNT - 1; i > 0; i = i - 1 )
      awaStageMap[ base + i ] = awaStageMap[ base + i - 1 ];
    awaStageMap[ base ] = cell;
}

void awaRollLeft( int floor )
{
    int base, i, cell;
    base = awaMapPtr( 0, floor );
    cell = awaStageMap[ base ];
    for( i = 0; i < AWA_COLUMN_COUNT - 1; i = i + 1 )
      awaStageMap[ base + i ] = awaStageMap[ base + i + 1 ];
    awaStageMap[ base + AWA_COLUMN_COUNT - 1 ] = cell;
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void awaLocateActor( AwaActor* pActor, int b )
{
    pActor->x = awaToColumn( b ) << AWA_COLUMN_COORD_SHIFT;
    pActor->y = awaToFloor( b ) << AWA_FLOOR_COORD_SHIFT;
}

void awaMoveActor( AwaActor* pActor )
{
    pActor->x = pActor->x + pActor->dx;
    pActor->y = pActor->y + pActor->dy;
}

bool awaCanMove( AwaActor* pActor, int dx, int dy )
{
    int x, y, column, row;
    awaNextCell = AWA_CELL_BLANK;
    x = pActor->x;
    if( ( x & AWA_COLUMN_COORD_MASK ) != 0 )
      return dy == 0;
    column = x >> AWA_COLUMN_COORD_SHIFT;
    if( dx < 0 )
    {
        if( column == 0 ) return false;
    }
    else if( dx != 0 )
    {
        if( column >= AWA_COLUMN_COUNT - 1 ) return false;
    }

    y = pActor->y;
    if( ( y & AWA_ROW_COORD_MASK ) != 0 )
      return dx == 0;
    row = y >> AWA_ROW_COORD_SHIFT;
    if( dy < 0 )
    {
        if( row == 0 ) return false;
    }
    else if( dy != 0 )
    {
        if( row >= AWA_ROW_COUNT - 2 ) return false;
    }

    if( ( pActor->status & AWA_ACTOR_FALL ) == 0 )
    {
        int nextColumn, nextRow;
        nextColumn = column + dx;
        nextRow = row + dy;
        awaNextCell = awaGetCell( nextColumn, nextRow );
        if( awaNextCell == AWA_CELL_BLANK )
          return dy >= 0;
        if( awaNextCell == AWA_CELL_LADDER )
          return dy >= 0 || awaGetCell( column, row ) == AWA_CELL_LADDER;
        return false;
    }
    return true;
}

bool awaFallActor( AwaActor* pActor )
{
    int nextRow, x, column;
    nextRow = ( pActor->y >> AWA_ROW_COORD_SHIFT ) + 1;
    if( nextRow >= AWA_ROW_COUNT - 1 )
    {
        pActor->status = pActor->status & ~AWA_ACTOR_FALL;
        return false;
    }
    x = pActor->x >> AWA_COORD_SHIFT;
    column = x >> AWA_COLUMN_SHIFT;
    if( ( x & 1 ) == 0 )
    {
        if( awaGetCell( column, nextRow ) == AWA_CELL_BLANK )
        {
            pActor->status = pActor->status | AWA_ACTOR_FALL;
            return true;
        }
        pActor->status = pActor->status & ~AWA_ACTOR_FALL;
        return false;
    }
    if( awaGetCell( column, nextRow ) == AWA_CELL_BLANK && awaGetCell( column + 1, nextRow ) == AWA_CELL_BLANK )
    {
        pActor->status = pActor->status | AWA_ACTOR_FALL;
        return true;
    }
    pActor->status = pActor->status & ~AWA_ACTOR_FALL;
    return false;
}

bool awaIsNear( AwaActor* p1, AwaActor* p2 )
{
    return
        p1->x + AWA_HIT_RANGE >= p2->x && p2->x + AWA_HIT_RANGE >= p1->x &&
        p1->y + AWA_HIT_RANGE >= p2->y && p2->y + AWA_HIT_RANGE >= p1->y;
}

bool awaInRange( AwaActor* pActor, int dx, int dy )
{
    int column, row;
    column = ( pActor->x >> AWA_COLUMN_COORD_SHIFT ) + dx;
    if( column >= AWA_COLUMN_COUNT ) return false;
    row = ( pActor->y >> AWA_ROW_COORD_SHIFT ) + dy;
    return row < AWA_ROW_COUNT - 1;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites directly into awaVVram (matching Cracky's own
//   single-buffer model - see this file's own header comment). Draw order
//   mirrors upstream's own DrawSprites() reverse-index composite (Man drawn
//   first/lowest-priority, then Monsters, then Bangs, then Points drawn
//   last/on-top) so overlapping sprites keep the same visual priority.
// -----------------------------------------------------------------------------

void awaHideAllSprites()
{
    int i;
    for( i = 0; i < AWA_SPRITE_END; i = i + 1 )
      awaSprites[ i ].y = AWA_INVALID_Y;
}

void awaShowSprite( AwaActor* pMovable, int pattern )
{
    AwaSprite* pSprite;
    pSprite = &awaSprites[ pMovable->sprite ];
    pSprite->x = pMovable->x;
    pSprite->y = pMovable->y;
    pSprite->c = ( pattern << 2 ) + AWA_CHAR_SPRITE;
}

void awaHideSprite( int index )
{
    awaSprites[ index ].y = AWA_INVALID_Y;
}

void awaDrawSpritesIntoVVram()
{
    int i;
    for( i = AWA_SPRITE_END - 1; i >= 0; i = i - 1 )
    {
        if( awaSprites[ i ].y <= AWA_VVRAM_HEIGHT - 1 )
        {
            int x, y, c;
            x = awaSprites[ i ].x;
            y = awaSprites[ i ].y;
            c = awaSprites[ i ].c;
            awaVVram[ y ][ x ] = c; c = c + 1;
            awaVVram[ y ][ x + 1 ] = c; c = c + 1;
            awaVVram[ y + 1 ][ x ] = c; c = c + 1;
            awaVVram[ y + 1 ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void awaInitBangs()
{
    int i;
    for( i = 0; i < AWA_MAX_BANG_COUNT; i = i + 1 )
    {
        awaBangs[ i ].sprite = AWA_SPRITE_BANG + i;
        awaBangs[ i ].time = 0;
        awaHideSprite( AWA_SPRITE_BANG + i );
    }
}

void awaShowBang( AwaBang* pBang )
{
    AwaActor tmp;
    tmp.x = pBang->x; tmp.y = pBang->y; tmp.sprite = pBang->sprite;
    awaShowSprite( &tmp, AWA_PATTERN_BANG );
}

void awaStartBang( int x, int y )
{
    int i;
    // RangeX = VVramWidth*CoordRate-1 - only start a bang whose x lands
    // within the real VVram column range.
    if( x >= AWA_VVRAM_WIDTH * AWA_COORD_RATE - 1 ) return;
    for( i = 0; i < AWA_MAX_BANG_COUNT; i = i + 1 )
    {
        if( awaBangs[ i ].time == 0 )
        {
            awaBangs[ i ].x = x;
            awaBangs[ i ].y = y;
            awaBangs[ i ].time = AWA_BANG_MAX_TIME * AWA_COORD_RATE;
            awaShowBang( &awaBangs[ i ] );
            return;
        }
    }
}

void awaUpdateBangs()
{
    int i;
    for( i = 0; i < AWA_MAX_BANG_COUNT; i = i + 1 )
    {
        if( awaBangs[ i ].time != 0 )
        {
            awaBangs[ i ].time = awaBangs[ i ].time - 1;
            if( awaBangs[ i ].time == 0 )
              awaHideSprite( awaBangs[ i ].sprite );
            else
              awaShowBang( &awaBangs[ i ] );
        }
    }
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into awaStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area),
//   matching Cracky's own identical technique and identical page/column
//   layout (SCORE page0, STAGE page3, TIME page5, lives page7 - confirmed
//   directly against upstream's own `PrintStatus()` VramRowSize*N offsets,
//   not assumed from Cracky's own layout by analogy).
// -----------------------------------------------------------------------------

int awaAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct port of PrintC()'s
    // own linear search, byte-identical table to Cracky's own copy.
    char table[27] = {
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

int awaPrintC( int page, int col, int c )
{
    awaStatusChar[ page ][ col ] = awaAsciiIndex( c );
    return col + 1;
}

int awaPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = awaPrintC( page, col, s[ i ] );
    return col;
}

// Map-area counterparts of awaPrintC/awaPrintS above, writing into
// awaMapTextChar (cols0-95) instead of awaStatusChar (cols96-127) - see
// that array's own declaration comment for why this is needed at all.
int awaPrintMapC( int page, int col, int c )
{
    awaMapTextChar[ page ][ col ] = awaAsciiIndex( c );
    return col + 1;
}

int awaPrintMapS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = awaPrintMapC( page, col, s[ i ] );
    return col;
}

int awaPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
{
    int c;
    c = value / n;
    if( c == 0 )
    {
        if( zeroVisible )
          c = '0';
        else
          c = ' ';
    }
    else
      c = c + '0';
    return awaPrintC( page, col, c );
}

void awaPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      awaPrintC( page, col, ' ' );
    else
      awaPrintC( page, col, d1 + '0' );
    awaPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void awaPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        awaPrintC( page, col, ' ' );
        if( d2 == 0 )
          awaPrintC( page, col + 1, ' ' );
        else
          awaPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        awaPrintC( page, col, d1 + '0' );
        awaPrintC( page, col + 1, d2 + '0' );
    }
    awaPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void awaPrintNumber5( int page, int col, int w )
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
          awaPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            awaPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    awaPrintC( page, col + 4, rem + '0' );
}

// Upstream: PrintNumber5(Vram + VramRowSize*1 + (LeftX+2)*VramStep, Score)
// then a trailing PrintC(vram,'0') right after the 5th digit. awaStatusChar
// stores LOCAL offsets relative to LeftX(24), not absolute real columns (see
// awaStatusChar's own declaration comment) - so (LeftX+2) is local offset 2,
// and the trailing '0' (right after 5 digits starting at offset 2, i.e.
// offset 2+5=7) is local offset 7. **A real, previously-shipped bug**: this
// used to read `awaPrintNumber5(1, 2+2, ...)` (local offset 4) and
// `awaPrintC(1, 2+7, '0')` (local offset 9) - wrong on two counts: (1) offset
// 4 pushes the whole 5-digit score two columns right of where upstream draws
// it, and (2) offsets 8/9 are past awaStatusChar's own real column bound
// (int[8][8], valid indices 0-7 only) - `awaStatusChar[1][8]`/`[1][9]` alias
// (row-major, 8 cols/row) into `awaStatusChar[2][0]`/`[2][1]` instead of
// genuinely crashing, silently splattering score digits into the otherwise-
// blank status page 2 rather than corrupting unrelated memory. Found by
// re-deriving upstream's real column math directly (Status.cpp's own
// `PrintScore()`), not from a report. Fixed to the correct local offsets.
void awaPrintScore()
{
    awaPrintNumber5( 1, 2, awaScore );
    awaPrintC( 1, 7, '0' );
}

void awaPrintTime()
{
    awaPrintByteNumber3( 5, 5, awaStageTime );
}

void awaPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    awaPrintS( 0, 0, sScore, 5 );
    awaPrintS( 3, 0, sStage, 5 );
    awaPrintByteNumber2( 3, 6, awaCurrentStage + 1 );
    awaPrintS( 5, 0, sTime, 4 );

    if( awaRemainCount > 1 )
    {
        i = awaRemainCount - 1;
        if( i > 2 )
        {
            // upstream draws a real 2x2 Char_Remain icon (Put2C) here, then
            // a space, then the remaining digit - simplified to plain text
            // digits throughout (matching Cracky's own precedent for this
            // exact lives-display simplification).
            awaPrintC( 7, 0, ' ' );
            awaPrintC( 7, 1, ' ' );
            awaPrintC( 7, 2, i + '0' );
        }
        else
        {
            for( i = 0; i < awaRemainCount - 1; i = i + 1 )
              awaPrintC( 7, i * 2, ' ' );
        }
    }

    awaPrintScore();
    awaPrintTime();
}

void awaBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    awaOverlayActive = true;
    awaOverlayLen = len;
    awaOverlayPage = page;
    awaOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      awaOverlayText[ i ] = s[ i ];
}

void awaPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    awaBeginOverlay( s, 9, 4, 8 );
}

void awaPrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    awaBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int awaMelodyLength( int id )
{
    if( id == AWA_MELODY_LOOSE ) return 3;
    if( id == AWA_MELODY_HIT ) return 17;
    if( id == AWA_MELODY_BANG ) return 25;
    if( id == AWA_MELODY_BEEP ) return 3;
    if( id == AWA_MELODY_MOVE ) return 3;
    if( id == AWA_MELODY_START ) return 25;
    if( id == AWA_MELODY_CLEAR ) return 21;
    if( id == AWA_MELODY_GAMEOVER ) return 21;
    if( id == AWA_MELODY_BGM1 ) return 189;
    if( id == AWA_MELODY_BGM2 ) return 319;
    return 0;
}

int awaMelodyValue( int id, int idx )
{
    if( id == AWA_MELODY_LOOSE ) return awaMelodyLoose[ idx ];
    if( id == AWA_MELODY_HIT ) return awaMelodyHit[ idx ];
    if( id == AWA_MELODY_BANG ) return awaMelodyBang[ idx ];
    if( id == AWA_MELODY_BEEP ) return awaMelodyBeep[ idx ];
    if( id == AWA_MELODY_MOVE ) return awaMelodyMove[ idx ];
    if( id == AWA_MELODY_START ) return awaMelodyStart[ idx ];
    if( id == AWA_MELODY_CLEAR ) return awaMelodyClear[ idx ];
    if( id == AWA_MELODY_GAMEOVER ) return awaMelodyGameOver[ idx ];
    if( id == AWA_MELODY_BGM1 ) return awaMelodyBgm1[ idx ];
    if( id == AWA_MELODY_BGM2 ) return awaMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/AWA_TEMPO = 2.0 real 60Hz ticks (a clean multiplier, unlike
// Cracky's own fractional 1.875 - see this file's own header comment).
int awaNoteFrames( int length )
{
    return (int)( length * 2.0 + 0.5 );
}

void awaStartSeq( int channel, int melodyId )
{
    awaSeqMelody[ channel ] = melodyId;
    awaSeqPos[ channel ] = 0;
    awaSeqWait[ channel ] = 0;
    awaSeqActive[ channel ] = 1;
}

void awaStopSeq( int channel )
{
    awaSeqActive[ channel ] = 0;
    awaSeqMelody[ channel ] = AWA_MELODY_NONE;
}

bool awaSeqPlaying( int channel )
{
    return awaSeqActive[ channel ] != 0;
}

void awaAdvanceOneSeq( int channel )
{
    int length, note;

    if( awaSeqActive[ channel ] == 0 ) return;

    if( awaSeqWait[ channel ] > 0 )
    {
        awaSeqWait[ channel ] = awaSeqWait[ channel ] - 1;
        return;
    }

    length = awaMelodyValue( awaSeqMelody[ channel ], awaSeqPos[ channel ] );
    if( length == 0 )
    {
        awaStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        awaSeqPos[ channel ] = 0;
        length = awaMelodyValue( awaSeqMelody[ channel ], 0 );
    }
    note = awaMelodyValue( awaSeqMelody[ channel ], awaSeqPos[ channel ] + 1 );
    awaSeqPos[ channel ] = awaSeqPos[ channel ] + 2;
    awaSeqWait[ channel ] = awaNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)awaFrequencies[ note - 1 ], (float)awaSeqWait[ channel ] / 60.0 );
}

void awaAdvanceSound()
{
    awaAdvanceOneSeq( 0 );
    awaAdvanceOneSeq( 1 );
    awaAdvanceOneSeq( 2 );
}

void awaStartBgm()
{
    awaStartSeq( 1, AWA_MELODY_BGM1 );
    awaStartSeq( 2, AWA_MELODY_BGM2 );
}

void awaStopBgm()
{
    // Matches upstream's own StopBGM(), which resets ALL 3 real hardware
    // tone channels (not just the 2 BGM voices) - a pending channel-0
    // one-shot SFX (e.g. a Bang melody still mid-flight when the player
    // dies) gets silenced too, not just the background music.
    awaStopSeq( 0 );
    awaStopSeq( 1 );
    awaStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score / stage progression
// -----------------------------------------------------------------------------

void awaAddScore( int pts )
{
    awaScore = awaScore + pts;
    if( awaScore > awaHiScore )
      awaHiScore = awaScore;
    awaPrintScore();
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

int awaPointValues[4] = { 10, 20, 40, 80 };

void awaInitPoints()
{
    int i;
    for( i = 0; i < AWA_MAX_POINT_COUNT; i = i + 1 )
    {
        awaPoints[ i ].sprite = AWA_SPRITE_POINT + i;
        awaPoints[ i ].time = 0;
        awaHideSprite( AWA_SPRITE_POINT + i );
    }
}

void awaStartPoint( int x, int y, int rate )
{
    int i;
    awaAddScore( awaPointValues[ rate ] );
    for( i = 0; i < AWA_MAX_POINT_COUNT; i = i + 1 )
    {
        if( awaPoints[ i ].time == 0 )
        {
            AwaActor tmp;
            awaPoints[ i ].time = AWA_POINT_MAX_TIME << AWA_COORD_SHIFT;
            awaPoints[ i ].x = x;
            awaPoints[ i ].y = y;
            tmp.x = x; tmp.y = y; tmp.sprite = awaPoints[ i ].sprite;
            awaShowSprite( &tmp, AWA_PATTERN_POINT + rate );
            return;
        }
    }
}

void awaUpdatePoints()
{
    int i;
    for( i = 0; i < AWA_MAX_POINT_COUNT; i = i + 1 )
    {
        if( awaPoints[ i ].time != 0 )
        {
            awaPoints[ i ].time = awaPoints[ i ].time - 1;
            if( awaPoints[ i ].time == 0 )
              awaHideSprite( awaPoints[ i ].sprite );
        }
    }
}


// -----------------------------------------------------------------------------
//   Map rendering - direct structural mirror of upstream's own DrawCell/
//   DrawCellRight/DrawCellLeft/DrawFloor/DrawFloorShift, translated from
//   pointer-walking into absolute-row/column array writes into awaVVram.
//   See this file's own header comment for why this rebuilds the whole map
//   layer fresh every frame instead of replicating VVramBack's own
//   incremental-patch persistence.
// -----------------------------------------------------------------------------

void awaDrawCellAt( int row, int col, int nibble )
{
    int c;
    c = nibble & AWA_CELL_MASK;
    awaVVram[ row ][ col ] = awaCellChars[ c ][ 0 ];
    awaVVram[ row ][ col + 1 ] = awaCellChars[ c ][ 1 ];
    awaVVram[ row + 1 ][ col ] = awaCellChars[ c ][ 2 ];
    awaVVram[ row + 1 ][ col + 1 ] = awaCellChars[ c ][ 3 ];
}

void awaDrawCellRightAt( int row, int col, int nibble )
{
    int c;
    c = nibble & AWA_CELL_MASK;
    awaVVram[ row ][ col ] = awaCellChars[ c ][ 1 ];
    awaVVram[ row + 1 ][ col ] = awaCellChars[ c ][ 3 ];
}

void awaDrawCellLeftAt( int row, int col, int nibble )
{
    int c;
    c = nibble & AWA_CELL_MASK;
    awaVVram[ row ][ col ] = awaCellChars[ c ][ 0 ];
    awaVVram[ row + 1 ][ col ] = awaCellChars[ c ][ 2 ];
}

void awaDrawFloorInto( int floor )
{
    int mapCol;
    int row;
    row = floor * AWA_FLOOR_HEIGHT;
    for( mapCol = 0; mapCol < AWA_COLUMN_COUNT; mapCol = mapCol + 1 )
    {
        int cell, col;
        cell = awaStageMap[ awaMapPtr( mapCol, floor ) ];
        col = mapCol * AWA_COLUMN_WIDTH;
        awaDrawCellAt( row, col, cell >> 4 );
        awaDrawCellAt( row + 2, col, cell & 0x0f );
    }
}

void awaDrawFloorShiftInto( int floor )
{
    // Mirrors upstream's DrawFloorShift() exactly: the current column-0 cell
    // ("endCell") splits across both screen edges - its right-half glyph
    // pinned at VVram column 0, its left-half glyph wrapped to the far
    // right edge (VVram column AWA_VVRAM_WIDTH-1) - while columns 1..11
    // render normally at their own real positions. See this file's own
    // header comment for the full derivation of why this reproduces
    // upstream's real mid-scroll animation frame.
    int row, endCell, mapCol;
    row = floor * AWA_FLOOR_HEIGHT;
    endCell = awaStageMap[ awaMapPtr( 0, floor ) ];
    awaDrawCellRightAt( row, 0, endCell >> 4 );
    awaDrawCellRightAt( row + 2, 0, endCell & 0x0f );
    for( mapCol = 1; mapCol < AWA_COLUMN_COUNT; mapCol = mapCol + 1 )
    {
        int cell, col;
        cell = awaStageMap[ awaMapPtr( mapCol, floor ) ];
        col = 1 + ( mapCol - 1 ) * AWA_COLUMN_WIDTH;
        awaDrawCellAt( row, col, cell >> 4 );
        awaDrawCellAt( row + 2, col, cell & 0x0f );
    }
    awaDrawCellLeftAt( row, AWA_VVRAM_WIDTH - 1, endCell >> 4 );
    awaDrawCellLeftAt( row + 2, AWA_VVRAM_WIDTH - 1, endCell & 0x0f );
}

void awaMapToVVram()
{
    int floor;
    for( floor = 0; floor < AWA_FLOOR_COUNT; floor = floor + 1 )
    {
        if( awaScrollShiftView && floor == awaScrollingFloor )
          awaDrawFloorShiftInto( floor );
        else
          awaDrawFloorInto( floor );
    }

    // Flag-blink overlay - matches upstream's own BlinkFlags(): while a
    // flag's type equals awaLastType (the type just collected, awaiting its
    // matching pair) and the blink phase is "off", blank that flag's cell
    // instead of drawing it. Doing this as a post-pass over the already-
    // rendered map (rather than a separate incremental VVram patch) is
    // exactly equivalent since every flag's own glyph was just drawn as
    // part of the normal map pass above.
    if( !awaFlagBlinkOn && awaLastType != AWA_INVALID_TYPE )
    {
        int i;
        for( i = 0; i < AWA_MAX_FLAG_COUNT; i = i + 1 )
        {
            if( awaFlags[ i ].column < AWA_COLUMN_COUNT && ( i >> 1 ) == awaLastType )
            {
                int row, col;
                row = awaFlags[ i ].floor * AWA_FLOOR_HEIGHT;
                col = awaFlags[ i ].column * AWA_COLUMN_WIDTH;
                awaVVram[ row ][ col ] = AWA_CHAR_SPACE;
                awaVVram[ row ][ col + 1 ] = AWA_CHAR_SPACE;
                awaVVram[ row + 1 ][ col ] = AWA_CHAR_SPACE;
                awaVVram[ row + 1 ][ col + 1 ] = AWA_CHAR_SPACE;
            }
        }
    }
}

void awaDrawAll()
{
    awaMapToVVram();
    awaDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   Flag.cpp - InitFlags()/PutFlags() already live in the Stage.cpp section
//   above (awaInitStage()/awaPutFlags()) since upstream's own InitStage()
//   calls InitFlags() directly. DrawFlags() needed no port at all - see this
//   file's own header comment. `Hit()` (upstream's own static helper) is
//   ported as `awaHitFlag()`, taking a flag index directly instead of a
//   pointer, since every real call site already has the index in hand.
// -----------------------------------------------------------------------------

void awaHitFlag( int index, int x, int y )
{
    int type, idx;
    type = index >> 1;
    if( type == awaLastType )
    {
        awaStartPoint( x << AWA_COORD_SHIFT, y << AWA_COORD_SHIFT, awaFlagRate );
        awaFlagRate = awaFlagRate + 1;
        awaLastType = AWA_INVALID_TYPE;
    }
    else
    {
        awaAddScore( 5 );
        if( awaLastType != AWA_INVALID_TYPE )
          awaFlagRate = 0;
        awaLastType = type;
    }
    idx = awaMapPtr( awaFlags[ index ].column, awaFlags[ index ].floor );
    awaStageMap[ idx ] = awaStageMap[ idx ] & 0x0f;
    awaFlags[ index ].column = AWA_INVALID_COLUMN;
    awaFlagCount = awaFlagCount - 1;
    awaStartSeq( 0, AWA_MELODY_HIT );
}

void awaHitFlags( int column, int floor )
{
    int index;
    for( index = 0; index < AWA_MAX_FLAG_COUNT; index = index + 1 )
    {
        if( awaFlags[ index ].column == column && awaFlags[ index ].floor == floor )
        {
            int x, y;
            x = column << AWA_COLUMN_SHIFT;
            y = floor << AWA_FLOOR_SHIFT;
            awaHitFlag( index, x, y );
            break;
        }
    }
}

void awaHitFlagsShift( int offset )
{
    int manX, index;
    manX = awaMan.x >> AWA_COORD_SHIFT;
    if( ( manX & 1 ) == 0 ) return;
    for( index = 0; index < AWA_MAX_FLAG_COUNT; index = index + 1 )
    {
        if( awaFlags[ index ].floor == awaScrollingFloor )
        {
            int x, y;
            x = ( awaFlags[ index ].column << AWA_COLUMN_SHIFT ) + offset;
            y = awaFlags[ index ].floor << AWA_FLOOR_SHIFT;
            if( x == manX && y == ( awaMan.y >> AWA_COORD_SHIFT ) )
            {
                awaHitFlag( index, x, y );
                break;
            }
        }
    }
}

void awaSlideFlags( int dx )
{
    int i;
    for( i = 0; i < AWA_MAX_FLAG_COUNT; i = i + 1 )
    {
        if( awaFlags[ i ].column < AWA_COLUMN_COUNT && awaFlags[ i ].floor == awaScrollingFloor )
          awaFlags[ i ].column = awaTreatColumn( awaFlags[ i ].column + dx );
    }
}


// -----------------------------------------------------------------------------
//   EraseBomb (Stage.cpp) - clears the bomb bit (upper nibble) from a map
//   cell, leaving the underlying floor tile (lower nibble) intact, and starts
//   the explosion sprite. VErase2XY's own single-cell VVram patch needs no
//   port at all - the next full map rebuild already reflects the cleared bit.
// -----------------------------------------------------------------------------

void awaEraseBomb( int column, int floor )
{
    int idx, x, y;
    x = column << AWA_COLUMN_SHIFT;
    y = floor << AWA_FLOOR_SHIFT;
    idx = awaMapPtr( column, floor );
    awaStageMap[ idx ] = awaStageMap[ idx ] & 0x0f;
    awaStartBang( x << AWA_COORD_SHIFT, y << AWA_COORD_SHIFT );
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void awaShowMan()
{
    int pattern, seq;
    pattern = awaMan.status & AWA_ACTOR_PATTERN_MASK;
    if( ( awaMan.status & AWA_ACTOR_FALL ) == 0 )
    {
        if( awaMan.dy != 0 )
        {
            seq = ( awaMan.y >> AWA_COORD_SHIFT ) & 1;
            pattern = pattern + seq;
        }
        else if( awaMan.dx != 0 )
        {
            seq = ( awaMan.x >> AWA_COORD_SHIFT ) & 3;
            if( seq == 3 )
              seq = 1;
            pattern = pattern + seq + 1;
        }
    }
    awaShowSprite( &awaMan, pattern );
}

void awaInitMan()
{
    awaMan.sprite = AWA_SPRITE_MAN;
    awaMan.status = AWA_MOVABLE_LIVE;
    awaMan.dx = 0;
    awaMan.dy = 0;
    awaManLastDx = -1;
    awaManDirIndex = AWA_DIR_LEFT;
    awaLocateActor( &awaMan, awaStageStart[ awaStageIndex ] );
    awaShowMan();
}

// Direct structural mirror of upstream's own MoveMan() - falling is handled
// inline at the end of this same function (upstream has no separate
// FallMan(), unlike Cracky's own split crkFallMan()/crkMoveMan() - confirmed
// by reading this game's own real Man.cpp directly, not assumed by analogy).
// The ACT-button scroll-trigger's own local `sbyte dx;` upstream genuinely
// shadows the outer movement `dx` - ported as a distinctly-named
// `scrollDx` local instead of relying on block-scope shadowing.
void awaMoveMan()
{
    if( awaScrolling != 0 ) return;
    if( ( ( awaMan.x | awaMan.y ) & AWA_COORD_MASK ) == 0 )
    {
        int dx, dy, pattern;
        dx = 0; dy = 0;
        pattern = awaMan.status & AWA_ACTOR_PATTERN_MASK;
        if( ( awaMan.status & AWA_ACTOR_FALL ) == 0 )
        {
            bool left, right, up, down;
            int key;
            left = isLeftPressed();
            right = isRightPressed();
            up = isUpPressed();
            down = isDownPressed();
            key = 0;
            if( left ) key = key | AWA_KEYS_LEFT;
            if( right ) key = key | AWA_KEYS_RIGHT;
            if( up ) key = key | AWA_KEYS_UP;
            if( down ) key = key | AWA_KEYS_DOWN;

            if( key != 0 )
            {
                int d;
                for( d = 0; d < 4; d = d + 1 )
                {
                    if( ( awaManDirTable[ d ][ 0 ] & key ) != 0 )
                    {
                        dx = awaManDirTable[ d ][ 1 ];
                        dy = awaManDirTable[ d ][ 2 ];
                        if( awaCanMove( &awaMan, dx, dy ) )
                          awaManDirIndex = d;
                        else
                        {
                            if( awaCanMove( &awaMan, awaManDirTable[ awaManDirIndex ][ 1 ], awaManDirTable[ awaManDirIndex ][ 2 ] ) )
                            {
                                dx = awaManDirTable[ awaManDirIndex ][ 1 ];
                                dy = awaManDirTable[ awaManDirIndex ][ 2 ];
                            }
                            else
                            {
                                awaManDirIndex = d;
                                dx = 0;
                                dy = 0;
                            }
                        }
                        pattern = awaManDirTable[ awaManDirIndex ][ 3 ];
                        awaManLastDx = awaManDirTable[ awaManDirIndex ][ 1 ];
                        break;
                    }
                }
            }
            if( dx != 0 )
              awaManLastDx = dx;
            awaMan.dx = dx;
            awaMan.dy = dy;
            awaMan.status = ( awaMan.status & ~AWA_ACTOR_PATTERN_MASK ) | pattern;

            if( isFirePressed() )
            {
                if( !awaManKeyOn )
                {
                    int scrollDx, column, x, row, cell;
                    x = awaMan.x >> AWA_COORD_SHIFT;
                    if( awaManLastDx < 0 )
                    {
                        column = ( x >> AWA_COLUMN_SHIFT ) - 1;
                        scrollDx = 1;
                    }
                    else
                    {
                        column = ( ( x + 1 ) >> AWA_COLUMN_SHIFT ) + 1;
                        scrollDx = -1;
                    }
                    column = awaTreatColumn( column );
                    row = awaMan.y >> ( AWA_COORD_SHIFT + AWA_ROW_SHIFT );
                    cell = awaGetCell( column, row );
                    if( cell != AWA_CELL_BOMB )
                    {
                        awaScrollingFloor = row >> 1;
                        awaScrollingClock = 0;
                        awaScrolling = scrollDx;
                        awaStartSeq( 0, AWA_MELODY_MOVE );
                        awaManKeyOn = true;
                        awaMan.dx = 0;
                    }
                }
            }
            else
              awaManKeyOn = false;
        }
    }

    awaMoveActor( &awaMan );
    if( ( awaMan.y & AWA_CELL_COORD_MASK ) == 0 )
    {
        if( ( awaMan.x & AWA_CELL_COORD_MASK ) == 0 )
        {
            int x, y;
            x = awaMan.x >> AWA_COORD_SHIFT;
            y = awaMan.y >> AWA_COORD_SHIFT;
            if( ( x & ( AWA_COLUMN_WIDTH - 1 ) ) == 0 && ( y & ( AWA_FLOOR_HEIGHT - 1 ) ) == 0 )
            {
                int column, floor;
                column = x >> AWA_COLUMN_SHIFT;
                floor = y >> AWA_FLOOR_SHIFT;
                if( ( awaStageMap[ awaMapPtr( column, floor ) ] & ( AWA_CELL_FLAG << 4 ) ) != 0 )
                  awaHitFlags( column, floor );
            }
        }
        if( ( awaMan.x & AWA_COORD_MASK ) == 0 && awaFallActor( &awaMan ) )
        {
            awaMan.dy = 1;
            awaMan.dx = 0;
            awaManDirIndex = AWA_DIR_DOWN;
        }
    }
    awaShowMan();
}

// LooseMan()'s own real 8-step blink-and-beep loop, converted into an
// explicit per-frame state (AWA_STATE_LOSE_ANIM) further below - this
// function itself just draws one step, matching Cracky's identical split.
int awaLoosePatterns[4] = { AWA_PATTERN_MAN_LEFT, AWA_PATTERN_MAN_LOOSE0, AWA_PATTERN_MAN_LOOSE1, AWA_PATTERN_MAN_LOOSE2 };


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

#define AWA_MONSTER_THROUGH 0x20
int awaMonsterClock;

void awaShowMonster( AwaActor* pMonster )
{
    int pattern, seq;
    pattern = pMonster->status & AWA_ACTOR_PATTERN_MASK;
    if( ( pMonster->status & AWA_MONSTER_THROUGH ) != 0 )
      pattern = ( pattern >> 1 ) + AWA_CHAR_MONSER_REV;
    else
    {
        seq = ( ( pMonster->x + pMonster->y ) >> AWA_COORD_SHIFT ) & 1;
        pattern = pattern + seq + AWA_PATTERN_MONSTER;
    }
    awaShowSprite( pMonster, pattern );
}

void awaDecideDirection( AwaActor* pMonster )
{
    int dirIdx[4];
    int verticalIdx, horizontalIdx;
    int i;

    if( awaAbs( awaMan.x, pMonster->x ) > awaAbs( awaMan.y, pMonster->y ) )
    {
        if( awaMan.x < pMonster->x )
        {
            if( pMonster->dx <= 0 )
            {
                dirIdx[ 0 ] = AWA_DIR_LEFT;
                dirIdx[ 3 ] = AWA_DIR_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                dirIdx[ 2 ] = AWA_DIR_RIGHT;
                dirIdx[ 3 ] = AWA_DIR_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                dirIdx[ 0 ] = AWA_DIR_RIGHT;
                dirIdx[ 3 ] = AWA_DIR_LEFT;
                verticalIdx = 1;
            }
            else
            {
                dirIdx[ 2 ] = AWA_DIR_LEFT;
                dirIdx[ 3 ] = AWA_DIR_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( awaMan.y < pMonster->y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            dirIdx[ verticalIdx ] = AWA_DIR_UP;
            verticalIdx = verticalIdx + 1;
            dirIdx[ verticalIdx ] = AWA_DIR_DOWN;
        }
        else
        {
            dirIdx[ verticalIdx ] = AWA_DIR_DOWN;
            verticalIdx = verticalIdx + 1;
            dirIdx[ verticalIdx ] = AWA_DIR_UP;
        }
    }
    else
    {
        if( awaMan.y < pMonster->y )
        {
            if( pMonster->dy <= 0 )
            {
                dirIdx[ 0 ] = AWA_DIR_UP;
                dirIdx[ 3 ] = AWA_DIR_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                dirIdx[ 2 ] = AWA_DIR_DOWN;
                dirIdx[ 3 ] = AWA_DIR_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                dirIdx[ 0 ] = AWA_DIR_DOWN;
                dirIdx[ 3 ] = AWA_DIR_UP;
                horizontalIdx = 1;
            }
            else
            {
                dirIdx[ 2 ] = AWA_DIR_UP;
                dirIdx[ 3 ] = AWA_DIR_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares `Man.x < pMonster->y` here too (a real upstream
        // quirk, not a transcription slip - confirmed directly against the
        // real source, kept exactly as-is matching Cracky's own identical
        // preserved quirk in its own Monster.cpp translation).
        if( ( awaMan.x < pMonster->y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            dirIdx[ horizontalIdx ] = AWA_DIR_LEFT;
            horizontalIdx = horizontalIdx + 1;
            dirIdx[ horizontalIdx ] = AWA_DIR_RIGHT;
        }
        else
        {
            dirIdx[ horizontalIdx ] = AWA_DIR_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            dirIdx[ horizontalIdx ] = AWA_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        int direction, dx, dy;
        bool can;
        direction = dirIdx[ i ];
        dx = awaMonsterDirTable[ direction ][ 0 ];
        dy = awaMonsterDirTable[ direction ][ 1 ];
        can = awaCanMove( pMonster, dx, dy );
        if( can )
        {
            if( i == 0 )
              pMonster->status = pMonster->status & ~AWA_MONSTER_THROUGH;
        }
        else if( ( pMonster->status & AWA_MONSTER_THROUGH ) != 0 )
          can = awaNextCell != AWA_CELL_BOMB && awaInRange( pMonster, dx, dy );
        if( can )
        {
            int pattern;
            pMonster->dx = dx;
            pMonster->dy = dy;
            pattern = direction << 1;
            pMonster->status = ( pMonster->status & ~AWA_ACTOR_PATTERN_MASK ) | pattern;
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void awaInitMonsters()
{
    int i, sprite;
    awaMonsterCount = awaStageMonsterCount[ awaStageIndex ];
    sprite = AWA_SPRITE_MONSTER;
    for( i = 0; i < awaMonsterCount; i = i + 1 )
    {
        awaMonsters[ i ].status = AWA_MOVABLE_LIVE;
        awaMonsters[ i ].sprite = sprite;
        awaMonsters[ i ].dx = 0;
        awaMonsters[ i ].dy = 0;
        awaLocateActor( &awaMonsters[ i ], awaStageMonsters[ awaStageIndex ][ i ] );
        awaDecideDirection( &awaMonsters[ i ] );
        awaShowMonster( &awaMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = awaMonsterCount; i < AWA_MAX_MONSTER_COUNT; i = i + 1 )
    {
        awaMonsters[ i ].status = 0;
        awaMonsters[ i ].sprite = sprite;
        awaHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void awaMoveMonsters()
{
    int i;
    awaMonsterClock = awaMonsterClock + 1;

    for( i = 0; i < awaMonsterCount; i = i + 1 )
    {
        int status;
        status = awaMonsters[ i ].status;
        if( ( status & AWA_MOVABLE_LIVE ) != 0 )
        {
            if( ( status & AWA_ACTOR_FALL ) == 0 )
            {
                if( ( ( awaMonsters[ i ].x | awaMonsters[ i ].y ) & AWA_CELL_COORD_MASK ) == 0 )
                {
                    if( ( status & AWA_ACTOR_FALL ) == 0 )
                    {
                        if( ( awaRnd() << 2 ) <= awaCurrentStage )
                        {
                            status = status | AWA_MONSTER_THROUGH;
                            awaMonsters[ i ].status = status;
                        }
                        awaDecideDirection( &awaMonsters[ i ] );
                    }
                }
            }
            if( ( awaMonsterClock & 1 ) == 0 || ( status & AWA_MONSTER_THROUGH ) == 0 )
              awaMoveActor( &awaMonsters[ i ] );
            if( awaIsNear( &awaMonsters[ i ], &awaMan ) )
              awaMan.status = awaMan.status & ~AWA_MOVABLE_LIVE;
            if( ( ( awaMonsters[ i ].x | awaMonsters[ i ].y ) & AWA_CELL_COORD_MASK ) == 0 && ( status & AWA_MONSTER_THROUGH ) == 0 )
            {
                if( awaFallActor( &awaMonsters[ i ] ) )
                {
                    awaMonsters[ i ].dy = 1;
                    awaMonsters[ i ].dx = 0;
                }
            }
            awaShowMonster( &awaMonsters[ i ] );
        }
    }
}

void awaMonsterHitBomb( AwaActor* pMonster, int column, int floor )
{
    awaStartSeq( 0, AWA_MELODY_BANG );
    pMonster->status = pMonster->status & ~AWA_MOVABLE_LIVE;
    awaEraseBomb( column, floor );
    awaHideSprite( pMonster->sprite );
    awaStartPoint( pMonster->x, pMonster->y, 2 );
    awaAddScore( 40 );
}

void awaHitMonsters()
{
    int i;
    for( i = 0; i < AWA_MAX_MONSTER_COUNT; i = i + 1 )
    {
        int status;
        status = awaMonsters[ i ].status;
        if( ( status & AWA_MOVABLE_LIVE ) != 0 )
        {
            int row;
            row = ( awaMonsters[ i ].y + AWA_COORD_RATE ) >> ( AWA_ROW_SHIFT + AWA_COORD_SHIFT );
            if( ( row & 1 ) == 0 )
            {
                int floor;
                floor = row >> 1;
                if( floor == awaScrollingFloor )
                {
                    int x, column;
                    x = awaMonsters[ i ].x >> AWA_COORD_SHIFT;
                    column = x >> AWA_COLUMN_SHIFT;
                    if( ( x & 1 ) == 0 )
                    {
                        if( awaGetCell( column, row ) == AWA_CELL_BOMB )
                          awaMonsterHitBomb( &awaMonsters[ i ], column, floor );
                    }
                    else
                    {
                        if( awaGetCell( column, row ) == AWA_CELL_BOMB )
                          awaMonsterHitBomb( &awaMonsters[ i ], column, floor );
                        else
                        {
                            column = column + 1;
                            if( awaGetCell( column, row ) == AWA_CELL_BOMB )
                              awaMonsterHitBomb( &awaMonsters[ i ], column, floor );
                        }
                    }
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   MoveFloor (Stage.cpp) - see this file's own header comment for the full
//   hand-trace of why a single call per gated tick reproduces upstream's real
//   observable cadence, and for the direction-asymmetric roll-then-draw
//   sequencing this preserves literally rather than simplifying.
// -----------------------------------------------------------------------------

void awaMoveFloor()
{
    if( awaScrolling == 0 )
    {
        awaScrollShiftView = false;
        return;
    }
    if( awaScrolling == -1 )
    {
        awaHitFlagsShift( -1 );
        awaScrollShiftView = true;
        awaScrolling = -2;
    }
    else if( awaScrolling == -2 )
    {
        awaRollLeft( awaScrollingFloor );
        awaScrollShiftView = false;
        awaScrolling = 0;
        awaSlideFlags( -1 );
        awaHitMonsters();
    }
    else if( awaScrolling == 1 )
    {
        awaHitFlagsShift( 1 );
        awaRollRight( awaScrollingFloor );
        awaScrollShiftView = true;
        awaScrolling = 2;
    }
    else if( awaScrolling == 2 )
    {
        awaScrollShiftView = false;
        awaScrolling = 0;
        awaSlideFlags( 1 );
        awaHitMonsters();
    }
    awaScrollingClock = awaScrollingClock + 1;
}


// -----------------------------------------------------------------------------
//   InitTrying
// -----------------------------------------------------------------------------

void awaInitTrying()
{
    int i, j;
    awaRndIndex = 0;
    awaStageTime = awaMaxTime;

    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 8; j = j + 1 )
          awaStatusChar[ i ][ j ] = 0;
    }
    awaOverlayActive = false;

    // Unpack this stage's own 12 packed bytes (3 bytes/floor row x 4 floors,
    // each byte holding 4 cells at 2 bits each) into the real 48-entry
    // awaStageMap - a direct structural mirror of upstream's own InitTrying()
    // unpacking loop (Stage.cpp). See this file's own header comment for the
    // full derivation of the packed format and the real bug this replaces.
    {
        int fl, g, k, byteIdx;
        byteIdx = 0;
        for( fl = 0; fl < AWA_FLOOR_COUNT; fl = fl + 1 )
        {
            for( g = 0; g < AWA_COLUMN_COUNT / 4; g = g + 1 )
            {
                int b;
                b = awaStageBytes[ awaStageIndex ][ byteIdx ];
                byteIdx = byteIdx + 1;
                for( k = 0; k < 4; k = k + 1 )
                {
                    int cell, column;
                    cell = b & 0x03;
                    if( cell == AWA_CELL_LADDER )
                      cell = cell | ( AWA_CELL_LADDER << 4 );
                    else if( cell == AWA_CELL_BOMB )
                      cell = ( AWA_CELL_BOMB << 4 ) | AWA_CELL_FLOOR;
                    column = g * 4 + k;
                    awaStageMap[ awaMapPtr( column, fl ) ] = cell;
                    b = b >> 2;
                }
            }
        }
    }

    awaPutFlags();
    awaScrolling = 0;
    awaScrollShiftView = false;
    awaFlagBlinkOn = true;
    awaInitMan();
    awaInitMonsters();
    awaInitPoints();
    awaInitBangs();
    awaPrintStatus();
    awaDrawAll();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// Reproduces upstream's own VVramToVram()/SendUL() nibble-interleaving
// exactly - see Cracky's own identical header-comment derivation, reused
// unchanged here since both games share the same Cate-engine VVram->raw-byte
// packing. No hardware-orientation transform - drawn directly at its own
// (rawCol,rawPage), matching this task's own explicit instruction and this
// file's own header comment.
//
// **Updated to OR-combine the map-area's two content layers instead of
// choosing one exclusively**, matching the same fix Cracky's own
// crkComposeRawByte() needed for its "CRACKY" bitmap logo. During the title
// screen (awaMapTextActive), awaVVram now holds ONLY the "AWASS" logo
// bitmap (see awaBeginTitle(), VVram rows 2-5 -> real hardware pages 1-2
// under this port's own page = row/2 mapping) - everywhere else in
// awaVVram is AWA_CHAR_SPACE (index 0, which resolves to an all-zero
// pattern byte, confirmed against awaCharPattern[0]/[1]), while every
// title-screen text piece ("MINI"/"INUFUTO 2026"/"START"/"CONTINUE") lives
// in awaMapTextChar at real pages 3/5/6/7 - entirely disjoint from the
// logo's own pages 1-2, so mapByte and textByte can never both be non-zero
// for the same (col,page) and ORing them can never blend two genuine
// pieces of content together. Outside the title screen (awaMapTextActive
// false, real gameplay), only mapByte is ever returned - awaMapTextChar
// may still hold stale title-screen text at that point (it's never
// re-cleared on leaving the title screen), but it must never be read while
// inactive, exactly like before this change.
int awaComposeRawByte( int rawCol, int rawPage )
{
    if( rawCol < AWA_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte, mapByte, textByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = awaVVram[ rawPage * 2 ][ mapX ];
        lower = awaVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = awaCharPattern[ upper * 2 + 0 ];
            lowerByte = awaCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = awaCharPattern[ upper * 2 + 0 ];
            lowerByte = awaCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = awaCharPattern[ upper * 2 + 1 ];
            lowerByte = awaCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = awaCharPattern[ upper * 2 + 1 ];
            lowerByte = awaCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }

        if( !awaMapTextActive ) return mapByte;

        // Title screen's own text ("MINI"/"INUFUTO 2026"/"START"/
        // "CONTINUE") lives in awaMapTextChar, not the normal map
        // (awaVVram) - see that array's own declaration comment for why.
        // Read exactly the same way the status-zone branch below reads
        // awaStatusChar.
        textByte = awaAsciiPattern[ awaMapTextChar[ rawPage ][ mapX ] * 4 + sub ];
        return mapByte | textByte;
    }
    else
    {
        int statCol, charCol, sub, c;
        statCol = rawCol - AWA_VVRAM_WIDTH * 4;
        if( statCol >= 32 ) return 0;
        charCol = statCol / 4;
        sub = statCol % 4;
        c = awaStatusChar[ rawPage ][ charCol ];
        return awaAsciiPattern[ c * 4 + sub ];
    }
}

void awaRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( awaOverlayActive && page == awaOverlayPage &&
                col >= awaOverlayCol * 4 && col < awaOverlayCol * 4 + awaOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - awaOverlayCol * 4 ) / 4;
                sub = ( col - awaOverlayCol * 4 ) % 4;
                value = awaAsciiPattern[ awaAsciiIndex( awaOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = awaComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine - converts Main.cpp's own goto-chained labels around one
//   big do-while (plus several real WaitMelody()/WaitTimer() blocking waits)
//   into an explicit frame-stepped state machine, the same treatment every
//   port in this project needs and the same state shape Cracky's own Main()
//   translation already established: AWA_STATE_TITLE (Title()'s own internal
//   key-poll loop), AWA_STATE_START_JINGLE (the blocking Sound_Start() held
//   before play begins), AWA_STATE_PLAYING (the main Clock-gated loop - see
//   this file's own header comment for why a single call per gated tick
//   reproduces upstream's real per-Clock-unit cadence, including MoveFloor's
//   own degenerate multi-call-per-tick behavior), AWA_STATE_LOSE_ANIM
//   (LooseMan()'s own 8-step blink-and-beep loop), AWA_STATE_GAMEOVER_JINGLE,
//   AWA_STATE_CLEAR_WAIT (the real WaitTimer(10) pause), AWA_STATE_CLEAR_JINGLE,
//   and AWA_STATE_BONUS_TALLY (the real `while(StageTime>=BonusRate){...
//   Sound_Beep();}` bonus-countdown loop, each iteration blocked by its own
//   note upstream - converted to one decrement+beep per real tick).
// -----------------------------------------------------------------------------

void awaBeginTitle()
{
    int i;

    for( i = 0; i < AWA_VVRAM_HEIGHT; i = i + 1 )
    {
        int j;
        for( j = 0; j < AWA_VVRAM_WIDTH; j = j + 1 )
          awaVVram[ i ][ j ] = AWA_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 8; j = j + 1 )
          awaStatusChar[ i ][ j ] = 0;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < AWA_VVRAM_WIDTH; j = j + 1 )
          awaMapTextChar[ i ][ j ] = 0;
    }
    awaMapTextActive = true;
    awaOverlayActive = false;
    awaHideAllSprites();
    // Matches Cracky's own fix for the identical bug ("on game over, the
    // time value remains visible on titlescreen"), applied proactively here
    // rather than waiting for a report: awaPrintStatus() below redraws TIME
    // from whatever awaStageTime last held during real gameplay - reset
    // first so the title screen always shows a fresh "TIME 000".
    awaStageTime = 0;
    awaPrintStatus();

    // A real bug found via live Puppeteer testing during this port's own
    // verification pass: every one of upstream's own Title() text pieces
    // below ("MINI", "INUFUTO 2026", "START"/"CONTINUE") uses an ABSOLUTE
    // VVram-column position within the MAP area (Status.cpp: `PrintS(Vram +
    // VramRowSize*N + ABSOLUTE_COL*VramStep, ...)`, no LeftX offset at all) -
    // genuinely different from PrintStatus()'s own always-LeftX-relative
    // text, which really does belong in the status zone. An earlier draft of
    // this port routed ALL of this through awaPrintS/awaPrintC (which only
    // ever renders into awaStatusChar, visible solely at cols96-127) -
    // matching Cracky's own crkBeginTitle(), which has this identical bug
    // too (confirmed directly against Cracky's own real upstream Status.cpp)
    // - so the whole title screen rendered crammed into the status zone
    // instead of the map area, with the map area left completely blank.
    // Fixed by routing through awaPrintMapC/awaPrintMapS instead (see
    // awaMapTextChar's own declaration comment), at upstream's real absolute
    // columns - also restored the missing "MINI" text and the "2026" year
    // suffix, both silently dropped in the original mis-targeted version.
    //
    // **The "AWASS" title WORD itself is not plain text at all upstream -
    // it's a real, big pixel-art bitmap logo (Status.cpp's `Title()`,
    // `TitleBytes[]`), the single largest, most prominent element on the
    // whole screen.** An earlier draft here simplified it to plain small
    // text instead, reasoning it was "purely decorative" - the exact same
    // wrong judgment call gameCracky.c's own crkBeginTitle() independently
    // made for its own "CRACKY" logo, later corrected there after a real
    // user-supplied hardware photo proved that reasoning wrong (see that
    // file's own header for the full story). Fixed the identical way here:
    // the real bitmap (awaTitleBytes[], byte-diff-verified against upstream)
    // is drawn directly into awaVVram at its own real position (VVram rows
    // 2-5, i.e. real hardware pages 1-2 under this port's own `page =
    // row/2` mapping - matching upstream's `VVramFront + VVramWidth*2 +
    // TitleLeft` starting offset exactly, where `TitleLeft = (VVramWidth -
    // 4*TitleLength)/2 = (24 - 4*5)/2 = 2` for this game's own 5-letter
    // title, unlike Cracky's own 6-letter "CRACKY" which happens to compute
    // TitleLeft=0). awaComposeRawByte() was updated to OR-combine this
    // awaVVram content with awaMapTextChar's own text layer rather than
    // choosing one exclusively, since the two occupy disjoint page ranges by
    // construction (logo: pages 1-2 only; MINI/START/CONTINUE/credit: pages
    // 3/5/6/7 - see that function's own comment) - the same OR-combine
    // technique Cracky's own fix uses.
    {
        char sMini[4] = { 'M', 'I', 'N', 'I' };
        char sInufuto[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                awaVVram[ 2 + row ][ 2 + ch * 4 + col ] = awaTitleBytes[ idx ];
                idx = idx + 1;
            }
        awaPrintMapS( 3, 17, sMini, 4 );
        awaPrintMapS( 7, 12, sInufuto, 12 );
        awaPrintMapS( 5, 9, sStart, 5 );
        awaPrintMapS( 6, 9, sContinue, 8 );
    }

    awaSelection = 0;
    awaSelectionChanged = true;
    awaPrevLeft = 0; awaPrevRight = 0; awaPrevUp = 0; awaPrevDown = 0; awaPrevFire = 0;
    awaState = AWA_STATE_TITLE;
}

void awaUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !awaPrevLeft ) || ( right && !awaPrevRight ) ||
                ( up && !awaPrevUp ) || ( down && !awaPrevDown ) );
    justFire = ( fire && !awaPrevFire );
    awaPrevLeft = left; awaPrevRight = right; awaPrevUp = up; awaPrevDown = down; awaPrevFire = fire;

    if( awaSelectionChanged )
    {
        awaSelectionChanged = false;
        // ArrowX=8 upstream - the cursor sits one char-cell left of the
        // "START"/"CONTINUE" text itself (col9, see awaBeginTitle()), in the
        // map-area text grid, not the status zone (same bug/fix as every
        // other title-screen text piece - see awaMapTextChar's own
        // declaration comment).
        if( awaSelection == 0 )
          awaPrintMapC( 5, 8, '>' );
        else
          awaPrintMapC( 5, 8, ' ' );
        if( awaSelection == 1 )
          awaPrintMapC( 6, 8, '>' );
        else
          awaPrintMapC( 6, 8, ' ' );
    }

    if( justFire )
    {
        // selection==1 ("CONTINUE") means upstream deliberately skips
        // resetting CurrentStage, resuming from wherever the player last
        // left off - matches upstream's own `if (selection==0)
        // CurrentStage=0;` exactly.
        awaPendingContinue = ( awaSelection == 1 );
        awaScore = 0;
        if( !awaPendingContinue )
          awaCurrentStage = 0;
        awaRemainCount = 3;
        // Leaving the title screen - hand the map area (cols0-95) back to
        // the real gameplay renderer (awaVVram via awaDrawAll() below)
        // instead of the title's own text grid.
        awaMapTextActive = false;
        awaInitStage();
        awaInitTrying();
        awaDrawAll();
        awaStartSeq( 1, AWA_MELODY_START );
        awaState = AWA_STATE_START_JINGLE;
        awaRender();
        return;
    }
    if( justDir )
    {
        awaSelection = awaSelection ^ 1;
        awaSelectionChanged = true;
    }
    awaRender();
}

void awaUpdateStartJingle()
{
    if( !awaSeqPlaying( 1 ) )
    {
        awaStartBgm();
        awaClock = 0;
        awaMonsterNum = 0;
        awaTimeDenom = AWA_MAX_TIME_DENOM;
        awaState = AWA_STATE_PLAYING;
    }
    awaRender();
}

void awaBeginLose()
{
    awaStopBgm();
    awaAnimStep = 0;
    awaWaitFrames = 0;
    awaState = AWA_STATE_LOSE_ANIM;
}

void awaUpdateLoseAnim()
{
    if( awaWaitFrames > 0 )
    {
        awaWaitFrames = awaWaitFrames - 1;
        awaRender();
        return;
    }

    awaShowSprite( &awaMan, awaLoosePatterns[ awaAnimStep & 3 ] );
    awaDrawAll();
    awaStartSeq( 0, AWA_MELODY_LOOSE );
    awaAnimStep = awaAnimStep + 1;
    awaWaitFrames = awaNoteFrames( 1 );

    if( awaAnimStep >= 8 )
    {
        awaRemainCount = awaRemainCount - 1;
        if( awaRemainCount > 0 )
        {
            awaInitTrying();
            awaDrawAll();
            awaOverlayActive = false;
            awaStartSeq( 1, AWA_MELODY_START );
            awaState = AWA_STATE_START_JINGLE;
        }
        else
        {
            awaPrintGameOver();
            awaStartSeq( 1, AWA_MELODY_GAMEOVER );
            awaState = AWA_STATE_GAMEOVER_JINGLE;
        }
    }
    awaRender();
}

void awaUpdateGameOverJingle()
{
    if( !awaSeqPlaying( 1 ) )
      awaBeginTitle();
    else
      awaRender();
}

void awaBeginClearWait()
{
    awaStopBgm();
    awaWaitFrames = 10;
    awaState = AWA_STATE_CLEAR_WAIT;
}

void awaUpdateClearWait()
{
    if( awaWaitFrames > 0 )
    {
        awaWaitFrames = awaWaitFrames - 1;
        awaRender();
        return;
    }
    awaStartSeq( 1, AWA_MELODY_CLEAR );
    awaState = AWA_STATE_CLEAR_JINGLE;
    awaRender();
}

void awaUpdateClearJingle()
{
    if( !awaSeqPlaying( 1 ) )
    {
        awaWaitFrames = 0;
        awaState = AWA_STATE_BONUS_TALLY;
    }
    awaRender();
}

void awaUpdateBonusTally()
{
    if( awaWaitFrames > 0 )
    {
        awaWaitFrames = awaWaitFrames - 1;
        awaRender();
        return;
    }

    if( awaStageTime >= AWA_BONUS_RATE )
    {
        awaAddScore( 2 );
        awaStageTime = awaStageTime - AWA_BONUS_RATE;
        awaPrintTime();
        awaStartSeq( 0, AWA_MELODY_BEEP );
        awaWaitFrames = awaNoteFrames( 1 );
        awaRender();
        return;
    }

    awaStageTime = 0;
    awaPrintStatus();
    awaCurrentStage = awaCurrentStage + 1;
    awaInitStage();
    awaInitTrying();
    awaDrawAll();
    awaStartSeq( 1, AWA_MELODY_START );
    awaState = AWA_STATE_START_JINGLE;
    awaRender();
}

// Direct structural mirror of Main.cpp's own Clock-gated do-while body - see
// this file's own header comment for the full hand-trace establishing that
// one call per gated tick (AWA_TICK_DIVISOR, matching upstream's real
// WaitTimer(8) cadence) reproduces every real observable frame, including
// MoveFloor's own degenerate multi-call-per-real-tick behavior.
void awaUpdatePlaying()
{
    awaTickCounter = awaTickCounter + 1;
    if( awaTickCounter < AWA_TICK_DIVISOR )
    {
        awaRender();
        return;
    }
    awaTickCounter = 0;

    awaMoveMan();
    if( awaMonsterNum >= 0 )
    {
        awaMoveMonsters();
        awaMonsterNum = awaMonsterNum - 9;
    }
    awaMonsterNum = awaMonsterNum + 3;

    awaTimeDenom = awaTimeDenom - 1;
    if( awaTimeDenom == 0 )
    {
        awaStageTime = awaStageTime - 1;
        awaTimeDenom = AWA_MAX_TIME_DENOM;
        awaPrintTime();
        if( awaStageTime == 0 )
        {
            awaPrintTimeUp();
            awaDrawAll();
            awaRender();
            awaBeginLose();
            return;
        }
    }

    awaUpdatePoints();
    awaUpdateBangs();

    // Tracks upstream's own raw Clock value (incrementing by 4 per gated
    // tick here, since 4 raw Clock units elapse per one real WaitTimer(8)
    // period - see this file's own header comment) purely so `(Clock&7)==0`
    // can be reproduced bit-for-bit, matching upstream's own BlinkFlags()
    // call cadence exactly.
    awaClock = awaClock + 4;
    if( ( awaClock & 7 ) == 0 )
      awaFlagBlinkOn = !awaFlagBlinkOn;

    awaMoveFloor();

    awaDrawAll();

    if( ( awaMan.status & AWA_MOVABLE_LIVE ) == 0 )
    {
        awaRender();
        awaBeginLose();
        return;
    }

    if( awaFlagCount == 0 )
    {
        awaRender();
        awaBeginClearWait();
        return;
    }

    awaRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameAwass_init()
{
    int i;

    awaHiScore = 0;
    awaScore = 0;
    awaCurrentStage = 0;
    awaRemainCount = 3;
    awaStageTime = 0;
    awaRndIndex = 0;
    awaMonsterClock = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        awaSeqActive[ i ] = 0;
        awaSeqMelody[ i ] = AWA_MELODY_NONE;
    }
    awaOverlayActive = false;
    awaTickCounter = 0;

    awaBeginTitle();
}

void gameAwass_update()
{
    awaAdvanceSound();

    if( awaState == AWA_STATE_TITLE )
      awaUpdateTitle();
    else if( awaState == AWA_STATE_START_JINGLE )
      awaUpdateStartJingle();
    else if( awaState == AWA_STATE_PLAYING )
      awaUpdatePlaying();
    else if( awaState == AWA_STATE_LOSE_ANIM )
      awaUpdateLoseAnim();
    else if( awaState == AWA_STATE_GAMEOVER_JINGLE )
      awaUpdateGameOverJingle();
    else if( awaState == AWA_STATE_CLEAR_WAIT )
      awaUpdateClearWait();
    else if( awaState == AWA_STATE_CLEAR_JINGLE )
      awaUpdateClearJingle();
    else if( awaState == AWA_STATE_BONUS_TALLY )
      awaUpdateBonusTally();
}
