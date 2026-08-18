#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// BATTLOT mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_battlot`) - a top-down robot
// battle: drive a robot around a 12x7 map, shoot enemy robots that spawn
// from the enemy base, and destroy the enemy base's fort to clear the stage;
// losing your own fort 3 times ends the game. 8 hand-authored stages, real
// destructible terrain (soft/cracked walls take bullet hits before clearing),
// and a real persistent-in-VVramBack map layer distinct from the sprite
// layer (see the VVram section below).
//
// Ported the same session, and following the exact same methodology, as the
// sibling `gameCracky.c` (also inufuto/UIAPduino+SSD1306, also CH32V003, no
// LICENSE file) - see that file's own header for the fuller writeup of the
// techniques reused verbatim here: the CharPattern/AsciiPattern nibble-
// interleaved font-composite math, the genuine 60Hz SysTick tick-pacing
// model (`Timer.cpp`'s own `kTimerHz=60`, `WaitTimer(t)`), and - most
// importantly - the finding that `InitOled()`'s `RightToLeft`/`BottomToTop`
// SSD1306 register settings do **not** need replicating in software at all:
// this project's earlier, hard-won conclusion (confirmed via a real
// reference photo on Cracky) that these two settings exist to compensate for
// a physical panel-mounting quirk on some SSD1306 breakout modules, not
// something a software recreation needs to correct for. `btlComposeRawByte()`
// draws directly at its own (col,page) - no column mirror, no page reorder,
// no bit-reversal, matching Cracky's own final, user-verified conclusion.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke CH32V003
// hardware (`ScanKeys.cpp`'s own single-analog-pin 4-direction ladder +
// one digital fire pin) - needed no new shim, `isLeftPressed()`/
// `isRightPressed()`/`isUpPressed()`/`isDownPressed()`/`isFirePressed()`
// cover the whole input surface exactly (matching the game's own README:
// "移動=UP,DOWN,LEFT,RIGHT / 撃つ=ACT" - move with the d-pad, shoot with one
// action button).
//
// **No real inheritance anywhere** - unlike this project's own class-
// flattening precedent for several other ported games, `Robo.h` declares
// ONE plain `struct Robo` reused directly for both `MyRobo` (a single
// instance) and `EnemyRobos[]` (an array) - `MyRobo.cpp`/`EnemyRobo.cpp`
// are just two separate source files implementing role-specific FUNCTIONS
// on that same shared struct, not two derived C++ classes. Ported as one
// `BtlRobo` struct (x,y,dx,dy,step,sprite,status,count, byte-for-byte
// matching upstream's own field list) plus `btl`-prefixed free functions
// grouped into the same three logical sections upstream has (generic
// Robo.cpp movement/collision, MyRobo.cpp player-specific, EnemyRobo.cpp
// AI-specific) - no struct flattening was actually needed here at all.
//
// **VVram is a genuine two-buffer system, unlike Cracky's own single-buffer
// recompute-every-frame design** - `VVramBack` holds the STATIC per-stage
// map (fence rows + wall/floor tiles, written once by `InitTrying()`'s own
// `SetWall()` calls and only re-touched when a destructible wall takes a
// hit, or when a fort's own health-meter/body tiles change via
// `DrawForts()`), while `VVramFront` is a per-frame WORKING COPY
// (`VVramBackToFront()`, a plain memcpy) that then gets the moving sprite
// layer composited on top (`DrawSprites()`) before being sent to the
// display. Ported as a direct structural mirror rather than re-derived into
// Cracky's own "recompute the whole map from bit-packed cell data every
// frame" shape - `btlVVramBack`/`btlVVramFront` are both real, persistent
// `int[BTL_VVRAM_WIDTH*BTL_VVRAM_HEIGHT]` globals, `btlDrawAll()` does the
// same copy-then-composite-sprites two-step every real engine tick
// (avoiding any VRAM-persistence-reliant partial-redraw risk, since the
// full copy+recomposite always runs regardless of what actually changed -
// the same "always redraw the full frame" precedent this project applies
// everywhere). `btlSetWall()`/`btlDrawForts()`'s own direct writes into
// `btlVVramBack` are exactly upstream's own mechanism for making a
// destroyed wall or a damaged fort "stick" on screen - no separate dirty-
// tracking cache was needed to reproduce this, since the persistent Back
// buffer already IS the thing that sticks.
//
// **Status/title/message text is unified into one whole-screen ASCII grid**,
// a deliberate generalization beyond Cracky's own two-tier design (a small
// `crkStatusChar[8][8]` side panel plus a separate single-message
// `crkOverlayActive` rectangle) - later widened to match Cracky's own real
// full-width layout once Cracky's own [8][32] fix landed (see below).
// Upstream's own `Status.cpp`/`Print.cpp` write text via `PrintC()`/
// `PrintS()` directly to arbitrary real Vram addresses spanning the WHOLE
// screen width - not just the right-side SCORE/STAGE/TIME/lives panel
// (columns 96-127, i.e. char-columns 24-31, exactly matching Cracky's own
// panel position and width) but ALSO the title screen's own credit/menu
// text (MINI/START/CONTINUE/the "INUFUTO 2026" line), which lives INSIDE
// the map's own column range (0-95, char-columns 0-23) since the title
// screen has no live map to share that space with. `btlStatusChar[8][32]`
// (8 real hardware pages x all 32 real char-columns, not just the panel's
// own 8) covers both: columns 24-31 always hold the live SCORE/STAGE/TIME/
// lives text (written by `btlPrintStatus()`, used unconditionally during
// both PLAYING and TITLE), while columns 0-23 hold either nothing (all-
// zero/space during normal gameplay, where `btlComposeRawByte()` OR-
// combines them with the VVram/CharPattern map - a safe no-op, since
// nothing ever writes text there outside the title screen) or the title
// screen's own credit/menu text (during `BTL_STATE_TITLE`, gated by a new
// `btlFullWidthText` flag - see below). The title screen's own big pixel-
// art LOGO itself is separate from this text grid entirely - it's real
// VVram/CharPattern map content (`btlTitleBytes`, drawn into
// `btlVVramFront` by `btlBeginTitle()`), not ASCII text - see "The title
// screen's own decorative logo" section further below for the full story
// of that restoration, and why the two now share one unified render path
// (`btlRender()`/`btlComposeRawByte()`) instead of the separate, ASCII-
// only `btlRenderTitle()` this port originally had.
// `btlOverlayActive`/`btlOverlayText`/`btlOverlayPage`/`btlOverlayCol` is
// kept as Cracky's own exact single-bounded-rectangle mechanism, layered
// ON TOP of the VVram-driven map columns, specifically for the GAME OVER /
// TIME UP messages (which DO need the live map/bang-animation visible
// behind them, matching upstream's own `PrintGameOver()`/`PrintTimeUp()`
// direct-Vram-write-over-the-map-area mechanic) - the same "message burned
// directly over map columns, needing an explicit persistent overlay rather
// than a one-shot write, since this port has no real VRAM persistence to
// rely on" reasoning Cracky's own header already documents for the exact
// same upstream shape.
//
// **The title screen's own decorative logo is a real, hand-authored
// pixel-art bitmap upstream** (`Status.cpp`'s own 96-value `TitleBytes[]`
// table, drawn via the SAME `CharPattern`/VVram machinery the map itself
// uses, not the ASCII font) - drawn as the real bitmap, not a text
// substitute. An earlier revision of this port had simplified it to plain
// ASCII text instead (reasoning it was "purely decorative"), the same
// wrong call Cracky's own earlier version made for its own "CRACKY" logo
// before a real user-supplied hardware photo proved that reasoning wrong
// there - see gameCracky.c's own header comment for the full story of
// that discovery. Once Cracky's own fix landed, a dedicated cross-sibling
// pass re-checked every other Cate-engine port in this project for the
// identical "simplified the real logo bitmap to text" gap and found this
// file was one of them - **restored** here the same way, byte-diff-
// verified against upstream's real `TitleBytes[]` via a small Python
// script before ever pasting it in (`btlTitleBytes`, see its own table
// comment). The plain-ASCII substitute's own real, known font-coverage
// limitation (`AsciiPattern`'s 27-glyph subset is missing 'B'/'L', so a
// text-based "BATTLOT" would have rendered as " ATT OT") is now moot -
// the real bitmap logo needs no ASCII glyphs at all, since it's built
// entirely from `btlCharPattern`'s own "logo" range (indices 0-15,
// confirmed byte-identical to gameCracky.c's own copy of the same shared
// palette by direct comparison against upstream's own `Chars.cpp`).
// `btlComposeRawByte()` was updated to OR-combine this VVram content with
// `btlStatusChar`'s own text layer (via a new `btlFullWidthText` flag,
// mirroring `crkFullWidthText` exactly) rather than the two staying on
// entirely separate render paths, since the logo (pages 1-2) and the
// text (pages 3/5/6/7) occupy disjoint page ranges by construction - see
// that function's own comment. This also let the earlier, separate
// `btlRenderTitle()` render path (which read `btlStatusChar` only,
// ignoring VVram entirely - needed back when the title screen had no
// real VVram content of its own to show) be retired in favor of just
// calling the same `btlRender()`/`btlComposeRawByte()` path gameplay
// already uses, the same unification Cracky's own fix already
// established.
//
// **This file's own status-text grid was already 32 columns wide from its
// very first draft** (`btlStatusChar[8][32]`, `btlPrintStatus()`'s own
// column arguments already matching upstream's real `LeftX`-based values)
// - unlike `gameCracky.c`, this port never actually had the 8-column-grid
// architectural bug at all. A dedicated cross-sibling verification pass
// (triggered directly by the real hardware photo that fixed Cracky's own
// title screen - see this project's own CLAUDE.md for the full story)
// re-checked every title-screen column against the real upstream
// `Status.cpp` line by line anyway, rather than assuming "already
// 32-wide" meant "already correct" - and found one real miss: "MINI" was
// printed at a plain column 0 instead of upstream's real computed column
// (`TitleLeft + 4*TitleLength - 5 = 19`), even though every OTHER
// title-screen element (START/CONTINUE/the selection arrow, the "INUFUTO
// 2026" credit line, and all of SCORE/STAGE/TIME/lives) already matched
// upstream's real columns exactly. Fixed in `btlBeginTitle()` - see the
// comment right at that call site for the exact derivation. (A second,
// later pass found and fixed the missing logo bitmap itself - see above -
// which is what the earlier "logo itself... already matched upstream's
// real columns exactly" line in a prior revision of this comment actually
// meant: the logo's plain-ASCII-text POSITION happened to already be
// approximately right, not that a real bitmap was already being drawn.)
//
// **Sound**: upstream's own `Sound.cpp` is a real 3-tone-channel PWM
// synthesizer plus a separate LFSR-noise `EffectChannel` (structurally
// identical in shape to Cracky's own Sound.cpp - same `ToneChannelCount=3`
// channel layout, same per-note volume-envelope/`NoteLength`/`Scale`
// design, even the exact same `Frequencies[]` table of 40 equal-tempered
// note values E2..G5) - ported the same way Cracky's was: every melody call
// goes straight to `md_playTone(freqHz, durationSeconds)` via a small
// frame-stepped 3-voice sequencer (`btlStartSeq`/`btlAdvanceOneSeq`/
// `btlAdvanceSound`, channel 0 = one-shot SFX [Fire's descending laser cue,
// reused for the bonus-tally Beep - the two never overlap in time],
// channel 1 = jingle/BGM-voice-A [reused for Start/Clear/GameOver, which
// also never overlap each other], channel 2 = BGM-voice-B) - safe to fan
// every cue straight into `md_playTone()` with no risk of one cue cutting
// another off, since this project's own `md_playTone()` became genuinely
// multi-voice project-wide in an earlier session (see this project's own
// CLAUDE.md). Each note's own real duration is derived directly from
// `SoundHandler()`'s own real tempo formula - `Tempo=180` here (Cracky's
// own was 160), so a channel advances once every `(600/2)/180 = 1.66667`
// real 60Hz ticks, giving `btlNoteFrames(length) = round(length*1.66667)`
// (Cracky's own analogous derivation was `length*1.875`, from its own
// Tempo=160). The `NoteLength`/`Scale` enum VALUES themselves (N8=6,
// N4=12, ..., E2=1..G5=40) and the `Frequencies[]` table are byte-for-byte
// identical to Cracky's own copy, confirmed by direct comparison rather
// than assumed from the shared filename/author - reused with the same
// literal numbers, just under a `BTL_` prefix and this game's own Tempo.
//
// **The EffectChannel's own LFSR-noise "explosion" synthesis has no direct
// equivalent** - approximated with a single representative one-shot
// `md_playTone()` call per bang (3000Hz for a small bang, 1500Hz for a
// large one, matching `Sound_SmallBang()`/`Sound_LargeBang()`'s own real
// `Effect.Start(frequency)` carrier pitch exactly) at a derived duration:
// the real envelope decays from volume 63 by 2 per `Next()` call (itself
// gated by the same `time<=0` accumulator as the 3 tone channels), needing
// `ceil(63/2)=32` such calls to reach silence, at ~1.66667 real ticks/call
// -> `32*1.66667/60 ≈ 0.889s` (`BTL_BANG_DURATION_SECONDS`). A one-shot
// tone with no decay envelope is a real, deliberate simplification (no
// noise synth exists here at all) - matching this project's own precedent
// for approximating a hardware-specific synthesis trick with a
// representative tone (e.g. UFO's own thruster-hum approximation).
//
// **A real, load-bearing AVR/CH32V003-style 8-bit-wraparound-reliance bug
// family, found and fixed proactively by inspection before ever compiling**
// - the same class of bug this whole project's own CLAUDE.md documents
// extensively for several ATtiny85 ports (byte truncation, signed
// sentinels, shift wraparound), just from a different 8-bit MCU family
// (CH32V003/RISC-V) rather than AVR, and via a different specific
// mechanism (unsigned-byte-arithmetic wraparound used as an implicit
// bounds-check) rather than any of the five/six mechanisms already
// catalogued there. Every position field here (`Robo.x/y`, `Bullet.x/y`,
// `Robo.count`, `MyRobo`'s own `RespawnCount`) is declared `byte`
// (`uint8_t`) upstream, and several places rely on real 8-bit wraparound
// as a deliberate detection trick:
// - `Fire()`'s own boundary check (`(xOffset>0 && x<pRobo->x) ||
//   (xOffset<0 && x>pRobo->x)`) only works because a byte addition that
//   overflows/underflows genuinely wraps - on Vircon32's non-truncating
//   `int`s, `x = pRobo->x + xOffset` with `xOffset=-1` and `pRobo->x=0`
//   just gives `-1`, not `255`, so the comparison would silently fail to
//   catch a shot fired from the very edge of the map, letting a bullet
//   spawn at a negative X that then never satisfies the later `x>=
//   StageWidth` off-screen check either (since `-1<24`), permanently
//   "leaking" a live bullet with a corrupted position.
// - `TestMoveRobo()`'s own candidate-position computation feeds directly
//   into `WallAroundRobo()`'s `x>=StageWidth||...` boundary check (the
//   very first thing it does) - without the same wraparound, a robot
//   pressing Left at x=0 would compute a genuinely negative candidate x,
//   sail straight past that boundary check (since `-1<StageWidth`), and
//   reach `GetWall()`'s own `WallMapPtr()` array indexing with a negative
//   index - a genuine out-of-bounds read.
// - `Bullet::Move()`'s own off-screen check (`pBullet->x>=StageWidth||
//   pBullet->y>=StageHeight`) has the identical shape/risk as `Fire()`'s.
// - `EnemyRobo.cpp`'s own `pRobo->count` periodically toggles which of
//   MyRobo/MyFort an enemy is currently targeting, gated on `++count==0`
//   (a real byte wraparound from 255 back to 0) - without reproducing the
//   wrap, `count` would just grow past 255 as a plain int and never equal
//   0 again after its first increment, permanently disabling the
//   retarget-every-256-ticks behavior (a real, if minor, AI-variety loss).
// - `MyRobo.cpp`'s own `RespawnCount` is the single highest-impact case:
//   `StartMyRobo()` sets it to exactly 0 on a successful respawn-position
//   search, and `MoveMyRobo()`'s very next statement (`--RespawnCount;`,
//   reached unconditionally on the same call whenever status was `None`)
//   immediately underflows it to 255 on real hardware - genuinely giving
//   every respawn a long (~255-tick) blinking-invincibility window before
//   `RespawnCount==0` fires again and status flips back to Live. Without
//   reproducing this wrap, `RespawnCount` would go to -1 and then keep
//   decrementing forever without ever hitting exactly 0 again - the
//   player would NEVER actually respawn after the first death, a
//   permanent soft-lock.
// **Fixed uniformly** with a small `btlWrap8(v)` helper (`v & 0xFF`)
// applied at every one of these genuinely wraparound-reliant computation
// sites (`Fire()`'s candidate x, `TestMoveRobo()`'s candidate x/y,
// `MoveRobo()`'s own position update for consistency, `Bullet::Move()`'s
// position update, `EnemyRobo`'s `count` increment, `MyRobo`'s
// `RespawnCount` decrement) - reproducing the real CH32V003 byte-wraparound
// behavior exactly rather than either leaving it broken or guessing at a
// "fixed" replacement mechanic upstream never actually had. A companion,
// purely defensive fix (not found to be load-bearing by tracing, just
// cheap insurance against any residual wraparound edge case missed above -
// e.g. `Bang.cpp`'s own `x -= Size/2` on a bang centered at map edge x=0):
// `btlDrawSprites()` skips (rather than writes) any sprite whose computed
// VVram cell falls outside the real `0..BTL_VVRAM_WIDTH-1` /
// `0..BTL_VVRAM_HEIGHT-1` bounds, a single central guard matching this
// project's own "fix once, centrally" precedent (e.g. `md_drawColumn()`'s
// own `&0xFF` mask, Tiny Arena's `arSliceByte()` guard) rather than trying
// to prove every possible sprite-position computation is safe by hand.
//
// **A deliberate simplification for the real per-real-tick sub-stepping
// upstream's own bare `do{...}while()` loop performs**: `Main()`'s own
// loop calls `MoveMyBullets()` on EVERY iteration, `MoveEnemyBullets()` on
// every OTHER iteration (`Clock&1==0`), `UpdateBangs()`/`MoveMyRobo()`/the
// StageTime countdown once per FOUR iterations (`Clock&3==0` - the same
// iteration that also calls the real `WaitTimer(1)` blocking wait, i.e.
// once per real 60Hz tick), and `StartEnemyRobo()`/`MoveEnemyRobos()` once
// per EIGHT iterations (`Clock&7==0`, i.e. once every OTHER real tick) -
// tracing the exact interleaving confirms this nets out, per real 60Hz
// engine tick, to: `MoveMyBullets()` x4, `MoveEnemyBullets()` x2,
// `UpdateBangs()`/`MoveMyRobo()`/the countdown x1, and
// `StartEnemyRobo()`/`MoveEnemyRobos()` x1-every-other-tick. Ported as
// exactly that many calls per real engine frame (`btlUpdatePlaying()`),
// the same "run the logic body N times per real frame" technique already
// established for Tiny Arkanoid's own decoupled-tick fix, rather than
// trying to reproduce the *exact* micro-interleaving order (which has no
// observable gameplay effect here, since these subsystems don't interact
// within the same sub-step) - each object's own `step&BTL_STEP_MASK==0`
// gate still correctly throttles its ACTUAL movement to the right real-
// time cadence regardless of the coarser call-count grouping (a player
// robot moves once every 8 `MoveRobo()` calls; called once per real tick,
// that's a genuine ~7.5Hz - matching Cracky's own similarly-derived
// effective gameplay tick rate almost exactly, a good cross-check that
// this translation is right).
//
// **The blocking upstream control flow** (`Main()`'s own goto-chained
// labels, several real `WaitMelody()`/`Wait()`(a local 10-tick bang-
// animation pause helper)/`WaitTimer()` blocking waits) rewritten as an
// explicit frame-stepped state machine, the same treatment every port in
// this project needs: BTL_STATE_TITLE (`Title()`'s own interactive
// selection loop), BTL_STATE_START_JINGLE (the blocking `Sound_Start()`
// jingle held before play begins), BTL_STATE_PLAYING (the main tick),
// BTL_STATE_DEATH_WAIT (the local `Wait()` 10-tick bang-animation pause,
// shared by BOTH the time-up path and the fort-destroyed path - see its
// own comment below for the one real ordering subtlety between the two:
// the fort-destroyed path keeps the BGM playing THROUGH the wait,
// stopping it only after, while the time-up path stops the BGM
// immediately, before the wait even begins), BTL_STATE_GAMEOVER_JINGLE,
// BTL_STATE_CLEAR_WAIT (the win-condition's own `Wait()` pause),
// BTL_STATE_CLEAR_JINGLE, and BTL_STATE_BONUS_TALLY (the real
// `while(StageTime>=BonusRate){...Sound_Beep();WaitTimer(5);}` bonus-
// countdown loop, converted to one decrement+beep+wait per real tick,
// matching this project's own HollowSeeker/Ardumania bonus-tally
// precedent - note this loop's own real per-iteration pause is
// `btlNoteFrames(1)` [the Beep note's own real duration, since
// `Sound_Beep()` is itself a BLOCKING `WaitMelody`] PLUS a separate,
// additional `WaitTimer(5)` afterward - a real difference from Cracky's
// own simpler bonus-tally loop, which has no equivalent extra wait).
//
// `Score`/`HiScore` - upstream's own `HiScore` global is entirely
// commented out (no persistence, no display anywhere) - `btlHiScore` is
// still tracked in-session for parity with Cracky's own identical choice
// (Cracky's own upstream `HiScore` is ALSO fully commented out, yet
// Cracky's port still tracks a session-local `crkHiScore`) even though,
// like Cracky, nothing here ever displays it either - a deliberate
// consistency choice with the sibling port, not a functional need.
//
// **A dedicated post-port verification pass** (this game had only been
// test-compiled, never played/screenshot-tested, before this pass) did a
// full line-by-line re-derivation of every draw/compositing function and
// every state transition against the real upstream source, byte-diffed
// every data table (AsciiPattern/CharPattern/Frequencies/all 7 melodies/
// all 8 stages' fort-position + wall-byte tables) via a small Python
// script rather than trusting the original port's own transcription, and
// specifically re-verified each of the byte-wraparound-reliance fixes
// this file's header already documents (`Fire()`'s bullet-spawn x/y,
// `TestMoveRobo()`, `MoveRobo()`, `StartRobo()`, `Bullet::Move()`,
// `EnemyRobo`'s `count`, `MyRobo`'s `RespawnCount`) by hand-tracing the
// exact arithmetic against upstream's real byte-typed fields rather than
// assuming the original fix was correct. All of those held up exactly as
// documented - including `Fire()`'s own bullet-spawn Y offset, which
// turns out to NOT need an explicit `btlWrap8()` despite X needing one:
// tracing it through confirmed the un-wrapped `y=-1` case (firing Up from
// the topmost stage row) still reproduces upstream's real byte-wrapped
// `y=255` behavior at every observable point - `btlShowSprite()`'s own
// `y + BTL_STAGE_TOP` computes `-1+1=0` directly, exactly matching
// upstream's real `255+1` 8-bit-wraparound-to-`0`, and the subsequent
// wall/collision check (`btlGetWall()`'s own defensive bounds guard, an
// addition beyond upstream that upstream's own real `GetWall()` doesn't
// have at all - a genuine out-of-bounds `WallMap` read on real hardware
// for this exact edge case) returns `BTL_WALL_HARD` either way, so the
// bullet dies on the very next tick regardless of whether Y was wrapped
// - a real, deliberately-non-obvious case where "leave it unwrapped" and
// "wrap it" produce provably identical output, not an overlooked gap.
//
// **One real, confirmed bug found and fixed** (rendering/state-
// progression, not fidelity-to-upstream): `btlBeginStageTry()` - the
// shared helper reached on every fresh game, every stage-retry after a
// life lost with lives remaining, and every subsequent stage - never
// reset `btlOverlayActive`. Upstream's own `InitTrying()` calls a real
// `ClearScreen()`, physically wiping the whole OLED VRAM (including
// wherever `PrintTimeUp()`'s own direct-VRAM "TIME UP" text was written)
// before a retried attempt's own gameplay resumes - this port's own
// message-overlay mechanism has no equivalent implicit clear, so without
// an explicit reset, a TIME-UP death (with lives remaining) left "TIME
// UP" burned indefinitely over every subsequent attempt's own gameplay,
// for the rest of the game (the only other place the flag is ever
// cleared is `btlBeginTitle()`, unreached until the whole game truly
// ends) - exactly the sibling `gameCracky.c`'s own already-documented fix
// for the identical upstream shape (`crkOverlayActive = false;` at its
// own stage-retry call site), just missing here. Confirmed both the bug
// and the fix live, via a temporary debug hook (`btlStageTime` shortened
// to 3, `btlDeathWaitFrames` extended to 120 for a comfortably-observable
// 2-second window - both fully reverted before shipping, confirmed via a
// final grep for any leftover debug markers) in this project's own
// isolated Puppeteer/WebGL test harness: screenshotted a real TIME UP
// death across 3 consecutive stage-retries, confirming "TIME UP" appears
// correctly, then fully clears on every retry (no stuck text), and that
// the eventual "GAME OVER" (once `btlRemainCount` reaches 0) correctly
// overwrites it rather than stacking. The fort-destroyed death path
// never triggers this overlay at all upstream (only `PrintTimeUp()`
// does), so it was never at risk - confirmed by tracing, not assumed.
//
// Every other rendering path (the CharPattern nibble-interleaved map
// composite, the VVram-to-real-column page/nibble math, the Title
// screen's own text layout, the fort meter/body tile writes, the sprite
// bounds-guard) and every other state transition (the exact per-real-
// tick sub-step call counts, the shared `try_:`/`lose:`-equivalent
// convergence between the time-up and fort-destroyed paths, the BGM-
// stop-timing asymmetry between them, `RemainCount`/`CurrentStage`/
// `Score` reset ordering across a fresh game vs. Continue) was traced
// against the real upstream control flow and confirmed correct - see
// each mechanism's own paragraph above for the detail. Live-tested via
// the same harness: menu navigation to BATTLOT, the title screen (attract
// text, cursor, credits), starting a game, movement in multiple
// directions, firing (a visible bullet trail), and enemy-robot AI
// (spawning and moving near the enemy fort) - all rendered and behaved
// correctly. Not independently forced this pass: a real fort-destroyed
// death/stage-clear/bonus-tally sequence, and an enemy bullet actually
// hitting the player's robot - both reuse logic paths already traced
// line-by-line against upstream above, so risk is low, but worth a
// direct check if anything looks off.
//
// No forceRedraw needed for this SDL3 port - btlUpdate*() (dispatched
// from gameBattlot_update() below) always finishes with an unconditional
// btlRender() call in every one of its 8 state branches, exactly the same
// "already redraws every real tick regardless of what changed" shape as
// gameCracky.c's own equivalent - so there is no "leftover stale frame"
// case a forceRedraw hook would ever need to paper over.
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h
// -----------------------------------------------------------------------------

