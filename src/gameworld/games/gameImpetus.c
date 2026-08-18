#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// IMPETUS mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_impetus`) - a vertically-
// scrolling shoot-em-up: steer a fighter left/right/up/down across a 24x16
// virtual grid, shooting sky enemies (3 AI archetypes), ground turrets, drop
// barriers, and a periodic multi-hit "fort" boss, while the terrain scrolls
// upward underneath. Endless play across 8 hand-authored stages (looping
// forever, difficulty scaling with CurrentStage) - 3 lives, real in-session
// score tracking (upstream has no EEPROM at all - a CH32V003 RISC-V
// microcontroller, not AVR, same platform family as this project's own
// already-shipped `gameCracky.c`, its primary structural reference here).
//
// Ported directly following gameCracky.c's own established methodology -
// same author, same hardware family, same real 60Hz SysTick timer
// (`Timer.cpp`), same `ScanKeys.h` 4-direction+1-button input scheme (a
// strict subset of what `tinyJoypadShim.h` already exposes - isFire2Pressed()
// goes unused here too), same two-level VVram/CharPattern tile-rendering
// system, same 3-tone-channel software mixer in `Sound.cpp`. No new shim
// primitive was needed.
//
// **No hardware display-orientation transform is applied, on purpose** -
// `InitOled()` here sends the exact same `OledCmd::RightToLeft`/
// `OledCmd::BottomToTop` register pair Cracky's own `InitOled()` sends, and
// Cracky's own header comment documents at length why a from-first-
// principles "fix" for those settings is *wrong*: on real UIAPduino/SSD1306
// hardware they compensate for a physical panel-mounting quirk with no
// equivalent to correct for in a software recreation. `impComposeMapByte()`
// draws directly at its own (col,page) - no mirroring, no bit-reversal.
//
// **Rendering is the same two-level VVram/CharPattern tile system as
// Cracky, reused via the identical nibble-interleave formula** (`SendUL()`'s
// own `(upper&0xF)|(lower<<4)` / `(upper>>4)|(lower&0xF0)` split, verified
// byte-identical between the two games' own `Vram.cpp` before ever writing
// `impComposeMapByte()` - not re-derived from scratch). Diverges from Cracky
// in one structural way: Cracky's own status text lives entirely within a
// fixed columns-96-127 side panel; this game's real `Status.cpp`/`Title()`
// write text (SCORE/STAGE/lives, and the whole title screen: logo/MINI/
// INUFUTO/START/CONTINUE/cursor) via raw `PrintC()`/`Put2C()` hardware-direct
// calls that land at BOTH the columns-96-127 status area *and* within the
// columns-0-95 map area itself (e.g. "GAME OVER" at column 32, "START" at
// column 36) - a real VRAM-persistence case, the same class already found
// and fixed in Cracky's own `PrintTimeUp()`/`PrintGameOver()` (see that
// file's own header comment). Rather than replicate Cracky's narrower
// "columns 96-127 status grid + a separate ad-hoc message overlay" split,
// this port unifies both into ONE general `impTextChar[8][32]` grid spanning
// the *entire* 128-column width (32 char-cells, matching `VramStep`'s own
// real 4-column granularity) - any non-space cell overrides the map's own
// composited byte for that exact position; columns >=96 have no map content
// at all, so a space cell there just renders blank. This is simpler than
// Cracky's own split needed to be, since Cracky's own title-screen
// simplification (see below) never had to draw text over its own map area
// in the first place.
//
// **`Put2C()` (a real 2x2 `CharPattern`-glyph hardware-direct write, used
// only for the "lives remaining" fighter-icon indicator) is simplified to
// plain digit/blank text, matching Cracky's own established precedent for
// this exact situation** (`PrintStatus()`'s own "upstream draws a real 2x2
// Char_Remain icon (Put2C) here... simplified to plain text digits
// throughout" - Cracky's header comment) - `impPrintStatus()` reproduces
// the identical two-branch shape (blank cells only when `RemainCount-1<=2`,
// a digit when `>2`), not a different simplification of my own invention.
//
// **The title screen's own real logo bitmap (`TitleBytes[96]`, drawn
// directly into VVram) was ORIGINALLY simplified to plain text, matching
// Cracky's own now-known-WRONG original decision for its own "CRACKY" logo
// bitmap - restored here to the real bitmap for the exact same reason
// Cracky's own was restored (see that file's own header comment in full:
// a genuine user-supplied photo of Cracky running on real UIAPduino
// hardware proved this logo is NOT decorative filler - it's the single
// largest, most prominent element on the whole title screen, and the
// small ASCII font is even missing several of the letters it would need to
// spell most titles out anyway).** `impTitleBytes[96]` is upstream's own
// real `Status.cpp`/`Title()` `TitleBytes[]` table (6 glyph-slots x 4x4
// VVram cells each, byte-diff-extracted from the real upstream source, not
// hand-transcribed), drawn into `impVVram` at real VVram rows 2-5 (`VVram +
// VVramWidth*2 + TitleLeft`, and `TitleLeft = (VVramWidth - 4*TitleLength)/2
// = 0` here, since `4*TitleLength = 4*6 = 24 = IMP_VVRAM_WIDTH` exactly -
// the logo fills the full VVram width with no left margin) - real hardware
// pages 1-2, matching Cracky's own logo placement exactly (same author,
// same `Status.cpp` shape, same VVram row offset). "MINI" moved from its
// old, wrong, arbitrary column (8) to upstream's own real computed column,
// `TitleLeft + 4*TitleLength - 5 = 19`, at page 3 (row 6/7, safely below
// the logo's own pages 1-2). `impComposeMapByte()`'s first 32 bytes
// (indices 0-15, the logo's own valid glyph range) were independently
// confirmed byte-identical to Cracky's own `crkCharPattern`'s identical
// range before trusting reuse, matching this shared driver family's own
// established "logo" `CharPattern` block (both games' real `Chars.cpp`
// literally comment that block `//logo`).
//
// **`impRender()`'s own text-vs-map compositing was deliberately NOT
// switched to an unconditional OR-combine, unlike Cracky's own
// `crkComposeRawByte()` fix** - a real behavioral difference from Cracky
// worth explaining rather than blindly mirroring. Cracky's own status-text
// grid (`crkStatusChar`) is NEVER written to for columns inside the map
// area (col<96) during real gameplay at all (its own "GAME OVER"/"TIME UP"
// messages bypass `crkComposeRawByte()` entirely via a separate
// `crkOverlayActive` branch in `crkRender()`) - so an unconditional OR
// there is genuinely safe, since map and status-text content never
// actually coexist in the same cell outside the title screen. This game's
// own `impTextChar` grid is architecturally different (see this file's own
// header comment further up) - it's the ONE mechanism used both for the
// status zone AND for text printed directly into the map area during real
// gameplay ("GAME OVER" at charCol 8-16, page 4), and that in-map text
// case genuinely NEEDS the exclusive-override behavior already fixed once
// in this same file (see finding 1 in the verification-pass writeup below,
// `impPrintC()`'s own `asciiIndex+1` fix) - an unconditional OR would
// silently let the frozen terrain underneath "GAME OVER" bleed back
// through both its embedded space AND every letter's own dark pixels,
// undoing that fix. Instead, `impRender()` OR-combines `impComposeMapByte()`
// with the text layer ONLY while `impState == IMP_STATE_TITLE` (the real
// analogue of Cracky's own `crkFullWidthText` gate) - during the title
// screen this is pure defense-in-depth (the logo's own pages 1-2 and every
// piece of title-screen text are confirmed disjoint by construction, so
// the OR never actually changes anything there either), while every other
// state keeps the original, already-verified exclusive-choice behavior
// gameplay's own in-map text depends on.
//
// **The main loop's own `Clock`-gated update cadence is reproduced exactly,
// not simplified away** - upstream's `loop:` runs a mix of conditions keyed
// off a free-running `Clock` byte (`Clock&1==0` gates Fighter/EnemyBullet
// movement AND the whole draw+`WaitTimer(6)` pair; `Clock&3==0` gates Sky/
// Barrier/Item; `Clock&7==0` gates Fort; `Clock&15==0` gates ground-scroll/
// GroundEnemy; `MoveFighterBullets()` is the only call with NO gate at all)
// - and, critically, `WaitTimer(6)` (the real frame-rate throttle) *only
// ever executes on even Clock values*, meaning every "real tick" upstream
// is actually TWO back-to-back loop iterations: one gated/throttled/drawn
// (even Clock), immediately followed by one fast, undrawn, ungated-except-
// for-MoveFighterBullets() iteration (odd Clock) with zero real delay
// before the next real wait. The net observable effect: fighter bullets
// move at 2x the rate of everything else. Ported as `impAdvanceOneClockTick()`
// called TWICE per real throttled engine tick (once for the even-Clock
// gated pass, once for the odd-Clock fighter-bullets-only pass) - since the
// tick this port is entered on is always even by construction, the several
// `(Clock&1)==0` checks that are unconditionally true at that entry point
// (MoveFighter/MoveEnemyBullets, UpdateBangs+DrawAll) are written directly
// rather than re-tested, with a comment explaining why.
//
// **`IMP_TICK_DIVISOR = 6`**, from `WaitTimer(6)` - the same real 60Hz
// SysTick timer as Cracky (`Timer.cpp`'s own `kTimerHz=60`), just a
// different divisor value (Cracky's own game used `WaitTimer(8)`).
//
// **Sound**: same real 3-tone-channel software mixer as Cracky
// (`Sound.cpp`), but `Tempo=180` here (not Cracky's 160) - re-derived the
// note-duration formula from scratch rather than reusing Cracky's own
// 1.875 multiplier: `SoundHandler()`'s real advance interval is
// `(600/2)/Tempo` real 60Hz ticks, giving `300/180 = 1.6667` here.
// `impNoteFrames(length) = round(length * 1.6667)`. Every melody table
// (`Sound_Fire`/`Sound_Up`/`Sound_Start`/`Sound_GameOver`/`StartBGM`'s own
// two simultaneous voices) was byte-diff-extracted via a small Python
// script (resolving the real `NoteLength`/`Scale` enum values), not hand-
// transcribed - `impFrequencies[40]` was independently confirmed byte-
// identical to Cracky's own `crkFrequencies[40]` before reuse (both games
// share the exact same equal-tempered E2..G5 table). Routed through the
// same `crkStartSeq`/`crkAdvanceOneSeq`-shaped sequencer as Cracky
// (3 independent voices: 0=one-shot SFX reused for Fire/Up, 1=jingle/BGM-
// voice-A, 2=BGM-voice-B), safe against collision the same way Cracky's
// own header explains (`md_playTone()` is genuinely multi-voice project-
// wide - see this project's own CLAUDE.md).
//
// This game also has a real `EffectChannel` (a decaying noise burst via a
// software LFSR, `Sound_SmallBang()`/`Sound_LargeBang()` - a genuinely
// different sound-generation mechanism than the tone channels, with no
// Vircon32 equivalent for true noise). Approximated as a single, short,
// representative `md_playTone()` blip at the same base frequency
// (3000Hz/1500Hz) rather than the real ~0.89-second linear volume-decay
// duration the raw timer math computes (`MaxVolume=63`, decrementing by 2
// per advance-tick, `~32 advance-ticks * 1.6667 / 60 ~= 0.89s`) - a
// deliberate simplification, since a held ~0.89s tone (especially with
// several enemies destroyed in quick succession, each grabbing its own
// free `md_playTone()` channel with no envelope/decay of its own) would
// read as an odd sustained drone rather than the intended quick "bang",
// matching this project's own established "no exact noise equivalent,
// use a short representative tone" precedent (e.g. UFO's own thruster hum).
//
// **Data extraction**: `Chars.cpp` (`AsciiPattern`/`CharPattern`) and the
// whole of `Stages.cpp` (`MapBytes[768]`, 54 `Tiles[16]` glyph blocks, 8
// `SkyElementsN[]` tables, 8 `GroundElementsN[]` tables each cross-
// referencing one of 38 named `RouteXBY[]` tables, and the final `Stages[8]`
// stitching them together) were extracted via a small Python script
// (parsing the real array literals directly, resolving `Char_X + N`
// expressions against `Chars.h`'s own real constants) rather than hand-
// transcribed, with every count cross-checked against upstream's own
// `Char_End`/array-length invariants before ever being pasted in - the
// same "byte-diff transcribed tables" discipline this project has needed
// repeatedly (Tiny Bomber's own dropped-byte bug is the canonical example).
// `AsciiPattern[108]` was independently confirmed byte-identical to
// Cracky's own `crkAsciiPattern[108]` (both games share the exact same
// " 0123456789>ACEFGIMNOPRSTUV" font) before reuse.
//
// **All of `Stages.cpp`'s own C++ struct cross-references (`GroundElement.
// pRoute`, `Stage.pSkyElements`, `Stage.pGroundElement` - real pointers
// into other const arrays) were flattened into explicit array indices
// instead**, matching this project's own established "resolve by id/index,
// not a raw pointer into game data" precedent (e.g. Tiny Dungeon's own
// bitmap-array resolver) - a lower-risk, more conservative choice than
// trusting untested pointer-into-a-data-table semantics on this dialect,
// even though plain struct pointers are confirmed to work elsewhere. Every
// stage's own `SkyElementsN[]`/`GroundElementsN[]` tables were concatenated
// into one flat `impSkyElem*[70]`/`impGroundElem*[142]` pair (with each
// stage's own `impStageSkyStart`/`impStageGroundStart` marking where its
// own slice begins), and every named `RouteXBY[]` table was concatenated
// into one flat `impRoute*[83]` (with each ground element's own
// `impGroundElemRS` marking where its own route slice begins) - built by
// the same extraction script, in first-use (stage) order, not re-derived
// by hand.
//
// **A genuine, real upstream out-of-bounds-array-read bug, fixed with an
// explicit clamp rather than reproduced** - the single riskiest finding
// of this whole port, caught by inspection before ever compiling, not by
// a crash. `GroundEnemy::pRoute` (a raw pointer walking one of the 38
// named `RouteXBY[]` arrays) is advanced every time a route leg's own
// `moveCount` reaches 0
// (`++pEnemy->pRoute; auto pRoute=pEnemy->pRoute; pEnemy->dx=pRoute->dx; ...`)
// - gated behind `if (pEnemy->routeCount > 0)`, which reads as "keep
// cycling while there are more route legs left." But `pEnemy->routeCount`
// is set ONCE at spawn (from the ground element's own real leg count) and
// is *never decremented* anywhere in this file - the intended decrement
// is a literal dead, commented-out line (`// pEnemy->routeCount;`,
// presumably meant to be `--pEnemy->routeCount;`, disabled by the original
// author and apparently never reinstated). Since the gate condition can
// therefore never become false, `++pEnemy->pRoute` walks PAST the real end
// of its own array (2-3 entries typically) the very first time it would
// otherwise "finish" a route, reading whatever memory happens to sit right
// after that specific `const Route[]` array in the compiled binary -
// undefined behavior, silently harmless on real embedded flash (an
// adjacent-static-data read, further bounded by the enemy scrolling off
// screen before it usually matters), but a genuine out-of-bounds global
// read on this platform - the exact class of bug this project's own
// history (Tiny Arena's `Lvl1[9][9]` off-by-one, Tiny Gilbert's key-search
// bound) has repeatedly found and fixed with an explicit clamp rather than
// reproduced. **Fixed** by tracking each ground enemy's own real route-leg
// count (`impGeRouteTotal[i]`, from the ground element's own true
// `impGroundElemRC` value) and its current position within that count
// (`impGeRouteIndex[i]`), clamping the advance so it can reach the LAST
// real leg and then simply hold there indefinitely (continuing along its
// final known direction forever) instead of ever reading past the flat
// `impRouteDx`/`Dy`/`Count` table's own real per-enemy slice. This
// preserves upstream's own observable intent (the `routeCount>0` check is
// clearly meant to allow indefinite cycling) without the real memory-
// safety risk.
//
// **The rest of the upstream `byte`/wraparound-as-a-feature idioms already
// well-documented in this project's own CLAUDE.md (byte-truncation bugs,
// AVR/CH32-narrow-type reliance) were replaced with provably-equivalent
// signed-range checks instead of literal `&0xFF`-style masking**, since
// every case here happens to be tightly bounded and admits a clean signed-
// comparison equivalent (documented at each site below), rather than
// reproducing the wraparound mechanism itself:
//   - `DrawGround()`'s own `byte yPos = GroundY;` (GroundY always in
//     [-4,-1]) relies on unsigned-byte wraparound to make "still above the
//     visible area" register as `>= VVramHeight` (false for `<16`) exactly
//     when logically negative. Since the real, unwrapped range of every
//     `yPos` value this function ever computes is provably [-4,18] (a
//     small, fixed range - see `impDrawGround()`'s own comment for the
//     derivation), a plain signed `destRow>=0 && destRow<16` check is
//     exactly equivalent and needs no masking at all - `impDrawGround()`
//     is rewritten as an explicit (groundRow,groundCol,subRow,subCol)
//     nested loop instead of replicating the original's own raw
//     `pVVram`/`pGround` pointer walk (which transiently points *before*
//     the real array start - real undefined behavior even on the original
//     platform - not something worth reproducing literally).
//   - `Fort::DrawFort()`'s own `y = FortY - Height;` (always <= 0, given
//     `FortY` never exceeds `MaxY=6` and `Height=6`) relies on the same
//     wraparound trick via `while (y >= VVramHeight)`; replaced with a
//     plain `while (y < 0)`.
//   - Every "is this bullet/enemy/barrier still active" check
//     (`pX->y >= RangeY` as the *inactive* sentinel, relying on `y`
//     wrapping to a large positive byte both for its initial `0xff`
//     sentinel value and for any negative in-flight position) is ported
//     as a genuinely signed field defaulting to `-1`, checked via
//     `y < 0 || y >= RangeY` wherever upstream checked `y >= RangeY`
//     alone - `EnemyBullet`'s own diagonal Bresenham-style velocity
//     accumulator (`numeratorX/Y`, `denominatorX/Y`) needed no such
//     change, since those fields never rely on wraparound themselves.
//   - `Bang::Show()`'s own `x -= 1; y -= 1; if (x < RangeX && y < RangeY)`
//     (an edge-of-screen guard relying on `x`/`y` wrapping to a large byte
//     when the bang's own quadrant offset would go negative) is ported as
//     a plain `x>=0 && x<RangeX && y>=0 && y<RangeY` check on signed ints.
//
// **A genuine, faithfully-preserved upstream quirk, left exactly as
// observed rather than "fixed"**: `Bang::UpdateBangs()`'s own
// `++count; if (count >= 1) {...}` is - once actually traced - always
// true on the very first tick a bang exists (`count` starts at 0, so
// `++count` makes it 1, and `1 >= 1` is unconditionally true) - meaning
// the `else` branch (`pBang->status = mode | count;`, which would hold a
// bang's own animation phase for more than one tick) is dead code, and a
// small bang flashes for exactly one real tick, a large bang for exactly
// two (one tick each for its "small" and "large-quadrant" phases) before
// vanishing. This reads as a real, if likely-unintentional, quirk rather
// than a deliberate design choice - kept exactly as written, matching this
// project's own "preserve a faithful, even if odd, upstream comparison
// rather than silently fixing it" precedent (already used once in Cracky's
// own `Monster.cpp` port). An earlier draft of this comment also claimed
// `SkyEnemy::DecideDirection()`'s own upstream had a similar cross-axis
// comparison typo ("Man.x < pMonster->y") preserved here too - re-checked
// directly against the real upstream `SkyEnemy.cpp` during a later
// verification pass and that claim was simply wrong: `DecideDirection()`'s
// `Type_Smart` branch correctly compares `FighterX` against `pEnemy->x`
// and `FighterY` against `pEnemy->y` on both axes, with no typo anywhere,
// and `impDecideDirectionSky()` already ports it exactly that way. No
// upstream source, no game in this project, actually contains that
// "Man.x < pMonster->y" line - corrected here rather than left standing.
//
// **A real declaration-order constraint, solved by careful function
// ordering rather than forward declarations**: unlike Cracky's own mostly-
// linear (acyclic) call graph, this game's collision/movement web is
// genuinely cyclic at the *module* level - `Fighter::MoveFighter()` needs
// `FighterBullet::StartFighterBullet()`, which (via its own `Hit()` helper)
// needs `SkyEnemy`/`GroundEnemy`/`Barrier`/`Fort`'s own `HitBullet*`
// functions, which in turn need `EnemyBullet::StartEnemyBullet()`, which
// itself needs `Fighter::HitBulletFighter()` - closing the loop back to
// Fighter. Resolved by splitting `Fighter`'s own functions into two
// groups at different points in the file: its "predicate" functions
// (`impHitBulletFighter`/`impHitEnemyFighter`, plus `impFighterCrash`/
// `Show`/`Hide`/`impInitFighter`, none of which ever call into
// `FighterBullet`) are defined early, breaking the cycle; `impMoveFighter`
// itself (the one function that genuinely needs `FighterBullet`'s own
// `impStartFighterBullet`) is defined only once every other module it
// depends on already exists. The full definition order used throughout
// this file, and why each step is safe, is documented inline at each
// section boundary below.
//
// **A meticulous post-ship verification pass** (this game was originally
// ported by a different agent in a large parallel batch and only test-
// compiled, never played) re-checked every data table byte-for-byte via a
// Python re-extraction of the real upstream source (`impMapBytes`/
// `impTilesData`/every `impSkyElem*`/`impGroundElem*`/`impRoute*`/every
// `impStage*` table/`impAsciiPattern`/`impCharPattern`/`impFrequencies`/
// all 6 melody tables - all confirmed byte-identical, zero transcription
// errors found), re-traced the entire state machine and every gameplay
// function (Fighter/FighterBullet/EnemyBullet/SkyEnemy/GroundEnemy/Fort/
// Barrier/Bang/Item/Sprite/Stage) line-by-line against the real upstream
// `.cpp` files, and confirmed the CPU stays comfortably under budget in
// both the title screen (~38-45%) and busy gameplay (~44-51% CPU, ~7-10%
// GPU) via the WebGL perf overlay - no CPU-overrun-driven frame
// truncation found here (unlike the sibling "Ascend" game from the same
// batch). Found and fixed two real bugs, both confirmed live via a
// Puppeteer/WebGL test instance:
//
// 1) **A rendering-fidelity bug in the shared text overlay, affecting
// any printed string with an embedded space positioned over the map area
// (col<96)** - most visibly "GAME OVER" (page4, charCol8-16, with a
// space at charCol12 between the two words). `impPrintC()` originally
// stored `impAsciiIndex(c)` directly into `impTextChar`, and space is
// index 0 in the ascii table - meaning a printed space was
// indistinguishable from "nothing ever printed here", so `impRender()`'s
// own `impTextChar[page][charCol] != 0` check fell through to
// `impComposeMapByte()` for that exact cell, letting whatever map/sprite
// content was underneath (the frozen gameplay scene, for "GAME OVER")
// show through the gap instead of a clean blank. Upstream's own real
// `PrintC()` has no such distinction - it's a direct hardware write that
// unconditionally draws real pixels (including blank ones for a space),
// physically overwriting whatever was on the OLED at that column
// regardless of what's underneath. **Fixed** by storing `asciiIndex+1`
// (never 0) so a literal space is a real, distinct "draw a blank glyph"
// value rather than "no override" - `impRender()`'s lookup subtracts 1
// back off before indexing `impAsciiPattern`. Verified visually via a
// forced-game-over test (see finding 2 below): "GAME OVER" now shows a
// clean blank gap between the two words instead of the dotted terrain
// texture bleeding through it.
//
// 2) **A genuine upstream out-of-bounds melody-table read that hangs the
// game over screen indefinitely on this platform** - found via live play
// (a temporary debug hook forcing `RemainCount` to 0 after ~2 real
// seconds of play, to reach Game Over quickly; removed again once this
// was fixed and confirmed). `Sound_GameOver()`'s own `notes[]` table is
// missing the trailing terminator sentinel every *other* melody in this
// file has (Fire/Up/Start all end with an explicit extra `0`; both
// `StartBGM()` voices end with an explicit extra `0xff`) - it's exactly
// 18 bytes (9 length/note pairs), with nothing after the last pair to
// signal "stop". Confirmed directly against the real upstream source,
// not a transcription slip. On real hardware this silently reads
// whatever static data sits next in flash (harmless there, since some
// nearby byte is eventually 0) - ported as a fixed-size `int[18]`
// global with no bounds check anywhere in `impMelodyValue()`, reading
// past its end instead walks into `impMelodyBgm1`/`impMelodyBgm2`/
// further persistent game globals as if they were melody bytes, with no
// guarantee any of that data ever contains a literal 0 again within a
// reasonable time. Symptom, confirmed via the debug-forced test: the
// "GAME OVER" screen (jingle playing on channel 1) never returned to the
// title screen at all - `impSeqPlaying(1)` never became false. **Fixed**
// with an explicit bounds check in `impAdvanceOneSeq()` against
// `impMelodyLength()` (already tracks each table's own real element
// count) - reaching or passing the true end of a melody's data is always
// treated as "stop", covering all 6 melody tables uniformly rather than
// patching `impMelodyGameOver` specifically. Re-verified with the same
// debug hook: the game over screen now reliably returns to the title
// screen within the jingle's own real ~2.7s duration, both title and
// game-over screens rendering cleanly on the way.
//
// Also fixed: `impStopBgm()` only stopped sequencer channels 1/2 (the
// two BGM voices), but upstream's real `StopBGM()` resets ALL 3
// `ToneChannel`s, including channel 0 (the one-shot Fire/Up SFX voice) -
// added the missing `impStopSeq(0)` so an in-flight Fire/Up blip gets
// silenced along with the BGM the instant a game ends, matching upstream
// exactly, instead of being left free to keep advancing its own queued
// notes straight through the game-over jingle. And: an earlier revision
// of this header comment claimed `SkyEnemy::DecideDirection()`'s own
// upstream had a cross-axis comparison typo ("Man.x < pMonster->y")
// preserved faithfully here too - re-checked directly against the real
// upstream `SkyEnemy.cpp` during this pass and that claim was simply
// wrong (both axes are compared correctly, matching what
// `impDecideDirectionSky()` already does) - corrected above rather than
// left standing.
//
// Everything else audited came back clean: every `impMoveFighter`/
// `impStartFighterBullet`/`impMoveEnemyBullets`/`impStartSkyEnemy`/
// `impMoveSkyEnemies`/`impStartGroundEnemy`/`impMoveGroundEnemy`/
// `impMoveFort`/`impStartBarrier`/`impMoveBarriers`/`impUpdateBangs`/
// `impMoveItem` function traced line-by-line against upstream with no
// further discrepancies; every signed-range/shift-safety rewrite
// documented above re-derived and confirmed still correct; the
// `impGeRouteIndex`/`impGeRouteTotal` route-advance clamp (the original
// port's own headline fix) re-verified against the real, never-
// decremented upstream `routeCount` field; `impDrawGround()`'s own
// (groundRow,subRow) coverage re-proven (by hand, for every real
// `impGroundY` value in its documented [-4,-1] range) to always cover
// all 16 VVram rows every call, so no explicit VVram-clear was ever
// needed on the fresh-game transition; and the title/status text layout
// (SCORE/STAGE/lives vs IMPETUS/MINI/credit/START/CONTINUE) confirmed to
// never collide, both by column-range analysis against the real
// `LeftX`/`ArrowX`/`TitleLeft` constants and via direct screenshot.
//
// This file is a mechanical dialect conversion of the sibling
// tinyjoypad_vircon32 project's own `src/games/gameImpetus.c` (see this
// project's own CLAUDE.md "Dialect conversion" section) - `TYPE[N] name`
// declarations became `TYPE name[N]`, the bare `struct ImpSprite {...}`
// tag became a real `typedef struct {...} ImpSprite`, and no game-logic
// or data-table content was altered from the source file in the process.
// No `gameImpetus_forceRedraw` is registered - `impRender()` draws the
// full 8x128-column frame unconditionally on every real state (title,
// jingles, playing), so nothing would ever call it anyway.
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define IMP_CHAR_SPACE 0x00
#define IMP_CHAR_FIGHTERBULLET 0x10
#define IMP_CHAR_ENEMYBULLET 0x11
#define IMP_CHAR_BARRIER 0x12
#define IMP_CHAR_BARRIERHEAD 0x13
#define IMP_CHAR_FIGHTER 0x14
#define IMP_CHAR_SKYENEMY 0x18
#define IMP_CHAR_SKYENEMY_A 0x18
#define IMP_CHAR_SKYENEMY_B 0x1C
#define IMP_CHAR_SKYENEMY_C 0x3C
#define IMP_CHAR_GROUNDENEMY 0x40
#define IMP_CHAR_BANG 0x48
#define IMP_CHAR_ITEM 0x5C
#define IMP_CHAR_FORT 0x60
#define IMP_CHAR_TERRAIN 0x84
#define IMP_CHAR_END 0x8E

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define IMP_SPRITE_GROUNDENEMY 0
#define IMP_SPRITE_FIGHTER 4
#define IMP_SPRITE_SKYENEMY 5
#define IMP_SPRITE_FIGHTERBULLET 8
#define IMP_SPRITE_ENEMYBULLET 10
#define IMP_SPRITE_ITEM 14
#define IMP_SPRITE_BANG 15
#define IMP_SPRITE_COUNT 19
#define IMP_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   VVram.h / Stage.h
// -----------------------------------------------------------------------------

