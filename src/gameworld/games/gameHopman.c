// =============================================================================
// HOPMAN (inufuto, UIAPduino+SSD1306+CH32V003 "cate" engine, license "None
// specified" - GitHub reports no LICENSE file for `UIAPduino_hopman`) - a
// side-scrolling platformer: jump between floors and needles across a wide
// (96-column, 3-floor) horizontally-scrolling stage, riding lifts and
// dodging patrolling monsters, to reach a goal marker far to the right while
// collecting items along the way for bonus points. 8 hand-authored stages,
// 3 lives, no persistent hi-score (upstream itself has this commented out -
// `// word HiScore;` - a genuinely disabled feature, not something this port
// dropped).
//
// Same author/engine family as this project's own already-shipped Cracky
// port (`gameCracky.c`) - read in full as the reference implementation for
// this port, per direct instruction. Real SSD1306 display streamed one raw
// byte at a time (`SendOledData()`), the same byte-per-(column,page) model
// `md_drawColumn()` already handles, and a real explicit 60Hz SysTick frame
// limiter (`Timer.cpp`'s own `kTimerHz=60`) - same as Cracky, no AVR-style
// "silently N times too fast" risk despite the CH32V003/RISC-V host.
//
// **No hardware display-orientation transform is needed, matching Cracky's
// own final, hard-won conclusion** (see that file's own header comment for
// the debugging saga that established this) - `hopComposeRawByte(col,page)`
// is drawn directly at its own `(col,page)` via plain `md_drawColumn()`, no
// mirroring, no per-bit reversal, no lookup table.
//
// **Rendering reuses Cracky's exact two-level tile system**: a 24x16 logical
// glyph-index grid (`hopVVram`, `VVramWidth==WindowWidth==24`) is packed into
// real screen bytes via a 2-byte-per-glyph `CharPattern` lookup and the same
// nibble-interleaving derived in Cracky (`hopComposeRawByte()`'s own
// sub-0/1/2/3 branches, upper/lower from two vertically-stacked VVram rows).
// The one genuine structural difference from Cracky: **this stage is much
// wider than the screen and scrolls** - the real map is 96 columns x 2 (a
// `StageWidth` of 192 x-units) x 3 floors, addressed via a 2-bit-per-cell
// `CellMap` (`Cell_Space/Cell_Floor/Cell_Item/Cell_Needle`), and only a
// 24-VVram-cell-wide window (`hopLeftX..hopLeftX+23`) is visible at once -
// `hopMapToVVram()` is a direct structural mirror of upstream's own
// `MapToVVram()` (its own scroll-offset/byte-boundary math kept exactly as
// upstream computes it, not re-derived), and `hopScroll()` recenters the
// window on the player every time they move horizontally or ride a lift,
// matching `Stage.cpp`'s own `Scroll()` exactly.
//
// **Movable coordinates are genuine pixel-scale x/y** (not VVram-cell-
// degenerate like Cracky's own `CoordShift=0` case) - `Movable{x,y,sprite}`
// has no `dx/dy/status` fields at all here (those live on `Man`/`Monster`/
// `Lift` individually), and `ShowSprite()`'s own `x = pMovable->x - LeftX`
// relies on real AVR `byte` (uint8_t) wraparound to detect "scrolled off
// the left edge" (a huge wrapped value fails the `<VVramWidth` check) - the
// exact "AVR-implicit-narrow-type-reliance" bug family this project has
// hit repeatedly elsewhere. Ported with a direct, explicit sign check
// instead (`x>=0 && x<HOP_VVRAM_WIDTH`) rather than reproducing the
// wraparound trick, matching this project's own standing practice.
//
// **A real, deliberate gravity/jump physics model** (`Man.dx/dy`, a
// three-tick rising jump `dy=-3,-2,-1` decaying toward 0 via +1/tick
// gravity, then falling `dy=+1,+2,+3,...` until a floor or a lift is
// found) - ported as a direct, careful line-by-line translation of
// `Man.cpp`'s own `MoveMan()`, preserving every exact nuance: horizontal
// movement only updates `dx`/the facing `pattern` when `CanMoveX()`
// actually succeeds (a blocked direction press doesn't even turn the
// sprite to face that way, matching upstream exactly); landing on a lift
// mid-fall (`FindLift()`) takes priority over landing on a floor; the
// jump-animation frame (`pattern+4`) happens to alias the same glyph index
// as each direction's own first walk-cycle frame (`Man_Left0`/
// `Man_Right0`), a real upstream quirk preserved rather than "corrected".
//
// **Monsters are simple, deterministic left-right patrollers** (no chase
// AI, unlike Cracky's own `crkDecideDirection()`) - `Monster.cpp` ported
// near-verbatim. **Lifts** (horizontal or vertical moving platforms,
// `Lift.cpp`) are a genuinely new mechanic Cracky doesn't have - ported
// with an index-based `hopFindLift()` (returns -1 for "not found") instead
// of upstream's own `ptr<Lift>`-or-`nullptr` return, matching this
// project's own established "resolve by id, not by real pointer" pattern
// (e.g. Tiny Dungeon's bitmap-array resolver) rather than risking an
// unproven pointer-vs-NULL comparison on this dialect.
//
// **A genuine dual-rate throttle, decoded precisely from `Main.cpp`'s own
// `Clock`-gated `do`/`while` loop rather than guessed**: the raw loop runs
// essentially unthrottled except for one real blocking `WaitTimer(10)`
// call, which only fires once every 4 raw iterations (`Clock&3==0`) - so
// the *effective* real-world "one full logic tick" period is exactly 10
// real 60Hz ticks (~166ms), reproduced here as a plain whole-function tick
// divisor, `HOP_TICK_DIVISOR=10`, gating `hopUpdatePlaying()`'s entire
// body (input/physics/redraw together) the same way this project's own
// NumberPlace/HollowSeeker/t2048/Doc/Pacman/Pipe/Tiny-Mania throttles do.
// Lift movement is additionally gated to only every *other* logic tick
// (`Clock&7==0`, true on alternating quanta since `Clock` increases by 4
// each real tick) - reproduced with a simple `hopLiftPhase` boolean
// toggled once per logic tick, rather than needing to replicate the exact
// numeric `Clock` value.
//
// **Sound**: the same real 3-tone-channel software mixer/tracker shape as
// Cracky's own `Sound.cpp` (a different `Tempo=180` here, vs Cracky's 160,
// giving a slightly different real note-duration-per-tick formula,
// `hopNoteFrames(length) = round(length*(300/180)+0.5)`), routed the same
// way onto `md_playTone()`/`md_stopTone()` with 3 independent frame-
// stepped sequencer slots (0=one-shot SFX, 1=jingle/BGM-voice-A reused
// exactly like Cracky's own channel-1 reuse, 2=BGM-voice-B), advanced
// every real engine frame regardless of `HOP_TICK_DIVISOR`. Every melody
// table (`Sound_Loose/Hit/Beep/Bonus/Start/Clear/GameOver`, `StartBGM`'s
// own two simultaneous voices) was extracted programmatically from the
// real upstream source (resolving the `NoteLength`/`Scale` enum constants
// and the one `N8+N2` arithmetic expression via a small Python evaluator),
// not hand-transcribed, matching this project's own "byte-diff transcribed
// tables" discipline. `crkMelodyLength()`'s own Cracky equivalent turned
// out to be genuine dead code (defined but never actually called anywhere
// in `crkAdvanceOneSeq()`, which only ever uses the terminator values 0/255
// to know when to stop/loop) - confirmed the same holds here and skipped
// porting the equivalent function entirely, rather than reproducing
// unreachable code.
//
// **The title screen's own real "HOPMAN" logo is a genuine raw-VVram
// pixel-art bitmap (`hopTitleBytes[]`, drawn in `hopBeginTitle()`), not
// decorative text** - an earlier version of this port had replaced it with
// plain status-column text, reasoning it was "purely decorative"; that
// turned out to be the same wrong call Cracky's own port initially made for
// its identical "CRACKY" logo (see `gameCracky.c`'s own header comment for
// the full debugging saga that established this) - it's the single
// biggest, most prominent element on the whole title screen, not a
// throwaway detail, and the real bitmap has its own dedicated per-letter
// pixel data with no dependency on the shared small status-text font at
// all. **Fixed to match Cracky's own restored logo exactly**: see
// `hopTitleBytes`'s own header comment (data table section) and
// `hopBeginTitle()`'s own comment (state machine section) for the full
// derivation and drawing-loop details, and `hopComposeRawByte()`'s own
// comment for how the resulting VVram content is OR-combined with the
// status-text layer rather than one excluding the other. This also means
// the title word no longer needs `hopAsciiPattern`'s own hand-added 'H'
// glyph workaround (see that table's own header comment, and finding #1
// below) - that glyph is left in place regardless, in case some other
// status text elsewhere in this file still needs it, but the title word
// itself is now a real pixel-art bitmap that never touches the reduced
// 27-glyph status font at all.
//
// **A real architectural bug, found and fixed after the fact via a real
// user-supplied photo of Cracky running on actual hardware** (see
// `gameCracky.c`'s own header comment, `crkStatusChar`/`crkFullWidthText`/
// `crkBeginTitle`, for the full writeup - this section only covers what
// changed here). The photo proved upstream's real `PrintC()`/`PrintS()`
// text mechanism writes to a genuinely 32-character-cell-wide row (128
// real pixels / VramStep=4), not just the narrow 8-cell status-label slice
// (`LeftX=24`..31) this port originally modeled `hopStatusChar` as. The
// title screen's own text (the logo, "MINI", "START"/"CONTINUE", the
// credit line) all live at upstream's real columns 8-23, well clear of
// the status labels - genuinely different real estate than the status
// labels, not a cramped overlap needing truncation/relocation. The
// original design here avoided Cracky's exact overflow/clobber symptoms
// only by *also* dropping real content to fit (not calling
// `hopPrintStatus()` first, matching upstream's own real `Title()`, which
// does; truncating "CONTINUE" to "CONTINU") - a real gap, not a correct
// alternative design, once the true column budget was understood.
// **Fixed** the same way as Cracky: `hopStatusChar` widened to `[8][32]`;
// `hopPrintScore()`/`hopPrintTime()`/`hopPrintStatus()` now use upstream's
// real `LeftX`-relative columns (24, 26, 29, 30) instead of a local 0-7
// offset; a new `hopFullWidthText` flag (true only during
// `HOP_STATE_TITLE`) makes `hopComposeRawByte()` read the full 0-31
// char-cell range from `hopStatusChar` instead of splitting between VVram
// (map) and the narrow status slice; and `hopBeginTitle()` now calls
// `hopPrintStatus()` first (matching upstream's real `Title()` exactly)
// and places every title-screen string at upstream's own real, literal
// columns - "HOPMAN" logo text at page 2 col 0 (simplified placement, no
// exact upstream bitmap position to preserve), "MINI" at page 3 col 19,
// "START"/"CONTINUE" (now the full 8-character word, no longer truncated)
// at page 5/6 col 9 with the cursor at col 8, and the credit line restored
// to upstream's real single-line "INUFUTO 2026" (previously split across
// two separate, differently-positioned calls) at page 7 col 12 - all
// genuinely clear of the status labels' own columns 24-31, so nothing
// needs trimming, relocating, or dropping anymore.
//
// **A dedicated post-ship verification pass** (this game was originally
// ported in a large parallel batch and only test-compiled, never played) -
// re-derived every data table programmatically from the real upstream
// source via a small Python script and byte-diffed it against this file's
// own copy (`hopAsciiPattern`/`hopCharPattern`/`hopCellChars`/every one of
// the 8 stages' `itemCount`/`monsterCount`/`liftCount`/monster-triple/
// lift-pair/72-byte cell-map tables/all 9 sound melodies including both
// simultaneous `StartBGM()` voices) - all matched byte-for-byte on the
// first pass, no transcription errors found anywhere in the data. Traced
// the entire state machine (title -> start jingle -> playing -> lose-
// anim -> retry-or-gameover-jingle -> title, plus clear-wait -> clear-
// jingle -> bonus-tally -> perfect-tally -> next stage) line-by-line
// against `Main.cpp`'s own real `goto`-chained loop, including the exact
// `Clock&3`/`Clock&7` tick-and-lift-phase cadence and every non-zero
// upstream global initializer. Found and fixed two real bugs:
//
// 1) **A genuine rendering bug, caught immediately from the very first
// live screenshot**: the title screen showed "OPMAN" instead of "HOPMAN"
// - the leading 'H' silently rendered as a blank space. Root cause:
// `hopAsciiPattern`/`hopAsciiIndex()` are a byte-for-byte-verified copy
// of upstream's own real, reduced 27-glyph status-text font
// (`" 0123456789>ACEFGIMNOPRSTUV"`, `Chars.cpp`) - which genuinely has
// **no 'H' at all**. Upstream itself never hits this gap, because its
// own `Title()` draws "HOPMAN" as a hand-authored raw-VVram bitmap logo
// (`TitleBytes[]`, `Status.cpp`), never through this text font at all -
// but this port's own header comment above (see "the title screen's
// decorative logo... was simplified... replaced with plain status-column
// text") replaced that bitmap logo with plain `hopPrintS()` calls,
// hitting the one letter the reduced font doesn't have, for the one
// string that happens to need it most (the game's own name). **Fixed**
// by appending a 29th (0-indexed 27th) glyph - an 'H' built the same
// two-full-verticals-plus-a-middle-crossbar shape already used by this
// same font's own 'A'/'8' glyphs (whose existing bit-for-row-2 pattern
// already establishes "row 2 = the vertical center" for a horizontal
// stroke) - appended at the *end* of both `hopAsciiPattern` and
// `hopAsciiIndex()`'s own search table, not inserted alphabetically, so
// no existing character's index shifts. Verified via a Python simulation
// of the exact composite-byte math (rendering the glyph as ASCII art)
// before ever rebuilding, then confirmed live via Puppeteer that the
// title screen now correctly reads "HOPMAN".
// **Superseded, later in the same session** (see this file's own top-level
// header comment and `hopTitleBytes`/`hopBeginTitle()`): the real "HOPMAN"
// title wordmark was itself restored as upstream's own genuine raw-VVram
// bitmap logo rather than plain text, so the title word no longer routes
// through `hopAsciiPattern`/`hopAsciiIndex()` at all and this specific 'H'
// gap can no longer affect it. The appended 'H' glyph itself is left in
// place exactly as fixed here (harmless, and may still matter for some
// other status-text string elsewhere in this file) - this bug's own root
// cause and fix are kept as real project history, not because the title
// screen still depends on the workaround today.
//
// 2) **A real timing bug in `hopUpdatePerfectTally()`**: upstream's own
// per-item "PERFECT" bonus loop is `AddScore(10); Sound_Bonus();
// WaitTimer(30);` - `Sound_Bonus()` is a non-blocking `StartMelody()`
// call (channel 0, 6 notes), so the loop's real pacing comes entirely
// from the *independent*, fixed `WaitTimer(30)` call (the melody plays
// concurrently in the background, finishing in ~13 real frames, well
// inside that 30-frame gate) - not from the melody's own play length.
// This port's own wait-frame value was `hopNoteFrames(1)*6 + 30 = 42`,
// incorrectly treating this like the genuinely *blocking* `Sound_Beep()`
// case in the tally loop just above it (`hopUpdateBonusTally()`, where
// the wait genuinely must equal the note's real duration, since
// `Sound_Beep()` is `WaitMelody`) - stretching every "PERFECT" bonus
// tick from upstream's real 30-real-frame pace out to 42, a ~40% slowdown
// across up to 21 ticks on the largest stage. **Fixed** to the literal
// upstream constant, `hopWaitFrames = 30;`.
//
// Both fixes were verified via a full rebuild and an isolated Puppeteer/
// WebGL test instance (own copy of the WebBuild folder, own HTTP port,
// per this project's own established multi-agent-safe testing practice -
// note two stale/duplicate `python -m http.server` processes were found
// bound to the same test port mid-session, from an earlier restart
// attempt that hadn't actually released the port; killed by PID and
// re-served from a fresh port once diagnosed, since the stale server was
// intermittently serving an old debug build and made one screenshot look
// like a rendering regression that wasn't real). Confirmed live: the
// title screen now reads "HOPMAN" correctly; menu -> launch -> title ->
// Fire-to-start -> start jingle -> active gameplay all transition
// correctly; the scroll window (`hopLeftX`/`hopScroll()`) correctly
// tracks the player and reveals more of the map, including a vertical
// lift and additional floors, as the player moves right; horizontal
// movement is correctly blocked at a needle cell (`hopCanMoveX()`)
// without killing the player, while jumping over one and continuing
// works; falling off the bottom of the lowest floor correctly ends the
// life (`hopMan.status &= ~HOP_MAN_LIVE`) and triggers a full stage
// retry (`hopBeginTrying()` correctly resets `hopStageTime`/player
// position/map/sprites). A temporary on-screen debug readout (printing
// `hopRemainCount`/`hopState` to an otherwise-unused status row during
// gameplay, since this game's own lives display is intentionally blank
// - see the "lives-remaining" note below) confirmed, across a live
// death-and-retry sequence, that `hopRemainCount` correctly decrements
// 3->2->1->0 across three real deaths, that each non-terminal death
// correctly re-enters `HOP_STATE_START_JINGLE` (matching upstream's
// `goto try_;`), and that the terminal death correctly reaches
// `HOP_STATE_GAMEOVER_JINGLE` with the "GAME OVER" overlay text rendered
// correctly, positioned exactly where upstream's own `PrintGameOver()`
// places it - the debug readout was fully removed again afterward
// (confirmed via a project-wide grep) before the final rebuild.
//
// **Checked and confirmed correct, not just skipped**: the lives-
// remaining status display (`hopPrintStatus()`'s own `hopRemainCount`
// block) draws blank space characters rather than any visible glyph or
// digit when `RemainCount` is 2 or 3 - this looks like it ought to be a
// bug (no on-screen indication of spare lives at all), but is a byte-
// for-byte match of this project's own already-shipped Cracky port's
// identical simplification of the same upstream `Put2C(Char_Remain)`
// icon-row mechanic (see `gameCracky.c`'s own `crkPrintStatus()`) -
// left as-is rather than "fixed" unilaterally, since it's a deliberate,
// already-established sibling precedent, not a Hopman-specific
// transcription error. Also checked and confirmed correct: the title
// screen's own held-direction-key selection-toggle behavior (pressing
// Fire while the title-screen's own arrow selection responds to an
// *already-held* direction key from before the screen was entered,
// since neither this port's `hopPrevLeft/Right/Up/Down` reset nor
// upstream's own real `Title()` loop have any equivalent debounce for
// this specific transition) - traced directly against upstream's real
// `while(true){ ...; if (key&Keys_Dir) { ...toggle...; while(ScanKeys()
// !=0); } }` and confirmed this is a genuine, faithfully-reproduced
// upstream quirk, not a porting defect. Also traced (and found provably
// unreachable, not a live bug) a theoretical `hopGetCellAtFloor()`
// column+1 overflow past the 96-column map at the stage's own far-right
// edge - the player's own real max-reachable X (190, blocked by
// `hopCanMoveX()`'s own `newX >= StageWidth-1` check) never lands on the
// one odd-X value that would trigger it.
//
// **Not independently verified live**: a genuine stage-clear (reaching
// `HOP_GOAL_X`/`HOP_GOAL_Y`) and the resulting `HOP_STATE_CLEAR_WAIT` ->
// `_CLEAR_JINGLE` -> `_BONUS_TALLY` -> `_PERFECT_TALLY` sequence -
// stage 0 alone needs 7 items collected and a genuine ~189-unit
// traversal past multiple lifts/monsters, well beyond what a scripted,
// unskilled automated playthrough reasonably reaches in a single
// session. The state machine and data for this whole sequence were
// still verified line-by-line against upstream (`Main.cpp`'s own
// post-`while(!Cleared)` code) and the one real bug found there (the
// `hopUpdatePerfectTally()` timing miscalculation above) was fixed -
// worth a direct check if a full stage clear is ever reached in real
// play and the pacing looks off.
//
// **Mechanically dialect-converted from the sibling tinyjoypad_vircon32
// project's own already-correct, already-verified gameHopman.c** (see that
// project's own CLAUDE.md for the full porting/bug-fix history behind
// every finding documented above) - only `int[N] name` -> `int name[N]`
// (and the 2D forms) array-declaration syntax and the five bare `struct`
// tags (`HopMovable`/`HopSprite`/`HopMan`/`HopMonster`/`HopLift`) ->
// `typedef struct` needed changing, matching Cracky's own identical,
// minimal conversion for this same author/engine family. Every
// string-shaped array in this file (`hopOverlayText`, the various
// `sScore`/`sStage`/`sMini`/`sContinue`/etc local buffers, the local
// `table[]` in `hopAsciiIndex()`) holds individual `'A'`-style char
// constants, never a `"..."` string literal, so - matching Cracky's own
// identical finding - none of them needed `int[]`->`char[]` widening;
// every value still round-trips through `hopAsciiIndex()`'s own
// int-comparison lookup exactly as upstream wrote it. No EEPROM/high-score
// persistence here, matching upstream having none at all (a CH32V003
// RISC-V board, not AVR, and upstream's own `HiScore` is commented out -
// see this file's own header above). No forceRedraw needed either: every
// one of this state machine's own branches calls `hopRender()`
// unconditionally on every real tick it runs, so there's no "last frame's
// pixels are still correct, skip redrawing" case anywhere in this file
// for a resumed screen to miss.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into hopCharPattern (map/sprite tiles) / hopAsciiPattern (text)
// -----------------------------------------------------------------------------

