// =============================================================================
// NEURAS mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_neuras`, same author/hardware/
// engine lineage as this project's own already-shipped Cracky port) - pick up
// and throw numbered cards, lining up two (or more) cards sharing the same
// number in a row/column to clear them; clear every card on a 32x16-cell
// stage to advance, across 8 hand-authored stages, while dodging a small
// chasing-monster roster, with a real countdown clock and 3 lives.
//
// Ported directly following `gameCracky.c`'s own already-proven structural
// patterns (same "Cate engine": `VVram`/`Vram`/`Chars`/`Sound`/`Timer`/`Oled`
// files are close cousins of Cracky's own, several data tables - the shared
// `logo`+`wall` glyph rows in `CharPattern`, the identical 27-character
// `AsciiPattern` table - are byte-identical between the two games) - see that
// file's own header comment for the shim/rendering architecture this port
// reuses wholesale. Only 4 directions + 1 action button
// (`isLeftPressed()`/`isRightPressed()`/`isUpPressed()`/`isDownPressed()`/
// `isFirePressed()`), no new shim primitive needed - `isFire2Pressed()` goes
// unused here too, matching Cracky.
//
// **No hardware display-orientation transform is needed here either** -
// confirmed directly from `Oled.h`'s own `SegRemap`/`ComScanDec` command
// values, identical to Cracky's own already-settled (after a real, painful,
// user-reported debugging saga - see that file's own header) conclusion:
// these two real-hardware panel-mounting-correction commands have no
// equivalent thing to correct for in a software recreation. `neuComposeRawByte
// (col,page)` is drawn directly at its own `(col,page)` via `md_drawColumn()`
// - no column mirror, no page reorder, no bit-reversal.
//
// **Rendering is the same two-level tile system as Cracky, ported the same
// way**: `VVram` is a 24x16 logical glyph-index grid (`neuVVram`);
// `VVramToVram()`'s own `SendUL()` nibble-interleaving is reproduced exactly
// in `neuComposeRawByte()`/`neuIconByte()`. Unlike Cracky (which keeps a
// separate persistent `VVramBack`/`VVramFront` pair plus a dirty-tracking
// `Backup[]` purely as a real-I2C-bandwidth optimization with no analogue
// here), this port rebuilds the *entire* `neuVVram` grid fresh every single
// frame from three layers (walls, floor cards, sprites) via `neuDrawAll()`,
// matching this project's own standing "always redraw the full frame, don't
// replicate a VRAM-persistence-reliant partial-redraw trick" precedent -
// the same choice Cracky's own `crkDrawAll()` already made for this exact
// engine.
//
// **A genuinely new rendering wrinkle beyond what Cracky needed**: two small
// UI elements - the "how many lives remain" icon strip and the "currently
// held card" icon - are drawn via upstream's `Put2C()` (a *direct*-to-
// hardware, VVram-bypassing 2x2-glyph icon blit, distinct from `PrintC()`'s
// plain-ASCII text blit) rather than through the VVram grid at all. Ported
// with a small dedicated `neuIconByte()` helper (an exact re-derivation of
// `Put2C()`'s own nibble math, confirmed by hand-tracing which two glyphs
// -c/c+1 upper, c+2/c+3 lower - contribute to which of the 4 output columns)
// plus two small pieces of state (`neuHeldCardGlyph`, `neuRemainIconCount`)
// that `neuComposeRawByte()` checks before falling back to the plain
// ASCII-text status grid for the rest of the status column - avoids needing
// upstream's own direct-Vram-write model entirely.
//
// **The title-screen logo draws upstream's own real 96-value `TitleBytes`
// bitmap table (`Status.cpp`'s `Title()`), the same fix already applied to
// the sibling game Cracky's own "CRACKY" logo (see that file's own header
// comment and CLAUDE.md's own "A second, related architectural issue"
// writeup for the full story).** An earlier version of this port
// simplified this ornate-but-supposedly-"decorative" pixel-art wordmark
// down to plain small ASCII text - that reasoning was wrong: it's the
// single biggest, most prominent element on the whole title screen, not
// filler. `neuTitleBytes[]` (byte-diff-extracted from the real upstream
// source via a small Python script) is now drawn directly into `neuVVram`
// at its own real position (VVram rows 2-5 / hardware pages 1-2, matching
// upstream's own `VVramFront + VVramWidth*2 + TitleLeft(0)` offset exactly
// - `TitleLeft=0` since the 6-letter "NEURAS" wordmark exactly fills the
// full 24-cell `VVramWidth`), with `neuComposeRawByte()` OR-combining that
// VVram content with the status-grid text layer rather than choosing one
// exclusively - the two occupy disjoint page ranges by construction, so
// they can never actually collide. See `neuBeginTitle()`'s own comment for
// the exact drawing loop. The credit line ("INUFUTO 2026") and "MINI" are
// both drawn at upstream's own real, literal columns too, once
// `neuStatusChar` was widened from an original, wrong 8-column grid to the
// real 32-column-per-page canvas upstream actually has (see that global's
// own header comment) - nothing needs shortening or dropping anymore.
//
// **The GAME OVER / TIME UP messages reuse Cracky's own overlay mechanism
// exactly** (`neuOverlayActive`/`neuOverlayText`/`neuOverlayLen`/
// `neuOverlayPage`/`neuOverlayCol`, checked by `neuRender()` before falling
// back to `neuComposeRawByte()`) - both messages are, in real upstream too,
// drawn directly into the *map* area (not the status area) via `PrintS()`
// calls that bypass VVram, so this is the same "persists on real hardware
// with nothing else re-touching those exact bytes" situation Cracky's own
// header comment already documents and solves the same way.
//
// **The blocking upstream control flow (Main.cpp's own goto-chained
// `title:`/`try_:`/`lose:` labels around one big `while(true)`+`do..while`,
// plus several real `WaitMelody()`/`WaitTimer()` blocking waits scattered
// through `Sound.cpp`/`Card.cpp`) is rewritten as an explicit frame-stepped
// state machine**, the usual treatment every port in this project needs:
// NEU_STATE_TITLE (`Title()`'s own key-poll loop), NEU_STATE_START_JINGLE
// (the blocking `Sound_Start()` held before play begins - and again once a
// stage's own bonus tally finishes, matching upstream's *identical* `try_:`
// entry sequence being reached from three different places: a fresh game, a
// life-lost retry, and stage advance), NEU_STATE_PLAYING (the main
// tick-gated loop), NEU_STATE_MATCH_REVEAL (see below), NEU_STATE_DIE_ANIM
// (`EndSolver()`'s own 20-step blink-and-beep loop, monster-collision death
// only), NEU_STATE_TIMEUP_SWEEP (the real `repeat(15){Sound_Loose();}` after
// a countdown reaches 0 - time-up death has *no* visible blink animation of
// its own, unlike a monster-collision death, matching upstream exactly),
// NEU_STATE_GAMEOVER_JINGLE, NEU_STATE_CLEAR_JINGLE (`Sound_Clear()`), and
// NEU_STATE_BONUS_TALLY (the real `while(StageTime>=BonusRate){...
// Sound_Beep();WaitTimer(1);}` bonus-countdown loop - note this one genuinely
// combines *two* separate upstream waits per iteration, the tempo-based
// `Sound_Beep()` blocking duration *plus* one additional literal
// `WaitTimer(1)` tick after it finishes - `neuWaitFrames = neuNoteFrames(1)
// + 1` reproduces both together, the one bonus-tally-specific case where a
// sound wait and a raw tick wait stack, checked carefully against every
// other single-sound-wait state in this file to confirm none of the others
// have the same extra tick tacked on).
//
// **`TestMatching()` - the one piece of upstream control flow with no direct
// analogue anywhere in Cracky - needed its own genuinely new state,
// NEU_STATE_MATCH_REVEAL.** Landing a thrown card triggers a scan through
// every other floor card for one aligned within 2 cells on the same row/
// column; a match blocks for 30 real ticks (a literal `WaitTimer(30)`, not
// tempo-based - reproduced as a direct `neuWaitFrames=30`) then removes both
// cards and stops; a *mismatch* also blocks 30 ticks (showing both cards'
// real face value first) but then, subtly, does **not** stop - upstream's
// own `for` loop has no `return` on the mismatch branch, so it keeps
// scanning for *further* aligned candidates from the same landing event,
// each one getting its own independent 30-tick reveal, only stopping once
// either a real match is found or the scan reaches the end of the card
// list. Reproduced with `neuBeginMatchScan()`/`neuContinueMatchScan()` (a
// synchronous "skip past cards that don't qualify, stop and pause the whole
// game the instant one does" scan, re-entered after each mismatch resolves)
// and `neuUpdateMatchReveal()` (the 30-tick pause + resolve step) - and,
// since upstream's real `WaitTimer(30)` blocks the *entire* CPU (monster/
// solver/clock included, since nothing past `MoveCard()` in the same loop
// iteration can run until it returns), `NEU_STATE_MATCH_REVEAL` is a genuine
// top-level state that freezes gameplay entirely while active, the same way
// `NEU_STATE_DIE_ANIM` does - `neuUpdatePlaying()` bails out immediately
// (skipping `MoveMonsters()`/the clock/the RemainCardCount check for that
// tick) the instant `neuMoveCard()` triggers a reveal, matching upstream's
// real control flow exactly rather than letting monster movement sneak in
// on the same tick a card lands.
//
// **A real, if likely upstream-unpolished, ambiguity around dying while
// holding or having just thrown a card, resolved deliberately rather than
// replicated literally.** `InitStage()` (fresh wallmap+cards) only ever runs
// once per *stage*, not on a life-lost retry (`goto try_` resumes *after*
// it) - `InitCards()`'s own `pHeldCard=pThrownCard=nullptr` reset therefore
// never re-runs on retry either, so a card that was genuinely Held or
// in-flight (Thrown) at the exact moment of death would, in upstream, stay
// permanently stuck in that status forever (nothing ever puts it back),
// silently keeping `RemainCardCount` from ever reaching 0 for that one card.
// Whether this is an intentional harsh design choice or a genuine, never-
// noticed upstream oversight is impossible to tell from the source alone.
// Resolved here with `neuResetHeldOrThrownCardOnRetry()` (called from
// `neuInitTrying()`, so it runs on every retry as well as harmlessly on a
// fresh stage where there's nothing to reset): a Held card is simply
// returned to Floor status at its own already-unchanged x/y (its position
// was never touched while held); a Thrown card is snapped to Floor at its
// current in-flight position, rounded to the nearest cell the same way a
// normal landing does. A deliberate, documented, low-risk simplification of
// a genuinely underspecified edge case, not a literal port.
//
// **A real, pervasive AVR-implicit-byte-wraparound-reliance hazard, found by
// inspection before ever compiling - the same bug family this whole project
// has hit repeatedly, here showing up via two distinct mechanisms in the
// same file.** (1) `InitStage()`'s own wall-bitmap-building algorithm relies
// on two local `byte` variables (`wallBit`, `bit`) overflowing from 0x80<<1
// back to 0 to know when 8 bits have been consumed and it's time to advance
// to the next source byte - fixed the standard way, `& 0xFF` after every
// shift, explicit `==0` check. (2) Nearly every proximity/collision check in
// `Card.cpp`/`Monster.cpp` (`TestToMove()`, `TestMatching()`,
// `Monster_TestHit()`, `MoveMonsters()`'s own solver-collision check) uses
// the classic `static_cast<byte>(diff+N) < M` idiom to test "is this
// difference within a small range" by deliberately relying on a *negative*
// byte subtraction wrapping around to a large positive value rather than
// computing an absolute value - ported with an explicit `neuByteWrap()`
// helper (`v & 0xFF`, correct for negative `v` too since Vircon32 ints are
// genuine 32-bit two's complement, matching this project's own established
// use of the same trick for `md_drawColumn()`'s own byte-truncation fix)
// applied at each such site, reproducing the exact same wraparound-based
// range test. (3) A related, higher-stakes case: `SolverX`/`SolverY`/
// `CardX`/`CardY`/`Monster.x`/`Monster.y` are *all* declared `byte` upstream,
// meaning **every** arithmetic update to them (a step left/right/up/down, a
// card's own flight, a monster's own step) is itself an implicit 0-255
// wraparound - not just the two collision-check idioms above. A bare,
// unwrapped `newX = SolverX + dx` landing on a genuinely negative value
// before ever reaching `TestMap2()` would, on Vircon32's own *logical* (not
// arithmetic) right shift, corrupt `TestMap2()`'s own wall-bitmap-index
// arithmetic into a huge value rather than the small, safe, wrapped-to-255
// index real hardware would compute - fixed by applying `neuByteWrap()` at
// every point an upstream `byte`-typed position variable is written
// (`neuSolverMove()`'s/`neuMoveCard()`'s/`neuMoveMonsters()`'s own `newX`/
// `newY` locals) *and*, as a second, independent safety layer, at the entry
// of every function whose own upstream signature takes `byte x, byte y`
// parameters (`neuTestMap2()`, `neuTestToMove()`, `neuThrowCard()`,
// `neuMonsterTestHit()`, `neuShowSprite()`) - reproducing the real
// parameter-truncation semantic a `byte`-typed C++ parameter gives for free,
// regardless of what a caller passed in. `neuTestMap2()` additionally gets
// an explicit bounds guard on its own `neuWallMap[]` index before ever
// reading it (return `false`, i.e. "blocked", rather than an out-of-bounds
// read) - a defensive fix matching this project's own repeated "preserve
// behavior, guard the crash" precedent (e.g. Tiny Arena's `Lvl1` fix),
// since proving *by hand* that every wrapped coordinate always stays within
// the real 16-row map (rather than trusting the border-wall data to always
// prevent it) wasn't worth the risk of a subtly-wrong manual proof.
//
// **Frame-pacing: `NEU_TICK_DIVISOR = 1` (effectively unthrottled), a
// deliberate, reasoned choice rather than a direct port of a single
// `WaitTimer(N)` call.** Unlike most of this project's other genuine-rate
// throttled ports, NEURAS's own real hardware timing isn't a single combined
// logic-and-redraw divisor the way Cracky's `CRK_TICK_DIVISOR=8` is - it's a
// *redraw-only* throttle layered on top of logic that already runs at the
// full real 60Hz rate: the main `do..while` body calls `MoveSolver()`/
// `MoveCard()`/`MoveMonsters()` every single bare-loop iteration, with only
// the *redraw* (`if((clock&1)==0){DrawAll();WaitTimer(2);}`) skipped every
// other iteration purely to save real I2C bandwidth - over any 2 iterations,
// exactly one `WaitTimer(2)` call (~33ms) elapses, meaning each individual
// logic iteration still averages a genuine 1/60s of real time regardless of
// whether it happens to redraw. Matching this project's own repeated
// "drop the real-hardware VRAM-bandwidth redraw-skip trick, redraw every
// frame" precedent (Tiny Pacman's `FPS_Control` split, etc) rather than
// halving the solver's real movement speed by naively applying the
// `WaitTimer(2)` value as a logic-throttling divisor, `NEU_TICK_DIVISOR=1`
// (a no-op today, kept for structural symmetry with every other tick-gated
// port in this project and as an obvious single knob if retuning is ever
// wanted) reproduces the *true* ~60Hz logic rate directly, with every frame
// redrawn. This also happens to match the literal `WaitTimer(1)` value used
// elsewhere in this same file's own bonus-tally loop.
//
// EEPROM/persistent high-score save is out of scope for this port (upstream
// itself has its own `// word HiScore;`/`// if(Score>HiScore)` lines
// commented out - a real, deliberately-disabled feature on the original
// hardware too, left disabled here to match rather than added as a new
// enhancement).
//
// **A meticulous line-by-line re-verification pass against the real
// upstream source** (dispatched after this port's own initial batch-ported
// state was flagged, project-wide, as "not independently verified - some
// games in this batch don't draw well or have state-progression issues,
// exact games unnamed"). Every draw/compositing function, every data table
// (byte-diffed programmatically against the real `Stages.cpp`/`Chars.cpp`/
// `Sound.cpp` source, not eyeballed), every position/offset formula, and
// the entire state machine were traced against the real `.cpp` files
// line-by-line before ever touching the emulator - confirmed correct:
// `neuMapToVVram()`'s 3-pass wall-bit-to-glyph conversion (matches
// `MapToVVram()`'s own in-place-mutation read order exactly, including
// which neighbor reads see already-transformed vs. still-raw data);
// `neuVPut2()`/`neuDrawSpritesIntoVVram()`'s glyph-offset math (matches
// `VPut2()`/`DrawSprites()`'s own pointer arithmetic exactly); the whole
// map-area nibble-interleave in `neuComposeRawByte()` (matches `SendUL()`/
// `VVramToVram()`'s own formula exactly); `neuIconByte()`'s 2x2-icon
// nibble math (matches `Put2C()`'s own byte-pair-advance-through-4-glyphs
// trick exactly, re-derived by hand from the real pointer arithmetic);
// every stage's wall bytes/card positions+numbers/monster positions (all
// byte-identical, confirmed via a Python re-extraction script); every
// sound melody table (all 10, byte-identical, confirmed the same way);
// the tempo-to-real-frame formula (independently re-derived from
// `Timer.cpp`'s real 60Hz `SysTick`+`Sound.cpp`'s own `Tempo`/`600/2`
// accumulator, confirms `neuNoteFrames()`'s `*1.363636` constant exactly);
// `neuDecideMonsterDirection()` (matches `DecideDirection()` element-for-
// element, including the real upstream `SolverX < pMonster->y` typo
// already correctly preserved); `neuMoveMonsters()`'s stop-count wraparound
// stun timer (a deliberate 0-to-255 byte-wrap stun duration, correctly
// reproduced via `neuByteWrap()`); the whole card-matching scan/reveal
// state machine (`neuBeginMatchScan()`/`neuContinueMatchScan()`/
// `neuUpdateMatchReveal()`, confirmed to resume scanning from the same
// index a real C++ `for` loop's own monotonic pointer advance would);
// `neuMoveSolver()`'s direction-priority/`keyOn` logic (matches
// `MoveSolver()`'s own `goto moved`-based short-circuit exactly).
//
// **Two real bugs found this pass, both fixed:**
//
// 1. **A real, screenshot-confirmed title-screen rendering bug**, first
//    fixed with a narrow patch, then **fully re-fixed a second time** once
//    a real user-supplied hardware photo of the sibling game Cracky proved
//    that first patch's own root-cause diagnosis was wrong (see
//    `neuStatusChar`'s own header comment and `neuBeginTitle()`'s own
//    comment for the full corrected story - this entry is kept for
//    history). `neuPrintStatus()` (called at the very top of
//    `neuBeginTitle()`) claims pages 0 (SCORE label)/1 (score digits)/3
//    (STAGE label+number)/5 (TIME label+digits)/7 (remain-life icons,
//    shown whenever `neuRemainCount>1` - true on cold boot, since
//    `gameNeuras_init()` sets it to 3 before ever reaching the title
//    screen, matching upstream's own identical `Main()` sequence) - the
//    *original* 8-page x 8-column `neuStatusChar` status grid this port
//    started with genuinely only had columns 0-7 to work with at all, so
//    every one of those 5 status pages left just pages 2/4/6 free, and
//    "MINI"/"START"/"CONTINUE" collided with SCORE/STAGE/TIME's own text
//    (screenshotted at the time as garbled "MINIE 01", "▸START 0", and a
//    genuine out-of-bounds write - `neuStatusChar[6][8]` silently aliasing
//    to `neuStatusChar[7][0]` via the flat row-major layout, corrupting
//    the credit line's own first letter). The *first* fix (dropping
//    "MINI"/the "INUFUTO" credit line outright, moving "START" to page 4,
//    truncating "CONTINUE" to "CONTINU") made this port render cleanly,
//    but was a symptom patch: it never questioned whether the 8-column
//    grid itself was the right model. Once Cracky's own real hardware
//    photo proved upstream's actual Vram canvas is a genuine 32-column-
//    per-page space (status labels confined to columns 24-31, title text
//    living at columns 8-23 - never actually colliding with anything on
//    real hardware), `neuStatusChar` was widened to `[8][32]` and
//    `neuBeginTitle()` was rewritten a second time to place every piece of
//    text (now including the previously-dropped "MINI" and "INUFUTO 2026"
//    credit line, and the full un-truncated "CONTINUE") at its real
//    upstream column - see that function's own current comment for the
//    exact columns used. Verified via a rebuilt-and-rescreenshotted title
//    screen showing every element ("NEURAS"/"MINI"/"SCORE"/"STAGE"/
//    "▸START"/"CONTINUE"/"TIME"/"INUFUTO 2026"/life icons) rendering fully,
//    non-overlapping, and un-truncated.
//
// 2. **A real timing-fidelity bug in the match-reveal pause**, found by
//    precisely deriving the real-time duration of `Sound_Hit()`/
//    `Sound_Miss()` from `Sound.cpp`'s own real tempo math (8 notes x
//    `neuNoteFrames(1)` = 8 real frames) and comparing it against how the
//    port's own `neuContinueMatchScan()` used it. Upstream's own
//    `TestMatching()` calls `Sound_Hit()`/`Sound_Miss()` - which
//    genuinely BLOCK the whole CPU via `WaitMelody()`'s busy-wait until
//    the cue finishes playing - and only THEN starts the separate,
//    literal `WaitTimer(30)` (`Sound_Hit(); WaitTimer(30);`, sequential,
//    not concurrent) - a real total pause of ~38-41 real ticks, not just
//    30. This project's own multi-voice `md_playTone()` (see the top-level
//    CLAUDE.md's own writeup of that change) means the port's SFX channel
//    and its own `neuWaitFrames` countdown run CONCURRENTLY instead,
//    which - unlike upstream's real blocking call - meant the previous
//    `neuWaitFrames=30` alone under-counted the true pause by the sound's
//    own ~8-tick duration (since 8 < 30, the sound was already finished
//    well before the reveal resolved, contributing nothing extra). This
//    is the exact same bug shape this port's own `neuUpdateBonusTally()`
//    already correctly handles for its own `Sound_Beep()`+`WaitTimer(1)`
//    stack (`neuWaitFrames = neuNoteFrames(1) + 1`) - simply not also
//    applied here at initial port time. **Fixed** with a new
//    `neuMelodyTotalFrames(id)` helper (sums `neuNoteFrames()` across
//    every note of a melody) and `neuWaitFrames = 30 +
//    neuMelodyTotalFrames(NEU_MELODY_HIT/MISS)` in `neuContinueMatchScan()`
//    - a ~27% longer, now-faithful reveal pause. A purely-timing fix (the
//    match/mismatch resolution itself, the score, and the card state were
//    all already correct before this) - verified by rebuilding cleanly
//    and re-confirming, via Puppeteer, that picking up and throwing a
//    card still renders correctly through the pickup (auto-triggered by
//    walking onto a floor card, matching upstream's own `PickCard()`
//    call inside `MoveSolver()` with no button gating) and throw sequence
//    with no corruption, though this specific improvised test throw
//    happened not to land aligned with another card, so the reveal state
//    itself wasn't re-screenshotted after this exact fix - low risk, since
//    the fix only changes a wait-duration constant, not any control flow,
//    and the state's own rendering was already screenshot-verified
//    correct (2x-zoomed pixel measurements confirming the held-card icon
//    and remain-life icons render at their correct, non-overlapping page
//    positions) earlier in the same session.
//
// Also specifically re-examined, per this pass's own instructions to
// treat every "not independently verified"/"deviation"/"simplification"
// note as a real lead: the deliberate held/thrown-card retry resolution
// (confirmed reasonable, no upstream "correct" answer exists to compare
// against - a real, inherently ambiguous edge case, not a bug); the
// initial monster-facing-direction default (`NEU_DIR_LEFT` instead of a
// null-equivalent sentinel - confirmed deliberate and safety-motivated,
// since upstream's own literal behavior there is an unavoidably undefined
// null-pointer dereference with no faithful port possible, and the actual
// behavioral difference is confined to one narrow initial-direction tie-
// break case with no crash risk either way); `neuCurrentStage`'s
// unbounded `int` growth vs. upstream's real `byte` wraparound-at-256
// (confirmed to compute an identical `neuStageIndex` regardless, since
// 8 divides 256 evenly - the display-only divergence would only start
// after 256 consecutive stage clears in one sitting, judged not worth
// guarding against). None of these needed a code change.
//
// **This port needs no `forceRedraw` hook** - every real state
// (`neuUpdateTitle()`/`neuUpdateStartJingle()`/`neuUpdatePlaying()`/
// `neuUpdateMatchReveal()`/`neuUpdateDieAnim()`/`neuUpdateTimeUpSweep()`/
// `neuUpdateGameOverJingle()`/`neuUpdateClearJingle()`/
// `neuUpdateBonusTally()`) calls `neuDrawAll()`+`neuRender()` (or, on the
// pure-wait-frame path, `neuRender()` alone, which still recomposes and
// redraws every column from the already-current `neuVVram`) unconditionally
// every single tick, matching this project's own standing "always redraw
// the full frame" choice already established for Cracky - there is no
// dirty-flag/skip-redraw path anywhere in this file that a quit-dialog
// resume could ever leave stale.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into neuCharPattern (map tiles/icons)
// -----------------------------------------------------------------------------