#define IMP_VVRAM_WIDTH 24
#define IMP_VVRAM_HEIGHT 16
#define IMP_TILE_SIZE 4
#define IMP_MAP_WIDTH 24
#define IMP_MAP_HEIGHT 32
#define IMP_GROUND_WIDTH 6
#define IMP_GROUND_HEIGHT 5
#define IMP_FORT_START_Y 4

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching
//   upstream's own enum exactly (Tempo=180, NOT Cracky's own 160 - see
//   header comment for the re-derived note-duration formula).
// -----------------------------------------------------------------------------

#define IMP_N8 6
#define IMP_N8L 8
#define IMP_N8R 4
#define IMP_N8P ( IMP_N8 * 3 / 2 )
#define IMP_N4 ( IMP_N8 * 2 )
#define IMP_N4P ( IMP_N4 * 3 / 2 )
#define IMP_N2 ( IMP_N4 * 2 )
#define IMP_N2P ( IMP_N2 * 3 / 2 )
#define IMP_N1 ( IMP_N2 * 2 )
#define IMP_N16 ( IMP_N8 / 2 )
#define IMP_TEMPO 180

#define IMP_MELODY_NONE 0
#define IMP_MELODY_FIRE 1
#define IMP_MELODY_UP 2
#define IMP_MELODY_START 3
#define IMP_MELODY_GAMEOVER 4
#define IMP_MELODY_BGM1 5
#define IMP_MELODY_BGM2 6

// -----------------------------------------------------------------------------
//   Fighter.cpp / FighterBullet.cpp / EnemyBullet.cpp constants
// -----------------------------------------------------------------------------

