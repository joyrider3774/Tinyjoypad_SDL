// =============================================================================
// LIFT mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_lift`, same "known author,
// unstated license" situation as this project's own sibling Cate-engine
// port, Cracky) - a climbing platformer: walk left/right across floors,
// ride vertical lifts up between them, and collect every item on a 12-
// column x 3-floor stage before the stage timer runs out, while dodging
// chasing monsters. 7 hand-authored stages (cycled repeatedly, with the
// per-stage time bonus shrinking every full lap through them), 3 lives,
// real in-session hi-score tracking (upstream has no EEPROM at all, same
// as Cracky - this is a CH32V003 RISC-V microcontroller, not AVR, so this
// project's own `eeprom_*` shim/avrCompat widening aren't relevant here).
//
// Ported directly on top of the already-proven `gameCracky.c` shim/
// rendering approach from the very same author's other Cate-engine game -
// see that file's own header comment for the full story of how its own
// hardware-orientation question was resolved. That resolution is reused
// here verbatim, with zero re-investigation: `InitOled()`'s two SegRemap/
// ComScanDec register writes exist purely to compensate for a physical
// panel-mounting quirk on some real SSD1306 breakout modules, with no
// equivalent to correct for in this software recreation - **no column
// mirror, no page reorder, no bit-reversal of any kind**. Every raw
// (col,page) byte is drawn directly at its own natural position via
// `md_drawColumn(col, page, value)`.
//
// **Structural differences from Cracky, despite the shared engine**:
// Movable/Actor here are two separate upstream structs (`Movable{x,y,
// sprite,clock}`, `Actor{Movable _; status; c; dx;}`), flattened into one
// combined `LftActor{x,y,sprite,clock,status,c,dx}` for Man/Monster (no
// vertical `dy` at all - Man/Monster never move themselves vertically,
// only via gravity or by riding a lift) rather than kept as a nested
// sub-struct - avoids ever needing a `->_.x`-style indirection chain, the
// same flattening precedent this whole project already uses for every
// other class-hierarchy port. `Lift{Movable _; x,bottom,top;}` is
// similarly flattened into `LftLift{x,y,sprite,clock,bottom,top}` -
// upstream's own separate `Lift.x` field and its embedded `_.x` are
// always numerically identical here (`_.x = x << CoordShift` with
// CoordShift permanently 0), so merging them into one field is a safe,
// deliberate simplification, not a behavior change. `CellMap` itself is
// genuinely NOT nibble-packed here (unlike Cracky's own 4-bit-per-cell
// `crkCellMap`) - it's one full byte (ported as one full `int`) per
// (column,floor) cell, 36 cells total (12 columns x 3 floors), since the
// richer cell encoding (floor/lift-shaft/lift-bottom/item type bits plus
// a 3-bit index into whichever table applies) needs more than 4 bits.
//
// **The map+status rendering split works out identically to Cracky's own
// (0-95 map / 96-127 status), just with a different row split**: VVram
// here is only 24x14 (`VVramHeight = 16-2`), not Cracky's 24x16 - the top
// 2 VVram rows are a fixed roof/ceiling decoration (not part of the
// scrollable stage), and the 12-row stage itself (3 floors x 4 rows) sits
// below that. 14 VVram rows / 2 = only **7** physical hardware pages get
// real map content (pages 0-6) - the map's own real columns (0-95) are
// simply never touched on page 7 at all (`VVramToVram()` only loops
// `VVramHeight/2` times). Reproduced directly rather than needing a fix:
// `lftComposeRawByte()` returns a flat 0 (black) for any (col<96, page==7)
// request, matching the known, constant, always-blank appearance this
// area has on real hardware (the same "reproduce a known-constant real-
// VRAM-persistence result directly" precedent already used for Wren
// Rollercoaster's own blank sky). Page 7's own real columns 96-127 are
// still live status text (the lives/remain-count display), exactly like
// every other page.
//
// **A genuine byte-truncation-reliance bug, caught proactively by
// inspection before ever compiling, not by a test run** - the first
// instance of this project's own well-documented "AVR-implicit-narrow-
// type-reliance" bug family found in this port: upstream's own
// `FillCellMap()` decodes a packed floor-layout bitstream with
// `mask <<= 1; if (mask == 0) { advance to next byte; mask = 1; }` -
// relying on a real `uint8_t mask` silently wrapping `0x80 << 1` down to
// `0` to detect "just consumed the 8th bit of this byte, advance to the
// next one." Vircon32's plain, non-truncating `int` would instead compute
// `256`, so `mask == 0` would never fire again after the very first byte,
// permanently desyncing the whole floor-layout bitstream from that point
// on (every column after the first 8 would silently read stale/wrong
// bits) - not a cosmetic glitch, a genuinely broken level layout on
// literally every stage. **Fixed** the same way as this whole project's
// very first documented bug (`md_drawColumn()`'s own byte-truncation
// fix): mask the shifted value explicitly, `mask = (mask << 1) & 0xFF;`,
// reproducing AVR's real 8-bit wraparound behavior on purpose rather than
// relying on an implicit narrowing this dialect doesn't have.
//
// **A second, much smaller latent-overrun risk, also fixed proactively**:
// `HitItems()`'s own combo-match bonus (`StartPoint(x,y,Rate); ++Rate;`)
// increments `Rate` completely unbounded on every consecutive same-type
// match, but `StartPoint()` only ever indexes a real 4-entry point-value
// table (and a matching 4-frame point-sprite range) - `Rate` reaching 4
// harmlessly overruns into adjacent PROGMEM on real AVR flash, but is a
// genuine out-of-bounds table read on Vircon32. Matching this project's
// own "preserve behavior, guard the crash" precedent (e.g. Tiny Arena's
// `Lvl1` off-by-one fix): `Rate` itself is still allowed to grow
// unbounded exactly like upstream (nothing else in the game reads it), it
// is only ever *clamped* right at the two places that actually index a
// table with it, inside `lftStartPoint()` itself.
//
// **Multi-rate real-time tick, decoded into one clean whole-tick
// divisor**: upstream's own main loop is NOT a single fixed-rate loop
// like Cracky's - it's `Clock`-parity-gated into three nested real-time
// rates (`Clock&1`: Fall+redraw, roughly every real `WaitTimer(4)` =
// 66.67ms; `Clock&3`: Man/Monster/timer logic, every other one of those;
// `Clock&7`: lift movement, every 4th one), with every ODD `Clock` value
// doing a full loop iteration that touches literally nothing (none of
// `&1`/`&3`/`&7` can ever match an odd number) other than incrementing
// `Clock` and re-checking the loop condition - a pure no-op that costs
// real time on AVR only insofar as the loop body itself takes time to
// skip past (negligible next to the real `WaitTimer(4)` blocking call on
// the following even iteration). Rather than simulate those no-op odd
// iterations at all, this port skips them entirely: `lftClock` only ever
// takes on the *even* values `Clock` would have held (0,2,4,6,8,...),
// advancing by 2 every real "tick" instead of 1 every real loop
// iteration - which reproduces the exact same `Clock&1`/`&3`/`&7` results
// upstream's own bitwise tests would have produced, with the exact same
// literal mask constants, just without wasting a whole port state on a
// value that can never do anything. One real logic tick = the real
// wall-clock duration of upstream's own `WaitTimer(4)` call (4 real 60Hz
// SysTick periods, confirmed via `Timer.cpp`'s own `kTimerHz=60`) = 4
// real Vircon32 engine frames at the same 60fps - `LFT_TICK_DIVISOR = 4`,
// gating the whole `gameLift_update()` playing-state body together
// (matching the majority "gate everything, not just movement" precedent
// already established by NumberPlace/HollowSeeker/t2048/Doc/Pacman/Pipe/
// Cracky in this project, rather than the movement-only/redraw-stays-60fps
// split used elsewhere) - `WaitTimer(4)` itself is simply dropped, its
// real-time pacing already fully provided by the divisor.
//
// **Sound**: same real 3-tone-channel software tracker shape as Cracky's
// own `Sound.cpp` (`Tempo=180` here, vs Cracky's `160`) - every call goes
// straight to `md_playTone()`/`md_stopTone()` exactly like Cracky, using
// the identical `crkMelodyLength()`/`crkMelodyValue()`-style id-based
// resolver and the identical 3-slot frame-stepped sequencer shape
// (0=one-shot SFX, 1=jingle/BGM-voice-A, 2=BGM-voice-B). Every melody
// table was byte-diff-extracted via a small Python script that evaluates
// the real upstream `NoteLength`/`Scale` enum expressions numerically
// (not hand-transcribed), confirming every table's own element count
// against the real upstream array before ever pasting it in. Each note's
// real duration is derived from `SoundHandler()`'s own real tempo formula
// (a channel advances once every `(600/2)/Tempo` real 60Hz ticks) exactly
// like Cracky's own `crkNoteFrames()`, just with Lift's own `Tempo=180`
// giving `300/180 = 5/3` ticks per note-length-unit instead of Cracky's
// `300/160 = 1.875`.
//
// **Item-matching combo mechanic, genuinely new versus Cracky**: collect
// an item of a DIFFERENT type than the last one collected for a flat +5;
// collect one of the SAME type as the immediately-previous one for an
// escalating bonus popup (`StartPoint`, values 10/20/40/80) instead,
// which also resets the "last type" tracker so a third consecutive same-
// type match needs a fresh two-in-a-row streak to trigger again -
// ported as a direct, faithful translation of `HitItems()`'s own real
// type-comparison state machine.
//
// **The title screen's own real "LIFT" bitmap logo (a hand-drawn 4-letter
// glyph table, `Status.cpp`'s own `TitleBytes[]`) was initially simplified
// to plain status-area text**, matching what was at the time Cracky's own
// established precedent for simplifying a purely decorative, non-gameplay
// title graphic - **this judgment call was later reversed, in both files,
// once a real user-supplied hardware photo of Cracky proved it wrong; see
// "A real bitmap logo restored, matching Cracky's own identical fix"
// further below for the final, current state.** A proactive fix applied
// from the start rather than rediscovered: reset `lftStageTime = 0`
// before `lftPrintStatus()` on the title screen, since nothing else
// clears it, and Cracky's own header comment already documents this
// exact "a stale TIME value lingers on the title screen after a game
// over" bug and fix for the identical situation.
//
// **A whole-file architectural fix, ported directly from Cracky's own
// identical, already-solved case, applied after a real user-supplied
// hardware photo of Cracky overturned this entire family's original
// title-screen text model** (see `gameCracky.c`'s own header comment,
// "A real user-supplied hardware photo overturns Cracky's own title-
// screen design", for the full story - summarized here since it applies
// verbatim to this file too). Upstream's real `PrintC()`/`PrintS()` write
// to a genuine 32-char-cell-wide row (`VramStep=4` real pixels/cell, 128
// real pixels / 4 = 32 cells), not the narrow 8-cell status-only slice
// this port originally modeled `lftStatusChar` as - the status labels
// (SCORE/STAGE/TIME/REMAIN) really are confined to upstream's own real
// `LeftX=24` columns (24-31, 8 cells), but the title screen's own text
// (the logo, "MINI", "START"/"CONTINUE", the credit line) lives at
// upstream's real columns 8-23, using the exact same shared
// `PrintC()`/`PrintS()` mechanism at different column arguments - not a
// separate, narrower grid at all, and - critically - NOT confined to only
// the pages nothing else uses either: since a page is 32 real columns
// wide, a status label at cols 24-31 and title text at cols 8-23 (or
// less) can share the SAME page with zero collision, something the
// original 8-column model had no way to represent. This is exactly why
// upstream's own real `Title()` freely places START/TIME on the same
// page (5) and CONTINUE/(nothing) on another (6) - it was never trying
// to dodge the status labels by hunting for an entirely free page, only
// by staying within its own real, wider column budget. Fixed the exact
// same way as Cracky: `lftStatusChar` widened from `int[8][8]` to
// `int[8][32]`, every status-label print call updated to upstream's real
// `LeftX`-based columns, and a new `lftFullWidthText` flag (true only
// during `LFT_STATE_TITLE`) lets `lftComposeRawByte()` read the full
// 32-column range instead of just the narrow status slice while the
// title screen is showing. `lftBeginTitle()` was rewritten to place
// "LIFT"/"MINI"/"START"/the now-un-truncated "CONTINUE"/"INUFUTO 2026"
// all at (a mix of) upstream's own real literal columns and, for the two
// purely decorative bitmap-logo replacements ("LIFT" itself, matching
// Cracky's own "CRACKY" precedent), a free page chosen the same way
// Cracky chose its own - nothing here needs trimming, relocating, or
// dropping anymore. See `lftStatusChar`'s own declaration comment and
// `lftBeginTitle()`'s own comment below for the exact column layout, and
// the dedicated verification-pass bug entry further below for how the
// missing 'L' glyph (needed to spell "LIFT" through the shared reduced
// font) was resolved - the same "append a hand-built glyph at the end of
// the font table" technique already proven for the sibling Hopman port's
// own missing 'H'.
//
// **A real bitmap logo restored, matching Cracky's own identical fix,
// found and applied in a later pass than everything above** - a second,
// related architectural issue to the whole-file fix just described:
// Cracky's own title screen was found (via a real user-supplied hardware
// photo) to have wrongly substituted its actual pixel-art wordmark logo
// for small plain text, on the mistaken assumption it was "purely
// decorative" - it's the single largest, most prominent element on the
// whole title screen, not a throwaway detail. This applies verbatim to
// this file too: `lftBeginTitle()`'s own "LIFT" text (drawn via the
// shared reduced font plus the hand-built 'L' glyph described above) has
// been replaced with upstream's own real `TitleBytes[]` bitmap logo (a
// new `lftTitleBytes[64]` table, byte-diff-verified against the real
// upstream `Status.cpp` source via a small Python script - 64 values,
// matching exactly), drawn directly into `lftVVram` at upstream's own
// real position (row 2, column `TitleLeft = (VVramWidth -
// 4*TitleLength)/2 = 4`) rather than as plain status-grid text. This also
// required rewriting `lftComposeRawByte()` from a plain if/else into an
// OR-combining compose (mirroring `crkComposeRawByte()` exactly) - the
// previous if/else meant `lftFullWidthText` disabled VVram/map rendering
// ENTIRELY while active, so the logo could never have reached the screen
// regardless of what was drawn into `lftVVram`; now the map byte and the
// status-text byte are computed independently and OR-combined, safe
// because the logo (pages 1-2) and every status-text element visible on
// the title screen (pages 0/3/5/6/7) occupy genuinely disjoint page
// ranges. `Chars.cpp`'s own "logo" glyph range (`CharPattern[]`'s first
// 32 bytes, indices 0-15) was independently re-checked against this
// port's own `lftCharPattern[0..31]` and confirmed already byte-identical
// - no fix needed there, only the title-drawing/compose logic. The
// hand-built 'L' glyph itself is left in `lftAsciiPattern`/
// `lftAsciiIndex()` unchanged (now unused by the title word specifically,
// but harmless, and nothing else in this file needs it removed) - see
// `lftTitleBytes`'s own declaration comment and `lftBeginTitle()`'s own
// comment for the full detail. Verified via a compile-only check
// (`compile src/main.c`, a clean "global variables" success line with no
// errors) per this pass's own explicit no-play-testing instruction - not
// independently re-verified live in the emulator this session.
//
// =============================================================================
// Verification pass (this session): this port had only ever been test-
// compiled, never played - a full line-by-line rendering-fidelity and
// game-state-progression audit against the real upstream source, plus
// live Puppeteer play, found and fixed five real bugs.
//
// **1) A genuinely broken title-screen word, found only by actually
// launching and looking, not by reading code**: `lftBeginTitle()` used to
// print "LIFT" as plain status-grid text via `lftPrintS(1,0,sLift,4)`.
// The shared 27-glyph `lftAsciiPattern` font (" 0123456789>ACEFGIMNOPRSTUV",
// the same real table upstream's own `PrintC()` uses - confirmed byte-
// identical to upstream via a script-driven diff) has **no 'L' glyph at
// all** - `lftAsciiIndex('L')` falls through its whole search and returns
// 0 (space), so the word silently rendered as " IFT" (a blank cell where
// 'L' should be), visible immediately on the real title screen. This
// wasn't a possible upstream oversight to preserve faithfully either:
// upstream never actually tries to spell "LIFT" through this limited
// font at all - its own real title uses a separate, hand-drawn bitmap
// logo (`TitleBytes[]`, rendered via `Put2C`/`CharPattern`, not
// `PrintC`/`AsciiPattern`) specifically because the mini-font can't
// represent every letter. A second, independent bug compounded it: page 1
// (where "LIFT" was drawn, columns 0-3) is not actually free - it's the
// exact page `lftPrintScore()` writes the live score's own digits into
// (columns 2-7, called moments earlier via `lftPrintStatus()`) - so even
// with a working glyph, "LIFT" would still have clobbered 2 of the 5
// score digits every time the title screen was shown, since the status
// grid was (at the time this was first found) still modeled as only 8
// columns wide - any two labels sharing a page collided regardless of
// which columns they'd have used on real, wider hardware. **Fixed at the
// time** by simply dropping the word (the menu already shows "LIFT" as
// the game's name before it's ever launched) rather than force-fitting it
// onto an already-claimed page - matching Cracky's own contemporaneous
// workaround for the identical situation.
//
// **This was later fully superseded, not just patched further**, once
// the real user-supplied hardware photo of Cracky (see this file's own
// top-of-file comment, "A whole-file architectural fix") proved the whole
// premise - "title text and status labels must fight over the same
// cramped 8-column page" - was wrong: a real page is 32 columns wide, so
// "LIFT" (cols 0-23) and the score digits (cols 24-31) were never
// actually going to collide on real hardware at all. With `lftStatusChar`
// widened to `[8][32]` and `lftFullWidthText` added, "LIFT" is restored
// as real text (page 2, col 0 - the same free page Cracky's own "CRACKY"
// title word uses, chosen for consistency rather than upstream's own
// literal bitmap-logo position, matching this port's established
// decorative-title-graphic-simplification precedent). Restoring it hit
// the exact missing-'L'-glyph problem described above again, this time
// fixed properly (not worked around) with a hand-built glyph appended to
// `lftAsciiPattern`/`lftAsciiIndex()`'s search table - the same technique
// already proven for the sibling Hopman port's own missing 'H' (see that
// file's own header comment for the precedent this follows). Verified via
// a fresh isolated Puppeteer test build: the title screen now shows
// "LIFT" (page 2), "SCORE"/live score digits (page 0/1), "MINI" (page 3),
// "STAGE N" (page 3), "START"/"TIME ..." sharing page 5, "CONTINUE" (now
// the full 8-letter word, page 6), and "INUFUTO 2026" (page 7) - all
// fully visible, non-overlapping, and un-truncated.
//
// **2) `lftPrintScore()`'s own column offset was doubled**, writing the
// 5-digit score at status-grid columns 4-8 and a trailing static '0' at
// column 9, instead of upstream's real columns 2-6 and 7
// (`PrintNumber5(Vram + VramRowSize*1 + (LeftX+2)*VramStep, Score)` -
// `LeftX` is the status region's own real origin, already fully
// accounted for by `lftComposeRawByte()`'s own column math, so it should
// never be added a second time as a local offset). Since
// `lftStatusChar` was only 8 columns wide (valid 0-7) at the time, columns
// 8 and 9 were genuine out-of-bounds writes - `[1][8]`/`[1][9]` spilled
// into the very next page's own row 0/1 (`lftStatusChar[2][0]`/`[2][1]`),
// so the score's own last digit and its trailing '0' visibly bled onto
// the row below TIME/STAGE as stray characters. **Fixed at the time** by
// using the correct local columns (2 and 7) - and, once `lftStatusChar`
// was later widened to `[8][32]` (see item 1's own follow-up above),
// re-expressed as upstream's real, literal columns (26 and 31) directly,
// the same "local col = real col - LeftX" -> "just use the real col"
// change every status-print call in this file went through. Verified via
// Puppeteer both times: the score renders cleanly with no stray digits
// appearing on adjacent rows, before and after the later widening.
//
// **3) `lftUpdateLoseAnim()` cut its own death-flash animation one frame
// short.** Upstream's real `LooseMan()` is a blocking
// `do { ShowSprite(...); Sound_Loose(); DrawAll(); ++i; } while (i<8);` -
// all 8 flashes, including the 8th/last one, get their own full wait for
// `Sound_Loose()` to finish before the function (and the whole animation)
// ends. The ported state machine instead checked `lftAnimStep >= 8` and
// transitioned straight to the retry/game-over path in the SAME call that
// had just shown the 8th flash and started its own wait timer - so that
// final wait was set but never consumed, and the last frame of the
// animation was never genuinely held on screen (overwritten by a fresh
// level's own first render on the retry path, or masked by the "GAME
// OVER" overlay in that same frame on the game-over path). **Fixed** by
// gating the post-animation transition behind `lftAnimStep < 8` too, so
// the 8th flash's own wait is genuinely consumed (one more pass through
// the wait branch) before the transition runs - matching upstream's real
// "show + wait, 8 times total, then transition" shape exactly. Verified
// via an extended live Puppeteer play session (deliberately walking into
// a monster repeatedly): RemainCount correctly stepped 3->2->1->0 across
// several real deaths (confirmed with a temporary on-screen debug digit,
// removed after), each retry correctly restored a fresh TIME/SCORE/board
// state, and the final death correctly reached the GAME OVER jingle and
// returned to the title screen - no stuck or frozen state at any point.
//
// **4) A real audio-ordering deviation**: upstream's own level-clear
// sequence is `WaitTimer(30); StopBGM(); Sound_Clear();` - the background
// music keeps playing through the real 500ms pause right after the last
// item is collected, only cut off the instant the CLEAR jingle itself
// begins. The port's `lftBeginClearWait()` instead called `lftStopBgm()`
// immediately on entering that state, silencing the BGM for the whole
// 500ms gap before the jingle even started. **Fixed** by moving
// `lftStopBgm()` into `lftUpdateClearWait()`, firing only once the wait
// genuinely elapses - matching upstream's real ordering. Audio-only, not
// independently verified by ear this session (matching this project's own
// standing caveat for audio-correctness fixes elsewhere), but the control-
// flow change itself was confirmed safe (build clean, no state-machine
// regression) via the same extended play session above.
//
// **5) A real, if narrow, crash risk in `lftCellMapPtr()`**, found by
// inspection while auditing every `>>` in the file for the logical-vs-
// arithmetic-shift hazard already documented extensively elsewhere in
// this project (HollowSeeker/Tiny Pipe/Nohzdyve/TinY Fi/Gilbert in the
// Downland, etc). Most callers guard `y >= LFT_STAGE_TOP` before calling
// this, matching upstream's own equivalent guards - but
// `lftDecideDirection()`'s two unconditional reads (matching upstream's
// identically-unguarded `DecideDirection()` in `Monster.cpp`) and
// `lftHitItems()` don't. A Man/Monster riding a lift right at its
// topmost point (`pLift->y == pLift->top`, a real, reachable position -
// e.g. stage 0's own first lift has `top=3`) lands at
// `y = pLift->y - LFT_COORD_RATE*2`, one row ABOVE `LFT_STAGE_TOP`. On
// real AVR, `(y - StageTop)` is a `uint8_t` subtraction that harmlessly
// wraps to a bounded garbage value; on Vircon32, the same subtraction is
// a genuine negative `int`, and `>>` is a documented *logical* shift -
// turning a small negative offset into an astronomically large positive
// one and reading `lftCellMap` wildly out of bounds (a real "ERROR:
// INVALID MEMORY READ" risk). **Fixed** by never shifting a negative row
// value (branch on sign first, matching this project's own established
// pattern) plus a defensive final clamp into the array's own real bounds
// as a centralized safety net for every caller, guarded or not. Not
// specifically reproduced live this session (a precise, multi-tick-timed
// edge case), but the fix is a pure guard with no effect on the common,
// already-extensively-verified case (lift-riding, monster chasing, and
// item pickup all rendered and behaved correctly throughout the play
// session above, with no regression from this change).
//
// Also checked carefully and confirmed correct (not just skipped): every
// data table (`lftAsciiPattern`/`lftCharPattern`/`lftFrequencies`/all 8
// melody tables/all 7 stages' floor-bits/man-position/monster/item/lift
// tables) byte-diffed programmatically against the real upstream source -
// all matched exactly, no transcription errors found. Every
// `lftComposeRawByte()` sub-column derivation (the `sub==0..3` cases)
// re-derived from `VVramToVram()`'s/`SendUL()`'s own real bit math and
// confirmed to match exactly - no hardware-orientation transform is
// present or needed, consistent with Cracky's own already-settled
// finding. Every `Actor.cpp`/`Movable.cpp`/`Monster.cpp`/`Man.cpp`/
// `Lift.cpp`/`Item.cpp`/`Point.cpp`/`Stage.cpp` function traced line by
// line against its ported counterpart - all matched exactly except the
// five bugs above. Every state transition's own reset steps (score/
// lives/stage/timers/sprites/cell-map/items) confirmed to happen in the
// same place upstream's own shared `NEWGAME:`/`title:`/`stage:`/`try_:`/
// `lose:` label structure does, including the item-persists-across-a-
// retry-but-not-across-a-new-stage distinction (`InitItems()` only ever
// called from `InitStage()`, `DrawItems()` from `InitTrying()` on every
// retry) - confirmed the port's own `lftInitStage()`/`lftInitTrying()`
// split preserves this exactly. No non-zero upstream global/member
// initializers were found needing an explicit port-side reset (grepped
// every real declaration in the upstream source; none exist beyond
// ordinary local-scope variables, all already correctly re-initialized
// per call).
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into lftCharPattern (map tiles) / lftAsciiPattern
//   (status text)
// -----------------------------------------------------------------------------