#define NEU_CHAR_SPACE 0x00
#define NEU_CHAR_WALL 0x10
#define NEU_CHAR_SOLVER_LEFT 0x20
#define NEU_CHAR_SOLVER_RIGHT 0x28
#define NEU_CHAR_SOLVER_UP 0x30
#define NEU_CHAR_SOLVER_DOWN 0x38
#define NEU_CHAR_MONSTER_LEFT 0x40
#define NEU_CHAR_MONSTER_RIGHT 0x48
#define NEU_CHAR_MONSTER_UP 0x50
#define NEU_CHAR_MONSTER_DOWN 0x58
#define NEU_CHAR_CARD 0x60

// -----------------------------------------------------------------------------
//   Stage.h / Card.h / Monster.h / Sprite.h - geometry + entity constants
// -----------------------------------------------------------------------------

#define NEU_VVRAM_WIDTH 24
#define NEU_VVRAM_HEIGHT 16
#define NEU_MAP_WIDTH 4
#define NEU_WALLMAP_SIZE ( NEU_MAP_WIDTH * NEU_VVRAM_HEIGHT )

#define NEU_CARD_STATUS_NONE 0x00
#define NEU_CARD_STATUS_FLOOR 0x10
#define NEU_CARD_STATUS_HELD 0x20
#define NEU_CARD_COUNT 26
#define NEU_MAX_CARDS_PER_STAGE 14