#define IMP_FIGHTER_INITIAL_X ( IMP_VVRAM_WIDTH / 2 - 1 )
#define IMP_FIGHTER_INITIAL_Y ( IMP_VVRAM_HEIGHT - 2 - 1 )
#define IMP_FIGHTER_CRASH_TIME 8
#define IMP_FIGHTER_REVIVE_TIME 31

#define IMP_FB_COUNT 2
#define IMP_FB_RANGE_Y IMP_VVRAM_HEIGHT
#define IMP_FB_SHORT_INTERVAL 1
#define IMP_FB_LONG_INTERVAL ( IMP_FB_SHORT_INTERVAL * 4 )

#define IMP_EB_COUNT 4
#define IMP_EB_RANGE_X IMP_VVRAM_WIDTH
#define IMP_EB_RANGE_Y IMP_VVRAM_HEIGHT
#define IMP_EB_HIVEL 100
#define IMP_EB_LOVEL ( IMP_EB_HIVEL * 70 / 100 )
#define IMP_EB_LONGVEL ( IMP_EB_HIVEL * 92 / 100 )
#define IMP_EB_SHORTVEL ( IMP_EB_HIVEL * 38 / 100 )

// -----------------------------------------------------------------------------
//   SkyEnemy.cpp constants
// -----------------------------------------------------------------------------

#define IMP_SKY_COUNT 3
#define IMP_SKY_RANGE_X ( IMP_VVRAM_WIDTH - 2 + 1 )
#define IMP_SKY_RANGE_Y ( IMP_VVRAM_HEIGHT - 2 + 1 )
#define IMP_SKY_FIRE_MASK 1
#define IMP_SKY_TURN_MASK 1

#define IMP_TYPE_CRASH 0
#define IMP_TYPE_SMART 1
#define IMP_TYPE_INSISTENT 2
#define IMP_TYPE_BARRIER 3
#define IMP_TYPE_COUNT 4

#define IMP_DIR_UP 0
#define IMP_DIR_UPRIGHT 1
#define IMP_DIR_RIGHT 2
#define IMP_DIR_DOWNRIGHT 3
#define IMP_DIR_DOWN 4
#define IMP_DIR_DOWNLEFT 5
#define IMP_DIR_LEFT 6
#define IMP_DIR_UPLEFT 7

int impDirectionDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
int impDirectionDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

int impSkyPatterns[3] = { IMP_CHAR_SKYENEMY + 0, IMP_CHAR_SKYENEMY + 4, IMP_CHAR_SKYENEMY + 4 * ( 1 + 8 ) };
int impSkyPoints[3] = { 4, 6, 10 };

// -----------------------------------------------------------------------------
//   GroundEnemy.cpp constants
// -----------------------------------------------------------------------------

#define IMP_GE_COUNT 4
#define IMP_GE_RANGE_X ( IMP_VVRAM_WIDTH - 2 + 1 )
#define IMP_GE_RANGE_Y ( IMP_VVRAM_HEIGHT - 2 + 1 )
#define IMP_GE_TYPE_FIXED 0
#define IMP_GE_TYPE_MOVABLE 1

int impGePoints[2] = { 3, 5 };

// -----------------------------------------------------------------------------
//   Fort.cpp constants
// -----------------------------------------------------------------------------

#define IMP_FORT_WIDTH 6
#define IMP_FORT_HEIGHT 6
#define IMP_FORT_LEFT ( ( IMP_VVRAM_WIDTH - IMP_FORT_WIDTH ) / 2 )
#define IMP_FORT_MAX_Y ( ( 4 + IMP_FORT_HEIGHT ) * 16 / 24 )
#define IMP_FORT_MAX_LIFE 10
#define IMP_FORT_POINT 50

// -----------------------------------------------------------------------------
//   Barrier.cpp / Item.cpp / Bang.cpp constants
// -----------------------------------------------------------------------------

#define IMP_BAR_COUNT 2
#define IMP_BAR_RANGE_Y IMP_VVRAM_HEIGHT
#define IMP_BAR_MAX_LENGTH 10

#define IMP_ITEM_RANGE_Y ( IMP_VVRAM_HEIGHT - 2 + 1 )

#define IMP_BANG_COUNT 4
#define IMP_BANG_STATUS_NONE 0x00
#define IMP_BANG_STATUS_SMALL 0x10
#define IMP_BANG_STATUS_LARGE_SMALL 0x20
#define IMP_BANG_STATUS_LARGE_LARGE 0x30
#define IMP_BANG_RANGE_X ( IMP_VVRAM_WIDTH - 2 + 1 )
#define IMP_BANG_RANGE_Y ( IMP_VVRAM_HEIGHT - 2 + 1 )

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied. See header comment.
// -----------------------------------------------------------------------------

int impAsciiPattern[108] = {
    0, 0, 0, 0, 31, 17, 31, 0, 0, 0, 31, 0,
    29, 21, 23, 0, 21, 21, 31, 0, 7, 4, 31, 0,
    23, 21, 29, 0, 31, 21, 29, 0, 1, 29, 3, 0,
    31, 21, 31, 0, 23, 21, 31, 0, 31, 14, 4, 0,
    30, 9, 30, 0, 14, 17, 10, 0, 31, 21, 17, 0,
    31, 5, 1, 0, 14, 17, 13, 0, 17, 31, 17, 0,
    31, 6, 31, 0, 31, 1, 30, 0, 14, 17, 14, 0,
    31, 5, 7, 0, 31, 5, 26, 0, 22, 21, 13, 0,
    1, 31, 1, 0, 31, 16, 31, 0, 15, 16, 15, 0,
};

int impCharPattern[284] = {
    0, 0, 51, 0, 204, 0, 255, 0, 0, 51, 51, 51,
    204, 51, 255, 51, 0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255, 0, 240, 246, 111,
    66, 66, 159, 249, 252, 164, 244, 12, 115, 51, 115, 3,
    128, 236, 140, 0, 49, 245, 53, 1, 200, 250, 202, 8,
    19, 113, 17, 3, 36, 247, 189, 15, 0, 1, 53, 1,
    152, 255, 202, 8, 64, 119, 18, 0, 0, 132, 237, 12,
    33, 119, 101, 7, 206, 252, 204, 14, 16, 114, 18, 0,
    236, 141, 4, 0, 103, 117, 39, 1, 200, 250, 159, 8,
    16, 114, 71, 0, 191, 253, 39, 4, 49, 5, 1, 0,
    134, 236, 140, 6, 60, 245, 53, 12, 126, 91, 181, 231,
    231, 173, 218, 126, 211, 110, 230, 61, 188, 103, 118, 203,
    228, 182, 74, 78, 114, 226, 37, 23, 0, 194, 132, 124,
    98, 255, 233, 111, 206, 236, 7, 136, 221, 54, 63, 1,
    100, 219, 123, 226, 0, 54, 17, 226, 151, 171, 88, 70,
    18, 49, 35, 0, 30, 31, 179, 227, 99, 102, 100, 54,
    236, 255, 78, 128, 200, 236, 206, 140, 8, 228, 255, 206,
    80, 85, 102, 7, 166, 204, 204, 106, 112, 102, 85, 5,
    16, 217, 157, 0, 247, 173, 218, 127, 0, 217, 157, 1,
    128, 185, 155, 0, 254, 91, 181, 239, 0, 185, 155, 8,
    160, 170, 102, 14, 86, 51, 51, 101, 224, 102, 170, 10,
    115, 255, 39, 16, 49, 115, 55, 19, 1, 114, 255, 55,
    1, 4, 34, 136, 0, 240, 15, 0, 17, 17, 17, 241,
    31, 17, 136, 136, 136, 248, 143, 136,
};

// TitleBytes - upstream's own real "IMPETUS" title-screen logo bitmap
// (Status.cpp's `Title()`), 6 glyph-slots x 4x4 VVram-cell glyph indices
// each (96 values total), byte-diff-extracted from the real upstream
// source (not hand-transcribed). Every value here is a valid index into
// impCharPattern[]'s own "logo" range (indices 0-15, the first 32 bytes of
// that table, confirmed byte-identical to Cracky's own crkCharPattern's
// identical range) - see impBeginTitle()'s own comment for why this
// replaces the earlier plain-text "IMPETUS" substitute.
int impTitleBytes[96] = {
    4, 15, 1, 15, 0, 15, 0, 15,
    0, 15, 0, 15, 4, 5, 1, 5,
    13, 13, 2, 15, 12, 12, 3, 15,
    12, 12, 3, 15, 4, 4, 1, 5,
    5, 11, 12, 7, 10, 7, 12, 11,
    0, 0, 12, 3, 0, 0, 4, 5,
    5, 4, 13, 7, 10, 0, 12, 3,
    0, 0, 12, 3, 5, 0, 4, 1,
    1, 15, 0, 15, 0, 15, 0, 15,
    0, 15, 0, 15, 0, 4, 5, 1,
    8, 7, 13, 2, 4, 11, 10, 0,
    8, 2, 12, 3, 0, 5, 5, 0,
};

int impMapBytes[768] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 3, 0, 0, 4, 1, 1, 2, 0, 5, 6, 6, 6, 7, 8, 0, 0, 9, 10, 11, 11, 11, 11,
    1, 1, 1, 2, 3, 12, 1, 1, 1, 0, 13, 0, 0, 0, 13, 0, 9, 10, 11, 11, 11, 11, 11, 11,
    1, 1, 1, 1, 2, 14, 14, 14, 14, 14, 15, 14, 14, 14, 15, 14, 10, 11, 11, 11, 11, 11, 11, 11,
    1, 1, 1, 1, 16, 0, 0, 0, 0, 0, 13, 0, 0, 0, 13, 0, 17, 11, 11, 11, 11, 11, 11, 11,
    1, 1, 16, 18, 5, 6, 6, 6, 6, 6, 19, 6, 20, 21, 22, 0, 0, 0, 0, 0, 0, 0, 23, 21,
    1, 16, 18, 0, 13, 0, 0, 24, 25, 0, 13, 0, 26, 27, 28, 6, 6, 6, 6, 6, 6, 29, 30, 27,
    0, 0, 0, 0, 13, 0, 0, 4, 1, 0, 13, 0, 0, 0, 0, 0, 0, 24, 25, 3, 0, 13, 0, 0,
    0, 0, 0, 0, 13, 0, 0, 12, 31, 0, 13, 4, 1, 1, 2, 0, 24, 32, 1, 2, 3, 33, 6, 6,
    6, 6, 29, 6, 34, 6, 6, 6, 6, 6, 35, 0, 12, 36, 16, 0, 32, 1, 37, 1, 2, 13, 4, 1,
    0, 0, 13, 4, 38, 9, 10, 11, 11, 11, 11, 11, 39, 40, 0, 0, 1, 1, 1, 1, 1, 13, 0, 0,
    0, 0, 13, 0, 0, 10, 11, 11, 11, 11, 11, 11, 11, 39, 40, 0, 36, 1, 1, 1, 16, 33, 6, 6,
    6, 6, 41, 8, 0, 11, 11, 11, 11, 11, 11, 11, 11, 11, 39, 0, 12, 36, 1, 16, 18, 13, 0, 32,
    0, 0, 13, 0, 9, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 40, 0, 0, 0, 0, 0, 13, 0, 1,
    0, 0, 13, 0, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 39, 40, 0, 0, 0, 0, 13, 0, 36,
    0, 0, 13, 0, 11, 11, 11, 11, 11, 42, 43, 11, 11, 11, 11, 11, 11, 39, 0, 32, 2, 44, 6, 6,
    6, 6, 41, 0, 11, 11, 11, 11, 11, 39, 45, 11, 11, 11, 11, 11, 11, 11, 0, 1, 1, 0, 0, 0,
    0, 0, 13, 0, 17, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 14, 14, 14, 14, 14, 14,
    0, 0, 13, 8, 0, 43, 17, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 46, 0, 0, 0, 36, 16, 0,
    6, 6, 34, 6, 6, 7, 43, 17, 11, 11, 11, 11, 11, 11, 11, 46, 47, 23, 48, 0, 5, 6, 6, 6,
    0, 0, 24, 32, 2, 13, 0, 43, 11, 46, 47, 0, 43, 49, 47, 24, 25, 26, 50, 0, 13, 0, 0, 0,
    1, 1, 1, 1, 1, 13, 4, 38, 51, 0, 0, 0, 0, 0, 0, 4, 16, 0, 0, 0, 33, 6, 6, 6,
    1, 1, 1, 1, 1, 33, 6, 6, 52, 6, 6, 6, 6, 6, 6, 6, 6, 29, 6, 6, 41, 32, 1, 1,
    1, 1, 1, 1, 16, 13, 32, 2, 51, 32, 1, 2, 0, 0, 32, 1, 2, 13, 0, 0, 13, 1, 1, 1,
    1, 1, 1, 1, 18, 13, 36, 16, 51, 36, 1, 1, 0, 0, 36, 1, 16, 13, 32, 2, 13, 1, 1, 1,
    1, 1, 1, 16, 0, 33, 6, 6, 52, 6, 7, 31, 0, 0, 0, 0, 0, 13, 36, 16, 13, 1, 1, 1,
    1, 1, 18, 0, 0, 13, 0, 0, 51, 0, 44, 6, 6, 6, 6, 6, 6, 34, 7, 0, 13, 36, 1, 1,
    14, 14, 14, 14, 14, 15, 14, 14, 53, 32, 2, 0, 0, 0, 0, 0, 32, 2, 44, 6, 41, 12, 36, 1,
    0, 0, 36, 16, 0, 13, 0, 0, 0, 36, 16, 0, 0, 0, 0, 0, 36, 16, 0, 0, 13, 0, 12, 1,
    6, 6, 6, 6, 6, 41, 8, 0, 0, 0, 0, 5, 6, 6, 6, 6, 6, 7, 0, 0, 13, 8, 0, 36,
    0, 4, 38, 0, 0, 44, 6, 6, 6, 6, 6, 35, 0, 4, 1, 1, 38, 44, 6, 6, 34, 6, 6, 6,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

