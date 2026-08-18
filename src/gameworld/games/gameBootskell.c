#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// BOOTSKELL mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_bootskell`) - a push-block
// action puzzle: push blocks around a 12x7 map to crush wandering monsters
// with them (readme: "Push a block to hit a monster and defeat it. Watch out
// - monsters push blocks at you too."). No ladders/climbing here (unlike the
// sibling `gameCracky.c` port) - every actor moves in 4 directions on a flat
// grid, and the only way to lose is a monster-pushed block sliding into you.
// 8 hand-authored stages, 3 lives, session-only score (upstream's own
// `word HiScore;` is commented out in Main.cpp - genuinely never tracked at
// all, not just dropped by this port - matched faithfully rather than added
// as a new feature).
//
// Same "cate" engine lineage as gameCracky.c (same author, same shared
// Vram/VVram/Oled/Timer/Sound/Print architecture) - this port follows that
// file's own proven methodology throughout; see its own header comment for
// the fuller rationale behind several of the choices repeated here. Only 4
// directions + 1 action button (`ScanKeys.h`'s own `Keys_Button0`, confirmed
// via `ScanKeys.cpp` - no second button exists on this hardware), covered by
// `tinyJoypadShim.h` with isFire2Pressed() unused, same as Cracky.
//
// **No hardware-orientation transform, confirmed by the same reasoning
// already proven for Cracky**: `Oled.cpp`'s `InitOled()` sends the identical
// `OledCmd::RightToLeft`/`OledCmd::BottomToTop` pair Cracky's own header
// explains is a real-panel-mounting correction with nothing to compensate
// for in software - `bskComposeRawByte(col,page)` draws directly at its own
// (col,page), no mirror, no bit-reversal.
//
// **Rendering mirrors Bootskell's own real Back/Front VVram split exactly**,
// which is a genuine architectural difference from Cracky (Cracky recomputes
// one VVram grid from scratch every tick; Bootskell keeps TWO 24x16 grids,
// `bskVVramBack` - the persistent map/fence/settled-or-animating-block layer,
// only touched when something in it actually changes - and `bskVVramFront` -
// rebuilt every real DrawAll() from Back, then had every live sprite (Man/
// Monsters/moving Blocks/Points) composited on top). Ported as a direct
// structural mirror of upstream's own `VVramBackToFront()`+`DrawSprites()`+
// `VVramToVram()` split (the same "faithfully copy an intricate stateful
// algorithm's own shape" reasoning as Cracky's own VVram note and Frogger's
// row-buffer compositing elsewhere in this project) rather than collapsing
// it into one function - `bskDrawAll()` only does the first two steps
// (rebuild Front); the final "blit to the real screen" step is `bskRender()`,
// called exactly once at the end of every real engine frame by the state
// machine, matching Cracky's own crkDrawAll()/crkRender() split. Upstream's
// own dirty-tracking `Backup[]` (a real-hardware bandwidth optimization) is
// dropped, matching this project's now-standard "always redraw the full
// frame" precedent.
//
// **A dense text-overlay layout puzzle, solved with a small generalized
// overlay mechanism rather than Cracky's single-slot one.** Every piece of
// UI text in this game (SCORE/STAGE/TIME/lives, the title screen's "MINI"/
// credit/START/CONTINUE+cursor, GAME OVER, TIME UP) is written via upstream's
// `PrintC()`, which sends raw ASCII-font bytes directly to real Vram
// addresses, bypassing VVram entirely - on real hardware these persist
// because nothing else re-touches those exact bytes (the same real-VRAM-
// persistence class already found and fixed for Cracky's own GAME OVER/
// TIME UP messages). Tracing the *real* absolute pixel columns upstream uses
// (not guessed) found two genuinely different regions in play at once:
//   - SCORE/STAGE/TIME/lives always sit at `LeftX`(24 char-cells)+N, i.e. the
//     fixed 8-char-cell-wide strip at real columns 96-127 - reused Cracky's
//     own `crkStatusChar[8][8]` grid mechanism verbatim (bskStatusChar),
//     but with the *correct* relative column math (upstream's own
//     `LeftX+N` value minus `LeftX` itself, e.g. `PrintNumber5(...,
//     LeftX+2)` becomes column 2, not "2+2"=4) - re-deriving this by hand
//     instead of literally transcribing gameCracky.c's own equivalent
//     numbers surfaced that crkPrintScore's own `2+2`/`2+7` column choices
//     don't actually match this derivation (they'd need relative columns
//     2 and 7, not 4 and 9 - the latter overruns crkStatusChar's real
//     8-column width). Not something this port needed to fix (it's
//     Cracky's own already-shipped file, out of scope here), but worth
//     flagging: this port's own status-grid columns were derived fresh
//     from Bootskell's real Status.cpp/Print.cpp source instead, and every
//     one lands inside 0-7 by construction (SCORE label cols0-4; score
//     value cols0-4 + a literal trailing '0' at col5, matching upstream's
//     own "track score as a x10 value, always show one extra zero digit"
//     design; STAGE label cols0-4 + 2-digit number cols6-7; TIME label
//     cols0-3 + 3-digit value cols5-7; a single lives digit at col0, a
//     deliberate simplification of upstream's own `Char_Remain` icon
//     row - see below).
//   - The title screen's logo/"MINI"/credit/START/CONTINUE/cursor are all
//     positioned at real absolute columns *within the map area* (0-95), not
//     the status strip - generalized Cracky's single `crkOverlayActive`
//     into a small fixed array (`BSK_OVERLAY_COUNT`=4: MINI, credit,
//     START-line, CONTINUE-line), each an independent (active,page,col,
//     text,len) slot checked in `bskRender()`'s own per-pixel loop before
//     falling back to the normal VVram-tile lookup - reproducing upstream's
//     real title-screen positions exactly (MINI at page3/col19, credit at
//     page7/col12, START/CONTINUE+cursor at page5-6/col8) rather than
//     rebasing everything into the cramped 8-column status strip the way
//     Cracky's own title screen did. The cursor character is folded
//     directly into the START/CONTINUE line's own text array (col0 of
//     each, '>' or ' ') and rebuilt unconditionally every real frame this
//     port's own always-redraw model needs no "did the selection change"
//     dirty-tracking at all, unlike upstream's own real `changed` flag
//     (a bandwidth optimization with no equivalent need here). All 4
//     slots are reused later for GAME OVER/TIME UP (only slot 0 needed at
//     a time there, cleared via `bskClearOverlays()` on every real state
//     transition that leaves them behind - InitTrying and BeginTitle both
//     call it, matching every prior overlay-user in this project's own
//     "clear state that doesn't get naturally overwritten" lesson).
//   - Deliberately NOT calling the gameplay SCORE/STAGE/TIME/lives status
//     readout (`bskPrintStatus()`) on the title screen at all, unlike
//     upstream's own `Title()` (which does call it first, then partially
//     overwrites some of the very same cells with its own decorative text
//     - upstream's `PrintStatus()` writes "STAGE" at the identical
//     page/column its own `INUFUTO`-equivalent credit text uses moments
//     later on Cracky's title screen, and something similar likely happens
//     here too). Since Bootskell's title/status layouts don't actually
//     overlap columns in *this* port (status strip vs. map area are
//     disjoint), there'd be no real collision to avoid here either way -
//     the real reason to skip it is that a freshly-reset SCORE/STAGE/TIME
//     readout isn't meaningful before a game has even started, and this
//     keeps the title screen simple. A deliberate, documented
//     simplification, not a bug.
//   - The title logo IS ported pixel-perfect (not simplified to text like
//     Cracky's own "CRACKY"/"INUFU" did) - checked the available font
//     first and confirmed simplifying to plain text wasn't actually
//     viable here: the ASCII font (`" 0123456789>ACEFGIMNOPRSTUV"`) has no
//     B, K, or L, so "BOOTSKELL" itself cannot be spelled with it at all -
//     exactly why upstream needed a hand-drawn pixel logo in the first
//     place. Porting the raw 96-byte `TitleBytes` table (byte-diff
//     extracted via script) directly into `bskVVramFront` once, at
//     `bskBeginTitle()`, following upstream's own exact nested-loop
//     layout (6 side-by-side 4x4-tile blocks spanning VVram rows 2-5, the
//     full 24-column map width) was simpler and more faithful than
//     inventing a substitute.
//
// **A dense multi-rate real-tick structure, genuinely different from
// Cracky's own single-gated loop.** Upstream's `do{...}while(MonsterCount!=0
// || AnyBlockMoving());` loop has THREE independent bitmask gates on the
// same free-running `Clock` byte (`Clock&3`, `Clock&0x1f`, and the render/
// pace gate `Clock&7` wrapping a single `WaitTimer(3)`), plus an UNGATED
// `UpdateBlocks()` call running on every single loop iteration - a real,
// deliberate design where blocks visibly slide faster (updated 8x per
// render) than Man/Monster movement and timer countdown (updated only 2x
// per render, at Clock%4==0) and far faster than new-block spawning
// (Clock%32==0, once per 4 renders). Since only the ONE `WaitTimer(3)` call
// (at the render gate) consumes any real wall-clock time - every other
// iteration in between executes at full bare-loop speed on real hardware,
// with nothing rendered in between to make the exact sub-batch ordering
// observable - this port batches one full "8 Clock values" cycle into a
// single inner loop, run once every `BSK_TICK_DIVISOR`(3) real engine
// frames (matching `WaitTimer(3)`'s own real duration at this project's
// 60Hz native tick rate), calling `bskDrawAll()`/`bskRender()` exactly once
// at the end of the batch rather than trying to reproduce upstream's own
// mid-batch render timing (which has no visible effect regardless, since
// nothing else redraws in between on real hardware either). The
// `Clock&3`/`Clock&0x1f` gates, the ungated `UpdateBlocks()`, the death
// check after every sub-tick, and the `MonsterCount!=0||AnyBlockMoving()`
// loop-exit check after every sub-tick are all reproduced exactly, matching
// upstream's own early-exit semantics (a mid-batch TimeUp or death event
// skips the remaining sub-ticks in that batch, same as upstream's own
// `goto lose;`). See `bskUpdatePlaying()`'s own comments for the exact
// mapping. Same "decouple logic-tick cadence from render cadence" family
// of fix already needed by Tiny Arkanoid/Tiny SQuest/Ardumania elsewhere in
// this project, just with three distinct sub-rates batched together instead
// of one uniform multiplier.
//
// **Sound**: same real 3-tone-channel tracker as Cracky's own `Sound.cpp`
// (byte-for-byte identical `ToneChannel`/`EffectChannel` mixer code), but a
// different `Tempo`(180, not 160) - re-derived the note-duration formula
// from scratch rather than reusing Cracky's own precomputed 1.875 constant:
// `SoundHandler()`'s real cadence is `(600/2)/Tempo` real 60Hz ticks per
// note-length-unit, giving `1.6666667` here (`bskNoteFrames(length) =
// round(length * 1.6666667)`). Every melody routed through
// `md_playTone(freqHz, durationSeconds)` the same way as Cracky (this
// engine's own multi-voice fix means the one-shot SFX channel and the two
// simultaneous BGM voices never fight over a shared channel) - melodies
// resolved by id via `bskMelodyLength()`/`bskMelodyValue()` (Tiny Dungeon's
// own "resolve by id, not by real pointer" precedent), byte-diff-extracted
// from `Sound.cpp` via a small Python script (not hand-copied) rather than
// trusted from a first eyeballed transcription, matching this project's own
// "byte-diff transcribed tables" discipline. Sequencer slots: 0=one-shot
// SFX (Sound_Loose/Hit/Beep/Push all multiplex this one channel, matching
// upstream's own real channel-0 usage exactly), 1=jingle/BGM-voice-A,
// 2=BGM-voice-B - all three advance every real engine frame regardless of
// `BSK_TICK_DIVISOR`, matching upstream's own `SoundHandler()` running off
// the same real 60Hz SysTick interrupt as `Main()`'s own loop, never itself
// throttled by any of the `Clock&N` gates.
//
// **Movable coordinates are genuinely non-degenerate here** (unlike
// Cracky's own `CoordShift=0` special case) - `MapShift=1`/`MapRate=2`:
// grid-cell lookups (`GetCell`/`SetCell`) use `x>>MapShift`, while a
// Movable's own raw `x`/`y` (and every Sprite's own draw position) stay in
// "half-cell" VVram-column units directly (`VVramWidth`(24) ==
// `MapWidth`(12)*`MapRate`(2), so no extra scaling is ever needed between a
// Movable's raw coordinate and its VVram draw position - confirmed by
// tracing `Sprite.cpp`'s own `ShowSprite()`, which copies `pMovable->x`
// straight into the sprite's draw `x` with zero shift). A single "step" of
// `MoveCountMask`(7)+1=8 real sub-ticks moves a Movable by exactly 1 raw
// unit (half a visual cell) via `DirectionElements[]`'s own literal ±1
// deltas - two such steps cross one real map cell, giving the same smooth
// half-cell walk/slide animation upstream has.
//
// **A real, proactively-fixed byte-truncation-reliance bug, the same class
// documented throughout this whole project's own history** (this port's
// own first instance, caught by inspection before ever compiling, not by a
// crash report): `Stage.cpp`'s real `GetCell(byte x, byte y)` relies on `x`/
// `y` being *unsigned* - a negative raw coordinate (reachable any time a
// Movable checks the cell one step past a map edge, e.g. `NextCellOrMovable`
// at `Man.x==0` facing left) wraps to a large positive byte value on real
// hardware, which the existing `if (x >= MapWidth || y >= MapHeight) return
// Cell_Wall;` bound already catches correctly by coincidence. Vircon32's
// plain `int` doesn't wrap the same way, so `bskGetCell()` explicitly checks
// `x < 0 || y < 0` too, rather than trusting an unsigned-wraparound trick
// this dialect's own widened-`int` model can't reproduce. A related,
// deliberately-added safety guard: `PushBlock`'s own local `x`/`y`
// ("2 raw units ahead of the pusher", used only to *check* whether a push
// is even legal) can also go negative right at a map edge, and gets shifted
// (`x>>1`) before that check - added an explicit sign-safe halving helper
// (`bskSafeShiftR1`) rather than relying on Vircon32's own *logical*
// (zero-fill, not sign-extending) `>>` happening to still produce a huge-
// enough positive value to be caught by the same bound (verified by hand
// that it *would* still work by coincidence, but this project has
// consistently preferred an explicit guard over relying on that kind of
// accident - see HollowSeeker/Tiny Pipe/Nohzdyve/Gilbert in the Downland's
// own identical `>>`-of-a-possibly-negative-value fixes). Every OTHER
// `>>MapShift` site in the file (`NextCellOrMovable`, block/monster
// position-to-grid conversions) was checked and confirmed to only ever
// operate on an already-non-negative Movable/Block coordinate (each one is
// kept in-bounds by its own "check before advancing" gate before ever
// being shifted), so none of those needed the same treatment.
//
// **A second real, proactively-fixed bug: `OnHitBlock`'s own score-
// multiplier table can run one hit past its own bounds.** `pBlock->clock`
// (reused here as a 0-3 "how many monsters has this one push already hit"
// counter, incremented up to `MaxRate`(3)+1=4 after the 4th hit) indexes a
// literal 4-entry `Values[]` table (`{10,20,40,80}`) - a 5th consecutive
// hit from the same still-moving block would read `Values[4]`, one past the
// table's own real bound. Harmless on real hardware (an adjacent-flash-byte
// read); a genuine out-of-bounds array read here. Clamped `rate` to 3
// before indexing `bskPointValues[]` - the same "preserve behavior, guard
// the crash" treatment already used for Tiny Arena's own `Lvl1` off-by-one
// and several others in this project.
//
// **A genuine, faithfully-preserved upstream typo, not "fixed"**:
// `Monster.cpp`'s `DecideDirection()` compares `pMonster->targetX <
// pMonster->_.y` (X against Y) in its horizontal-tie-break branch - the
// exact same X-vs-Y mixup already found and deliberately preserved in
// Cracky's own `crkDecideDirection()` (see that file's own comment on this
// one) - both games share the same author and the same underlying AI
// shape, so this is very likely the identical, real, shipped quirk rather
// than two independent transcription slips. Kept exactly as upstream has
// it. A second, separate, genuine upstream oddity (not a transcription
// risk, traced through by hand): `DecideTarget()`'s own line-of-sight
// distance formula, `distance = Abs(manX,thisX) + Abs(manY,thisY)`, doesn't
// actually depend on the candidate target position it's comparing at all -
// it recomputes the exact same Man-to-monster Manhattan distance every one
// of the 4 direction checks, so `minDistance` can only ever be set by the
// FIRST qualifying direction (later directions' identical `distance` value
// can never beat an already-equal `minDistance`). Ported exactly as
// written - a faithful reproduction of upstream's own real targeting
// behavior, not a bug this port introduced.
//
// **Two dead upstream fields/functions confirmed via grep, not ported**:
// `Math.cpp`'s own `Rnd()`/`RndIndex`/`Numbers[]` (a real PRNG, but never
// actually *called* anywhere in this game - Bootskell has no randomness at
// all, unlike Cracky which does use its own identical table); `Monster.h`'s
// `pushCount` field and `Sprite.h`'s `StepMask` constant (both declared,
// neither ever referenced). `Stage.h`'s own `StageWidth`/`StageHeight`
// constants are likewise declared but never used anywhere in the real
// source. `Status.cpp`'s `PrintPerfect()` is defined but never called.
// None of these were ported.
//
// **The lives readout was simplified**, matching Cracky's own precedent for
// the identical `Char_Remain`-icon display upstream has (both games share
// this exact status-line shape) - shown as a single plain digit
// (`RemainCount`) rather than porting the icon-row logic, partly because
// the ASCII font can't spell "LIVES" either (no L).
//
// **A 30fps whole-tick throttle was NOT added** - upstream's own real
// `WaitTimer(3)` IS a genuine, deliberate real-time rate (not the "no
// timing model whatsoever" category several other beyond-scope ports in
// this project needed a throttle added to), and `BSK_TICK_DIVISOR`(3)
// already reproduces it directly - no further slowdown was requested or
// warranted. Confirmed the exact match, not just approximately: real
// hardware's `Timer.cpp` runs its own SysTick at a genuine, hardcoded
// 60Hz (`kTimerHz = 60`), the same rate this whole engine runs at, so
// `WaitTimer(3)` really does mean "wait 3 real 60Hz ticks" on both
// platforms identically - no unit-conversion guesswork needed. Also
// confirmed `SoundHandler()`'s own real `Tempo=180`/`(600/2)`-per-fire
// cadence against this same real 60Hz tick: a channel's `Next()` (one
// melody-note-length unit) fires on average every `300/180 =
// 1.6666667` real ticks, exactly matching `BSK_NOTE_UNIT_FRAMES` - both
// re-derived from the real register/constant math rather than assumed
// from Cracky's own already-shipped value.
//
// **A meticulous re-verification pass** (this game was ported in a large
// parallel batch and only ever test-compiled, never played - a real
// dialect bug, a 2D array passed by value into `bskVPut2S`/`bskVPut2C`,
// was already caught and fixed by removing the unused-in-practice grid
// parameter during central registration, making this file a known
// higher-risk candidate for a live audit) re-read every real upstream
// `.cpp`/`.h` file line by line against this port, byte-diff-verified
// every data table via a small Python script (`AsciiPattern`/
// `CharPattern`/`TitleBytes`/`Frequencies` from `Chars.cpp`/`Status.cpp`/
// `Sound.cpp`, all 8 stages' `start`/`enemyCount`/`pEnemies`/`bytes[]`
// from `Stages.cpp`, and all 9 melody note tables from `Sound.cpp`,
// resolving each side's own symbolic note names to the same numeric
// values before comparing) - every single one matched exactly, and also
// confirmed via a direct grep that every real upstream `VPut2S`/`VPut2C`
// call site (`Block.cpp` x2, `Stage.cpp` x2) always targets `VVramBack`
// and never `VVramFront`, so the parameter-removal fix above is safe by
// construction, not just by absence of a counter-example found so far.
// Two genuine bugs were found and fixed:
//
// 1) **A real, significant gameplay-balance bug: monsters moved ~1.67x
//    too fast.** `Main()`'s own do-while loop gates `MoveMonsters()`
//    behind more than just `(Clock&3)==0` - it also has a second,
//    independent `sbyte monsterNum` frame-skip accumulator
//    (`if(monsterNum>=0){MoveMonsters();monsterNum-=10;}monsterNum+=6;`)
//    that throttles monster movement to only 3 calls out of every 5
//    times the outer gate fires (a real, deliberate difficulty choice -
//    monsters visibly move slower than the player and blocks). This
//    port's first version omitted the accumulator entirely, calling
//    `bskMoveMonsters()` unconditionally every time `(bskClock&3)==0`
//    fired - the same rate as the player, i.e. monsters moved 5/3 times
//    faster than upstream intends. **Fixed** with a new `bskMonsterNum`
//    global reproducing the exact same accumulator (reset to 0 alongside
//    `bskClock` in `bskUpdateStartJingle()`, matching upstream's own
//    `try_:` label resetting `Clock`/`monsterNum` together before the
//    start jingle plays) - verified the exact call/skip sequence via a
//    standalone Python simulation (18 calls out of 30 gate-fires = 3/5,
//    matching by construction). The `goto lose`-skips-the-whole-block
//    semantics (a mid-iteration TimeUp discards the pending monsterNum
//    update for that tick too, not just the `MoveMonsters()` call) are
//    preserved by nesting the whole accumulator inside the existing
//    `if(!timeUp)` guard.
// 2) **A real rendering-fidelity bug: the live score number rendered 2
//    columns too far left.** `Status.cpp`'s real `PrintScore()` places
//    the 5-digit value at `(LeftX+2)*VramStep` (relative column 2, not
//    0) with the trailing extra '0' digit immediately after it at
//    relative column 7 (not 5) - re-derived directly from `Print.h`'s
//    `VramStep`/`Vram.h`'s `VramRowSize` math rather than assumed. This
//    port's first version used columns 0 and 5 instead - harmless (page1
//    is otherwise unused, so nothing overlapped), but a genuine, visible
//    divergence from upstream's real on-screen position. **Fixed** by
//    changing `bskPrintScore()` to `bskPrintNumber5(1,2,...)` and
//    `bskPrintC(1,7,'0')`. Every other status-text column (SCORE/STAGE/
//    TIME labels and values, the lives digit, the title-screen MINI/
//    credit/START/CONTINUE overlays, GAME OVER/TIME UP) was independently
//    re-derived the same way and confirmed already correct.
//
// **Verified live** via this project's own Puppeteer/WebGL harness (a
// freshly rebuilt ROM, own isolated server instance): the alphabetized
// menu correctly lists "BOOTSKELL" (credited "BY INUFUTO"); the title
// screen renders the pixel-perfect logo, "MINI", the cursor-driven START/
// CONTINUE selection, and the "INUFUTO 2026" credit with no corruption;
// starting a game shows the correct initial STAGE 1/TIME 90 (60 + 30 for
// stage 0's 1 enemy, matching `InitTrying()`'s own formula exactly) with
// the score value now visibly offset 2 columns right of the "SCORE"
// label (confirming fix #2); movement in all 4 directions and repeated
// block-pushing (score climbing correctly, e.g. 0->40->50 across a play
// session) all rendered correctly with no garbled tiles; and, reached
// live rather than forced via a debug hook, a genuine player death
// (RemainCount visibly dropping 3->2 on the status line) correctly
// triggered the full `LOSE_ANIM`->respawn sequence - the same stage
// re-initialized with a fresh TIME value near 90 and a newly-reset block
// layout, exactly matching `goto try_`'s own "retry the same stage, keep
// score, decrement lives" semantics. A genuine level-clear/game-over
// were not independently forced this session (stage 0's own single
// monster wasn't cornered into a scripted kill within a reasonable
// number of blind automated button presses) - both reuse the exact same
// `OnHitBlock`/`bskUpdateBonusTally`/`bskUpdateGameOverJingle` control
// flow already verified correct by direct line-by-line comparison
// against `Block.cpp`/`Main.cpp` above, so risk is low, but worth a
// direct check if anything looks off in a future session.
//
// -----------------------------------------------------------------------------
//   Ported to this SDL3/SDL2/Playdate project from tinyjoypad_vircon32's own
//   already-correct Vircon32-dialect gameBootskell.c (the header above is
//   that project's own, describing the real design work done porting from
//   real UIAPduino/CH32V003 hardware to the Vircon32 dialect - all of that
//   context still applies unchanged, since this port reuses the exact same
//   C logic; only a mechanical dialect conversion happened here, same recipe
//   as every other game in this project - see this project's own CLAUDE.md).
//   Concretely for this file: every `TYPE[N] name`/`TYPE[N][M] name`
//   declaration was reordered to standard `TYPE name[N]`/`TYPE name[N][M]`;
//   the three bare `struct Foo {...}; Foo x;` tags (BskMovable, BskMonster,
//   BskSprite) became real `typedef struct {...} Foo;`; every array/pointer
//   that actually carries display TEXT (the SCORE/STAGE/TIME/lives labels,
//   the title-screen MINI/START/CONTINUE/credit strings, GAME OVER/TIME UP,
//   bskAsciiIndex()'s own lookup table, bskOverlayText[], and bskPrintS()'s/
//   bskSetOverlay()'s own `text` parameters) was retyped `char`/`char*`
//   instead of `int`/`int*` - genuine glyph/pixel-pattern data
//   (bskAsciiPattern, bskCharPattern, bskTitleBytes, bskCellChars) was left
//   as `int`, matching this project's own "only text, not sprite/pixel data"
//   rule. The already-fixed 2D-array-by-value bug this header's own comment
//   above describes (bskVPut2S()/bskVPut2C() dropping their grid parameter
//   in favor of operating directly on the global bskVVramBack) was already
//   fixed in the source this was ported from and is preserved exactly as-is
//   here - not reintroduced. No EEPROM/high-score persistence (upstream has
//   none - see header above). No forceRedraw hook: every one of this game's
//   own states (bskUpdateTitle/StartJingle/Playing/LoseAnim/GameOverJingle/
//   ClearJingle/BonusTally) calls bskRender() unconditionally on every real
//   tick, the same "always redraws, no dirty-flag skip" shape as most games
//   in this project that don't need one.
// -----------------------------------------------------------------------------
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define BSK_CHAR_SPACE 0x00
#define BSK_CHAR_FENCE 0x10
#define BSK_CHAR_WALL 0x12
#define BSK_CHAR_BLOCK 0x16
#define BSK_CHAR_MAN 0x3A
#define BSK_CHAR_MONSTER 0x5A
#define BSK_CHAR_POINT 0x7A
#define BSK_CHAR_END 0x8A

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define BSK_MAP_SHIFT 1
#define BSK_MAP_RATE 2
#define BSK_MAP_MASK 1