#define HOP_CHAR_SPACE 0x00
#define HOP_CHAR_LOGO 0x00
#define HOP_CHAR_FLOOR 0x10
#define HOP_CHAR_NEEDLE 0x11
#define HOP_CHAR_GOAL 0x12
#define HOP_CHAR_MAN 0x22
#define HOP_CHAR_MAN_LEFT 0x22
#define HOP_CHAR_MAN_LEFT_STOP 0x22
#define HOP_CHAR_MAN_LEFT0 0x26
#define HOP_CHAR_MAN_LEFT1 0x2A
#define HOP_CHAR_MAN_LEFT2 0x2E
#define HOP_CHAR_MAN_RIGHT 0x32
#define HOP_CHAR_MAN_RIGHT_STOP 0x32
#define HOP_CHAR_MAN_RIGHT0 0x36
#define HOP_CHAR_MAN_RIGHT1 0x3A
#define HOP_CHAR_MAN_RIGHT2 0x3E
#define HOP_CHAR_MAN_LOOSE 0x42
#define HOP_CHAR_MAN_LOOSE0 0x42
#define HOP_CHAR_MAN_LOOSE1 0x46
#define HOP_CHAR_MAN_LOOSE2 0x4A
#define HOP_CHAR_MONSTER 0x4E
#define HOP_CHAR_MONSTER_LEFT 0x4E
#define HOP_CHAR_MONSTER_LEFT0 0x4E
#define HOP_CHAR_MONSTER_LEFT1 0x52
#define HOP_CHAR_MONSTER_RIGHT 0x56
#define HOP_CHAR_MONSTER_RIGHT0 0x56
#define HOP_CHAR_MONSTER_RIGHT1 0x5A
#define HOP_CHAR_MONSTER_UP 0x5E
#define HOP_CHAR_LIFT 0x5E
#define HOP_CHAR_ITEM 0x62
#define HOP_CHAR_END 0x66

// -----------------------------------------------------------------------------
//   ScanKeys.h - kept for structural fidelity, though this port reads
//   isLeftPressed()/etc directly rather than a combined mask.
// -----------------------------------------------------------------------------

#define HOP_KEYS_LEFT 0x01
#define HOP_KEYS_RIGHT 0x02
#define HOP_KEYS_UP 0x04
#define HOP_KEYS_DOWN 0x08
#define HOP_KEYS_BUTTON0 0x10

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y;
    int sprite;
} HopMovable;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define HOP_STAGE_TOP 4