#define BTL_CHAR_SPACE 0
#define BTL_CHAR_WALL 16
#define BTL_CHAR_HARDWALL 20
#define BTL_CHAR_METER 21
#define BTL_CHAR_BULLET 23
#define BTL_CHAR_FENCE 25
#define BTL_CHAR_MYFORT 27
#define BTL_CHAR_ENEMYFORT 39
#define BTL_CHAR_MYROBO 51
#define BTL_CHAR_ENEMYROBO 83
#define BTL_CHAR_SMALLBANG 115
#define BTL_CHAR_LARGEBANG 119
#define BTL_CHAR_REMAIN 135

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define BTL_MAP_WIDTH 12
#define BTL_MAP_HEIGHT 7
#define BTL_STAGE_WIDTH ( BTL_MAP_WIDTH * 2 )
#define BTL_STAGE_HEIGHT ( BTL_MAP_HEIGHT * 2 )

#define BTL_WALL_NONE 0
#define BTL_WALL_CRACK 1
#define BTL_WALL_SOFT 2
#define BTL_WALL_HARD 3
#define BTL_WALL_MYROBO 4
#define BTL_WALL_MYFORT 5
#define BTL_WALL_MYBULLET 6
#define BTL_WALL_ENEMYROBO 7
#define BTL_WALL_ENEMYFORT 8
#define BTL_WALL_ENEMYBULLET 9

#define BTL_WALLMAP_WIDTH ( BTL_STAGE_WIDTH / 4 )
#define BTL_WALLMAP_SIZE ( BTL_WALLMAP_WIDTH * BTL_STAGE_HEIGHT )

#define BTL_STAGE_COUNT 8

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define BTL_VVRAM_WIDTH 24
#define BTL_VVRAM_HEIGHT 16
#define BTL_STAGE_TOP 1

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define BTL_STEP_MASK 7
#define BTL_SPRITE_MYROBO 0
#define BTL_SPRITE_ENEMYROBO 1
#define BTL_SPRITE_MYBULLET 5
#define BTL_SPRITE_ENEMYBULLET 7
#define BTL_SPRITE_BANG 13
#define BTL_SPRITE_COUNT 21
#define BTL_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Robo.h
// -----------------------------------------------------------------------------