#define BSK_MOVE_COUNT_MASK 7

#define BSK_MOVABLE_SEQ_MASK 0x01
#define BSK_MOVABLE_DIRECTION_MASK 0x06
#define BSK_MOVABLE_PATTERN_MASK 0x07
#define BSK_MOVABLE_LIVE 0x80
#define BSK_MOVABLE_PUSHING 0x40

#define BSK_DIRECTION_LEFT 0
#define BSK_DIRECTION_RIGHT 2
#define BSK_DIRECTION_UP 4
#define BSK_DIRECTION_DOWN 6

#define BSK_HIT_RANGE ( 3 / 2 )

typedef struct
{
    int x, y;
    int dx, dy;
    int sprite;
    int status;
    int clock;
    int moveCount;
} BskMovable;

int bskDirectionElements[8] = {
    -1, 0,
    1, 0,
    0, -1,
    0, 1,
};

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define BSK_MAP_WIDTH 12
#define BSK_MAP_HEIGHT 7

#define BSK_CELL_MASK 3
#define BSK_CELL_NONE 0
#define BSK_CELL_BLOCK 1
#define BSK_CELL_WALL 2
#define BSK_CELL_MAN 4
#define BSK_CELL_MOVING_BLOCK 5
#define BSK_CELL_MONSTER 6