#define LFT_CHAR_SPACE 0x00
#define LFT_CHAR_FLOOR 0x10
#define LFT_CHAR_LIFTBOTTOM 0x11
#define LFT_CHAR_ROOF_CENTER 0x12
#define LFT_CHAR_ROOF_LEFT 0x13
#define LFT_CHAR_ROOF_RIGHT 0x14
#define LFT_CHAR_MAN 0x15
#define LFT_CHAR_MAN_LEFT 0x15
#define LFT_CHAR_MAN_LEFT_STOP 0x21
#define LFT_CHAR_MAN_RIGHT 0x25
#define LFT_CHAR_MAN_RIGHT_STOP 0x31
#define LFT_CHAR_MAN_LOOSE 0x35
#define LFT_CHAR_MONSTER_LEFT 0x41
#define LFT_CHAR_MONSTER_RIGHT 0x49
#define LFT_CHAR_LIFT 0x51
#define LFT_CHAR_POINT 0x55
#define LFT_CHAR_ITEM 0x65

// -----------------------------------------------------------------------------
//   ScanKeys.h - kept for structural fidelity (matching Cracky's own
//   precedent), though this port reads isLeftPressed()/etc directly.
// -----------------------------------------------------------------------------

#define LFT_KEYS_LEFT 0x01
#define LFT_KEYS_RIGHT 0x02
#define LFT_KEYS_UP 0x04
#define LFT_KEYS_DOWN 0x08
#define LFT_KEYS_DIR ( LFT_KEYS_LEFT | LFT_KEYS_RIGHT | LFT_KEYS_UP | LFT_KEYS_DOWN )
#define LFT_KEYS_BUTTON0 0x10

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define LFT_COORD_SHIFT 0
#define LFT_COORD_RATE ( 1 << LFT_COORD_SHIFT )
#define LFT_COORD_MASK ( LFT_COORD_RATE - 1 )
#define LFT_MAP_SHIFT ( LFT_COORD_SHIFT + 1 )
#define LFT_MAP_RATE ( LFT_COORD_RATE * 2 )
#define LFT_MAP_MASK ( LFT_MAP_RATE - 1 )
#define LFT_HIT_RANGE LFT_COORD_RATE

// -----------------------------------------------------------------------------
//   Actor.h / Monster.h
// -----------------------------------------------------------------------------

#define LFT_ACTOR_SEQMASK 0x03
#define LFT_ACTOR_LIVE 0x80
#define LFT_WAIT_BIT 0x40

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define LFT_CELL_INDEX 0x07
#define LFT_CELL_TYPE 0x38
#define LFT_CELL_TYPE_FLOOR 0x08
#define LFT_CELL_TYPE_LIFTBIT 0x10
#define LFT_CELL_TYPE_LIFTBOTTOM ( LFT_CELL_TYPE_LIFTBIT | LFT_CELL_TYPE_FLOOR )
#define LFT_CELL_TYPE_ITEM 0x28
#define LFT_CELL_LEFTLIFT 0x40
#define LFT_CELL_RIGHTLIFT 0x80