#define HOP_COLUMN_COUNT 96
#define HOP_FLOOR_COUNT 3

#define HOP_COLUMNS_PER_BYTE 4
#define HOP_COLUMN_WIDTH 2
#define HOP_FLOOR_HEIGHT 4

#define HOP_STAGE_WIDTH ( HOP_COLUMN_COUNT * HOP_COLUMN_WIDTH )
#define HOP_STAGE_HEIGHT ( HOP_FLOOR_COUNT * HOP_FLOOR_HEIGHT )

#define HOP_COLUMN_SHIFT 1
#define HOP_COLUMN_MASK ( HOP_COLUMN_WIDTH - 1 )
#define HOP_FLOOR_SHIFT 2
#define HOP_FLOOR_MASK ( HOP_FLOOR_HEIGHT - 1 )

#define HOP_OFFSET_HEAD 1
#define HOP_OFFSET_FOOT ( HOP_OFFSET_HEAD + 2 )

#define HOP_CELL_SPACE 0x0
#define HOP_CELL_FLOOR 0x1
#define HOP_CELL_ITEM 0x2
#define HOP_CELL_NEEDLE 0x3
#define HOP_CELL_MASK 0x03

#define HOP_STAGE_COUNT 8
#define HOP_MAX_MONSTER_COUNT 5
#define HOP_MAX_LIFT_COUNT 3

#define HOP_CELLMAP_BYTES ( ( HOP_COLUMN_COUNT / HOP_COLUMNS_PER_BYTE ) * HOP_FLOOR_COUNT )

// -----------------------------------------------------------------------------
//   Goal.h
// -----------------------------------------------------------------------------

#define HOP_GOAL_X ( HOP_STAGE_WIDTH - 4 + 1 )
#define HOP_GOAL_Y ( HOP_STAGE_TOP + HOP_FLOOR_HEIGHT * ( HOP_FLOOR_COUNT - 1 ) + 1 )

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define HOP_VVRAM_WIDTH 24
#define HOP_VVRAM_HEIGHT 16
#define HOP_WINDOW_WIDTH HOP_VVRAM_WIDTH

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define HOP_SPRITE_MAN 0
#define HOP_SPRITE_MONSTER 1
#define HOP_SPRITE_MONSTER_END 6
#define HOP_SPRITE_LIFT 6
#define HOP_SPRITE_LIFT_END 10
#define HOP_SPRITE_END 10
#define HOP_INVALID_CODE 255
#define HOP_INVALID_PATTERN 255
#define HOP_INVALID_POSITION 255

typedef struct
{
    int x, y, code;
} HopSprite;

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions matching upstream's
//   own enum exactly (not resolved to their current literal values).
// -----------------------------------------------------------------------------

#define HOP_N8 6
#define HOP_N8L 8
#define HOP_N8R 4
#define HOP_N8P ( HOP_N8 * 3 / 2 )
#define HOP_N4 ( HOP_N8 * 2 )
#define HOP_N4P ( HOP_N4 * 3 / 2 )
#define HOP_N2 ( HOP_N4 * 2 )
#define HOP_N2P ( HOP_N2 * 3 / 2 )
#define HOP_N1 ( HOP_N2 * 2 )
#define HOP_N16 ( HOP_N8 / 2 )

#define HOP_E2 1
#define HOP_F2 2
#define HOP_F2S 3
#define HOP_G2 4
#define HOP_G2S 5
#define HOP_A2 6
#define HOP_A2S 7
#define HOP_B2 8
#define HOP_C3 9
#define HOP_C3S 10
#define HOP_D3 11
#define HOP_D3S 12
#define HOP_E3 13
#define HOP_F3 14
#define HOP_F3S 15
#define HOP_G3 16
#define HOP_G3S 17
#define HOP_A3 18
#define HOP_A3S 19
#define HOP_B3 20
#define HOP_C4 21
#define HOP_C4S 22
#define HOP_D4 23
#define HOP_D4S 24
#define HOP_E4 25
#define HOP_F4 26
#define HOP_F4S 27
#define HOP_G4 28
#define HOP_G4S 29
#define HOP_A4 30
#define HOP_A4S 31
#define HOP_B4 32
#define HOP_C5 33
#define HOP_C5S 34
#define HOP_D5 35
#define HOP_D5S 36
#define HOP_E5 37
#define HOP_F5 38
#define HOP_F5S 39
#define HOP_G5 40

#define HOP_TEMPO 180

#define HOP_MELODY_NONE 0
#define HOP_MELODY_LOOSE 1
#define HOP_MELODY_HIT 2
#define HOP_MELODY_BEEP 3
#define HOP_MELODY_BONUS 4
#define HOP_MELODY_START 5
#define HOP_MELODY_CLEAR 6
#define HOP_MELODY_GAMEOVER 7
#define HOP_MELODY_BGM1 8
#define HOP_MELODY_BGM2 9

// -----------------------------------------------------------------------------
//   Data tables - extracted programmatically (a small Python script parsing
//   the real upstream source directly) rather than hand-copied, matching this
//   project's own "byte-diff transcribed tables" discipline.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph, used for
// status text (SCORE/STAGE/TIME/lives/title/GAME OVER/etc). Byte-identical
// to Cracky's own copy of this same font (same author/engine family), and
// in turn byte-identical to upstream's own real `AsciiTable`/`AsciiPattern`
// (Chars.cpp) - a genuinely reduced 27-glyph set with **no 'H' at all**.
// Upstream itself never needs one: its own Title() draws "HOPMAN" as a
// hand-authored raw-VVram bitmap logo (`TitleBytes[]`, Status.cpp), never
// through this text font. This port's own title screen (see this file's
// own header comment) deliberately replaces that bitmap logo with plain
// status-column text instead, matching Cracky's own established
// simplification for this exact game family - but doing that for THIS
// game's own name specifically hits the one letter the reduced font
// doesn't have: printing "HOPMAN" through hopPrintS()/hopAsciiIndex()
// silently mapped 'H' to the "not found" fallback (index 0 = a blank
// space), rendering the title as "OPMAN" - confirmed via a live Puppeteer
// screenshot showing exactly that missing leading letter. Fixed by
// appending a 29th... (28th, 0-indexed 27) glyph, an 'H' built the same
// two-full-verticals-plus-a-middle-crossbar shape as this font's own
// existing 'A'/'8' (whose glyphs already establish "bit for row 2 = the
// vertical-center row" for a horizontal stroke) - appended at the END of
// both the search table and this data array (not inserted alphabetically)
// so no existing character's own index shifts.
int hopAsciiPattern[112] = {
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
    0x1f, 0x04, 0x1f, 0x00,  // H (added - see comment above)
};

// CharPattern - 102 map/sprite-tile glyphs, 2 bytes/glyph (a "logo" dither
// block 0-15, "floor"+"needle" 16-17, "goal" 18-33, then Man/Monster/Item
// sprites 34-101 up to Char_End=102).
int hopCharPattern[204] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x33, 0x33, 0xfc, 0x0c, 0x1e, 0x5d, 0x1f, 0x1d,
    0x3f, 0x35, 0x1f, 0xef, 0xc7, 0xcd, 0xcf, 0xcd,
    0xcf, 0xcf, 0xcf, 0x7d, 0xe0, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0xe0, 0x0e, 0xf0, 0x0f, 0x00, 0x00,
    0x00, 0x00, 0xf0, 0x0f, 0x80, 0xf5, 0x7d, 0x08,
    0x10, 0x3c, 0xc3, 0x01, 0x00, 0xf5, 0xfd, 0x00,
    0x90, 0x34, 0x43, 0x05, 0x00, 0x75, 0x7d, 0x00,
    0x00, 0xd0, 0xc1, 0x00, 0x00, 0xf5, 0x7d, 0x00,
    0x10, 0x2d, 0x43, 0x05, 0x80, 0xd7, 0x5f, 0x08,
    0x10, 0x3c, 0xc3, 0x01, 0x00, 0xdf, 0x5f, 0x00,
    0x50, 0x34, 0x43, 0x09, 0x00, 0xd7, 0x57, 0x00,
    0x00, 0x1c, 0x0d, 0x00, 0x00, 0xd7, 0x5f, 0x00,
    0x50, 0x34, 0xd2, 0x01, 0x4c, 0xac, 0x8a, 0x44,
    0x13, 0x53, 0x15, 0x22, 0x80, 0xc3, 0x3c, 0x08,
    0x10, 0xbe, 0xaf, 0x01, 0x44, 0xa8, 0xca, 0xc8,
    0x22, 0x51, 0x35, 0x32, 0xa8, 0xaf, 0xef, 0x08,
    0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e, 0xce, 0x00,
    0x32, 0xf7, 0xff, 0x02, 0x80, 0xfe, 0xfa, 0x8a,
    0x00, 0xfb, 0x37, 0x01, 0x00, 0xec, 0xe4, 0x04,
    0x20, 0xff, 0x7f, 0x23, 0xf7, 0xff, 0xff, 0x7f,
    0x00, 0x11, 0x11, 0x00, 0x00, 0xaf, 0xa5, 0x05,
    0x80, 0x8f, 0x00, 0x00,
};

// MapToVVram()'s own CellChars[cellValue][row*ColumnWidth+col] lookup - each
// map cell is FloorHeight(4) rows x ColumnWidth(2) cols of glyph indices.
int hopCellChars[4][8] = {
    { HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE },
    { HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_FLOOR, HOP_CHAR_FLOOR },
    { HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_ITEM + 0, HOP_CHAR_ITEM + 1, HOP_CHAR_ITEM + 2, HOP_CHAR_ITEM + 3, HOP_CHAR_FLOOR, HOP_CHAR_FLOOR },
    { HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_SPACE, HOP_CHAR_NEEDLE, HOP_CHAR_NEEDLE, HOP_CHAR_FLOOR, HOP_CHAR_FLOOR },
};

// TitleBytes - upstream's own real "HOPMAN" title-screen logo bitmap
// (Status.cpp's `Title()`), 6 letters x 4x4 VVram-cell glyph indices each
// (96 values total), byte-diff-verified via a small script against the real
// upstream source. Every value here is a valid index into hopCharPattern[]'s
// own "logo" range (indices 0-15, the first 32 bytes of that table,
// confirmed byte-identical to Chars.cpp's own real `CharPattern[]` "logo"
// block) - the exact same shared dither-block palette every map tile
// already draws through, reused here to build the actual title wordmark
// instead of a wall/floor/needle tile. See hopBeginTitle()'s own comment
// for why this replaces the earlier plain-text "HOPMAN" substitute, and
// Cracky's own `crkTitleBytes` for the sibling precedent this mirrors.
int hopTitleBytes[96] = {
    0x0c, 0x03, 0x00, 0x0f, 0x0c, 0x0b, 0x0a, 0x0f,
    0x0c, 0x03, 0x00, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x08, 0x07, 0x05, 0x0b, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x03, 0x00, 0x0f, 0x00, 0x05, 0x05, 0x01,
    0x0c, 0x07, 0x05, 0x0b, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x01, 0x04, 0x01, 0x00, 0x00,
    0x0c, 0x07, 0x07, 0x0b, 0x0c, 0x03, 0x03, 0x0f,
    0x0c, 0x03, 0x03, 0x0f, 0x04, 0x01, 0x01, 0x05,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x0b, 0x00, 0x0f, 0x0c, 0x0f, 0x0b, 0x0f,
    0x0c, 0x03, 0x0d, 0x0f, 0x04, 0x01, 0x00, 0x05,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int hopFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int hopMelodyLoose[3] = {
    1, 18, 0,
};

int hopMelodyHit[17] = {
    1, 26, 1, 28, 1, 30, 1, 32, 1, 33, 1, 35, 1,
    37, 1, 38, 0,
};

int hopMelodyBeep[3] = {
    1, 30, 0,
};

int hopMelodyBonus[13] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30, 1, 33, 0,
};

int hopMelodyStart[23] = {
    12, 21, 12, 25, 6, 28, 12, 25, 12, 26, 6, 26, 12,
    30, 6, 33, 18, 30, 36, 33, 12, 0, 0,
};