#define BSK_MAP_SIZE ( ( BSK_MAP_WIDTH / 4 ) * BSK_MAP_HEIGHT )
#define BSK_STAGE_COUNT 8
#define BSK_MAX_MONSTER_COUNT 6

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define BSK_VVRAM_WIDTH 24
#define BSK_VVRAM_HEIGHT 16
#define BSK_STAGE_TOP 1

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define BSK_SPRITE_MAN 0
#define BSK_SPRITE_MONSTER 1
#define BSK_SPRITE_BLOCK 7
#define BSK_SPRITE_POINT 14
#define BSK_SPRITE_COUNT 18
#define BSK_INVALID_CODE 255

#define BSK_MONSTER_SLOT_COUNT ( BSK_SPRITE_BLOCK - BSK_SPRITE_MONSTER )
#define BSK_BLOCK_COUNT ( BSK_SPRITE_POINT - BSK_SPRITE_BLOCK )
#define BSK_POINT_COUNT ( BSK_SPRITE_COUNT - BSK_SPRITE_POINT )

// -----------------------------------------------------------------------------
//   Block.h - block status byte constants
// -----------------------------------------------------------------------------

#define BSK_BLOCK_STATUS_NONE 0x00
#define BSK_BLOCK_STATUS_MOVE 0x10
#define BSK_BLOCK_STATUS_START 0x20
#define BSK_BLOCK_STATUS_DESTROY 0x30
#define BSK_BLOCK_STATUS_MASK 0x30
#define BSK_BLOCK_MANS 0x40
#define BSK_BLOCK_MAX_RATE 3

// -----------------------------------------------------------------------------
//   Point.h
// -----------------------------------------------------------------------------

#define BSK_POINT_TIME ( 6 * 2 )

// -----------------------------------------------------------------------------
//   Monster.h
// -----------------------------------------------------------------------------

#define BSK_INTERVAL_SHIFT ( BSK_MAP_SHIFT * 2 )
#define BSK_INTERVAL_MASK ( BSK_MAP_RATE * 2 - 1 )

typedef struct
{
    BskMovable m;
    int targetX, targetY;
} BskMonster;

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching
//   upstream's own enum exactly (Tempo differs from Cracky's - see header).
// -----------------------------------------------------------------------------

#define BSK_N8 6
#define BSK_N8L 8
#define BSK_N8R 4
#define BSK_N8P ( BSK_N8 * 3 / 2 )
#define BSK_N4 ( BSK_N8 * 2 )
#define BSK_N4P ( BSK_N4 * 3 / 2 )
#define BSK_N2 ( BSK_N4 * 2 )
#define BSK_N2P ( BSK_N2 * 3 / 2 )
#define BSK_N1 ( BSK_N2 * 2 )
#define BSK_N16 ( BSK_N8 / 2 )

#define BSK_E2 1
#define BSK_F2 2
#define BSK_F2S 3
#define BSK_G2 4
#define BSK_G2S 5
#define BSK_A2 6
#define BSK_A2S 7
#define BSK_B2 8
#define BSK_C3 9
#define BSK_C3S 10
#define BSK_D3 11
#define BSK_D3S 12
#define BSK_E3 13
#define BSK_F3 14
#define BSK_F3S 15
#define BSK_G3 16
#define BSK_G3S 17
#define BSK_A3 18
#define BSK_A3S 19
#define BSK_B3 20
#define BSK_C4 21
#define BSK_C4S 22
#define BSK_D4 23
#define BSK_D4S 24
#define BSK_E4 25
#define BSK_F4 26
#define BSK_F4S 27
#define BSK_G4 28
#define BSK_G4S 29
#define BSK_A4 30
#define BSK_A4S 31
#define BSK_B4 32
#define BSK_C5 33
#define BSK_C5S 34
#define BSK_D5 35
#define BSK_D5S 36
#define BSK_E5 37
#define BSK_F5 38
#define BSK_F5S 39
#define BSK_G5 40

// SoundHandler()'s own real tempo (180, not Cracky's 160): a channel
// advances once every (600/2)/180 = 1.6666667 real 60Hz ticks - see header.
#define BSK_NOTE_UNIT_FRAMES 1.6666667