#define LFT_STAGE_TOP ( 0 + 2 )
#define LFT_FLOOR_COUNT 3
#define LFT_COLUMN_COUNT 12
#define LFT_FLOOR_HEIGHT 4
#define LFT_COLUMN_WIDTH 2
#define LFT_STAGE_BOTTOM ( LFT_STAGE_TOP + LFT_FLOOR_HEIGHT * LFT_FLOOR_HEIGHT )

// -----------------------------------------------------------------------------
//   Stages.h
// -----------------------------------------------------------------------------

#define LFT_STAGE_COUNT 7
#define LFT_MAX_ITEM_COUNT 8
#define LFT_MAX_MONSTER_COUNT 2
#define LFT_MAX_LIFT_COUNT 5

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define LFT_VVRAM_WIDTH 24
#define LFT_VVRAM_HEIGHT ( 16 - 2 )

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define LFT_SPRITE_ITEM 0
#define LFT_SPRITE_MAN 8
#define LFT_SPRITE_MONSTER 9
#define LFT_SPRITE_LIFT 11
#define LFT_SPRITE_POINT 17
#define LFT_SPRITE_COUNT 21
#define LFT_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Item.cpp / Point.cpp / Main.cpp local constants
// -----------------------------------------------------------------------------

#define LFT_INVALID_X 255
#define LFT_INVALID_TYPE 255

#define LFT_POINT_COUNT ( LFT_SPRITE_COUNT - LFT_SPRITE_POINT )
#define LFT_POINT_TIME 6
#define LFT_POINT_INVALID_Y 255

#define LFT_MAX_TIME_DENOM ( 50 / ( 8 / LFT_COORD_RATE ) )
#define LFT_BONUS_RATE 5

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching Cracky's own precedent exactly.
// -----------------------------------------------------------------------------

#define LFT_N8 6
#define LFT_N8L 8
#define LFT_N8R 4
#define LFT_N8P ( LFT_N8 * 3 / 2 )
#define LFT_N4 ( LFT_N8 * 2 )
#define LFT_N4P ( LFT_N4 * 3 / 2 )
#define LFT_N2 ( LFT_N4 * 2 )
#define LFT_N2P ( LFT_N2 * 3 / 2 )
#define LFT_N1 ( LFT_N2 * 2 )
#define LFT_N16 ( LFT_N8 / 2 )

#define LFT_E2 1
#define LFT_F2 2
#define LFT_F2S 3
#define LFT_G2 4
#define LFT_G2S 5
#define LFT_A2 6
#define LFT_A2S 7
#define LFT_B2 8
#define LFT_C3 9
#define LFT_C3S 10
#define LFT_D3 11
#define LFT_D3S 12
#define LFT_E3 13
#define LFT_F3 14
#define LFT_F3S 15
#define LFT_G3 16
#define LFT_G3S 17
#define LFT_A3 18
#define LFT_A3S 19
#define LFT_B3 20
#define LFT_C4 21
#define LFT_C4S 22
#define LFT_D4 23
#define LFT_D4S 24
#define LFT_E4 25
#define LFT_F4 26
#define LFT_F4S 27
#define LFT_G4 28
#define LFT_G4S 29
#define LFT_A4 30
#define LFT_A4S 31
#define LFT_B4 32
#define LFT_C5 33
#define LFT_C5S 34
#define LFT_D5 35
#define LFT_D5S 36
#define LFT_E5 37
#define LFT_F5 38
#define LFT_F5S 39
#define LFT_G5 40

#define LFT_TEMPO 180

// Sound sequencer melody ids, resolved by lftMelodyLength()/lftMelodyValue()
// instead of a real pointer-per-channel (matching Cracky's own established
// "resolve by id" pattern).
#define LFT_MELODY_NONE 0
#define LFT_MELODY_LOOSE 1
#define LFT_MELODY_HIT 2
#define LFT_MELODY_BEEP 3
#define LFT_MELODY_START 4
#define LFT_MELODY_CLEAR 5
#define LFT_MELODY_GAMEOVER 6
#define LFT_MELODY_BGM1 7
#define LFT_MELODY_BGM2 8

// -----------------------------------------------------------------------------
//   Sound state
// -----------------------------------------------------------------------------

#define LFT_STATE_TITLE 0
#define LFT_STATE_START_JINGLE 1
#define LFT_STATE_PLAYING 2
#define LFT_STATE_LOSE_ANIM 3
#define LFT_STATE_GAMEOVER_JINGLE 4
#define LFT_STATE_CLEAR_WAIT 5
#define LFT_STATE_CLEAR_JINGLE 6
#define LFT_STATE_BONUS_TALLY 7

#define LFT_TICK_DIVISOR 4

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream source (not hand-copied), same discipline as every other port
//   in this project.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph - the exact
// same 27-glyph font subset Cracky's own crkAsciiPattern already uses,
// plus one glyph this game's own title needs that Cracky's never did.
//
// **A 28th (0-indexed 27) 'L' glyph appended at the END** (matching the
// exact precedent already established for the sibling Hopman port's own
// missing 'H' - see that file's own header comment) - the real reduced
// upstream font genuinely has no 'L' at all (upstream never spells "LIFT"
// through this font itself; its own real title uses a separate hand-drawn
// VVram bitmap logo instead, see this file's own top-of-file comment).
// Restoring "LIFT" as plain status-grid text (once `lftStatusChar` was
// widened wide enough to actually fit it without colliding with anything
// - see that same top-of-file comment) needed this glyph for real, not
// just cosmetically - printing it through the unmodified 27-glyph table
// silently rendered as " IFT" (a blank cell where 'L' should be),
// confirmed via a live screenshot before this fix. Built the same way as
// Hopman's own 'H': a full left vertical stroke (column 0 = 0x1f, all 5
// rows) plus a bottom-row horizontal stroke extending two columns to the
// right (columns 1-2 = 0x10, bit4/row4 only - the same "row bit set in
// every column the stroke spans" convention this font's own 'E'/'F'/'T'
// glyphs already establish for their own horizontal strokes), appended at
// the end of both this array and lftAsciiIndex()'s own search table
// (never inserted alphabetically) so no existing character's own index
// shifts.
int lftAsciiPattern[112] = {
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
    0x1f, 0x10, 0x10, 0x00,  // L (added - see comment above)
};

// TitleBytes - upstream's own real "LIFT" title-screen logo bitmap
// (Status.cpp's `Title()`), 4 letters x 4x4 VVram-cell glyph indices each
// (64 values total), byte-diff-verified against the real upstream source
// (a Python re.search extraction of the literal `TitleBytes[]` array
// confirmed 64 values matching this table exactly, value for value).
// Every value here is a valid index into lftCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table, independently
// confirmed byte-identical to upstream's own Chars.cpp "logo" section) -
// the exact same shared block-pattern palette every other map tile in
// this game already draws through, just reused here to build a big
// pixel-art wordmark instead of a wall/floor tile. See lftBeginTitle()'s
// own comment for why this replaces the earlier plain-text "LIFT"
// substitute (the same fix already applied to the sibling Cracky port,
// once a real user-supplied hardware photo proved the "purely
// decorative, simplify to text" reasoning wrong for this whole family -
// it's the game's actual title wordmark, not a throwaway detail).
int lftTitleBytes[64] = {
    0x0c, 0x03, 0x00, 0x00, 0x0c, 0x03, 0x00, 0x00,
    0x0c, 0x03, 0x00, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x04, 0x0d, 0x07, 0x01, 0x00, 0x0c, 0x03, 0x00,
    0x00, 0x0c, 0x03, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x0c, 0x07, 0x05, 0x01, 0x0c, 0x0b, 0x0a, 0x02,
    0x0c, 0x03, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00,
    0x04, 0x0d, 0x07, 0x01, 0x00, 0x0c, 0x03, 0x00,
    0x00, 0x0c, 0x03, 0x00, 0x00, 0x04, 0x01, 0x00,
};

// CharPattern - 117 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int lftCharPattern[234] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x33, 0x33, 0xa5, 0xa5, 0xff, 0xff, 0xec, 0xff,
    0xff, 0xce, 0x00, 0xf5, 0xfd, 0x00, 0x90, 0x34,
    0x43, 0x05, 0x00, 0x75, 0x7d, 0x00, 0x00, 0xd0,
    0xc1, 0x00, 0x00, 0xf5, 0x7d, 0x00, 0x10, 0x2d,
    0x43, 0x05, 0x80, 0xf5, 0x7d, 0x08, 0x10, 0x3c,
    0xc3, 0x01, 0x00, 0xdf, 0x5f, 0x00, 0x50, 0x34,
    0x43, 0x09, 0x00, 0xd7, 0x57, 0x00, 0x00, 0x1c,
    0x0d, 0x00, 0x00, 0xd7, 0x5f, 0x00, 0x50, 0x34,
    0xd2, 0x01, 0x80, 0xd7, 0x5f, 0x08, 0x10, 0x3c,
    0xc3, 0x01, 0x4c, 0xac, 0x8a, 0x44, 0x13, 0x53,
    0x15, 0x22, 0x80, 0xc3, 0x3c, 0x08, 0x10, 0xbe,
    0xaf, 0x01, 0x44, 0xa8, 0xca, 0xc8, 0x22, 0x51,
    0x35, 0x32, 0xa8, 0xaf, 0xef, 0x08, 0x10, 0x73,
    0xbf, 0x00, 0x40, 0x4e, 0xce, 0x00, 0x32, 0xf7,
    0xff, 0x02, 0x80, 0xfe, 0xfa, 0x8a, 0x00, 0xfb,
    0x37, 0x01, 0x00, 0xec, 0xe4, 0x04, 0x20, 0xff,
    0x7f, 0x23, 0x97, 0x11, 0x11, 0x79, 0x00, 0x11,
    0x11, 0x00, 0xe4, 0xc0, 0xc2, 0x00, 0x32, 0x02,
    0x61, 0x69, 0x24, 0xcc, 0xc2, 0x00, 0x32, 0x02,
    0x61, 0x69, 0x8c, 0xce, 0xc2, 0x00, 0x00, 0x03,
    0x61, 0x69, 0xa4, 0xc4, 0xc2, 0x00, 0x21, 0x01,
    0x61, 0x69, 0x00, 0xaf, 0xa5, 0x05, 0x80, 0x8f,
    0x00, 0x00, 0x00, 0xff, 0x37, 0x01, 0x80, 0x8f,
    0x00, 0x00, 0x00, 0x1f, 0xa1, 0x04, 0x80, 0x9f,
    0x01, 0x00, 0x00, 0xff, 0xce, 0x08, 0x80, 0x8f,
    0x00, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40)
// - the exact same table Cracky's own crkFrequencies already uses.
int lftFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int lftMelodyLoose[3] = { 1, LFT_A3, 0 };

int lftMelodyHit[17] = {
    1, LFT_F4, 1, LFT_G4, 1, LFT_A4, 1, LFT_B4, 1, LFT_C5,
    1, LFT_D5, 1, LFT_E5, 1, LFT_F5, 0,
};

int lftMelodyBeep[3] = { 1, LFT_A4, 0 };

int lftMelodyStart[23] = {
    LFT_N4, LFT_C4, LFT_N4, LFT_E4, LFT_N8, LFT_G4, LFT_N4, LFT_E4, LFT_N4, LFT_F4,
    LFT_N8, LFT_F4, LFT_N4, LFT_A4, LFT_N8, LFT_C5, LFT_N4P, LFT_A4, LFT_N2P, LFT_C5,
    LFT_N4, 0, 0,
};

int lftMelodyClear[29] = {
    LFT_N8, LFT_A4, LFT_N8, LFT_A4, LFT_N8, LFT_G4, LFT_N8, LFT_F4, LFT_N8, LFT_G4,
    LFT_N4, LFT_A4, LFT_N4, LFT_B4, LFT_N8, LFT_B4, LFT_N8, LFT_A4, LFT_N8, LFT_G4,
    LFT_N8, LFT_A4, LFT_N4, LFT_B4, LFT_N8 + LFT_N2, LFT_C5, LFT_N2, 0, 0,
};

int lftMelodyGameOver[21] = {
    LFT_N8, LFT_C5, LFT_N8, LFT_F4, LFT_N8, LFT_A4, LFT_N8, LFT_E4, LFT_N8, LFT_G4,
    LFT_N8, LFT_A4, LFT_N8, LFT_B4, LFT_N8, LFT_C5, LFT_N2P, LFT_C5, LFT_N4, 0,
    0,
};

int lftMelodyBgm1[119] = {
    LFT_N4, LFT_C4, LFT_N4, LFT_G4, LFT_N8, LFT_C4, LFT_N4, LFT_G4, LFT_N4, LFT_A4,
    LFT_N8, LFT_A4, LFT_N8, LFT_G4, LFT_N8, LFT_G4, LFT_N8, LFT_F4, LFT_N8, LFT_F4,
    LFT_N8, LFT_E4, LFT_N8, LFT_E4, LFT_N4, LFT_D4, LFT_N4, LFT_D4, LFT_N8, LFT_D4,
    LFT_N4, LFT_E4, LFT_N4P, LFT_D4, LFT_N2P, 0, LFT_N4, LFT_C4, LFT_N4, LFT_G4,
    LFT_N8, LFT_C4, LFT_N4, LFT_G4, LFT_N4, LFT_A4, LFT_N8, LFT_A4, LFT_N8, LFT_G4,
    LFT_N8, LFT_G4, LFT_N8, LFT_F4, LFT_N8, LFT_F4, LFT_N8, LFT_E4, LFT_N8, LFT_E4,
    LFT_N4, LFT_F4, LFT_N4, LFT_F4, LFT_N8, LFT_F4, LFT_N4, LFT_A4, LFT_N4P, LFT_G4,
    LFT_N2P, 0, LFT_N8, LFT_E4, LFT_N8, LFT_E4, LFT_N8, LFT_E4, LFT_N4, LFT_E4,
    LFT_N8, LFT_E4, LFT_N4, LFT_A4, LFT_N8, LFT_D4, LFT_N8, LFT_D4, LFT_N8, LFT_D4,
    LFT_N4, LFT_D4, LFT_N8, LFT_D4, LFT_N4, LFT_G4, LFT_N8, 0, LFT_N8, LFT_A4,
    LFT_N8, 0, LFT_N8, LFT_G4, LFT_N8, 0, LFT_N8, LFT_F4, LFT_N8, 0, LFT_N8, LFT_E4,
    LFT_N4, LFT_D4, LFT_N4, LFT_E4, LFT_N2, LFT_C4, 255,
};