#define NEU_MONSTER_NONE 0
#define NEU_MONSTER_NORMAL 1
#define NEU_MONSTER_STOP 2
#define NEU_MONSTER_COUNT 4

#define NEU_SOLVER_DIE 0
#define NEU_SOLVER_LIVE 1

#define NEU_SPRITE_MONSTER 0
#define NEU_SPRITE_SOLVER 4
#define NEU_SPRITE_CARD 5
#define NEU_SPRITE_COUNT 6
#define NEU_INVALID_CODE 255

#define NEU_STAGE_COUNT 8

#define NEU_DIR_LEFT 0
#define NEU_DIR_RIGHT 1
#define NEU_DIR_UP 2
#define NEU_DIR_DOWN 3

#define NEU_TIME_RATE 100
#define NEU_BONUS_RATE 3

#define NEU_SPEED_NUMERATOR 3
#define NEU_SPEED_DENOMINATOR 7

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching upstream.
// -----------------------------------------------------------------------------

#define NEU_MELODY_NONE 0
#define NEU_MELODY_LOOSE 1
#define NEU_MELODY_BEEP 2
#define NEU_MELODY_GET 3
#define NEU_MELODY_HIT 4
#define NEU_MELODY_MISS 5
#define NEU_MELODY_START 6
#define NEU_MELODY_CLEAR 7
#define NEU_MELODY_GAMEOVER 8
#define NEU_MELODY_BGM1 9
#define NEU_MELODY_BGM2 10

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph, byte-identical
// to Cracky's own copy of the same table (shared Cate-engine font).
int neuAsciiPattern[108] = {
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

// CharPattern - 152 map-tile/icon glyphs, 2 bytes/glyph.
int neuCharPattern[304] = {
    0, 0, 51, 0, 204, 0, 255, 0,
    0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255,
    0, 0, 15, 0, 0, 240, 15, 240,
    17, 17, 30, 17, 17, 225, 30, 225,
    136, 136, 135, 136, 136, 120, 135, 120,
    153, 153, 150, 153, 153, 105, 5, 0,
    160, 191, 239, 0, 248, 55, 247, 11,
    160, 191, 239, 0, 50, 183, 127, 33,
    0, 254, 251, 10, 176, 127, 115, 143,
    0, 254, 251, 10, 18, 247, 123, 35,
    224, 255, 239, 0, 241, 63, 119, 3,
    224, 255, 239, 0, 115, 55, 255, 1,
    0, 190, 191, 14, 48, 119, 243, 31,
    0, 190, 191, 14, 16, 255, 115, 55,
    168, 175, 239, 8, 16, 115, 191, 0,
    64, 78, 206, 0, 50, 247, 255, 2,
    128, 254, 250, 138, 0, 251, 55, 1,
    0, 236, 228, 4, 32, 255, 127, 35,
    232, 239, 239, 8, 48, 247, 55, 0,
    192, 206, 206, 0, 113, 255, 127, 1,
    128, 190, 190, 142, 0, 115, 127, 3,
    0, 108, 108, 12, 16, 247, 255, 23,
    190, 181, 181, 14, 167, 173, 173, 7,
    126, 219, 123, 14, 199, 238, 206, 7,
    190, 93, 181, 14, 215, 220, 221, 7,
    254, 93, 181, 14, 247, 221, 237, 7,
    126, 219, 241, 14, 231, 238, 236, 7,
    30, 85, 245, 14, 215, 221, 237, 7,
    62, 85, 245, 14, 231, 221, 237, 7,
    158, 221, 149, 14, 247, 207, 255, 7,
    190, 85, 181, 14, 231, 221, 237, 7,
    190, 85, 53, 14, 247, 221, 237, 7,
    30, 31, 29, 14, 199, 207, 205, 7,
    254, 221, 209, 14, 231, 221, 254, 7,
    62, 93, 61, 14, 231, 221, 222, 7,
    254, 113, 219, 14, 247, 252, 222, 7,
};

// TitleBytes - upstream's own real "NEURAS" title-screen logo bitmap
// (Status.cpp's `Title()`), 6 letters x 4x4 VVram-cell glyph indices each
// (96 values total), byte-diff-verified against the real upstream source
// via a small Python script. Every value here is a valid index into
// neuCharPattern[]'s own "logo" range (indices 0-15, the first 32 bytes
// of that table - confirmed byte-identical to Cracky's own copy of the
// same shared block) - the exact same shared block-pattern palette every
// other map tile in this game already draws through, just reused here to
// build a big pixel-art wordmark instead of a wall/floor tile. See
// neuBeginTitle()'s own comment for why this replaces the earlier plain-
// text "NEURAS" substitute (the same fix already applied to gameCracky.c's
// own "CRACKY" logo - see that file's own header comment for the full
// story of why the original "purely decorative" simplification was wrong).
int neuTitleBytes[96] = {
    0x0c, 0x0b, 0x00, 0x0f, 0x0c, 0x0f, 0x0b, 0x0f,
    0x0c, 0x03, 0x0d, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x07, 0x05, 0x01, 0x0c, 0x0b, 0x0a, 0x02,
    0x0c, 0x03, 0x00, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x0c, 0x03, 0x00, 0x0f, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x03, 0x00, 0x0f, 0x00, 0x05, 0x05, 0x01,
    0x0c, 0x07, 0x05, 0x0b, 0x0c, 0x03, 0x08, 0x0f,
    0x0c, 0x07, 0x0f, 0x02, 0x04, 0x01, 0x04, 0x05,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x08, 0x07, 0x05, 0x0b, 0x04, 0x0b, 0x0a, 0x02,
    0x08, 0x02, 0x00, 0x0f, 0x00, 0x05, 0x05, 0x01,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int neuFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int neuMelodyLoose[3] = { 1, 18, 0 };
int neuMelodyBeep[3] = { 1, 30, 0 };
int neuMelodyGet[3] = { 1, 30, 0 };
int neuMelodyHit[17] = { 1, 26, 1, 28, 1, 30, 1, 32, 1, 33, 1, 35, 1, 37, 1, 38, 0 };
int neuMelodyMiss[17] = { 1, 38, 1, 37, 1, 35, 1, 33, 1, 32, 1, 30, 1, 28, 1, 26, 0 };
int neuMelodyStart[11] = { 6, 21, 6, 28, 6, 25, 6, 28, 24, 33, 0 };
int neuMelodyClear[21] = { 6, 21, 6, 25, 6, 28, 6, 23, 6, 26, 6, 30, 6, 25, 6, 28, 6, 32, 18, 33, 0 };
int neuMelodyGameOver[23] = { 6, 33, 6, 28, 6, 25, 6, 33, 6, 32, 6, 28, 6, 25, 6, 32, 12, 30, 12, 32, 24, 33, 0 };

int neuMelodyBgm1[89] = {
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

int neuMelodyBgm2[41] = {
    18, 0, 18, 14, 18, 14, 12, 14, 18, 21,
    18, 21, 12, 21, 24, 14, 24, 16, 30, 18,
    18, 0, 18, 18, 18, 18, 12, 18, 18, 16,
    18, 16, 12, 16, 24, 14, 24, 21, 30, 14,
    255,
};

// Stage data - flattened from upstream's own `struct Stage` array + separate
// per-stage Cards0-7/Monsters0-7 arrays into parallel fixed arrays (matches
// this project's own "flatten to plain arrays" precedent, e.g. Cracky's own
// crkStageEnemies). neuStageBytes keeps the full 18-byte struct field size
// upstream declares (only the first ~10 bytes of which InitStage()'s own
// bit-consuming algorithm ever actually reads) rather than trying to prove by
// hand exactly how few bytes are truly needed - the extra zero-padding is
// free and removes any risk from a subtly-wrong manual bit-count.
int neuStageBytes[8][18] = {
    { 128, 122, 128, 118, 128, 110, 128, 94, 128, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 217, 70, 128, 190, 144, 198, 156, 69, 128, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 193, 60, 128, 182, 162, 157, 224, 20, 147, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 128, 109, 146, 169, 128, 78, 176, 85, 140, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 144, 70, 137, 216, 131, 120, 136, 166, 128, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 130, 85, 176, 158, 128, 219, 132, 36, 233, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 128, 86, 187, 68, 196, 76, 145, 84, 193, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 228, 18, 139, 120, 144, 213, 128, 198, 152, 255, 0, 0, 0, 0, 0, 0, 0, 0 },
};

// Packed ( column << 4 ) | row - every stage happens to start at (0,0), kept
// as a table (not hardcoded) to match upstream's own general per-stage field.
int neuStageStartPacked[8] = {
    ( 0 << 4 ) | 0, ( 0 << 4 ) | 0, ( 0 << 4 ) | 0, ( 0 << 4 ) | 0,
    ( 0 << 4 ) | 0, ( 0 << 4 ) | 0, ( 0 << 4 ) | 0, ( 0 << 4 ) | 0,
};

int neuStageCardCount[8] = { 4, 6, 8, 10, 4, 10, 12, 14 };
int neuStageMonsterCount[8] = { 1, 1, 1, 1, 4, 1, 1, 2 };

int neuStageCardNumber[8][14] = {
    { 1, 1, 13, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 12, 12, 13, 13, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 13, 1, 1, 11, 12, 12, 11, 13, 0, 0, 0, 0, 0, 0 },
    { 1, 13, 12, 1, 11, 7, 7, 12, 11, 13, 0, 0, 0, 0 },
    { 1, 13, 13, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 12, 11, 12, 1, 2, 13, 1, 2, 13, 11, 0, 0, 0, 0 },
    { 2, 13, 1, 12, 11, 7, 1, 7, 2, 12, 13, 11, 0, 0 },
    { 2, 12, 11, 13, 1, 3, 1, 9, 2, 9, 11, 3, 12, 13 },
};

int neuStageCardPos[8][14] = {
    { ( 0 << 4 ) | 2, ( 7 << 4 ) | 2, ( 0 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { ( 3 << 4 ) | 1, ( 0 << 4 ) | 2, ( 7 << 4 ) | 2, ( 0 << 4 ) | 4, ( 6 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0, 0, 0, 0, 0, 0, 0 },
    { ( 1 << 4 ) | 0, ( 3 << 4 ) | 1, ( 7 << 4 ) | 1, ( 2 << 4 ) | 2, ( 7 << 4 ) | 2, ( 0 << 4 ) | 4, ( 4 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0, 0, 0, 0, 0 },
    { ( 7 << 4 ) | 0, ( 0 << 4 ) | 1, ( 1 << 4 ) | 1, ( 2 << 4 ) | 1, ( 4 << 4 ) | 1, ( 3 << 4 ) | 2, ( 0 << 4 ) | 3, ( 2 << 4 ) | 4, ( 3 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0, 0, 0 },
    { ( 2 << 4 ) | 0, ( 2 << 4 ) | 1, ( 0 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { ( 2 << 4 ) | 0, ( 4 << 4 ) | 0, ( 7 << 4 ) | 0, ( 0 << 4 ) | 1, ( 1 << 4 ) | 1, ( 6 << 4 ) | 1, ( 7 << 4 ) | 2, ( 6 << 4 ) | 3, ( 0 << 4 ) | 4, ( 5 << 4 ) | 4, 0, 0, 0, 0 },
    { ( 2 << 4 ) | 0, ( 3 << 4 ) | 0, ( 7 << 4 ) | 0, ( 1 << 4 ) | 1, ( 4 << 4 ) | 1, ( 5 << 4 ) | 1, ( 2 << 4 ) | 2, ( 2 << 4 ) | 3, ( 6 << 4 ) | 3, ( 2 << 4 ) | 4, ( 6 << 4 ) | 4, ( 7 << 4 ) | 4, 0, 0 },
    { ( 1 << 4 ) | 0, ( 2 << 4 ) | 0, ( 4 << 4 ) | 0, ( 6 << 4 ) | 0, ( 1 << 4 ) | 1, ( 2 << 4 ) | 1, ( 1 << 4 ) | 2, ( 2 << 4 ) | 3, ( 6 << 4 ) | 3, ( 7 << 4 ) | 3, ( 0 << 4 ) | 4, ( 2 << 4 ) | 4, ( 5 << 4 ) | 4, ( 7 << 4 ) | 4 },
};

int neuStageMonsterPos[8][4] = {
    { ( 7 << 4 ) | 0, 0, 0, 0 },
    { ( 7 << 4 ) | 0, 0, 0, 0 },
    { ( 7 << 4 ) | 3, 0, 0, 0 },
    { ( 6 << 4 ) | 3, 0, 0, 0 },
    { ( 7 << 4 ) | 0, ( 3 << 4 ) | 1, ( 7 << 4 ) | 2, ( 5 << 4 ) | 4 },
    { ( 7 << 4 ) | 4, 0, 0, 0 },
    { ( 6 << 4 ) | 1, 0, 0, 0 },
    { ( 7 << 4 ) | 0, ( 6 << 4 ) | 4, 0, 0 },
};

// Shared direction tables (Left,Right,Up,Down - matches upstream's own
// Directions[] ordering in both Solver.cpp and Monster.cpp).
int neuDirDx[4] = { -1, 1, 0, 0 };
int neuDirDy[4] = { 0, 0, -1, 1 };
int neuSolverDirPattern[4] = { NEU_CHAR_SOLVER_LEFT, NEU_CHAR_SOLVER_RIGHT, NEU_CHAR_SOLVER_UP, NEU_CHAR_SOLVER_DOWN };
int neuMonsterDirPattern[4] = { NEU_CHAR_MONSTER_LEFT, NEU_CHAR_MONSTER_RIGHT, NEU_CHAR_MONSTER_UP, NEU_CHAR_MONSTER_DOWN };

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

typedef struct
{
    int status;
    int x, y;
} NeuCard;
NeuCard neuCards[NEU_CARD_COUNT];
int neuHeldCardIndex;
int neuThrownCardIndex;
int neuCardX, neuCardY;
int neuCardDx, neuCardDy;
int neuRemainCardCount;

typedef struct
{
    int status;
    int x, y;
    int dirIndex;
    int stopCount;
} NeuMonster;
NeuMonster neuMonsters[NEU_MONSTER_COUNT];
int neuMonsterCount;
int neuSpeedValue;

typedef struct
{
    int x, y;
    int pattern;
} NeuSprite;
NeuSprite neuSprites[NEU_SPRITE_COUNT];

int neuVVram[NEU_VVRAM_HEIGHT][NEU_VVRAM_WIDTH];
int neuWallMap[NEU_WALLMAP_SIZE];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into neuAsciiPattern (0 = space) per cell.
//
// **This width was widened from an original, wrong `[8][8]` after Cracky's
// own sibling bug (see gameCracky.c's own `crkStatusChar` header comment
// for the full story, proven via a real user-supplied hardware photo) was
// traced through and confirmed to apply identically here.** The original
// design only ever modeled upstream's own status-label columns (SCORE/
// STAGE/TIME/lives, which upstream's `LeftX=24` constant genuinely does
// confine to columns 24-31, 8 cells) - but the title screen's own text
// ("MINI", "START"/"CONTINUE", the "INUFUTO 2026" credit) lives at
// upstream's real columns 8-23, well to the LEFT of the status zone, using
// the exact same shared PrintC()/PrintS() mechanism at different column
// arguments - not a separate, narrower grid at all. Cramming all of that
// title-screen text into the same 8-cell-wide status grid (reusing columns
// 24-31 that the status labels ALSO use) is what caused this port's own
// first verification-pass bug (see the header comment's own "1." writeup
// below, now superseded by this wider fix): "CONTINUE" needing truncation
// to fit, title text colliding with SCORE/STAGE/TIME, and "MINI"/the
// credit line being dropped outright since there was genuinely no room
// left for them in the cramped model. See `neuComposeRawByte()`'s own
// comment for how this wider grid actually reaches the screen.
int neuStatusChar[8][32];

// Set true only while on the title screen (NEU_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its initial
// ClearScreen(), and instead drives the ENTIRE screen (not just the status
// zone) through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range ("NEURAS", "MINI", "START"/
// "CONTINUE", the credit line all live at columns 0-23, well inside what
// during gameplay is the map area). When true, neuComposeRawByte() reads
// neuStatusChar across the full width instead of just columns 24-31,
// letting the title screen use that same wide real estate instead of being
// artificially confined to the narrow status-only zone - matching Cracky's
// own `crkFullWidthText` exactly.
bool neuFullWidthText;

int neuHeldCardGlyph;
int neuRemainIconCount;

bool neuOverlayActive;
int neuOverlayText[10];
int neuOverlayLen;
int neuOverlayPage;
int neuOverlayCol;

int neuSolverX, neuSolverY;
int neuSolverStatus;
int neuSolverDirIndex;
bool neuSolverButtonOn;

int neuScore;
int neuCurrentStage;
int neuRemainCount;
int neuStageTime;
int neuTimeCount;
int neuStageIndex;

int neuRevealCardIndexA;
int neuRevealCardIndexB;
int neuMatchCardIndex;
int neuMatchOtherIndex;
int neuMatchScanIndex;
bool neuMatchIsHit;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame, matching Cracky's own shape.
int neuSeqMelody[3];
int neuSeqPos[3];
int neuSeqWait[3];
int neuSeqActive[3];

#define NEU_TICK_DIVISOR 1
int neuTickCounter;

#define NEU_STATE_TITLE 0
#define NEU_STATE_START_JINGLE 1
#define NEU_STATE_PLAYING 2
#define NEU_STATE_MATCH_REVEAL 3
#define NEU_STATE_DIE_ANIM 4
#define NEU_STATE_TIMEUP_SWEEP 5
#define NEU_STATE_GAMEOVER_JINGLE 6
#define NEU_STATE_CLEAR_JINGLE 7
#define NEU_STATE_BONUS_TALLY 8
int neuState;
int neuWaitFrames;
int neuAnimStep;
int neuTimeUpSweepCount;
int neuSelection;
bool neuSelectionChanged;
int neuPrevLeft, neuPrevRight, neuPrevUp, neuPrevDown, neuPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int neuByteWrap( int v )
{
    return v & 0xFF;
}

int neuAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int neuFromPositionPixel( int a )
{
    return ( ( a + a + a + 1 ) << 3 ) & 0xFF;
}

int neuFromPositionCell( int a )
{
    return a + a + a + 1;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - TestMap2 (pixel-scale wall collision test)
// -----------------------------------------------------------------------------

bool neuTestMap2( int x, int y )
{
    int left, top, width, height;
    int mapIdx;
    int mask, maskLow, maskHigh;
    int c;

    x = neuByteWrap( x );
    y = neuByteWrap( y );

    left = x >> 3;
    width = ( ( x + 23 ) >> 3 ) - left;
    top = y >> 3;
    height = ( ( y + 23 ) >> 3 ) - top;

    mapIdx = ( top << 2 ) + ( left >> 3 );
    if( width > 2 )
      mask = 7;
    else
      mask = 3;
    mask = mask << ( left & 7 );
    maskLow = mask & 0xFF;
    maskHigh = ( mask >> 8 ) & 0xFF;

    c = height;
    while( c != 0 )
    {
        if( mapIdx < 0 || mapIdx + 1 >= NEU_WALLMAP_SIZE )
          return false;
        if( ( neuWallMap[ mapIdx ] & maskLow ) != 0 || ( neuWallMap[ mapIdx + 1 ] & maskHigh ) != 0 )
          return false;
        mapIdx = mapIdx + 4;
        c = c - 1;
    }
    return true;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites directly into neuVVram (VVram-cell coords are
//   already what ShowSprite() converts pixel coords into, no extra scaling).
// -----------------------------------------------------------------------------

void neuShowSprite( int index, int x, int y, int pattern )
{
    x = neuByteWrap( x );
    y = neuByteWrap( y );
    neuSprites[ index ].x = ( ( x + 4 ) >> 3 ) - 1;
    neuSprites[ index ].y = ( y + 4 ) >> 3;
    neuSprites[ index ].pattern = pattern;
}

void neuHideSprite( int index )
{
    neuSprites[ index ].pattern = NEU_INVALID_CODE;
}

void neuHideAllSprites()
{
    int i;
    for( i = 0; i < NEU_SPRITE_COUNT; i = i + 1 )
      neuSprites[ i ].pattern = NEU_INVALID_CODE;
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp
// -----------------------------------------------------------------------------

int neuAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct port of upstream's
    // own linear search (only 27 entries, no cost concern doing this live).
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

int neuPrintC( int page, int col, int c )
{
    neuStatusChar[ page ][ col ] = neuAsciiIndex( c );
    return col + 1;
}

int neuPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = neuPrintC( page, col, s[ i ] );
    return col;
}

int neuPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
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
    return neuPrintC( page, col, c );
}

void neuPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      neuPrintC( page, col, ' ' );
    else
      neuPrintC( page, col, d1 + '0' );
    neuPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void neuPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        neuPrintC( page, col, ' ' );
        if( d2 == 0 )
          neuPrintC( page, col + 1, ' ' );
        else
          neuPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        neuPrintC( page, col, d1 + '0' );
        neuPrintC( page, col + 1, d2 + '0' );
    }
    neuPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void neuPrintNumber5( int page, int col, int w )
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
          neuPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            neuPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    neuPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// neuStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void neuPrintScore()
{
    neuPrintNumber5( 1, 26, neuScore );
    neuPrintC( 1, 31, '0' );
}

void neuPrintTime()
{
    neuPrintByteNumber3( 5, 29, neuStageTime );
}

void neuPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };

    neuPrintS( 0, 24, sScore, 5 );
    neuPrintS( 3, 24, sStage, 5 );
    neuPrintByteNumber2( 3, 30, neuCurrentStage + 1 );
    neuPrintS( 5, 24, sTime, 4 );

    // upstream's own "i>2" fallback digit display (RemainCount would need
    // to exceed 3, which never happens since it only ever starts at 3 and
    // counts down) is unreachable given this game's real RemainCount range
    // - confirmed dead, not ported, matching this project's own precedent
    // for skipping a genuinely unreachable branch once actually confirmed
    // (rather than guessed) dead.
    if( neuRemainCount > 1 )
      neuRemainIconCount = neuRemainCount - 1;
    else
      neuRemainIconCount = 0;

    neuPrintScore();
    neuPrintTime();
}

void neuPrintHeldCard()
{
    if( neuHeldCardIndex >= 0 )
      neuHeldCardGlyph = NEU_CHAR_CARD + ( ( neuCards[ neuHeldCardIndex ].status & 0x0f ) << 2 );
    else
      neuHeldCardGlyph = -1;
}

void neuBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    neuOverlayActive = true;
    neuOverlayLen = len;
    neuOverlayPage = page;
    neuOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      neuOverlayText[ i ] = s[ i ];
}

void neuPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    neuBeginOverlay( s, 9, 4, 8 );
}

void neuPrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    neuBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int neuMelodyLength( int id )
{
    if( id == NEU_MELODY_LOOSE ) return 3;
    if( id == NEU_MELODY_BEEP ) return 3;
    if( id == NEU_MELODY_GET ) return 3;
    if( id == NEU_MELODY_HIT ) return 17;
    if( id == NEU_MELODY_MISS ) return 17;
    if( id == NEU_MELODY_START ) return 11;
    if( id == NEU_MELODY_CLEAR ) return 21;
    if( id == NEU_MELODY_GAMEOVER ) return 23;
    if( id == NEU_MELODY_BGM1 ) return 89;
    if( id == NEU_MELODY_BGM2 ) return 41;
    return 0;
}

int neuMelodyValue( int id, int idx )
{
    if( id == NEU_MELODY_LOOSE ) return neuMelodyLoose[ idx ];
    if( id == NEU_MELODY_BEEP ) return neuMelodyBeep[ idx ];
    if( id == NEU_MELODY_GET ) return neuMelodyGet[ idx ];
    if( id == NEU_MELODY_HIT ) return neuMelodyHit[ idx ];
    if( id == NEU_MELODY_MISS ) return neuMelodyMiss[ idx ];
    if( id == NEU_MELODY_START ) return neuMelodyStart[ idx ];
    if( id == NEU_MELODY_CLEAR ) return neuMelodyClear[ idx ];
    if( id == NEU_MELODY_GAMEOVER ) return neuMelodyGameOver[ idx ];
    if( id == NEU_MELODY_BGM1 ) return neuMelodyBgm1[ idx ];
    if( id == NEU_MELODY_BGM2 ) return neuMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/NEU_TEMPO(220) = 1.3636... real 60Hz ticks - see header comment.
int neuNoteFrames( int length )
{
    return (int)( length * 1.363636 + 0.5 );
}

// Sums neuNoteFrames() across every note of a melody - the real total real-
// frame duration of playing it start to finish. Needed because upstream's
// own Sound_Hit()/Sound_Miss() (via WaitMelody()) genuinely BLOCK the whole
// CPU until the melody finishes, before a separate, later WaitTimer() call
// even starts counting - see neuContinueMatchScan()'s own use of this,
// and the header comment's own writeup of this bug.
int neuMelodyTotalFrames( int id )
{
    int total, len, idx, length;
    total = 0;
    len = neuMelodyLength( id );
    idx = 0;
    while( idx < len )
    {
        length = neuMelodyValue( id, idx );
        if( length == 0 || length == 255 ) return total;
        total = total + neuNoteFrames( length );
        idx = idx + 2;
    }
    return total;
}

void neuStartSeq( int channel, int melodyId )
{
    neuSeqMelody[ channel ] = melodyId;
    neuSeqPos[ channel ] = 0;
    neuSeqWait[ channel ] = 0;
    neuSeqActive[ channel ] = 1;
}

void neuStopSeq( int channel )
{
    neuSeqActive[ channel ] = 0;
    neuSeqMelody[ channel ] = NEU_MELODY_NONE;
}

bool neuSeqPlaying( int channel )
{
    return neuSeqActive[ channel ] != 0;
}

void neuAdvanceOneSeq( int channel )
{
    int length, note;

    if( neuSeqActive[ channel ] == 0 ) return;

    if( neuSeqWait[ channel ] > 0 )
    {
        neuSeqWait[ channel ] = neuSeqWait[ channel ] - 1;
        return;
    }

    length = neuMelodyValue( neuSeqMelody[ channel ], neuSeqPos[ channel ] );
    if( length == 0 )
    {
        neuStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        neuSeqPos[ channel ] = 0;
        length = neuMelodyValue( neuSeqMelody[ channel ], 0 );
    }
    note = neuMelodyValue( neuSeqMelody[ channel ], neuSeqPos[ channel ] + 1 );
    neuSeqPos[ channel ] = neuSeqPos[ channel ] + 2;
    neuSeqWait[ channel ] = neuNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)neuFrequencies[ note - 1 ], (float)neuSeqWait[ channel ] / 60.0 );
}

void neuAdvanceSound()
{
    neuAdvanceOneSeq( 0 );
    neuAdvanceOneSeq( 1 );
    neuAdvanceOneSeq( 2 );
}

void neuStartBgm()
{
    neuStartSeq( 1, NEU_MELODY_BGM1 );
    neuStartSeq( 2, NEU_MELODY_BGM2 );
}

void neuStopBgm()
{
    neuStopSeq( 1 );
    neuStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void neuAddScore( int pts )
{
    neuScore = neuScore + pts;
    neuPrintScore();
}


// -----------------------------------------------------------------------------
//   VVram.cpp - card floor-tile / sprite compositing
// -----------------------------------------------------------------------------

void neuVPut2( int x, int y, int c )
{
    neuVVram[ y ][ x - 1 ] = c;
    neuVVram[ y ][ x ] = c + 1;
    neuVVram[ y + 1 ][ x - 1 ] = c + 2;
    neuVVram[ y + 1 ][ x ] = c + 3;
}

void neuDrawCardsIntoVVram()
{
    int i, c, number;
    for( i = 0; i < NEU_CARD_COUNT; i = i + 1 )
    {
        if( ( neuCards[ i ].status & 0xf0 ) == NEU_CARD_STATUS_FLOOR )
        {
            number = neuCards[ i ].status & 0x0f;
            if( i == neuRevealCardIndexA || i == neuRevealCardIndexB )
              c = NEU_CHAR_CARD + ( number << 2 );
            else
              c = NEU_CHAR_CARD;
            neuVPut2( neuCards[ i ].x, neuCards[ i ].y, c );
        }
    }
}

void neuDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < NEU_SPRITE_COUNT; i = i + 1 )
    {
        if( neuSprites[ i ].pattern != NEU_INVALID_CODE )
        {
            x = neuSprites[ i ].x;
            y = neuSprites[ i ].y;
            c = neuSprites[ i ].pattern;
            neuVVram[ y ][ x ] = c; c = c + 1;
            neuVVram[ y ][ x + 1 ] = c; c = c + 1;
            neuVVram[ y + 1 ][ x ] = c; c = c + 1;
            neuVVram[ y + 1 ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Card.cpp
// -----------------------------------------------------------------------------

void neuInitCards()
{
    int i, count;
    count = neuStageCardCount[ neuStageIndex ];
    neuRemainCardCount = count;
    for( i = 0; i < count && i < NEU_CARD_COUNT; i = i + 1 )
    {
        neuCards[ i ].status = NEU_CARD_STATUS_FLOOR | neuStageCardNumber[ neuStageIndex ][ i ];
        neuCards[ i ].x = neuFromPositionCell( neuStageCardPos[ neuStageIndex ][ i ] >> 4 );
        neuCards[ i ].y = neuFromPositionCell( neuStageCardPos[ neuStageIndex ][ i ] & 0x0f );
    }
    for( ; i < NEU_CARD_COUNT; i = i + 1 )
      neuCards[ i ].status = NEU_CARD_STATUS_NONE;

    neuHeldCardIndex = -1;
    neuThrownCardIndex = -1;
    neuPrintHeldCard();
}

// Deliberate, documented resolution of an ambiguous upstream edge case
// (dying while holding/having just thrown a card) - see this file's own
// header comment for the full reasoning.
void neuResetHeldOrThrownCardOnRetry()
{
    int number;
    if( neuHeldCardIndex >= 0 )
    {
        number = neuCards[ neuHeldCardIndex ].status & 0x0f;
        neuCards[ neuHeldCardIndex ].status = NEU_CARD_STATUS_FLOOR | number;
        neuHeldCardIndex = -1;
    }
    if( neuThrownCardIndex >= 0 )
    {
        number = neuCards[ neuThrownCardIndex ].status & 0x0f;
        neuCards[ neuThrownCardIndex ].x = ( neuCardX + 4 ) >> 3;
        neuCards[ neuThrownCardIndex ].y = ( neuCardY + 4 ) >> 3;
        neuCards[ neuThrownCardIndex ].status = NEU_CARD_STATUS_FLOOR | number;
        neuThrownCardIndex = -1;
    }
    neuHideSprite( NEU_SPRITE_CARD );
    neuPrintHeldCard();
}

bool neuMonsterTestHit( int x, int y );  // used by neuMoveCard below; real
                                          // definition follows in the
                                          // Monster.cpp section - kept as a
                                          // one-line prototype here rather
                                          // than reordering the whole Card/
                                          // Monster section, matching this
                                          // dialect's documented support for
                                          // forward declarations "allowed
                                          // for ordering" (see this project's
                                          // own CLAUDE.md notes on Tiny
                                          // Pipe's identical use of this).

void neuPickCard()
{
    int x, y, i;
    if( neuHeldCardIndex < 0 )
    {
        x = ( neuSolverX + 4 ) >> 3;
        y = ( neuSolverY + 4 ) >> 3;
        for( i = 0; i < NEU_CARD_COUNT; i = i + 1 )
        {
            if( ( neuCards[ i ].status & 0xf0 ) == NEU_CARD_STATUS_FLOOR &&
                neuCards[ i ].x == x && neuCards[ i ].y == y )
            {
                neuHeldCardIndex = i;
                neuStartSeq( 0, NEU_MELODY_GET );
                neuCards[ i ].status = ( neuCards[ i ].status & 0x0f ) | NEU_CARD_STATUS_HELD;
                neuPrintHeldCard();
                return;
            }
        }
    }
}

bool neuTestToMove( int x, int y )
{
    int gridX, gridY, i;

    x = neuByteWrap( x );
    y = neuByteWrap( y );

    if( neuTestMap2( x, y ) )
    {
        gridX = x >> 3;
        gridY = y >> 3;
        for( i = 0; i < NEU_CARD_COUNT; i = i + 1 )
        {
            if( ( neuCards[ i ].status & 0xf0 ) == NEU_CARD_STATUS_FLOOR )
            {
                int xDiff, yDiff;
                xDiff = neuByteWrap( gridX - neuCards[ i ].x );
                yDiff = neuByteWrap( gridY - neuCards[ i ].y );
                if( neuByteWrap( xDiff + 1 ) < 3 && neuByteWrap( yDiff + 1 ) < 3 )
                  return false;
            }
        }
        return true;
    }
    return false;
}

void neuThrowCard( int x, int y, int dx, int dy )
{
    x = neuByteWrap( x );
    y = neuByteWrap( y );
    x = ( x + 3 ) & 0xf8;
    y = ( y + 3 ) & 0xf8;
    if( neuHeldCardIndex >= 0 && neuThrownCardIndex < 0 && neuTestToMove( x, y ) )
    {
        neuCardX = x;
        neuCardY = y;
        neuCardDx = dx << 3;
        neuCardDy = dy << 3;
        neuThrownCardIndex = neuHeldCardIndex;
        neuHeldCardIndex = -1;
        neuStartSeq( 0, NEU_MELODY_GET );
        neuShowSprite( NEU_SPRITE_CARD, neuCardX, neuCardY, NEU_CHAR_CARD );
        neuPrintHeldCard();
    }
}

void neuContinueMatchScan()
{
    int i;
    int px, py, pNumber;

    px = neuCards[ neuMatchCardIndex ].x;
    py = neuCards[ neuMatchCardIndex ].y;
    pNumber = neuCards[ neuMatchCardIndex ].status & 0x0f;

    for( i = neuMatchScanIndex; i < NEU_CARD_COUNT; i = i + 1 )
    {
        if( i != neuMatchCardIndex && ( neuCards[ i ].status & 0xf0 ) == NEU_CARD_STATUS_FLOOR )
        {
            bool xAligned, yAligned;
            int otherNumber;
            otherNumber = neuCards[ i ].status & 0x0f;
            xAligned = ( px == neuCards[ i ].x ) && ( neuByteWrap( py - neuCards[ i ].y + 2 ) < 5 );
            yAligned = ( py == neuCards[ i ].y ) && ( neuByteWrap( px - neuCards[ i ].x + 2 ) < 5 );
            if( xAligned || yAligned )
            {
                neuRevealCardIndexA = neuMatchCardIndex;
                neuRevealCardIndexB = i;
                neuMatchOtherIndex = i;
                neuMatchScanIndex = i + 1;
                neuMatchIsHit = ( pNumber == otherNumber );
                // A real timing-fidelity bug found via careful derivation
                // against the real Sound.cpp/Timer.cpp source (see header
                // comment): upstream's own Sound_Hit()/Sound_Miss() calls
                // are genuinely BLOCKING (WaitMelody() busy-waits for the
                // whole 8-note cue to finish) and happen BEFORE the
                // separate, literal WaitTimer(30) - i.e. upstream's real
                // reveal hold is (melody duration) + 30 ticks, not just 30.
                // This project's own multi-voice md_playTone() lets the
                // port's sound and its own neuWaitFrames run concurrently
                // instead, which would under-count the real pause by the
                // melody's own ~11-tick duration if not added explicitly -
                // matching the same fix shape neuUpdateBonusTally() already
                // uses for its own Sound_Beep()+WaitTimer(1) stack.
                if( neuMatchIsHit )
                {
                    neuStartSeq( 0, NEU_MELODY_HIT );
                    neuWaitFrames = 30 + neuMelodyTotalFrames( NEU_MELODY_HIT );
                }
                else
                {
                    neuStartSeq( 0, NEU_MELODY_MISS );
                    neuWaitFrames = 30 + neuMelodyTotalFrames( NEU_MELODY_MISS );
                }
                neuState = NEU_STATE_MATCH_REVEAL;
                return;
            }
        }
    }
    neuState = NEU_STATE_PLAYING;
}

void neuBeginMatchScan( int cardIndex )
{
    neuMatchCardIndex = cardIndex;
    neuMatchScanIndex = 0;
    neuContinueMatchScan();
}

void neuMoveCard()
{
    if( neuThrownCardIndex >= 0 )
    {
        int newX, newY;
        newX = neuByteWrap( neuCardX + neuCardDx );
        newY = neuByteWrap( neuCardY + neuCardDy );
        if( neuTestToMove( newX, newY ) && !neuMonsterTestHit( newX, newY ) )
        {
            neuCardX = newX;
            neuCardY = newY;
            neuShowSprite( NEU_SPRITE_CARD, newX, newY, NEU_CHAR_CARD );
        }
        else
        {
            int idx, number;
            idx = neuThrownCardIndex;
            neuThrownCardIndex = -1;
            number = neuCards[ idx ].status & 0x0f;
            neuCards[ idx ].status = NEU_CARD_STATUS_FLOOR | number;
            neuCards[ idx ].x = ( neuCardX + 4 ) >> 3;
            neuCards[ idx ].y = ( neuCardY + 4 ) >> 3;
            neuHideSprite( NEU_SPRITE_CARD );
            neuBeginMatchScan( idx );
        }
    }
}


// -----------------------------------------------------------------------------
//   Solver.cpp
// -----------------------------------------------------------------------------

void neuInitSolver( int x, int y )
{
    neuSolverX = neuByteWrap( x );
    neuSolverY = neuByteWrap( y );
    neuSolverStatus = NEU_SOLVER_LIVE;
    neuSolverDirIndex = NEU_DIR_LEFT;
    neuSolverButtonOn = false;
    neuShowSprite( NEU_SPRITE_SOLVER, neuSolverX, neuSolverY, neuSolverDirPattern[ NEU_DIR_LEFT ] );
}

bool neuSolverMove( int dirIndex )
{
    int newX, newY, seq;
    newX = neuByteWrap( neuSolverX + neuDirDx[ dirIndex ] );
    newY = neuByteWrap( neuSolverY + neuDirDy[ dirIndex ] );
    if( neuTestMap2( newX, newY ) )
    {
        neuSolverDirIndex = dirIndex;
        neuSolverX = newX;
        neuSolverY = newY;
        seq = ( ( ( neuSolverX + neuSolverY + 4 ) >> 3 ) & 1 ) << 2;
        neuShowSprite( NEU_SPRITE_SOLVER, neuSolverX, neuSolverY, neuSolverDirPattern[ dirIndex ] + seq );
        return true;
    }
    return false;
}

void neuMoveSolver()
{
    bool keyOn, moved;
    int dx, dy;

    if( neuSolverStatus != NEU_SOLVER_LIVE ) return;

    moved = false;
    if( ( neuSolverX & 3 ) == 0 && ( neuSolverY & 3 ) == 0 )
    {
        keyOn = false;
        if( isLeftPressed() ) { keyOn = true; if( neuSolverMove( NEU_DIR_LEFT ) ) moved = true; }
        if( !moved && isRightPressed() ) { keyOn = true; if( neuSolverMove( NEU_DIR_RIGHT ) ) moved = true; }
        if( !moved && isUpPressed() ) { keyOn = true; if( neuSolverMove( NEU_DIR_UP ) ) moved = true; }
        if( !moved && isDownPressed() ) { keyOn = true; if( neuSolverMove( NEU_DIR_DOWN ) ) moved = true; }
    }
    else
      keyOn = true;

    if( !moved && keyOn )
      neuSolverMove( neuSolverDirIndex );

    neuPickCard();

    dx = neuDirDx[ neuSolverDirIndex ];
    dy = neuDirDy[ neuSolverDirIndex ];
    if( isFirePressed() )
    {
        if( !neuSolverButtonOn )
        {
            neuSolverButtonOn = true;
            neuThrowCard( neuSolverX + ( dx << 4 ), neuSolverY + ( dy << 4 ), dx, dy );
        }
    }
    else
      neuSolverButtonOn = false;
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void neuDecideMonsterDirection( int idx )
{
    int directions[4];
    int verticalIdx, horizontalIdx;
    int i, direction, dx, dy;
    int curDx, curDy;
    int mx, my;

    mx = neuMonsters[ idx ].x;
    my = neuMonsters[ idx ].y;
    verticalIdx = 0;
    horizontalIdx = 0;
    if( neuMonsters[ idx ].dirIndex < 0 )
    {
        curDx = 0;
        curDy = 0;
    }
    else
    {
        curDx = neuDirDx[ neuMonsters[ idx ].dirIndex ];
        curDy = neuDirDy[ neuMonsters[ idx ].dirIndex ];
    }

    if( neuAbs( neuSolverX, mx ) > neuAbs( neuSolverY, my ) )
    {
        if( neuSolverX < mx )
        {
            if( curDx <= 0 )
            {
                directions[ 0 ] = NEU_DIR_LEFT;
                directions[ 3 ] = NEU_DIR_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = NEU_DIR_RIGHT;
                directions[ 3 ] = NEU_DIR_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( curDx >= 0 )
            {
                directions[ 0 ] = NEU_DIR_RIGHT;
                directions[ 3 ] = NEU_DIR_LEFT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = NEU_DIR_LEFT;
                directions[ 3 ] = NEU_DIR_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( neuSolverY < my && curDy <= 0 ) || curDy < 0 )
        {
            directions[ verticalIdx ] = NEU_DIR_UP;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = NEU_DIR_DOWN;
        }
        else
        {
            directions[ verticalIdx ] = NEU_DIR_DOWN;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = NEU_DIR_UP;
        }
    }
    else
    {
        if( neuSolverY < my )
        {
            if( curDy <= 0 )
            {
                directions[ 0 ] = NEU_DIR_UP;
                directions[ 3 ] = NEU_DIR_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = NEU_DIR_DOWN;
                directions[ 3 ] = NEU_DIR_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( curDy >= 0 )
            {
                directions[ 0 ] = NEU_DIR_DOWN;
                directions[ 3 ] = NEU_DIR_UP;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = NEU_DIR_UP;
                directions[ 3 ] = NEU_DIR_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares SolverX < pMonster->y here too (a real upstream
        // quirk, not a transcription slip - the exact same bug already
        // found, confirmed, and preserved for this identical engine's own
        // monster AI in Cracky's crkDecideDirection() - kept faithfully
        // here for the same reason).
        if( ( neuSolverX < my && curDx <= 0 ) || curDx < 0 )
        {
            directions[ horizontalIdx ] = NEU_DIR_LEFT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = NEU_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalIdx ] = NEU_DIR_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = NEU_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        dx = neuDirDx[ direction ];
        dy = neuDirDy[ direction ];
        if( neuTestMap2( mx + dx, my + dy ) )
        {
            neuMonsters[ idx ].dirIndex = direction;
            return;
        }
    }
    // upstream leaves pDirection unchanged if none work; this port already
    // initializes dirIndex to a safe default (NEU_DIR_LEFT, never -1) at
    // monster spawn specifically to avoid ever indexing neuDirDx/Dy with an
    // invalid value here - a defensive guard against a theoretical (never
    // actually reachable given the shipped stage data, every monster always
    // has at least one open adjacent cell) upstream edge case.
}

void neuInitMonsters()
{
    int i;
    neuMonsterCount = neuStageMonsterCount[ neuStageIndex ];
    for( i = 0; i < neuMonsterCount; i = i + 1 )
    {
        int packed;
        neuMonsters[ i ].status = NEU_MONSTER_NORMAL;
        packed = neuStageMonsterPos[ neuStageIndex ][ i ];
        neuMonsters[ i ].x = neuFromPositionPixel( packed >> 4 );
        neuMonsters[ i ].y = neuFromPositionPixel( packed & 0x0f );
        neuMonsters[ i ].dirIndex = NEU_DIR_LEFT;
        neuDecideMonsterDirection( i );
        neuShowSprite( NEU_SPRITE_MONSTER + i, neuMonsters[ i ].x, neuMonsters[ i ].y, NEU_CHAR_MONSTER_LEFT );
    }
    for( ; i < NEU_MONSTER_COUNT; i = i + 1 )
    {
        neuMonsters[ i ].status = NEU_MONSTER_NONE;
        neuHideSprite( NEU_SPRITE_MONSTER + i );
    }
    neuSpeedValue = 0;
}

void neuMoveMonsters()
{
    int i;
    int rotationPatterns[4] = { NEU_CHAR_MONSTER_LEFT, NEU_CHAR_MONSTER_DOWN, NEU_CHAR_MONSTER_RIGHT, NEU_CHAR_MONSTER_UP };

    neuSpeedValue = neuSpeedValue + NEU_SPEED_NUMERATOR;
    if( neuSpeedValue < 0 ) return;
    neuSpeedValue = neuSpeedValue - NEU_SPEED_DENOMINATOR;

    for( i = 0; i < NEU_MONSTER_COUNT; i = i + 1 )
    {
        if( neuMonsters[ i ].status == NEU_MONSTER_NORMAL )
        {
            int newX, newY, seq;

            if( neuMonsters[ i ].dirIndex < 0 || ( ( neuMonsters[ i ].x + neuMonsters[ i ].y ) & 7 ) == 0 )
              neuDecideMonsterDirection( i );

            newX = neuByteWrap( neuMonsters[ i ].x + neuDirDx[ neuMonsters[ i ].dirIndex ] );
            newY = neuByteWrap( neuMonsters[ i ].y + neuDirDy[ neuMonsters[ i ].dirIndex ] );
            if( neuTestMap2( newX, newY ) )
            {
                int xDiff, yDiff;
                neuMonsters[ i ].x = newX;
                neuMonsters[ i ].y = newY;
                seq = ( ( ( newX + newY + 4 ) >> 3 ) & 1 ) << 2;
                neuShowSprite( NEU_SPRITE_MONSTER + i, newX, newY, neuMonsterDirPattern[ neuMonsters[ i ].dirIndex ] + seq );

                xDiff = neuByteWrap( newX - neuSolverX + 12 );
                yDiff = neuByteWrap( newY - neuSolverY + 12 );
                if( xDiff < 24 && yDiff < 24 )
                  neuSolverStatus = NEU_SOLVER_DIE;
            }
        }
        else if( neuMonsters[ i ].status == NEU_MONSTER_STOP )
        {
            int pattern;
            pattern = rotationPatterns[ ( neuMonsters[ i ].stopCount >> 3 ) & 3 ];
            neuShowSprite( NEU_SPRITE_MONSTER + i, neuMonsters[ i ].x, neuMonsters[ i ].y, pattern );
            neuMonsters[ i ].stopCount = neuByteWrap( neuMonsters[ i ].stopCount - 1 );
            if( neuMonsters[ i ].stopCount == 0 )
            {
                neuMonsters[ i ].status = NEU_MONSTER_NORMAL;
                neuDecideMonsterDirection( i );
            }
        }
    }
}

bool neuMonsterTestHit( int x, int y )
{
    bool hit;
    int i;

    x = neuByteWrap( x );
    y = neuByteWrap( y );
    hit = false;
    for( i = 0; i < NEU_MONSTER_COUNT; i = i + 1 )
    {
        if( neuMonsters[ i ].status == NEU_MONSTER_NORMAL )
        {
            int xDiff, yDiff;
            xDiff = neuByteWrap( neuMonsters[ i ].x - x );
            yDiff = neuByteWrap( neuMonsters[ i ].y - y );
            if( neuByteWrap( xDiff + 8 ) < 16 && neuByteWrap( yDiff + 8 ) < 16 )
            {
                neuMonsters[ i ].status = NEU_MONSTER_STOP;
                neuMonsters[ i ].stopCount = 0;
                neuMonsters[ i ].x = ( neuMonsters[ i ].x + 4 ) & 0xf8;
                neuMonsters[ i ].y = ( neuMonsters[ i ].y + 4 ) & 0xf8;
                hit = true;
            }
        }
    }
    return hit;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage()'s own wall-bitmap-building algorithm, ported as
//   a direct structural mirror rather than re-derived into a closed form
//   (matching this project's own "faithfully copy an intricate stateful
//   algorithm's own shape" precedent, e.g. Frogger's row-buffer compositing)
//   - the exact semantics of each bit don't need to be understood by hand
//   as long as the bit-consumption order is reproduced exactly.
// -----------------------------------------------------------------------------

void neuBuildWallMap()
{
    int mapIdx, b, bit, wall, wallBit;
    int byteIdx, floorLoop, k, c;

    mapIdx = 0;
    for( ; mapIdx < NEU_MAP_WIDTH - 1; mapIdx = mapIdx + 1 )
      neuWallMap[ mapIdx ] = 0xff;
    neuWallMap[ mapIdx ] = 0x01;
    mapIdx = mapIdx + 1;

    byteIdx = 0;
    b = neuStageBytes[ neuStageIndex ][ byteIdx ]; byteIdx = byteIdx + 1;
    bit = 1;

    for( floorLoop = 0; floorLoop < 5; floorLoop = floorLoop + 1 )
    {
        // vertical walls
        wall = 1;
        wallBit = 2;
        for( k = 0; k < 8; k = k + 1 )
        {
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            if( ( b & bit ) != 0 )
              wall = wall | wallBit;
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            bit = ( bit << 1 ) & 0xFF;
            if( bit == 0 )
            {
                b = neuStageBytes[ neuStageIndex ][ byteIdx ]; byteIdx = byteIdx + 1;
                bit = 1;
            }
        }
        neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;

        // vertical walls (copy of upper row)
        for( c = 0; c < NEU_MAP_WIDTH; c = c + 1 )
        {
            neuWallMap[ mapIdx ] = neuWallMap[ mapIdx - NEU_MAP_WIDTH ];
            mapIdx = mapIdx + 1;
        }

        // horizontal walls
        wall = 1;
        wallBit = 2;
        for( k = 0; k < 8; k = k + 1 )
        {
            if( ( b & bit ) != 0 )
              wall = wall | wallBit;
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            if( ( b & bit ) != 0 )
              wall = wall | wallBit;
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            wall = wall | wallBit;
            wallBit = ( wallBit << 1 ) & 0xFF;
            if( wallBit == 0 )
            {
                neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
                wall = 0; wallBit = 1;
            }
            bit = ( bit << 1 ) & 0xFF;
            if( bit == 0 )
            {
                b = neuStageBytes[ neuStageIndex ][ byteIdx ]; byteIdx = byteIdx + 1;
                bit = 1;
            }
        }
        neuWallMap[ mapIdx ] = wall; mapIdx = mapIdx + 1;
    }
}

void neuInitStage()
{
    int index;
    index = neuCurrentStage;
    while( index >= NEU_STAGE_COUNT )
      index = index - NEU_STAGE_COUNT;
    neuStageIndex = index;

    neuBuildWallMap();
    neuInitCards();
}

void neuInitTrying()
{
    int x, y;
    int cardCount, t;
    int i, j;

    neuResetHeldOrThrownCardOnRetry();

    x = neuFromPositionPixel( neuStageStartPacked[ neuStageIndex ] >> 4 );
    y = neuFromPositionPixel( neuStageStartPacked[ neuStageIndex ] & 0x0f );
    neuInitSolver( x, y );

    cardCount = neuStageCardCount[ neuStageIndex ];
    t = cardCount << 4;
    t = t - ( neuCurrentStage >> 2 );
    if( t < 0 ) t = 0;
    t = t + 60;
    neuStageTime = t;

    neuInitMonsters();

    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        neuStatusChar[ i ][ j ] = 0;
    neuOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in neuUpdateTitle()) - matches neuOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches neuInitTrying() without going through that transition first
    // - the same defensive pattern crkInitTrying() already established.
    neuFullWidthText = false;
    neuRevealCardIndexA = -1;
    neuRevealCardIndexB = -1;
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void neuMapToVVram()
{
    int x, y, b, bit, mapIdx;

    // Pass 1: raw wall-bit grid (0 or 0x0f) from neuWallMap bits.
    mapIdx = 0;
    for( y = 0; y < NEU_VVRAM_HEIGHT; y = y + 1 )
    {
        b = neuWallMap[ mapIdx ]; mapIdx = mapIdx + 1;
        bit = 2;
        for( x = 0; x < NEU_VVRAM_WIDTH - 1; x = x + 1 )
        {
            if( ( b & bit ) != 0 )
              neuVVram[ y ][ x ] = 0x0f;
            else
              neuVVram[ y ][ x ] = 0;
            bit = ( bit << 1 ) & 0xFF;
            if( bit == 0 )
            {
                b = neuWallMap[ mapIdx ]; mapIdx = mapIdx + 1;
                bit = 1;
            }
        }
        neuVVram[ y ][ NEU_VVRAM_WIDTH - 1 ] = 0;
    }

    // Pass 2: per-cell corner/edge variant (0-15), still raw. Reading an
    // already-transformed neighbor (the row above, or an earlier column in
    // this same row) is safe here - the transform below preserves each
    // cell's own zero/nonzero-ness exactly, so a nonzero check gives the
    // same answer whether the neighbor has been visited by this pass yet
    // or not (verified by hand before trusting this in-place, single-pass
    // shape rather than needing a separate raw-vs-transformed buffer).
    for( y = 0; y < NEU_VVRAM_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < NEU_VVRAM_WIDTH - 1; x = x + 1 )
        {
            int old, c;
            old = neuVVram[ y ][ x ];
            c = old;
            if( x == 0 || neuVVram[ y ][ x - 1 ] != 0 )
              c = c & ~0x01;
            if( x == NEU_VVRAM_WIDTH - 2 || neuVVram[ y ][ x + 1 ] != 0 )
              c = c & ~0x02;
            if( y > 0 && neuVVram[ y - 1 ][ x ] != 0 )
              c = c & ~0x04;
            if( y < NEU_VVRAM_HEIGHT - 1 && neuVVram[ y + 1 ][ x ] != 0 )
              c = c & ~0x08;
            if( c == 0 && old != 0 )
              c = 0x10;
            neuVVram[ y ][ x ] = c;
        }
        neuVVram[ y ][ NEU_VVRAM_WIDTH - 1 ] = 0x0f;
    }

    // Pass 3: convert to Char_Wall-based glyph indices.
    for( y = 0; y < NEU_VVRAM_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < NEU_VVRAM_WIDTH; x = x + 1 )
        {
            int c;
            c = neuVVram[ y ][ x ];
            if( c == 0 || c == 0x10 )
              c = NEU_CHAR_WALL;
            else
              c = c + NEU_CHAR_WALL;
            neuVVram[ y ][ x ] = c;
        }
    }
}

void neuDrawAll()
{
    neuMapToVVram();
    neuDrawCardsIntoVVram();
    neuDrawSpritesIntoVVram();
}

// Reproduces Put2C()'s own nibble packing for a 2x2-glyph icon drawn
// directly at a status-area (page,charCol) position - see header comment.
int neuIconByte( int glyphBase, bool isRightHalf, int sub )
{
    int upper, lower, upperByte, lowerByte;
    if( isRightHalf )
    {
        upper = glyphBase + 1;
        lower = glyphBase + 3;
    }
    else
    {
        upper = glyphBase;
        lower = glyphBase + 2;
    }
    if( sub == 0 )
    {
        upperByte = neuCharPattern[ upper * 2 + 0 ];
        lowerByte = neuCharPattern[ lower * 2 + 0 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    if( sub == 1 )
    {
        upperByte = neuCharPattern[ upper * 2 + 0 ];
        lowerByte = neuCharPattern[ lower * 2 + 0 ];
        return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
    }
    if( sub == 2 )
    {
        upperByte = neuCharPattern[ upper * 2 + 1 ];
        lowerByte = neuCharPattern[ lower * 2 + 1 ];
        return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    }
    upperByte = neuCharPattern[ upper * 2 + 1 ];
    lowerByte = neuCharPattern[ lower * 2 + 1 ];
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly for
// the map area, plus the two direct-to-hardware status-area icons (held
// card, remaining-lives) and the plain ASCII status-grid text - all drawn
// directly at their own (col,page), no mirroring/reordering (see header).
//
// **OR-combines the map layer (mapByte) with the text layer (textByte)
// exactly the way gameCracky.c's own crkComposeRawByte() does**, rather
// than exclusively choosing one - needed so the title screen's own real
// bitmap logo (drawn into neuVVram at pages 1-2 by neuBeginTitle(), see
// that function's own comment) can coexist with the status-grid text
// ("MINI"/"START"/"CONTINUE"/the credit line/lives icons, pages 0/3/5/6/7)
// drawn at the very same (neuFullWidthText-widened) columns. The two never
// actually collide: mapByte is only ever nonzero on pages 1-2 (the logo's
// own real VVram rows 2-5), and every piece of status-grid text sits on a
// disjoint page - so this can never blend two real, distinct pieces of
// content together, it just lets both exist in one composed byte instead
// of one silently excluding the other.
int neuComposeRawByte( int col, int page )
{
    int mapByte, textByte;

    mapByte = 0;
    if( col < NEU_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = col / 4;
        sub = col % 4;
        upper = neuVVram[ page * 2 ][ mapX ];
        lower = neuVVram[ page * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = neuCharPattern[ upper * 2 + 0 ];
            lowerByte = neuCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = neuCharPattern[ upper * 2 + 0 ];
            lowerByte = neuCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = neuCharPattern[ upper * 2 + 1 ];
            lowerByte = neuCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = neuCharPattern[ upper * 2 + 1 ];
            lowerByte = neuCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !neuFullWidthText && col < NEU_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // neuStatusChar's own full-width indexing directly - no "subtract the
    // map width" local-offset math needed, since col/4 already lands on
    // the correct real column either way (whether this is the
    // neuFullWidthText title path using the whole range, or the normal
    // gameplay path where col is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = col / 4;
        sub = col % 4;
        if( charCol < 32 )
        {
            // Held-card / remain-life icons are drawn directly via Put2C()
            // upstream, bypassing the plain-ASCII status grid - both live
            // at real column LeftX(24) onward, same as every other status
            // element, so the checks below compare against the real
            // column number directly.
            if( page == 6 && neuHeldCardGlyph >= 0 && ( charCol == 24 || charCol == 25 ) )
              textByte = neuIconByte( neuHeldCardGlyph, charCol == 25, sub );
            else if( page == 7 && neuRemainIconCount > 0 &&
                     charCol >= 24 && charCol < 24 + neuRemainIconCount * 2 )
              textByte = neuIconByte( NEU_CHAR_SOLVER_LEFT, ( charCol & 1 ) != 0, sub );
            else
            {
                c = neuStatusChar[ page ][ charCol ];
                textByte = neuAsciiPattern[ c * 4 + sub ];
            }
        }
    }
    return mapByte | textByte;
}

void neuRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( neuOverlayActive && page == neuOverlayPage &&
                col >= neuOverlayCol * 4 && col < neuOverlayCol * 4 + neuOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - neuOverlayCol * 4 ) / 4;
                sub = ( col - neuOverlayCol * 4 ) % 4;
                value = neuAsciiPattern[ neuAsciiIndex( neuOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = neuComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void neuBeginLoseDecide()
{
    neuRemainCount = neuRemainCount - 1;
    if( neuRemainCount != 0 )
    {
        neuInitTrying();
        neuPrintStatus();
        neuDrawAll();
        neuStartSeq( 1, NEU_MELODY_START );
        neuState = NEU_STATE_START_JINGLE;
    }
    else
    {
        neuPrintGameOver();
        neuStartSeq( 1, NEU_MELODY_GAMEOVER );
        neuState = NEU_STATE_GAMEOVER_JINGLE;
    }
}

// **Rewritten a second time, after a real user-supplied photo of the
// sibling game Cracky running on actual UIAPduino hardware proved this
// whole "cram title text into an 8-column status grid" model was wrong
// from the start - see neuStatusChar's own header comment, and
// gameCracky.c's own crkBeginTitle() comment, for the full corrected
// story.** The previous version of this function (see git history / the
// CLAUDE.md writeup of this exact fix) treated the earlier out-of-bounds/
// collision bug as proof that upstream's own title text genuinely has
// nowhere to go - so it dropped "MINI" and the "INUFUTO" credit line
// outright and truncated "CONTINUE" to "CONTINU". Re-reading upstream's
// real `Status.cpp` (`Title()`) line by line shows that diagnosis was
// backwards: upstream's own Vram address space is a genuinely wide
// 32-char-cell-per-page canvas, the status labels occupy only columns
// 24-31 (`LeftX=24`), and every piece of title-screen text sits at
// columns 0-23 (the real "NEURAS" bitmap logo, columns 8-23 for "MINI"),
// 9-16 ("START"/"CONTINUE", with the cursor arrow at column 8), and 12-23
// (the "INUFUTO 2026" credit line) - all genuinely clear of the status
// labels' own columns. The ROOT problem was this port's own
// `neuStatusChar` being modeled as an 8-column-wide grid in the first
// place - now fixed there, this function places everything at upstream's
// real, literal columns, with `neuFullWidthText=true` so
// neuComposeRawByte() renders the full canvas instead of just the narrow
// status zone.
//
// **Rewritten a THIRD time to fix a second, separate architectural bug
// found in the sibling game Cracky (see that file's own header comment,
// `crkBeginTitle()`'s own comment, and CLAUDE.md's own "A second, related
// architectural issue" writeup): the earlier version of this function
// still simplified upstream's real, prominent 96-value pixel-art
// "NEURAS" wordmark logo (`Status.cpp`'s own `TitleBytes[]` table, the
// single biggest, most visually important element on the whole title
// screen) down to plain small ASCII text - reasoning it was "purely
// decorative". That reasoning was wrong, the same way it was wrong for
// Cracky's own "CRACKY" logo: it's not filler, it's the actual title
// wordmark. Restored to the real bitmap below, drawn directly into
// neuVVram from neuTitleBytes[] at its own real position (VVram rows 2-5,
// i.e. real hardware pages 1-2 - matching upstream's `Status.cpp`
// `Title()`'s own `VVramFront + VVramWidth*2 + TitleLeft` starting offset
// exactly, where `TitleLeft = (VVramWidth(24) - 4*TitleLength(6))/2 = 0`
// here, same as Cracky's own logo, since both titles are also 6 letters
// long). neuComposeRawByte() was updated to OR-combine this VVram content
// with neuStatusChar's own text layer rather than choosing one
// exclusively, since the two occupy disjoint page ranges by construction
// (see that function's own comment) - the exact same fix shape already
// applied to gameCracky.c.
void neuBeginTitle()
{
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int i, j;

    for( i = 0; i < NEU_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < NEU_VVRAM_WIDTH; j = j + 1 )
        neuVVram[ i ][ j ] = NEU_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        neuStatusChar[ i ][ j ] = 0;
    neuOverlayActive = false;
    neuFullWidthText = true;
    neuHideAllSprites();
    neuHeldCardGlyph = -1;
    neuRemainIconCount = 0;
    neuRevealCardIndexA = -1;
    neuRevealCardIndexB = -1;

    // Reset stale TIME value so a game-over doesn't leave the final
    // countdown visible on the title screen - matching Cracky's own
    // identical, already-user-reported fix for this exact engine.
    neuStageTime = 0;
    neuPrintStatus();

    // **Restored, after the same real user-supplied hardware photo (of the
    // sibling game Cracky) that proved the earlier plain-text substitute
    // was wrong for that game too** - this is upstream's own real 6-glyph
    // "NEURAS" logo bitmap, drawn directly into neuVVram from
    // neuTitleBytes[] at its own real position (VVram rows 2-5, i.e. real
    // hardware pages 1-2 - matching upstream's `Status.cpp` `Title()`'s
    // own `VVramFront + VVramWidth*2 + TitleLeft` starting offset exactly,
    // TitleLeft=0 here since the 6-letter wordmark exactly fills the full
    // 24-cell VVramWidth). The earlier version of this function replaced
    // this with plain small text, reasoning it was "purely decorative" -
    // wrong: it's the actual title wordmark, meant to be the single
    // biggest, most prominent element on the whole screen, not a
    // throwaway detail. `neuComposeRawByte()` was updated to OR-combine
    // this VVram content with neuStatusChar's own text layer rather than
    // choosing one exclusively, since the two occupy disjoint page ranges
    // by construction (see that function's own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                neuVVram[ 2 + row ][ ch * 4 + col ] = neuTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at TitleLeft+4*TitleLength-5 = col 19,
    // START/CONTINUE at ArrowX+1 = col 9 with the cursor at ArrowX = col 8,
    // the credit line at col 12) - all genuinely clear of the status
    // labels' own columns 24-31, so nothing here needs trimming,
    // relocating, or dropping anymore.
    neuPrintS( 3, 19, sMini, 4 );
    neuPrintS( 5, 9, sStart, 5 );
    neuPrintS( 6, 9, sContinue, 8 );
    neuPrintS( 7, 12, sCredit, 12 );

    neuSelection = 0;
    neuSelectionChanged = true;
    neuPrevLeft = 0; neuPrevRight = 0; neuPrevUp = 0; neuPrevDown = 0; neuPrevFire = 0;
    neuState = NEU_STATE_TITLE;
}

void neuUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !neuPrevLeft ) || ( right && !neuPrevRight ) ||
                ( up && !neuPrevUp ) || ( down && !neuPrevDown ) );
    justFire = ( fire && !neuPrevFire );
    neuPrevLeft = left; neuPrevRight = right; neuPrevUp = up; neuPrevDown = down; neuPrevFire = fire;

    if( neuSelectionChanged )
    {
        neuSelectionChanged = false;
        // ArrowX = 8, matching upstream's own real column - see
        // neuBeginTitle()'s own comment for where START/CONTINUE
        // themselves (col 9) sit relative to this.
        if( neuSelection == 0 )
          neuPrintC( 5, 8, '>' );
        else
          neuPrintC( 5, 8, ' ' );
        if( neuSelection == 1 )
          neuPrintC( 6, 8, '>' );
        else
          neuPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        neuFullWidthText = false;
        neuScore = 0;
        if( neuSelection == 0 )
          neuCurrentStage = 0;
        neuRemainCount = 3;
        neuInitStage();
        neuInitTrying();
        neuPrintStatus();
        neuDrawAll();
        neuStartSeq( 1, NEU_MELODY_START );
        neuState = NEU_STATE_START_JINGLE;
        neuRender();
        return;
    }
    if( justDir )
    {
        neuSelection = neuSelection ^ 1;
        neuSelectionChanged = true;
    }
    neuRender();
}

void neuUpdateStartJingle()
{
    if( !neuSeqPlaying( 1 ) )
    {
        neuStartBgm();
        neuTimeCount = NEU_TIME_RATE;
        neuTickCounter = 0;
        neuState = NEU_STATE_PLAYING;
    }
    neuRender();
}

void neuBeginDieAnim()
{
    neuAnimStep = 0;
    neuWaitFrames = 0;
    neuState = NEU_STATE_DIE_ANIM;
}

void neuUpdateDieAnim()
{
    int patterns[4] = { NEU_CHAR_SOLVER_LEFT, NEU_CHAR_SOLVER_DOWN, NEU_CHAR_SOLVER_RIGHT, NEU_CHAR_SOLVER_UP };

    if( neuWaitFrames > 0 )
    {
        neuWaitFrames = neuWaitFrames - 1;
        neuRender();
        return;
    }

    neuShowSprite( NEU_SPRITE_SOLVER, neuSolverX, neuSolverY, patterns[ neuAnimStep & 3 ] );
    neuDrawAll();
    neuStartSeq( 0, NEU_MELODY_LOOSE );
    neuAnimStep = neuAnimStep + 1;
    neuWaitFrames = neuNoteFrames( 1 );

    if( neuAnimStep >= 20 )
      neuBeginLoseDecide();

    neuRender();
}

void neuBeginTimeUp()
{
    neuPrintTimeUp();
    neuTimeUpSweepCount = 0;
    neuWaitFrames = 0;
    neuState = NEU_STATE_TIMEUP_SWEEP;
}

void neuUpdateTimeUpSweep()
{
    if( neuWaitFrames > 0 )
    {
        neuWaitFrames = neuWaitFrames - 1;
        neuRender();
        return;
    }

    neuStartSeq( 0, NEU_MELODY_LOOSE );
    neuWaitFrames = neuNoteFrames( 1 );
    neuTimeUpSweepCount = neuTimeUpSweepCount + 1;
    if( neuTimeUpSweepCount >= 15 )
      neuBeginLoseDecide();

    neuRender();
}

void neuUpdateGameOverJingle()
{
    if( !neuSeqPlaying( 1 ) )
      neuBeginTitle();
    else
      neuRender();
}

void neuBeginClearJingle()
{
    neuStartSeq( 1, NEU_MELODY_CLEAR );
    neuState = NEU_STATE_CLEAR_JINGLE;
}

void neuUpdateClearJingle()
{
    if( !neuSeqPlaying( 1 ) )
      neuState = NEU_STATE_BONUS_TALLY;
    neuRender();
}

void neuUpdateBonusTally()
{
    if( neuWaitFrames > 0 )
    {
        neuWaitFrames = neuWaitFrames - 1;
        neuRender();
        return;
    }

    if( neuStageTime >= NEU_BONUS_RATE )
    {
        neuScore = neuScore + 1;
        neuStageTime = neuStageTime - NEU_BONUS_RATE;
        neuPrintStatus();
        neuStartSeq( 0, NEU_MELODY_BEEP );
        // matches upstream's own Sound_Beep() (tempo-based block) *plus* a
        // separate, literal WaitTimer(1) tick right after it - see header.
        neuWaitFrames = neuNoteFrames( 1 ) + 1;
        neuRender();
        return;
    }

    neuStageTime = 0;
    neuPrintStatus();
    neuCurrentStage = neuCurrentStage + 1;
    neuInitStage();
    neuInitTrying();
    neuPrintStatus();
    neuDrawAll();
    neuStartSeq( 1, NEU_MELODY_START );
    neuState = NEU_STATE_START_JINGLE;
    neuRender();
}

void neuUpdateMatchReveal()
{
    if( neuWaitFrames > 0 )
    {
        neuWaitFrames = neuWaitFrames - 1;
        neuRender();
        return;
    }

    if( neuMatchIsHit )
    {
        neuCards[ neuMatchCardIndex ].status = NEU_CARD_STATUS_NONE;
        neuCards[ neuMatchOtherIndex ].status = NEU_CARD_STATUS_NONE;
        neuRevealCardIndexA = -1;
        neuRevealCardIndexB = -1;
        neuRemainCardCount = neuRemainCardCount - 2;
        neuAddScore( 10 );
        neuState = NEU_STATE_PLAYING;
        neuDrawAll();
        neuRender();
        return;
    }

    neuRevealCardIndexA = -1;
    neuRevealCardIndexB = -1;
    neuContinueMatchScan();
    neuDrawAll();
    neuRender();
}

void neuUpdatePlaying()
{
    neuTickCounter = neuTickCounter + 1;
    if( neuTickCounter < NEU_TICK_DIVISOR )
    {
        neuDrawAll();
        neuRender();
        return;
    }
    neuTickCounter = 0;

    neuMoveSolver();
    neuMoveCard();
    if( neuState != NEU_STATE_PLAYING )
    {
        neuDrawAll();
        neuRender();
        return;
    }

    neuMoveMonsters();

    if( neuSolverStatus == NEU_SOLVER_DIE )
    {
        neuStopBgm();
        neuBeginDieAnim();
        neuDrawAll();
        neuRender();
        return;
    }

    neuTimeCount = neuTimeCount - 1;
    if( neuTimeCount == 0 )
    {
        neuStageTime = neuStageTime - 1;
        neuTimeCount = NEU_TIME_RATE;
        neuPrintTime();
        if( neuStageTime == 0 )
        {
            neuStopBgm();
            neuBeginTimeUp();
            neuDrawAll();
            neuRender();
            return;
        }
    }

    if( neuRemainCardCount == 0 )
    {
        neuStopBgm();
        neuBeginClearJingle();
        neuDrawAll();
        neuRender();
        return;
    }

    neuDrawAll();
    neuRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameNeuras_init( void )
{
    int i;

    neuScore = 0;
    neuCurrentStage = 0;
    neuRemainCount = 3;
    neuStageTime = 0;
    neuHeldCardIndex = -1;
    neuThrownCardIndex = -1;

    for( i = 0; i < 3; i = i + 1 )
    {
        neuSeqActive[ i ] = 0;
        neuSeqMelody[ i ] = NEU_MELODY_NONE;
    }
    neuOverlayActive = false;
    neuTickCounter = 0;
    neuHeldCardGlyph = -1;
    neuRemainIconCount = 0;
    neuRevealCardIndexA = -1;
    neuRevealCardIndexB = -1;

    neuBeginTitle();
}

void gameNeuras_update( void )
{
    neuAdvanceSound();

    if( neuState == NEU_STATE_TITLE )
      neuUpdateTitle();
    else if( neuState == NEU_STATE_START_JINGLE )
      neuUpdateStartJingle();
    else if( neuState == NEU_STATE_PLAYING )
      neuUpdatePlaying();
    else if( neuState == NEU_STATE_MATCH_REVEAL )
      neuUpdateMatchReveal();
    else if( neuState == NEU_STATE_DIE_ANIM )
      neuUpdateDieAnim();
    else if( neuState == NEU_STATE_TIMEUP_SWEEP )
      neuUpdateTimeUpSweep();
    else if( neuState == NEU_STATE_GAMEOVER_JINGLE )
      neuUpdateGameOverJingle();
    else if( neuState == NEU_STATE_CLEAR_JINGLE )
      neuUpdateClearJingle();
    else if( neuState == NEU_STATE_BONUS_TALLY )
      neuUpdateBonusTally();
}