int hopMelodyClear[29] = {
    6, 30, 6, 30, 6, 28, 6, 26, 6, 28, 12, 30, 12,
    32, 6, 32, 6, 30, 6, 28, 6, 30, 12, 32, 30, 33,
    24, 0, 0,
};

int hopMelodyGameOver[21] = {
    6, 33, 6, 26, 6, 30, 6, 25, 6, 28, 6, 30, 6,
    32, 6, 33, 36, 33, 12, 0, 0,
};

int hopMelodyBgm1[119] = {
    12, 21, 12, 28, 6, 21, 12, 28, 12, 30, 6, 30, 6,
    28, 6, 28, 6, 26, 6, 26, 6, 25, 6, 25, 12, 23,
    12, 23, 6, 23, 12, 25, 18, 23, 36, 0, 12, 21, 12,
    28, 6, 21, 12, 28, 12, 30, 6, 30, 6, 28, 6, 28,
    6, 26, 6, 26, 6, 25, 6, 25, 12, 26, 12, 26, 6,
    26, 12, 30, 18, 28, 36, 0, 6, 25, 6, 25, 6, 25,
    12, 25, 6, 25, 12, 30, 6, 23, 6, 23, 6, 23, 12,
    23, 6, 23, 12, 28, 6, 0, 6, 30, 6, 0, 6, 28,
    6, 0, 6, 26, 6, 0, 6, 25, 12, 23, 12, 25, 24,
    21, 255,
};

int hopMelodyBgm2[133] = {
    6, 21, 12, 0, 18, 25, 6, 28, 6, 0, 6, 18, 12,
    0, 18, 21, 6, 25, 6, 0, 6, 23, 12, 0, 18, 26,
    6, 18, 6, 0, 6, 16, 12, 0, 18, 20, 6, 23, 6,
    0, 6, 21, 12, 0, 18, 25, 6, 28, 6, 0, 6, 18,
    12, 0, 18, 21, 6, 25, 6, 0, 6, 26, 12, 0, 6,
    26, 6, 16, 12, 0, 6, 16, 6, 21, 12, 0, 18, 25,
    6, 28, 6, 0, 6, 21, 12, 0, 6, 21, 6, 18, 12,
    0, 6, 18, 6, 23, 12, 0, 6, 23, 6, 16, 12, 0,
    6, 16, 6, 0, 6, 14, 6, 0, 6, 14, 6, 0, 6,
    16, 6, 0, 6, 16, 6, 21, 12, 0, 18, 25, 6, 28,
    6, 0, 255,
};

// Stage data - flattened from upstream's own `struct Stage { itemCount,
// monsterCount, pMonsters, liftCount, pLifts, bytes[72] }` array (plus its
// separate MonstersN/LiftsN tables) into parallel fixed arrays, matching
// this project's own "flatten a struct-with-a-real-pointer-member into
// plain arrays" precedent (e.g. Cracky's own crkStageStart/crkStageEnemies).
int hopStageItemCount[8] = { 7, 9, 12, 10, 21, 18, 17, 20 };
int hopStageMonsterCount[8] = { 4, 3, 3, 2, 2, 5, 4, 4 };
int hopStageLiftCount[8] = { 2, 3, 3, 3, 3, 2, 3, 0 };

// Each stage's own monster start bytes, {floor, leftColumn, rightColumn}
// triples, up to HOP_MAX_MONSTER_COUNT(5) per stage (zero-padded).
int hopStageMonsters[8][15] = {
    { 1, 28, 38, 2, 84, 94, 0, 23, 31, 1, 85, 92, 0, 0, 0 },
    { 1, 76, 83, 2, 56, 64, 1, 26, 32, 0, 0, 0, 0, 0, 0 },
    { 1, 45, 52, 0, 41, 48, 2, 89, 92, 0, 0, 0, 0, 0, 0 },
    { 1, 35, 50, 1, 76, 81, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 2, 84, 91, 1, 44, 47, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 2, 11, 22, 1, 68, 74, 2, 50, 57, 1, 34, 38, 1, 58, 62 },
    { 1, 75, 80, 2, 40, 44, 2, 58, 62, 1, 17, 20, 0, 0, 0 },
    { 2, 25, 32, 2, 53, 59, 2, 45, 52, 1, 20, 23, 0, 0, 0 },
};

// Each stage's own lift start bytes, {b, column} pairs (b packs
// direction(bit7)|length(bits3-6)|floor(bits0-1)), up to
// HOP_MAX_LIFT_COUNT(3) per stage (zero-padded).
int hopStageLifts[8][6] = {
    { 144, 10, 49, 65, 0, 0 },
    { 144, 89, 144, 2, 57, 45 },
    { 144, 71, 50, 2, 137, 29 },
    { 49, 83, 144, 20, 144, 64 },
    { 41, 62, 57, 28, 66, 75 },
    { 144, 23, 144, 39, 0, 0 },
    { 33, 65, 33, 10, 144, 32 },
    { 0, 0, 0, 0, 0, 0 },
};

int hopStageBytes[8][72] = {
    {
        0x00, 0x00, 0x40, 0x55, 0x56, 0x41, 0x55, 0x55, 0x02, 0x50, 0x55, 0x15,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x60, 0x00, 0x00, 0x40, 0x55, 0x15, 0x00, 0x95, 0x55, 0x15, 0x50, 0x15,
        0x54, 0x41, 0x56, 0x41, 0x01, 0x00, 0x55, 0x01, 0x40, 0x57, 0x55, 0x01,
        0x55, 0x75, 0x45, 0x55, 0x05, 0x55, 0x54, 0x01, 0x00, 0x00, 0x59, 0x55,
        0x55, 0x55, 0x95, 0x15, 0x50, 0x15, 0x00, 0x54, 0x15, 0x55, 0x55, 0x55,
    },
    {
        0x40, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x16, 0x00, 0x00,
        0x00, 0x54, 0x59, 0x25, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x40, 0x75, 0x55, 0x56, 0x50, 0x55, 0xc5, 0x55, 0x19, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x00, 0x55, 0x55, 0x00, 0x40, 0x05,
        0x05, 0x40, 0x55, 0x95, 0x55, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x55, 0x64, 0x95, 0x55, 0x55, 0x01, 0x50, 0x00, 0x55, 0x55, 0x51, 0x55,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x65, 0x55, 0x05, 0x00, 0x54, 0x55,
        0x01, 0x54, 0x59, 0x00, 0x00, 0x00, 0x95, 0x55, 0x01, 0x50, 0x55, 0x09,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x50, 0x55, 0x65, 0x01, 0x54,
        0x55, 0x0d, 0x00, 0x64, 0x55, 0x16, 0x01, 0x00, 0x50, 0x94, 0x05, 0x00,
        0x05, 0x00, 0x54, 0x15, 0x77, 0x95, 0x15, 0x50, 0x00, 0x00, 0x14, 0x54,
        0x55, 0x65, 0x55, 0x55, 0x55, 0x15, 0x54, 0x95, 0xd9, 0x55, 0x55, 0x55,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x56, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x54, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x74, 0x47, 0xd7, 0x05, 0x00, 0x00, 0x00, 0x40, 0x65, 0x55, 0x55,
        0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x55, 0x16, 0x00, 0x50, 0x0d,
        0x75, 0x57, 0x95, 0x00, 0x00, 0x94, 0x55, 0x55, 0x55, 0x55, 0xde, 0x01,
        0xf0, 0x97, 0x55, 0x59, 0x94, 0x15, 0x00, 0x00, 0x00, 0x11, 0x01, 0x50,
    },
    {
        0x76, 0x01, 0x50, 0x97, 0x06, 0x54, 0x59, 0x0c, 0x00, 0x00, 0x00, 0x50,
        0x95, 0x01, 0x54, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0xd4, 0x15, 0x00, 0x00, 0x00, 0x55, 0x01, 0x55,
        0x17, 0x44, 0x5d, 0x05, 0x00, 0x82, 0x20, 0x08, 0x82, 0x00, 0x00, 0x00,
        0x55, 0xd4, 0x95, 0x19, 0x8a, 0xd5, 0x35, 0x71, 0x1f, 0x00, 0x74, 0x94,
        0x55, 0x17, 0xc0, 0xd6, 0x01, 0x00, 0x10, 0x00, 0x00, 0x65, 0x65, 0x50,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x65, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x06, 0x0c, 0x00, 0x00, 0x00,
        0x40, 0x57, 0x51, 0x95, 0x03, 0x00, 0x90, 0x24, 0x90, 0x15, 0x00, 0x48,
        0x2d, 0x00, 0x51, 0x16, 0x14, 0x65, 0x55, 0x55, 0x56, 0x50, 0x55, 0x0d,
        0x25, 0x00, 0x75, 0x55, 0x56, 0x15, 0x00, 0x00, 0x56, 0x15, 0x00, 0x7f,
        0x55, 0x55, 0x15, 0x40, 0xbd, 0x5d, 0xd7, 0x59, 0x55, 0x84, 0x55, 0x55,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x16, 0x00, 0x58, 0x00,
        0x50, 0x95, 0x05, 0x40, 0x94, 0x01, 0x00, 0x00, 0x54, 0x79, 0x00, 0x40,
        0x25, 0x05, 0xd0, 0x65, 0x01, 0x50, 0x7e, 0x59, 0x01, 0xc0, 0x19, 0x03,
        0x55, 0x02, 0x20, 0x40, 0x66, 0x45, 0x75, 0x59, 0x00, 0x00, 0x55, 0x11,
        0x00, 0x40, 0x5d, 0x15, 0x05, 0x00, 0x00, 0x00, 0xb0, 0x5d, 0x59, 0x57,
    },
    {
        0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x24, 0x49, 0x92, 0x24, 0x00,
        0x00, 0x00, 0x59, 0x00, 0x05, 0x55, 0x00, 0x00, 0xf5, 0x12, 0x00, 0x09,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x59, 0x95, 0x55, 0x00, 0x05,
        0x51, 0x19, 0xc0, 0xd5, 0x00, 0x00, 0x54, 0x55, 0x01, 0x80, 0x24, 0x54,
        0x56, 0x55, 0x55, 0x75, 0x64, 0x55, 0x95, 0x44, 0x65, 0x51, 0x05, 0x5f,
    },
};

// -----------------------------------------------------------------------------
//   Struct declarations: Man/Monster/Lift
// -----------------------------------------------------------------------------

#define HOP_MAN_LIVE 0x80
#define HOP_MAN_JUMP 0x40

typedef struct
{
    HopMovable pos;
    int dx, dy;
    int pattern;
    int status;
    int clock;
} HopMan;

typedef struct
{
    HopMovable pos;
    int left, right;
    int dx;
    int pattern;
} HopMonster;

typedef struct
{
    HopMovable pos;
    int leftTop, rightBottom;
    int dx, dy;
} HopLift;

#define HOP_HIT_RANGE_X 1
#define HOP_HIT_RANGE_Y 1

#define HOP_MAN_BOTTOM ( HOP_FLOOR_COUNT * HOP_FLOOR_HEIGHT + HOP_STAGE_TOP - 3 )

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int hopScore;
int hopRemainCount;
int hopCurrentStage;
int hopStageTime;
int hopItemCount;
int hopMonsterNum;
int hopTimeDenom;
int hopStageIndex;

#define HOP_MAX_TIME_DENOM ( 60 / 8 )
#define HOP_BONUS_RATE 5

int hopVVram[HOP_VVRAM_HEIGHT][HOP_VVRAM_WIDTH];
int hopCellMap[HOP_CELLMAP_BYTES];
int hopLeftX;
bool hopCleared;

HopSprite hopSprites[HOP_SPRITE_END];

HopMan hopMan;
HopMonster hopMonsters[HOP_MAX_MONSTER_COUNT];
HopLift hopLifts[HOP_MAX_LIFT_COUNT];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into hopAsciiPattern (0 = space) per cell.
//
// **Widened from an original, wrong `[8][8]` - see this file's own header
// comment for the full writeup.** The original design only ever modeled
// upstream's own status-label columns (SCORE/STAGE/TIME/lives, which
// upstream's `LeftX=24` constant genuinely does confine to columns 24-31,
// 8 cells) - but the title screen's own text (the logo, "MINI", "START"/
// "CONTINUE", the credit line) lives at upstream's real columns 8-23, well
// to the LEFT of the status zone, using the exact same shared
// PrintC()/PrintS() mechanism at different column arguments - not a
// separate, narrower grid at all. See `hopComposeRawByte()`'s own header
// for how this wider grid actually reaches the screen.
int hopStatusChar[8][32];