#define BTL_ROBO_DIRECTION_MASK 0x06
#define BTL_ROBO_SEQ_MASK 0x01
#define BTL_ROBO_PATTERN_MASK 0x07
#define BTL_ROBO_STATUS_MASK 0x30
#define BTL_ROBO_STATUS_NONE 0x00
#define BTL_ROBO_STATUS_LIVE 0x10
#define BTL_ROBO_STATUS_WAIT 0x20
#define BTL_ROBO_CANMOVE_MASK 0x40
#define BTL_ROBO_TARGET_MASK 0x80
#define BTL_ROBO_SIZE 2
#define BTL_ENEMY_ROBO_COUNT ( BTL_SPRITE_MYBULLET - BTL_SPRITE_ENEMYROBO )

typedef struct
{
    int x, y;
    int dx, dy;
    int step;
    int sprite;
    int status;
    int count;
} BtlRobo;

// -----------------------------------------------------------------------------
//   Bullet.h
// -----------------------------------------------------------------------------

#define BTL_BULLET_DIRECTION_MASK 0x06
#define BTL_BULLET_PATTERN_MASK 0x01
#define BTL_BULLET_LIVE_MASK 0x10
#define BTL_BULLET_SIDE_MASK 0x20
#define BTL_BULLET_SIDE_MY 0
#define BTL_MAX_MY_BULLETS ( BTL_SPRITE_ENEMYBULLET - BTL_SPRITE_MYBULLET )
#define BTL_MAX_ENEMY_BULLETS ( BTL_SPRITE_BANG - BTL_SPRITE_ENEMYBULLET )

typedef struct
{
    int x, y;
    int dx, dy;
    int width, height;
    int step;
    int sprite;
    int status;
} BtlBullet;

// -----------------------------------------------------------------------------
//   Bang.h
// -----------------------------------------------------------------------------

#define BTL_BANG_STATUS_NONE 0x00
#define BTL_BANG_STATUS_SMALL 0x10
#define BTL_BANG_STATUS_LARGE_SMALL 0x20
#define BTL_BANG_STATUS_LARGE_LARGE 0x30
#define BTL_BANG_STATUS_MASK 0xf0
#define BTL_BANG_COUNT_MASK 0x0f
#define BTL_BANG_SIZE 2
#define BTL_MAX_BANGS ( BTL_SPRITE_COUNT - BTL_SPRITE_BANG )

typedef struct
{
    int x, y;
    int status;
} BtlBang;

// -----------------------------------------------------------------------------
//   Fort.h
// -----------------------------------------------------------------------------

#define BTL_FORT_MAX_LIFE 8
#define BTL_FORT_WIDTH 4
#define BTL_FORT_HEIGHT 3

typedef struct
{
    int x, y;
    int life;
} BtlFort;

// -----------------------------------------------------------------------------
//   Sprite struct (Sprite.cpp)
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y;
    int code;
} BtlSprite;

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching
//   upstream's own enum exactly (byte-for-byte identical values to
//   gameCracky.c's own copy, confirmed by direct comparison).
// -----------------------------------------------------------------------------

#define BTL_N8 6
#define BTL_N8P ( BTL_N8 * 3 / 2 )
#define BTL_N4 ( BTL_N8 * 2 )
#define BTL_N4P ( BTL_N4 * 3 / 2 )
#define BTL_N2 ( BTL_N4 * 2 )
#define BTL_N2P ( BTL_N2 * 3 / 2 )
#define BTL_N1 ( BTL_N2 * 2 )

#define BTL_A4 30
#define BTL_G4 28
#define BTL_C5 33
#define BTL_D5 35
#define BTL_E5 37

#define BTL_MELODY_NONE 0
#define BTL_MELODY_FIRE 1
#define BTL_MELODY_BEEP 2
#define BTL_MELODY_START 3
#define BTL_MELODY_CLEAR 4
#define BTL_MELODY_GAMEOVER 5
#define BTL_MELODY_BGM1 6
#define BTL_MELODY_BGM2 7

#define BTL_MAX_TIME_DENOM 50
#define BTL_BONUS_RATE 4
#define BTL_BANG_DURATION_SECONDS 0.89

// -----------------------------------------------------------------------------
//   Data tables - extracted via a small Python script (byte-diff against
//   upstream), not hand-copied. See this file's own header comment for the
//   Sound/NoteLength/Scale derivation.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph.
int btlAsciiPattern[108] = {
    0, 0, 0, 0, 31, 17, 31, 0, 0, 0,
    31, 0, 29, 21, 23, 0, 21, 21, 31, 0,
    7, 4, 31, 0, 23, 21, 29, 0, 31, 21,
    29, 0, 1, 29, 3, 0, 31, 21, 31, 0,
    23, 21, 31, 0, 31, 14, 4, 0, 30, 9,
    30, 0, 14, 17, 10, 0, 31, 21, 17, 0,
    31, 5, 1, 0, 14, 17, 13, 0, 17, 31,
    17, 0, 31, 6, 31, 0, 31, 1, 30, 0,
    14, 17, 14, 0, 31, 5, 7, 0, 31, 5,
    26, 0, 22, 21, 13, 0, 1, 31, 1, 0,
    31, 16, 31, 0, 15, 16, 15, 0,
};

// CharPattern - 139 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int btlCharPattern[278] = {
    0, 0, 51, 0, 204, 0, 255, 0, 0, 51,
    51, 51, 204, 51, 255, 51, 0, 204, 51, 204,
    204, 204, 255, 204, 0, 255, 51, 255, 204, 255,
    255, 255, 74, 10, 10, 10, 238, 14, 14, 238,
    255, 255, 6, 6, 6, 0, 136, 136, 0, 240,
    136, 136, 17, 17, 0, 0, 0, 0, 0, 236,
    175, 0, 128, 108, 58, 221, 209, 226, 46, 15,
    220, 69, 85, 204, 221, 197, 92, 221, 0, 168,
    239, 12, 0, 0, 0, 0, 192, 166, 46, 174,
    221, 188, 139, 14, 164, 170, 170, 106, 166, 170,
    170, 74, 128, 115, 85, 8, 192, 21, 50, 140,
    0, 123, 85, 0, 0, 208, 9, 0, 128, 85,
    55, 8, 200, 35, 81, 12, 0, 85, 183, 0,
    0, 144, 13, 0, 40, 85, 37, 8, 3, 63,
    22, 0, 40, 85, 37, 8, 3, 55, 15, 1,
    40, 119, 39, 8, 1, 55, 15, 0, 40, 119,
    39, 8, 0, 63, 7, 1, 128, 118, 101, 8,
    192, 21, 50, 140, 0, 126, 101, 0, 0, 208,
    9, 0, 128, 86, 103, 8, 200, 35, 81, 12,
    0, 86, 231, 0, 0, 144, 13, 0, 104, 87,
    103, 8, 3, 63, 22, 0, 104, 87, 103, 8,
    3, 55, 15, 1, 104, 119, 103, 8, 1, 55,
    15, 0, 104, 119, 103, 8, 0, 63, 7, 1,
    228, 182, 74, 78, 114, 226, 37, 23, 0, 194,
    132, 124, 98, 255, 233, 111, 206, 236, 7, 136,
    221, 54, 63, 1, 100, 219, 123, 226, 0, 54,
    17, 226, 151, 171, 88, 70, 18, 49, 35, 0,
    0, 236, 175, 0, 209, 226, 46, 15,
};

// TitleBytes - upstream's own real title-screen logo bitmap (Status.cpp's
// `Title()`), 6 letters x 4x4 VVram-cell glyph indices each (96 values
// total), byte-diff-verified against the real upstream source. Every value
// here is a valid index into btlCharPattern[]'s own "logo" range (indices
// 0-15, the first 32 bytes of that table - confirmed byte-identical to
// gameCracky.c's own copy of the same shared palette by direct comparison
// against upstream's own Chars.cpp) - the exact same shared block-pattern
// palette every other map tile in this game already draws through, just
// reused here to build a big pixel-art wordmark instead of a wall/floor
// tile. See btlBeginTitle()'s own comment for why this replaces the
// earlier plain-text "BATTLOT" substitute.
int btlTitleBytes[96] = {
    0x0f, 0x05, 0x0b, 0x00, 0x0f, 0x0a, 0x07, 0x0c,
    0x0f, 0x00, 0x0f, 0x0c, 0x05, 0x05, 0x01, 0x04,
    0x0e, 0x0b, 0x00, 0x05, 0x03, 0x0c, 0x03, 0x00,
    0x07, 0x0d, 0x03, 0x00, 0x01, 0x04, 0x01, 0x00,
    0x0f, 0x05, 0x04, 0x0d, 0x0f, 0x00, 0x00, 0x0c,
    0x0f, 0x00, 0x00, 0x0c, 0x05, 0x00, 0x00, 0x04,
    0x07, 0x01, 0x0f, 0x00, 0x03, 0x00, 0x0f, 0x00,
    0x03, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x05, 0x05,
    0x00, 0x08, 0x07, 0x0d, 0x00, 0x0c, 0x03, 0x0c,
    0x00, 0x0c, 0x03, 0x0c, 0x05, 0x00, 0x05, 0x05,
    0x02, 0x05, 0x0f, 0x05, 0x03, 0x00, 0x0f, 0x00,
    0x03, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x05, 0x00,
};

// Frequencies - E2..G5, Scale enum values 1-40. Byte-for-byte identical to
// gameCracky.c's own crkFrequencies table.
int btlFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int btlMelodyFire[13] = {
    1, 38, 1, 36, 1, 34, 1, 32, 1, 30,
    1, 40, 0,
};

int btlMelodyBeep[3] = {
    1, 30, 0,
};

int btlMelodyStart[17] = {
    12, 30, 12, 30, 6, 30, 12, 33, 6, 35,
    24, 37, 12, 0, 12, 0, 0,
};

int btlMelodyClear[25] = {
    6, 30, 6, 0, 6, 30, 6, 28, 6, 30,
    12, 33, 6, 35, 6, 0, 6, 33, 6, 0,
    18, 30, 24, 0, 0,
};

int btlMelodyGameOver[25] = {
    6, 30, 6, 37, 6, 35, 6, 33, 6, 35,
    6, 33, 6, 32, 18, 30, 6, 0, 12, 28,
    6, 28, 12, 30, 0,
};

int btlMelodyBgm1[193] = {
    12, 30, 12, 30, 6, 30, 12, 33, 6, 35,
    24, 37, 12, 0, 6, 37, 6, 38, 9, 37,
    9, 35, 6, 33, 9, 35, 9, 33, 6, 32,
    9, 33, 9, 32, 6, 30, 9, 32, 9, 30,
    6, 28, 12, 30, 12, 30, 6, 30, 12, 33,
    6, 35, 24, 37, 12, 0, 6, 37, 6, 38,
    9, 37, 9, 35, 6, 33, 9, 35, 9, 33,
    6, 32, 9, 33, 9, 32, 6, 30, 9, 32,
    9, 30, 6, 28, 6, 30, 6, 30, 12, 30,
    6, 0, 6, 28, 6, 0, 6, 30, 6, 33,
    6, 33, 12, 33, 6, 0, 6, 32, 6, 0,
    6, 33, 6, 35, 6, 35, 12, 35, 6, 0,
    6, 33, 6, 0, 6, 35, 9, 37, 9, 35,
    6, 33, 9, 35, 9, 33, 6, 32, 6, 30,
    6, 30, 12, 30, 6, 0, 6, 28, 6, 0,
    6, 30, 6, 33, 6, 33, 12, 33, 6, 0,
    6, 32, 6, 0, 6, 33, 6, 35, 6, 35,
    12, 35, 6, 0, 6, 33, 6, 0, 6, 35,
    9, 37, 9, 35, 6, 33, 9, 35, 9, 33,
    6, 32, 255,
};

int btlMelodyBgm2[233] = {
    12, 14, 6, 0, 6, 14, 6, 0, 6, 14,
    6, 0, 6, 14, 12, 21, 6, 0, 6, 21,
    6, 0, 6, 21, 6, 0, 6, 21, 12, 23,
    6, 0, 6, 23, 6, 0, 6, 23, 6, 0,
    6, 23, 6, 14, 6, 0, 6, 14, 6, 0,
    6, 16, 6, 0, 6, 16, 6, 0, 12, 14,
    6, 0, 6, 14, 6, 0, 6, 14, 6, 0,
    6, 14, 12, 21, 6, 0, 6, 21, 6, 0,
    6, 21, 6, 0, 6, 21, 12, 23, 6, 0,
    6, 23, 6, 0, 6, 23, 6, 0, 6, 23,
    6, 14, 6, 0, 6, 14, 6, 0, 6, 16,
    6, 0, 6, 16, 6, 0, 12, 18, 6, 0,
    6, 18, 6, 0, 6, 18, 6, 0, 6, 18,
    12, 14, 6, 0, 6, 14, 6, 0, 6, 14,
    6, 0, 6, 14, 12, 23, 6, 0, 6, 23,
    6, 0, 6, 23, 6, 0, 6, 23, 6, 21,
    6, 0, 6, 21, 6, 0, 6, 20, 6, 0,
    6, 20, 6, 0, 12, 18, 6, 0, 6, 18,
    6, 0, 6, 18, 6, 0, 6, 18, 12, 14,
    6, 0, 6, 14, 6, 0, 6, 14, 6, 0,
    6, 14, 12, 23, 6, 0, 6, 23, 6, 0,
    6, 23, 6, 0, 6, 23, 6, 21, 6, 0,
    6, 21, 6, 0, 6, 20, 6, 0, 6, 20,
    6, 0, 255,
};