#define BSK_MELODY_NONE 0
#define BSK_MELODY_LOOSE 1
#define BSK_MELODY_HIT 2
#define BSK_MELODY_BEEP 3
#define BSK_MELODY_PUSH 4
#define BSK_MELODY_START 5
#define BSK_MELODY_CLEAR 6
#define BSK_MELODY_GAMEOVER 7
#define BSK_MELODY_BGM1 8
#define BSK_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph. Confirmed
// byte-identical to Cracky's own crkAsciiPattern (same shared font).
int bskAsciiPattern[108] = {
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

// CharPattern - 138 map-tile glyphs, 2 bytes/glyph.
int bskCharPattern[276] = {
    0, 0, 51, 0, 204, 0, 255, 0,
    0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255,
    136, 136, 17, 17, 123, 222, 123, 14,
    115, 86, 115, 6, 30, 221, 29, 14,
    67, 85, 69, 3, 28, 88, 5, 14,
    66, 21, 69, 2, 0, 200, 72, 8,
    67, 37, 68, 2, 0, 0, 0, 0,
    82, 82, 37, 4, 0, 0, 0, 0,
    64, 4, 4, 4, 0, 200, 8, 0,
    0, 16, 0, 0, 0, 204, 12, 0,
    0, 17, 1, 0, 128, 108, 140, 0,
    0, 49, 1, 0, 192, 162, 194, 0,
    16, 34, 18, 0, 0, 117, 117, 0,
    0, 168, 65, 12, 0, 117, 117, 0,
    0, 130, 30, 2, 0, 87, 87, 0,
    192, 20, 138, 0, 0, 87, 87, 0,
    32, 225, 40, 0, 128, 119, 119, 8,
    128, 46, 98, 5, 128, 119, 119, 8,
    80, 38, 226, 8, 128, 117, 117, 8,
    129, 46, 98, 5, 128, 87, 87, 8,
    80, 38, 226, 24, 168, 175, 239, 8,
    16, 115, 191, 0, 64, 78, 206, 0,
    50, 247, 255, 2, 128, 254, 250, 138,
    0, 251, 55, 1, 0, 236, 228, 4,
    32, 255, 127, 35, 232, 239, 239, 8,
    48, 247, 55, 0, 192, 206, 206, 0,
    113, 255, 127, 1, 128, 190, 190, 142,
    0, 115, 127, 3, 0, 108, 108, 12,
    16, 247, 255, 23, 228, 192, 194, 0,
    50, 2, 97, 105, 36, 204, 194, 0,
    50, 2, 97, 105, 140, 206, 194, 0,
    0, 3, 97, 105, 164, 196, 194, 0,
    33, 1, 97, 105,
};

// Title-screen pixel logo, 6 side-by-side 4x4-tile blocks spanning the
// full map width (VVram rows 2-5) - upstream's own real `TitleBytes[]`.
int bskTitleBytes[96] = {
    15, 5, 13, 2, 15, 10, 14, 1, 15, 0, 12, 3, 5, 5, 5, 0,
    0, 0, 0, 0, 14, 13, 2, 14, 15, 12, 3, 15, 4, 5, 0, 4,
    0, 0, 8, 2, 13, 2, 13, 7, 12, 3, 12, 3, 5, 0, 0, 5,
    0, 0, 0, 12, 8, 7, 5, 12, 0, 5, 11, 12, 4, 5, 1, 4,
    3, 0, 0, 0, 11, 7, 8, 7, 15, 2, 12, 7, 1, 5, 0, 5,
    0, 12, 3, 15, 11, 12, 3, 15, 5, 12, 3, 15, 1, 4, 1, 5,
};

// Standard equal-tempered note frequencies, E2..G5 - identical to Cracky's
// own crkFrequencies (confirmed byte-for-byte the same table).
int bskFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int bskMelodyLoose[3] = { 1, BSK_A3, 0 };

int bskMelodyHit[17] = {
    1, BSK_F4, 1, BSK_G4, 1, BSK_A4, 1, BSK_B4, 1, BSK_C5,
    1, BSK_D5, 1, BSK_E5, 1, BSK_F5, 0,
};

int bskMelodyBeep[3] = { 1, BSK_A4, 0 };

int bskMelodyPush[13] = {
    1, BSK_C4, 1, BSK_C4S, 1, BSK_D4, 1, BSK_F4, 1, BSK_A4, 1, BSK_C5, 0,
};

int bskMelodyStart[23] = {
    BSK_N4, BSK_C4, BSK_N4, BSK_E4, BSK_N8, BSK_G4, BSK_N4, BSK_E4, BSK_N4, BSK_F4,
    BSK_N8, BSK_F4, BSK_N4, BSK_A4, BSK_N8, BSK_C5, BSK_N4P, BSK_A4, BSK_N2P, BSK_C5,
    BSK_N4, 0, 0,
};

int bskMelodyClear[29] = {
    BSK_N8, BSK_A4, BSK_N8, BSK_A4, BSK_N8, BSK_G4, BSK_N8, BSK_F4, BSK_N8, BSK_G4,
    BSK_N4, BSK_A4, BSK_N4, BSK_B4, BSK_N8, BSK_B4, BSK_N8, BSK_A4, BSK_N8, BSK_G4,
    BSK_N8, BSK_A4, BSK_N4, BSK_B4, 30 /* N8+N2 */, BSK_C5, BSK_N2, 0, 0,
};

int bskMelodyGameOver[21] = {
    BSK_N8, BSK_C5, BSK_N8, BSK_F4, BSK_N8, BSK_A4, BSK_N8, BSK_E4, BSK_N8, BSK_G4,
    BSK_N8, BSK_A4, BSK_N8, BSK_B4, BSK_N8, BSK_C5, BSK_N2P, BSK_C5, BSK_N4, 0, 0,
};

int bskMelodyBgm1[119] = {
    BSK_N4, BSK_C4, BSK_N4, BSK_G4, BSK_N8, BSK_C4, BSK_N4, BSK_G4, BSK_N4, BSK_A4,
    BSK_N8, BSK_A4, BSK_N8, BSK_G4, BSK_N8, BSK_G4, BSK_N8, BSK_F4, BSK_N8, BSK_F4,
    BSK_N8, BSK_E4, BSK_N8, BSK_E4, BSK_N4, BSK_D4, BSK_N4, BSK_D4, BSK_N8, BSK_D4,
    BSK_N4, BSK_E4, BSK_N4P, BSK_D4, BSK_N2P, 0,
    BSK_N4, BSK_C4, BSK_N4, BSK_G4, BSK_N8, BSK_C4, BSK_N4, BSK_G4, BSK_N4, BSK_A4,
    BSK_N8, BSK_A4, BSK_N8, BSK_G4, BSK_N8, BSK_G4, BSK_N8, BSK_F4, BSK_N8, BSK_F4,
    BSK_N8, BSK_E4, BSK_N8, BSK_E4, BSK_N4, BSK_F4, BSK_N4, BSK_F4, BSK_N8, BSK_F4,
    BSK_N4, BSK_A4, BSK_N4P, BSK_G4, BSK_N2P, 0,
    BSK_N8, BSK_E4, BSK_N8, BSK_E4, BSK_N8, BSK_E4, BSK_N4, BSK_E4, BSK_N8, BSK_E4,
    BSK_N4, BSK_A4, BSK_N8, BSK_D4, BSK_N8, BSK_D4, BSK_N8, BSK_D4, BSK_N4, BSK_D4,
    BSK_N8, BSK_D4, BSK_N4, BSK_G4,
    BSK_N8, 0, BSK_N8, BSK_A4, BSK_N8, 0, BSK_N8, BSK_G4, BSK_N8, 0, BSK_N8, BSK_F4, BSK_N8, 0, BSK_N8, BSK_E4,
    BSK_N4, BSK_D4, BSK_N4, BSK_E4, BSK_N2, BSK_C4, 255,
};

int bskMelodyBgm2[173] = {
    BSK_N4, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0,
    BSK_N8, BSK_C4, BSK_N4, BSK_A3, BSK_N8, 0, BSK_N8, BSK_A3, BSK_N8, 0, BSK_N8, BSK_A3,
    BSK_N8, 0, BSK_N8, BSK_A3, BSK_N4, BSK_D4, BSK_N8, 0, BSK_N8, BSK_D4, BSK_N8, 0,
    BSK_N8, BSK_D4, BSK_N8, 0, BSK_N8, BSK_D4, BSK_N4, BSK_G3, BSK_N8, 0, BSK_N8, BSK_G3,
    BSK_N8, 0, BSK_N8, BSK_G3, BSK_N8, 0, BSK_N8, BSK_G3,
    BSK_N4, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0,
    BSK_N8, BSK_C4, BSK_N4, BSK_A3, BSK_N8, 0, BSK_N8, BSK_A3, BSK_N8, 0, BSK_N8, BSK_A3,
    BSK_N8, 0, BSK_N8, BSK_A3, BSK_N4, BSK_F3, BSK_N8, 0, BSK_N8, BSK_F3, BSK_N8, 0,
    BSK_N8, BSK_G3, BSK_N8, 0, BSK_N8, BSK_G3, BSK_N4, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4,
    BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4,
    BSK_N4, BSK_C4, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0, BSK_N8, BSK_A3, BSK_N8, 0,
    BSK_N8, BSK_A3, BSK_N4, BSK_D4, BSK_N8, 0, BSK_N8, BSK_D4, BSK_N8, 0, BSK_N8, BSK_G3,
    BSK_N8, 0, BSK_N8, BSK_G3, BSK_N8, 0, BSK_N8, BSK_F3, BSK_N8, 0, BSK_N8, BSK_F3,
    BSK_N8, 0, BSK_N8, BSK_G3, BSK_N8, 0, BSK_N8, BSK_G3,
    BSK_N8, BSK_C4, BSK_N8, 0, BSK_N8, BSK_E3, BSK_N8, 0, BSK_N8, BSK_C4, BSK_N8, 0,
    BSK_N8, BSK_E3, BSK_N8, 0, 255,
};

// Point-hit score multiplier table (StartPoint's own `Values[]`), and the
// clamp guarding OnHitBlock's own real 5th-hit-overrun - see header.
int bskPointValues[4] = { 10, 20, 40, 80 };

// CellChars - the 4-glyph VVram block shown for each 2-bit map-cell value:
// 0=space (logo tile), 1=block, 2=wall. Value 3 never appears in real
// stage data (confirmed by scanning every stage byte before writing this).
int bskCellChars[12] = {
    BSK_CHAR_SPACE, BSK_CHAR_SPACE, BSK_CHAR_SPACE, BSK_CHAR_SPACE,
    BSK_CHAR_BLOCK + 0, BSK_CHAR_BLOCK + 1, BSK_CHAR_BLOCK + 2, BSK_CHAR_BLOCK + 3,
    BSK_CHAR_WALL + 0, BSK_CHAR_WALL + 1, BSK_CHAR_WALL + 2, BSK_CHAR_WALL + 3,
};

// Stage data - flattened from upstream's own `struct Stage { start,
// enemyCount, pEnemies, bytes[21] }` array + separate Enemies0-7 arrays
// into parallel fixed arrays, matching this project's own "flatten a
// struct-with-a-real-pointer-member into plain arrays" precedent.
int bskStageStart[8] = { 2, 6, 6, 176, 128, 1, 80, 6 };
int bskStageEnemyCount[8] = { 1, 2, 2, 3, 3, 4, 4, 6 };

int bskStageEnemies[8][6] = {
    { 182, 0, 0, 0, 0, 0 },
    { 128, 179, 0, 0, 0, 0 },
    { 160, 33, 0, 0, 0, 0 },
    { 2, 6, 182, 0, 0, 0 },
    { 113, 6, 182, 0, 0, 0 },
    { 176, 6, 86, 182, 0, 0 },
    { 70, 86, 102, 118, 0, 0 },
    { 0, 96, 176, 115, 86, 182 },
};

int bskStageBytes[8][21] = {
    { 85, 69, 85, 21, 69, 85, 0, 5, 84, 21, 68, 85, 85, 5, 85, 89, 5, 85, 25, 1, 21 },
    { 0, 21, 84, 21, 149, 106, 17, 20, 84, 25, 21, 20, 81, 69, 170, 17, 16, 0, 80, 85, 65 },
    { 170, 10, 64, 5, 24, 85, 37, 73, 0, 33, 8, 64, 36, 85, 85, 20, 81, 85, 84, 16, 64 },
    { 20, 22, 1, 21, 128, 65, 64, 148, 64, 170, 170, 84, 0, 129, 97, 168, 2, 96, 88, 68, 33 },
    { 64, 170, 16, 1, 40, 42, 80, 37, 4, 6, 17, 170, 82, 20, 0, 64, 0, 82, 32, 168, 42 },
    { 81, 128, 1, 72, 144, 162, 1, 144, 2, 65, 4, 66, 10, 33, 66, 0, 36, 64, 20, 161, 2 },
    { 0, 0, 0, 101, 85, 101, 32, 0, 36, 32, 69, 37, 96, 20, 37, 36, 0, 100, 33, 0, 100 },
    { 84, 69, 21, 85, 85, 85, 85, 85, 85, 85, 21, 85, 85, 85, 85, 85, 85, 85, 84, 81, 21 },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int bskScore;
int bskRemainCount;
int bskCurrentStage;
int bskStageTime;
int bskStageIndex;
int bskMonsterCount;
int bskMonsterNum;

#define BSK_MAX_TIME_DENOM 50
#define BSK_BONUS_RATE 9

int bskVVramBack[BSK_VVRAM_HEIGHT][BSK_VVRAM_WIDTH];
int bskVVramFront[BSK_VVRAM_HEIGHT][BSK_VVRAM_WIDTH];
int bskCellMap[BSK_MAP_SIZE];

typedef struct
{
    int x, y;
    int code;
} BskSprite;
BskSprite bskSprites[BSK_SPRITE_COUNT];

BskMovable bskMan;

BskMovable bskBlocks[BSK_BLOCK_COUNT];
int bskBlockStartCount;
int bskBlockStartX;
int bskBlockStartY;

BskMonster bskMonsters[BSK_MONSTER_SLOT_COUNT];

BskMovable bskPoints[BSK_POINT_COUNT];

// status-text grid (real columns 96-127, 8 char-cells wide x 8 pages tall)
// - a pattern index into bskAsciiPattern (0 = space) per cell.
int bskStatusChar[8][8];

// generalized text overlays for the title screen / GAME OVER / TIME UP -
// each draws directly into the MAP area (real columns 0-95), bypassing the
// normal VVram-tile composite, matching upstream's own real Vram-direct
// text writes there. See header comment for why this needed to be a small
// array of slots rather than Cracky's own single overlay.
#define BSK_OVERLAY_COUNT 4
int bskOverlayActive[BSK_OVERLAY_COUNT];
int bskOverlayPage[BSK_OVERLAY_COUNT];
int bskOverlayCol[BSK_OVERLAY_COUNT];
char bskOverlayText[BSK_OVERLAY_COUNT][12];
int bskOverlayLen[BSK_OVERLAY_COUNT];

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of BSK_TICK_DIVISOR.
int bskSeqMelody[3];
int bskSeqPos[3];
int bskSeqWait[3];
int bskSeqActive[3];

#define BSK_TICK_DIVISOR 3
int bskTickCounter;
int bskClock;
int bskTimeDenom;

#define BSK_STATE_TITLE 0
#define BSK_STATE_START_JINGLE 1
#define BSK_STATE_PLAYING 2
#define BSK_STATE_LOSE_ANIM 3
#define BSK_STATE_GAMEOVER_JINGLE 4
#define BSK_STATE_CLEAR_JINGLE 5
#define BSK_STATE_BONUS_TALLY 6
int bskState;
int bskWaitFrames;
int bskAnimStep;
int bskSelection;
int bskPrevLeft;
int bskPrevRight;
int bskPrevUp;
int bskPrevDown;
int bskPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp (only Abs() is real upstream code here - Rnd()/RndIndex/
//   Numbers[] are confirmed dead by grep, this game has no randomness)
// -----------------------------------------------------------------------------

int bskAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int bskSafeShiftR1( int v )
{
    // logical-vs-arithmetic-shift-of-a-negative-value safe halving - see
    // header comment (PushBlock's own lookahead check is the one site that
    // can genuinely receive a negative raw coordinate).
    if( v >= 0 )
      return v >> 1;
    return -( ( -v + 1 ) >> 1 );
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into bskStatusChar.
// -----------------------------------------------------------------------------

int bskAsciiIndex( int c )
{
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

int bskPrintC( int page, int col, int c )
{
    bskStatusChar[ page ][ col ] = bskAsciiIndex( c );
    return col + 1;
}

int bskPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = bskPrintC( page, col, s[ i ] );
    return col;
}

void bskPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      bskPrintC( page, col, ' ' );
    else
      bskPrintC( page, col, d1 + '0' );
    bskPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void bskPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        bskPrintC( page, col, ' ' );
        if( d2 == 0 )
          bskPrintC( page, col + 1, ' ' );
        else
          bskPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        bskPrintC( page, col, d1 + '0' );
        bskPrintC( page, col + 1, d2 + '0' );
    }
    bskPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void bskPrintNumber5( int page, int col, int w )
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
          bskPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            bskPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    bskPrintC( page, col + 4, rem + '0' );
}

void bskPrintScore()
{
    // upstream tracks Score as a x10 value and always shows one extra
    // trailing '0' digit - preserved exactly (see header comment). Real
    // upstream column is (LeftX+2)*VramStep for the 5-digit value (relative
    // column 2, not 0) with the trailing '0' immediately after it at
    // relative column 7 - re-verified directly against Status.cpp's own
    // PrintScore()/Print.cpp's VramStep math (a bug found during a later
    // verification pass - see header comment).
    bskPrintNumber5( 1, 2, bskScore );
    bskPrintC( 1, 7, '0' );
}

void bskPrintTime()
{
    bskPrintByteNumber3( 5, 5, bskStageTime );
}

void bskPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };

    bskPrintS( 0, 0, sScore, 5 );
    bskPrintS( 3, 0, sStage, 5 );
    bskPrintByteNumber2( 3, 6, bskCurrentStage + 1 );
    bskPrintS( 5, 0, sTime, 4 );

    // Simplified lives readout (a single digit) - matches Cracky's own
    // precedent for this exact upstream icon-row display; the ASCII font
    // has no L to spell "LIVES" with anyway.
    if( bskRemainCount > 0 )
      bskPrintC( 7, 0, bskRemainCount + '0' );
    else
      bskPrintC( 7, 0, ' ' );

    bskPrintScore();
    bskPrintTime();
}

void bskAddScore( int pts )
{
    bskScore = bskScore + pts;
    bskPrintScore();
}

void bskClearOverlays()
{
    int i;
    for( i = 0; i < BSK_OVERLAY_COUNT; i = i + 1 )
      bskOverlayActive[ i ] = 0;
}

void bskSetOverlay( int slot, int page, int col, char* text, int len )
{
    int i;
    bskOverlayActive[ slot ] = 1;
    bskOverlayPage[ slot ] = page;
    bskOverlayCol[ slot ] = col;
    bskOverlayLen[ slot ] = len;
    for( i = 0; i < len; i = i + 1 )
      bskOverlayText[ slot ][ i ] = text[ i ];
}

void bskPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    bskClearOverlays();
    bskSetOverlay( 0, 4, 8, s, 9 );
}

void bskPrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    bskClearOverlays();
    bskSetOverlay( 0, 4, 9, s, 7 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int bskMelodyLength( int id )
{
    if( id == BSK_MELODY_LOOSE ) return 3;
    if( id == BSK_MELODY_HIT ) return 17;
    if( id == BSK_MELODY_BEEP ) return 3;
    if( id == BSK_MELODY_PUSH ) return 13;
    if( id == BSK_MELODY_START ) return 23;
    if( id == BSK_MELODY_CLEAR ) return 29;
    if( id == BSK_MELODY_GAMEOVER ) return 21;
    if( id == BSK_MELODY_BGM1 ) return 119;
    if( id == BSK_MELODY_BGM2 ) return 173;
    return 0;
}

int bskMelodyValue( int id, int idx )
{
    if( id == BSK_MELODY_LOOSE ) return bskMelodyLoose[ idx ];
    if( id == BSK_MELODY_HIT ) return bskMelodyHit[ idx ];
    if( id == BSK_MELODY_BEEP ) return bskMelodyBeep[ idx ];
    if( id == BSK_MELODY_PUSH ) return bskMelodyPush[ idx ];
    if( id == BSK_MELODY_START ) return bskMelodyStart[ idx ];
    if( id == BSK_MELODY_CLEAR ) return bskMelodyClear[ idx ];
    if( id == BSK_MELODY_GAMEOVER ) return bskMelodyGameOver[ idx ];
    if( id == BSK_MELODY_BGM1 ) return bskMelodyBgm1[ idx ];
    if( id == BSK_MELODY_BGM2 ) return bskMelodyBgm2[ idx ];
    return 0;
}

int bskNoteFrames( int length )
{
    return (int)( length * BSK_NOTE_UNIT_FRAMES + 0.5 );
}

void bskStartSeq( int channel, int melodyId )
{
    bskSeqMelody[ channel ] = melodyId;
    bskSeqPos[ channel ] = 0;
    bskSeqWait[ channel ] = 0;
    bskSeqActive[ channel ] = 1;
}

void bskStopSeq( int channel )
{
    bskSeqActive[ channel ] = 0;
    bskSeqMelody[ channel ] = BSK_MELODY_NONE;
}

bool bskSeqPlaying( int channel )
{
    return bskSeqActive[ channel ] != 0;
}

void bskAdvanceOneSeq( int channel )
{
    int length, note;

    if( bskSeqActive[ channel ] == 0 ) return;

    if( bskSeqWait[ channel ] > 0 )
    {
        bskSeqWait[ channel ] = bskSeqWait[ channel ] - 1;
        return;
    }

    length = bskMelodyValue( bskSeqMelody[ channel ], bskSeqPos[ channel ] );
    if( length == 0 )
    {
        bskStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        bskSeqPos[ channel ] = 0;
        length = bskMelodyValue( bskSeqMelody[ channel ], 0 );
    }
    note = bskMelodyValue( bskSeqMelody[ channel ], bskSeqPos[ channel ] + 1 );
    bskSeqPos[ channel ] = bskSeqPos[ channel ] + 2;
    bskSeqWait[ channel ] = bskNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)bskFrequencies[ note - 1 ], (float)bskSeqWait[ channel ] / 60.0 );
}

void bskAdvanceSound()
{
    bskAdvanceOneSeq( 0 );
    bskAdvanceOneSeq( 1 );
    bskAdvanceOneSeq( 2 );
}

void bskStartBgm()
{
    bskStartSeq( 1, BSK_MELODY_BGM1 );
    bskStartSeq( 2, BSK_MELODY_BGM2 );
}