int lftMelodyBgm2[133] = {
    LFT_N8, LFT_C4, LFT_N4, 0, LFT_N4P, LFT_E4, LFT_N8, LFT_G4, LFT_N8, 0,
    LFT_N8, LFT_A3, LFT_N4, 0, LFT_N4P, LFT_C4, LFT_N8, LFT_E4, LFT_N8, 0,
    LFT_N8, LFT_D4, LFT_N4, 0, LFT_N4P, LFT_F4, LFT_N8, LFT_A3, LFT_N8, 0,
    LFT_N8, LFT_G3, LFT_N4, 0, LFT_N4P, LFT_B3, LFT_N8, LFT_D4, LFT_N8, 0,
    LFT_N8, LFT_C4, LFT_N4, 0, LFT_N4P, LFT_E4, LFT_N8, LFT_G4, LFT_N8, 0,
    LFT_N8, LFT_A3, LFT_N4, 0, LFT_N4P, LFT_C4, LFT_N8, LFT_E4, LFT_N8, 0,
    LFT_N8, LFT_F4, LFT_N4, 0, LFT_N8, LFT_F4, LFT_N8, LFT_G3, LFT_N4, 0,
    LFT_N8, LFT_G3, LFT_N8, LFT_C4, LFT_N4, 0, LFT_N4P, LFT_E4, LFT_N8, LFT_G4,
    LFT_N8, 0, LFT_N8, LFT_C4, LFT_N4, 0, LFT_N8, LFT_C4, LFT_N8, LFT_A3,
    LFT_N4, 0, LFT_N8, LFT_A3, LFT_N8, LFT_D4, LFT_N4, 0, LFT_N8, LFT_D4,
    LFT_N8, LFT_G3, LFT_N4, 0, LFT_N8, LFT_G3, LFT_N8, 0, LFT_N8, LFT_F3,
    LFT_N8, 0, LFT_N8, LFT_F3, LFT_N8, 0, LFT_N8, LFT_G3, LFT_N8, 0, LFT_N8, LFT_G3,
    LFT_N8, LFT_C4, LFT_N4, 0, LFT_N4P, LFT_E4, LFT_N8, LFT_G4, LFT_N8, 0,
    255,
};

// Stage data - flattened from upstream's own `struct Stage {floorBits[4],
// manPosition, monsterCount, pMonsters, itemCount, pItems, liftCount,
// pLifts}` array + separate per-stage Monsters/Items/Lifts arrays into
// parallel fixed arrays (avoids porting a struct with real array-pointer
// members, matching Cracky's own equivalent flattening).
int lftStageFloorBits[7][3] = {
    { 0xfa, 0xe7, 0x7f },
    { 0x7e, 0xe7, 0x7b },
    { 0xfa, 0xe7, 0x6e },
    { 0xbe, 0xe6, 0x6a },
    { 0xfe, 0xe5, 0x5e },
    { 0xee, 0xe6, 0x6e },
    { 0x12, 0xa4, 0x1c },
};

int lftStageManPosition[7] = {
    10 | ( 2 << 4 ), 10 | ( 2 << 4 ), 10 | ( 2 << 4 ), 10 | ( 0 << 4 ), 10 | ( 0 << 4 ), 6 | ( 0 << 4 ), 10 | ( 2 << 4 ),
};

int lftStageMonsterCount[7] = { 1, 1, 1, 1, 1, 2, 1 };
int lftStageItemCount[7] = { 8, 8, 8, 8, 8, 6, 8 };
int lftStageLiftCount[7] = { 2, 2, 4, 5, 4, 4, 3 };

// pos|(floor<<4) per monster, up to LFT_MAX_MONSTER_COUNT(2) per stage.
int lftStageMonsters[7][2] = {
    { 2 | ( 2 << 4 ), 0 },
    { 1 | ( 0 << 4 ), 0 },
    { 1 | ( 1 << 4 ), 0 },
    { 1 | ( 2 << 4 ), 0 },
    { 1 | ( 0 << 4 ), 0 },
    { 3 | ( 0 << 4 ), 9 | ( 2 << 4 ) },
    { 1 | ( 2 << 4 ), 0 },
};

// (pos|(floor<<4), type) pairs, up to LFT_MAX_ITEM_COUNT(8) items/stage.
int lftStageItems[7][16] = {
    { 4 | ( 0 << 4 ), 0, 9 | ( 0 << 4 ), 0, 1 | ( 1 << 4 ), 3, 3 | ( 1 << 4 ), 1, 8 | ( 1 << 4 ), 1, 1 | ( 2 << 4 ), 3, 4 | ( 2 << 4 ), 2, 7 | ( 2 << 4 ), 2 },
    { 6 | ( 0 << 4 ), 3, 9 | ( 0 << 4 ), 0, 1 | ( 1 << 4 ), 2, 4 | ( 1 << 4 ), 3, 7 | ( 1 << 4 ), 0, 3 | ( 2 << 4 ), 2, 6 | ( 2 << 4 ), 1, 9 | ( 2 << 4 ), 1 },
    { 3 | ( 0 << 4 ), 0, 9 | ( 0 << 4 ), 0, 3 | ( 1 << 4 ), 1, 5 | ( 1 << 4 ), 2, 7 | ( 1 << 4 ), 1, 3 | ( 2 << 4 ), 3, 6 | ( 2 << 4 ), 3, 9 | ( 2 << 4 ), 2 },
    { 2 | ( 0 << 4 ), 0, 9 | ( 0 << 4 ), 2, 3 | ( 1 << 4 ), 3, 7 | ( 1 << 4 ), 1, 10 | ( 1 << 4 ), 3, 2 | ( 2 << 4 ), 0, 6 | ( 2 << 4 ), 1, 10 | ( 2 << 4 ), 2 },
    { 2 | ( 0 << 4 ), 0, 7 | ( 0 << 4 ), 1, 3 | ( 1 << 4 ), 3, 6 | ( 1 << 4 ), 1, 10 | ( 1 << 4 ), 3, 2 | ( 2 << 4 ), 2, 7 | ( 2 << 4 ), 0, 10 | ( 2 << 4 ), 2 },
    { 2 | ( 0 << 4 ), 0, 2 | ( 1 << 4 ), 0, 6 | ( 1 << 4 ), 1, 9 | ( 1 << 4 ), 2, 5 | ( 2 << 4 ), 1, 10 | ( 2 << 4 ), 2, 0, 0, 0, 0 },
    { 1 | ( 0 << 4 ), 2, 10 | ( 0 << 4 ), 1, 1 | ( 1 << 4 ), 2, 3 | ( 1 << 4 ), 0, 6 | ( 1 << 4 ), 0, 8 | ( 1 << 4 ), 1, 2 | ( 2 << 4 ), 3, 4 | ( 2 << 4 ), 3 },
};

// (pos|(floor<<4), (floor<<4)|offset) pairs, up to LFT_MAX_LIFT_COUNT(5)
// lifts/stage - the second value of each pair really can have a different
// floor nibble than the first (a deliberate upstream animation-phase
// offset, not a transcription artifact - see lftInitLifts()'s own comment).
int lftStageLifts[7][10] = {
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 2, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 0, 0, 0, 0, 0, 0 },
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 2, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 0, 0, 0, 0, 0, 0 },
    { 0 | ( 0 << 4 ), ( 1 << 4 ) | 1, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 4 | ( 1 << 4 ), ( 2 << 4 ) | 2, 8 | ( 1 << 4 ), ( 2 << 4 ) | 2, 0, 0 },
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 2, 6 | ( 0 << 4 ), ( 1 << 4 ) | 1, 8 | ( 0 << 4 ), ( 2 << 4 ) | 1, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 4 | ( 1 << 4 ), ( 2 << 4 ) | 2 },
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 2, 9 | ( 0 << 4 ), ( 1 << 4 ) | 1, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 4 | ( 1 << 4 ), ( 2 << 4 ) | 2, 0, 0 },
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 2, 4 | ( 0 << 4 ), ( 2 << 4 ) | 1, 8 | ( 0 << 4 ), ( 2 << 4 ) | 2, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 0, 0 },
    { 0 | ( 0 << 4 ), ( 2 << 4 ) | 1, 5 | ( 0 << 4 ), ( 2 << 4 ) | 2, 11 | ( 0 << 4 ), ( 2 << 4 ) | 1, 0, 0, 0, 0 },
};

// -----------------------------------------------------------------------------
//   Struct definitions
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y, code;
} LftSprite;

// Man/Monster - Movable+Actor flattened into one struct, see header comment.
typedef struct
{
    int x, y, sprite, clock;
    int status, c, dx;
} LftActor;

// Lift - Movable+Lift flattened, with Lift's own redundant "x" field
// merged into the shared x (always numerically identical given
// LFT_COORD_SHIFT==0 - see header comment).
typedef struct
{
    int x, y, sprite, clock;
    int bottom, top;
} LftLift;

typedef struct
{
    int x, y, sprite, type;
} LftItem;

typedef struct
{
    int x, y, sprite, clock;
} LftPoint;

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int lftCellMap[LFT_COLUMN_COUNT * LFT_FLOOR_COUNT];

int lftScore;
int lftHiScore;
int lftRemainCount;
int lftCurrentStage;
int lftStageTime;
int lftClock;
int lftMonsterNum;
int lftTimeDenom;
int lftStageIndex;
int lftTimeRate;

int lftVVram[LFT_VVRAM_HEIGHT][LFT_VVRAM_WIDTH];
LftSprite lftSprites[LFT_SPRITE_COUNT];

LftActor lftMan;

int lftMonsterCount;
LftActor lftMonsters[LFT_MAX_MONSTER_COUNT];

int lftLiftCount;
LftLift lftLifts[LFT_MAX_LIFT_COUNT];

LftItem lftItems[LFT_MAX_ITEM_COUNT];
int lftItemCount;
int lftLastType;
int lftItemRate;

LftPoint lftPoints[LFT_POINT_COUNT];
int lftPointValues[4] = { 10, 20, 40, 80 };

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize=0x100 selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into lftAsciiPattern (0 = space) per cell.
//
// **Widened from an original, wrong `[8][8]` - see this file's own
// top-of-file comment, "A whole-file architectural fix", ported directly
// from Cracky's own identical, already-solved case (itself found via a
// real user-supplied hardware photo).** The original design only ever
// modeled upstream's own status-label columns (SCORE/STAGE/TIME/REMAIN,
// which upstream's `LeftX=24` constant genuinely does confine to columns
// 24-31, 8 cells) - but the title screen's own text (the logo, "MINI",
// "START"/"CONTINUE", the "INUFUTO 2026" credit) lives at upstream's real
// columns 8-23, well to the LEFT of the status zone, using the exact same
// shared PrintC()/PrintS() mechanism at different column arguments - not
// a separate, narrower grid at all. See `lftComposeRawByte()`'s own
// header for how this wider grid actually reaches the screen.
int lftStatusChar[8][32];

// Set true only while on the title screen (LFT_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its
// initial ClearScreen(), and instead drives the ENTIRE screen (not just
// the status zone) through the same PrintC()/PrintS() text mechanism, at
// real columns spanning the whole 0-31 char-cell range. When true,
// lftComposeRawByte() reads lftStatusChar across the full width instead
// of just columns 24-31, letting the title screen use that same wide
// real estate instead of being artificially confined to the narrow
// status-only zone. Matches Cracky's own crkFullWidthText exactly.
bool lftFullWidthText;

// message overlay burned directly over the map area, matching Cracky's
// own PrintTimeUp()/PrintGameOver() Vram-direct writes.
bool lftOverlayActive;
int lftOverlayText[10];
int lftOverlayLen;
int lftOverlayPage;
int lftOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of LFT_TICK_DIVISOR.
int lftSeqMelody[3];
int lftSeqPos[3];
int lftSeqWait[3];
int lftSeqActive[3];

int lftTickCounter;

int lftState;
int lftWaitFrames;
int lftAnimStep;
int lftSelection;
bool lftSelectionChanged;
int lftPrevLeft;
int lftPrevRight;
int lftPrevUp;
int lftPrevDown;
int lftPrevFire;
bool lftPendingContinue;


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status text written into lftStatusChar (a
//   pattern-index grid covering the real full 32-char-cell x 8-page Vram
//   address space - see lftStatusChar's own header comment). Same 27-glyph
//   alphabet as Cracky's own crkPrintC/crkPrintS, plus one hand-built 'L'
//   glyph this game's own title needs - see lftAsciiPattern's own header.
// -----------------------------------------------------------------------------

int lftAsciiIndex( int c )
{
    // 'L' appended at the end (index 27) rather than inserted
    // alphabetically - see lftAsciiPattern's own header comment for why
    // (this game's own name, "LIFT", needs it and upstream's real reduced
    // font never had one to begin with) - matching the identical
    // precedent already established for the sibling Hopman port's own
    // missing 'H'.
    int table[28] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V',
        'L',
    };
    int i;
    for( i = 0; i < 28; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

int lftPrintC( int page, int col, int c )
{
    lftStatusChar[ page ][ col ] = lftAsciiIndex( c );
    return col + 1;
}

int lftPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = lftPrintC( page, col, s[ i ] );
    return col;
}

int lftPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
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
    return lftPrintC( page, col, c );
}

void lftPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      lftPrintC( page, col, ' ' );
    else
      lftPrintC( page, col, d1 + '0' );
    lftPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void lftPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        lftPrintC( page, col, ' ' );
        if( d2 == 0 )
          lftPrintC( page, col + 1, ' ' );
        else
          lftPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        lftPrintC( page, col, d1 + '0' );
        lftPrintC( page, col + 1, d2 + '0' );
    }
    lftPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void lftPrintNumber5( int page, int col, int w )
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
          lftPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            lftPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    lftPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// lftStatusChar's own header comment for why this changed from the