// Stage data - myFort/enemyFort packed (x<<4)|y positions, plus 21 raw map
// bytes/stage (2-bit wall values, 4 per byte, MapWidth/4 * MapHeight).
int btlStageMyFort[8] = {
    5, 0, 0, 34, 161, 160, 5, 5,
};
int btlStageEnemyFort[8] = {
    160, 165, 160, 165, 18, 53, 160, 165,
};
int btlStageBytes[8][21] = {
    {
        0x8a, 0xaa, 0x08, 0x8a, 0xaa, 0x0a,
        0x2a, 0x00, 0x80, 0x2a, 0xaa, 0x8a,
        0x2a, 0x2a, 0x80, 0x20, 0x2a, 0xaa,
        0x00, 0x00, 0xa2,
    },
    {
        0x00, 0x2a, 0x02, 0xa0, 0xfe, 0xa3,
        0x00, 0xa8, 0xa2, 0xa8, 0xa8, 0x80,
        0xaa, 0x20, 0x08, 0xaa, 0x02, 0x0b,
        0xaa, 0xaa, 0x0b,
    },
    {
        0x80, 0x32, 0x00, 0x00, 0xba, 0x02,
        0x2a, 0x32, 0xf0, 0x2a, 0x3a, 0x00,
        0x2a, 0x8a, 0xa0, 0x2a, 0x00, 0xff,
        0xa2, 0x08, 0x82,
    },
    {
        0x8a, 0xc0, 0x0a, 0x8a, 0xea, 0xff,
        0x08, 0x08, 0x80, 0x00, 0x0c, 0x0a,
        0x02, 0x00, 0x03, 0x20, 0x0a, 0x03,
        0x2a, 0x0a, 0x03,
    },
    {
        0x20, 0x08, 0x82, 0xe0, 0x08, 0x0e,
        0xc2, 0x00, 0x0c, 0xc0, 0x08, 0xa8,
        0xf8, 0xff, 0x0b, 0x00, 0x00, 0x02,
        0xc2, 0xa0, 0x00,
    },
    {
        0x00, 0xc0, 0x02, 0x20, 0xce, 0x00,
        0x00, 0xcc, 0x03, 0x38, 0xcc, 0x28,
        0xf0, 0x8f, 0x00, 0x0c, 0xfc, 0x83,
        0x00, 0xfc, 0x03,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
    },
    {
        0x80, 0x0b, 0x08, 0x0c, 0xc3, 0x30,
        0x2c, 0xe3, 0x32, 0x8c, 0xc3, 0x30,
        0x0c, 0xcb, 0x08, 0x20, 0xc3, 0x08,
        0x20, 0xe0, 0x0a,
    },
};

// Direction.cpp - dx,dy pairs indexed by Direction_Right/Left/Down/Up (0,2,4,6).
int btlDirectionBytes[8] = {
    1, 0,
    -1, 0,
    0, 1,
    0, -1,
};

// Robo.cpp's StartRobo() - 12 candidate spawn offsets (dx,dy) tried in
// sequence from a shared, persistent index.
int btlStartOffsets[24] = {
    -2, -1,
    -2, 1,
    0, 3,
    2, 3,
    4, 1,
    4, -1,
    2, -3,
    0, -3,
    -4, 0,
    1, 5,
    6, 0,
    1, 4,
};

// Robo.cpp's Fire() - bullet spawn offset per direction (0,2,4,6).
int btlBulletOffsets[8] = {
    2, 0,
    -1, 0,
    0, 2,
    0, -1,
};

// Bullet.cpp's Start() - bullet width,height per direction (0,2,4,6).
int btlBulletSizes[8] = {
    0, 1,
    0, 1,
    1, 0,
    1, 0,
};

// EnemyRobo.cpp's own Rnd() - a 16-entry rotating lookup table, distinct
// from any random table used by any other game in this project.
int btlEnemyRndTable[16] = {
    11, 8, 9, 4, 4, 12, 0, 12, 13, 11, 0, 6, 13, 12, 5, 15,
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int btlScore;
int btlHiScore;
int btlRemainCount;
int btlCurrentStage;
int btlStageTime;
int btlStageIndex;
int btlTimeDenom;
int btlEnemyRoboPhase;

int btlVVramBack[BTL_VVRAM_WIDTH * BTL_VVRAM_HEIGHT];
int btlVVramFront[BTL_VVRAM_WIDTH * BTL_VVRAM_HEIGHT];
int btlWallMap[BTL_WALLMAP_SIZE];

BtlFort btlMyFort;
BtlFort btlEnemyFort;

BtlRobo btlMyRobo;
int btlMyIntervalCount;
int btlMyRespawnCount;
int btlMyRespawnTime;

BtlRobo btlEnemyRobos[BTL_ENEMY_ROBO_COUNT];
int btlRoboStartIndex;
int btlEnemyRndIndex;
int btlEnemyStartCount;
int btlEnemyStartInterval;

BtlBullet btlMyBullets[BTL_MAX_MY_BULLETS];
BtlBullet btlEnemyBullets[BTL_MAX_ENEMY_BULLETS];

BtlBang btlBangs[BTL_MAX_BANGS];

BtlSprite btlSprites[BTL_SPRITE_COUNT];

// whole-screen text grid: cols 0-23 (title-only content), cols 24-31
// (SCORE/STAGE/TIME/lives panel, always live) - see header comment.
int btlStatusChar[8][32];

// Set true only while on the title screen (BTL_STATE_TITLE) - lets
// btlComposeRawByte() OR-combine btlStatusChar's own cols 0-23 (the title
// logo's own MINI/START/CONTINUE/credit text) into the composed byte on
// top of the VVram-derived map/logo byte, instead of the narrower gameplay
// behavior of only ever reading cols 24-31 (the always-live status panel).
// See btlComposeRawByte()'s own header comment for the full mechanism -
// mirrors gameCracky.c's own crkFullWidthText exactly.
bool btlFullWidthText;

// message overlay burned directly over map columns, matching upstream's
// own PrintTimeUp()/PrintGameOver() direct-Vram-write mechanic.
bool btlOverlayActive;
int btlOverlayText[10];
int btlOverlayLen;
int btlOverlayPage;
int btlOverlayCol;

// sound sequencer - 3 independent voices, each advances every real frame.
int btlSeqMelody[3];
int btlSeqPos[3];
int btlSeqWait[3];
int btlSeqActive[3];

#define BTL_STATE_TITLE 0
#define BTL_STATE_START_JINGLE 1
#define BTL_STATE_PLAYING 2
#define BTL_STATE_DEATH_WAIT 3
#define BTL_STATE_GAMEOVER_JINGLE 4
#define BTL_STATE_CLEAR_WAIT 5
#define BTL_STATE_CLEAR_JINGLE 6
#define BTL_STATE_BONUS_TALLY 7
int btlState;

int btlDeathWaitFrames;
bool btlDeathViaTimeUp;
int btlClearWaitFrames;
int btlBonusWaitFrames;

int btlSelection;
bool btlSelectionChanged;
int btlPrevLeft;
int btlPrevRight;
int btlPrevUp;
int btlPrevDown;
int btlPrevFire;


// -----------------------------------------------------------------------------
//   Wraparound helper - see header comment ("A real, load-bearing
//   AVR/CH32V003-style 8-bit-wraparound-reliance bug family").
// -----------------------------------------------------------------------------

int btlWrap8( int v )
{
    return v & 0xFF;
}


// -----------------------------------------------------------------------------
//   Bang sound - a one-shot representative tone, see header comment.
// -----------------------------------------------------------------------------

void btlSoundSmallBang()
{
    md_playTone( 3000.0, BTL_BANG_DURATION_SECONDS );
}

void btlSoundLargeBang()
{
    md_playTone( 1500.0, BTL_BANG_DURATION_SECONDS );
}


// -----------------------------------------------------------------------------
//   Sound sequencer - 3 independent voices, see header comment.
// -----------------------------------------------------------------------------

int btlMelodyLength( int id )
{
    if( id == BTL_MELODY_FIRE ) return 13;
    if( id == BTL_MELODY_BEEP ) return 3;
    if( id == BTL_MELODY_START ) return 17;
    if( id == BTL_MELODY_CLEAR ) return 25;
    if( id == BTL_MELODY_GAMEOVER ) return 25;
    if( id == BTL_MELODY_BGM1 ) return 193;
    if( id == BTL_MELODY_BGM2 ) return 233;
    return 0;
}

int btlMelodyValue( int id, int idx )
{
    if( id == BTL_MELODY_FIRE ) return btlMelodyFire[ idx ];
    if( id == BTL_MELODY_BEEP ) return btlMelodyBeep[ idx ];
    if( id == BTL_MELODY_START ) return btlMelodyStart[ idx ];
    if( id == BTL_MELODY_CLEAR ) return btlMelodyClear[ idx ];
    if( id == BTL_MELODY_GAMEOVER ) return btlMelodyGameOver[ idx ];
    if( id == BTL_MELODY_BGM1 ) return btlMelodyBgm1[ idx ];
    if( id == BTL_MELODY_BGM2 ) return btlMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/180 = 1.66667 real 60Hz ticks - see header comment.
int btlNoteFrames( int length )
{
    return (int)( (float)length * ( 300.0 / 180.0 ) + 0.5 );
}

void btlStartSeq( int channel, int melodyId )
{
    btlSeqMelody[ channel ] = melodyId;
    btlSeqPos[ channel ] = 0;
    btlSeqWait[ channel ] = 0;
    btlSeqActive[ channel ] = 1;
}

void btlStopSeq( int channel )
{
    btlSeqActive[ channel ] = 0;
    btlSeqMelody[ channel ] = BTL_MELODY_NONE;
}

bool btlSeqPlaying( int channel )
{
    return btlSeqActive[ channel ] != 0;
}

void btlAdvanceOneSeq( int channel )
{
    int length, note;

    if( btlSeqActive[ channel ] == 0 ) return;

    if( btlSeqWait[ channel ] > 0 )
    {
        btlSeqWait[ channel ] = btlSeqWait[ channel ] - 1;
        return;
    }

    length = btlMelodyValue( btlSeqMelody[ channel ], btlSeqPos[ channel ] );
    if( length == 0 )
    {
        btlStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        btlSeqPos[ channel ] = 0;
        length = btlMelodyValue( btlSeqMelody[ channel ], 0 );
    }
    note = btlMelodyValue( btlSeqMelody[ channel ], btlSeqPos[ channel ] + 1 );
    btlSeqPos[ channel ] = btlSeqPos[ channel ] + 2;
    btlSeqWait[ channel ] = btlNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)btlFrequencies[ note - 1 ], (float)btlSeqWait[ channel ] / 60.0 );
}

void btlAdvanceSound()
{
    btlAdvanceOneSeq( 0 );
    btlAdvanceOneSeq( 1 );
    btlAdvanceOneSeq( 2 );
}

void btlStartBgm()
{
    btlStartSeq( 1, BTL_MELODY_BGM1 );
    btlStartSeq( 2, BTL_MELODY_BGM2 );
}

void btlStopBgm()
{
    btlStopSeq( 1 );
    btlStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status/title/message text written into
//   btlStatusChar (a pattern-index grid covering all 32 real char-columns).
// -----------------------------------------------------------------------------

int btlAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct port of PrintC()'s
    // own linear search. Missing letters (e.g. B, L) fall back to space -
    // see header comment.
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

int btlPrintC( int page, int col, int c )
{
    btlStatusChar[ page ][ col ] = btlAsciiIndex( c );
    return col + 1;
}

int btlPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = btlPrintC( page, col, s[ i ] );
    return col;
}

int btlPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
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
    return btlPrintC( page, col, c );
}

int btlPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      btlPrintC( page, col, ' ' );
    else
      btlPrintC( page, col, d1 + '0' );
    return btlPrintC( page, col + 1, ( b % 10 ) + '0' );
}

int btlPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        btlPrintC( page, col, ' ' );
        if( d2 == 0 )
          btlPrintC( page, col + 1, ' ' );
        else
          btlPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        btlPrintC( page, col, d1 + '0' );
        btlPrintC( page, col + 1, d2 + '0' );
    }
    return btlPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

int btlPrintNumber5( int page, int col, int w )
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
          btlPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            btlPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    return btlPrintC( page, col + 4, rem + '0' );
}

void btlPrintScore()
{
    btlPrintNumber5( 1, 24 + 2, btlScore );
    btlPrintC( 1, 24 + 7, '0' );
}

void btlPrintTime()
{
    btlPrintByteNumber3( 5, 24 + 5, btlStageTime );
}

void btlPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    btlPrintS( 0, 24, sScore, 5 );
    btlPrintS( 3, 24, sStage, 5 );
    btlPrintByteNumber2( 3, 24 + 6, btlCurrentStage + 1 );
    btlPrintS( 5, 24, sTime, 4 );

    // RemainCount lives indicator - upstream draws a real 2x2 Char_Remain
    // icon (Put2C) here; simplified to plain text, matching gameCracky.c's
    // own established simplification for the exact same upstream shape
    // (including its own "i<=2 just blanks cells, no visible indicator"
    // limitation - see gameCracky.c's own crkPrintStatus() comment).
    if( btlRemainCount > 1 )
    {
        i = btlRemainCount - 1;
        if( i > 2 )
        {
            btlPrintC( 7, 24, ' ' );
            btlPrintC( 7, 25, ' ' );
            btlPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < btlRemainCount - 1; i = i + 1 )
              btlPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    btlPrintScore();
    btlPrintTime();
}

void btlBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    btlOverlayActive = true;
    btlOverlayLen = len;
    btlOverlayPage = page;
    btlOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      btlOverlayText[ i ] = s[ i ];
}

void btlPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    btlBeginOverlay( s, 9, 4, 8 );
}

void btlPrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    btlBeginOverlay( s, 7, 4, 9 );
}

void btlAddScore( int pts )
{
    btlScore = btlScore + pts;
    if( btlScore > btlHiScore )
      btlHiScore = btlScore;
    btlPrintScore();
}


// -----------------------------------------------------------------------------
//   VVram.cpp helper
// -----------------------------------------------------------------------------