void bskStopBgm()
{
    bskStopSeq( 1 );
    bskStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   VVram.cpp helpers
// -----------------------------------------------------------------------------

// Both of these were originally written taking a grid parameter, but every
// real call site always passes bskVVramBack - the only grid that ever
// exists - and this dialect can't pass a 2D array as a function argument
// (size > 1) anyway, so they operate directly on the global instead.
void bskVPut2S( int x, int y, int* chars )
{
    bskVVramBack[ y ][ x ] = chars[ 0 ];
    bskVVramBack[ y ][ x + 1 ] = chars[ 1 ];
    bskVVramBack[ y + 1 ][ x ] = chars[ 2 ];
    bskVVramBack[ y + 1 ][ x + 1 ] = chars[ 3 ];
}

void bskVPut2C( int x, int y, int c )
{
    bskVVramBack[ y ][ x ] = c; c = c + 1;
    bskVVramBack[ y ][ x + 1 ] = c; c = c + 1;
    bskVVramBack[ y + 1 ][ x ] = c; c = c + 1;
    bskVVramBack[ y + 1 ][ x + 1 ] = c;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int bskGetCell( int x, int y )
{
    int idx, xMod, b;
    // explicit negative-coordinate guard - see header comment (byte-
    // truncation-reliance bug family).
    if( x < 0 || x >= BSK_MAP_WIDTH || y < 0 || y >= BSK_MAP_HEIGHT )
      return BSK_CELL_WALL;
    idx = ( y * ( BSK_MAP_WIDTH / 4 ) ) + ( x >> 2 );
    b = bskCellMap[ idx ];
    xMod = x & 3;
    while( xMod != 0 )
    {
        b = b >> 2;
        xMod = xMod - 1;
    }
    return b & BSK_CELL_MASK;
}

void bskSetCell( int x, int y, int cell )
{
    int idx, mask, shifted, yy;
    int chars[4];
    int cellVal;

    cellVal = cell;
    if( cellVal > 2 ) cellVal = 2;

    yy = y << 1;
    chars[ 0 ] = bskCellChars[ ( cellVal << 2 ) + 0 ];
    chars[ 1 ] = bskCellChars[ ( cellVal << 2 ) + 1 ];
    chars[ 2 ] = bskCellChars[ ( cellVal << 2 ) + 2 ];
    chars[ 3 ] = bskCellChars[ ( cellVal << 2 ) + 3 ];
    bskVPut2S( x << 1, yy + BSK_STAGE_TOP, chars );

    idx = ( y * ( BSK_MAP_WIDTH / 4 ) ) + ( x >> 2 );
    mask = 0xfc;
    shifted = cell;
    xMod_loop:
    {
        int xMod;
        xMod = x & 3;
        while( xMod != 0 )
        {
            shifted = shifted << 2;
            mask = ( mask << 2 ) | 3;
            xMod = xMod - 1;
        }
    }
    bskCellMap[ idx ] = ( bskCellMap[ idx ] & mask ) | shifted;
}

void bskDrawFence()
{
    int i;
    for( i = 0; i < BSK_VVRAM_WIDTH; i = i + 1 )
    {
        bskVVramBack[ 0 ][ i ] = BSK_CHAR_FENCE;
        bskVVramBack[ BSK_VVRAM_HEIGHT - 1 ][ i ] = BSK_CHAR_FENCE + 1;
    }
}


// -----------------------------------------------------------------------------
//   Movable.cpp - basics (no cross-object dependency)
// -----------------------------------------------------------------------------

void bskLocateMovableB( BskMovable* pMovable, int b )
{
    pMovable->x = ( b >> 3 ) & 0xfe;
    pMovable->y = ( b & 15 ) << 1;
}

bool bskIsOnGrid( BskMovable* pMovable )
{
    return ( ( pMovable->x | pMovable->y ) & BSK_MAP_MASK ) == 0;
}

void bskSetDirection( BskMovable* pMovable, int direction )
{
    pMovable->status = ( pMovable->status & ~BSK_MOVABLE_DIRECTION_MASK ) | direction;
    pMovable->dx = bskDirectionElements[ direction ];
    pMovable->dy = bskDirectionElements[ direction + 1 ];
}

bool bskIsNear( BskMovable* p1, BskMovable* p2 )
{
    return
        p1->x + BSK_HIT_RANGE >= p2->x && p2->x + BSK_HIT_RANGE >= p1->x &&
        p1->y + BSK_HIT_RANGE >= p2->y && p2->y + BSK_HIT_RANGE >= p1->y;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void bskHideAllSprites()
{
    int i;
    for( i = 0; i < BSK_SPRITE_COUNT; i = i + 1 )
      bskSprites[ i ].code = BSK_INVALID_CODE;
}

void bskShowSprite( BskMovable* pMovable, int code )
{
    bskSprites[ pMovable->sprite ].x = pMovable->x;
    bskSprites[ pMovable->sprite ].y = pMovable->y + BSK_STAGE_TOP;
    bskSprites[ pMovable->sprite ].code = code;
}

void bskHideSprite( int index )
{
    bskSprites[ index ].code = BSK_INVALID_CODE;
}

void bskDrawSprites()
{
    int i, x, y, c;
    for( i = 0; i < BSK_SPRITE_COUNT; i = i + 1 )
    {
        if( bskSprites[ i ].code != BSK_INVALID_CODE )
        {
            x = bskSprites[ i ].x;
            y = bskSprites[ i ].y;
            c = bskSprites[ i ].code;
            bskVVramFront[ y ][ x ] = c; c = c + 1;
            bskVVramFront[ y ][ x + 1 ] = c; c = c + 1;
            bskVVramFront[ y + 1 ][ x ] = c; c = c + 1;
            bskVVramFront[ y + 1 ][ x + 1 ] = c;
        }
    }
}

void bskVVramBackToFront()
{
    int i, j;
    for( i = 0; i < BSK_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < BSK_VVRAM_WIDTH; j = j + 1 )
        bskVVramFront[ i ][ j ] = bskVVramBack[ i ][ j ];
}

void bskDrawAll()
{
    bskVVramBackToFront();
    bskDrawSprites();
}


// -----------------------------------------------------------------------------
//   IsNearX helpers (each only needs its own global array + bskIsOnGrid) -
//   defined here, ahead of the cross-object Movable functions that need
//   them, per this file's own no-forward-declaration ordering rule.
// -----------------------------------------------------------------------------

bool bskIsNearMan( BskMovable* pMovable, int x, int y )
{
    if( pMovable != &bskMan )
    {
        int thisX, thisY;
        thisX = bskMan.x >> BSK_MAP_SHIFT;
        thisY = bskMan.y >> BSK_MAP_SHIFT;
        if( x == thisX && y == thisY ) return true;
        if( !bskIsOnGrid( &bskMan ) )
        {
            thisX = thisX + bskMan.dx;
            thisY = thisY + bskMan.dy;
            if( x == thisX && y == thisY ) return true;
        }
    }
    return false;
}

bool bskIsNearMovingBlock( BskMovable* pMovable, int x, int y )
{
    int i;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        if( ( bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK ) != BSK_BLOCK_STATUS_MOVE ) continue;
        if( pMovable != &bskBlocks[ i ] )
        {
            int thisX, thisY;
            thisX = bskBlocks[ i ].x >> BSK_MAP_SHIFT;
            thisY = bskBlocks[ i ].y >> BSK_MAP_SHIFT;
            if( x == thisX && y == thisY ) return true;
            if( !bskIsOnGrid( &bskBlocks[ i ] ) )
            {
                thisX = thisX + bskBlocks[ i ].dx;
                thisY = thisY + bskBlocks[ i ].dy;
                if( x == thisX && y == thisY ) return true;
            }
        }
    }
    return false;
}

bool bskIsNearMonster( BskMovable* pMovable, int x, int y )
{
    int i;
    for( i = 0; i < BSK_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( bskMonsters[ i ].m.status & BSK_MOVABLE_LIVE ) == 0 ) continue;
        if( pMovable != &bskMonsters[ i ].m )
        {
            int thisX, thisY;
            thisX = bskMonsters[ i ].m.x >> BSK_MAP_SHIFT;
            thisY = bskMonsters[ i ].m.y >> BSK_MAP_SHIFT;
            if( x == thisX && y == thisY ) return true;
            if( !bskIsOnGrid( &bskMonsters[ i ].m ) )
            {
                thisX = thisX + bskMonsters[ i ].m.dx;
                thisY = thisY + bskMonsters[ i ].m.dy;
                if( x == thisX && y == thisY ) return true;
            }
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Movable.cpp - cross-object lookups
// -----------------------------------------------------------------------------

int bskGetCellOrMovable( BskMovable* pMovable, int x, int y )
{
    int cell;
    cell = bskGetCell( x, y );
    if( cell == BSK_CELL_NONE )
    {
        if( bskIsNearMan( pMovable, x, y ) ) return BSK_CELL_MAN;
        if( bskIsNearMovingBlock( pMovable, x, y ) ) return BSK_CELL_MOVING_BLOCK;
        if( bskIsNearMonster( pMovable, x, y ) ) return BSK_CELL_MONSTER;
    }
    return cell;
}

int bskNextCellOrMovable( BskMovable* pMovable, int direction )
{
    int x, y;
    x = ( pMovable->x >> BSK_MAP_SHIFT ) + bskDirectionElements[ direction ];
    y = ( pMovable->y >> BSK_MAP_SHIFT ) + bskDirectionElements[ direction + 1 ];
    return bskGetCellOrMovable( pMovable, x, y );
}

int bskGetCellOrBlock( BskMovable* pMovable, int x, int y )
{
    int cell;
    cell = bskGetCell( x, y );
    if( cell == BSK_CELL_NONE )
    {
        if( bskIsNearMovingBlock( pMovable, x, y ) ) return BSK_CELL_MOVING_BLOCK;
    }
    return cell;
}


// -----------------------------------------------------------------------------
//   Block.cpp - part 1 (no dependency on Man/Monster function bodies, only
//   needs the shared Stage/Movable/Sprite machinery already defined above).
// -----------------------------------------------------------------------------

void bskShowBlockDestroy( BskMovable* pBlock )
{
    int c;
    c = BSK_CHAR_BLOCK + 4 + ( ( pBlock->clock >> 1 ) & 0xfc );
    bskVPut2C( pBlock->x, pBlock->y + BSK_STAGE_TOP, c );
}

void bskShowBlockMove( BskMovable* pBlock )
{
    bskShowSprite( pBlock, BSK_CHAR_BLOCK );
}

void bskShowBlockStart( BskMovable* pBlock )
{
    int c;
    c = BSK_CHAR_BLOCK + 20 + ( ( pBlock->clock >> 3 ) & 0xfc );
    bskVPut2C( pBlock->x, pBlock->y + BSK_STAGE_TOP, c );
}

void bskDestroyBlock( int x, int y )
{
    int i;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        if( ( bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK ) == BSK_BLOCK_STATUS_NONE )
        {
            bskBlocks[ i ].status = BSK_BLOCK_STATUS_DESTROY;
            bskBlocks[ i ].x = x;
            bskBlocks[ i ].y = y;
            bskBlocks[ i ].clock = 0;
            bskSetCell( x >> 1, y >> 1, BSK_CELL_WALL );
            bskShowBlockDestroy( &bskBlocks[ i ] );
            return;
        }
    }
}

void bskMoveBlock( int x, int y, int dx, int dy, int flag )
{
    int i;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        if( ( bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK ) == BSK_BLOCK_STATUS_NONE )
        {
            bskBlocks[ i ].status = BSK_BLOCK_STATUS_MOVE | flag;
            bskBlocks[ i ].x = x;
            bskBlocks[ i ].y = y;
            bskBlocks[ i ].dx = dx;
            bskBlocks[ i ].dy = dy;
            bskBlocks[ i ].clock = 0;
            bskBlocks[ i ].moveCount = 0;
            bskSetCell( x >> BSK_MAP_SHIFT, y >> BSK_MAP_SHIFT, BSK_CELL_NONE );
            bskShowBlockMove( &bskBlocks[ i ] );
            return;
        }
    }
}

bool bskPushBlock( BskMovable* pMovable )
{
    if( ( pMovable->status & BSK_MOVABLE_PUSHING ) == 0 )
    {
        int direction, cell, x, y, dx, dy;
        direction = pMovable->status & BSK_MOVABLE_DIRECTION_MASK;
        dx = bskDirectionElements[ direction ];
        dy = bskDirectionElements[ direction + 1 ];
        x = pMovable->x + dx + dx;
        y = pMovable->y + dy + dy;
        cell = bskGetCell( bskSafeShiftR1( x ), bskSafeShiftR1( y ) );
        if( cell == BSK_CELL_BLOCK )
        {
            int nx, ny, mansFlag;
            nx = bskSafeShiftR1( x ) + dx;
            ny = bskSafeShiftR1( y ) + dy;
            if( bskGetCell( nx, ny ) == BSK_CELL_NONE )
            {
                if( pMovable == &bskMan )
                  mansFlag = BSK_BLOCK_MANS;
                else
                  mansFlag = 0;
                bskMoveBlock( x, y, dx, dy, mansFlag );
            }
            else
              bskDestroyBlock( x, y );
            pMovable->status = pMovable->status | BSK_MOVABLE_PUSHING;
            return true;
        }
    }
    return false;
}

void bskStartBlock()
{
    bskBlockStartCount = bskBlockStartCount + 1;
    if( bskBlockStartCount < 64 ) return;
    bskBlockStartCount = 0;
    if( bskGetCellOrMovable( (BskMovable*)0, bskBlockStartX, bskBlockStartY ) == BSK_CELL_NONE )
    {
        int i;
        for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
        {
            if( ( bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK ) == BSK_BLOCK_STATUS_NONE )
            {
                bskBlocks[ i ].status = BSK_BLOCK_STATUS_START;
                bskBlocks[ i ].x = bskBlockStartX << 1;
                bskBlocks[ i ].y = bskBlockStartY << 1;
                bskBlocks[ i ].clock = 0;
                bskSetCell( bskBlockStartX, bskBlockStartY, BSK_CELL_WALL );
                bskShowBlockStart( &bskBlocks[ i ] );
                i = BSK_BLOCK_COUNT;
            }
        }
    }
    bskBlockStartX = bskBlockStartX + 1;
    if( bskBlockStartX >= BSK_MAP_WIDTH - 1 )
    {
        bskBlockStartX = 1;
        bskBlockStartY = bskBlockStartY + 1;
        if( bskBlockStartY >= BSK_MAP_HEIGHT - 1 )
          bskBlockStartY = 1;
    }
}

void bskInitBlocks()
{
    int i, sprite;
    sprite = BSK_SPRITE_BLOCK;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        bskBlocks[ i ].sprite = sprite;
        bskBlocks[ i ].status = BSK_BLOCK_STATUS_NONE;
        sprite = sprite + 1;
    }
    bskBlockStartCount = 0;
    bskBlockStartX = 1;
    bskBlockStartY = 1;
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void bskShowMan()
{
    int c;
    c = ( ( bskMan.status & BSK_MOVABLE_PATTERN_MASK ) << 2 ) + BSK_CHAR_MAN;
    bskShowSprite( &bskMan, c );
}

void bskInitMan()
{
    bskMan.sprite = BSK_SPRITE_MAN;
    bskMan.status = BSK_MOVABLE_LIVE | BSK_DIRECTION_RIGHT;
    bskMan.dx = 0;
    bskMan.dy = 0;
    bskMan.moveCount = 0;
    bskLocateMovableB( &bskMan, bskStageStart[ bskStageIndex ] );
    bskShowMan();
}

void bskMoveMan()
{
    if( ( bskMan.status & BSK_MOVABLE_PUSHING ) != 0 )
    {
        bskMan.clock = bskMan.clock + 1;
        if( bskMan.clock >= 1 )
          bskMan.status = bskMan.status & ~BSK_MOVABLE_PUSHING;
    }
    if( bskIsOnGrid( &bskMan ) )
    {
        bool left, right, up, down, fire;
        int direction, cell, moveNow, oldDirection;

        left = isLeftPressed();
        right = isRightPressed();
        up = isUpPressed();
        down = isDownPressed();
        fire = isFirePressed();

        direction = -1;
        if( left ) direction = BSK_DIRECTION_LEFT;
        else if( right ) direction = BSK_DIRECTION_RIGHT;
        else if( up ) direction = BSK_DIRECTION_UP;
        else if( down ) direction = BSK_DIRECTION_DOWN;

        moveNow = 0;
        if( direction >= 0 )
        {
            cell = bskNextCellOrMovable( &bskMan, direction );
            if( cell >= BSK_CELL_WALL )
            {
                oldDirection = bskMan.status & BSK_MOVABLE_DIRECTION_MASK;
                cell = bskNextCellOrMovable( &bskMan, oldDirection );
                if( cell == BSK_CELL_NONE )
                  moveNow = 1;
                else
                  bskSetDirection( &bskMan, direction );
            }
            else
            {
                bskSetDirection( &bskMan, direction );
                if( cell == BSK_CELL_NONE )
                  moveNow = 1;
            }
        }
        if( !moveNow )
        {
            bskMan.dx = 0;
            bskMan.dy = 0;
        }

        if( fire )
        {
            if( bskPushBlock( &bskMan ) )
            {
                bskMan.clock = 0;
                bskAddScore( 1 );
                bskStartSeq( 0, BSK_MELODY_PUSH );
            }
        }
    }

    bskMan.moveCount = bskMan.moveCount + 1;
    if( ( bskMan.moveCount & BSK_MOVE_COUNT_MASK ) == 0 )
    {
        int seq;
        bskMan.x = bskMan.x + bskMan.dx;
        bskMan.y = bskMan.y + bskMan.dy;
        seq = ( bskMan.x + bskMan.y ) & 1;
        bskMan.status = ( bskMan.status & ~BSK_MOVABLE_SEQ_MASK ) | seq;
    }
    bskShowMan();
}

bool bskHitMan( BskMovable* pBlock )
{
    if( ( bskMan.status & BSK_MOVABLE_LIVE ) != 0 && bskIsNear( pBlock, &bskMan ) )
    {
        bskStartSeq( 0, BSK_MELODY_LOOSE );
        bskMan.status = bskMan.status & ~BSK_MOVABLE_LIVE;
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void bskInitPoints()
{
    int i, sprite;
    sprite = BSK_SPRITE_POINT;
    for( i = 0; i < BSK_POINT_COUNT; i = i + 1 )
    {
        bskPoints[ i ].status = 0;
        bskPoints[ i ].sprite = sprite;
        bskHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void bskStartPoint( int x, int y, int rate )
{
    int i, clampedRate;
    clampedRate = rate;
    if( clampedRate > 3 ) clampedRate = 3;   // see header (OOB-guard)
    bskAddScore( bskPointValues[ clampedRate ] );
    for( i = 0; i < BSK_POINT_COUNT; i = i + 1 )
    {
        if( ( bskPoints[ i ].status & BSK_MOVABLE_LIVE ) != 0 ) continue;
        bskPoints[ i ].status = bskPoints[ i ].status | BSK_MOVABLE_LIVE;
        bskPoints[ i ].x = x;
        bskPoints[ i ].y = y;
        bskPoints[ i ].clock = BSK_POINT_TIME;
        bskShowSprite( &bskPoints[ i ], BSK_CHAR_POINT + ( clampedRate << 2 ) );
        return;
    }
}

void bskUpdatePoints()
{
    int i;
    for( i = 0; i < BSK_POINT_COUNT; i = i + 1 )
    {
        if( ( bskPoints[ i ].status & BSK_MOVABLE_LIVE ) == 0 ) continue;
        if( bskPoints[ i ].clock == 0 )
        {
            bskPoints[ i ].status = 0;
            bskHideSprite( bskPoints[ i ].sprite );
        }
        else
          bskPoints[ i ].clock = bskPoints[ i ].clock - 1;
    }
}


// -----------------------------------------------------------------------------
//   Block.cpp - OnHitBlock (needs bskStartPoint, defined above)
// -----------------------------------------------------------------------------

void bskOnHitBlock( BskMovable* pBlock, BskMovable* pMonster )
{
    if( ( pBlock->status & BSK_BLOCK_MANS ) != 0 )
    {
        int rate;
        rate = pBlock->clock;
        bskStartPoint( pMonster->x, pMonster->y, rate );
        if( rate < BSK_BLOCK_MAX_RATE + 1 )
        {
            rate = rate + 1;
            pBlock->clock = rate;
        }
        bskDrawAll();
        bskStartSeq( 0, BSK_MELODY_HIT );
    }
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void bskShowMonster( BskMonster* pMonster )
{
    int c;
    c = ( ( pMonster->m.status & BSK_MOVABLE_PATTERN_MASK ) << 2 ) + BSK_CHAR_MONSTER;
    bskShowSprite( &pMonster->m, c );
}

void bskDecideTarget( BskMonster* pMonster )
{
    if( pMonster->m.x == bskMan.x || pMonster->m.y == bskMan.y )
    {
        pMonster->targetX = bskMan.x;
        pMonster->targetY = bskMan.y;
        return;
    }
    {
        int thisX, thisY, manX, manY, minX, minY, minDistance, i;
        thisX = pMonster->m.x >> BSK_MAP_SHIFT;
        thisY = pMonster->m.y >> BSK_MAP_SHIFT;
        manX = bskMan.x >> BSK_MAP_SHIFT;
        manY = bskMan.y >> BSK_MAP_SHIFT;
        minX = 0; minY = 0;
        minDistance = 0xff;
        for( i = 0; i < 4; i = i + 1 )
        {
            int dx, dy, x, y, cell;
            dx = bskDirectionElements[ i * 2 ];
            dy = bskDirectionElements[ i * 2 + 1 ];
            x = manX;
            y = manY;
            cell = BSK_CELL_NONE;
            while( cell == BSK_CELL_NONE )
            {
                x = x + dx;
                y = y + dy;
                cell = bskGetCell( x, y );
            }
            if( cell < BSK_CELL_WALL )
            {
                int distance;
                x = x + dx;
                y = y + dy;
                cell = bskGetCell( x, y );
                if( cell < BSK_CELL_WALL )
                {
                    distance = bskAbs( manX, thisX ) + bskAbs( manY, thisY );
                    if( distance < minDistance )
                    {
                        minDistance = distance;
                        minX = x;
                        minY = y;
                    }
                }
            }
        }
        if( minDistance == 0xff )
        {
            pMonster->targetX = bskMan.x;
            pMonster->targetY = bskMan.y;
        }
        else
        {
            pMonster->targetX = minX << BSK_MAP_SHIFT;
            pMonster->targetY = minY << BSK_MAP_SHIFT;
        }
    }
}

void bskDecideDirection( BskMonster* pMonster )
{
    int directions[4];

    if( pMonster->targetX != pMonster->m.x || pMonster->targetY != pMonster->m.y )
    {
        if( bskAbs( pMonster->targetX, pMonster->m.x ) > bskAbs( pMonster->targetY, pMonster->m.y ) )
        {
            int verticalIdx;
            if( pMonster->targetX < pMonster->m.x )
            {
                if( pMonster->m.dx <= 0 )
                {
                    directions[ 0 ] = BSK_DIRECTION_LEFT;
                    directions[ 3 ] = BSK_DIRECTION_RIGHT;
                    verticalIdx = 1;
                }
                else
                {
                    directions[ 2 ] = BSK_DIRECTION_RIGHT;
                    directions[ 3 ] = BSK_DIRECTION_LEFT;
                    verticalIdx = 0;
                }
            }
            else
            {
                if( pMonster->m.dx >= 0 )
                {
                    directions[ 0 ] = BSK_DIRECTION_RIGHT;
                    directions[ 3 ] = BSK_DIRECTION_LEFT;
                    verticalIdx = 1;
                }
                else
                {
                    directions[ 2 ] = BSK_DIRECTION_LEFT;
                    directions[ 3 ] = BSK_DIRECTION_RIGHT;
                    verticalIdx = 0;
                }
            }
            if( ( pMonster->targetY < pMonster->m.y && pMonster->m.dy <= 0 ) || pMonster->m.dy < 0 )
            {
                directions[ verticalIdx ] = BSK_DIRECTION_UP;
                verticalIdx = verticalIdx + 1;
                directions[ verticalIdx ] = BSK_DIRECTION_DOWN;
            }
            else
            {
                directions[ verticalIdx ] = BSK_DIRECTION_DOWN;
                verticalIdx = verticalIdx + 1;
                directions[ verticalIdx ] = BSK_DIRECTION_UP;
            }
        }
        else
        {
            int horizontalIdx;
            if( pMonster->targetY < pMonster->m.y )
            {
                if( pMonster->m.dy <= 0 )
                {
                    directions[ 0 ] = BSK_DIRECTION_UP;
                    directions[ 3 ] = BSK_DIRECTION_DOWN;
                    horizontalIdx = 1;
                }
                else
                {
                    directions[ 2 ] = BSK_DIRECTION_DOWN;
                    directions[ 3 ] = BSK_DIRECTION_UP;
                    horizontalIdx = 0;
                }
            }
            else
            {
                if( pMonster->m.dy >= 0 )
                {
                    directions[ 0 ] = BSK_DIRECTION_DOWN;
                    directions[ 3 ] = BSK_DIRECTION_UP;
                    horizontalIdx = 1;
                }
                else
                {
                    directions[ 2 ] = BSK_DIRECTION_UP;
                    directions[ 3 ] = BSK_DIRECTION_DOWN;
                    horizontalIdx = 0;
                }
            }
            // upstream compares `targetX < pMonster->y` here too (a real
            // upstream quirk, matching the identical one already found and
            // preserved in Cracky's own crkDecideDirection - see header).
            if( ( pMonster->targetX < pMonster->m.y && pMonster->m.dx <= 0 ) || pMonster->m.dx < 0 )
            {
                directions[ horizontalIdx ] = BSK_DIRECTION_LEFT;
                horizontalIdx = horizontalIdx + 1;
                directions[ horizontalIdx ] = BSK_DIRECTION_RIGHT;
            }
            else
            {
                directions[ horizontalIdx ] = BSK_DIRECTION_RIGHT;
                horizontalIdx = horizontalIdx + 1;
                directions[ horizontalIdx ] = BSK_DIRECTION_LEFT;
            }
        }
        {
            int i;
            for( i = 0; i < 4; i = i + 1 )
            {
                int direction, cell;
                direction = directions[ i ];
                cell = bskNextCellOrMovable( &pMonster->m, direction );
                if( cell < BSK_CELL_WALL )
                {
                    bskSetDirection( &pMonster->m, direction );
                    return;
                }
            }
        }
    }
    pMonster->m.dx = 0;
    pMonster->m.dy = 0;
}

void bskInitMonsters()
{
    int i, sprite, count;
    count = bskStageEnemyCount[ bskStageIndex ];
    sprite = BSK_SPRITE_MONSTER;
    for( i = 0; i < count; i = i + 1 )
    {
        bskMonsters[ i ].m.status = BSK_MOVABLE_LIVE;
        bskMonsters[ i ].m.sprite = sprite;
        bskMonsters[ i ].m.moveCount = 0;
        sprite = sprite + 1;
    }
    for( i = count; i < BSK_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        bskMonsters[ i ].m.status = 0;
        bskMonsters[ i ].m.sprite = sprite;
        bskHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void bskStartMonsters()
{
    int i;
    bskMonsterCount = 0;
    for( i = 0; i < BSK_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( bskMonsters[ i ].m.status & BSK_MOVABLE_LIVE ) != 0 )
        {
            bskMonsters[ i ].m.status = BSK_MOVABLE_LIVE;
            bskMonsters[ i ].m.clock = 0;
            bskLocateMovableB( &bskMonsters[ i ].m, bskStageEnemies[ bskStageIndex ][ i ] );
            bskDecideTarget( &bskMonsters[ i ] );
            bskDecideDirection( &bskMonsters[ i ] );
            bskShowMonster( &bskMonsters[ i ] );
            bskMonsterCount = bskMonsterCount + 1;
        }
    }
}

void bskMoveMonsters()
{
    int i;
    for( i = 0; i < BSK_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( bskMonsters[ i ].m.status & BSK_MOVABLE_LIVE ) == 0 ) continue;

        if( ( bskMonsters[ i ].m.status & BSK_MOVABLE_PUSHING ) != 0 )
        {
            bskMonsters[ i ].m.clock = bskMonsters[ i ].m.clock + 1;
            if( bskMonsters[ i ].m.clock >= 1 )
              bskMonsters[ i ].m.status = bskMonsters[ i ].m.status & ~BSK_MOVABLE_PUSHING;
            continue;
        }

        if( ( bskMonsters[ i ].m.clock & BSK_INTERVAL_MASK ) == 0 &&
            ( ( ( bskMonsters[ i ].m.clock >> BSK_INTERVAL_SHIFT ) & 0x0f ) <= bskCurrentStage ||
              ( bskMonsters[ i ].targetX == bskMonsters[ i ].m.x && bskMonsters[ i ].targetY == bskMonsters[ i ].m.y ) ) )
          bskDecideTarget( &bskMonsters[ i ] );

        if( bskIsOnGrid( &bskMonsters[ i ].m ) )
        {
            int cell;
            if( ( bskMonsters[ i ].m.clock & BSK_MAP_MASK ) == 0 )
              bskDecideDirection( &bskMonsters[ i ] );
            cell = bskNextCellOrMovable( &bskMonsters[ i ].m, bskMonsters[ i ].m.status & BSK_MOVABLE_DIRECTION_MASK );
            if( cell != BSK_CELL_NONE )
            {
                if( cell == BSK_CELL_BLOCK )
                {
                    if( bskPushBlock( &bskMonsters[ i ].m ) )
                      bskMonsters[ i ].m.clock = 0;
                }
                bskMonsters[ i ].m.dx = 0;
                bskMonsters[ i ].m.dy = 0;
            }
        }

        bskMonsters[ i ].m.moveCount = bskMonsters[ i ].m.moveCount + 1;
        if( ( bskMonsters[ i ].m.moveCount & BSK_MOVE_COUNT_MASK ) == 0 )
        {
            int seq;
            bskMonsters[ i ].m.x = bskMonsters[ i ].m.x + bskMonsters[ i ].m.dx;
            bskMonsters[ i ].m.y = bskMonsters[ i ].m.y + bskMonsters[ i ].m.dy;
            seq = ( bskMonsters[ i ].m.x + bskMonsters[ i ].m.y ) & 1;
            bskMonsters[ i ].m.status = ( bskMonsters[ i ].m.status & ~BSK_MOVABLE_SEQ_MASK ) | seq;
        }
        bskShowMonster( &bskMonsters[ i ] );
        bskMonsters[ i ].m.clock = bskMonsters[ i ].m.clock + 1;
    }
}

void bskHitMonster( BskMovable* pBlock )
{
    int i;
    for( i = 0; i < BSK_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( bskMonsters[ i ].m.status & BSK_MOVABLE_LIVE ) == 0 ) continue;
        if( bskIsNear( pBlock, &bskMonsters[ i ].m ) )
        {
            bskMonsters[ i ].m.status = bskMonsters[ i ].m.status & ~BSK_MOVABLE_LIVE;
            bskHideSprite( bskMonsters[ i ].m.sprite );
            bskOnHitBlock( pBlock, &bskMonsters[ i ].m );
            bskMonsterCount = bskMonsterCount - 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - orchestration (needs InitMonsters/InitMan/InitBlocks/
//   InitPoints, all now defined above)
// -----------------------------------------------------------------------------

void bskInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never stops the player from continuing past the last stage) -
    // preserved via the same wrap loop upstream uses, matching Cracky's
    // own identical precedent.
    int i, j;
    i = 0;
    j = 0;
    while( i < bskCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= BSK_STAGE_COUNT )
          j = 0;
    }
    bskStageIndex = j;
    bskInitMonsters();
}

void bskInitTrying()
{
    int i;

    bskStageTime = 60;
    i = bskStageEnemyCount[ bskStageIndex ];
    while( i != 0 )
    {
        bskStageTime = bskStageTime + 30;
        i = i - 1;
    }

    bskHideAllSprites();
    for( i = 0; i < BSK_VVRAM_HEIGHT; i = i + 1 )
    {
        int j;
        for( j = 0; j < BSK_VVRAM_WIDTH; j = j + 1 )
        {
            bskVVramBack[ i ][ j ] = BSK_CHAR_SPACE;
            bskVVramFront[ i ][ j ] = BSK_CHAR_SPACE;
        }
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 8; j = j + 1 )
          bskStatusChar[ i ][ j ] = 0;
    }
    bskClearOverlays();

    for( i = 0; i < BSK_MAP_SIZE; i = i + 1 )
      bskCellMap[ i ] = bskStageBytes[ bskStageIndex ][ i ];

    {
        int row, colGroup;
        for( row = 0; row < BSK_MAP_HEIGHT; row = row + 1 )
        {
            for( colGroup = 0; colGroup < BSK_MAP_WIDTH / 4; colGroup = colGroup + 1 )
            {
                int sourceByte, sub;
                sourceByte = bskCellMap[ row * ( BSK_MAP_WIDTH / 4 ) + colGroup ];
                for( sub = 0; sub < 4; sub = sub + 1 )
                {
                    int b, x, y;
                    int chars[4];
                    b = sourceByte & BSK_CELL_MASK;
                    sourceByte = sourceByte >> 2;
                    x = ( colGroup * 4 + sub ) * 2;
                    y = row * 2 + BSK_STAGE_TOP;
                    chars[ 0 ] = bskCellChars[ ( b << 2 ) + 0 ];
                    chars[ 1 ] = bskCellChars[ ( b << 2 ) + 1 ];
                    chars[ 2 ] = bskCellChars[ ( b << 2 ) + 2 ];
                    chars[ 3 ] = bskCellChars[ ( b << 2 ) + 3 ];
                    bskVPut2S( x, y, chars );
                }
            }
        }
    }

    bskInitMan();
    bskInitBlocks();
    bskStartMonsters();
    bskInitPoints();
    bskDrawFence();
    bskDrawAll();
    bskPrintStatus();
}


// -----------------------------------------------------------------------------
//   Block.cpp - part 2 (needs bskHitMan[Man]/bskHitMonster[Monster], both
//   defined above)
// -----------------------------------------------------------------------------

void bskUpdateBlocks()
{
    int i;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        int status;
        status = bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK;
        if( status == BSK_BLOCK_STATUS_NONE )
        {
            // nothing to do
        }
        else if( status == BSK_BLOCK_STATUS_DESTROY )
        {
            bskBlocks[ i ].clock = bskBlocks[ i ].clock + 1;
            if( bskBlocks[ i ].clock >= 4 * 8 )
            {
                bskBlocks[ i ].status = BSK_BLOCK_STATUS_NONE;
                bskSetCell( bskBlocks[ i ].x >> 1, bskBlocks[ i ].y >> 1, BSK_CELL_NONE );
            }
            else
              bskShowBlockDestroy( &bskBlocks[ i ] );
        }
        else if( status == BSK_BLOCK_STATUS_MOVE )
        {
            bool settled;
            settled = false;
            if( bskIsOnGrid( &bskBlocks[ i ] ) )
            {
                int x, y;
                x = bskBlocks[ i ].x >> BSK_MAP_SHIFT;
                y = bskBlocks[ i ].y >> BSK_MAP_SHIFT;
                if( bskGetCellOrBlock( &bskBlocks[ i ], x + bskBlocks[ i ].dx, y + bskBlocks[ i ].dy ) != BSK_CELL_NONE )
                {
                    bskSetCell( x, y, BSK_CELL_BLOCK );
                    bskBlocks[ i ].status = BSK_BLOCK_STATUS_NONE;
                    bskHideSprite( bskBlocks[ i ].sprite );
                    settled = true;
                }
            }
            if( !settled )
            {
                bskBlocks[ i ].moveCount = bskBlocks[ i ].moveCount + 1;
                if( ( bskBlocks[ i ].moveCount & BSK_MOVE_COUNT_MASK ) == 0 )
                {
                    bskBlocks[ i ].x = bskBlocks[ i ].x + bskBlocks[ i ].dx;
                    bskBlocks[ i ].y = bskBlocks[ i ].y + bskBlocks[ i ].dy;
                    bskShowBlockMove( &bskBlocks[ i ] );
                    bskHitMan( &bskBlocks[ i ] );
                    bskHitMonster( &bskBlocks[ i ] );
                }
            }
        }
        else if( status == BSK_BLOCK_STATUS_START )
        {
            bskBlocks[ i ].clock = bskBlocks[ i ].clock + 1;
            if( bskBlocks[ i ].clock >= 4 * 4 * 8 )
            {
                bskBlocks[ i ].status = BSK_BLOCK_STATUS_NONE;
                bskSetCell( bskBlocks[ i ].x >> 1, bskBlocks[ i ].y >> 1, BSK_CELL_BLOCK );
            }
            else
              bskShowBlockStart( &bskBlocks[ i ] );
        }
    }
}

bool bskAnyBlockMoving()
{
    int i;
    for( i = 0; i < BSK_BLOCK_COUNT; i = i + 1 )
    {
        if( ( bskBlocks[ i ].status & BSK_BLOCK_STATUS_MASK ) == BSK_BLOCK_STATUS_MOVE ) return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int bskComposeRawByte( int rawCol, int rawPage )
{
    if( rawCol < BSK_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = bskVVramFront[ rawPage * 2 ][ mapX ];
        lower = bskVVramFront[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = bskCharPattern[ upper * 2 + 0 ];
            lowerByte = bskCharPattern[ lower * 2 + 0 ];
            return ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        if( sub == 1 )
        {
            upperByte = bskCharPattern[ upper * 2 + 0 ];
            lowerByte = bskCharPattern[ lower * 2 + 0 ];
            return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        if( sub == 2 )
        {
            upperByte = bskCharPattern[ upper * 2 + 1 ];
            lowerByte = bskCharPattern[ lower * 2 + 1 ];
            return ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        upperByte = bskCharPattern[ upper * 2 + 1 ];
        lowerByte = bskCharPattern[ lower * 2 + 1 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    else
    {
        int statCol, charCol, sub, c;
        statCol = rawCol - BSK_VVRAM_WIDTH * 4;
        if( statCol >= 32 ) return 0;
        charCol = statCol / 4;
        sub = statCol % 4;
        c = bskStatusChar[ rawPage ][ charCol ];
        return bskAsciiPattern[ c * 4 + sub ];
    }
}

void bskRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            int matched, i;
            matched = 0;
            for( i = 0; i < BSK_OVERLAY_COUNT; i = i + 1 )
            {
                if( bskOverlayActive[ i ] && page == bskOverlayPage[ i ] &&
                    col >= bskOverlayCol[ i ] * 4 && col < bskOverlayCol[ i ] * 4 + bskOverlayLen[ i ] * 4 )
                {
                    int idx, sub;
                    idx = ( col - bskOverlayCol[ i ] * 4 ) / 4;
                    sub = ( col - bskOverlayCol[ i ] * 4 ) % 4;
                    value = bskAsciiPattern[ bskAsciiIndex( bskOverlayText[ i ][ idx ] ) * 4 + sub ];
                    matched = 1;
                    i = BSK_OVERLAY_COUNT;
                }
            }
            if( !matched )
              value = bskComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void bskUpdateTitleCursorText()
{
    char sStart[6];
    char sContinue[9];

    if( bskSelection == 0 ) sStart[0] = '>'; else sStart[0] = ' ';
    sStart[1] = 'S'; sStart[2] = 'T'; sStart[3] = 'A'; sStart[4] = 'R'; sStart[5] = 'T';

    if( bskSelection == 1 ) sContinue[0] = '>'; else sContinue[0] = ' ';
    sContinue[1] = 'C'; sContinue[2] = 'O'; sContinue[3] = 'N'; sContinue[4] = 'T';
    sContinue[5] = 'I'; sContinue[6] = 'N'; sContinue[7] = 'U'; sContinue[8] = 'E';

    bskSetOverlay( 2, 5, 8, sStart, 6 );
    bskSetOverlay( 3, 6, 8, sContinue, 9 );
}

void bskBeginTitle()
{
    int i, j;

    for( i = 0; i < BSK_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < BSK_VVRAM_WIDTH; j = j + 1 )
        bskVVramFront[ i ][ j ] = BSK_CHAR_SPACE;

    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 8; j = j + 1 )
        bskStatusChar[ i ][ j ] = 0;

    bskHideAllSprites();
    // avoid a stale TIME value bleeding onto the title screen after a game
    // over - same fix already needed for Cracky's own title screen.
    bskStageTime = 0;
    bskClearOverlays();

    {
        int blockIdx, row, col, srcIdx;
        srcIdx = 0;
        for( blockIdx = 0; blockIdx < 6; blockIdx = blockIdx + 1 )
        {
            for( row = 0; row < 4; row = row + 1 )
            {
                for( col = 0; col < 4; col = col + 1 )
                {
                    bskVVramFront[ 2 + row ][ blockIdx * 4 + col ] = bskTitleBytes[ srcIdx ];
                    srcIdx = srcIdx + 1;
                }
            }
        }
    }

    {
        char sMini[4] = { 'M', 'I', 'N', 'I' };
        bskSetOverlay( 0, 3, 19, sMini, 4 );
    }
    {
        char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        bskSetOverlay( 1, 7, 12, sCredit, 12 );
    }

    bskSelection = 0;
    bskPrevLeft = 0; bskPrevRight = 0; bskPrevUp = 0; bskPrevDown = 0; bskPrevFire = 0;
    bskState = BSK_STATE_TITLE;
}

void bskUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !bskPrevLeft ) || ( right && !bskPrevRight ) ||
                ( up && !bskPrevUp ) || ( down && !bskPrevDown ) );
    justFire = ( fire && !bskPrevFire );
    bskPrevLeft = left; bskPrevRight = right; bskPrevUp = up; bskPrevDown = down; bskPrevFire = fire;

    if( justFire )
    {
        int selection;
        selection = bskSelection;
        bskScore = 0;
        if( selection == 0 )
          bskCurrentStage = 0;
        bskRemainCount = 3;
        bskInitStage();
        bskInitTrying();
        bskDrawAll();
        bskStartSeq( 1, BSK_MELODY_START );
        bskState = BSK_STATE_START_JINGLE;
        bskRender();
        return;
    }
    if( justDir )
      bskSelection = bskSelection ^ 1;

    bskUpdateTitleCursorText();
    bskRender();
}

void bskUpdateStartJingle()
{
    if( !bskSeqPlaying( 1 ) )
    {
        bskStartBgm();
        bskClock = 0;
        bskMonsterNum = 0;
        bskTimeDenom = BSK_MAX_TIME_DENOM;
        bskState = BSK_STATE_PLAYING;
    }
    bskRender();
}

void bskBeginLose()
{
    bskStopBgm();
    bskAnimStep = 0;
    bskWaitFrames = 0;
    bskState = BSK_STATE_LOSE_ANIM;
}

void bskUpdateLoseAnim()
{
    int patterns[4] = {
        BSK_CHAR_MAN + 2 * 4, BSK_CHAR_MAN + 4 * 4, BSK_CHAR_MAN + 0 * 4, BSK_CHAR_MAN + 6 * 4,
    };

    if( bskWaitFrames > 0 )
    {
        bskWaitFrames = bskWaitFrames - 1;
        bskRender();
        return;
    }

    bskShowSprite( &bskMan, patterns[ bskAnimStep & 3 ] );
    bskDrawAll();
    bskStartSeq( 0, BSK_MELODY_LOOSE );
    bskAnimStep = bskAnimStep + 1;
    bskWaitFrames = bskNoteFrames( 1 );

    if( bskAnimStep >= 8 )
    {
        bskRemainCount = bskRemainCount - 1;
        if( bskRemainCount > 0 )
        {
            bskInitTrying();
            bskDrawAll();
            bskStartSeq( 1, BSK_MELODY_START );
            bskState = BSK_STATE_START_JINGLE;
        }
        else
        {
            bskPrintGameOver();
            bskStartSeq( 1, BSK_MELODY_GAMEOVER );
            bskState = BSK_STATE_GAMEOVER_JINGLE;
        }
    }
    bskRender();
}

void bskUpdateGameOverJingle()
{
    if( !bskSeqPlaying( 1 ) )
      bskBeginTitle();
    else
      bskRender();
}

void bskBeginClearJingle()
{
    bskStopBgm();
    bskStartSeq( 1, BSK_MELODY_CLEAR );
    bskState = BSK_STATE_CLEAR_JINGLE;
}

void bskUpdateClearJingle()
{
    if( !bskSeqPlaying( 1 ) )
    {
        bskWaitFrames = 0;
        bskState = BSK_STATE_BONUS_TALLY;
    }
    bskRender();
}

void bskUpdateBonusTally()
{
    if( bskWaitFrames > 0 )
    {
        bskWaitFrames = bskWaitFrames - 1;
        bskRender();
        return;
    }

    if( bskStageTime >= BSK_BONUS_RATE )
    {
        bskAddScore( 1 );
        bskStageTime = bskStageTime - BSK_BONUS_RATE;
        bskPrintTime();
        bskStartSeq( 0, BSK_MELODY_BEEP );
        bskWaitFrames = bskNoteFrames( 1 );
        bskRender();
        return;
    }

    bskStageTime = 0;
    bskPrintStatus();
    bskCurrentStage = bskCurrentStage + 1;
    bskInitStage();
    bskInitTrying();
    bskDrawAll();
    bskStartSeq( 1, BSK_MELODY_START );
    bskState = BSK_STATE_START_JINGLE;
    bskRender();
}

// Reproduces Main()'s own real per-tick multi-rate structure - one call to
// this function corresponds to one full "batch of 8 Clock values" (matching
// upstream's own single real WaitTimer(3) pause per batch) - see the
// header comment's own detailed derivation of why this is the correct
// real-time-equivalent grouping.
void bskUpdatePlaying()
{
    int sub;
    bool timeUp;

    bskTickCounter = bskTickCounter + 1;
    if( bskTickCounter < BSK_TICK_DIVISOR )
    {
        bskRender();
        return;
    }
    bskTickCounter = 0;

    timeUp = false;
    for( sub = 0; sub < 8; sub = sub + 1 )
    {
        if( ( bskClock & 3 ) == 0 )
        {
            bskUpdatePoints();
            bskMoveMan();
            bskTimeDenom = bskTimeDenom - 1;
            if( bskTimeDenom == 0 )
            {
                bskStageTime = bskStageTime - 1;
                bskTimeDenom = BSK_MAX_TIME_DENOM;
                bskPrintTime();
                if( bskStageTime == 0 )
                {
                    bskPrintTimeUp();
                    timeUp = true;
                    sub = 8;   // break the loop, matching upstream's own goto lose
                }
            }
            if( !timeUp )
            {
                // Reproduces Main()'s own real `monsterNum` (sbyte) frame-
                // skip accumulator - upstream does NOT call MoveMonsters()
                // every time this Clock%4==0 gate fires; it throttles monster
                // movement to 3 calls out of every 5 gate-fires via this
                // +6/-10 accumulator (net -4 per call, +6 per skip - a real,
                // deliberate difficulty/balance choice, monsters visibly move
                // slower than the player). A prior version of this port
                // omitted this entirely, making monsters move at the SAME
                // rate as the player (~1.67x too fast) - see header comment.
                if( bskMonsterNum >= 0 )
                {
                    bskMoveMonsters();
                    bskMonsterNum = bskMonsterNum - 10;
                }
                bskMonsterNum = bskMonsterNum + 6;
            }
        }
        if( !timeUp )
        {
            if( ( bskClock & 0x1f ) == 0 )
              bskStartBlock();
            bskUpdateBlocks();
            bskClock = bskClock + 1;
            if( ( bskMan.status & BSK_MOVABLE_LIVE ) == 0 )
              sub = 8;   // break, matching upstream's own goto lose
            else if( bskMonsterCount == 0 && !bskAnyBlockMoving() )
              sub = 8;   // break, matching upstream's own loop-exit check
        }
    }

    bskDrawAll();

    if( timeUp || ( bskMan.status & BSK_MOVABLE_LIVE ) == 0 )
    {
        bskRender();
        bskBeginLose();
        return;
    }
    if( bskMonsterCount == 0 && !bskAnyBlockMoving() )
    {
        bskRender();
        bskBeginClearJingle();
        return;
    }
    bskRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameBootskell_init()
{
    int i;

    bskScore = 0;
    bskCurrentStage = 0;
    bskRemainCount = 3;
    bskStageTime = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        bskSeqActive[ i ] = 0;
        bskSeqMelody[ i ] = BSK_MELODY_NONE;
    }
    bskClearOverlays();
    bskTickCounter = 0;

    bskBeginTitle();
}

void gameBootskell_update()
{
    bskAdvanceSound();

    if( bskState == BSK_STATE_TITLE )
      bskUpdateTitle();
    else if( bskState == BSK_STATE_START_JINGLE )
      bskUpdateStartJingle();
    else if( bskState == BSK_STATE_PLAYING )
      bskUpdatePlaying();
    else if( bskState == BSK_STATE_LOSE_ANIM )
      bskUpdateLoseAnim();
    else if( bskState == BSK_STATE_GAMEOVER_JINGLE )
      bskUpdateGameOverJingle();
    else if( bskState == BSK_STATE_CLEAR_JINGLE )
      bskUpdateClearJingle();
    else if( bskState == BSK_STATE_BONUS_TALLY )
      bskUpdateBonusTally();
}