// original, too-narrow model. Matches Cracky's own already-fixed
// crkPrintScore()/crkPrintTime() exactly, since both games share the
// identical LeftX=24-based Status.cpp layout.
void lftPrintScore()
{
    // Upstream: PrintNumber5(Vram + VramRowSize*1 + (LeftX+2)*VramStep, Score)
    // - digits at real columns 26-30 (LeftX+2..LeftX+6), the trailing
    // static '0' at real column 31 (LeftX+7).
    lftPrintNumber5( 1, 26, lftScore );
    lftPrintC( 1, 31, '0' );
}

void lftPrintTime()
{
    // Upstream: PrintByteNumber3(Vram + VramRowSize*5 + (LeftX+5)*VramStep,
    // StageTime) - real column 29 (LeftX+5).
    lftPrintByteNumber3( 5, 29, lftStageTime );
}

void lftPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    lftPrintS( 0, 24, sScore, 5 );
    lftPrintS( 3, 24, sStage, 5 );
    lftPrintByteNumber2( 3, 30, lftCurrentStage + 1 );
    lftPrintS( 5, 24, sTime, 4 );

    if( lftRemainCount > 1 )
    {
        i = lftRemainCount - 1;
        if( i > 2 )
        {
            // upstream draws a real 2x2 Char_Remain (Man) icon here via
            // Put2C, then a space, then the remaining digit -
            // simplified to plain digits, matching Cracky's own
            // established precedent for its own equivalent lives
            // display (see gameCracky.c's own crkPrintStatus()).
            lftPrintC( 7, 24, ' ' );
            lftPrintC( 7, 25, ' ' );
            lftPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < lftRemainCount - 1; i = i + 1 )
              lftPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    lftPrintScore();
    lftPrintTime();
}

void lftBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    lftOverlayActive = true;
    lftOverlayLen = len;
    lftOverlayPage = page;
    lftOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      lftOverlayText[ i ] = s[ i ];
}

void lftPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    lftBeginOverlay( s, 9, 4, 8 );
}

void lftPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    lftBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Main.cpp - AddScore()
// -----------------------------------------------------------------------------

void lftAddScore( int pts )
{
    lftScore = lftScore + pts;
    if( lftScore > lftHiScore )
      lftHiScore = lftScore;
    lftPrintScore();
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int lftMelodyLength( int id )
{
    if( id == LFT_MELODY_LOOSE ) return 3;
    if( id == LFT_MELODY_HIT ) return 17;
    if( id == LFT_MELODY_BEEP ) return 3;
    if( id == LFT_MELODY_START ) return 23;
    if( id == LFT_MELODY_CLEAR ) return 29;
    if( id == LFT_MELODY_GAMEOVER ) return 21;
    if( id == LFT_MELODY_BGM1 ) return 119;
    if( id == LFT_MELODY_BGM2 ) return 133;
    return 0;
}

int lftMelodyValue( int id, int idx )
{
    if( id == LFT_MELODY_LOOSE ) return lftMelodyLoose[ idx ];
    if( id == LFT_MELODY_HIT ) return lftMelodyHit[ idx ];
    if( id == LFT_MELODY_BEEP ) return lftMelodyBeep[ idx ];
    if( id == LFT_MELODY_START ) return lftMelodyStart[ idx ];
    if( id == LFT_MELODY_CLEAR ) return lftMelodyClear[ idx ];
    if( id == LFT_MELODY_GAMEOVER ) return lftMelodyGameOver[ idx ];
    if( id == LFT_MELODY_BGM1 ) return lftMelodyBgm1[ idx ];
    if( id == LFT_MELODY_BGM2 ) return lftMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/LFT_TEMPO = 5/3 real 60Hz ticks - see header comment.
int lftNoteFrames( int length )
{
    return (int)( (float)length * 5.0 / 3.0 + 0.5 );
}

void lftStartSeq( int channel, int melodyId )
{
    lftSeqMelody[ channel ] = melodyId;
    lftSeqPos[ channel ] = 0;
    lftSeqWait[ channel ] = 0;
    lftSeqActive[ channel ] = 1;
}

void lftStopSeq( int channel )
{
    lftSeqActive[ channel ] = 0;
    lftSeqMelody[ channel ] = LFT_MELODY_NONE;
}

bool lftSeqPlaying( int channel )
{
    return lftSeqActive[ channel ] != 0;
}

void lftAdvanceOneSeq( int channel )
{
    int length, note;

    if( lftSeqActive[ channel ] == 0 ) return;

    if( lftSeqWait[ channel ] > 0 )
    {
        lftSeqWait[ channel ] = lftSeqWait[ channel ] - 1;
        return;
    }

    length = lftMelodyValue( lftSeqMelody[ channel ], lftSeqPos[ channel ] );
    if( length == 0 )
    {
        lftStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        lftSeqPos[ channel ] = 0;
        length = lftMelodyValue( lftSeqMelody[ channel ], 0 );
    }
    note = lftMelodyValue( lftSeqMelody[ channel ], lftSeqPos[ channel ] + 1 );
    lftSeqPos[ channel ] = lftSeqPos[ channel ] + 2;
    lftSeqWait[ channel ] = lftNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)lftFrequencies[ note - 1 ], (float)lftSeqWait[ channel ] / 60.0 );
}

void lftAdvanceSound()
{
    lftAdvanceOneSeq( 0 );
    lftAdvanceOneSeq( 1 );
    lftAdvanceOneSeq( 2 );
}

void lftStartBgm()
{
    lftStartSeq( 1, LFT_MELODY_BGM1 );
    lftStartSeq( 2, LFT_MELODY_BGM2 );
}