int btlVVramOffset( int x, int y )
{
    return y * BTL_VVRAM_WIDTH + x;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites directly into btlVVramFront.
// -----------------------------------------------------------------------------

void btlHideAllSprites()
{
    int i;
    for( i = 0; i < BTL_SPRITE_COUNT; i = i + 1 )
      btlSprites[ i ].code = BTL_INVALID_CODE;
}

void btlShowSprite( int index, int x, int y, int code )
{
    btlSprites[ index ].x = x;
    btlSprites[ index ].y = y + BTL_STAGE_TOP;
    btlSprites[ index ].code = code;
}

void btlHideSprite( int index )
{
    btlSprites[ index ].code = BTL_INVALID_CODE;
}

void btlDrawSprites()
{
    // A central defensive bounds guard, not found to be load-bearing by
    // tracing but cheap insurance against a residual wraparound edge case
    // (e.g. Bang.cpp's own `x -= Size/2` at map edge x=0) - see header
    // comment.
    int i, x, y, c;
    for( i = 0; i < BTL_SPRITE_COUNT; i = i + 1 )
    {
        if( btlSprites[ i ].code != BTL_INVALID_CODE )
        {
            x = btlSprites[ i ].x;
            y = btlSprites[ i ].y;
            if( x < 0 || x >= BTL_VVRAM_WIDTH || y < 0 || y >= BTL_VVRAM_HEIGHT ) continue;
            c = btlSprites[ i ].code;
            btlVVramFront[ btlVVramOffset( x, y ) ] = c;
            if( c >= BTL_CHAR_MYROBO )
            {
                if( x + 1 < BTL_VVRAM_WIDTH )
                {
                    c = c + 1;
                    btlVVramFront[ btlVVramOffset( x + 1, y ) ] = c;
                    c = c + 1;
                }
                else
                  c = c + 2;
                if( y + 1 < BTL_VVRAM_HEIGHT )
                {
                    btlVVramFront[ btlVVramOffset( x, y + 1 ) ] = c;
                    c = c + 1;
                    if( x + 1 < BTL_VVRAM_WIDTH )
                      btlVVramFront[ btlVVramOffset( x + 1, y + 1 ) ] = c;
                }
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - low-level wall map (2-bit-per-cell, 4 cells/byte).
// -----------------------------------------------------------------------------

int btlWallMapPtr( int x, int y )
{
    return ( y * BTL_STAGE_WIDTH + x ) / 4;
}

int btlGetWall( int x, int y )
{
    int idx, b, xMod;
    idx = btlWallMapPtr( x, y );
    if( idx < 0 || idx >= BTL_WALLMAP_SIZE ) return BTL_WALL_HARD;
    b = btlWallMap[ idx ];
    xMod = x & 3;
    while( xMod != 0 )
    {
        b = b >> 2;
        xMod = xMod - 1;
    }
    return b & BTL_WALL_HARD;
}

void btlSetWall( int x, int y, int wall )
{
    int c;
    int idx, mask, shifted, xMod;

    if( wall == 0 )
      c = BTL_CHAR_SPACE;
    else if( wall == 1 )
      c = BTL_CHAR_WALL + ( y & 1 );
    else if( wall == 2 )
      c = BTL_CHAR_WALL + 2 + ( y & 1 );
    else
      c = BTL_CHAR_HARDWALL;
    btlVVramBack[ btlVVramOffset( x, y + 1 ) ] = c;

    idx = btlWallMapPtr( x, y );
    if( idx < 0 || idx >= BTL_WALLMAP_SIZE ) return;
    mask = 0xfc;
    shifted = wall;
    xMod = x & 3;
    while( xMod != 0 )
    {
        shifted = shifted << 2;
        mask = ( mask << 2 ) | 3;
        xMod = xMod - 1;
    }
    btlWallMap[ idx ] = ( btlWallMap[ idx ] & mask ) | shifted;
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void btlInitBangs()
{
    int i;
    for( i = 0; i < BTL_MAX_BANGS; i = i + 1 )
      btlBangs[ i ].status = BTL_BANG_STATUS_NONE;
}

void btlStartBang( int x, int y, bool large )
{
    int i;
    for( i = 0; i < BTL_MAX_BANGS; i = i + 1 )
    {
        if( ( btlBangs[ i ].status & BTL_BANG_STATUS_MASK ) != BTL_BANG_STATUS_NONE ) continue;
        btlBangs[ i ].x = x;
        btlBangs[ i ].y = y;
        if( large )
          btlBangs[ i ].status = BTL_BANG_STATUS_LARGE_SMALL;
        else
          btlBangs[ i ].status = BTL_BANG_STATUS_SMALL;
        return;
    }
}

void btlUpdateBangs()
{
    int i, x, y, sprite, mode, count, pattern;
    sprite = BTL_SPRITE_BANG;
    for( i = 0; i < BTL_MAX_BANGS; i = i + 1 )
    {
        mode = btlBangs[ i ].status & BTL_BANG_STATUS_MASK;
        if( mode == BTL_BANG_STATUS_NONE ) continue;
        count = btlBangs[ i ].status & BTL_BANG_COUNT_MASK;

        x = btlBangs[ i ].x;
        y = btlBangs[ i ].y;
        if( mode == BTL_BANG_STATUS_LARGE_LARGE )
        {
            int qx, qy;
            x = btlWrap8( x - BTL_BANG_SIZE / 2 );
            y = btlWrap8( y - BTL_BANG_SIZE / 2 );
            pattern = BTL_CHAR_LARGEBANG;
            for( qy = 0; qy < 2; qy = qy + 1 )
            {
                for( qx = 0; qx < 2; qx = qx + 1 )
                {
                    btlShowSprite( sprite, x, y, pattern );
                    sprite = sprite + 1;
                    pattern = pattern + 4;
                    x = btlWrap8( x + BTL_BANG_SIZE );
                }
                x = btlWrap8( x - BTL_BANG_SIZE * 2 );
                y = btlWrap8( y + BTL_BANG_SIZE );
            }
        }
        else
        {
            btlShowSprite( sprite, x, y, BTL_CHAR_SMALLBANG );
            sprite = sprite + 1;
        }

        count = count + 1;
        if( count >= 8 )
        {
            if( mode == BTL_BANG_STATUS_LARGE_SMALL )
              btlBangs[ i ].status = BTL_BANG_STATUS_LARGE_LARGE;
            else
              btlBangs[ i ].status = BTL_BANG_STATUS_NONE;
        }
        else
          btlBangs[ i ].status = mode | count;
    }
    while( sprite < BTL_SPRITE_COUNT )
    {
        btlHideSprite( sprite );
        sprite = sprite + 1;
    }
}


// -----------------------------------------------------------------------------
//   Fort.cpp
// -----------------------------------------------------------------------------

void btlFortInit( BtlFort* pFort, int position )
{
    pFort->x = ( position >> 3 ) & 0xfe;
    pFort->y = ( ( position & 15 ) << 1 ) + 1;
    pFort->life = BTL_FORT_MAX_LIFE;
}

void btlDrawOneFort( BtlFort* pFort, int c )
{
    int x, y, count, i, hx, hy;
    x = pFort->x;
    y = pFort->y - 1 + BTL_STAGE_TOP;

    count = pFort->life >> 1;
    i = 0;
    while( i < count )
    {
        btlVVramBack[ btlVVramOffset( x + i, y ) ] = BTL_CHAR_METER;
        i = i + 1;
    }
    if( ( pFort->life & 1 ) != 0 )
    {
        btlVVramBack[ btlVVramOffset( x + i, y ) ] = BTL_CHAR_METER + 1;
        i = i + 1;
    }
    while( i < BTL_FORT_WIDTH )
    {
        btlVVramBack[ btlVVramOffset( x + i, y ) ] = BTL_CHAR_SPACE;
        i = i + 1;
    }

    if( pFort->life > 0 )
    {
        for( hy = 0; hy < BTL_FORT_HEIGHT; hy = hy + 1 )
        {
            for( hx = 0; hx < BTL_FORT_WIDTH; hx = hx + 1 )
            {
                btlVVramBack[ btlVVramOffset( x + hx, y + 1 + hy ) ] = c;
                c = c + 1;
            }
        }
    }
    else
    {
        for( hy = 0; hy < BTL_FORT_HEIGHT; hy = hy + 1 )
        {
            for( hx = 0; hx < BTL_FORT_WIDTH; hx = hx + 1 )
              btlVVramBack[ btlVVramOffset( x + hx, y + 1 + hy ) ] = BTL_CHAR_SPACE;
        }
    }
}

void btlDrawForts()
{
    btlDrawOneFort( &btlMyFort, BTL_CHAR_MYFORT );
    btlDrawOneFort( &btlEnemyFort, BTL_CHAR_ENEMYFORT );
}

bool btlFortHitR( BtlFort* pFort, int x, int y )
{
    return
        pFort->life > 0 &&
        x + ( BTL_ROBO_SIZE - 1 ) >= pFort->x && pFort->x + ( BTL_FORT_WIDTH - 1 ) >= x &&
        y + ( BTL_ROBO_SIZE - 1 ) >= pFort->y && pFort->y + ( BTL_FORT_HEIGHT - 1 ) >= y;
}

bool btlHitMyFortR( int x, int y )
{
    return btlFortHitR( &btlMyFort, x, y );
}

bool btlHitEnemyFortR( int x, int y )
{
    return btlFortHitR( &btlEnemyFort, x, y );
}

bool btlFortHitB( BtlFort* pFort, int x, int y, int width, int height )
{
    return
        pFort->life > 0 &&
        x + width >= pFort->x && pFort->x + ( BTL_FORT_WIDTH - 1 ) >= x &&
        y + height >= pFort->y && pFort->y + ( BTL_FORT_HEIGHT - 1 ) >= y;
}

void btlFortDamage( BtlFort* pFort, int x, int y )
{
    if( pFort->life > 0 )
    {
        pFort->life = pFort->life - 1;
        btlDrawForts();
        if( pFort->life == 0 )
        {
            btlStartBang( pFort->x + 1, pFort->y, true );
            btlSoundLargeBang();
        }
        else
        {
            btlStartBang( x, y, false );
            btlSoundSmallBang();
        }
    }
}

bool btlHitMyFortB( int x, int y, int width, int height, int side )
{
    if( btlFortHitB( &btlMyFort, x, y, width, height ) )
    {
        if( side != 0 && btlMyFort.life > 0 )
          btlFortDamage( &btlMyFort, x, y );
        return true;
    }
    return false;
}

bool btlHitEnemyFortB( int x, int y, int width, int height, int side )
{
    if( btlFortHitB( &btlEnemyFort, x, y, width, height ) )
    {
        if( side == 0 && btlEnemyFort.life > 0 )
          btlFortDamage( &btlEnemyFort, x, y );
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Bullet.cpp - part 1 (spawn/draw, no collision dependency).
// -----------------------------------------------------------------------------

void btlDrawBullet( BtlBullet* pBullet )
{
    if( ( pBullet->status & BTL_BULLET_LIVE_MASK ) != 0 )
      btlShowSprite( pBullet->sprite, pBullet->x, pBullet->y,
          BTL_CHAR_BULLET + ( pBullet->status & BTL_BULLET_PATTERN_MASK ) );
    else
      btlHideSprite( pBullet->sprite );
}

void btlIntBullets()
{
    int i;
    for( i = 0; i < BTL_MAX_MY_BULLETS; i = i + 1 )
      btlMyBullets[ i ].status = 0;
    for( i = 0; i < BTL_MAX_ENEMY_BULLETS; i = i + 1 )
      btlEnemyBullets[ i ].status = 0;
}

void btlStartOneBullet( BtlBullet* pBullet, int x, int y, int sprite, int direction, int side )
{
    int pattern;

    pBullet->x = x;
    pBullet->y = y;
    pBullet->dx = btlDirectionBytes[ direction ];
    pBullet->dy = btlDirectionBytes[ direction + 1 ];
    pBullet->width = btlBulletSizes[ direction ];
    pBullet->height = btlBulletSizes[ direction + 1 ];
    pBullet->step = 0;
    pBullet->sprite = sprite;
    if( ( direction & 4 ) != 0 )
      pattern = 1;
    else
      pattern = 0;
    pBullet->status = direction | pattern | BTL_BULLET_LIVE_MASK | side;
    btlDrawBullet( pBullet );
}

bool btlStartMyBullet( int x, int y, int direction )
{
    int i, sprite;
    sprite = BTL_SPRITE_MYBULLET;
    for( i = 0; i < BTL_MAX_MY_BULLETS; i = i + 1 )
    {
        if( ( btlMyBullets[ i ].status & BTL_BULLET_LIVE_MASK ) == 0 )
        {
            btlStartOneBullet( &btlMyBullets[ i ], x, y, sprite, direction, 0 );
            return true;
        }
        sprite = sprite + 1;
    }
    return false;
}

bool btlStartEnemyBullet( int x, int y, int direction )
{
    int i, sprite;
    sprite = BTL_SPRITE_ENEMYBULLET;
    for( i = 0; i < BTL_MAX_ENEMY_BULLETS; i = i + 1 )
    {
        if( ( btlEnemyBullets[ i ].status & BTL_BULLET_LIVE_MASK ) == 0 )
        {
            btlStartOneBullet( &btlEnemyBullets[ i ], x, y, sprite, direction, BTL_BULLET_SIDE_MASK );
            return true;
        }
        sprite = sprite + 1;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Robo.cpp - generic pure collision checks (no further function deps).
// -----------------------------------------------------------------------------

#define BTL_HIT_RANGE ( BTL_ROBO_SIZE - 1 )

bool btlHitRoboR( BtlRobo* pRobo, int x, int y )
{
    if( ( pRobo->status & BTL_ROBO_STATUS_MASK ) == BTL_ROBO_STATUS_NONE ) return false;
    return
        pRobo->x + BTL_HIT_RANGE >= x && x + BTL_HIT_RANGE >= pRobo->x &&
        pRobo->y + BTL_HIT_RANGE >= y && y + BTL_HIT_RANGE >= pRobo->y;
}

bool btlHitRoboB( BtlRobo* pRobo, BtlBullet* pBullet )
{
    if( ( pRobo->status & BTL_ROBO_STATUS_MASK ) == BTL_ROBO_STATUS_NONE ) return false;
    return
        pBullet->x + pBullet->width >= pRobo->x &&
        pRobo->x + BTL_HIT_RANGE >= pBullet->x &&
        pBullet->y + pBullet->height >= pRobo->y &&
        pRobo->y + BTL_HIT_RANGE >= pBullet->y;
}


// -----------------------------------------------------------------------------
//   Robo.cpp - Fire() (needs the Bullet start functions above).
// -----------------------------------------------------------------------------

bool btlFire( BtlRobo* pRobo, bool my )
{
    int direction, x, y, xOffset;
    direction = pRobo->status & BTL_ROBO_DIRECTION_MASK;
    xOffset = btlBulletOffsets[ direction ];
    x = btlWrap8( pRobo->x + xOffset );
    if(
        ( xOffset > 0 && x < pRobo->x ) ||
        ( xOffset < 0 && x > pRobo->x )
    )
      return false;
    y = pRobo->y + btlBulletOffsets[ direction + 1 ];
    if( my )
      return btlStartMyBullet( x, y, direction );
    return btlStartEnemyBullet( x, y, direction );
}


// -----------------------------------------------------------------------------
//   MyRobo.cpp - hit-response functions.
// -----------------------------------------------------------------------------

bool btlHitMyRoboR( BtlRobo* pRobo, int x, int y )
{
    return
        ( btlMyRobo.status & BTL_ROBO_STATUS_MASK ) != BTL_ROBO_STATUS_NONE &&
        pRobo != &btlMyRobo &&
        btlHitRoboR( &btlMyRobo, x, y );
}

void btlDrawMyRobo()
{
    int status;
    status = btlMyRobo.status & BTL_ROBO_STATUS_MASK;
    if(
        status == BTL_ROBO_STATUS_LIVE ||
        ( status == BTL_ROBO_STATUS_WAIT && ( btlMyRespawnCount & 4 ) == 0 )
    )
      btlShowSprite( BTL_SPRITE_MYROBO, btlMyRobo.x, btlMyRobo.y,
          BTL_CHAR_MYROBO + ( ( btlMyRobo.status & BTL_ROBO_PATTERN_MASK ) << 2 ) );
    else
      btlHideSprite( BTL_SPRITE_MYROBO );
}

bool btlHitMyRoboB( BtlBullet* pBullet )
{
    if( !btlHitRoboB( &btlMyRobo, pBullet ) ) return false;
    if(
        ( pBullet->status & BTL_BULLET_SIDE_MASK ) != BTL_BULLET_SIDE_MY &&
        ( btlMyRobo.status & BTL_ROBO_STATUS_MASK ) == BTL_ROBO_STATUS_LIVE
    )
    {
        btlStartBang( btlMyRobo.x, btlMyRobo.y, false );
        btlSoundSmallBang();
        btlMyRobo.status = btlMyRobo.status & ~BTL_ROBO_STATUS_MASK;
        btlMyRespawnCount = btlMyRespawnTime;
        btlDrawMyRobo();
    }
    return true;
}


// -----------------------------------------------------------------------------
//   EnemyRobo.cpp - hit-response functions.
// -----------------------------------------------------------------------------

void btlDrawEnemyRobo( BtlRobo* pRobo )
{
    if( ( pRobo->status & BTL_ROBO_STATUS_MASK ) != BTL_ROBO_STATUS_NONE )
      btlShowSprite( pRobo->sprite, pRobo->x, pRobo->y,
          BTL_CHAR_ENEMYROBO + ( ( pRobo->status & BTL_ROBO_PATTERN_MASK ) << 2 ) );
    else
      btlHideSprite( pRobo->sprite );
}

bool btlHitEnemyRoboR( BtlRobo* p, int x, int y )
{
    int i;
    for( i = 0; i < BTL_ENEMY_ROBO_COUNT; i = i + 1 )
    {
        if( &btlEnemyRobos[ i ] != p && ( btlEnemyRobos[ i ].status & BTL_ROBO_STATUS_MASK ) != BTL_ROBO_STATUS_NONE )
        {
            if( btlHitRoboR( &btlEnemyRobos[ i ], x, y ) )
              return true;
        }
    }
    return false;
}

bool btlHitEnemyRoboB( BtlBullet* pBullet )
{
    #define BTL_ENEMY_HIT_RANGE ( ( BTL_STEP_MASK + 1 ) / 2 )
    int i, side;
    for( i = 0; i < BTL_ENEMY_ROBO_COUNT; i = i + 1 )
    {
        side = pBullet->status & BTL_BULLET_SIDE_MASK;
        if( side != BTL_BULLET_SIDE_MY && pBullet->step < BTL_ENEMY_HIT_RANGE ) continue;
        if( btlHitRoboB( &btlEnemyRobos[ i ], pBullet ) )
        {
            if(
                side == BTL_BULLET_SIDE_MY &&
                ( btlEnemyRobos[ i ].status & BTL_ROBO_STATUS_MASK ) == BTL_ROBO_STATUS_LIVE
            )
            {
                btlStartBang( btlEnemyRobos[ i ].x, btlEnemyRobos[ i ].y, false );
                btlSoundSmallBang();
                btlEnemyRobos[ i ].status = btlEnemyRobos[ i ].status & ~BTL_ROBO_STATUS_MASK;
                btlDrawEnemyRobo( &btlEnemyRobos[ i ] );
                btlAddScore( 10 );
            }
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Robo.cpp - generic movement/collision, part 2 (needs the fort/robo hit
//   checks above).
// -----------------------------------------------------------------------------

int btlWallAroundRobo( BtlRobo* pRobo, int x, int y )
{
    int maxWall, wall, hx, hy, sx;

    if(
        x >= BTL_STAGE_WIDTH ||
        x + ( BTL_ROBO_SIZE - 1 ) >= BTL_STAGE_WIDTH ||
        y >= BTL_STAGE_HEIGHT ||
        y + ( BTL_ROBO_SIZE - 1 ) >= BTL_STAGE_HEIGHT
    )
      return BTL_WALL_HARD;
    if( btlHitMyFortR( x, y ) ) return BTL_WALL_MYFORT;
    if( btlHitEnemyFortR( x, y ) ) return BTL_WALL_ENEMYFORT;
    if( btlHitMyRoboR( pRobo, x, y ) ) return BTL_WALL_MYROBO;
    if( btlHitEnemyRoboR( pRobo, x, y ) ) return BTL_WALL_ENEMYROBO;

    maxWall = 0;
    sx = x;
    for( hy = 0; hy < BTL_ROBO_SIZE; hy = hy + 1 )
    {
        x = sx;
        for( hx = 0; hx < BTL_ROBO_SIZE; hx = hx + 1 )
        {
            wall = btlGetWall( x, y );
            if( wall > maxWall )
              maxWall = wall;
            if( wall >= BTL_WALL_HARD )
              return wall;
            x = x + 1;
        }
        y = y + 1;
    }
    return maxWall;
}

int btlTestMoveRobo( BtlRobo* pRobo, int direction )
{
    int x, y, maxWall;
    x = btlWrap8( pRobo->x + btlDirectionBytes[ direction ] );
    y = btlWrap8( pRobo->y + btlDirectionBytes[ direction + 1 ] );
    maxWall = btlWallAroundRobo( pRobo, x, y );
    if( maxWall == 0 )
      pRobo->status = pRobo->status | BTL_ROBO_CANMOVE_MASK;
    else
      pRobo->status = pRobo->status & ~BTL_ROBO_CANMOVE_MASK;
    return maxWall;
}

void btlSetRoboDirection( BtlRobo* pRobo, int direction )
{
    pRobo->status = ( pRobo->status & ~BTL_ROBO_DIRECTION_MASK ) | direction;
    pRobo->dx = btlDirectionBytes[ direction ];
    pRobo->dy = btlDirectionBytes[ direction + 1 ];
}

bool btlMoveRobo( BtlRobo* pRobo )
{
    bool moved;
    int seq;
    if( ( pRobo->status & BTL_ROBO_CANMOVE_MASK ) == 0 ) return false;

    moved = false;
    if( ( pRobo->step & BTL_STEP_MASK ) == 0 )
    {
        pRobo->x = btlWrap8( pRobo->x + pRobo->dx );
        pRobo->y = btlWrap8( pRobo->y + pRobo->dy );
        seq = ( pRobo->x + pRobo->y ) & 1;
        pRobo->status = ( pRobo->status & ~BTL_ROBO_SEQ_MASK ) | seq;
        moved = true;
    }
    pRobo->step = pRobo->step + 1;
    return moved;
}

bool btlStartRobo( BtlRobo* pRobo, int fortX, int fortY )
{
    int i, x, y;
    for( i = 0; i < 12; i = i + 1 )
    {
        x = btlWrap8( fortX + btlStartOffsets[ btlRoboStartIndex ] );
        y = btlWrap8( fortY + btlStartOffsets[ btlRoboStartIndex + 1 ] );
        btlRoboStartIndex = btlRoboStartIndex + 2;
        if( btlRoboStartIndex >= 24 )
          btlRoboStartIndex = 0;
        if( btlWallAroundRobo( pRobo, x, y ) == 0 )
        {
            pRobo->x = x;
            pRobo->y = y;
            pRobo->step = 0;
            return true;
        }
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Bullet.cpp - part 2 (collision, needs the fort/robo hit checks above).
//   Upstream's own `goto hit;` shape rewritten as plain if/else, matching
//   this project's own established treatment of intra-function gotos.
// -----------------------------------------------------------------------------

bool btlHitWall( int x, int y )
{
    int wall;
    wall = btlGetWall( x, y );
    if( wall == 0 ) return false;
    if( wall < BTL_WALL_HARD )
      btlSetWall( x, y, wall - 1 );
    return true;
}

bool btlHitB( BtlBullet* pBullet, BtlBullet* p )
{
    if( ( pBullet->status & BTL_BULLET_LIVE_MASK ) == 0 ) return false;
    if(
        p->x + p->width >= pBullet->x &&
        pBullet->x + pBullet->width >= p->x &&
        p->y + p->height >= pBullet->y &&
        pBullet->y + pBullet->height >= p->y
    )
    {
        pBullet->status = pBullet->status & ~BTL_BULLET_LIVE_MASK;
        btlDrawBullet( pBullet );
        return true;
    }
    return false;
}

bool btlHitAnyBullet( BtlBullet* p )
{
    int i;
    for( i = 0; i < BTL_MAX_MY_BULLETS; i = i + 1 )
    {
        if( &btlMyBullets[ i ] != p && btlHitB( &btlMyBullets[ i ], p ) ) return true;
    }
    for( i = 0; i < BTL_MAX_ENEMY_BULLETS; i = i + 1 )
    {
        if( &btlEnemyBullets[ i ] != p && btlHitB( &btlEnemyBullets[ i ], p ) ) return true;
    }
    return false;
}

bool btlHitBullet( BtlBullet* pBullet )
{
    int x, y, width, height, side, addedX, addedY;
    bool hit;
    if( ( pBullet->step & BTL_STEP_MASK ) != 0 ) return false;

    x = pBullet->x; y = pBullet->y; width = pBullet->width; height = pBullet->height;
    side = pBullet->status & BTL_BULLET_SIDE_MASK;

    hit = false;
    if( btlHitWall( x, y ) ) hit = true;
    addedX = x + width;
    addedY = y + height;
    if( btlHitWall( addedX, addedY ) ) hit = true;

    if( !hit )
    {
        if( btlHitMyRoboB( pBullet ) || btlHitEnemyRoboB( pBullet ) || btlHitAnyBullet( pBullet ) )
          hit = true;
    }
    if( !hit )
    {
        if( btlHitMyFortB( x, y, width, height, side ) || btlHitEnemyFortB( x, y, width, height, side ) )
          hit = true;
    }
    if( !hit ) return false;

    pBullet->status = pBullet->status & ~BTL_BULLET_LIVE_MASK;
    return true;
}

void btlMoveBullet( BtlBullet* pBullet )
{
    if( ( pBullet->step & BTL_STEP_MASK ) == 0 )
    {
        if( !btlHitBullet( pBullet ) )
        {
            pBullet->x = btlWrap8( pBullet->x + pBullet->dx );
            pBullet->y = btlWrap8( pBullet->y + pBullet->dy );
            if( pBullet->x >= BTL_STAGE_WIDTH || pBullet->y >= BTL_STAGE_HEIGHT )
              pBullet->status = pBullet->status & ~BTL_BULLET_LIVE_MASK;
            else
              btlHitBullet( pBullet );
        }
        btlDrawBullet( pBullet );
    }
    pBullet->step = pBullet->step + 1;
}

void btlMoveMyBullets()
{
    int i;
    for( i = 0; i < BTL_MAX_MY_BULLETS; i = i + 1 )
    {
        if( ( btlMyBullets[ i ].status & BTL_BULLET_LIVE_MASK ) != 0 )
          btlMoveBullet( &btlMyBullets[ i ] );
    }
}

void btlMoveEnemyBullets()
{
    int i;
    for( i = 0; i < BTL_MAX_ENEMY_BULLETS; i = i + 1 )
    {
        if( ( btlEnemyBullets[ i ].status & BTL_BULLET_LIVE_MASK ) != 0 )
          btlMoveBullet( &btlEnemyBullets[ i ] );
    }
}


// -----------------------------------------------------------------------------
//   EnemyRobo.cpp - remaining functions (AI direction/spawn/movement).
// -----------------------------------------------------------------------------

#define BTL_DIR_RIGHT 0
#define BTL_DIR_LEFT 2
#define BTL_DIR_DOWN 4
#define BTL_DIR_UP 6

int btlEnemyRnd()
{
    int r;
    r = btlEnemyRndTable[ btlEnemyRndIndex ];
    btlEnemyRndIndex = btlEnemyRndIndex + 1;
    if( btlEnemyRndIndex >= 16 )
      btlEnemyRndIndex = 0;
    return r;
}

int btlEnemyAbs( int a, int b )
{
    if( a < b ) return b - a;
    return a - b;
}

int btlDecideDirection( BtlRobo* pRobo )
{
    int directions[4];
    int verticalDirectionIndex, horizontalDirectionIndex;
    int x, y, targetX, targetY, target;
    int i, newDirection, newX, newY, wall;

    x = pRobo->x;
    y = pRobo->y;
    target = pRobo->status & BTL_ROBO_TARGET_MASK;
    if( target == 0 )
    {
        targetX = btlMyRobo.x;
        targetY = btlMyRobo.y;
    }
    else
    {
        targetX = btlMyFort.x;
        targetY = btlMyFort.y;
    }

    if( btlEnemyAbs( targetX, x ) > btlEnemyAbs( targetY, y ) )
    {
        if( targetX < x )
        {
            if( pRobo->dx <= 0 )
            {
                directions[ 0 ] = BTL_DIR_LEFT;
                directions[ 3 ] = BTL_DIR_RIGHT;
                verticalDirectionIndex = 1;
            }
            else
            {
                directions[ 2 ] = BTL_DIR_RIGHT;
                directions[ 3 ] = BTL_DIR_LEFT;
                verticalDirectionIndex = 0;
            }
        }
        else
        {
            if( pRobo->dx >= 0 )
            {
                directions[ 0 ] = BTL_DIR_RIGHT;
                directions[ 3 ] = BTL_DIR_LEFT;
                verticalDirectionIndex = 1;
            }
            else
            {
                directions[ 2 ] = BTL_DIR_LEFT;
                directions[ 3 ] = BTL_DIR_RIGHT;
                verticalDirectionIndex = 0;
            }
        }
        if( ( targetY < y && pRobo->dy <= 0 ) || pRobo->dy < 0 )
        {
            directions[ verticalDirectionIndex ] = BTL_DIR_UP;
            verticalDirectionIndex = verticalDirectionIndex + 1;
            directions[ verticalDirectionIndex ] = BTL_DIR_DOWN;
        }
        else
        {
            directions[ verticalDirectionIndex ] = BTL_DIR_DOWN;
            verticalDirectionIndex = verticalDirectionIndex + 1;
            directions[ verticalDirectionIndex ] = BTL_DIR_UP;
        }
    }
    else
    {
        if( targetY < y )
        {
            if( pRobo->dy <= 0 )
            {
                directions[ 0 ] = BTL_DIR_UP;
                directions[ 3 ] = BTL_DIR_DOWN;
                horizontalDirectionIndex = 1;
            }
            else
            {
                directions[ 2 ] = BTL_DIR_DOWN;
                directions[ 3 ] = BTL_DIR_UP;
                horizontalDirectionIndex = 0;
            }
        }
        else
        {
            if( pRobo->dy >= 0 )
            {
                directions[ 0 ] = BTL_DIR_DOWN;
                directions[ 3 ] = BTL_DIR_UP;
                horizontalDirectionIndex = 1;
            }
            else
            {
                directions[ 2 ] = BTL_DIR_UP;
                directions[ 3 ] = BTL_DIR_DOWN;
                horizontalDirectionIndex = 0;
            }
        }
        // upstream compares `targetX < y` here too (a real upstream quirk,
        // not a transcription slip - kept exactly as-is, matching this
        // project's own "preserve a faithful, even if odd, upstream
        // comparison" precedent - gameCracky.c's own near-identical
        // enemy-AI direction-picker has the exact same shaped quirk).
        if( ( targetX < y && pRobo->dx <= 0 ) || pRobo->dx < 0 )
        {
            directions[ horizontalDirectionIndex ] = BTL_DIR_LEFT;
            horizontalDirectionIndex = horizontalDirectionIndex + 1;
            directions[ horizontalDirectionIndex ] = BTL_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalDirectionIndex ] = BTL_DIR_RIGHT;
            horizontalDirectionIndex = horizontalDirectionIndex + 1;
            directions[ horizontalDirectionIndex ] = BTL_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        newDirection = directions[ i ];
        newX = btlWrap8( x + btlDirectionBytes[ newDirection ] );
        newY = btlWrap8( y + btlDirectionBytes[ newDirection + 1 ] );
        wall = btlWallAroundRobo( pRobo, newX, newY );
        if(
            wall == BTL_WALL_NONE ||
            ( wall >= BTL_WALL_MYROBO && wall < BTL_WALL_ENEMYROBO ) ||
            ( wall < BTL_WALL_HARD && ( pRobo->count & 0x08 ) == 0 )
        )
        {
            btlSetRoboDirection( pRobo, newDirection );
            return wall;
        }
    }
    return BTL_WALL_HARD;
}

void btlInitEnemyRobos()
{
    int i, maxInterval, maxStage, step;
    for( i = 0; i < BTL_ENEMY_ROBO_COUNT; i = i + 1 )
      btlEnemyRobos[ i ].status = 0;

    maxInterval = 0x7f;
    maxStage = 0x20;
    step = maxInterval / maxStage;
    btlEnemyStartInterval = maxInterval;
    i = 0;
    while( i < maxStage && i < btlCurrentStage )
    {
        btlEnemyStartInterval = btlEnemyStartInterval - step;
        i = i + 1;
    }
    btlEnemyStartCount = 0;
}

bool btlStartEnemyRobo()
{
    int i, sprite;
    btlEnemyStartCount = btlEnemyStartCount + 1;
    if( btlEnemyStartCount < btlEnemyStartInterval ) return false;
    btlEnemyStartCount = 0;

    sprite = BTL_SPRITE_ENEMYROBO;
    for( i = 0; i < BTL_ENEMY_ROBO_COUNT; i = i + 1 )
    {
        if( ( btlEnemyRobos[ i ].status & BTL_ROBO_STATUS_MASK ) == BTL_ROBO_STATUS_NONE )
        {
            if( btlStartRobo( &btlEnemyRobos[ i ], btlEnemyFort.x, btlEnemyFort.y ) )
            {
                btlEnemyRobos[ i ].sprite = sprite;
                btlEnemyRobos[ i ].status = BTL_ROBO_STATUS_LIVE;
                if( ( btlEnemyRnd() & 1 ) != 0 )
                  btlEnemyRobos[ i ].status = btlEnemyRobos[ i ].status | BTL_ROBO_TARGET_MASK;
                btlEnemyRobos[ i ].count = 0;
                btlSetRoboDirection( &btlEnemyRobos[ i ], 0 );
                btlDecideDirection( &btlEnemyRobos[ i ] );
                btlDrawEnemyRobo( &btlEnemyRobos[ i ] );
                return true;
            }
        }
        sprite = sprite + 1;
    }
    return false;
}

void btlMoveEnemyRobos()
{
    int i, status, wall;
    bool fire;
    for( i = 0; i < BTL_ENEMY_ROBO_COUNT; i = i + 1 )
    {
        status = btlEnemyRobos[ i ].status & BTL_ROBO_STATUS_MASK;
        if( status != BTL_ROBO_STATUS_NONE )
        {
            if( ( btlEnemyRobos[ i ].step & BTL_STEP_MASK ) == 0 )
            {
                fire = false;
                wall = btlDecideDirection( &btlEnemyRobos[ i ] );
                if( wall != BTL_WALL_NONE )
                {
                    btlEnemyRobos[ i ].status = btlEnemyRobos[ i ].status & ~BTL_ROBO_CANMOVE_MASK;
                    if( wall < BTL_WALL_HARD || wall == BTL_WALL_MYROBO || wall == BTL_WALL_MYFORT )
                    {
                        if( ( btlEnemyRobos[ i ].count & 7 ) == 0 )
                          fire = true;
                    }
                }
                else
                  btlEnemyRobos[ i ].status = btlEnemyRobos[ i ].status | BTL_ROBO_CANMOVE_MASK;

                if( fire || ( ( btlEnemyRobos[ i ].count & 3 ) == 0 && btlEnemyRnd() <= btlCurrentStage ) )
                  btlFire( &btlEnemyRobos[ i ], false );

                btlEnemyRobos[ i ].count = btlWrap8( btlEnemyRobos[ i ].count + 1 );
                if( btlEnemyRobos[ i ].count == 0 )
                  btlEnemyRobos[ i ].status = btlEnemyRobos[ i ].status ^ BTL_ROBO_TARGET_MASK;
            }
            if( btlMoveRobo( &btlEnemyRobos[ i ] ) )
              btlDrawEnemyRobo( &btlEnemyRobos[ i ] );
        }
    }
}


// -----------------------------------------------------------------------------
//   MyRobo.cpp - remaining functions (spawn/movement/input).
// -----------------------------------------------------------------------------

#define BTL_BULLET_INTERVAL 20

void btlStartMyRobo( int status )
{
    btlMyRespawnTime = ( btlCurrentStage + 1 ) << 1;
    if( btlStartRobo( &btlMyRobo, btlMyFort.x, btlMyFort.y ) )
    {
        btlMyRobo.status = status;
        btlSetRoboDirection( &btlMyRobo, BTL_DIR_RIGHT );
        btlDrawMyRobo();
        btlMyIntervalCount = 0;
        btlMyRespawnCount = 0;
    }
}

// Upstream's own `goto move;`/`goto draw;` control flow (see this game's
// own MyRobo.cpp) rewritten as plain if/else with boolean flags, matching
// this project's own established treatment of intra-function gotos-as-
// structured-control-flow.
//
// **A real, severe bug found via direct user report ("the player can't
// move around")**: the ORIGINAL version of this rewrite put the entire
// function body - including the `btlMoveRobo(&btlMyRobo)` call - inside
// the `if((btlMyRobo.step & BTL_STEP_MASK)==0)` block. Upstream's real
// control flow is different: that same `(step&StepMask)==0` check only
// gates the INPUT-READING/direction-decision code, but the actual
// `MoveRobo(&MyRobo)` call sits at a `move:` label OUTSIDE/AFTER that
// block (upstream's own `goto move;`/plain fallthrough both jump there),
// so it runs on EVERY tick regardless of whether input was just
// re-evaluated that tick. `BTL_STEP_MASK`=7, meaning a single grid-cell
// crossing takes 8 real ticks - `btlMoveRobo()` itself only updates x/y
// once every 8th tick (its own `step&BTL_STEP_MASK==0` check) but still
// unconditionally increments `step` on the other 7 "in transit" ticks,
// which is what eventually brings it back around to a multiple of 8 so
// the next input read can happen. The buggy version never called
// `btlMoveRobo()` at all on those intermediate 7 ticks (the enclosing
// `if` was already false), so `step` could only ever be incremented from
// *within* that same `if` block - meaning the very FIRST successful move
// (step 0->1) permanently left `step` stuck at a non-multiple-of-8 value
// forever, and every subsequent call to this function saw
// `(step&BTL_STEP_MASK)!=0` and did nothing at all: no input read, no
// movement, no facing update - a single one-cell nudge at the very start
// of a life, then permanently frozen for its entire remaining lifetime.
// Confirmed the sibling `btlMoveEnemyRobos()` (a few functions above)
// already gets this right - its own `btlMoveRobo(&btlEnemyRobos[i])`
// call is unconditional, outside its own per-enemy AI-decision gate -
// this was purely a MyRobo-specific porting slip, not a project-wide
// misunderstanding of the pattern. **Fixed** by restructuring so the
// gated block only ever sets `cannotMove`/`drawOnly` flags (never
// directly returns except for the genuine "no direction held" case,
// matching upstream's own `cannot:`-labeled early `return;`), with
// `btlMoveRobo()`/`btlDrawMyRobo()` now called unconditionally at the
// very end of the function - the same "always reached, whether or not
// the gated block ran this tick" position upstream's own `move:` label
// occupies.
void btlMoveMyRobo()
{
    int status;
    bool cannotMove, drawOnly;

    status = btlMyRobo.status & BTL_ROBO_STATUS_MASK;
    if( status != BTL_ROBO_STATUS_LIVE )
    {
        if( status == BTL_ROBO_STATUS_NONE )
          btlStartMyRobo( BTL_ROBO_STATUS_WAIT );
        btlMyRespawnCount = btlWrap8( btlMyRespawnCount - 1 );
        if( btlMyRespawnCount == 0 )
          btlMyRobo.status = ( btlMyRobo.status & ~BTL_ROBO_STATUS_MASK ) | BTL_ROBO_STATUS_LIVE;
        btlDrawMyRobo();
        return;
    }

    cannotMove = false;
    drawOnly = false;

    if( ( btlMyRobo.step & BTL_STEP_MASK ) == 0 )
    {
        bool left, right, up, down, fire;
        int direction, wall, oldDirection;
        bool doMove, handled;

        left = isLeftPressed();
        right = isRightPressed();
        up = isUpPressed();
        down = isDownPressed();
        fire = isFirePressed();

        if( fire )
        {
            if( btlMyIntervalCount == 0 )
            {
                if( btlFire( &btlMyRobo, true ) )
                {
                    btlStartSeq( 0, BTL_MELODY_FIRE );
                    btlMyIntervalCount = BTL_BULLET_INTERVAL;
                }
            }
            else
              btlMyIntervalCount = btlMyIntervalCount - 1;
        }
        else
          btlMyIntervalCount = 0;

        doMove = false;
        handled = false;
        direction = 0;

        if( right )
        {
            direction = BTL_DIR_RIGHT;
            handled = true;
        }
        else if( left )
        {
            direction = BTL_DIR_LEFT;
            handled = true;
        }
        else if( down )
        {
            direction = BTL_DIR_DOWN;
            handled = true;
        }
        else if( up )
        {
            direction = BTL_DIR_UP;
            handled = true;
        }

        if( !handled )
        {
            btlMyRobo.status = btlMyRobo.status & ~BTL_ROBO_CANMOVE_MASK;
            cannotMove = true;
        }
        else
        {
            wall = btlTestMoveRobo( &btlMyRobo, direction );
            if( wall >= BTL_WALL_HARD && wall < BTL_WALL_ENEMYROBO )
            {
                oldDirection = btlMyRobo.status & BTL_ROBO_DIRECTION_MASK;
                if( btlTestMoveRobo( &btlMyRobo, oldDirection ) == 0 )
                  doMove = true;
            }
            if( !doMove )
            {
                btlSetRoboDirection( &btlMyRobo, direction );
                if( wall != 0 )
                  drawOnly = true;
                else
                  doMove = true;
            }
        }
    }

    if( cannotMove ) return;
    if( drawOnly )
    {
        btlDrawMyRobo();
        return;
    }
    if( btlMoveRobo( &btlMyRobo ) )
      btlDrawMyRobo();
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage()/InitTrying() (needs every subsystem above).
// -----------------------------------------------------------------------------

void btlDrawAll()
{
    int i;
    for( i = 0; i < BTL_VVRAM_WIDTH * BTL_VVRAM_HEIGHT; i = i + 1 )
      btlVVramFront[ i ] = btlVVramBack[ i ];
    btlDrawSprites();
}

void btlInitStage()
{
    int i, j;
    btlHideAllSprites();
    i = 0;
    j = 0;
    while( i < btlCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= BTL_STAGE_COUNT )
          j = 0;
    }
    btlStageIndex = j;
    btlFortInit( &btlEnemyFort, btlStageEnemyFort[ btlStageIndex ] );
}

void btlInitTrying()
{
    int i, x, y, colGroup, sourceByte, bits, sub;

    btlStageTime = 120;
    btlHideAllSprites();

    for( x = 0; x < BTL_VVRAM_WIDTH; x = x + 1 )
    {
        btlVVramBack[ btlVVramOffset( x, 0 ) ] = BTL_CHAR_FENCE;
        btlVVramBack[ btlVVramOffset( x, BTL_VVRAM_HEIGHT - 1 ) ] = BTL_CHAR_FENCE + 1;
    }

    y = 0;
    for( i = 0; i < BTL_MAP_HEIGHT; i = i + 1 )
    {
        x = 0;
        for( colGroup = 0; colGroup < BTL_MAP_WIDTH / 4; colGroup = colGroup + 1 )
        {
            sourceByte = btlStageBytes[ btlStageIndex ][ i * ( BTL_MAP_WIDTH / 4 ) + colGroup ];
            for( sub = 0; sub < 4; sub = sub + 1 )
            {
                bits = sourceByte & BTL_WALL_HARD;
                btlSetWall( x, y, bits );
                btlSetWall( x, y + 1, bits );
                x = x + 1;
                btlSetWall( x, y, bits );
                btlSetWall( x, y + 1, bits );
                x = x + 1;
                sourceByte = sourceByte >> 2;
            }
        }
        y = y + 2;
    }

    btlFortInit( &btlMyFort, btlStageMyFort[ btlStageIndex ] );
    btlRoboStartIndex = 0;
    btlStartMyRobo( BTL_ROBO_STATUS_LIVE );
    btlInitEnemyRobos();
    btlIntBullets();
    btlInitBangs();
    btlDrawForts();
}


// -----------------------------------------------------------------------------
//   Rendering - no hardware-orientation transform (see header comment):
//   the composed byte is drawn directly at its own (col,page).
// -----------------------------------------------------------------------------

int btlVVramAt( int x, int y )
{
    return btlVVramFront[ btlVVramOffset( x, y ) ];
}

// **OR-combines the map/VVram-derived byte with the status-text-derived
// byte instead of exclusively choosing one** - mirrors gameCracky.c's own
// crkComposeRawByte() fix exactly (see this file's own header comment,
// "The title screen's own decorative logo..." section). During normal
// PLAYING, btlStatusChar's own cols 0-23 are always left at index 0
// (space, an all-zero glyph) since nothing ever writes text there except
// the title screen itself (btlPrintStatus() only ever writes cols 24-31),
// so ORing them in here is a safe no-op during gameplay - the OR only
// actually contributes real pixels while btlFullWidthText is true (the
// title screen), where the real bitmap logo (drawn into btlVVramFront by
// btlBeginTitle(), occupying pages 1-2 only) and the MINI/START/CONTINUE/
// credit text (btlStatusChar, occupying pages 3/5/6/7) never collide -
// disjoint page ranges by construction, same reasoning as Cracky's own.
int btlComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < BTL_VVRAM_WIDTH * 4 )
    {
        if( btlOverlayActive && rawPage == btlOverlayPage &&
            rawCol >= btlOverlayCol * 4 && rawCol < btlOverlayCol * 4 + btlOverlayLen * 4 )
        {
            int i, sub;
            i = ( rawCol - btlOverlayCol * 4 ) / 4;
            sub = ( rawCol - btlOverlayCol * 4 ) % 4;
            return btlAsciiPattern[ btlAsciiIndex( btlOverlayText[ i ] ) * 4 + sub ];
        }
        {
            int mapX, sub, upper, lower, upperByte, lowerByte;
            mapX = rawCol / 4;
            sub = rawCol % 4;
            upper = btlVVramAt( mapX, rawPage * 2 );
            lower = btlVVramAt( mapX, rawPage * 2 + 1 );
            if( sub == 0 )
            {
                upperByte = btlCharPattern[ upper * 2 + 0 ];
                lowerByte = btlCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else if( sub == 1 )
            {
                upperByte = btlCharPattern[ upper * 2 + 0 ];
                lowerByte = btlCharPattern[ lower * 2 + 0 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
            else if( sub == 2 )
            {
                upperByte = btlCharPattern[ upper * 2 + 1 ];
                lowerByte = btlCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
            }
            else
            {
                upperByte = btlCharPattern[ upper * 2 + 1 ];
                lowerByte = btlCharPattern[ lower * 2 + 1 ];
                mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
            }
        }
    }

    if( !btlFullWidthText && rawCol < BTL_VVRAM_WIDTH * 4 )
      return mapByte;

    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = btlStatusChar[ rawPage ][ charCol ];
            textByte = btlAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void btlRender()
{
    int page, col, value;
    md_beginFrame();
    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            value = btlComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine - Main()'s own goto-chained labels/blocking waits
//   rewritten as an explicit frame-stepped state machine (see header
//   comment for the full BTL_STATE_* mapping).
// -----------------------------------------------------------------------------

// Shared "begin a fresh attempt at the current stage" helper - matches
// upstream's own `try_:` label (reached from a fresh game, a life lost
// with lives remaining, and every subsequent stage).
void btlBeginStageTry()
{
    btlTimeDenom = BTL_MAX_TIME_DENOM;
    btlInitTrying();
    // Real bug found via this verification pass, not upstream fidelity -
    // see this file's own header comment's "Bugs found and fixed" section.
    // Upstream's real `InitTrying()` calls `ClearScreen()`, which wipes the
    // whole physical OLED VRAM - including any leftover "TIME UP" text a
    // just-finished death sequence direct-wrote there - before a retried
    // attempt begins. This port's own message overlay has no equivalent
    // implicit clear, so without this explicit reset a TIME UP death with
    // lives remaining would leave "TIME UP" burned over every subsequent
    // attempt's own gameplay indefinitely (the only other place this flag
    // is ever cleared is `btlBeginTitle()`, never reached again until the
    // whole game truly ends). Matches `gameCracky.c`'s own identical fix
    // at its own stage-retry call site (`crkOverlayActive = false;`).
    btlOverlayActive = false;
    btlPrintStatus();
    while( !btlStartEnemyRobo() );
    btlDrawAll();
    btlStartSeq( 1, BTL_MELODY_START );
    btlEnemyRoboPhase = 0;
    btlState = BTL_STATE_START_JINGLE;
}

void btlBeginTitle()
{
    int i, j;
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };

    // Clear the whole VVram front buffer before drawing the title screen -
    // matches gameCracky.c's own identical full-clear at this exact point
    // (crkBeginTitle()) - without this, whatever gameplay/map content was
    // last composited into btlVVramFront would peek through in any cell
    // the logo bitmap below doesn't itself overwrite. Safe to write
    // directly into btlVVramFront (not btlVVramBack) here: nothing else
    // touches btlVVramFront while BTL_STATE_TITLE is active, and the next
    // real game's own btlDrawAll() (called from btlBeginStageTry(), right
    // after leaving this screen) unconditionally overwrites the ENTIRE
    // buffer from btlVVramBack before anything is ever rendered again - so
    // no separate cleanup is needed at the title->gameplay transition.
    for( i = 0; i < BTL_VVRAM_WIDTH * BTL_VVRAM_HEIGHT; i = i + 1 )
      btlVVramFront[ i ] = BTL_CHAR_SPACE;

    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          btlStatusChar[ i ][ j ] = 0;
    }
    btlOverlayActive = false;
    btlFullWidthText = true;
    btlHideAllSprites();
    // Reset before printing status - a real bug found via direct user
    // report on gameCracky.c's own identical title screen ("on game over,
    // the time value remains visible on titlescreen") - fixed proactively
    // here rather than waiting for the same report a second time.
    btlStageTime = 0;
    btlPrintStatus();

    // **Restored, matching gameCracky.c's own identical fix**: this is
    // upstream's own real "BATTLOT"-family logo bitmap (`Status.cpp`'s own
    // 96-value `TitleBytes[]` table), drawn directly into btlVVramFront at
    // its own real position (VVram rows 2-5, i.e. real hardware pages 1-2
    // - matching upstream's `Title()`'s own `VVramFront + VVramWidth*2 +
    // TitleLeft` starting offset exactly, with TitleLeft=(24-4*6)/2=0).
    // The earlier version of this function replaced this with plain small
    // ASCII text ("BATTLOT", missing its own 'B'/'L' glyphs entirely - see
    // this file's own header comment) reasoning it was "purely
    // decorative" - wrong, matching the exact same wrong call Cracky's own
    // earlier version made for its own logo: it's the actual title
    // wordmark, meant to be the single biggest, most prominent element on
    // the whole screen, not a throwaway detail. btlComposeRawByte() was
    // updated to OR-combine this VVram content with btlStatusChar's own
    // text layer rather than choosing one exclusively, since the two
    // occupy disjoint page ranges by construction (see that function's
    // own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                btlVVramFront[ btlVVramOffset( ch * 4 + col, 2 + row ) ] = btlTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns - all
    // genuinely clear of the status labels' own columns 24-31 and of the
    // logo's own pages 1-2, so nothing here needs trimming, relocating, or
    // dropping.
    // Real upstream column for "MINI": TitleLeft + 4*TitleLength - 5 =
    // 0 + 4*6 - 5 = 19 (Status.cpp's own Title(), `PrintS(Vram +
    // VramRowSize*3 + (TitleLeft+4*TitleLength-5)*VramStep, "MINI")`) -
    // found via this project's own cross-sibling verification pass
    // (triggered by the real hardware photo that fixed gameCracky.c's own
    // title screen) to have been ported as a plain col 0 instead, a real
    // positioning miss even though this file's own status-grid width and
    // every OTHER title-screen column already matched upstream exactly
    // from this port's very first draft. Fixed to the real value.
    btlPrintS( 3, 19, sMini, 4 );
    btlPrintS( 7, 12, sCredit, 12 );

    btlPrintS( 5, 9, sStart, 5 );
    btlPrintS( 6, 9, sContinue, 8 );
    btlSelection = 0;
    btlSelectionChanged = true;
    btlPrevLeft = 0; btlPrevRight = 0; btlPrevUp = 0; btlPrevDown = 0; btlPrevFire = 0;
    btlState = BTL_STATE_TITLE;
}

void btlUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !btlPrevLeft ) || ( right && !btlPrevRight ) ||
                ( up && !btlPrevUp ) || ( down && !btlPrevDown ) );
    justFire = ( fire && !btlPrevFire );
    btlPrevLeft = left; btlPrevRight = right; btlPrevUp = up; btlPrevDown = down; btlPrevFire = fire;

    if( btlSelectionChanged )
    {
        btlSelectionChanged = false;
        if( btlSelection == 0 )
          btlPrintC( 5, 8, '>' );
        else
          btlPrintC( 5, 8, ' ' );
        if( btlSelection == 1 )
          btlPrintC( 6, 8, '>' );
        else
          btlPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        btlFullWidthText = false;
        btlScore = 0;
        if( btlSelection == 0 )
          btlCurrentStage = 0;
        btlRemainCount = 3;
        btlInitStage();
        btlBeginStageTry();
        btlRender();
        return;
    }
    if( justDir )
    {
        btlSelection = btlSelection ^ 1;
        btlSelectionChanged = true;
    }
    btlRender();
}

void btlUpdateStartJingle()
{
    if( !btlSeqPlaying( 1 ) )
    {
        btlStartBgm();
        btlState = BTL_STATE_PLAYING;
    }
    btlRender();
}

// Shared 10-tick bang-animation pause, entered from BOTH the time-up path
// and the fort-destroyed path - see header comment for the one real
// ordering subtlety between the two (the fort-destroyed path keeps the
// BGM playing through the wait; the time-up path stops it immediately,
// before this function is ever reached).
void btlBeginDeathWait( bool viaTimeUp )
{
    btlDeathWaitFrames = 10;
    btlDeathViaTimeUp = viaTimeUp;
    btlState = BTL_STATE_DEATH_WAIT;
}

void btlBeginClearWait()
{
    btlStopBgm();
    btlClearWaitFrames = 10;
    btlState = BTL_STATE_CLEAR_WAIT;
}

// Main()'s own per-real-tick sub-stepping, translated to fixed call counts
// per real engine frame - see header comment ("A deliberate simplification
// for the real per-real-tick sub-stepping").
void btlUpdatePlaying()
{
    btlMoveMyBullets();
    btlMoveEnemyBullets();
    btlMoveMyBullets();
    btlUpdateBangs();
    btlMoveMyRobo();
    btlTimeDenom = btlTimeDenom - 1;
    if( btlTimeDenom == 0 )
    {
        btlStageTime = btlStageTime - 1;
        btlTimeDenom = BTL_MAX_TIME_DENOM;
        btlPrintTime();
        if( btlStageTime == 0 )
        {
            btlStopBgm();
            btlPrintTimeUp();
            btlDrawAll();
            btlRender();
            btlBeginDeathWait( true );
            return;
        }
    }
    if( btlEnemyRoboPhase == 0 )
    {
        btlStartEnemyRobo();
        btlMoveEnemyRobos();
    }
    btlEnemyRoboPhase = btlEnemyRoboPhase ^ 1;
    btlMoveMyBullets();
    btlMoveEnemyBullets();
    btlMoveMyBullets();
    btlDrawAll();

    if( btlMyFort.life == 0 )
    {
        btlRender();
        btlBeginDeathWait( false );
        return;
    }
    if( btlEnemyFort.life == 0 )
    {
        btlRender();
        btlBeginClearWait();
        return;
    }
    btlRender();
}

void btlUpdateDeathWait()
{
    if( btlDeathWaitFrames > 0 )
    {
        btlUpdateBangs();
        btlDeathWaitFrames = btlDeathWaitFrames - 1;
        btlDrawAll();
        btlRender();
        return;
    }
    if( !btlDeathViaTimeUp )
      btlStopBgm();
    btlRemainCount = btlRemainCount - 1;
    if( btlRemainCount > 0 )
      btlBeginStageTry();
    else
    {
        btlPrintGameOver();
        btlStartSeq( 1, BTL_MELODY_GAMEOVER );
        btlState = BTL_STATE_GAMEOVER_JINGLE;
    }
    btlRender();
}

void btlUpdateGameOverJingle()
{
    if( !btlSeqPlaying( 1 ) )
      btlBeginTitle();
    else
      btlRender();
}

void btlUpdateClearWait()
{
    if( btlClearWaitFrames > 0 )
    {
        btlUpdateBangs();
        btlClearWaitFrames = btlClearWaitFrames - 1;
        btlDrawAll();
        btlRender();
        return;
    }
    btlStartSeq( 1, BTL_MELODY_CLEAR );
    btlState = BTL_STATE_CLEAR_JINGLE;
    btlRender();
}

void btlUpdateClearJingle()
{
    if( !btlSeqPlaying( 1 ) )
    {
        btlState = BTL_STATE_BONUS_TALLY;
        btlBonusWaitFrames = 0;
    }
    btlRender();
}

// Bonus-tally loop - a real per-iteration pause of `btlNoteFrames(1)` (the
// Beep note's own real duration, since upstream's own `Sound_Beep()` is a
// BLOCKING `WaitMelody`) PLUS a separate additional `WaitTimer(5)` - see
// header comment.
void btlUpdateBonusTally()
{
    if( btlBonusWaitFrames > 0 )
    {
        btlBonusWaitFrames = btlBonusWaitFrames - 1;
        btlRender();
        return;
    }

    if( btlStageTime >= BTL_BONUS_RATE )
    {
        btlAddScore( 3 );
        btlStageTime = btlStageTime - BTL_BONUS_RATE;
        btlPrintTime();
        btlDrawAll();
        btlStartSeq( 0, BTL_MELODY_BEEP );
        btlBonusWaitFrames = btlNoteFrames( 1 ) + 5;
        btlRender();
        return;
    }

    btlStageTime = 0;
    btlPrintStatus();
    btlCurrentStage = btlCurrentStage + 1;
    btlInitStage();
    btlBeginStageTry();
    btlRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameBattlot_init()
{
    int i;

    btlHiScore = 0;
    btlScore = 0;
    btlCurrentStage = 0;
    btlRemainCount = 3;
    btlStageTime = 0;
    btlEnemyRndIndex = 0;
    btlRoboStartIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        btlSeqActive[ i ] = 0;
        btlSeqMelody[ i ] = BTL_MELODY_NONE;
    }
    btlOverlayActive = false;

    btlBeginTitle();
}

void gameBattlot_update()
{
    btlAdvanceSound();

    if( btlState == BTL_STATE_TITLE )
      btlUpdateTitle();
    else if( btlState == BTL_STATE_START_JINGLE )
      btlUpdateStartJingle();
    else if( btlState == BTL_STATE_PLAYING )
      btlUpdatePlaying();
    else if( btlState == BTL_STATE_DEATH_WAIT )
      btlUpdateDeathWait();
    else if( btlState == BTL_STATE_GAMEOVER_JINGLE )
      btlUpdateGameOverJingle();
    else if( btlState == BTL_STATE_CLEAR_WAIT )
      btlUpdateClearWait();
    else if( btlState == BTL_STATE_CLEAR_JINGLE )
      btlUpdateClearJingle();
    else if( btlState == BTL_STATE_BONUS_TALLY )
      btlUpdateBonusTally();
}