int impTilesData[864] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132,
    132, 132, 0, 0, 132, 132, 132, 0, 132, 132, 132, 132, 132, 132, 132, 132,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 132, 0, 0, 0,
    0, 0, 132, 132, 0, 132, 132, 132, 0, 132, 132, 132, 0, 0, 132, 132,
    0, 139, 139, 139, 134, 0, 0, 0, 134, 0, 0, 0, 134, 0, 0, 138,
    139, 139, 139, 139, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 136, 136,
    139, 139, 139, 0, 0, 0, 0, 135, 0, 0, 0, 135, 137, 0, 0, 135,
    0, 0, 0, 0, 0, 132, 132, 0, 0, 132, 132, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 133,
    0, 0, 133, 133, 0, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133,
    133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133,
    0, 0, 0, 132, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    134, 0, 0, 135, 134, 0, 0, 135, 134, 0, 0, 135, 134, 0, 0, 135,
    0, 0, 0, 0, 133, 133, 133, 133, 133, 133, 133, 133, 0, 0, 0, 0,
    134, 0, 0, 135, 133, 133, 133, 133, 133, 133, 133, 133, 134, 0, 0, 135,
    132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 0, 132, 132, 0, 0,
    133, 133, 133, 133, 133, 133, 133, 133, 0, 133, 133, 133, 0, 0, 133, 133,
    132, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    140, 0, 0, 141, 0, 0, 0, 0, 0, 0, 0, 0, 137, 0, 0, 138,
    139, 139, 139, 139, 0, 0, 0, 0, 0, 0, 0, 0, 137, 0, 0, 0,
    139, 139, 139, 139, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    140, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135,
    0, 139, 139, 139, 134, 0, 0, 0, 134, 0, 0, 0, 134, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 132,
    0, 0, 0, 0, 0, 132, 132, 0, 132, 132, 132, 132, 132, 132, 132, 132,
    134, 0, 0, 0, 134, 0, 0, 0, 134, 0, 0, 0, 0, 136, 136, 136,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 136, 136,
    0, 0, 0, 141, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 136, 136,
    139, 139, 139, 139, 0, 0, 0, 0, 0, 0, 0, 0, 137, 0, 0, 138,
    140, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 136, 136,
    132, 132, 132, 132, 132, 132, 132, 132, 0, 132, 132, 0, 0, 0, 0, 0,
    0, 0, 132, 132, 0, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132,
    134, 0, 0, 141, 134, 0, 0, 0, 134, 0, 0, 0, 134, 0, 0, 138,
    140, 0, 0, 141, 0, 0, 0, 0, 0, 0, 0, 0, 136, 136, 136, 136,
    140, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135, 136, 136, 136, 0,
    132, 132, 132, 132, 132, 132, 132, 132, 0, 132, 132, 132, 0, 0, 132, 132,
    132, 0, 0, 132, 0, 0, 0, 0, 0, 0, 0, 0, 132, 0, 0, 132,
    132, 132, 0, 0, 132, 132, 132, 0, 132, 132, 132, 0, 132, 132, 0, 0,
    133, 133, 0, 0, 133, 133, 133, 0, 133, 133, 133, 133, 133, 133, 133, 133,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 133, 0, 0, 0,
    140, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135, 137, 0, 0, 135,
    133, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 133, 0, 0, 0,
    0, 0, 0, 133, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    134, 0, 0, 141, 134, 0, 0, 0, 134, 0, 0, 0, 0, 136, 136, 136,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 133, 0, 0, 133,
    133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 0, 133, 133, 0, 0,
    133, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    139, 139, 139, 0, 0, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135,
    133, 133, 133, 133, 133, 133, 133, 133, 0, 133, 133, 0, 0, 0, 0, 0,
    0, 0, 0, 135, 0, 0, 0, 135, 0, 0, 0, 135, 136, 136, 136, 0,
    0, 133, 133, 0, 0, 133, 133, 0, 0, 133, 133, 0, 0, 133, 133, 0,
    139, 133, 133, 139, 0, 133, 133, 0, 0, 133, 133, 0, 136, 133, 133, 136,
    0, 133, 133, 0, 133, 133, 133, 0, 133, 133, 133, 0, 0, 0, 0, 0,
};

#define IMP_TILE_COUNT 54

int impSkyElemRow[70] = {
    124, 96, 92, 64, 60, 48, 44, 16, 12, 4, 124, 96,
    88, 76, 56, 36, 32, 20, 16, 4, 124, 104, 80, 76,
    48, 44, 36, 20, 8, 4, 124, 112, 72, 52, 40, 16,
    4, 0, 124, 120, 76, 64, 60, 40, 32, 4, 124, 120,
    76, 68, 56, 48, 24, 4, 124, 120, 76, 68, 48, 44,
    16, 4, 124, 120, 92, 40, 24, 8, 4, 0,
};

int impSkyElemBitsTbl[70] = {
    1, 3, 2, 10, 8, 0, 4, 0, 1, 0, 1, 3,
    2, 10, 8, 12, 4, 5, 1, 0, 1, 3, 2, 10,
    8, 12, 4, 5, 1, 0, 1, 3, 2, 6, 4, 12,
    8, 0, 1, 3, 11, 10, 14, 6, 4, 0, 2, 3,
    11, 15, 14, 6, 4, 0, 2, 3, 7, 15, 14, 6,
    4, 0, 2, 3, 7, 6, 14, 12, 4, 0,
};

int impGroundElemX[142] = {
    1, 9, 1, 5, 21, 21, 21, 9, 21, 13, 17, 9,
    1, 5, 1, 5, 9, 1, 21, 9, 9, 21, 5, 21,
    1, 5, 5, 21, 13, 21, 1, 21, 21, 17, 5, 17,
    17, 9, 1, 1, 1, 9, 9, 13, 21, 17, 5, 1,
    13, 21, 9, 21, 9, 1, 5, 5, 9, 9, 9, 21,
    13, 9, 13, 21, 5, 13, 21, 1, 21, 21, 1, 1,
    5, 21, 1, 1, 17, 17, 21, 21, 9, 5, 17, 9,
    1, 5, 1, 9, 13, 1, 17, 9, 21, 5, 1, 21,
    1, 13, 1, 1, 17, 5, 21, 5, 9, 13, 17, 21,
    9, 1, 5, 13, 1, 1, 21, 1, 9, 13, 21, 5,
    9, 21, 1, 9, 1, 21, 17, 1, 9, 1, 9, 21,
    21, 13, 5, 9, 9, 5, 17, 21, 1, 5,
};

int impGroundElemY[142] = {
    117, 113, 105, 97, 93, 89, 85, 81, 77, 49, 49, 41,
    25, 25, 21, 21, 17, 9, 113, 97, 89, 85, 77, 61,
    37, 33, 25, 21, 17, 9, 5, 121, 113, 101, 97, 89,
    85, 73, 69, 61, 37, 33, 29, 25, 21, 17, 9, 5,
    121, 121, 117, 117, 113, 89, 85, 81, 81, 77, 73, 65,
    57, 53, 49, 45, 37, 29, 21, 5, 125, 121, 113, 109,
    109, 109, 105, 89, 85, 81, 81, 77, 49, 41, 37, 29,
    25, 21, 9, 9, 121, 117, 113, 105, 105, 81, 77, 77,
    73, 61, 53, 49, 45, 41, 37, 33, 29, 25, 9, 9,
    5, 121, 113, 113, 89, 85, 85, 77, 77, 77, 77, 53,
    53, 45, 37, 29, 25, 25, 21, 5, 121, 117, 113, 113,
    101, 77, 61, 53, 45, 29, 29, 21, 5, 5,
};

int impGroundElemRC[142] = {
    1, 0, 2, 0, 1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0,
    2, 0, 0, 1, 0, 0, 0, 3, 0, 3, 0, 1,
    0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
    2, 3, 0, 0, 0, 3, 0, 0, 0, 0, 0, 3,
    0, 0, 0, 3, 0, 0, 0, 0, 0, 3, 0, 0,
    0, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 3, 0, 0, 3, 0, 3, 1,
    0, 0, 0, 3, 0, 0, 3, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0,
    0, 3, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0,
    3, 3, 0, 0, 0, 0, 0, 3, 0, 0,
};

int impGroundElemRS[142] = {
    0, 0, 1, 0, 3, 4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 5, 0, 0, 0, 7, 0, 0, 0,
    9, 0, 0, 11, 0, 0, 0, 12, 0, 15, 0, 18,
    0, 0, 0, 0, 19, 0, 0, 0, 20, 0, 0, 0,
    22, 24, 0, 0, 0, 27, 0, 0, 0, 0, 0, 30,
    0, 0, 0, 33, 0, 0, 0, 0, 0, 36, 0, 0,
    0, 39, 42, 44, 0, 0, 0, 0, 0, 0, 0, 0,
    45, 0, 0, 0, 0, 46, 0, 0, 49, 0, 52, 55,
    0, 0, 0, 56, 0, 0, 59, 0, 0, 0, 0, 0,
    0, 62, 0, 0, 63, 0, 0, 0, 65, 0, 0, 0,
    0, 66, 0, 0, 69, 0, 0, 0, 0, 71, 0, 0,
    74, 77, 0, 0, 0, 0, 0, 80, 0, 0,
};

int impRouteDx[83] = {
    1, 1, -1, 0, -1, 0, 1, -1, 0, 1, 0, -1,
    -1, 0, -1, -1, 0, -1, -1, 1, -1, 0, 0, 1,
    -1, 0, -1, 1, 0, 1, -1, 1, -1, -1, 1, 1,
    -1, 1, -1, -1, 0, 1, 1, 0, 1, 1, 1, 0,
    1, 0, 0, 1, 1, 0, -1, -1, 1, 0, -1, -1,
    0, 1, 1, 1, 0, 0, -1, 0, 1, 1, 0, 1,
    0, 1, -1, 0, 1, -1, 0, 1, -1, 0, 1,
};

int impRouteDy[83] = {
    0, 0, 0, 1, 0, 1, 0, 0, 1, 0, -1, 0,
    0, -1, 0, 0, -1, 0, 0, 0, 0, 1, -1, 0,
    0, -1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 1,
    0, -1, -1, 0, 0, -1, 0, 0, 0, -1, 0, 0,
    -1, 0, 0, 0, 1, 1, 0, -1, 0, 0, 1, 0,
    1, 0, 0, -1, 0, 0, -1, 0, 0, 1, 0,
};

int impRouteCount[83] = {
    20, 20, 22, 28, 22, 8, 87, 8, 16, 20, 20, 22,
    12, 4, 10, 8, 24, 10, 8, 95, 16, 16, 48, 83,
    16, 16, 6, 12, 8, 83, 4, 4, 22, 4, 4, 75,
    8, 4, 18, 4, 16, 79, 16, 16, 95, 95, 20, 4,
    75, 4, 12, 75, 8, 12, 10, 22, 8, 12, 10, 4,
    16, 79, 20, 8, 24, 40, 8, 12, 83, 12, 36, 12,
    4, 8, 8, 12, 83, 12, 40, 95, 12, 16, 87,
};

int impStageMapOffset[8] = {
    12, 5, 3, 17, 14, 0, 18, 2,
};

int impStageSkyStart[8] = {
    0, 10, 20, 30, 38, 46, 54, 62,
};

int impStageSkyCount[8] = {
    10, 10, 10, 8, 8, 8, 8, 8,
};

int impStageGroundStart[8] = {
    0, 18, 31, 48, 68, 88, 109, 128,
};

int impStageGroundCount[8] = {
    18, 13, 17, 20, 20, 21, 19, 14,
};

#define IMP_STAGE_COUNT 8

int impFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139, 147, 156,
    165, 175, 185, 196, 208, 220, 233, 247, 262, 277, 294, 311,
    330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622,
    659, 698, 740, 784,
};

int impMelodyFire[13] = {
    1, 38, 1, 36, 1, 34, 1, 32, 1, 30, 1, 40,
    0,
};

int impMelodyUp[13] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30, 1, 33,
    0,
};

int impMelodyStart[23] = {
    6, 30, 12, 32, 12, 33, 12, 33, 6, 30, 12, 35,
    12, 35, 6, 33, 18, 35, 36, 37, 12, 0, 0,
};

int impMelodyGameOver[18] = {
    12, 30, 6, 25, 6, 30, 6, 28, 6, 26, 6, 25,
    6, 23, 36, 25, 12, 0,
};

int impMelodyBgm1[105] = {
    18, 30, 18, 32, 24, 33, 12, 33, 12, 32, 12, 33, 18, 32,
    18, 28, 36, 28, 24, 0, 18, 30, 18, 32, 24, 33, 12, 33,
    12, 32, 12, 33, 18, 40, 18, 35, 36, 35, 24, 0, 18, 38,
    18, 37, 24, 38, 12, 38, 12, 37, 12, 38, 18, 37, 18, 33,
    36, 33, 24, 0, 6, 30, 6, 30, 6, 32, 12, 33, 12, 33,
    6, 33, 6, 32, 6, 32, 6, 33, 12, 35, 12, 35, 6, 35,
    6, 33, 6, 33, 6, 35, 12, 37, 12, 37, 6, 37, 6, 38,
    12, 38, 18, 37, 12, 0, 255,
};

int impMelodyBgm2[227] = {
    12, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0, 6, 10,
    12, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11,
    12, 13, 6, 0, 6, 13, 6, 0, 6, 13, 6, 0, 6, 17,
    12, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6,
    12, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0, 6, 13,
    12, 14, 6, 0, 6, 14, 6, 0, 6, 14, 6, 0, 6, 15,
    12, 16, 6, 0, 6, 16, 6, 0, 6, 16, 6, 0, 6, 16,
    12, 13, 6, 0, 6, 13, 6, 0, 6, 13, 6, 0, 6, 13,
    12, 11, 6, 0, 6, 11, 6, 0, 6, 11, 6, 0, 6, 7,
    12, 16, 6, 0, 6, 16, 6, 0, 6, 16, 6, 0, 6, 16,
    12, 9, 6, 0, 6, 9, 6, 0, 6, 9, 6, 0, 6, 9,
    12, 9, 6, 0, 6, 9, 6, 0, 6, 9, 6, 0, 6, 9,
    6, 6, 6, 6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 0,
    6, 10, 6, 11, 6, 11, 6, 0, 6, 11, 6, 0, 6, 11,
    6, 0, 6, 17, 6, 6, 6, 6, 6, 0, 6, 6, 6, 0,
    6, 6, 6, 0, 6, 6, 6, 14, 6, 14, 6, 0, 18, 13,
    12, 0, 255,
};

// -----------------------------------------------------------------------------
//   Global state (all declared up front - see header comment on why this
//   whole file's own genuinely-cyclic module call graph is resolved purely
//   by function-definition ORDER, not by forward declarations; plain
//   variable declarations have no such ordering constraint).
// -----------------------------------------------------------------------------

int impScore;
int impRemainCount;
int impCurrentStage;
int impClock;

int impVVram[IMP_VVRAM_HEIGHT][IMP_VVRAM_WIDTH];
int impTextChar[8][32];

typedef struct
{
    int x, y, code;
} ImpSprite;
ImpSprite impSprites[IMP_SPRITE_COUNT];

// Fighter
int impFighterX, impFighterY;
int impFighterCrashCount;
int impFighterReviveCount;

// FighterBullet[IMP_FB_COUNT]
int impFbX[IMP_FB_COUNT];
int impFbY[IMP_FB_COUNT];
int impFbIntervalCount;

// EnemyBullet[IMP_EB_COUNT]
int impEbX[IMP_EB_COUNT];
int impEbY[IMP_EB_COUNT];
int impEbDx[IMP_EB_COUNT];
int impEbDy[IMP_EB_COUNT];
int impEbNumX[IMP_EB_COUNT];
int impEbDenX[IMP_EB_COUNT];
int impEbNumY[IMP_EB_COUNT];
int impEbDenY[IMP_EB_COUNT];

// SkyEnemy[IMP_SKY_COUNT]
int impSkyX[IMP_SKY_COUNT];
int impSkyY[IMP_SKY_COUNT];
int impSkyDx[IMP_SKY_COUNT];
int impSkyDy[IMP_SKY_COUNT];
int impSkyPattern[IMP_SKY_COUNT];
int impSkyType[IMP_SKY_COUNT];
int impSkyDirection[IMP_SKY_COUNT];
int impSkyClock[IMP_SKY_COUNT];
int impSkyBulletCount[IMP_SKY_COUNT];
int impSkyNextType;
int impSkyTypeBit;

// GroundEnemy[IMP_GE_COUNT]
int impGeX[IMP_GE_COUNT];
int impGeY[IMP_GE_COUNT];
int impGeType[IMP_GE_COUNT];
int impGeRouteTotal[IMP_GE_COUNT];
int impGeRouteBase[IMP_GE_COUNT];
int impGeRouteIndex[IMP_GE_COUNT];
int impGeDx[IMP_GE_COUNT];
int impGeDy[IMP_GE_COUNT];
int impGeMoveCount[IMP_GE_COUNT];
int impGeClock[IMP_GE_COUNT];

// Fort
int impFortY;
int impFortLife;
int impFortBulletX;

// Barrier[IMP_BAR_COUNT]
int impBarY[IMP_BAR_COUNT];
int impBarLeft[IMP_BAR_COUNT];
int impBarRight[IMP_BAR_COUNT];
int impBarLength[IMP_BAR_COUNT];

// Bang[IMP_BANG_COUNT]
int impBangX[IMP_BANG_COUNT];
int impBangY[IMP_BANG_COUNT];
int impBangStatus[IMP_BANG_COUNT];