// Set true only while on the title screen (HOP_STATE_TITLE) - matches
// Cracky's own `crkFullWidthText` exactly (same author/engine family, same
// bug, same fix). When true, hopComposeRawByte() reads hopStatusChar
// across the full 0-31 char-cell width instead of just the narrow
// status-only columns 24-31, letting the title screen use that same wide
// real estate instead of being artificially confined to the status zone.
bool hopFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintTimeUp()/PrintGameOver()/PrintPerfect() Vram-direct writes -
// see this file's own header comment.
bool hopOverlayActive;
int hopOverlayText[10];
int hopOverlayLen;
int hopOverlayPage;
int hopOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of HOP_TICK_DIVISOR.
int hopSeqMelody[3];
int hopSeqPos[3];
int hopSeqWait[3];
int hopSeqActive[3];

#define HOP_TICK_DIVISOR 10
int hopTickCounter;
bool hopLiftPhase;

#define HOP_STATE_TITLE 0
#define HOP_STATE_START_JINGLE 1
#define HOP_STATE_PLAYING 2
#define HOP_STATE_LOSE_ANIM 3
#define HOP_STATE_GAMEOVER_JINGLE 4
#define HOP_STATE_CLEAR_WAIT 5
#define HOP_STATE_CLEAR_JINGLE 6
#define HOP_STATE_BONUS_TALLY 7
#define HOP_STATE_PERFECT_TALLY 8
int hopState;
int hopWaitFrames;
int hopAnimStep;
int hopSelection;
bool hopSelectionChanged;
int hopPrevLeft;
int hopPrevRight;
int hopPrevUp;
int hopPrevDown;
int hopPrevFire;
bool hopPendingContinue;
int hopPerfectRemaining;


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int hopXToColumn( int x )
{
    return x >> HOP_COLUMN_SHIFT;
}

int hopYToFloor( int y )
{
    if( y < HOP_STAGE_TOP ) return 0;
    return ( y - HOP_STAGE_TOP ) >> HOP_FLOOR_SHIFT;
}

int hopFloorToY( int floor )
{
    return ( floor << HOP_FLOOR_SHIFT ) + HOP_STAGE_TOP;
}

int hopColumnToX( int column )
{
    return column << HOP_COLUMN_SHIFT;
}

int hopCellMapPtr( int column, int floor )
{
    return ( floor * HOP_COLUMN_COUNT + column ) / HOP_COLUMNS_PER_BYTE;
}

int hopGetCell( int column, int floor )
{
    int idx, b, shift;
    if( floor >= HOP_FLOOR_COUNT ) return HOP_CELL_SPACE;
    idx = hopCellMapPtr( column, floor );
    if( idx < 0 || idx >= HOP_CELLMAP_BYTES ) return HOP_CELL_SPACE;
    b = hopCellMap[ idx ];
    shift = ( column << 1 ) & 6;
    return ( b >> shift ) & HOP_CELL_MASK;
}

void hopSetCell( int column, int floor, int cell )
{
    int idx, shift, b, mask;
    idx = hopCellMapPtr( column, floor );
    if( idx < 0 || idx >= HOP_CELLMAP_BYTES ) return;
    b = hopCellMap[ idx ];
    shift = ( column << 1 ) & 6;
    mask = ~( HOP_CELL_MASK << shift );
    b = ( b & mask ) | ( ( cell & HOP_CELL_MASK ) << shift );
    hopCellMap[ idx ] = b;
}