void lftStopBgm()
{
    lftStopSeq( 1 );
    lftStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void lftHideAllSprites()
{
    int i;
    for( i = 0; i < LFT_SPRITE_COUNT; i = i + 1 )
      lftSprites[ i ].code = LFT_INVALID_CODE;
}

void lftShowSpriteXY( int sprite, int x, int y, int code )
{
    lftSprites[ sprite ].x = x;
    lftSprites[ sprite ].y = y;
    lftSprites[ sprite ].code = code;
}

void lftHideSprite( int index )
{
    lftSprites[ index ].code = LFT_INVALID_CODE;
}


// -----------------------------------------------------------------------------
//   Stage.cpp helpers
// -----------------------------------------------------------------------------

int lftCellMapPtr( int x, int y )
{
    // A real, reachable crash risk, caught by inspection: most callers
    // guard `y >= LFT_STAGE_TOP` before calling this (matching upstream's
    // own equivalent guards), but lftDecideDirection()'s own two
    // unconditional CellMapPtr reads (matching upstream's identically-
    // unguarded DecideDirection() in Monster.cpp) and lftHitItems() don't
    // - and a Man/Monster riding a lift right at its topmost point
    // (`pLift->y == pLift->top`, a genuinely reachable position - see
    // e.g. stage 0's own first lift, whose `top` computes to 3, one of
    // several lifts across the 7 real stages whose top is low enough for
    // this) lands at `y = pLift->y - LFT_COORD_RATE*2`, one row ABOVE
    // LFT_STAGE_TOP. On real AVR, `(y - StageTop)` is a `uint8_t`
    // subtraction that wraps to a large-but-bounded value (255), still a
    // harmless PROGMEM overrun; on Vircon32, the same subtraction is a
    // genuine negative `int`, and `>>` here is a documented *logical*
    // (zero-fill) shift - the same "negative operand right-shifted"
    // hazard already found and fixed in HollowSeeker/Tiny Pipe/Nohzdyve/
    // TinY Fi/Gilbert in the Downland - turning a small negative offset
    // into an astronomically large positive one and reading `lftCellMap`
    // wildly out of bounds (a real "ERROR: INVALID MEMORY READ" risk).
    // Fixed the same way as every prior instance: never shift a negative
    // row value, and clamp the final index into the array's own real
    // bounds as a second, centralized safety net (this is the one shared
    // choke point every caller - guarded or not - already goes through).
    int row, idx;
    row = y - LFT_STAGE_TOP;
    if( row < 0 )
      row = 0;
    idx = ( row >> 2 ) * LFT_COLUMN_COUNT + ( x >> 1 );
    if( idx < 0 )
      idx = 0;
    if( idx >= LFT_COLUMN_COUNT * LFT_FLOOR_COUNT )
      idx = LFT_COLUMN_COUNT * LFT_FLOOR_COUNT - 1;
    return idx;
}

int lftToX( int b )
{
    return ( b & 0x0f ) << 1;
}

int lftToY( int b )
{
    return ( ( b & 0xf0 ) >> 2 ) + LFT_STAGE_TOP;
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void lftInitPoints()
{
    int i, sprite;
    sprite = LFT_SPRITE_POINT;
    for( i = 0; i < LFT_POINT_COUNT; i = i + 1 )
    {
        lftPoints[ i ].y = LFT_POINT_INVALID_Y;
        lftPoints[ i ].sprite = sprite;
        lftHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void lftStartPoint( int x, int y, int rate )
{
    int i;
    int clampedRate;
    // defensive clamp against the unbounded upstream ++Rate counter - see
    // header comment ("A second, much smaller latent-overrun risk").
    clampedRate = rate;
    if( clampedRate > 3 )
      clampedRate = 3;
    lftAddScore( lftPointValues[ clampedRate ] );
    for( i = 0; i < LFT_POINT_COUNT; i = i + 1 )
    {
        if( lftPoints[ i ].y != LFT_POINT_INVALID_Y ) continue;
        lftPoints[ i ].x = x;
        lftPoints[ i ].y = y;
        lftPoints[ i ].clock = LFT_POINT_TIME << LFT_COORD_SHIFT;
        lftShowSpriteXY( lftPoints[ i ].sprite, x, y, LFT_CHAR_POINT + ( clampedRate << 2 ) );
        return;
    }
}

void lftUpdatePoints()
{
    int i;
    for( i = 0; i < LFT_POINT_COUNT; i = i + 1 )
    {
        if( lftPoints[ i ].y == LFT_POINT_INVALID_Y ) continue;
        if( lftPoints[ i ].clock == 0 )
        {
            lftPoints[ i ].y = LFT_POINT_INVALID_Y;
            lftHideSprite( lftPoints[ i ].sprite );
        }
        else
          lftPoints[ i ].clock = lftPoints[ i ].clock - 1;
    }
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

bool lftIsOnGrid( LftActor* pMovable )
{
    return ( pMovable->x & LFT_MAP_MASK ) == 0;
}

bool lftIsNear( LftActor* p1, LftActor* p2 )
{
    return
        p1->x + LFT_HIT_RANGE >= p2->x && p2->x + LFT_HIT_RANGE >= p1->x &&
        p1->y + LFT_HIT_RANGE >= p2->y && p2->y + LFT_HIT_RANGE >= p1->y;
}


// -----------------------------------------------------------------------------
//   Actor.cpp
// -----------------------------------------------------------------------------

bool lftCanMoveTo( LftActor* pActor, int dx )
{
    int x, y;
    x = ( pActor->x >> LFT_COORD_SHIFT ) + dx + dx;
    y = pActor->y >> LFT_COORD_SHIFT;
    // Real bug, found via a direct user report ("is it normal we can walk
    // off left edge from screen"), confirmed against upstream's own
    // CanMoveTo() (Actor.cpp): `x` is a `byte` there, so walking off the
    // left edge (dx=-1 at column 0, computing e.g. 0-1-1=-2) wraps to a
    // large unsigned value (254) that the single `x >= ColumnCount*2`
    // check correctly catches, blocking the move - the same unsigned-byte-
    // wraparound reliance already documented extensively elsewhere in this
    // project. This port's `x` is a plain, non-wrapping signed int, so a
    // negative x here just stays negative and never satisfies that
    // upper-bound-only check, silently allowing the move straight off the
    // left edge instead of blocking it. Fixed with an explicit `x < 0`
    // check alongside the existing upper bound, reproducing upstream's
    // real two-sided boundary behavior directly rather than relying on
    // wraparound.
    if( x < 0 || x >= LFT_COLUMN_COUNT * 2 ) return false;
    if( y >= LFT_STAGE_TOP )
    {
        int cell;
        cell = lftCellMap[ lftCellMapPtr( x, y ) ];
        if( ( cell & LFT_CELL_TYPE_LIFTBIT ) != 0 )
        {
            int liftY;
            liftY = lftLifts[ cell & LFT_CELL_INDEX ].y;
            return
                liftY + LFT_COORD_RATE < pActor->y ||
                pActor->y + LFT_COORD_RATE * 2 < liftY;
        }
        if( ( cell & LFT_CELL_TYPE_FLOOR ) != 0 )
        {
            return
                ( pActor->y & LFT_COORD_MASK ) == 0 &&
                ( ( ( ( pActor->y + LFT_COORD_MASK ) >> LFT_COORD_SHIFT ) - LFT_STAGE_TOP ) & 3 ) < 3;
        }
    }
    return true;
}

bool lftIsOnLift( LftActor* pActor, LftLift* pLift )
{
    int width, leftEnd, bottom;
    width = 2 * LFT_COORD_RATE;
    leftEnd = LFT_COLUMN_COUNT * LFT_COORD_RATE * 2 - LFT_COORD_RATE * 2;
    bottom = pActor->y + LFT_COORD_RATE * 2;
    if( pLift->y >= bottom - LFT_COORD_RATE && bottom >= pLift->y )
    {
        if( pLift->x < LFT_COORD_RATE * 2 )
          return pActor->x < LFT_COORD_RATE * 2;
        if( pLift->x >= leftEnd )
          return pActor->x >= leftEnd;
        return
            pActor->x + width > pLift->x &&
            pActor->x < pLift->x + width;
    }
    return false;
}

bool lftMoveOnLift( LftActor* pActor, LftLift* pLift )
{
    if( lftIsOnLift( pActor, pLift ) )
    {
        pActor->y = pLift->y - LFT_COORD_RATE * 2;
        return true;
    }
    return false;
}

void lftFall( LftActor* pActor )
{
    int x, y, count, remaining, cellIdx, fall;

    y = pActor->y >> LFT_COORD_SHIFT;
    if( y < LFT_STAGE_TOP )
    {
        pActor->y = pActor->y + 1;
        return;
    }
    if( y >= LFT_STAGE_BOTTOM + 1 ) return;

    fall = 1;
    x = pActor->x >> LFT_COORD_SHIFT;
    cellIdx = lftCellMapPtr( x, y );
    if( ( x & 1 ) == 0 )
      count = 1;
    else
      count = 2;
    // Genuinely degenerate given LFT_COORD_MASK==0 - kept as the real
    // expression rather than dropped, matching this project's own
    // preference for preserving upstream formulas verbatim.
    if( ( pActor->x & LFT_COORD_MASK ) != 0 )
      count = count + 1;

    remaining = count;
    while( remaining > 0 )
    {
        int cell;
        cell = lftCellMap[ cellIdx ];
        cellIdx = cellIdx + 1;
        if( ( cell & LFT_CELL_TYPE_LIFTBIT ) != 0 )
        {
            if( lftIsOnLift( pActor, &lftLifts[ cell & LFT_CELL_INDEX ] ) )
            {
                fall = 0;
                break;
            }
        }
        if( ( cell & LFT_CELL_TYPE_FLOOR ) != 0 )
        {
            if( ( ( y - LFT_STAGE_TOP ) & 3 ) == 1 && ( pActor->y & LFT_COORD_MASK ) == 0 )
            {
                fall = 0;
                break;
            }
        }
        remaining = remaining - 1;
    }
    if( fall != 0 )
      pActor->y = pActor->y + 1;
}


// -----------------------------------------------------------------------------
//   Lift.cpp, part A (no dependency on Man/Monster - see header comment on
//   file ordering) - lftMoveLifts() itself lives further down, after both
//   Man.cpp and Monster.cpp's own equivalents are defined.
// -----------------------------------------------------------------------------

void lftLiftShow( LftLift* pLift )
{
    lftShowSpriteXY( pLift->sprite, pLift->x, pLift->y, LFT_CHAR_LIFT );
}

void lftInitLifts()
{
    int i, sprite;
    sprite = LFT_SPRITE_LIFT;
    lftLiftCount = lftStageLiftCount[ lftStageIndex ];
    for( i = 0; i < lftLiftCount; i = i + 1 )
    {
        int b, gx, top, bottom, cellIdx, y;
        b = lftStageLifts[ lftStageIndex ][ i * 2 ];
        gx = lftToX( b );
        top = lftToY( b ) + 1;
        b = lftStageLifts[ lftStageIndex ][ i * 2 + 1 ];
        bottom = lftToY( b ) + 3;
        lftLifts[ i ].x = gx << LFT_COORD_SHIFT;
        // The second stage-data value's own LOW nibble parametrizes the
        // lift's real starting pixel Y (its animation phase) - can genuinely
        // differ from the HIGH nibble used just above for `bottom`, a
        // deliberate upstream design (not a transcription slip) letting
        // different lifts on the same stage start their up/down cycle out
        // of phase with each other - kept exactly as-is.
        lftLifts[ i ].y = ( ( ( b & 0x0f ) << 2 ) + 3 + LFT_STAGE_TOP ) << LFT_COORD_SHIFT;
        lftLifts[ i ].sprite = sprite;
        lftLifts[ i ].clock = 0;
        lftLifts[ i ].bottom = bottom;
        lftLifts[ i ].top = top;
        lftLiftShow( &lftLifts[ i ] );

        y = bottom;
        cellIdx = lftCellMapPtr( gx, y );
        lftCellMap[ cellIdx ] = ( lftCellMap[ cellIdx ] & ~LFT_CELL_TYPE ) | LFT_CELL_TYPE_LIFTBOTTOM | ( i & 7 );
        cellIdx = cellIdx - LFT_COLUMN_COUNT;
        y = y - LFT_FLOOR_HEIGHT;
        while( y >= top )
        {
            lftCellMap[ cellIdx ] = ( lftCellMap[ cellIdx ] & ~LFT_CELL_TYPE ) | LFT_CELL_TYPE_LIFTBIT | ( i & 7 );
            cellIdx = cellIdx - LFT_COLUMN_COUNT;
            y = y - LFT_FLOOR_HEIGHT;
        }

        sprite = sprite + 1;
    }
    for( i = lftLiftCount; i < LFT_MAX_LIFT_COUNT; i = i + 1 )
      lftLifts[ i ].x = LFT_INVALID_X;
}

bool lftIsOnAnyLift( LftActor* pActor )
{
    int i;
    for( i = 0; i < LFT_MAX_LIFT_COUNT; i = i + 1 )
    {
        if( lftLifts[ i ].x == LFT_INVALID_X ) continue;
        if( lftIsOnLift( pActor, &lftLifts[ i ] ) ) return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

void lftInitItems()
{
    int sprite, i;
    sprite = LFT_SPRITE_ITEM;
    for( i = 0; i < lftStageItemCount[ lftStageIndex ]; i = i + 1 )
    {
        int b;
        b = lftStageItems[ lftStageIndex ][ i * 2 ];
        lftItems[ i ].x = lftToX( b );
        lftItems[ i ].y = lftToY( b ) + 1;
        lftItems[ i ].type = lftStageItems[ lftStageIndex ][ i * 2 + 1 ];
        lftItems[ i ].sprite = sprite;
        sprite = sprite + 1;
    }
    lftItemCount = lftStageItemCount[ lftStageIndex ];
    for( i = lftItemCount; i < LFT_MAX_ITEM_COUNT; i = i + 1 )
      lftItems[ i ].x = LFT_INVALID_X;
    lftLastType = LFT_INVALID_TYPE;
    lftItemRate = 0;
}

void lftItemShow( LftItem* pItem )
{
    lftShowSpriteXY( pItem->sprite, pItem->x, pItem->y, ( pItem->type << 2 ) + LFT_CHAR_ITEM );
}

void lftDrawItems()
{
    int index, i;
    index = 0;
    for( i = 0; i < LFT_MAX_ITEM_COUNT; i = i + 1 )
    {
        if( lftItems[ i ].x != LFT_INVALID_X )
        {
            int cellIdx, cell;
            cellIdx = lftCellMapPtr( lftItems[ i ].x, lftItems[ i ].y );
            cell = lftCellMap[ cellIdx ];
            lftCellMap[ cellIdx ] = ( cell & ~LFT_CELL_INDEX ) | index | LFT_CELL_TYPE_ITEM;
            lftItemShow( &lftItems[ i ] );
        }
        index = index + 1;
    }
}

void lftBlinkItems()
{
    int i;
    for( i = 0; i < LFT_MAX_ITEM_COUNT; i = i + 1 )
    {
        if( lftItems[ i ].x != LFT_INVALID_X )
        {
            if( lftItems[ i ].type == lftLastType && ( lftClock & ( 0x04 << LFT_COORD_SHIFT ) ) != 0 )
              lftHideSprite( lftItems[ i ].sprite );
            else
              lftItemShow( &lftItems[ i ] );
        }
    }
}

void lftHitItems()
{
    int x, y, cellIdx, cell, itemIdx;
    x = lftMan.x >> LFT_COORD_SHIFT;
    y = lftMan.y >> LFT_COORD_SHIFT;
    cellIdx = lftCellMapPtr( x, y );
    cell = lftCellMap[ cellIdx ];
    if( ( cell & LFT_CELL_TYPE ) != LFT_CELL_TYPE_ITEM ) return;
    itemIdx = cell & LFT_CELL_INDEX;
    if( lftItems[ itemIdx ].x != x || lftItems[ itemIdx ].y != y ) return;
    if( lftItems[ itemIdx ].type == lftLastType )
    {
        lftStartPoint( x << LFT_COORD_SHIFT, y << LFT_COORD_SHIFT, lftItemRate );
        lftItemRate = lftItemRate + 1;
        lftLastType = LFT_INVALID_TYPE;
    }
    else
    {
        lftAddScore( 5 );
        if( lftLastType != LFT_INVALID_TYPE )
          lftItemRate = 0;
        lftLastType = lftItems[ itemIdx ].type;
    }
    lftCellMap[ cellIdx ] = LFT_CELL_TYPE_FLOOR;
    lftHideSprite( lftItems[ itemIdx ].sprite );
    lftItems[ itemIdx ].x = LFT_INVALID_X;
    lftItemCount = lftItemCount - 1;
    lftStartSeq( 0, LFT_MELODY_HIT );
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void lftManShow()
{
    int c;
    if( lftMan.dx == 0 )
      c = lftMan.c + 3 * 4;
    else
      c = lftMan.c + ( ( lftMan.status & LFT_ACTOR_SEQMASK ) << 2 );
    lftShowSpriteXY( lftMan.sprite, lftMan.x, lftMan.y, c );
}

void lftInitMan()
{
    int b;
    b = lftStageManPosition[ lftStageIndex ];
    lftMan.x = lftToX( b ) << LFT_COORD_SHIFT;
    lftMan.y = ( lftToY( b ) + 1 ) << LFT_COORD_SHIFT;
    lftMan.sprite = LFT_SPRITE_MAN;
    lftMan.status = LFT_ACTOR_LIVE;
    lftMan.dx = 0;
    lftMan.c = LFT_CHAR_MAN_RIGHT;
    lftManShow();
}

void lftMoveMan()
{
    if( lftIsOnGrid( &lftMan ) )
    {
        int dx;
        bool left, right;
        dx = 0;
        left = isLeftPressed();
        right = isRightPressed();
        if( left )
        {
            dx = -1;
            lftMan.c = LFT_CHAR_MAN_LEFT;
        }
        else if( right )
        {
            dx = 1;
            lftMan.c = LFT_CHAR_MAN_RIGHT;
        }
        if( dx != 0 )
        {
            if( !lftCanMoveTo( &lftMan, dx ) )
              dx = 0;
        }
        lftMan.dx = dx;
        lftHitItems();
    }
    if( lftMan.dx != 0 )
    {
        int seq;
        lftMan.x = lftMan.x + lftMan.dx;
        seq = ( ( lftMan.x + LFT_COORD_RATE / 2 ) >> LFT_COORD_SHIFT ) & 3;
        if( seq == 3 )
          seq = 1;
        lftMan.status = ( lftMan.status & ~LFT_ACTOR_SEQMASK ) | seq;
    }
    lftManShow();
    lftMan.clock = lftMan.clock + 1;
}

void lftMoveManOnLift( LftLift* pLift )
{
    if( lftMoveOnLift( &lftMan, pLift ) )
      lftManShow();
}

void lftFallMan()
{
    lftFall( &lftMan );
    lftManShow();
}

void lftHitMan( LftActor* pMovable )
{
    if( lftIsNear( pMovable, &lftMan ) )
      lftMan.status = lftMan.status & ~LFT_ACTOR_LIVE;
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void lftMonsterShow( LftActor* pMonster )
{
    int c;
    c = pMonster->c + ( ( pMonster->status & LFT_ACTOR_SEQMASK ) << 2 );
    lftShowSpriteXY( pMonster->sprite, pMonster->x, pMonster->y, c );
}

bool lftIsNearOtherMonster( LftActor* pMonster )
{
    int i;
    for( i = 0; i < LFT_MAX_MONSTER_COUNT; i = i + 1 )
    {
        LftActor* p;
        p = &lftMonsters[ i ];
        if( ( p->status & LFT_ACTOR_LIVE ) == 0 ) continue;
        if( p->sprite >= pMonster->sprite ) continue;
        if( lftIsNear( pMonster, p ) ) return true;
    }
    return false;
}

void lftDecideDirection( LftActor* pMonster )
{
    int dx, x, y, wait;
    wait = pMonster->status & LFT_WAIT_BIT;
    x = pMonster->x >> LFT_COORD_SHIFT;
    y = pMonster->y >> LFT_COORD_SHIFT;
    dx = pMonster->dx;
    if( dx == 0 )
    {
        if( lftMan.y < pMonster->y )
        {
            if( lftIsOnAnyLift( pMonster ) )
              wait = LFT_WAIT_BIT;
            else
            {
                int cell, flags;
                cell = lftCellMap[ lftCellMapPtr( x, y ) ];
                if( ( cell & LFT_CELL_TYPE_FLOOR ) == 0 )
                  flags = LFT_CELL_LEFTLIFT | LFT_CELL_RIGHTLIFT;
                else
                  flags = cell & ( LFT_CELL_LEFTLIFT | LFT_CELL_RIGHTLIFT );
                if( flags == LFT_CELL_LEFTLIFT || lftMan.x < pMonster->x )
                  dx = -1;
                else if( flags == LFT_CELL_RIGHTLIFT || lftMan.x > pMonster->x )
                  dx = 1;
            }
        }
        else
        {
            if( lftMan.x < pMonster->x )
              dx = -1;
            else if( lftMan.x > pMonster->x )
              dx = 1;
        }
    }
    if( lftMan.y < pMonster->y )
    {
        int cell;
        cell = lftCellMap[ lftCellMapPtr( x, y ) ];
        if( ( cell & LFT_CELL_TYPE_LIFTBIT ) != 0 )
        {
            if( lftLifts[ cell & LFT_CELL_INDEX ].y <= pMonster->y )
            {
                wait = LFT_WAIT_BIT;
                dx = 0;
            }
        }
    }
    else
      wait = 0;
    if( wait == 0 )
    {
        if( !lftCanMoveTo( pMonster, dx ) )
        {
            if( wait == 0 && lftCanMoveTo( pMonster, -dx ) )
              dx = -dx;
            else
              dx = 0;
        }
    }
    if( dx < 0 )
      pMonster->c = LFT_CHAR_MONSTER_LEFT;
    else if( dx > 0 )
      pMonster->c = LFT_CHAR_MONSTER_RIGHT;
    pMonster->dx = dx;
    pMonster->status = ( pMonster->status & ~LFT_WAIT_BIT ) | wait;
}

void lftInitMonsters()
{
    int i, sprite;
    lftMonsterCount = lftStageMonsterCount[ lftStageIndex ];
    sprite = LFT_SPRITE_MONSTER;
    for( i = 0; i < lftMonsterCount; i = i + 1 )
    {
        int b;
        b = lftStageMonsters[ lftStageIndex ][ i ];
        lftMonsters[ i ].x = lftToX( b ) << LFT_COORD_SHIFT;
        lftMonsters[ i ].y = ( lftToY( b ) + 1 ) << LFT_COORD_SHIFT;
        lftMonsters[ i ].sprite = sprite;
        lftMonsters[ i ].clock = 0;
        lftMonsters[ i ].dx = 0;
        lftMonsters[ i ].status = LFT_ACTOR_LIVE;
        lftMonsters[ i ].c = LFT_CHAR_MONSTER_RIGHT;
        lftDecideDirection( &lftMonsters[ i ] );
        lftMonsterShow( &lftMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = lftMonsterCount; i < LFT_MAX_MONSTER_COUNT; i = i + 1 )
    {
        lftMonsters[ i ].status = 0;
        lftMonsters[ i ].sprite = sprite;
        lftHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void lftFallMonsters()
{
    int i;
    for( i = 0; i < lftMonsterCount; i = i + 1 )
    {
        if( ( lftMonsters[ i ].status & LFT_ACTOR_LIVE ) == 0 ) continue;
        lftFall( &lftMonsters[ i ] );
        lftMonsterShow( &lftMonsters[ i ] );
    }
}

void lftMoveMonsters()
{
    int i;
    for( i = 0; i < LFT_MAX_MONSTER_COUNT; i = i + 1 )
    {
        LftActor* pMonster;
        pMonster = &lftMonsters[ i ];
        if( ( pMonster->status & LFT_ACTOR_LIVE ) == 0 ) continue;
        lftHitMan( pMonster );
        if( lftIsOnGrid( pMonster ) )
        {
            if( !lftIsNearOtherMonster( pMonster ) )
              lftDecideDirection( pMonster );
            else if( ( pMonster->status & LFT_WAIT_BIT ) == 0 )
            {
                if( !lftCanMoveTo( pMonster, pMonster->dx << ( LFT_COORD_SHIFT * 2 ) ) )
                  pMonster->dx = 0;
            }
        }
        if( pMonster->dx != 0 && ( pMonster->status & LFT_WAIT_BIT ) == 0 )
        {
            int seq;
            pMonster->x = pMonster->x + pMonster->dx;
            seq = ( ( pMonster->x + LFT_COORD_RATE / 2 ) >> LFT_COORD_SHIFT ) & 1;
            pMonster->status = ( pMonster->status & ~LFT_ACTOR_SEQMASK ) | seq;
            lftHitMan( pMonster );
        }
        lftMonsterShow( pMonster );
        pMonster->clock = pMonster->clock + 1;
    }
}

void lftMoveMonstersOnLift( LftLift* pLift )
{
    int i;
    for( i = 0; i < LFT_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( lftMonsters[ i ].status & LFT_ACTOR_LIVE ) == 0 ) continue;
        if( lftMoveOnLift( &lftMonsters[ i ], pLift ) )
          lftMonsterShow( &lftMonsters[ i ] );
    }
}


// -----------------------------------------------------------------------------
//   Lift.cpp, part B - needs lftMoveManOnLift()/lftMoveMonstersOnLift(),
//   defined just above.
// -----------------------------------------------------------------------------

void lftMoveLifts()
{
    int i;
    for( i = 0; i < LFT_MAX_LIFT_COUNT; i = i + 1 )
    {
        LftLift* pLift;
        pLift = &lftLifts[ i ];
        if( pLift->x == LFT_INVALID_X ) continue;
        pLift->y = pLift->y - 1;
        if( ( pLift->y >> LFT_COORD_SHIFT ) < pLift->top )
          pLift->y = pLift->bottom << LFT_COORD_SHIFT;
        lftLiftShow( pLift );
        lftMoveManOnLift( pLift );
        lftMoveMonstersOnLift( pLift );
        pLift->clock = pLift->clock + 1;
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

void lftFillCellMap()
{
    int floorIdx, col, cellIdx, byteIdx, b, mask, cell;
    cellIdx = 0;
    byteIdx = 0;
    b = lftStageFloorBits[ lftStageIndex ][ 0 ];
    mask = 1;
    for( floorIdx = 0; floorIdx < LFT_FLOOR_COUNT - 1; floorIdx = floorIdx + 1 )
    {
        for( col = 0; col < LFT_COLUMN_COUNT; col = col + 1 )
        {
            if( ( b & mask ) != 0 )
              cell = LFT_CELL_TYPE_FLOOR;
            else
              cell = 0;
            lftCellMap[ cellIdx ] = cell;
            cellIdx = cellIdx + 1;
            // Real byte-truncation-reliance bug fix, caught by inspection
            // before ever compiling - see header comment for the full
            // derivation of why this explicit "& 0xFF" is load-bearing,
            // not decorative (upstream relies on a real uint8_t silently
            // wrapping 0x80<<1 down to 0 here).
            mask = ( mask << 1 ) & 0xFF;
            if( mask == 0 )
            {
                byteIdx = byteIdx + 1;
                b = lftStageFloorBits[ lftStageIndex ][ byteIdx ];
                mask = 1;
            }
        }
    }
    for( col = 0; col < LFT_COLUMN_COUNT; col = col + 1 )
    {
        lftCellMap[ cellIdx ] = LFT_CELL_TYPE_FLOOR;
        cellIdx = cellIdx + 1;
    }
}

void lftSetLiftFlags()
{
    int floor, col, cellIdx, leftIdx, rightBit, cell;
    cellIdx = 0;
    for( floor = 0; floor < LFT_FLOOR_COUNT; floor = floor + 1 )
    {
        leftIdx = cellIdx;
        rightBit = 0;
        for( col = 0; col < LFT_COLUMN_COUNT; col = col + 1 )
        {
            cell = lftCellMap[ cellIdx ];
            if( ( cell & LFT_CELL_TYPE_FLOOR ) != 0 )
              lftCellMap[ cellIdx ] = lftCellMap[ cellIdx ] | rightBit;
            else
            {
                if( ( cell & LFT_CELL_TYPE_LIFTBIT ) != 0 )
                {
                    while( leftIdx != cellIdx )
                    {
                        lftCellMap[ leftIdx ] = lftCellMap[ leftIdx ] | LFT_CELL_LEFTLIFT;
                        leftIdx = leftIdx + 1;
                    }
                    rightBit = LFT_CELL_RIGHTLIFT;
                }
                else
                  rightBit = 0;
            }
            cellIdx = cellIdx + 1;
            if( ( cell & LFT_CELL_TYPE_FLOOR ) == 0 )
              leftIdx = cellIdx;
        }
    }
}

void lftInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=6 (the
    // game never actually stops the player from continuing past stage 7),
    // and shrinks the per-monster time bonus every full lap through the
    // 7 stages (down to a floor of 0) - preserved via the same wrap loop
    // upstream uses instead of a plain modulo, matching Cracky's own
    // equivalent wrap loop structurally, with the added timeRate handling
    // Cracky's own stage cycling doesn't have.
    int i, j;
    i = 0;
    j = 0;
    lftTimeRate = 30;
    while( i < lftCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= LFT_STAGE_COUNT )
        {
            j = 0;
            if( lftTimeRate != 0 )
              lftTimeRate = lftTimeRate - 10;
        }
    }
    lftStageIndex = j;
    lftInitItems();
}

void lftDrawBackground()
{
    int j, floor, col, cellIdx, b, c, x, y, i;

    lftVVram[ 0 ][ 0 ] = LFT_CHAR_ROOF_LEFT;
    for( j = 1; j < LFT_VVRAM_WIDTH - 1; j = j + 1 )
      lftVVram[ 0 ][ j ] = LFT_CHAR_ROOF_CENTER;
    lftVVram[ 0 ][ LFT_VVRAM_WIDTH - 1 ] = LFT_CHAR_ROOF_RIGHT;
    for( j = 0; j < LFT_VVRAM_WIDTH; j = j + 1 )
      lftVVram[ 1 ][ j ] = LFT_CHAR_ROOF_CENTER;

    for( floor = 0; floor < LFT_FLOOR_COUNT; floor = floor + 1 )
    {
        for( col = 0; col < LFT_COLUMN_COUNT; col = col + 1 )
        {
            cellIdx = floor * LFT_COLUMN_COUNT + col;
            b = lftCellMap[ cellIdx ];
            x = col * LFT_COLUMN_WIDTH;
            y = 2 + floor * LFT_FLOOR_HEIGHT;
            for( i = 0; i < LFT_FLOOR_HEIGHT - 1; i = i + 1 )
            {
                lftVVram[ y + i ][ x ] = LFT_CHAR_SPACE;
                lftVVram[ y + i ][ x + 1 ] = LFT_CHAR_SPACE;
            }
            if( ( b & LFT_CELL_TYPE_LIFTBOTTOM ) == LFT_CELL_TYPE_LIFTBOTTOM )
              c = LFT_CHAR_LIFTBOTTOM;
            else if( ( b & LFT_CELL_TYPE_FLOOR ) != 0 )
              c = LFT_CHAR_FLOOR;
            else
              c = LFT_CHAR_SPACE;
            lftVVram[ y + LFT_FLOOR_HEIGHT - 1 ][ x ] = c;
            lftVVram[ y + LFT_FLOOR_HEIGHT - 1 ][ x + 1 ] = c;
        }
    }
}

void lftDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < LFT_SPRITE_COUNT; i = i + 1 )
    {
        if( lftSprites[ i ].code != LFT_INVALID_CODE )
        {
            x = lftSprites[ i ].x;
            y = lftSprites[ i ].y;
            c = lftSprites[ i ].code;
            lftVVram[ y ][ x ] = c;
            c = c + 1;
            lftVVram[ y ][ x + 1 ] = c;
            if( y < LFT_VVRAM_HEIGHT - 1 )
            {
                c = c + 1;
                lftVVram[ y + 1 ][ x ] = c;
                c = c + 1;
                lftVVram[ y + 1 ][ x + 1 ] = c;
            }
        }
    }
}

void lftDrawAll()
{
    lftDrawBackground();
    lftDrawSpritesIntoVVram();
}

void lftInitTrying()
{
    int i;
    lftHideAllSprites();

    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          lftStatusChar[ i ][ j ] = 0;
    }
    lftOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in lftUpdateTitle()) - matches lftOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches lftInitTrying() without going through that transition first.
    // Matches Cracky's own identical defensive reset for crkFullWidthText.
    lftFullWidthText = false;

    lftPrintStatus();
    lftFillCellMap();
    lftInitLifts();
    lftDrawItems();
    lftInitMan();
    lftInitMonsters();
    lftInitPoints();

    lftStageTime = 90;
    i = lftStageMonsterCount[ lftStageIndex ];
    while( i > 0 )
    {
        lftStageTime = lftStageTime + lftTimeRate;
        i = i - 1;
    }

    lftSetLiftFlags();

    lftPrintTime();
    lftDrawAll();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// **Restructured from a plain if/else into an OR-combining compose,
// matching Cracky's own crkComposeRawByte() exactly** (see this file's
// own top-of-file comment, "A whole-file architectural fix", and
// lftBeginTitle()'s own comment below) - the previous if/else shape meant
// `lftFullWidthText` (true only during LFT_STATE_TITLE) disabled
// map/VVram rendering ENTIRELY rather than layering status text on top
// of it, so the real "LIFT" bitmap logo (restored into lftVVram by
// lftBeginTitle(), see its own comment) could never actually reach the
// screen no matter what was drawn into lftVVram. Now mapByte is always
// computed first (when rawCol is in the map's own column range), and
// textByte is computed and OR-combined whenever full-width text is
// active OR the column falls outside the map's own range - exactly
// mirroring Cracky's own structure. Safe by construction: the logo
// occupies real hardware pages 1-2 only (VVram rows 2-5), while every
// status-text element visible during the title screen (SCORE/MINI/
// START/CONTINUE/credit) lives on pages 0/3/5/6/7 - genuinely disjoint
// page ranges, so this can never blend two distinct pieces of real
// content together, only let each shine through on its own page.
int lftComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < LFT_VVRAM_WIDTH * 4 )
    {
        // Real hardware page 7 is never touched by VVramToVram() at all
        // (VVramHeight/2 == 7, one page short of the full 8) - stays
        // permanently black there during real gameplay, reproduced
        // directly. This guard is exactly what it was before this
        // restructure - it only ever zeroes mapByte, so it can't hide the
        // "INUFUTO 2026" credit line, which lives on this exact page (7)
        // at real columns 48-95 (inside this same rawCol<96 range) during
        // the title screen: that text comes entirely from the textByte
        // path below, computed and OR-combined regardless of what mapByte
        // ended up being.
        if( rawPage < LFT_VVRAM_HEIGHT / 2 )
        {
            int mapX, sub, upper, lower, upperByte, lowerByte;
            mapX = rawCol / 4;
            sub = rawCol % 4;
            upper = lftVVram[ rawPage * 2 ][ mapX ];
            lower = lftVVram[ rawPage * 2 + 1 ][ mapX ];
            if( sub == 0 )
            {
                upperByte = lftCharPattern[ upper * 2 + 0 ];
                lowerByte = lftCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else if( sub == 1 )
            {
                upperByte = lftCharPattern[ upper * 2 + 0 ];
                lowerByte = lftCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
            else if( sub == 2 )
            {
                upperByte = lftCharPattern[ upper * 2 + 1 ];
                lowerByte = lftCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else
            {
                upperByte = lftCharPattern[ upper * 2 + 1 ];
                lowerByte = lftCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
        }
    }

    if( !lftFullWidthText && rawCol < LFT_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31),
    // matching lftStatusChar's own full-width indexing directly - no
    // "subtract the map width" local-offset math needed, since rawCol/4
    // already lands on the correct real column either way (whether this
    // is the lftFullWidthText title path using the whole range, or the
    // normal gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = lftStatusChar[ rawPage ][ charCol ];
            textByte = lftAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void lftRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( lftOverlayActive && page == lftOverlayPage &&
                col >= lftOverlayCol * 4 && col < lftOverlayCol * 4 + lftOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - lftOverlayCol * 4 ) / 4;
                sub = ( col - lftOverlayCol * 4 ) % 4;
                value = lftAsciiPattern[ lftAsciiIndex( lftOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = lftComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after the same real user-supplied hardware photo of Cracky
// (see this file's own top-of-file comment) proved the previous version
// of this function was simply wrong** - it believed upstream's own
// title-screen text collided with the SCORE/STAGE/TIME/REMAIN status
// labels and had to be trimmed/dropped to fit: "LIFT" was cut entirely
// (compounded by the missing 'L' glyph bug, see lftAsciiPattern's own
// header) and "CONTINUE" was truncated to "CONTINU". Re-reading
// upstream's real `Status.cpp` (`Title()`) line by line shows this
// diagnosis was backwards: none of that text ever collides with anything
// upstream, because upstream's own Vram address space is a genuinely wide
// 32-char-cell-per-page canvas (see lftStatusChar's own header comment) -
// the status labels occupy only columns 24-31 (upstream's own
// `LeftX=24`), and every piece of title-screen text sits at columns 8-23
// or less, well clear of them. The ROOT problem was this port's own
// `lftStatusChar` being modeled as an 8-column-wide grid in the first
// place - now fixed there, this function is rewritten to place
// "START"/"CONTINUE"/the credit line at upstream's real, literal columns
// (with `lftFullWidthText=true` so lftComposeRawByte() renders the full
// canvas instead of just the narrow status zone), and "LIFT"/"MINI" -
// upstream's own real hand-drawn VVram bitmap logo and its subtitle - as
// plain status-grid text at a chosen free page, matching Cracky's own
// established "simplify a purely decorative title graphic to plain text"
// precedent rather than reproducing the literal 16x4 pixel-block bitmap.
void lftBeginTitle()
{
    int i, j;
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };

    for( i = 0; i < LFT_VVRAM_HEIGHT; i = i + 1 )
    {
        for( j = 0; j < LFT_VVRAM_WIDTH; j = j + 1 )
          lftVVram[ i ][ j ] = LFT_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          lftStatusChar[ i ][ j ] = 0;
    }
    lftOverlayActive = false;
    lftFullWidthText = true;
    lftHideAllSprites();

    // matches Cracky's own already-documented fix for the identical "a
    // stale TIME value lingers on the titlescreen after a game over" bug
    // - applied here proactively rather than needing to rediscover it.
    lftStageTime = 0;
    lftPrintStatus();

    // **Restored, matching Cracky's own identical "restore the real
    // bitmap logo" fix (see gameCracky.c's own header comment, "A real
    // user-supplied hardware photo overturns Cracky's own title-screen
    // design", for the full story that prompted revisiting this)**: this
    // is upstream's own real 4-glyph "LIFT" logo bitmap, drawn directly
    // into lftVVram from lftTitleBytes[] at its own real position -
    // upstream's own `Status.cpp` `Title()` places it at
    // `VVram + VVramWidth*2 + TitleLeft` with
    // `TitleLeft = (VVramWidth - 4*TitleLength)/2 = (24-16)/2 = 4` - i.e.
    // VVram row 2, column 4 (the same row-2 starting offset Cracky's own
    // "CRACKY" logo uses; only the starting column differs, since this
    // game's own title is 4 letters wide, not 6). The previous version of
    // this function replaced this with plain small text ("LIFT" via the
    // shared reduced font, needing a hand-built 'L' glyph - see
    // lftAsciiPattern's own header comment; that glyph is now unused by
    // this title word specifically but left in the table regardless,
    // since nothing else in this file's own rendering needs it removed)
    // - reasoning it was "purely decorative", which turned out to be the
    // wrong call: it's the game's actual title wordmark, meant to be the
    // single biggest, most prominent element on the whole title screen,
    // not a throwaway detail. This also needed `lftComposeRawByte()`
    // rewritten to OR-combine this VVram content with lftStatusChar's own
    // text layer rather than choosing one exclusively - see that
    // function's own header comment for why the previous plain if/else
    // shape could never have let this logo reach the screen at all,
    // regardless of what was drawn into lftVVram.
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 4; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                lftVVram[ 2 + row ][ 4 + ch * 4 + col ] = lftTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 15 (`TitleLeft + 4*TitleLength -
    // 5` = 4+16-5), START/CONTINUE at col 9 with the cursor at col 8
    // (ArrowX), the credit line at col 12) - all genuinely clear of both
    // the status labels' own columns 24-31 and the logo's own columns
    // 4-19, so nothing here needs trimming, relocating, or dropping.
    // START shares page 5 with TIME, CONTINUE has page 6 entirely to
    // itself (upstream never uses it for a status label) - exactly
    // matching upstream's own real layout now that a page can hold both
    // a status label and title text/logo at once.
    lftPrintS( 3, 15, sMini, 4 );
    lftPrintS( 5, 9, sStart, 5 );
    lftPrintS( 6, 9, sContinue, 8 );
    lftPrintS( 7, 12, sCredit, 12 );

    lftSelection = 0;
    lftSelectionChanged = true;
    lftPrevLeft = 0; lftPrevRight = 0; lftPrevUp = 0; lftPrevDown = 0; lftPrevFire = 0;
    lftState = LFT_STATE_TITLE;
}

void lftBeginTrying()
{
    // InitTrying() itself calls DrawAll()/PrintTime() at its own end,
    // matching real upstream Stage.cpp exactly (unlike Cracky's own
    // equivalent, whose upstream InitTrying() doesn't) - no extra
    // lftDrawAll() call needed here.
    lftInitTrying();
    lftClock = 0;
    lftMonsterNum = 0;
    lftTimeDenom = LFT_MAX_TIME_DENOM;
    lftStartSeq( 1, LFT_MELODY_START );
    lftState = LFT_STATE_START_JINGLE;
    lftRender();
}

void lftBeginStage()
{
    lftInitStage();
    lftBeginTrying();
}

void lftUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !lftPrevLeft ) || ( right && !lftPrevRight ) ||
                ( up && !lftPrevUp ) || ( down && !lftPrevDown ) );
    justFire = ( fire && !lftPrevFire );
    lftPrevLeft = left; lftPrevRight = right; lftPrevUp = up; lftPrevDown = down; lftPrevFire = fire;

    if( lftSelectionChanged )
    {
        // Cursor at upstream's real ArrowX(8), pages 5 (START) and 6
        // (CONTINUE) - matches Status.cpp's Title() exactly now that the
        // wider grid has real estate to spare here.
        lftSelectionChanged = false;
        if( lftSelection == 0 )
          lftPrintC( 5, 8, '>' );
        else
          lftPrintC( 5, 8, ' ' );
        if( lftSelection == 1 )
          lftPrintC( 6, 8, '>' );
        else
          lftPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        lftFullWidthText = false;
        lftPendingContinue = ( lftSelection == 1 );
        lftScore = 0;
        if( !lftPendingContinue )
          lftCurrentStage = 0;
        lftRemainCount = 3;
        lftBeginStage();
        return;
    }
    if( justDir )
    {
        lftSelection = lftSelection ^ 1;
        lftSelectionChanged = true;
    }
    lftRender();
}

void lftUpdateStartJingle()
{
    if( !lftSeqPlaying( 1 ) )
    {
        lftStartBgm();
        lftClock = 0;
        lftMonsterNum = 0;
        lftTimeDenom = LFT_MAX_TIME_DENOM;
        lftState = LFT_STATE_PLAYING;
    }
    lftRender();
}

void lftBeginLose()
{
    lftStopBgm();
    lftAnimStep = 0;
    lftWaitFrames = 0;
    lftState = LFT_STATE_LOSE_ANIM;
}

void lftUpdateLoseAnim()
{
    int patterns[4] = { LFT_CHAR_MAN_LEFT_STOP, LFT_CHAR_MAN_LOOSE + 0 * 4, LFT_CHAR_MAN_LOOSE + 1 * 4, LFT_CHAR_MAN_LOOSE + 2 * 4 };

    if( lftWaitFrames > 0 )
    {
        lftWaitFrames = lftWaitFrames - 1;
        lftRender();
        return;
    }

    // Upstream's own LooseMan() is a real `do { ShowSprite(...); Sound_Loose();
    // /* blocking */ DrawAll(); ++i; } while (i < 8);` - every one of the 8
    // flashes, INCLUDING the 8th/last one, gets its own full blocking wait
    // for Sound_Loose() to finish playing before the loop (and therefore
    // the whole function) exits. An earlier draft of this port instead
    // checked `lftAnimStep >= 8` and transitioned to the next state (retry
    // or game-over) in the SAME call that had just shown the 8th flash and
    // started its wait timer - meaning that final wait was set but never
    // actually consumed, and the last frame of the death animation
    // (LFT_CHAR_MAN_LOOSE + 2*4) was never genuinely held on screen: either
    // immediately overwritten by a fresh level's own first render (retry
    // path) or masked out by the "GAME OVER" overlay in that same frame's
    // own lftRender() call (game-over path) - the flashing animation
    // effectively lost its final frame. Fixed by gating the post-animation
    // transition behind `lftAnimStep < 8` too, so the 8th flash's own
    // lftWaitFrames is genuinely consumed (one more pass through the wait
    // branch above) before the transition ever runs - matching upstream's
    // real "show + wait, 8 times total, then transition" shape exactly.
    if( lftAnimStep < 8 )
    {
        lftShowSpriteXY( lftMan.sprite, lftMan.x, lftMan.y, patterns[ lftAnimStep & 3 ] );
        lftDrawAll();
        lftStartSeq( 0, LFT_MELODY_LOOSE );
        lftAnimStep = lftAnimStep + 1;
        lftWaitFrames = lftNoteFrames( 1 );
        lftRender();
        return;
    }

    lftMan.status = lftMan.status & ~LFT_ACTOR_LIVE;
    lftRemainCount = lftRemainCount - 1;
    if( lftRemainCount > 0 )
    {
        lftBeginTrying();
        return;
    }
    else
    {
        lftPrintGameOver();
        lftStartSeq( 1, LFT_MELODY_GAMEOVER );
        lftState = LFT_STATE_GAMEOVER_JINGLE;
    }
    lftRender();
}

void lftUpdateGameOverJingle()
{
    if( !lftSeqPlaying( 1 ) )
      lftBeginTitle();
    else
      lftRender();
}

void lftBeginClearWait()
{
    // Upstream orders this `WaitTimer(30); StopBGM(); Sound_Clear();` - the
    // background music keeps playing through the real 30-tick/500ms pause
    // right after the last item is collected, only getting cut off the
    // instant the CLEAR jingle itself begins. An earlier draft of this
    // port called lftStopBgm() immediately on entering this state instead
    // (silencing the BGM for that whole 500ms gap before the jingle even
    // starts) - reordered so lftStopBgm() only fires once the wait
    // actually elapses, in lftUpdateClearWait() below, matching upstream's
    // real ordering exactly.
    lftWaitFrames = 30;
    lftState = LFT_STATE_CLEAR_WAIT;
}

void lftUpdateClearWait()
{
    if( lftWaitFrames > 0 )
    {
        lftWaitFrames = lftWaitFrames - 1;
        lftRender();
        return;
    }
    lftStopBgm();
    lftStartSeq( 1, LFT_MELODY_CLEAR );
    lftState = LFT_STATE_CLEAR_JINGLE;
    lftRender();
}

void lftUpdateClearJingle()
{
    if( !lftSeqPlaying( 1 ) )
    {
        lftWaitFrames = 0;
        lftState = LFT_STATE_BONUS_TALLY;
    }
    lftRender();
}

void lftUpdateBonusTally()
{
    if( lftWaitFrames > 0 )
    {
        lftWaitFrames = lftWaitFrames - 1;
        lftRender();
        return;
    }

    if( lftStageTime >= LFT_BONUS_RATE )
    {
        lftAddScore( 1 );
        lftStageTime = lftStageTime - LFT_BONUS_RATE;
        lftPrintTime();
        lftStartSeq( 0, LFT_MELODY_BEEP );
        lftWaitFrames = lftNoteFrames( 1 );
        lftRender();
        return;
    }

    lftStageTime = 0;
    lftPrintStatus();
    lftCurrentStage = lftCurrentStage + 1;
    lftBeginStage();
}

void lftUpdatePlaying()
{
    lftTickCounter = lftTickCounter + 1;
    if( lftTickCounter < LFT_TICK_DIVISOR )
    {
        lftRender();
        return;
    }
    lftTickCounter = 0;

    // lftClock only ever takes on the "even" values upstream's own Clock
    // held right before a real WaitTimer(4) call - see header comment for
    // the full derivation of why the odd (pure no-op) iterations are
    // skipped entirely and why lftClock advances by 2, not 1, each tick.
    if( ( lftClock & 1 ) == 0 )
    {
        lftFallMan();
        lftFallMonsters();
    }
    if( ( lftClock & 3 ) == 0 )
    {
        lftUpdatePoints();
        lftMoveMan();
        if( lftMonsterNum >= 0 )
        {
            lftMoveMonsters();
            lftMonsterNum = lftMonsterNum - 10;
        }
        lftMonsterNum = lftMonsterNum + 6;
        lftTimeDenom = lftTimeDenom - 1;
        if( lftTimeDenom == 0 )
        {
            lftStageTime = lftStageTime - 1;
            lftTimeDenom = LFT_MAX_TIME_DENOM;
            lftPrintTime();
            if( lftStageTime == 0 )
            {
                lftPrintTimeUp();
                lftDrawAll();
                lftRender();
                lftBeginLose();
                return;
            }
        }
    }
    if( ( lftClock & 7 ) == 0 )
      lftMoveLifts();
    if( ( lftClock & 1 ) == 0 )
    {
        lftBlinkItems();
        lftDrawAll();
    }
    lftClock = lftClock + 2;

    if( ( lftMan.status & LFT_ACTOR_LIVE ) == 0 )
    {
        lftRender();
        lftBeginLose();
        return;
    }

    if( lftItemCount == 0 )
    {
        lftRender();
        lftBeginClearWait();
        return;
    }

    lftRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameLift_init()
{
    int i;

    lftHiScore = 0;
    lftScore = 0;
    lftCurrentStage = 0;
    lftRemainCount = 3;
    lftStageTime = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        lftSeqActive[ i ] = 0;
        lftSeqMelody[ i ] = LFT_MELODY_NONE;
    }
    lftOverlayActive = false;
    lftTickCounter = 0;

    lftBeginTitle();
}

void gameLift_update()
{
    lftAdvanceSound();

    if( lftState == LFT_STATE_TITLE )
      lftUpdateTitle();
    else if( lftState == LFT_STATE_START_JINGLE )
      lftUpdateStartJingle();
    else if( lftState == LFT_STATE_PLAYING )
      lftUpdatePlaying();
    else if( lftState == LFT_STATE_LOSE_ANIM )
      lftUpdateLoseAnim();
    else if( lftState == LFT_STATE_GAMEOVER_JINGLE )
      lftUpdateGameOverJingle();
    else if( lftState == LFT_STATE_CLEAR_WAIT )
      lftUpdateClearWait();
    else if( lftState == LFT_STATE_CLEAR_JINGLE )
      lftUpdateClearJingle();
    else if( lftState == LFT_STATE_BONUS_TALLY )
      lftUpdateBonusTally();
}