// Item
int impItemX, impItemY;

// Stage / Ground scroll state
int impStageIndex;
int impPMapIndex;
int impTopRow;
int impSkyElementCursor;
int impSkyElementRemaining;
int impSkyElementBits;
int impGroundElementCursor;
int impGroundElementRemaining;
int impGround[IMP_GROUND_WIDTH * IMP_GROUND_HEIGHT];
int impGroundY;

int impRndIndex;

// Sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame, matching Cracky's own shape.
int impSeqMelody[3];
int impSeqPos[3];
int impSeqWait[3];
int impSeqActive[3];

#define IMP_TICK_DIVISOR 6
int impTickCounter;

#define IMP_STATE_TITLE 0
#define IMP_STATE_START_JINGLE 1
#define IMP_STATE_PLAYING 2
#define IMP_STATE_GAMEOVER_JINGLE 3
int impState;
int impSelection;
bool impSelectionChanged;
bool impPendingContinue;
int impPrevLeft, impPrevRight, impPrevUp, impPrevDown, impPrevFire;

int impRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int impRnd()
{
    int r;
    r = impRndNumbers[ impRndIndex ];
    impRndIndex = impRndIndex + 1;
    if( impRndIndex >= 32 )
      impRndIndex = 0;
    return r & 0x0f;
}

int impAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int impSign( int from, int to )
{
    if( from == to )
      return 0;
    if( from < to )
      return 1;
    return -1;
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - text overlay, spanning the FULL 128-column
//   width (32 char-cells) - see header comment for why this differs from
//   Cracky's own narrower columns-96-127-only status grid.
// -----------------------------------------------------------------------------

int impAsciiIndex( int c )
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

int impPrintC( int page, int charCol, int c )
{
    // Stored as (asciiIndex+1), never 0, so a literal space character is
    // still distinguishable from "nothing ever printed here" - see the
    // impRender() comment below for why this matters (upstream's real
    // PrintC() unconditionally draws real pixels, including blank ones
    // for a space, physically overwriting whatever was on screen; a
    // naive port using 0 == "no override, show the map/sprites through"
    // would instead let stale map/sprite content bleed through any
    // embedded space in a printed string, e.g. the gap in "GAME OVER").
    impTextChar[ page ][ charCol ] = impAsciiIndex( c ) + 1;
    return charCol + 1;
}

int impPrintS( int page, int charCol, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      charCol = impPrintC( page, charCol, s[ i ] );
    return charCol;
}

void impPrintByteNumber2( int page, int charCol, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      impPrintC( page, charCol, ' ' );
    else
      impPrintC( page, charCol, d1 + '0' );
    impPrintC( page, charCol + 1, ( b % 10 ) + '0' );
}

void impPrintByteNumber3( int page, int charCol, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        impPrintC( page, charCol, ' ' );
        if( d2 == 0 )
          impPrintC( page, charCol + 1, ' ' );
        else
          impPrintC( page, charCol + 1, d2 + '0' );
    }
    else
    {
        impPrintC( page, charCol, d1 + '0' );
        impPrintC( page, charCol + 1, d2 + '0' );
    }
    impPrintC( page, charCol + 2, ( rem % 10 ) + '0' );
}

void impPrintNumber5( int page, int charCol, int w )
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
          impPrintC( page, charCol + i, ' ' );
        else
        {
            zeroVisible = true;
            impPrintC( page, charCol + i, d + '0' );
        }
        div = div / 10;
    }
    impPrintC( page, charCol + 4, rem + '0' );
}

void impClearTextOverlay()
{
    int i, j;
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          impTextChar[ i ][ j ] = 0;
    }
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void impHideAllSprites()
{
    int i;
    for( i = 0; i < IMP_SPRITE_COUNT; i = i + 1 )
      impSprites[ i ].code = IMP_INVALID_CODE;
}

void impShowSprite( int index, int x, int y, int code )
{
    impSprites[ index ].x = x;
    impSprites[ index ].y = y;
    impSprites[ index ].code = code;
}

void impHideSprite( int index )
{
    impSprites[ index ].code = IMP_INVALID_CODE;
}

void impDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < IMP_SPRITE_COUNT; i = i + 1 )
    {
        if( impSprites[ i ].code != IMP_INVALID_CODE )
        {
            x = impSprites[ i ].x;
            y = impSprites[ i ].y;
            c = impSprites[ i ].code;
            impVVram[ y ][ x ] = c;
            if( impSprites[ i ].code >= IMP_CHAR_FIGHTER )
            {
                c = c + 1; impVVram[ y ][ x + 1 ] = c;
                c = c + 1; impVVram[ y + 1 ][ x ] = c;
                c = c + 1; impVVram[ y + 1 ][ x + 1 ] = c;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   VVram.cpp - DrawGround() rewritten as an explicit (groundRow,groundCol,
//   subRow,subCol) nested loop rather than a literal port of upstream's own
//   raw pointer walk. Provably equivalent: upstream's own `byte yPos =
//   GroundY` relies on unsigned-byte wraparound so a genuinely negative
//   logical row (GroundY is always in [-4,-1]) reads as ">= VVramHeight"
//   (i.e. "not yet visible, still scrolling in"). Since this function's own
//   real (never-wrapped) row range is provably bounded to [-4,18] (GroundY
//   in [-4,-1], plus up to 4 whole ground-rows of TileSize(4) each, plus up
//   to 3 sub-row pixels), a plain signed `destRow>=0 && destRow<16` check
//   is exactly equivalent to the wrapped one over that whole range, with no
//   masking needed at all - see header comment for the full derivation.
// -----------------------------------------------------------------------------

void impDrawGround()
{
    int groundRow, groundCol, subRow, subCol, tileIndex, destRow, destCol, tileByte;

    for( groundRow = 0; groundRow < IMP_GROUND_HEIGHT; groundRow = groundRow + 1 )
    {
        for( groundCol = 0; groundCol < IMP_GROUND_WIDTH; groundCol = groundCol + 1 )
        {
            tileIndex = impGround[ groundRow * IMP_GROUND_WIDTH + groundCol ];
            for( subRow = 0; subRow < IMP_TILE_SIZE; subRow = subRow + 1 )
            {
                destRow = impGroundY + groundRow * IMP_TILE_SIZE + subRow;
                if( destRow >= 0 && destRow < IMP_VVRAM_HEIGHT )
                {
                    for( subCol = 0; subCol < IMP_TILE_SIZE; subCol = subCol + 1 )
                    {
                        destCol = groundCol * IMP_TILE_SIZE + subCol;
                        tileByte = impTilesData[ tileIndex * 16 + subRow * IMP_TILE_SIZE + subCol ];
                        impVVram[ destRow ][ destCol ] = tileByte;
                    }
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Fort.cpp - DrawFort() only (impInitFort/impStartFort/impMoveFort/
//   impHitFort are defined much further below, once EnemyBullet/Bang/Sound/
//   impDrawAll all exist - see header comment on this file's own required
//   definition order). `bits` (upstream: computed, mutated via `bits>>=1`
//   in the skip loop, but never actually READ anywhere in DrawFort()) is
//   confirmed dead by inspection and dropped, matching this project's own
//   "drop confirmed-dead code" precedent.
// -----------------------------------------------------------------------------

void impDrawFort()
{
    int y, height, c, row, col;

    if( impFortLife == 0 ) return;

    y = impFortY - IMP_FORT_HEIGHT;
    height = IMP_FORT_HEIGHT;
    c = IMP_CHAR_FORT;
    while( y < 0 )
    {
        y = y + 1;
        height = height - 1;
        c = c + IMP_FORT_WIDTH;
    }

    for( row = 0; row < height; row = row + 1 )
    {
        for( col = 0; col < IMP_FORT_WIDTH; col = col + 1 )
          impVVram[ y + row ][ IMP_FORT_LEFT + col ] = c + row * IMP_FORT_WIDTH + col;
    }
}


// -----------------------------------------------------------------------------
//   Barrier.cpp - DrawBarriers() only (the rest of Barrier's own functions
//   are defined further below).
// -----------------------------------------------------------------------------

void impDrawBarriers()
{
    int i, col;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
    {
        if( impBarY[ i ] >= 0 && impBarY[ i ] < IMP_BAR_RANGE_Y )
        {
            impVVram[ impBarY[ i ] ][ impBarLeft[ i ] ] = IMP_CHAR_BARRIERHEAD;
            for( col = 0; col < impBarLength[ i ]; col = col + 1 )
              impVVram[ impBarY[ i ] ][ impBarLeft[ i ] + 1 + col ] = IMP_CHAR_BARRIER;
            impVVram[ impBarY[ i ] ][ impBarRight[ i ] ] = IMP_CHAR_BARRIERHEAD;
        }
    }
}


// -----------------------------------------------------------------------------
//   VVram.cpp - DrawAll(). Needs impDrawGround/impDrawFort/impDrawBarriers/
//   impDrawSpritesIntoVVram, all already defined above.
// -----------------------------------------------------------------------------

void impDrawAll()
{
    impDrawGround();
    impDrawFort();
    impDrawBarriers();
    impDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   Sound sequencer - same shape as Cracky's own crkStartSeq/
//   crkAdvanceOneSeq (see that file's own header comment); Tempo=180 here
//   gives a different note-duration multiplier (300/180=1.6667, not
//   Cracky's 1.875 for its own Tempo=160).
// -----------------------------------------------------------------------------

int impMelodyLength( int id )
{
    if( id == IMP_MELODY_FIRE ) return 13;
    if( id == IMP_MELODY_UP ) return 13;
    if( id == IMP_MELODY_START ) return 23;
    if( id == IMP_MELODY_GAMEOVER ) return 18;
    if( id == IMP_MELODY_BGM1 ) return 105;
    if( id == IMP_MELODY_BGM2 ) return 227;
    return 0;
}

int impMelodyValue( int id, int idx )
{
    if( id == IMP_MELODY_FIRE ) return impMelodyFire[ idx ];
    if( id == IMP_MELODY_UP ) return impMelodyUp[ idx ];
    if( id == IMP_MELODY_START ) return impMelodyStart[ idx ];
    if( id == IMP_MELODY_GAMEOVER ) return impMelodyGameOver[ idx ];
    if( id == IMP_MELODY_BGM1 ) return impMelodyBgm1[ idx ];
    if( id == IMP_MELODY_BGM2 ) return impMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/IMP_TEMPO = 1.6667 real 60Hz ticks - see header comment.
int impNoteFrames( int length )
{
    return (int)( length * 1.666667 + 0.5 );
}

void impStartSeq( int channel, int melodyId )
{
    impSeqMelody[ channel ] = melodyId;
    impSeqPos[ channel ] = 0;
    impSeqWait[ channel ] = 0;
    impSeqActive[ channel ] = 1;
}

void impStopSeq( int channel )
{
    impSeqActive[ channel ] = 0;
    impSeqMelody[ channel ] = IMP_MELODY_NONE;
}

bool impSeqPlaying( int channel )
{
    return impSeqActive[ channel ] != 0;
}

void impAdvanceOneSeq( int channel )
{
    int length, note;

    if( impSeqActive[ channel ] == 0 ) return;

    if( impSeqWait[ channel ] > 0 )
    {
        impSeqWait[ channel ] = impSeqWait[ channel ] - 1;
        return;
    }

    // A genuine, real upstream bug, found via live play: `Sound_GameOver()`'s
    // own `notes[]` table (unlike every other melody in this file - Fire/Up/
    // Start all end with an explicit trailing 0, StartBGM's two voices both
    // end with an explicit trailing 0xff) has NO terminating sentinel at all
    // after its last (length,note) pair - upstream's own real `notes[]` array
    // is exactly 18 bytes, ending right after a length=N4/note=0 rest pair,
    // with nothing signaling "stop" to `ToneChannel::Next()`'s own melodyOffset
    // read. On real embedded flash this silently reads whatever static data
    // happens to sit right after that specific array - harmless there, since
    // SOME nearby byte is eventually 0. Ported literally (each melody table
    // is its own fixed-size global array, immediately followed in memory by
    // the NEXT declared global - `impMelodyBgm1[105]`, then `impMelodyBgm2`,
    // then persistent game state) this becomes far more serious: with no
    // bounds check at all in `impMelodyValue()`, `impSeqPos[1]` just kept
    // growing past `impMelodyGameOver`'s own real 18 elements, reading
    // arbitrary unrelated global data as if it were melody bytes - observed
    // directly via a temporary debug hook forcing an early game over: the
    // GAME OVER jingle/screen never returned to the title screen at all,
    // stuck indefinitely (`impSeqPlaying(1)` never became false within any
    // reasonable time, since nothing guaranteed the "read" data would ever
    // contain a literal 0 length value again). Fixed with an explicit bounds
    // check against `impMelodyLength()` (already tracks each table's own
    // real element count) - reaching or passing the real end of a melody's
    // own data is always treated as "stop", the same as a literal 0 would
    // be, regardless of whether the source table actually has one. This
    // guards all 6 melodies uniformly, not just the one found broken.
    if( impSeqPos[ channel ] >= impMelodyLength( impSeqMelody[ channel ] ) )
    {
        impStopSeq( channel );
        return;
    }

    length = impMelodyValue( impSeqMelody[ channel ], impSeqPos[ channel ] );
    if( length == 0 )
    {
        impStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        impSeqPos[ channel ] = 0;
        length = impMelodyValue( impSeqMelody[ channel ], 0 );
    }
    note = impMelodyValue( impSeqMelody[ channel ], impSeqPos[ channel ] + 1 );
    impSeqPos[ channel ] = impSeqPos[ channel ] + 2;
    impSeqWait[ channel ] = impNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)impFrequencies[ note - 1 ], (float)impSeqWait[ channel ] / 60.0 );
}

void impAdvanceSound()
{
    impAdvanceOneSeq( 0 );
    impAdvanceOneSeq( 1 );
    impAdvanceOneSeq( 2 );
}

void impStartBgm()
{
    impStartSeq( 1, IMP_MELODY_BGM1 );
    impStartSeq( 2, IMP_MELODY_BGM2 );
}

void impStopBgm()
{
    // Upstream's real StopBGM() resets ALL 3 ToneChannels (a plain `for
    // (channel : ToneChannels) channel.Reset();` loop), not just the two
    // BGM voices - including channel 0, the one-shot Fire/Up SFX voice.
    // Stop that one too, so a Fire/Up blip that happens to still be
    // in-flight (mid-melody on channel 0) the instant the game ends gets
    // silenced along with the BGM, matching upstream exactly, rather than
    // being left free to keep scheduling its own remaining notes via
    // impAdvanceOneSeq(0) (still called every real frame regardless of
    // game state) straight through the game-over jingle.
    impStopSeq( 0 );
    impStopSeq( 1 );
    impStopSeq( 2 );
    md_stopTone();
}

// EffectChannel (a real decaying-noise LFSR upstream, no Vircon32
// equivalent) approximated as a short representative tone - see header
// comment for the real ~0.89s decay-time derivation and why a short blip
// was chosen instead.
void impSoundSmallBang()
{
    md_playTone( 3000.0, 0.12 );
}

void impSoundLargeBang()
{
    md_playTone( 1500.0, 0.12 );
}

void impSoundFire()
{
    impStartSeq( 0, IMP_MELODY_FIRE );
}

void impSoundUp()
{
    impStartSeq( 0, IMP_MELODY_UP );
}


// -----------------------------------------------------------------------------
//   Status.cpp (print helpers) + Main.cpp's AddScore()
// -----------------------------------------------------------------------------

void impPrintScore()
{
    impPrintNumber5( 1, 26, impScore );
    impPrintC( 1, 31, '0' );
}

void impPrintStage()
{
    impPrintByteNumber2( 3, 30, impCurrentStage + 1 );
}

void impPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int i;

    impPrintS( 0, 24, sScore, 5 );
    impPrintS( 3, 24, sStage, 5 );

    if( impRemainCount > 1 )
    {
        i = impRemainCount - 1;
        if( i > 2 )
        {
            // upstream draws a real 2x2 Char_Remain (fighter) icon here via
            // Put2C(), then a space, then the remaining digit - simplified
            // to plain blanks + a digit, matching Cracky's own established
            // precedent for this exact situation (see header comment).
            impPrintC( 7, 24, ' ' );
            impPrintC( 7, 25, ' ' );
            impPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < impRemainCount - 1; i = i + 1 )
              impPrintC( 7, 24 + i, ' ' );
        }
    }

    impPrintScore();
    impPrintStage();
}

void impPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    impPrintS( 4, 8, s, 9 );
}

void impAddScore( int pts )
{
    impScore = impScore + pts;
    impPrintScore();
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void impInitBangs()
{
    int i;
    for( i = 0; i < IMP_BANG_COUNT; i = i + 1 )
      impBangStatus[ i ] = IMP_BANG_STATUS_NONE;
}

void impStartBang( int x, int y, bool large )
{
    int i;
    for( i = 0; i < IMP_BANG_COUNT; i = i + 1 )
    {
        if( ( impBangStatus[ i ] & 0xf0 ) != IMP_BANG_STATUS_NONE ) continue;
        impBangX[ i ] = x;
        impBangY[ i ] = y;
        if( large )
          impBangStatus[ i ] = IMP_BANG_STATUS_LARGE_SMALL;
        else
          impBangStatus[ i ] = IMP_BANG_STATUS_SMALL;
        return;
    }
}

int impBangShowOne( int x, int y, int sprite, int pattern, int nextSprite )
{
    if( sprite < nextSprite )
    {
        x = x - 1;
        y = y - 1;
        if( x >= 0 && x < IMP_BANG_RANGE_X && y >= 0 && y < IMP_BANG_RANGE_Y )
        {
            impShowSprite( sprite, x, y, pattern );
            return sprite + 1;
        }
    }
    return sprite;
}

void impUpdateBangs()
{
    int sprite, i, mode, count;
    sprite = IMP_SPRITE_BANG;
    for( i = 0; i < IMP_BANG_COUNT; i = i + 1 )
    {
        mode = impBangStatus[ i ] & 0xf0;
        if( mode == IMP_BANG_STATUS_NONE ) continue;
        count = impBangStatus[ i ] & 0x0f;

        if( mode == IMP_BANG_STATUS_LARGE_LARGE )
        {
            sprite = impBangShowOne( impBangX[ i ] - 1, impBangY[ i ] - 1, sprite, IMP_CHAR_BANG + 4, IMP_SPRITE_COUNT );
            sprite = impBangShowOne( impBangX[ i ] + 1, impBangY[ i ] - 1, sprite, IMP_CHAR_BANG + 8, IMP_SPRITE_COUNT );
            sprite = impBangShowOne( impBangX[ i ] - 1, impBangY[ i ] + 1, sprite, IMP_CHAR_BANG + 12, IMP_SPRITE_COUNT );
            sprite = impBangShowOne( impBangX[ i ] + 1, impBangY[ i ] + 1, sprite, IMP_CHAR_BANG + 16, IMP_SPRITE_COUNT );
        }
        else
          sprite = impBangShowOne( impBangX[ i ], impBangY[ i ], sprite, IMP_CHAR_BANG, IMP_SPRITE_COUNT );

        // `++count; if (count>=1)` is unconditionally true here (count
        // always starts at 0) - the upstream `else` branch is dead code,
        // preserved faithfully rather than "fixed" - see header comment.
        count = count + 1;
        if( count >= 1 )
        {
            if( mode == IMP_BANG_STATUS_LARGE_SMALL )
              impBangStatus[ i ] = IMP_BANG_STATUS_LARGE_LARGE;
            else
              impBangStatus[ i ] = IMP_BANG_STATUS_NONE;
        }
        else
          impBangStatus[ i ] = mode | count;
    }
    while( sprite < IMP_SPRITE_COUNT )
    {
        impHideSprite( sprite );
        sprite = sprite + 1;
    }
}


// -----------------------------------------------------------------------------
//   Barrier.cpp (remaining functions)
// -----------------------------------------------------------------------------

void impInitBarriers()
{
    int i;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
      impBarY[ i ] = -1;
}

void impStartBarrier()
{
    int i, x, length;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
    {
        if( impBarY[ i ] < 0 || impBarY[ i ] >= IMP_BAR_RANGE_Y )
        {
            x = ( impRnd() & 0x0f ) << 1;
            length = impCurrentStage + 3;
            if( length > IMP_BAR_MAX_LENGTH )
              length = IMP_BAR_MAX_LENGTH;
            if( x + length >= IMP_VVRAM_HEIGHT - 1 ) return;
            impBarLeft[ i ] = x;
            impBarRight[ i ] = impBarLeft[ i ] + 1 + length;
            impBarY[ i ] = 0;
            impBarLength[ i ] = length;
            return;
        }
    }
}

void impMoveBarriers()
{
    int i;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
    {
        if( impBarY[ i ] < 0 || impBarY[ i ] >= IMP_BAR_RANGE_Y ) continue;
        impBarY[ i ] = impBarY[ i ] + 1;
    }
}

void impBarrierDestroy( int i, int x )
{
    impSoundSmallBang();
    impStartBang( x, impBarY[ i ], false );
    impBarY[ i ] = -1;
    impAddScore( impBarLength[ i ] );
}

bool impHitBulletBarrier( int x, int y )
{
    int i, leftRight;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
    {
        if( impBarY[ i ] < 0 || impBarY[ i ] >= IMP_BAR_RANGE_Y ) continue;

        if( y + 1 >= impBarY[ i ] && y < impBarY[ i ] + 1 )
        {
            leftRight = impBarLeft[ i ] + 1;
            if( x + 1 >= impBarLeft[ i ] && x < leftRight )
            {
                impBarrierDestroy( i, impBarLeft[ i ] );
                return true;
            }
            if( x + 1 >= impBarRight[ i ] && x < impBarRight[ i ] + 1 )
            {
                impBarrierDestroy( i, impBarRight[ i ] );
                return true;
            }
            if( x + 1 >= leftRight && x < impBarRight[ i ] )
              return true;
        }
    }
    return false;
}

bool impHitFighterBarrier()
{
    int i, leftRight, fighterRight;
    for( i = 0; i < IMP_BAR_COUNT; i = i + 1 )
    {
        if( impBarY[ i ] < 0 || impBarY[ i ] >= IMP_BAR_RANGE_Y ) continue;

        if( impFighterY + 1 >= impBarY[ i ] && impFighterY - 1 < impBarY[ i ] )
        {
            leftRight = impBarLeft[ i ] + 1;
            fighterRight = impFighterX + 2;
            if( impBarLeft[ i ] < fighterRight && impFighterX < leftRight )
            {
                impBarrierDestroy( i, impBarLeft[ i ] );
                return true;
            }
            if( impBarRight[ i ] < fighterRight && impFighterX < impBarRight[ i ] + 1 )
            {
                impBarrierDestroy( i, impBarRight[ i ] );
                return true;
            }
            if( leftRight < fighterRight && impFighterX < impBarRight[ i ] )
              return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Fighter.cpp (predicate/init functions only - impMoveFighter is defined
//   much further below, once FighterBullet exists - see header comment).
// -----------------------------------------------------------------------------

void impFighterShow()
{
    impShowSprite( IMP_SPRITE_FIGHTER, impFighterX, impFighterY, IMP_CHAR_FIGHTER );
}

void impFighterHide()
{
    impHideSprite( IMP_SPRITE_FIGHTER );
}

void impInitFighter()
{
    impFighterX = IMP_FIGHTER_INITIAL_X;
    impFighterY = IMP_FIGHTER_INITIAL_Y;
    impFighterShow();
    impFighterCrashCount = 0;
    impFighterReviveCount = 0;
}

void impFighterCrash()
{
    impFighterHide();
    impSoundLargeBang();
    impStartBang( impFighterX + 1, impFighterY + 1, true );
    impFighterCrashCount = 1;
    impPrintStatus();
}

bool impHitBulletFighter( int x, int y )
{
    if(
        impFighterCrashCount == 0 && impFighterReviveCount == 0 &&
        x >= impFighterX && x < impFighterX + 2 &&
        y >= impFighterY && y < impFighterY + 2
    )
    {
        impFighterCrash();
        return true;
    }
    return false;
}

bool impHitEnemyFighter( int x, int y )
{
    if(
        impFighterCrashCount == 0 && impFighterReviveCount == 0 &&
        x + 1 >= impFighterX && x < impFighterX + 2 &&
        y + 1 >= impFighterY && y < impFighterY + 2
    )
    {
        impFighterCrash();
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

void impInitItem()
{
    impItemY = -1;
}

void impItemShow()
{
    impShowSprite( IMP_SPRITE_ITEM, impItemX, impItemY, IMP_CHAR_ITEM );
}

void impItemHide()
{
    impHideSprite( IMP_SPRITE_ITEM );
    impItemY = -1;
}

void impStartItem( int x, int y )
{
    if( impItemY >= 0 && impItemY < IMP_ITEM_RANGE_Y ) return;
    impItemX = x;
    impItemY = y;
    impItemShow();
}

void impMoveItem()
{
    if( impItemY >= 0 && impItemY < IMP_ITEM_RANGE_Y )
    {
        impItemY = impItemY + 1;
        if( impItemY >= IMP_ITEM_RANGE_Y )
        {
            impItemHide();
            return;
        }
        if(
            impItemX + 1 >= impFighterX && impItemX < impFighterX + 2 &&
            impItemY + 1 >= impFighterY && impItemY < impFighterY + 2
        )
        {
            impItemHide();
            impSoundUp();
            impRemainCount = impRemainCount + 1;
            impPrintStatus();
            return;
        }
        impItemShow();
    }
}


// -----------------------------------------------------------------------------
//   EnemyBullet.cpp - needs impHitBulletFighter, already defined above.
// -----------------------------------------------------------------------------

void impInitEnemyBullets()
{
    int i;
    for( i = 0; i < IMP_EB_COUNT; i = i + 1 )
      impEbY[ i ] = -1;
}

void impEbShow( int i )
{
    impShowSprite( IMP_SPRITE_ENEMYBULLET + i, impEbX[ i ], impEbY[ i ], IMP_CHAR_ENEMYBULLET );
}

bool impStartEnemyBullet( int x, int y )
{
    int i, lx, ly;
    for( i = 0; i < IMP_EB_COUNT; i = i + 1 )
    {
        if( impEbY[ i ] >= 0 && impEbY[ i ] < IMP_EB_RANGE_Y ) continue;

        impEbDx[ i ] = impSign( x, impFighterX );
        impEbDy[ i ] = impSign( y, impFighterY );
        if( impEbDx[ i ] != 0 && impEbDy[ i ] != 0 )
        {
            lx = impAbs( x, impFighterX );
            ly = impAbs( y, impFighterY );
            if( lx < ly )
            {
                impEbNumX[ i ] = IMP_EB_SHORTVEL;
                impEbNumY[ i ] = IMP_EB_LONGVEL;
            }
            else if( lx > ly )
            {
                impEbNumX[ i ] = IMP_EB_LONGVEL;
                impEbNumY[ i ] = IMP_EB_SHORTVEL;
            }
            else
            {
                impEbNumX[ i ] = IMP_EB_LOVEL;
                impEbNumY[ i ] = IMP_EB_LOVEL;
            }
        }
        else
        {
            impEbNumX[ i ] = IMP_EB_HIVEL;
            impEbNumY[ i ] = IMP_EB_HIVEL;
        }
        impEbX[ i ] = x;
        impEbY[ i ] = y;
        impEbDenX[ i ] = 0;
        impEbDenY[ i ] = 0;
        impEbShow( i );
        return true;
    }
    return false;
}

void impMoveEnemyBullets()
{
    int i;
    for( i = 0; i < IMP_EB_COUNT; i = i + 1 )
    {
        if( impEbY[ i ] < 0 || impEbY[ i ] >= IMP_EB_RANGE_Y ) continue;

        impEbDenX[ i ] = impEbDenX[ i ] - impEbNumX[ i ];
        if( impEbDenX[ i ] < 0 )
        {
            impEbX[ i ] = impEbX[ i ] + impEbDx[ i ];
            impEbDenX[ i ] = impEbDenX[ i ] + IMP_EB_HIVEL;
        }
        impEbDenY[ i ] = impEbDenY[ i ] - impEbNumY[ i ];
        if( impEbDenY[ i ] < 0 )
        {
            impEbY[ i ] = impEbY[ i ] + impEbDy[ i ];
            impEbDenY[ i ] = impEbDenY[ i ] + IMP_EB_HIVEL;
        }
        if(
            impEbX[ i ] < 0 || impEbX[ i ] >= IMP_EB_RANGE_X ||
            impEbY[ i ] < 0 || impEbY[ i ] >= IMP_EB_RANGE_Y ||
            impHitBulletFighter( impEbX[ i ], impEbY[ i ] )
        )
        {
            // Real bug, found via a direct user report ("is it normal the
            // bullet remains on screen displayed when it normally is about
            // to go off screen"), confirmed against upstream's own
            // MoveEnemyBullets() (EnemyBullet.cpp): the off-screen/hit
            // branch there calls BOTH HideSprite(pBullet->sprite) AND
            // pBullet->y = InvalidY - this port had only ever ported the
            // second half (marking the slot free for reuse via
            // impEbY[i]=-1), never the first. impEbY[i]<0 already makes
            // impMoveEnemyBullets() skip this slot on every future tick
            // (so the LOGIC correctly stops treating it as an active
            // bullet), but nothing ever told the display layer to stop
            // drawing its last-shown sprite - so the bullet's final
            // on-screen frame (right at the edge, or at the fighter's own
            // position on a hit) just stayed visible indefinitely instead
            // of disappearing, exactly matching the reported symptom.
            impHideSprite( IMP_SPRITE_ENEMYBULLET + i );
            impEbY[ i ] = -1;
        }
        else
          impEbShow( i );
    }
}


// -----------------------------------------------------------------------------
//   Barrier.cpp's own impStartBarrier is already defined above (needed by
//   SkyEnemy's own impStartSkyEnemy below).
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//   SkyEnemy.cpp - needs impStartEnemyBullet, impStartBarrier, impStartItem
//   (all already defined above).
// -----------------------------------------------------------------------------

void impSkyShow( int i )
{
    impShowSprite( IMP_SPRITE_SKYENEMY + i, impSkyX[ i ], impSkyY[ i ], impSkyPattern[ i ] );
}

void impSkyEnd( int i )
{
    impSkyY[ i ] = -1;
    impHideSprite( IMP_SPRITE_SKYENEMY + i );
}

void impDecideDirectionSky( int i )
{
    int dx, direction, diff, x, y;

    if( impSkyType[ i ] == IMP_TYPE_CRASH )
    {
        dx = impSign( impSkyX[ i ], impFighterX );
        if( impSkyBulletCount[ i ] == 0 )
        {
            if( dx != 0 ) dx = -dx;
            else dx = -1;
        }
        impSkyDx[ i ] = dx;
        impSkyDy[ i ] = 1;
    }
    else if( impSkyType[ i ] == IMP_TYPE_SMART )
    {
        if( impFighterX < impSkyX[ i ] )
        {
            if( impFighterY < impSkyY[ i ] ) direction = IMP_DIR_UPLEFT;
            else if( impFighterY == impSkyY[ i ] ) direction = IMP_DIR_LEFT;
            else direction = IMP_DIR_DOWNLEFT;
        }
        else if( impFighterX == impSkyX[ i ] )
        {
            if( impFighterY < impSkyY[ i ] ) direction = IMP_DIR_UP;
            else direction = IMP_DIR_DOWN;
        }
        else
        {
            if( impFighterY < impSkyY[ i ] ) direction = IMP_DIR_UPRIGHT;
            else if( impFighterY == impSkyY[ i ] ) direction = IMP_DIR_RIGHT;
            else direction = IMP_DIR_DOWNRIGHT;
        }
        if( impSkyBulletCount[ i ] == 0 )
          direction = ( direction + 4 ) & 7;
        if( direction != impSkyDirection[ i ] )
        {
            diff = ( direction - impSkyDirection[ i ] ) & 7;
            if( diff <= 4 )
              impSkyDirection[ i ] = ( impSkyDirection[ i ] + 1 ) & 7;
            else
              impSkyDirection[ i ] = ( impSkyDirection[ i ] - 1 ) & 7;
        }
        impSkyPattern[ i ] = IMP_CHAR_SKYENEMY + 4 + ( impSkyDirection[ i ] << 2 );
        impSkyDx[ i ] = impDirectionDx[ impSkyDirection[ i ] ];
        impSkyDy[ i ] = impDirectionDy[ impSkyDirection[ i ] ];
    }
    else if( impSkyType[ i ] == IMP_TYPE_INSISTENT )
    {
        x = ( impRnd() & 0x0f ) << 1;
        y = ( impRnd() & 0x0f );
        impSkyDy[ i ] = impSign( impSkyY[ i ], y );
        impSkyDx[ i ] = impSign( impSkyX[ i ], x );
    }
}

void impInitSkyEnemies()
{
    int i;
    for( i = 0; i < IMP_SKY_COUNT; i = i + 1 )
      impSkyY[ i ] = -1;
    impSkyNextType = 0;
    impSkyTypeBit = 1;
}

void impSkyDestroy( int i )
{
    impSoundSmallBang();
    impStartBang( impSkyX[ i ] + 1, impSkyY[ i ] + 1, false );
    impSkyEnd( i );
    impAddScore( impSkyPoints[ impSkyType[ i ] ] );
}

void impStartSkyEnemy()
{
    int x, n, i;

    if( impRnd() >= impCurrentStage + 3 ) return;
    x = ( impRnd() & 0x0f ) << 1;
    if( x >= IMP_SKY_RANGE_X ) return;

    for( n = 0; n < IMP_TYPE_COUNT; n = n + 1 )
    {
        impSkyTypeBit = impSkyTypeBit << 1;
        impSkyNextType = impSkyNextType + 1;
        if( impSkyNextType >= IMP_TYPE_COUNT )
        {
            impSkyNextType = 0;
            impSkyTypeBit = 1;
        }
        if( ( impSkyTypeBit & impSkyElementBits ) != 0 ) break;
    }

    if( impSkyNextType == IMP_TYPE_BARRIER )
    {
        impStartBarrier();
        return;
    }

    for( i = 0; i < IMP_SKY_COUNT; i = i + 1 )
    {
        if( impSkyY[ i ] < 0 || impSkyY[ i ] >= IMP_SKY_RANGE_Y )
        {
            impSkyX[ i ] = x;
            impSkyY[ i ] = 0;
            impSkyType[ i ] = impSkyNextType;
            impSkyPattern[ i ] = impSkyPatterns[ impSkyNextType ];
            impSkyBulletCount[ i ] = impCurrentStage + 1;
            impSkyDirection[ i ] = IMP_DIR_DOWN;
            impSkyClock[ i ] = 0;
            impDecideDirectionSky( i );
            impSkyShow( i );
            return;
        }
    }
}

void impMoveSkyEnemies()
{
    int i;
    for( i = 0; i < IMP_SKY_COUNT; i = i + 1 )
    {
        if( impSkyY[ i ] < 0 || impSkyY[ i ] >= IMP_SKY_RANGE_Y ) continue;

        impSkyClock[ i ] = impSkyClock[ i ] + 1;
        if(
            ( impSkyClock[ i ] & IMP_SKY_FIRE_MASK ) == 0 &&
            ( impSkyType[ i ] == IMP_TYPE_INSISTENT || impSkyBulletCount[ i ] > 0 ) &&
            ( impRnd() << 1 ) < impCurrentStage + 2
        )
        {
            if( impStartEnemyBullet( impSkyX[ i ], impSkyY[ i ] ) )
              impSkyBulletCount[ i ] = impSkyBulletCount[ i ] - 1;
        }

        if( impSkyType[ i ] == IMP_TYPE_CRASH )
        {
            if( ( impSkyY[ i ] & 3 ) == 0 )
            {
                impSkyX[ i ] = impSkyX[ i ] + impSkyDx[ i ];
                impDecideDirectionSky( i );
            }
            impSkyY[ i ] = impSkyY[ i ] + 1;
        }
        else if( impSkyType[ i ] == IMP_TYPE_SMART || impSkyType[ i ] == IMP_TYPE_INSISTENT )
        {
            if( ( impSkyClock[ i ] & IMP_SKY_TURN_MASK ) == 0 )
              impDecideDirectionSky( i );
            impSkyX[ i ] = impSkyX[ i ] + impSkyDx[ i ];
            impSkyY[ i ] = impSkyY[ i ] + impSkyDy[ i ];
        }

        if(
            impSkyX[ i ] < 0 || impSkyX[ i ] >= IMP_SKY_RANGE_X ||
            impSkyY[ i ] < 0 || impSkyY[ i ] >= IMP_SKY_RANGE_Y ||
            impHitEnemyFighter( impSkyX[ i ], impSkyY[ i ] )
        )
          impSkyEnd( i );
        else
          impSkyShow( i );
    }
}

bool impHitBulletSkyEnemy( int x, int y )
{
    int i;
    for( i = 0; i < IMP_SKY_COUNT; i = i + 1 )
    {
        if( impSkyY[ i ] < 0 || impSkyY[ i ] >= IMP_SKY_RANGE_Y ) continue;
        if(
            x + 1 >= impSkyX[ i ] && x < impSkyX[ i ] + 2 &&
            y + 1 >= impSkyY[ i ] && y < impSkyY[ i ] + 2
        )
        {
            if(
                y < IMP_VVRAM_HEIGHT / 2 + 1 &&
                impRemainCount < 10 &&
                impRnd() == 0
            )
              impStartItem( impSkyX[ i ], impSkyY[ i ] );
            impSkyDestroy( i );
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   GroundEnemy.cpp - needs impStartEnemyBullet (already defined above).
// -----------------------------------------------------------------------------

void impGeShow( int i )
{
    impShowSprite( IMP_SPRITE_GROUNDENEMY + i, impGeX[ i ], impGeY[ i ], ( impGeType[ i ] << 2 ) + IMP_CHAR_GROUNDENEMY );
}

void impGeEnd( int i )
{
    impGeY[ i ] = -1;
    impHideSprite( IMP_SPRITE_GROUNDENEMY + i );
}

void impInitGroundEnemies()
{
    int i;
    for( i = 0; i < IMP_GE_COUNT; i = i + 1 )
      impGeY[ i ] = -1;
}

void impGeDestroy( int i )
{
    impSoundSmallBang();
    impStartBang( impGeX[ i ] + 1, impGeY[ i ] + 1, false );
    impGeEnd( i );
    impAddScore( impGePoints[ impGeType[ i ] ] );
}

void impStartGroundEnemy()
{
    int i, cursor, slot;
    while( impGroundElementRemaining > 0 && impGroundElemY[ impGroundElementCursor ] == impTopRow )
    {
        cursor = impGroundElementCursor;
        for( i = 0; i < IMP_GE_COUNT; i = i + 1 )
        {
            if( impGeY[ i ] < 0 || impGeY[ i ] >= IMP_GE_RANGE_Y )
            {
                slot = i;
                impGeX[ slot ] = impGroundElemX[ cursor ];
                // provably 0 given the while-loop's own equality condition
                // above (impGroundElemY[cursor]==impTopRow) - kept as a
                // literal subtraction to mirror upstream's own expression.
                impGeY[ slot ] = impGroundElemY[ cursor ] - impTopRow;
                if( impGroundElemRC[ cursor ] > 0 )
                {
                    impGeType[ slot ] = IMP_GE_TYPE_MOVABLE;
                    impGeRouteTotal[ slot ] = impGroundElemRC[ cursor ];
                    impGeRouteBase[ slot ] = impGroundElemRS[ cursor ];
                    impGeRouteIndex[ slot ] = 0;
                    impGeDx[ slot ] = impRouteDx[ impGeRouteBase[ slot ] ];
                    impGeDy[ slot ] = impRouteDy[ impGeRouteBase[ slot ] ];
                    impGeMoveCount[ slot ] = impRouteCount[ impGeRouteBase[ slot ] ];
                }
                else
                  impGeType[ slot ] = IMP_GE_TYPE_FIXED;
                impGeClock[ slot ] = 0;
                impGeShow( slot );
                break;
            }
        }
        impGroundElementCursor = impGroundElementCursor + 1;
        impGroundElementRemaining = impGroundElementRemaining - 1;
    }
}

void impScrollGroundEnemy()
{
    int i;
    for( i = 0; i < IMP_GE_COUNT; i = i + 1 )
    {
        if( impGeY[ i ] < 0 || impGeY[ i ] >= IMP_GE_RANGE_Y ) continue;
        impGeY[ i ] = impGeY[ i ] + 1;
        if( impGeY[ i ] >= IMP_GE_RANGE_Y )
          impGeEnd( i );
        else
          impGeShow( i );
    }
}

void impMoveGroundEnemy()
{
    int i, idx;
    for( i = 0; i < IMP_GE_COUNT; i = i + 1 )
    {
        if( impGeY[ i ] < 0 || impGeY[ i ] >= IMP_GE_RANGE_Y ) continue;

        impGeClock[ i ] = impGeClock[ i ] + 1;
        if( ( impRnd() << 1 ) < impCurrentStage + 1 )
          impStartEnemyBullet( impGeX[ i ], impGeY[ i ] );

        if( impGeType[ i ] == IMP_GE_TYPE_FIXED ) continue;

        impGeX[ i ] = impGeX[ i ] + impGeDx[ i ];
        impGeY[ i ] = impGeY[ i ] + impGeDy[ i ];
        if( impGeX[ i ] < 0 || impGeX[ i ] >= IMP_GE_RANGE_X || impGeY[ i ] < 0 || impGeY[ i ] >= IMP_GE_RANGE_Y )
        {
            impGeEnd( i );
            continue;
        }
        impGeShow( i );

        impGeMoveCount[ i ] = impGeMoveCount[ i ] - 1;
        if( impGeMoveCount[ i ] == 0 )
        {
            // upstream: `++pEnemy->pRoute` with no bound check at all,
            // relying on `routeCount` (never actually decremented - a
            // real, dead-code upstream bug) to stop it - a genuine out-
            // of-bounds array read once the route's real last leg
            // finishes. Fixed with an explicit clamp instead of
            // reproducing the read - see header comment.
            if( impGeRouteIndex[ i ] < impGeRouteTotal[ i ] - 1 )
              impGeRouteIndex[ i ] = impGeRouteIndex[ i ] + 1;
            idx = impGeRouteBase[ i ] + impGeRouteIndex[ i ];
            impGeDx[ i ] = impRouteDx[ idx ];
            impGeDy[ i ] = impRouteDy[ idx ];
            impGeMoveCount[ i ] = impRouteCount[ idx ];
        }
    }
}

bool impHitBulletGroundEnemy( int x, int y )
{
    int i;
    for( i = 0; i < IMP_GE_COUNT; i = i + 1 )
    {
        if( impGeY[ i ] < 0 || impGeY[ i ] >= IMP_GE_RANGE_Y ) continue;
        if(
            x + 1 >= impGeX[ i ] && x < impGeX[ i ] + 2 &&
            y + 1 >= impGeY[ i ] && y < impGeY[ i ] + 2
        )
        {
            impGeDestroy( i );
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Fort.cpp (remaining functions) - needs impStartEnemyBullet (above) and
//   impDrawAll (already defined earlier in the file).
// -----------------------------------------------------------------------------

void impInitFort()
{
    impFortLife = 0;
}

void impStartFort()
{
    impFortY = 1;
    impFortLife = IMP_FORT_MAX_LIFE;
    impFortBulletX = IMP_FORT_LEFT;
}

void impMoveFort()
{
    int y, n;
    if( impFortLife != 0 )
    {
        if( impFortY < IMP_FORT_MAX_Y )
          impFortY = impFortY + 1;
        y = impFortY - 1;
        for( n = 0; n < IMP_FORT_HEIGHT / 2 - 1; n = n + 1 )
        {
            impFortBulletX = impFortBulletX + 1;
            if( impFortBulletX >= IMP_FORT_LEFT + IMP_FORT_WIDTH )
              impFortBulletX = IMP_FORT_LEFT;
            if( impRnd() < impCurrentStage + 3 )
              impStartEnemyBullet( impFortBulletX, y );
            y = y - 1;
        }
    }
}

bool impHitFort( int x, int y )
{
    if( impFortLife == 0 ) return false;
    if( x + 1 >= IMP_FORT_LEFT && x < IMP_FORT_LEFT + IMP_FORT_WIDTH && y < impFortY )
    {
        impFortLife = impFortLife - 1;
        if( impFortLife == 0 )
        {
            impSoundLargeBang();
            impStartBang( IMP_FORT_LEFT + IMP_FORT_WIDTH / 2, impFortY - IMP_FORT_HEIGHT / 2, true );
            impAddScore( IMP_FORT_POINT );
            impDrawAll();
        }
        else
        {
            impSoundSmallBang();
            impStartBang( x + 1, y + 1, false );
        }
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   FighterBullet.cpp - needs impHitBulletSkyEnemy/impHitBulletGroundEnemy/
//   impHitBulletBarrier/impHitFort, all already defined above.
// -----------------------------------------------------------------------------

void impInitFighterBullets()
{
    int i;
    for( i = 0; i < IMP_FB_COUNT; i = i + 1 )
      impFbY[ i ] = -1;
    impFbIntervalCount = IMP_FB_SHORT_INTERVAL;
}

void impFbShow( int i )
{
    impShowSprite( IMP_SPRITE_FIGHTERBULLET + i, impFbX[ i ], impFbY[ i ], IMP_CHAR_FIGHTERBULLET );
}

void impFbEnd( int i )
{
    impFbY[ i ] = -1;
    impHideSprite( IMP_SPRITE_FIGHTERBULLET + i );
}

void impStartFighterBullet( bool on )
{
    int i;
    if( impFbIntervalCount != 0 )
      impFbIntervalCount = impFbIntervalCount - 1;
    if( !on )
    {
        if( impFbIntervalCount > IMP_FB_SHORT_INTERVAL )
          impFbIntervalCount = IMP_FB_SHORT_INTERVAL;
        return;
    }
    if( impFbIntervalCount != 0 ) return;
    for( i = 0; i < IMP_FB_COUNT; i = i + 1 )
    {
        if( impFbY[ i ] >= 0 && impFbY[ i ] < IMP_FB_RANGE_Y ) continue;
        impSoundFire();
        impFbX[ i ] = impFighterX;
        impFbY[ i ] = impFighterY;
        impFbShow( i );
        impFbIntervalCount = IMP_FB_LONG_INTERVAL;
        return;
    }
}

bool impFbHit( int i )
{
    if( impHitBulletSkyEnemy( impFbX[ i ], impFbY[ i ] ) ) return true;
    if( impHitBulletGroundEnemy( impFbX[ i ], impFbY[ i ] ) ) return true;
    if( impHitBulletBarrier( impFbX[ i ], impFbY[ i ] ) ) return true;
    if( impHitFort( impFbX[ i ], impFbY[ i ] ) ) return true;
    return false;
}

void impMoveFighterBullets()
{
    int i;
    for( i = 0; i < IMP_FB_COUNT; i = i + 1 )
    {
        if( impFbY[ i ] < 0 || impFbY[ i ] >= IMP_FB_RANGE_Y ) continue;
        impFbY[ i ] = impFbY[ i ] - 1;
        if( impFbY[ i ] < 0 || impFbY[ i ] >= IMP_FB_RANGE_Y || impFbHit( i ) )
          impFbEnd( i );
        else
          impFbShow( i );
    }
}


// -----------------------------------------------------------------------------
//   Fighter.cpp's own MoveFighter() - needs impStartFighterBullet (above)
//   and impHitFighterBarrier/impFighterCrash (both already defined earlier
//   in the file). This is the closing function of the whole module cycle
//   described in the header comment.
// -----------------------------------------------------------------------------

void impMoveFighter()
{
    bool left, right, up, down, fire;

    if( impFighterCrashCount >= 1 )
    {
        impFighterCrashCount = impFighterCrashCount + 1;
        if( impFighterCrashCount >= IMP_FIGHTER_CRASH_TIME )
        {
            impRemainCount = impRemainCount - 1;
            impFighterX = IMP_FIGHTER_INITIAL_X;
            impFighterY = IMP_FIGHTER_INITIAL_Y;
            impFighterCrashCount = 0;
            impFighterReviveCount = IMP_FIGHTER_REVIVE_TIME;
            impPrintStatus();
        }
        return;
    }

    left = isLeftPressed();
    right = isRightPressed();
    up = isUpPressed();
    down = isDownPressed();
    fire = isFirePressed();

    if( left && impFighterX > 0 )
      impFighterX = impFighterX - 1;
    if( right && impFighterX < IMP_VVRAM_WIDTH - 2 )
      impFighterX = impFighterX + 1;
    if( up && impFighterY > 2 )
      impFighterY = impFighterY - 1;
    if( down && impFighterY < IMP_VVRAM_HEIGHT - 2 )
      impFighterY = impFighterY + 1;

    if( impFighterReviveCount > 0 )
    {
        impFighterReviveCount = impFighterReviveCount - 1;
        if( ( impFighterReviveCount & 1 ) != 0 )
          impFighterShow();
        else
          impFighterHide();
    }
    else
    {
        if( impHitFighterBarrier() )
          impFighterCrash();
        else
          impFighterShow();
    }

    impStartFighterBullet( fire );
}


// -----------------------------------------------------------------------------
//   Stage.cpp - needs every Init*/Start* function above (impInitFighter,
//   impInitFighterBullets, impInitSkyEnemies, impInitBarriers,
//   impInitGroundEnemies, impInitFort, impInitEnemyBullets, impInitBangs,
//   impInitItem, impScrollGroundEnemy, impStartFort, impStartSkyEnemy,
//   impStartGroundEnemy), all already defined above.
// -----------------------------------------------------------------------------

void impFillTiles()
{
    int i;
    for( i = 0; i < IMP_GROUND_WIDTH; i = i + 1 )
      impGround[ i ] = impMapBytes[ impPMapIndex + i ];
    impGroundY = -IMP_TILE_SIZE;
}

void impRollDown()
{
    int row, col;
    for( row = IMP_GROUND_HEIGHT - 1; row > 0; row = row - 1 )
    {
        for( col = 0; col < IMP_GROUND_WIDTH; col = col + 1 )
          impGround[ row * IMP_GROUND_WIDTH + col ] = impGround[ ( row - 1 ) * IMP_GROUND_WIDTH + col ];
    }
}

void impInitStage()
{
    int i, j;
    // upstream cycles through Stages[] repeatedly past CurrentStage, the
    // same wrap loop already established in Cracky's own crkInitStage().
    i = 0; j = 0;
    while( i < impCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= IMP_STAGE_COUNT )
          j = 0;
    }
    impStageIndex = j;

    impPMapIndex = ( IMP_MAP_HEIGHT - 1 ) * IMP_MAP_WIDTH + impStageMapOffset[ impStageIndex ];
    impTopRow = IMP_MAP_HEIGHT * IMP_TILE_SIZE;
    impSkyElementCursor = impStageSkyStart[ impStageIndex ];
    impSkyElementRemaining = impStageSkyCount[ impStageIndex ];
    impGroundElementCursor = impStageGroundStart[ impStageIndex ];
    impGroundElementRemaining = impStageGroundCount[ impStageIndex ];

    impFillTiles();
}

void impInitTrying()
{
    int i;
    impSkyElementBits = 0;
    impRndIndex = 0;

    impHideAllSprites();
    impInitFighter();
    impInitFighterBullets();
    impInitSkyEnemies();
    impInitBarriers();
    impInitGroundEnemies();
    impInitFort();
    impInitEnemyBullets();
    impInitBangs();
    impInitItem();

    for( i = IMP_GROUND_WIDTH; i < IMP_GROUND_WIDTH * IMP_GROUND_HEIGHT; i = i + 1 )
      impGround[ i ] = 1;
}

void impScrollGround()
{
    if( impFortLife != 0 ) return;

    impScrollGroundEnemy();

    impGroundY = impGroundY + 1;
    impTopRow = impTopRow - 1;
    if( impTopRow == IMP_FORT_START_Y && impFortLife == 0 )
      impStartFort();
    if( impTopRow == 0 )
    {
        impCurrentStage = impCurrentStage + 1;
        impRollDown();
        impInitStage();
        impPrintStage();
    }
    else if( ( impTopRow & 3 ) == 0 )
    {
        impPMapIndex = impPMapIndex - IMP_MAP_WIDTH;
        impRollDown();
        impFillTiles();
    }
    if( impSkyElementRemaining > 0 && impSkyElemRow[ impSkyElementCursor ] >= impTopRow )
    {
        impSkyElementBits = impSkyElemBitsTbl[ impSkyElementCursor ];
        impSkyElementCursor = impSkyElementCursor + 1;
        impSkyElementRemaining = impSkyElementRemaining - 1;
    }
    impStartSkyEnemy();
    impStartGroundEnemy();
}


// -----------------------------------------------------------------------------
//   Vram.cpp - impComposeMapByte() reproduces VVramToVram()'s own SendUL()
//   nibble-interleaving exactly, the same formula already proven in
//   Cracky's own crkComposeRawByte() (byte-diff-confirmed identical
//   Vram.cpp source before reuse - see header comment). No hardware
//   orientation transform - drawn directly at its own (col,page).
// -----------------------------------------------------------------------------

int impComposeMapByte( int col, int page )
{
    int mapX, sub, upper, lower, upperByte, lowerByte;
    mapX = col / 4;
    sub = col % 4;
    upper = impVVram[ page * 2 ][ mapX ];
    lower = impVVram[ page * 2 + 1 ][ mapX ];
    if( sub == 0 )
    {
        upperByte = impCharPattern[ upper * 2 + 0 ];
        lowerByte = impCharPattern[ lower * 2 + 0 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    if( sub == 1 )
    {
        upperByte = impCharPattern[ upper * 2 + 0 ];
        lowerByte = impCharPattern[ lower * 2 + 0 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    if( sub == 2 )
    {
        upperByte = impCharPattern[ upper * 2 + 1 ];
        lowerByte = impCharPattern[ lower * 2 + 1 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    upperByte = impCharPattern[ upper * 2 + 1 ];
    lowerByte = impCharPattern[ lower * 2 + 1 ];
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

// Title screen: OR-combines the map/VVram byte (the real logo bitmap, see
// impBeginTitle()) with the text-overlay byte, the same defense-in-depth
// mirroring Cracky's own crkComposeRawByte() fix. Every other state keeps
// the original exclusive choice - gameplay's own in-map text ("GAME OVER",
// printed directly over the frozen terrain) needs the text to fully
// REPLACE whatever's underneath it, including at an embedded space, or the
// terrain would bleed back through it - see this file's own header comment
// for the full reasoning on why these two cases are deliberately different.
void impRender()
{
    int page, col, value, charCol, tc, mapByte, textByte;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            charCol = col / 4;
            tc = impTextChar[ page ][ charCol ];

            if( impState == IMP_STATE_TITLE )
            {
                mapByte = 0;
                if( col < 96 )
                  mapByte = impComposeMapByte( col, page );
                textByte = 0;
                if( tc != 0 )
                  textByte = impAsciiPattern[ ( tc - 1 ) * 4 + ( col % 4 ) ];
                value = mapByte | textByte;
            }
            else if( tc != 0 )
              value = impAsciiPattern[ ( tc - 1 ) * 4 + ( col % 4 ) ];
            else if( col < 96 )
              value = impComposeMapByte( col, page );
            else
              value = 0;
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine - the same overall shape as Cracky's own state machine,
//   but with only 4 states (this game has no "clear a stage" win
//   condition at all - it scrolls through its own 8 stages forever,
//   looping, with rising difficulty; the only terminal state is
//   RemainCount==0 -> game over -> title).
// -----------------------------------------------------------------------------

void impBeginTitle()
{
    int sStart[8] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int i, j;

    for( i = 0; i < IMP_VVRAM_HEIGHT; i = i + 1 )
    {
        for( j = 0; j < IMP_VVRAM_WIDTH; j = j + 1 )
          impVVram[ i ][ j ] = IMP_CHAR_SPACE;
    }
    impClearTextOverlay();
    impHideAllSprites();

    // upstream's own real Title() shows whatever Score/CurrentStage/
    // RemainCount currently hold (frozen from the just-ended game, or the
    // initial 0/0/3 on first boot) - a real, observed-as-intentional
    // arcade convention (show the last game's own final stats while
    // idling at the title), not reset here - see header comment.
    impPrintStatus();

    // **Restored, matching Cracky's own identical restoration**: this is
    // upstream's own real 6-glyph-slot "IMPETUS" logo bitmap, drawn
    // directly into impVVram from impTitleBytes[] at its own real position
    // (VVram rows 2-5, i.e. real hardware pages 1-2 - matching upstream's
    // `Status.cpp` `Title()`'s own `VVram + VVramWidth*2 + TitleLeft`
    // starting offset exactly, with `TitleLeft=0` since `4*TitleLength`
    // already equals the full `IMP_VVRAM_WIDTH`). `impRender()` OR-combines
    // this VVram content with impTextChar's own text layer while on the
    // title screen (see that function's own comment) - the two occupy
    // disjoint page ranges by construction (logo: pages 1-2 only; every
    // piece of title-screen text below: pages 0/3/5/6/7), so this never
    // actually blends two distinct pieces of content together.
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                impVVram[ 2 + row ][ ch * 4 + col ] = impTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // MINI's own real column, matching upstream's literal computed value
    // (`TitleLeft + 4*TitleLength - 5 = 0 + 24 - 5 = 19`) - moved here from
    // an earlier, wrong, arbitrary column (8) that the plain-text
    // "IMPETUS" placeholder this replaces had used instead.
    impPrintS( 3, 19, sMini, 4 );
    impPrintS( 7, 12, sCredit, 12 );

    impPrintS( 5, 9, sStart, 5 );
    impPrintS( 6, 9, sContinue, 8 );
    impSelection = 0;
    impSelectionChanged = true;
    impPrevLeft = 0; impPrevRight = 0; impPrevUp = 0; impPrevDown = 0; impPrevFire = 0;
    impState = IMP_STATE_TITLE;
}

void impUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !impPrevLeft ) || ( right && !impPrevRight ) ||
                ( up && !impPrevUp ) || ( down && !impPrevDown ) );
    justFire = ( fire && !impPrevFire );
    impPrevLeft = left; impPrevRight = right; impPrevUp = up; impPrevDown = down; impPrevFire = fire;

    if( impSelectionChanged )
    {
        impSelectionChanged = false;
        if( impSelection == 0 )
          impPrintC( 5, 8, '>' );
        else
          impPrintC( 5, 8, ' ' );
        if( impSelection == 1 )
          impPrintC( 6, 8, '>' );
        else
          impPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        impPendingContinue = ( impSelection == 1 );
        impScore = 0;
        if( !impPendingContinue )
          impCurrentStage = 0;
        impRemainCount = 3;

        impInitStage();
        impInitTrying();
        impClearTextOverlay();
        impPrintStatus();
        impDrawAll();
        impStartSeq( 1, IMP_MELODY_START );
        impClock = 0;
        impTickCounter = 0;
        impState = IMP_STATE_START_JINGLE;
        impRender();
        return;
    }
    if( justDir )
    {
        impSelection = impSelection ^ 1;
        impSelectionChanged = true;
    }
    impRender();
}

void impUpdateStartJingle()
{
    if( !impSeqPlaying( 1 ) )
    {
        impStartBgm();
        impClock = 0;
        impState = IMP_STATE_PLAYING;
    }
    impRender();
}

void impBeginGameOverJingle()
{
    impStopBgm();
    impPrintGameOver();
    impStartSeq( 1, IMP_MELODY_GAMEOVER );
    impState = IMP_STATE_GAMEOVER_JINGLE;
}

void impUpdateGameOverJingle()
{
    if( !impSeqPlaying( 1 ) )
      impBeginTitle();
    else
      impRender();
}

// One real throttled tick = upstream's own (even Clock, gated+drawn)
// iteration - see header comment for the full derivation of why this
// port only needs the even-iteration's own gated blocks written out
// literally (they're all unconditionally true at this exact entry point).
void impAdvanceOneClockTick()
{
    impMoveFighter();
    impMoveEnemyBullets();
    if( ( impClock & 3 ) == 0 )
    {
        impMoveSkyEnemies();
        impMoveBarriers();
        impMoveItem();
    }
    if( ( impClock & 7 ) == 0 )
      impMoveFort();
    impMoveFighterBullets();
    if( ( impClock & 15 ) == 0 )
    {
        impScrollGround();
        impMoveGroundEnemy();
    }
    impUpdateBangs();
    impDrawAll();
}

void impUpdatePlaying()
{
    impTickCounter = impTickCounter + 1;
    if( impTickCounter < IMP_TICK_DIVISOR )
    {
        impRender();
        return;
    }
    impTickCounter = 0;

    impAdvanceOneClockTick();

    // upstream's own second, odd-Clock loop iteration - every gated block
    // in impAdvanceOneClockTick() would evaluate false at an odd Clock
    // value except the unconditional impMoveFighterBullets() call, so
    // only that one call is reproduced directly rather than re-running
    // the whole (mostly no-op) function a second time.
    impMoveFighterBullets();
    impClock = impClock + 2;

    if( impRemainCount == 0 )
    {
        impBeginGameOverJingle();
        impRender();
        return;
    }

    impRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameImpetus_init()
{
    impScore = 0;
    impCurrentStage = 0;
    impRemainCount = 3;
    impRndIndex = 0;

    impSeqActive[ 0 ] = 0; impSeqMelody[ 0 ] = IMP_MELODY_NONE;
    impSeqActive[ 1 ] = 0; impSeqMelody[ 1 ] = IMP_MELODY_NONE;
    impSeqActive[ 2 ] = 0; impSeqMelody[ 2 ] = IMP_MELODY_NONE;
    impTickCounter = 0;
    impClock = 0;

    impBeginTitle();
}

void gameImpetus_update()
{
    impAdvanceSound();

    if( impState == IMP_STATE_TITLE )
      impUpdateTitle();
    else if( impState == IMP_STATE_START_JINGLE )
      impUpdateStartJingle();
    else if( impState == IMP_STATE_PLAYING )
      impUpdatePlaying();
    else if( impState == IMP_STATE_GAMEOVER_JINGLE )
      impUpdateGameOverJingle();
}