void hopScroll()
{
    int newLeft;
    if( hopMan.pos.x < HOP_WINDOW_WIDTH / 2 - 1 )
      newLeft = 0;
    else
    {
        newLeft = hopMan.pos.x - ( HOP_WINDOW_WIDTH / 2 - 1 );
        if( newLeft >= HOP_STAGE_WIDTH - HOP_WINDOW_WIDTH )
          newLeft = HOP_STAGE_WIDTH - HOP_WINDOW_WIDTH;
    }
    hopLeftX = newLeft;
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

int hopGetCellAtFloor( HopMovable* pMovable, int floor )
{
    int column, left, right;
    if( floor >= HOP_FLOOR_COUNT ) return HOP_CELL_SPACE;
    column = hopXToColumn( pMovable->x );
    if( ( pMovable->x & 1 ) != 0 )
    {
        left = hopGetCell( column, floor );
        right = hopGetCell( column + 1, floor );
        if( left == HOP_CELL_NEEDLE || right == HOP_CELL_NEEDLE ) return HOP_CELL_NEEDLE;
        if( left == HOP_CELL_ITEM || right == HOP_CELL_ITEM ) return HOP_CELL_ITEM;
        return left | right;
    }
    return hopGetCell( column, floor );
}

int hopGetCellAt( HopMovable* pMovable )
{
    int y;
    y = pMovable->y;
    if( y < HOP_STAGE_TOP ) return HOP_CELL_SPACE;
    if( ( ( y - HOP_STAGE_TOP ) & HOP_FLOOR_MASK ) != HOP_OFFSET_HEAD ) return HOP_CELL_SPACE;
    return hopGetCellAtFloor( pMovable, hopYToFloor( pMovable->y ) );
}

bool hopIsFloor( HopMovable* pMovable, int floor )
{
    return hopGetCellAtFloor( pMovable, floor ) != HOP_CELL_SPACE;
}

bool hopIsOnFloor( HopMovable* pMovable )
{
    return hopGetCellAt( pMovable ) != HOP_CELL_SPACE;
}

bool hopIsOnNeedle( HopMovable* pMovable )
{
    return hopGetCellAt( pMovable ) == HOP_CELL_NEEDLE;
}

bool hopCanMoveX( HopMovable* pMovable, int dx )
{
    int newX, y, column;
    newX = pMovable->x + dx;
    if( newX < 0 ) return false;
    if( newX >= HOP_STAGE_WIDTH - 1 ) return false;
    y = pMovable->y;
    if( y < HOP_STAGE_TOP ) return true;
    y = y - HOP_STAGE_TOP;
    if( ( y & HOP_FLOOR_MASK ) < 1 ) return true;
    if( ( newX & 1 ) != 0 && dx > 0 ) newX = newX + 1;
    column = hopXToColumn( newX );
    return hopGetCell( column, y >> HOP_FLOOR_SHIFT ) != HOP_CELL_NEEDLE;
}

bool hopMoveX( HopMovable* pMovable, int dx )
{
    pMovable->x = pMovable->x + dx;
    return true;
}

bool hopIsNearX( int x1, int x2 )
{
    return x1 + HOP_HIT_RANGE_X >= x2 && x2 + HOP_HIT_RANGE_X >= x1;
}

bool hopIsNear( HopMovable* p1, HopMovable* p2 )
{
    return
        p1->x + HOP_HIT_RANGE_X >= p2->x && p2->x + HOP_HIT_RANGE_X >= p1->x &&
        p1->y + HOP_HIT_RANGE_Y >= p2->y && p2->y + HOP_HIT_RANGE_Y >= p1->y;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void hopHideAllSprites()
{
    int i;
    for( i = 0; i < HOP_SPRITE_END; i = i + 1 )
      hopSprites[ i ].code = HOP_INVALID_CODE;
}

void hopShowSprite( HopMovable* pMovable, int code )
{
    int idx, x, y;
    idx = pMovable->sprite;
    x = pMovable->x - hopLeftX;
    y = pMovable->y;
    if( x >= 0 && x < HOP_VVRAM_WIDTH && y >= 0 && y < HOP_VVRAM_HEIGHT )
    {
        hopSprites[ idx ].x = x;
        hopSprites[ idx ].y = y;
        hopSprites[ idx ].code = code;
    }
    else
      hopSprites[ idx ].code = HOP_INVALID_CODE;
}

void hopDrawSprites()
{
    int i, x, y, c, row, col;
    for( i = 0; i < HOP_SPRITE_END; i = i + 1 )
    {
        if( hopSprites[ i ].code != HOP_INVALID_CODE )
        {
            x = hopSprites[ i ].x;
            y = hopSprites[ i ].y;
            c = hopSprites[ i ].code;
            for( row = 0; row < 2; row = row + 1 )
            {
                if( y < HOP_VVRAM_HEIGHT )
                {
                    for( col = 0; col < 2; col = col + 1 )
                    {
                        if( c != 0 && ( x + col ) < HOP_VVRAM_WIDTH )
                          hopVVram[ y ][ x + col ] = c;
                        c = c + 1;
                    }
                }
                else
                  c = c + 2;
                y = y + 1;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Goal.cpp
// -----------------------------------------------------------------------------

void hopDrawGoal()
{
    int x, row, col, c;
    x = HOP_GOAL_X - 1 - hopLeftX;
    if( x < HOP_VVRAM_WIDTH )
    {
        for( row = 0; row < 4; row = row + 1 )
        {
            for( col = 0; col < 4; col = col + 1 )
            {
                c = HOP_CHAR_GOAL + row * 4 + col;
                if( ( x + col ) >= 0 && ( x + col ) < HOP_VVRAM_WIDTH )
                  hopVVram[ HOP_GOAL_Y - 2 + row ][ x + col ] = c;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into hopStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area).
// -----------------------------------------------------------------------------

int hopAsciiIndex( int c )
{
    // 'H' appended at the end (index 27) rather than inserted alphabetically
    // - see hopAsciiPattern's own header comment for why (this game's own
    // name, "HOPMAN", needs it and upstream's real reduced font never had
    // one to begin with).
    int table[28] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'P', 'R', 'S', 'T', 'U', 'V',
        'H',
    };
    int i;
    for( i = 0; i < 28; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

int hopPrintC( int page, int col, int c )
{
    hopStatusChar[ page ][ col ] = hopAsciiIndex( c );
    return col + 1;
}

int hopPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = hopPrintC( page, col, s[ i ] );
    return col;
}

void hopPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      hopPrintC( page, col, ' ' );
    else
      hopPrintC( page, col, d1 + '0' );
    hopPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void hopPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        hopPrintC( page, col, ' ' );
        if( d2 == 0 )
          hopPrintC( page, col + 1, ' ' );
        else
          hopPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        hopPrintC( page, col, d1 + '0' );
        hopPrintC( page, col + 1, d2 + '0' );
    }
    hopPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void hopPrintNumber5( int page, int col, int w )
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
          hopPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            hopPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    hopPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// hopStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void hopPrintScore()
{
    hopPrintNumber5( 1, 24 + 2, hopScore );
    hopPrintC( 1, 24 + 2 + 5, '0' );
}

void hopPrintTime()
{
    hopPrintByteNumber3( 5, 24 + 5, hopStageTime );
}

void hopPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    hopPrintS( 0, 24, sScore, 5 );
    hopPrintS( 3, 24, sStage, 5 );
    hopPrintByteNumber2( 3, 24 + 6, hopCurrentStage + 1 );
    hopPrintS( 5, 24, sTime, 4 );

    // upstream draws a real 2x2 Char_Remain icon (Put2C) here, then a space
    // and a digit - simplified to plain text digits throughout, matching
    // Cracky's own identical precedent for the exact same situation (see
    // this file's own header comment).
    if( hopRemainCount > 1 )
    {
        i = hopRemainCount - 1;
        if( i > 2 )
        {
            hopPrintC( 7, 24, ' ' );
            hopPrintC( 7, 24 + 1, ' ' );
            hopPrintC( 7, 24 + 2, i + '0' );
        }
        else
        {
            for( i = 0; i < hopRemainCount - 1; i = i + 1 )
              hopPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    hopPrintScore();
    hopPrintTime();
}

void hopBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    hopOverlayActive = true;
    hopOverlayLen = len;
    hopOverlayPage = page;
    hopOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      hopOverlayText[ i ] = s[ i ];
}

void hopPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    hopBeginOverlay( s, 9, 4, 8 );
}

void hopPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    hopBeginOverlay( s, 7, 4, 9 );
}

void hopPrintPerfect()
{
    int s[7] = { 'P', 'E', 'R', 'F', 'E', 'C', 'T' };
    hopBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int hopMelodyValue( int id, int idx )
{
    if( id == HOP_MELODY_LOOSE ) return hopMelodyLoose[ idx ];
    if( id == HOP_MELODY_HIT ) return hopMelodyHit[ idx ];
    if( id == HOP_MELODY_BEEP ) return hopMelodyBeep[ idx ];
    if( id == HOP_MELODY_BONUS ) return hopMelodyBonus[ idx ];
    if( id == HOP_MELODY_START ) return hopMelodyStart[ idx ];
    if( id == HOP_MELODY_CLEAR ) return hopMelodyClear[ idx ];
    if( id == HOP_MELODY_GAMEOVER ) return hopMelodyGameOver[ idx ];
    if( id == HOP_MELODY_BGM1 ) return hopMelodyBgm1[ idx ];
    if( id == HOP_MELODY_BGM2 ) return hopMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/HOP_TEMPO = 1.6667 real 60Hz ticks - see this file's own header.
int hopNoteFrames( int length )
{
    return (int)( length * ( 300.0 / 180.0 ) + 0.5 );
}

void hopStartSeq( int channel, int melodyId )
{
    hopSeqMelody[ channel ] = melodyId;
    hopSeqPos[ channel ] = 0;
    hopSeqWait[ channel ] = 0;
    hopSeqActive[ channel ] = 1;
}

void hopStopSeq( int channel )
{
    hopSeqActive[ channel ] = 0;
    hopSeqMelody[ channel ] = HOP_MELODY_NONE;
}

bool hopSeqPlaying( int channel )
{
    return hopSeqActive[ channel ] != 0;
}

void hopAdvanceOneSeq( int channel )
{
    int length, note;

    if( hopSeqActive[ channel ] == 0 ) return;

    if( hopSeqWait[ channel ] > 0 )
    {
        hopSeqWait[ channel ] = hopSeqWait[ channel ] - 1;
        return;
    }

    length = hopMelodyValue( hopSeqMelody[ channel ], hopSeqPos[ channel ] );
    if( length == 0 )
    {
        hopStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        hopSeqPos[ channel ] = 0;
        length = hopMelodyValue( hopSeqMelody[ channel ], 0 );
    }
    note = hopMelodyValue( hopSeqMelody[ channel ], hopSeqPos[ channel ] + 1 );
    hopSeqPos[ channel ] = hopSeqPos[ channel ] + 2;
    hopSeqWait[ channel ] = hopNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)hopFrequencies[ note - 1 ], (float)hopSeqWait[ channel ] / 60.0 );
}

void hopAdvanceSound()
{
    hopAdvanceOneSeq( 0 );
    hopAdvanceOneSeq( 1 );
    hopAdvanceOneSeq( 2 );
}

void hopStartBgm()
{
    hopStartSeq( 1, HOP_MELODY_BGM1 );
    hopStartSeq( 2, HOP_MELODY_BGM2 );
}

void hopStopBgm()
{
    hopStopSeq( 1 );
    hopStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void hopAddScore( int pts )
{
    hopScore = hopScore + pts;
    hopPrintScore();
}


// -----------------------------------------------------------------------------
//   Man.cpp (part 1) - just the sprite-facing helper, needed by Lift.cpp below.
// -----------------------------------------------------------------------------

void hopShowManSprite()
{
    int pattern, seq;
    pattern = hopMan.pattern;
    if( ( hopMan.status & HOP_MAN_JUMP ) != 0 )
      pattern = pattern + 4;
    else if( hopMan.dx != 0 )
    {
        seq = hopMan.pos.x & 3;
        if( seq == 3 ) seq = 1;
        pattern = pattern + ( ( seq + 1 ) << 2 );
    }
    hopShowSprite( &hopMan.pos, pattern );
}


// -----------------------------------------------------------------------------
//   Lift.cpp
// -----------------------------------------------------------------------------

void hopShowLift( int idx )
{
    hopShowSprite( &hopLifts[ idx ].pos, HOP_CHAR_LIFT );
}

void hopInitLifts()
{
    int i, n, sprite, b, direction, column, floor, length;
    n = hopStageLiftCount[ hopStageIndex ];
    sprite = HOP_SPRITE_LIFT;
    for( i = 0; i < n; i = i + 1 )
    {
        b = hopStageLifts[ hopStageIndex ][ i * 2 + 0 ];
        column = hopStageLifts[ hopStageIndex ][ i * 2 + 1 ];
        hopLifts[ i ].pos.x = hopColumnToX( column );
        floor = b & 0x03;
        hopLifts[ i ].pos.y = hopFloorToY( floor ) + HOP_OFFSET_FOOT;
        direction = b & 0x80;
        length = ( b & 0x78 ) >> 3;
        if( direction == 0 )
        {
            hopLifts[ i ].leftTop = hopLifts[ i ].pos.x;
            hopLifts[ i ].rightBottom = hopColumnToX( column + length );
            hopLifts[ i ].dx = 1;
            hopLifts[ i ].dy = 0;
        }
        else
        {
            hopLifts[ i ].leftTop = hopLifts[ i ].pos.y;
            hopLifts[ i ].rightBottom = hopLifts[ i ].pos.y + ( length << HOP_FLOOR_SHIFT );
            hopLifts[ i ].dx = 0;
            hopLifts[ i ].dy = 1;
        }
        hopLifts[ i ].pos.sprite = sprite;
        hopShowLift( i );
        sprite = sprite + 1;
    }
    for( i = n; i < HOP_MAX_LIFT_COUNT; i = i + 1 )
      hopLifts[ i ].leftTop = HOP_INVALID_POSITION;
}

bool hopIsManOnLift( int x, int y )
{
    int bottom;
    if( hopIsNearX( hopMan.pos.x, x ) )
    {
        bottom = hopMan.pos.y + 2;
        return bottom == y;
    }
    return false;
}

bool hopMoveManOnLift( HopMovable* pLift, int dx, int oldY )
{
    if( hopIsManOnLift( pLift->x, oldY ) )
    {
        hopMan.pos.y = pLift->y - 2;
        hopMoveX( &hopMan.pos, dx );
        if( dx != 0 )
          hopScroll();
        hopShowManSprite();
        return true;
    }
    return false;
}

bool hopIsManOnAnyLift()
{
    int i;
    for( i = 0; i < HOP_MAX_LIFT_COUNT; i = i + 1 )
    {
        if( hopLifts[ i ].leftTop != HOP_INVALID_POSITION )
        {
            if( hopIsManOnLift( hopLifts[ i ].pos.x, hopLifts[ i ].pos.y ) )
              return true;
        }
    }
    return false;
}

int hopFindLift( HopMovable* pMovable, int nextY )
{
    int i, top;
    for( i = 0; i < HOP_MAX_LIFT_COUNT; i = i + 1 )
    {
        if( hopLifts[ i ].leftTop != HOP_INVALID_POSITION )
        {
            if( hopIsNearX( hopLifts[ i ].pos.x, pMovable->x ) )
            {
                top = hopLifts[ i ].pos.y - 2;
                if( top >= pMovable->y && nextY >= top )
                  return i;
            }
        }
    }
    return -1;
}

void hopMoveLifts()
{
    int i, oldY, oldDx;
    bool scroll;
    scroll = false;
    for( i = 0; i < HOP_MAX_LIFT_COUNT; i = i + 1 )
    {
        if( hopLifts[ i ].leftTop != HOP_INVALID_POSITION )
        {
            oldY = hopLifts[ i ].pos.y;
            oldDx = hopLifts[ i ].dx;
            if( hopLifts[ i ].dx == 0 )
            {
                if( hopLifts[ i ].dy > 0 )
                {
                    hopLifts[ i ].pos.y = hopLifts[ i ].pos.y + 1;
                    if( hopLifts[ i ].pos.y == hopLifts[ i ].rightBottom )
                      hopLifts[ i ].dy = -1;
                }
                else
                {
                    hopLifts[ i ].pos.y = hopLifts[ i ].pos.y - 1;
                    if( hopLifts[ i ].pos.y == hopLifts[ i ].leftTop )
                      hopLifts[ i ].dy = 1;
                }
            }
            else
            {
                if( hopLifts[ i ].dx > 0 )
                {
                    hopMoveX( &hopLifts[ i ].pos, hopLifts[ i ].dx );
                    if( hopLifts[ i ].pos.x == hopLifts[ i ].rightBottom )
                      hopLifts[ i ].dx = -1;
                }
                else
                {
                    hopMoveX( &hopLifts[ i ].pos, hopLifts[ i ].dx );
                    if( hopLifts[ i ].pos.x == hopLifts[ i ].leftTop )
                      hopLifts[ i ].dx = 1;
                }
            }
            if( hopMoveManOnLift( &hopLifts[ i ].pos, oldDx, oldY ) )
            {
                hopMan.dy = 0;
                hopMan.status = hopMan.status & ~HOP_MAN_JUMP;
                scroll = true;
            }
            hopShowLift( i );
        }
    }
    if( scroll )
      hopScroll();
}


// -----------------------------------------------------------------------------
//   Man.cpp (part 2)
// -----------------------------------------------------------------------------

bool hopIsManOn()
{
    return hopIsOnFloor( &hopMan.pos ) || hopIsManOnAnyLift();
}

void hopInitMan()
{
    hopMan.pos.sprite = HOP_SPRITE_MAN;
    hopMan.pos.x = 0;
    hopMan.pos.y = HOP_MAN_BOTTOM;
    hopMan.dx = 0;
    hopMan.dy = 0;
    hopMan.pattern = HOP_CHAR_MAN;
    hopMan.status = HOP_MAN_LIVE;
    hopMan.clock = 0;
    hopShowManSprite();
}

void hopMoveMan()
{
    int oldY;
    int dx, pattern;
    bool left, right, moved;

    oldY = hopMan.pos.y;
    hopMan.dx = 0;
    dx = 0;
    pattern = hopMan.pattern;
    moved = false;

    left = isLeftPressed();
    right = isRightPressed();

    if( isFirePressed() && hopMan.dy == 0 && hopIsManOn() )
    {
        hopMan.dy = -3;
        hopMan.clock = 0;
        hopMan.status = hopMan.status | HOP_MAN_JUMP;
    }

    if( left )
    {
        dx = -1; pattern = HOP_CHAR_MAN_LEFT; moved = true;
    }
    else if( right )
    {
        dx = 1; pattern = HOP_CHAR_MAN_RIGHT; moved = true;
    }

    if( moved && hopCanMoveX( &hopMan.pos, dx ) )
    {
        hopMan.dx = dx;
        hopMan.pattern = pattern;
    }

    if( hopMan.dx != 0 )
    {
        hopMoveX( &hopMan.pos, hopMan.dx );
        hopScroll();
    }

    if( hopMan.dy > 0 )
    {
        int nextY, floor, nextFloor, liftIdx;
        nextY = hopMan.pos.y + hopMan.dy;
        liftIdx = hopFindLift( &hopMan.pos, nextY );
        if( liftIdx >= 0 )
        {
            nextY = hopLifts[ liftIdx ].pos.y - 2;
            hopMan.dy = 0;
            hopMan.status = hopMan.status & ~HOP_MAN_JUMP;
        }
        else
        {
            floor = hopYToFloor( hopMan.pos.y + ( HOP_FLOOR_HEIGHT - HOP_OFFSET_HEAD ) );
            nextFloor = hopYToFloor( nextY + ( HOP_FLOOR_HEIGHT - HOP_OFFSET_HEAD ) );
            if( nextFloor > floor && hopIsFloor( &hopMan.pos, floor ) )
            {
                nextY = hopFloorToY( floor ) + HOP_OFFSET_HEAD;
                hopMan.dy = 0;
                hopMan.status = hopMan.status & ~HOP_MAN_JUMP;
            }
        }
        hopMan.pos.y = nextY;
    }
    else if( hopMan.dy < 0 )
    {
        int floor, nextY, nextFloor;
        floor = hopYToFloor( hopMan.pos.y + HOP_OFFSET_HEAD );
        nextY = hopMan.pos.y + hopMan.dy;
        nextFloor = hopYToFloor( nextY + HOP_OFFSET_HEAD );
        if( nextFloor < floor && hopIsFloor( &hopMan.pos, nextFloor ) )
        {
            hopMan.dy = 0;
            hopMan.status = hopMan.status & ~HOP_MAN_JUMP;
        }
        hopMan.pos.y = nextY;
    }

    hopShowManSprite();

    if( hopMan.pos.y > oldY && ( hopYToFloor( hopMan.pos.y ) == HOP_FLOOR_COUNT || hopIsOnNeedle( &hopMan.pos ) ) )
      hopMan.status = hopMan.status & ~HOP_MAN_LIVE;

    hopMan.clock = hopMan.clock + 1;
    if( !hopIsManOn() || hopMan.dy != 0 )
    {
        hopMan.dy = hopMan.dy + 1;
        hopMan.status = hopMan.status | HOP_MAN_JUMP;
    }

    if( ( hopMan.pos.x & HOP_COLUMN_MASK ) == 0 )
    {
        if( hopGetCellAt( &hopMan.pos ) == HOP_CELL_ITEM )
        {
            int column, floor;
            column = hopXToColumn( hopMan.pos.x );
            floor = hopYToFloor( hopMan.pos.y );
            hopSetCell( column, floor, HOP_CELL_FLOOR );
            hopAddScore( 10 );
            hopStartSeq( 0, HOP_MELODY_HIT );
            hopItemCount = hopItemCount - 1;
        }
    }

    if( hopMan.pos.x == HOP_GOAL_X && hopMan.pos.y == HOP_GOAL_Y )
      hopCleared = true;
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void hopShowMonster( int idx )
{
    int pattern;
    pattern = hopMonsters[ idx ].pattern + ( ( hopMonsters[ idx ].pos.x & 1 ) << 2 );
    hopShowSprite( &hopMonsters[ idx ].pos, pattern );
}

void hopInitMonsters()
{
    int i, n, sprite, floor, left, right;
    n = hopStageMonsterCount[ hopStageIndex ];
    sprite = HOP_SPRITE_MONSTER;
    for( i = 0; i < n; i = i + 1 )
    {
        floor = hopStageMonsters[ hopStageIndex ][ i * 3 + 0 ];
        left = hopColumnToX( hopStageMonsters[ hopStageIndex ][ i * 3 + 1 ] );
        right = hopColumnToX( hopStageMonsters[ hopStageIndex ][ i * 3 + 2 ] );
        hopMonsters[ i ].pos.y = hopFloorToY( floor ) + HOP_OFFSET_HEAD;
        hopMonsters[ i ].pos.x = left;
        hopMonsters[ i ].left = left;
        hopMonsters[ i ].right = right;
        hopMonsters[ i ].dx = 1;
        hopMonsters[ i ].pattern = HOP_CHAR_MONSTER_RIGHT;
        hopMonsters[ i ].pos.sprite = sprite;
        hopShowMonster( i );
        sprite = sprite + 1;
    }
    for( i = n; i < HOP_MAX_MONSTER_COUNT; i = i + 1 )
      hopMonsters[ i ].pattern = HOP_INVALID_PATTERN;
}

void hopHitMan( int idx )
{
    if( hopIsNear( &hopMan.pos, &hopMonsters[ idx ].pos ) )
      hopMan.status = hopMan.status & ~HOP_MAN_LIVE;
}

void hopMoveMonsters()
{
    int i;
    for( i = 0; i < HOP_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( hopMonsters[ i ].pattern != HOP_INVALID_PATTERN )
        {
            hopHitMan( i );
            if( hopMonsters[ i ].dx > 0 )
            {
                hopMoveX( &hopMonsters[ i ].pos, hopMonsters[ i ].dx );
                if( hopMonsters[ i ].pos.x == hopMonsters[ i ].right )
                {
                    hopMonsters[ i ].dx = -1;
                    hopMonsters[ i ].pattern = HOP_CHAR_MONSTER_LEFT;
                }
            }
            else
            {
                hopMoveX( &hopMonsters[ i ].pos, hopMonsters[ i ].dx );
                if( hopMonsters[ i ].pos.x == hopMonsters[ i ].left )
                {
                    hopMonsters[ i ].dx = 1;
                    hopMonsters[ i ].pattern = HOP_CHAR_MONSTER_RIGHT;
                }
            }
            hopHitMan( i );
            hopShowMonster( i );
        }
    }
}


// -----------------------------------------------------------------------------
//   VVram.cpp / Rendering
// -----------------------------------------------------------------------------

void hopMapToVVram()
{
    int leftBoundary, left, floor, colStartX, mapByteIdx, vy;
    int b, sub, cellByte, i, j, realX;

    for( i = 0; i < HOP_STAGE_TOP; i = i + 1 )
    {
        for( j = 0; j < HOP_VVRAM_WIDTH; j = j + 1 )
          hopVVram[ i ][ j ] = HOP_CHAR_SPACE;
    }

    leftBoundary = hopLeftX / ( HOP_COLUMNS_PER_BYTE * HOP_COLUMN_WIDTH );
    left = leftBoundary * HOP_COLUMNS_PER_BYTE * HOP_COLUMN_WIDTH - hopLeftX;

    for( floor = 0; floor < HOP_FLOOR_COUNT; floor = floor + 1 )
    {
        colStartX = left;
        mapByteIdx = floor * ( HOP_COLUMN_COUNT / HOP_COLUMNS_PER_BYTE ) + leftBoundary;
        vy = HOP_STAGE_TOP + floor * HOP_FLOOR_HEIGHT;
        while( colStartX < HOP_VVRAM_WIDTH )
        {
            if( mapByteIdx >= 0 && mapByteIdx < HOP_CELLMAP_BYTES )
              b = hopCellMap[ mapByteIdx ];
            else
              b = 0;
            mapByteIdx = mapByteIdx + 1;
            for( sub = 0; sub < HOP_COLUMNS_PER_BYTE; sub = sub + 1 )
            {
                cellByte = b & HOP_CELL_MASK;
                for( i = 0; i < HOP_FLOOR_HEIGHT; i = i + 1 )
                {
                    for( j = 0; j < HOP_COLUMN_WIDTH; j = j + 1 )
                    {
                        realX = colStartX + j;
                        if( realX >= 0 && realX < HOP_VVRAM_WIDTH )
                          hopVVram[ vy + i ][ realX ] = hopCellChars[ cellByte ][ i * HOP_COLUMN_WIDTH + j ];
                    }
                }
                colStartX = colStartX + HOP_COLUMN_WIDTH;
                b = b >> HOP_COLUMN_WIDTH;
            }
        }
    }
}

void hopDrawAll()
{
    hopMapToVVram();
    hopDrawGoal();
    hopDrawSprites();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly, the
// same derivation already established and proven in Cracky's own
// crkComposeRawByte() - see that file's own header comment for the details.
//
// **OR-combines the map/VVram layer with the status-text layer instead of
// choosing one exclusively**, mirroring Cracky's own identical fix exactly
// (see this file's own header comment for the full writeup): during the
// title screen (hopFullWidthText), the real "HOPMAN" logo bitmap drawn into
// hopVVram (see hopBeginTitle()) occupies real hardware pages 1-2 only,
// while every status-text element (SCORE/STAGE/TIME/lives/MINI/START/
// CONTINUE/credit) is printed on pages 0/3/5/6/7 - entirely disjoint page
// ranges, so this can never actually blend two real, distinct pieces of
// content together. It just lets the logo (mapByte, non-zero only on its
// own 2 pages) and the text (textByte, non-zero only on its own 5 pages)
// coexist within one composed byte instead of one silently excluding the
// other. During normal gameplay (!hopFullWidthText), behavior is unchanged
// from before: the map area (rawCol < HOP_VVRAM_WIDTH*4) returns mapByte
// alone, and the status-only zone beyond it returns textByte alone.
int hopComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < HOP_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = hopVVram[ rawPage * 2 ][ mapX ];
        lower = hopVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = hopCharPattern[ upper * 2 + 0 ];
            lowerByte = hopCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = hopCharPattern[ upper * 2 + 0 ];
            lowerByte = hopCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = hopCharPattern[ upper * 2 + 1 ];
            lowerByte = hopCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = hopCharPattern[ upper * 2 + 1 ];
            lowerByte = hopCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !hopFullWidthText && rawCol < HOP_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // hopStatusChar's own full-width indexing directly - no "subtract the
    // map width" local-offset math needed, since rawCol/4 already lands on
    // the correct real column either way (whether this is the
    // hopFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = hopStatusChar[ rawPage ][ charCol ];
            textByte = hopAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void hopRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( hopOverlayActive && page == hopOverlayPage &&
                col >= hopOverlayCol * 4 && col < hopOverlayCol * 4 + hopOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - hopOverlayCol * 4 ) / 4;
                sub = ( col - hopOverlayCol * 4 ) % 4;
                value = hopAsciiPattern[ hopAsciiIndex( hopOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = hopComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp (part 2) / progression
// -----------------------------------------------------------------------------

void hopInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never actually stops the player from continuing past stage 8) -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching Cracky's own identical precedent for this exact
    // shape (both games share the same author/engine).
    int i, j;
    i = 0;
    j = 0;
    while( i < hopCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= HOP_STAGE_COUNT )
          j = 0;
    }
    hopStageIndex = j;
}

void hopBeginTrying()
{
    int i;
    hopStageTime = 18;
    i = hopStageItemCount[ hopStageIndex ];
    while( i != 0 )
    {
        hopStageTime = hopStageTime + 8;
        i = i - 1;
    }
    hopItemCount = hopStageItemCount[ hopStageIndex ];
    hopHideAllSprites();

    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          hopStatusChar[ i ][ j ] = 0;
    }
    hopOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in hopUpdateTitle()) - matches Cracky's own identical
    // belt-and-suspenders reset in crkInitTrying(), in case any future call
    // site ever reaches hopBeginTrying() without going through that
    // transition first.
    hopFullWidthText = false;
    hopPrintStatus();

    for( i = 0; i < HOP_CELLMAP_BYTES; i = i + 1 )
      hopCellMap[ i ] = hopStageBytes[ hopStageIndex ][ i ];
    hopLeftX = 0;
    hopCleared = false;
    hopInitMan();
    hopInitLifts();
    hopInitMonsters();
    hopDrawAll();

    hopMonsterNum = 0;
    hopTimeDenom = HOP_MAX_TIME_DENOM;
    hopLiftPhase = true;
    hopStartSeq( 1, HOP_MELODY_START );
    hopState = HOP_STATE_START_JINGLE;
}

void hopBeginStage()
{
    hopInitStage();
    hopBeginTrying();
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after the same real user-supplied hardware photo that fixed
// Cracky's own title screen proved the previous version of this function
// was simply wrong.** The earlier version believed upstream's own title-
// screen text collided with the SCORE/STAGE/TIME status labels and had to
// be trimmed/relocated/dropped to fit - "CONTINUE" was truncated to
// "CONTINU", `hopPrintStatus()` was never called at all (to avoid the
// clobbering that would otherwise happen on the too-narrow grid), and the
// credit line was split across two separately-positioned calls instead of
// upstream's real single "INUFUTO 2026" line. Re-reading upstream's real
// `Status.cpp` (`Title()`) line by line shows this diagnosis was backwards:
// none of that text ever collides with anything upstream, because
// upstream's own Vram address space is a genuinely wide 32-char-cell-per-
// page canvas (see hopStatusChar's own header comment) - the status labels
// occupy only columns 24-31 (upstream's own `LeftX=24`), and every piece
// of title-screen text sits at columns 8-23, well clear of them. The ROOT
// problem was this port's own `hopStatusChar` being modeled as an
// 8-column-wide grid in the first place - now fixed there, this function
// is rewritten to place everything at upstream's real, literal columns,
// with `hopFullWidthText=true` so hopComposeRawByte() renders the full
// canvas instead of just the narrow status zone.
void hopBeginTitle()
{
    int i, j;

    for( i = 0; i < HOP_VVRAM_HEIGHT; i = i + 1 )
    {
        for( j = 0; j < HOP_VVRAM_WIDTH; j = j + 1 )
          hopVVram[ i ][ j ] = HOP_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 32; j = j + 1 )
          hopStatusChar[ i ][ j ] = 0;
    }
    hopOverlayActive = false;
    hopFullWidthText = true;
    hopHideAllSprites();
    hopStageTime = 0;
    // Matches upstream's real Title(), which calls PrintStatus() first
    // (SCORE/STAGE/TIME/lives at their own real columns 24-31) before
    // drawing any of the title-screen-specific text below - safe now that
    // hopStatusChar/hopFullWidthText give it the real 32-column canvas to
    // draw into, rather than something to avoid.
    hopPrintStatus();

    // **Restored, matching Cracky's own identical fix** (see this file's
    // own header comment and `hopComposeRawByte()`'s own header for the
    // full writeup): this is upstream's own real 6-glyph "HOPMAN" logo
    // bitmap, drawn directly into hopVVram from hopTitleBytes[] at its own
    // real position. Upstream's own placement: `TitleLength=6`,
    // `TitleLeft=(VVramWidth-4*TitleLength)/2 = (24-24)/2 = 0`, starting at
    // `VVram + VVramWidth*2 + TitleLeft` - i.e. VVram rows 2-5 (real
    // hardware pages 1-2), columns 0-23 (the full VVram width, since 6
    // letters x 4 columns each exactly fills it - matching Cracky's own
    // identical TitleLeft=0 derivation for "CRACKY", also 6 letters). This
    // previously used a plain-text substitute (reasoning it was "purely
    // decorative") - wrong, per the same Cracky finding: it's the actual
    // title wordmark, the single biggest, most prominent element on the
    // whole screen, not a throwaway detail. This also means the title word
    // no longer depends on hopAsciiPattern's own hand-added 'H' glyph
    // workaround at all (that glyph is a status-text-font addition, purely
    // for the "H" the reduced 27-glyph font otherwise lacks - left in place
    // since it may still legitimately matter for other text elsewhere, but
    // the title word itself now bypasses that font entirely, drawn as a
    // real pixel-art bitmap through hopCharPattern instead).
    // hopComposeRawByte() OR-combines this VVram content with
    // hopStatusChar's own text layer rather than choosing one exclusively,
    // since the two occupy disjoint page ranges by construction (see that
    // function's own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                hopVVram[ 2 + row ][ ch * 4 + col ] = hopTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col TitleLeft+4*TitleLength-5=19,
    // START/CONTINUE at col ArrowX+1=9 with the cursor at col ArrowX=8, the
    // credit line at col 12) - all genuinely clear of the status labels'
    // own columns 24-31, so nothing here needs trimming, relocating, or
    // dropping anymore. "CONTINUE" is now the full 8-character word
    // (previously truncated to "CONTINU" to fit the too-narrow grid), and
    // the credit line is now upstream's real single "INUFUTO 2026" line
    // (previously split across two separately-positioned calls).
    {
        int sMini[4] = { 'M', 'I', 'N', 'I' };
        int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        hopPrintS( 3, 19, sMini, 4 );
        hopPrintS( 5, 9, sStart, 5 );
        hopPrintS( 6, 9, sContinue, 8 );
        hopPrintS( 7, 12, sCredit, 12 );
    }

    hopSelection = 0;
    hopSelectionChanged = true;
    hopPrevLeft = 0; hopPrevRight = 0; hopPrevUp = 0; hopPrevDown = 0; hopPrevFire = 0;
    hopState = HOP_STATE_TITLE;
}

void hopUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !hopPrevLeft ) || ( right && !hopPrevRight ) ||
                ( up && !hopPrevUp ) || ( down && !hopPrevDown ) );
    justFire = ( fire && !hopPrevFire );
    hopPrevLeft = left; hopPrevRight = right; hopPrevUp = up; hopPrevDown = down; hopPrevFire = fire;

    if( hopSelectionChanged )
    {
        hopSelectionChanged = false;
        if( hopSelection == 0 )
          hopPrintC( 5, 8, '>' );
        else
          hopPrintC( 5, 8, ' ' );
        if( hopSelection == 1 )
          hopPrintC( 6, 8, '>' );
        else
          hopPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        hopFullWidthText = false;
        hopPendingContinue = ( hopSelection == 1 );
        hopScore = 0;
        if( !hopPendingContinue )
          hopCurrentStage = 0;
        hopRemainCount = 3;
        hopBeginStage();
        hopRender();
        return;
    }
    if( justDir )
    {
        hopSelection = hopSelection ^ 1;
        hopSelectionChanged = true;
    }
    hopRender();
}

void hopUpdateStartJingle()
{
    if( !hopSeqPlaying( 1 ) )
    {
        hopStartBgm();
        hopTickCounter = 0;
        hopState = HOP_STATE_PLAYING;
    }
    hopRender();
}

void hopBeginLose()
{
    hopStopBgm();
    hopAnimStep = 0;
    hopWaitFrames = 0;
    hopState = HOP_STATE_LOSE_ANIM;
}

void hopUpdateLoseAnim()
{
    int patterns[4] = { HOP_CHAR_MAN_LEFT, HOP_CHAR_MAN_LOOSE0, HOP_CHAR_MAN_LOOSE1, HOP_CHAR_MAN_LOOSE2 };

    if( hopWaitFrames > 0 )
    {
        hopWaitFrames = hopWaitFrames - 1;
        hopRender();
        return;
    }

    hopShowSprite( &hopMan.pos, patterns[ hopAnimStep & 3 ] );
    hopDrawAll();
    hopStartSeq( 0, HOP_MELODY_LOOSE );
    hopAnimStep = hopAnimStep + 1;
    hopWaitFrames = hopNoteFrames( 1 );

    if( hopAnimStep >= 8 )
    {
        hopRemainCount = hopRemainCount - 1;
        if( hopRemainCount > 0 )
          hopBeginTrying();
        else
        {
            hopPrintGameOver();
            hopStartSeq( 1, HOP_MELODY_GAMEOVER );
            hopState = HOP_STATE_GAMEOVER_JINGLE;
        }
    }
    hopRender();
}

void hopUpdateGameOverJingle()
{
    if( !hopSeqPlaying( 1 ) )
      hopBeginTitle();
    else
      hopRender();
}

void hopBeginClearWait()
{
    hopStopBgm();
    hopWaitFrames = 10;
    hopState = HOP_STATE_CLEAR_WAIT;
}

void hopUpdateClearWait()
{
    if( hopWaitFrames > 0 )
    {
        hopWaitFrames = hopWaitFrames - 1;
        hopRender();
        return;
    }
    hopStartSeq( 1, HOP_MELODY_CLEAR );
    hopState = HOP_STATE_CLEAR_JINGLE;
    hopRender();
}

void hopUpdateClearJingle()
{
    if( !hopSeqPlaying( 1 ) )
    {
        hopWaitFrames = 0;
        hopState = HOP_STATE_BONUS_TALLY;
    }
    hopRender();
}

void hopUpdateBonusTally()
{
    if( hopWaitFrames > 0 )
    {
        hopWaitFrames = hopWaitFrames - 1;
        hopRender();
        return;
    }

    if( hopStageTime >= HOP_BONUS_RATE )
    {
        hopAddScore( 5 );
        hopStageTime = hopStageTime - HOP_BONUS_RATE;
        hopPrintTime();
        hopStartSeq( 0, HOP_MELODY_BEEP );
        hopWaitFrames = hopNoteFrames( 1 );
        hopRender();
        return;
    }

    hopStageTime = 0;
    hopPrintStatus();

    if( hopItemCount == 0 )
    {
        hopPrintPerfect();
        hopPerfectRemaining = hopStageItemCount[ hopStageIndex ];
        hopWaitFrames = 0;
        hopState = HOP_STATE_PERFECT_TALLY;
        hopRender();
        return;
    }

    hopCurrentStage = hopCurrentStage + 1;
    hopBeginStage();
    hopRender();
}

void hopUpdatePerfectTally()
{
    if( hopWaitFrames > 0 )
    {
        hopWaitFrames = hopWaitFrames - 1;
        hopRender();
        return;
    }

    if( hopPerfectRemaining <= 0 )
    {
        hopCurrentStage = hopCurrentStage + 1;
        hopBeginStage();
        hopRender();
        return;
    }

    hopPerfectRemaining = hopPerfectRemaining - 1;
    hopAddScore( 10 );
    hopStartSeq( 0, HOP_MELODY_BONUS );
    // Upstream: `Sound_Bonus(); WaitTimer(30);` - Sound_Bonus() is a real
    // non-blocking StartMelody() call (channel 0), so the loop's actual
    // real-time pacing comes ENTIRELY from the independent WaitTimer(30)
    // call, not from the melody's own play length (the 6-note melody plays
    // concurrently in the background via the SysTick-driven SoundHandler(),
    // finishing in ~13 real frames, well inside the 30-frame gate). A prior
    // version of this port used `hopNoteFrames(1)*6 + 30` here, incorrectly
    // treating this like the *blocking* Sound_Beep() case in
    // hopUpdateBonusTally() above (where the wait genuinely must equal the
    // note's own duration) - that stretched every "PERFECT" bonus tick from
    // upstream's real 30 real-frame pace out to 42, a ~40% slowdown across
    // up to 21 ticks per stage. Fixed to the literal upstream constant.
    hopWaitFrames = 30;
    hopRender();
}

void hopUpdatePlaying()
{
    hopTickCounter = hopTickCounter + 1;
    if( hopTickCounter < HOP_TICK_DIVISOR )
    {
        hopRender();
        return;
    }
    hopTickCounter = 0;

    hopMoveMan();
    if( hopMonsterNum >= 0 )
    {
        hopMoveMonsters();
        hopMonsterNum = hopMonsterNum - 10;
    }
    hopMonsterNum = hopMonsterNum + 6;

    hopTimeDenom = hopTimeDenom - 1;
    if( hopTimeDenom == 0 )
    {
        hopStageTime = hopStageTime - 1;
        hopTimeDenom = HOP_MAX_TIME_DENOM;
        hopPrintTime();
        if( hopStageTime == 0 )
        {
            hopPrintTimeUp();
            hopDrawAll();
            hopRender();
            hopBeginLose();
            return;
        }
    }

    if( hopLiftPhase )
      hopMoveLifts();
    hopLiftPhase = !hopLiftPhase;

    hopDrawAll();

    if( ( hopMan.status & HOP_MAN_LIVE ) == 0 )
    {
        hopRender();
        hopBeginLose();
        return;
    }

    if( hopCleared )
    {
        hopRender();
        hopBeginClearWait();
        return;
    }

    hopRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameHopman_init()
{
    int i;

    hopScore = 0;
    hopCurrentStage = 0;
    hopRemainCount = 3;
    hopStageTime = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        hopSeqActive[ i ] = 0;
        hopSeqMelody[ i ] = HOP_MELODY_NONE;
    }
    hopOverlayActive = false;
    hopTickCounter = 0;

    hopBeginTitle();
}

void gameHopman_update()
{
    hopAdvanceSound();

    if( hopState == HOP_STATE_TITLE ) hopUpdateTitle();
    else if( hopState == HOP_STATE_START_JINGLE ) hopUpdateStartJingle();
    else if( hopState == HOP_STATE_PLAYING ) hopUpdatePlaying();
    else if( hopState == HOP_STATE_LOSE_ANIM ) hopUpdateLoseAnim();
    else if( hopState == HOP_STATE_GAMEOVER_JINGLE ) hopUpdateGameOverJingle();
    else if( hopState == HOP_STATE_CLEAR_WAIT ) hopUpdateClearWait();
    else if( hopState == HOP_STATE_CLEAR_JINGLE ) hopUpdateClearJingle();
    else if( hopState == HOP_STATE_BONUS_TALLY ) hopUpdateBonusTally();
    else if( hopState == HOP_STATE_PERFECT_TALLY ) hopUpdatePerfectTally();
}
