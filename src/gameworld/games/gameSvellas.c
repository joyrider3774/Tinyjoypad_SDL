// =============================================================================
// SVELLAS mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_svellas`) - a puzzle game:
// walk a 6x4 grid of sliding 4x4-cell "panels" (each its own little room with
// walls/openings on each side), sliding panels into adjacent empty slots to
// reconnect passages and collect every star, while dodging up to 4 chasing
// monsters. 8 hand-authored stages, 3 lives, no persisted high score at all
// (upstream's own `HiScore` is entirely commented out throughout Main.cpp -
// a deliberate absence, not a gap this port should fill in, matching this
// project's own precedent of leaving a genuinely-absent upstream feature
// alone rather than inventing one, e.g. Tiny Bomber's own dead Music[]
// off-by-one left uncorrected).
//
// Ported directly from the sibling `UIAPduino_cracky` port already shipped
// in this project (`src/games/gameCracky.c`) - same author, same CH32V003
// RISC-V + SSD1306 hardware/driver lineage (`Oled.cpp`'s own
// `SendOledData()` streams one raw byte at a time, the exact same model
// `md_drawColumn()` already handles), the same real 60Hz SysTick timer
// (`Timer.cpp`'s `kTimerHz=60`), the same 4-direction+1-button input
// (`ScanKeys.h`), and - confirmed by direct comparison - byte-identical
// `AsciiPattern`/status-text machinery and identical GAME OVER/TIME UP
// overlay page+column+content. No new shim primitive was needed
// (isFire2Pressed() simply goes unused, same as Cracky).
//
// **No hardware display-orientation transform, per Cracky's own hard-won
// conclusion** - `InitOled()` sends the exact same `OledCmd::RightToLeft`/
// `OledCmd::BottomToTop` register writes Cracky's own investigation already
// concluded need no software compensation (see gameCracky.c's own header
// for the full story of the wrong transforms tried and reverted there).
// `svlComposeRawByte(col,page)` is drawn directly at its own (col,page),
// no mirroring, no bit-reversal, no lookup table.
//
// **Rendering strategy deliberately diverges from Cracky's own "direct
// structural mirror" of upstream's incremental Back/Front VVram buffers** -
// upstream's `VVramBack` is a persistent map buffer incrementally patched
// by `DrawPanel()`/`ErasePanel()` calls only when a panel actually changes,
// copied to `VVramFront` and overlaid with sprites once per real frame.
// This port instead recomputes the *entire* 24x16 VVram grid from
// `svlPanelMap[24]` (the real source-of-truth panel-byte array) every
// single engine frame via `svlComputeMapVVram()` - the same "always
// redraw the full frame from source data, don't replicate a persistent-
// buffer incremental-patch trick" standing precedent this project has used
// repeatedly (Pinball/Doc/Bert/Tris/Pipe/Plaque/SQuest/Frogger, and
// Cracky's own `crkMapToVVram()`), applied here from the start rather than
// needing a later fix. This works cleanly specifically because
// `DrawPanel(pVVram, b, edge)`'s own glyph-selection logic is a *pure
// function* of (panel byte, edge-info) - re-derived here as
// `svlPanelGlyph(b, edge, relX, relY)`, called for all 16 cells of each of
// the 24 panels (24*16 = 384 = exactly VVramWidth(24) x VVramHeight(16),
// confirming the whole VVram grid really is just "the 24 panels laid out
// side by side", not a coincidence). One direct, useful consequence of
// this choice: upstream's own `VErase2()` call (patching `VVramBack`
// directly when a star is collected) becomes entirely redundant here -
// clearing the `Panel_Star` bit in `svlPanelMap[]` is sufficient, since
// the very next frame's from-scratch recompute naturally stops drawing
// the star glyphs on its own.
//
// The one thing that genuinely can't be derived from the static
// `svlPanelMap[]` alone is the panel *currently sliding* between two grid
// slots (removed from `svlPanelMap[]` for the duration of its move,
// tracked in `svlMovingPanel`/`svlPanelX`/`svlPanelY` instead, matching
// upstream's own equivalent fields) - composited as a small overlay pass
// right after the static grid, using `svlPanelGlyph(...,edge=0,...)`
// (upstream's own `MovePanel()` always calls `DrawPanel(pVVram,
// MovingPanel, 0)` while sliding - edge=0 forces "no arrows drawn on a
// panel that's actively mid-move", still reproduced exactly here).
//
// **A genuine, load-bearing timing derivation, worth spelling out in
// full - including a real bug in an earlier draft's own reasoning about
// it, found via live-play verification, not just inspection.** Upstream's
// `Main()` do-while loop calls `MovePanel()` unconditionally on *every*
// iteration, but `MoveMan()`/`MoveMonsters()`/`DrawAll()`/
// `WaitTimer(8/CoordRate)` only run once every 4th iteration (gated
// behind `(Clock&3)==0`). Since `MovePanel()`'s own internal step-gate
// (`(clock&CoordMask)==0`, with `CoordMask=CoordRate-1=0` since
// `CoordRate=1` here, exactly the same CoordShift=0 degenerate
// configuration Cracky's own Movable.h has) is *always true* regardless of
// its counter's value, every one of those 4 iterations performs a real
// panel-slide step - so between two consecutive *rendered* frames (i.e.
// two consecutive `WaitTimer(8)` calls), `MovePanel()` really does run
// exactly 4 times total. An earlier draft of this port concluded from
// this that a full panel-move (always exactly 4 grid-cell steps, since a
// panel only ever slides into an immediately-adjacent empty slot)
// "completes within a single gap between two rendered frames, and is
// never visibly seen mid-slide by the player" - and built
// `svlUpdatePlayingTick()` around that conclusion, running all 4
// `svlMovePanelStep()` calls back-to-back in the same tick a move starts,
// before ever rendering.
//
// **That conclusion was wrong**, caught via a live Puppeteer test that
// placed the man at a known slide-trigger cell (a temporary debug hook,
// removed again once confirmed) and sampled screenshots every ~70ms
// through an actual slide. Re-tracing the *exact* iteration order (not
// just the total call count) shows why: `MovePanel()` is called
// unconditionally EVERY iteration, including the one that just started a
// move via `MoveMan()` - so a freshly-started move's own FIRST step
// happens in the SAME iteration as the tick that started it, *before*
// that same iteration's own `DrawAll()`/`WaitTimer(8)` render. The
// render right after only ever shows that one step (1-of-4, a quarter of
// the way through the slide) - the remaining 3 steps run immediately
// afterward with no `WaitTimer()` gating them (so they happen back-to-
// back in real time, no frame displayed in between), and their combined
// effect - a *fully completed* slide - only becomes visible on the
// *next* rendered frame. A real player does see a brief, genuine 2-frame
// slide animation (1/4-done, then fully complete) - not the instant
// single-frame "snap" the original draft rendered. Confirmed directly:
// diffing 10 closely-spaced (~70ms apart) screenshots taken through a
// real slide showed two separate, non-adjacent frame-pairs with a real
// wall-structure change (each ~140ms apart, matching the real tick
// period), not one.
//
// **Fixed** by restructuring `svlUpdatePlayingTick()` (see that
// function's own header comment for the exact mechanics) to run any
// leftover "quiet" steps from a move the *previous* tick started FIRST,
// then this tick's own `MoveMan()`/`MoveMonsters()`/timer logic, then
// exactly one new panel step - mirroring upstream's real per-iteration
// interleaving instead of front-loading all 4 steps into the tick a move
// starts. The man's own alive-status check still runs after every single
// `MovePanel()` call in spirit (matching upstream's own
// `if((Man.status&Live)==0) goto lose;`, which runs after every
// `MovePanel()` call, not just once per group) - `Movable_Live` can only
// ever be cleared by `MoveMonsters()`, which itself only ever runs once
// per tick, strictly *before* any of that tick's own panel steps (leftover
// or new), so one check after the 3 leftover steps and one more after the
// new single step together are still equivalent to checking after every
// individual step.
//
// **A genuinely subtle, easy-to-miss C++ `static`-local semantic,
// preserved deliberately rather than accidentally reproduced or
// silently "fixed"**: upstream's `Main()` declares `static sbyte
// monsterNum = 0;` and `static byte timeDenom = MaxTimeDenom;` *inside*
// the function body, right after the `try_:` label - meaning these are
// genuine C++ function-local statics whose own initializer expression
// only ever runs the *first* time control reaches that declaration in
// the whole program's lifetime, never again on any subsequent `goto
// try_;` (life retry) or `goto stage;` (next stage). Real, intentional-
// or-not upstream behavior: both variables carry their mid-countdown
// value across every stage transition and life retry for the entire
// session, never resetting to their nominal "starting" value again after
// the very first time. Ported by declaring `svlMonsterNum`/
// `svlTimeDenom` as plain globals, explicitly initialized **once**, only
// in `gameSvellas_init()` - and deliberately *not* touched anywhere else
// in the state machine (not in `svlBeginTitle()`, not in
// `svlInitTrying()`, not in `svlUpdateStartJingle()`), reproducing the
// real "runs once, ever" semantic exactly. `MovePanel()`'s own separate
// `static byte clock;` needed no such treatment - since `CoordMask` is
// always 0, `(clock&CoordMask)==0` is always true regardless of `clock`'s
// value, making the counter itself entirely inert; dropped rather than
// ported, since there's nothing for it to gate.
//
// **A real AVR/C++-implicit-unsigned-byte-wraparound bug, caught by
// inspection and fixed before ever compiling - the same "AVR-implicit-
// narrow-type-reliance" bug family this project's own CLAUDE.md documents
// extensively (byte truncation / rand() range / shift wraparound /
// signed-sentinel comparison / logical-vs-arithmetic shift / int8
// overflow reliance), here via a sixth distinct mechanism**:
// `Monster::DecideDirection()`'s own pass-through ("ghost") fallback does
// `byte nextColumn = column + dx; byte nextRow = row + dy; can =
// nextColumn < ColumnCount && nextRow < RowCount;` - on real AVR hardware,
// `column=0` with `dx=-1` wraps `nextColumn` to 255 (a real `uint8_t`
// wraparound), which correctly fails the `< ColumnCount` bound and
// rejects the out-of-grid move. Vircon32's plain, non-truncating `int`
// would instead leave `nextColumn` at a genuine `-1`, which is `< 6`
// (`SVL_COLUMN_COUNT`) and would therefore *wrongly allow* a ghosting
// monster to wander off the left/top edge of the grid. **Fixed** with
// explicit `>= 0` checks alongside the existing upper-bound checks,
// reproducing the real bounds test the wraparound achieved implicitly on
// real hardware, without depending on any wraparound at all.
//
// **A real, root-cause title-screen text-layout bug, found and fixed after
// a user-supplied photo of the sibling Cracky game running on actual
// hardware proved this whole family's ported title screens had been using
// the wrong text-width model - see gameCracky.c's own header comment
// (search for `crkStatusChar`/`crkFullWidthText`/`crkBeginTitle`) for the
// full story.** Upstream's real `PrintC()`/`PrintS()` write to a Vram
// address space spanning the *entire* physical screen width - a genuine
// 32-char-cell-wide row (`VramStep=4` real pixels/cell, 128/4=32), not
// just the 8-cell status-label slice. The status labels (SCORE/STAGE/
// TIME/lives) really are confined to columns `LeftX`(24)-31 (confirmed
// directly in this game's own `Status.cpp`, `constexpr auto LeftX = 24;`
// - identical to Cracky's own `LeftX`) - but every piece of *title-
// screen* text (MINI, START, CONTINUE, the "INUFUTO 2026" credit) lives
// at real columns 8-23, using the exact same shared `PrintC()`/`PrintS()`
// mechanism at different column arguments, well clear of the status
// zone. This port's own `svlStatusChar` had been modeled as an 8-column-
// wide grid (matching only the status labels' own real range) and then
// ALSO used to hold the title screen's own text, reusing the same
// columns 24-31 the status labels need - the exact root cause of the
// symptoms an earlier draft "fixed" by truncating "CONTINUE" to "CONT"
// and shortening the credit line to "INUFUTO" alone, rather than by
// widening the grid to its real size. **Fixed** by widening
// `svlStatusChar` to `[8][32]` and adding a `svlFullWidthText` flag (true
// only during `SVL_STATE_TITLE`) so `svlComposeRawByte()` reads the full
// 32-column range instead of just columns 24-31 while the title screen is
// showing - see `svlBeginTitle()` below, now placing every piece of text
// at upstream's real, literal columns (re-derived directly from this
// game's own `Status.cpp`, not assumed identical to Cracky's): MINI at
// page 3 col 17 (`TitleLeft + 4*TitleLength - 5` = `2 + 20 - 5`, since
// this game's own `TitleLength=5` differs from Cracky's), the full
// "INUFUTO 2026" credit at page 7 col 12 (both fully spellable with this
// font after all - 'E' *is* in the shared `AsciiPattern` table, so
// "CONTINUE" needed no truncation either, contrary to the earlier draft's
// assumption), and "START"/"CONTINUE" at page 5/6 col 9 with the cursor
// at col 8 - upstream's own `ArrowX=8` - none of which collides with
// SCORE/STAGE/TIME's own columns 24-31 once the grid is genuinely wide
// enough to hold both regions side by side without overwriting either.
//
// **A second, related architectural issue, found and fixed the same way**:
// this port had also dropped upstream's own real title-screen logo bitmap
// (the hand-drawn "SVELLAS" wordmark, `Status.cpp`'s `Title()`'s own
// `TitleBytes[]` table) in favor of small plain text, reasoning - by
// direct analogy with Cracky's own earlier (and, at the time, also wrong)
// "purely decorative, drop it" call - that it was non-gameplay-relevant.
// It isn't: it's the single largest, most prominent element on the whole
// title screen, exactly like Cracky's own restored "CRACKY" logo (see that
// file's own header comment). **Fixed** the identical way: `svlTitleBytes`
// (80 values, byte-diff-verified against the real upstream source) is now
// drawn directly into `svlVVram` by `svlBeginTitle()` at upstream's own
// real position, and `svlComposeRawByte()` OR-combines that map-layer
// content with `svlStatusChar`'s own text layer instead of choosing one
// exclusively - safe because the logo (VVram rows 2-5, real hardware pages
// 1-2) and every piece of status/title text (pages 0/3/5/6/7) occupy
// entirely disjoint page ranges, confirmed the same way as Cracky's own
// fix. `svlCharPattern`'s own "logo" range (indices 0-15, the first 32
// bytes of that table) needed no fix at all - already byte-for-byte
// identical to upstream's real `CharPattern[]` logo range, confirmed via
// script before assuming otherwise.
//
// **The lives-remaining indicator** (upstream's real `Put2C(vram,
// Char_Remain)` - a little walking-man icon rendered via the *map-area*
// nibble-packed `CharPattern` glyph table, not the plain-text
// `AsciiPattern` this port's status-zone grid supports) is dropped in
// favor of plain text/blanks at upstream's real `LeftX`(24)-based
// columns, reusing Cracky's own already-established simplification for
// the identical situation (see `svlPrintStatus()` below) - not
// re-derived independently.
//
// **GAME OVER / TIME UP messages** reuse Cracky's own exact page/column/
// length/content (`svlBeginOverlay`, mirroring `crkBeginOverlay` -
// confirmed page4/col8/len9/"GAME OVER" and page4/col9/len7/"TIME UP" are
// byte-for-byte identical between the two upstream `Status.cpp` files, not
// a coincidence given the shared author/engine lineage) - the exact same
// "burn a message directly over the map area, outside the VVram grid"
// mechanism Cracky's own header comment documents in detail.
//
// **Sound**: same real multi-channel tracker as Cracky
// (`ToneChannel::Next()`/`SoundHandler()`), but a *different* tempo
// constant - `Tempo=170` here, not Cracky's `160` - giving a different
// real note-tick rate: `SoundHandler()`'s own `time -= Tempo; if(time<=0)
// time += 600/2;` amortizes to one note-tick every `300/170 =
// 1.7647058823529411` real 60Hz SysTick ticks (re-derived the same way
// Cracky's own `1.875` was, not assumed identical). `svlNoteFrames(length)
// = round(length * 300.0/170.0)`. Every melody table (`Sound_Loose/Hit/
// Beep/Move/Start/Clear/GameOver`, plus the two `StartBGM()` voices) was
// extracted via a small Python script that evaluates the real
// `NoteLength`/`Scale` enum arithmetic directly out of `Sound.cpp` (not
// hand-copied) and cross-checked against a raw token count of the
// original source with only line-comments stripped, confirming an exact
// 131/229-token match for the two BGM voices with no drift. `Sound_Move()`
// (a new one-shot "click" cue with no Cracky equivalent, fired via
// `StartMelody()`/fire-and-forget when a panel move begins) shares
// channel 0 with the item-pickup "Hit" cue exactly like Cracky's own
// shared-channel-0 design - confirmed the two can never collide in the
// same tick, since a panel-move can only start from an edge cell
// (`xMod`/`yMod` in {0,2}) while a star pickup can only fire from the
// panel's own center cell (`xMod==1 && yMod==1`), mutually exclusive by
// construction. `Sound_Beep()`'s own bonus-tally loop has a real detail
// Cracky's own bonus tally doesn't: upstream calls `Sound_Beep();
// WaitTimer(6);` - a real *additional* 6-tick wait on top of the beep's
// own blocking note duration, not just the note's own duration alone -
// reproduced as a single combined wait, `svlNoteFrames(1) + 6`.
//
// Movable coordinates are, just like Cracky's own Movable.h, already
// expressed directly in VVram-cell-grid units (`CoordShift=0`/
// `CoordRate=1` again make the generic sub-cell-precision parameterization
// fully degenerate) - kept as the real expressions throughout
// (`SVL_COORD_SHIFT`/`SVL_COORD_RATE`/`SVL_COORD_MASK`,
// `SVL_MAX_TIME_DENOM`'s own `50/(8/SVL_COORD_RATE)`) rather than
// silently pre-resolved to their current literal values, matching this
// project's - and Cracky's own - established preference.
//
// **Dialect conversion for this standalone SDL3 port**: mechanically
// converted from the sibling tinyjoypad_vircon32 project's own already-
// correct, already user-verified-on-real-hardware gameSvellas.c (see that
// project's own CLAUDE.md for the full porting/bug-fix history behind
// every finding documented above) - only `int[N] name` -> `int name[N]`
// (and the 2D form) array-declaration syntax and the two bare `struct`
// tags (`SvlMovable`/`SvlSprite`) -> `typedef struct` needed changing;
// every string-shaped array in this file (svlOverlayText, the various
// `sScore`/`sStage`/`sMini`/`sContinue`/etc local buffers) holds
// individual `'A'`-style char constants, never a `"..."` string literal,
// so - matching gameCracky.c's own identical finding - none of them
// needed `int[]`->`char[]` widening; every value still round-trips
// through svlAsciiIndex()'s own int-comparison lookup exactly as upstream
// wrote it. No EEPROM/high-score persistence here, matching upstream
// having none at all (see above, and confirmed here too - a CH32V003
// RISC-V board, not AVR). No forceRedraw needed either: every one of this
// state machine's own branches calls svlRender() unconditionally on every
// real tick it runs, so there's no "last frame's pixels are still
// correct, skip redrawing" case anywhere in this file for a resumed
// screen to miss.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into svlCharPattern (map tiles) / svlAsciiPattern
// -----------------------------------------------------------------------------

#define SVL_CHAR_SPACE 0x00
#define SVL_CHAR_PANEL 0x10
#define SVL_CHAR_MAN 0x15
#define SVL_CHAR_MAN_LEFT 0x15
#define SVL_CHAR_MAN_RIGHT 0x1D
#define SVL_CHAR_MAN_UP 0x25
#define SVL_CHAR_MAN_DOWN 0x2D
#define SVL_CHAR_MONSTER 0x35
#define SVL_CHAR_MONSTER_REV 0x55
#define SVL_CHAR_STAR 0x65
#define SVL_CHAR_ARROW 0x69
#define SVL_CHAR_END 0x71

// -----------------------------------------------------------------------------
//   ScanKeys.h - key bitmask constants (kept for structural fidelity, though
//   this port reads isLeftPressed()/etc directly, matching Cracky's own
//   equivalent note)
// -----------------------------------------------------------------------------

#define SVL_KEYS_LEFT 0x01
#define SVL_KEYS_RIGHT 0x02
#define SVL_KEYS_UP 0x04
#define SVL_KEYS_DOWN 0x08
#define SVL_KEYS_BUTTON0 0x10

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define SVL_COORD_SHIFT 0
#define SVL_COORD_RATE ( 1 << SVL_COORD_SHIFT )
#define SVL_COORD_MASK ( SVL_COORD_RATE - 1 )

#define SVL_HIT_RANGE ( SVL_COORD_RATE * 4 / 3 )

#define SVL_MOVABLE_LIVE 0x80
#define SVL_MOVABLE_ON_PANEL 0x40
#define SVL_MOVABLE_PATTERN_MASK 0x07

#define SVL_DIRECTION_LEFT 0
#define SVL_DIRECTION_RIGHT 1
#define SVL_DIRECTION_UP 2
#define SVL_DIRECTION_DOWN 3

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
} SvlMovable;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define SVL_COLUMN_COUNT 6
#define SVL_ROW_COUNT 4
#define SVL_PANEL_MAP_SIZE ( SVL_COLUMN_COUNT * SVL_ROW_COUNT )

#define SVL_PANEL_LEFT 0x01
#define SVL_PANEL_RIGHT 0x02
#define SVL_PANEL_TOP 0x04
#define SVL_PANEL_BOTTOM 0x08
#define SVL_PANEL_EXIST 0x10
#define SVL_PANEL_STAR 0x40

#define SVL_STAGE_COUNT 8
#define SVL_MAX_MONSTER_COUNT 4

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define SVL_VVRAM_WIDTH 24
#define SVL_VVRAM_HEIGHT 16

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define SVL_SPRITE_MAN 0
#define SVL_SPRITE_MONSTER 1
#define SVL_SPRITE_END 5
#define SVL_INVALID_PATTERN 255

typedef struct
{
    int x, y;
    int code;
} SvlSprite;

// -----------------------------------------------------------------------------
//   Sound.h - melody sequencer ids
// -----------------------------------------------------------------------------

#define SVL_MELODY_NONE 0
#define SVL_MELODY_LOOSE 1
#define SVL_MELODY_HIT 2
#define SVL_MELODY_BEEP 3
#define SVL_MELODY_MOVE 4
#define SVL_MELODY_START 5
#define SVL_MELODY_CLEAR 6
#define SVL_MELODY_GAMEOVER 7
#define SVL_MELODY_BGM1 8
#define SVL_MELODY_BGM2 9

#define SVL_TEMPO 170

// -----------------------------------------------------------------------------
//   Data tables - extracted via a small Python script from the real upstream
//   source (evaluating Sound.cpp's own NoteLength/Scale enum arithmetic
//   directly, and cross-checked with a raw token count against the original
//   source), not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph - byte-for-byte
// identical to Cracky's own crkAsciiPattern (confirmed by direct comparison).
int svlAsciiPattern[108] = {
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

// CharPattern - 113 map-tile glyphs (Char_End=0x71), 2 bytes/glyph (a 4x4
// pixel block), matching Cracky's own CharPattern format exactly.
int svlCharPattern[226] = {
    0, 0, 51, 0, 204, 0, 255, 0,
    0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255,
    254, 255, 255, 239, 247, 255, 255, 127,
    255, 255, 160, 191, 239, 0, 248, 55,
    247, 11, 160, 191, 239, 0, 50, 183,
    127, 33, 0, 254, 251, 10, 176, 127,
    115, 143, 0, 254, 251, 10, 18, 247,
    123, 35, 224, 255, 239, 0, 241, 63,
    119, 3, 224, 255, 239, 0, 115, 55,
    255, 1, 0, 190, 191, 14, 48, 119,
    243, 31, 0, 190, 191, 14, 16, 255,
    115, 55, 168, 175, 239, 8, 16, 115,
    191, 0, 64, 78, 206, 0, 50, 247,
    255, 2, 128, 254, 250, 138, 0, 251,
    55, 1, 0, 236, 228, 4, 32, 255,
    127, 35, 232, 239, 239, 8, 48, 247,
    55, 0, 192, 206, 206, 0, 113, 255,
    127, 1, 128, 190, 190, 142, 0, 115,
    127, 3, 0, 108, 108, 12, 16, 247,
    255, 23, 94, 81, 17, 14, 33, 68,
    168, 5, 224, 17, 21, 229, 80, 138,
    68, 18, 224, 17, 17, 225, 16, 66,
    72, 18, 30, 21, 21, 14, 33, 132,
    36, 1, 196, 252, 204, 4, 64, 19,
    67, 0, 200, 138, 168, 140, 16, 2,
    32, 1, 64, 242, 66, 0, 32, 244,
    36, 0,
};

// TitleBytes - upstream's own real "SVELLAS" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 groups of 4x4 VVram-cell glyph indices each
// (80 values total - TitleLength=5 for this game, not a letter count; the
// hand-drawn 7-letter "SVELLAS" wordmark is packed across those 5 groups'
// own 20-column canvas without regard to per-letter cell boundaries, the
// same way Cracky's own 6-group/6-letter "CRACKY" logo happened to line up
// 1:1 purely by coincidence). Byte-diff-verified against the real upstream
// source via a small script. Every value here is a valid index into
// svlCharPattern[]'s own "logo" range (indices 0-15, the first 32 bytes of
// that table, confirmed identical to upstream byte-for-byte) - the exact
// same shared block-pattern palette every other map tile in this game
// already draws through. See svlBeginTitle()'s own comment for why this
// replaces the earlier plain-text-only substitute.
int svlTitleBytes[80] = {
    14, 5, 11, 0, 13, 10, 2, 12, 10, 0, 15, 4, 4, 5, 1, 0,
    0, 0, 0, 0, 3, 15, 12, 7, 11, 7, 12, 7, 4, 0, 4, 5,
    0, 0, 0, 0, 1, 15, 0, 12, 1, 15, 0, 12, 1, 5, 5, 4,
    0, 0, 0, 0, 3, 0, 14, 13, 3, 0, 15, 14, 5, 1, 5, 4,
    0, 14, 5, 11, 2, 13, 10, 2, 3, 10, 0, 15, 1, 4, 5, 1,
};

// Standard equal-tempered note frequencies, E2..G5 - byte-for-byte identical
// to Cracky's own table (both derived from the same Scale enum numbering).
int svlFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int svlMelodyLoose[3] = { 1, 18, 0 };

int svlMelodyHit[17] = {
    1, 26, 1, 28, 1, 30, 1, 32, 1, 33,
    1, 35, 1, 37, 1, 38, 0,
};

int svlMelodyBeep[3] = { 1, 30, 0 };

int svlMelodyMove[3] = { 1, 30, 0 };

int svlMelodyStart[23] = {
    12, 33, 12, 33, 12, 28, 12, 28, 8, 37,
    4, 35, 8, 37, 16, 35, 12, 35, 36, 33,
    12, 0, 0,
};

int svlMelodyClear[27] = {
    12, 21, 12, 25, 20, 28, 12, 30, 4, 30,
    8, 28, 4, 26, 8, 28, 4, 28, 8, 30,
    4, 32, 36, 33, 12, 0, 0,
};

int svlMelodyGameOver[27] = {
    8, 33, 4, 33, 8, 32, 4, 32, 8, 30,
    4, 30, 8, 28, 4, 25, 8, 28, 4, 28,
    8, 30, 4, 32, 24, 33, 0,
};

int svlMelodyBgm1[131] = {
    12, 33, 12, 33, 12, 28, 12, 28, 8, 37,
    4, 35, 8, 37, 24, 35, 4, 0, 12, 37,
    12, 40, 8, 35, 4, 33, 8, 35, 48, 37,
    4, 0, 12, 38, 12, 38, 12, 37, 12, 37,
    8, 35, 4, 35, 8, 35, 24, 37, 4, 0,
    12, 38, 12, 38, 12, 37, 12, 37, 8, 40,
    4, 40, 8, 40, 24, 35, 4, 0, 12, 33,
    12, 33, 12, 28, 12, 28, 8, 37, 4, 35,
    8, 37, 24, 35, 4, 0, 12, 37, 12, 40,
    8, 35, 4, 33, 8, 35, 48, 37, 4, 0,
    12, 38, 12, 38, 12, 37, 12, 37, 8, 40,
    4, 40, 8, 40, 16, 37, 12, 35, 12, 33,
    12, 28, 8, 33, 12, 35, 48, 33, 4, 0,
    255,
};

int svlMelodyBgm2[229] = {
    8, 9, 4, 0, 8, 9, 4, 0, 12, 9,
    8, 0, 4, 9, 8, 0, 4, 9, 8, 0,
    4, 9, 8, 9, 4, 11, 8, 9, 4, 12,
    8, 13, 4, 8, 8, 13, 4, 0, 12, 13,
    8, 0, 12, 6, 4, 6, 8, 0, 4, 6,
    8, 6, 4, 0, 8, 6, 4, 0, 8, 14,
    4, 9, 8, 14, 4, 0, 12, 14, 8, 0,
    12, 11, 4, 11, 8, 0, 4, 11, 8, 11,
    4, 0, 8, 11, 4, 0, 8, 2, 4, 0,
    8, 2, 4, 0, 12, 2, 8, 0, 12, 16,
    4, 16, 8, 0, 4, 11, 8, 16, 4, 11,
    8, 16, 4, 0, 8, 9, 4, 0, 8, 9,
    4, 0, 12, 9, 8, 0, 4, 9, 8, 0,
    4, 9, 8, 0, 4, 9, 8, 9, 4, 11,
    8, 9, 4, 12, 8, 13, 4, 8, 8, 13,
    4, 0, 12, 13, 8, 0, 12, 6, 4, 6,
    8, 0, 4, 1, 8, 6, 4, 1, 8, 6,
    4, 0, 8, 2, 4, 0, 8, 2, 4, 0,
    12, 2, 8, 0, 12, 1, 4, 1, 8, 0,
    4, 1, 8, 1, 4, 0, 8, 1, 4, 0,
    8, 9, 4, 0, 8, 9, 4, 0, 12, 4,
    8, 0, 12, 9, 4, 9, 8, 0, 4, 9,
    8, 9, 4, 0, 8, 9, 4, 0, 255,
};

// Rnd() table - byte-for-byte identical to Cracky's own crkRndNumbers (both
// Math.cpp files are literally the same file, same author/engine lineage).
int svlRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

// Stage data - flattened from upstream's own `struct Stage { bytes[24],
// start, monsterCount, pMonsters }` array into parallel fixed arrays,
// matching Cracky's own crkStageStart/crkStageItemCount/crkStageEnemies
// flattening precedent. svlStageMonsters is padded to SVL_MAX_MONSTER_COUNT
// (4) per stage with unused trailing zeros - only the first
// svlStageMonsterCount[stage] entries are ever read, matching
// svlInitMonsters()'s own loop bound.
int svlStageStart[8] = {
    0x11, 0x30, 0x10, 0x21, 0x00, 0x00, 0x00, 0x11,
};
int svlStageMonsterCount[8] = { 1, 1, 2, 2, 2, 2, 4, 2 };

int svlStageMonsters[8][4] = {
    { 83, 0, 0, 0 },
    { 35, 0, 0, 0 },
    { 80, 19, 0, 0 },
    { 51, 83, 0, 0 },
    { 64, 83, 0, 0 },
    { 80, 83, 0, 0 },
    { 80, 49, 3, 83 },
    { 65, 51, 0, 0 },
};

int svlStageBytes[8][24] = {
    {
        85, 22, 85, 112, 0, 86,
        81, 26, 0, 82, 0, 91,
        17, 112, 0, 94, 0, 23,
        89, 94, 93, 92, 88, 26,
    },
    {
        93, 92, 84, 28, 86, 87,
        0, 112, 86, 0, 16, 0,
        83, 0, 0, 0, 112, 90,
        27, 24, 24, 88, 92, 90,
    },
    {
        85, 20, 0, 0, 87, 23,
        17, 90, 0, 16, 24, 50,
        113, 0, 0, 83, 113, 0,
        89, 30, 0, 0, 88, 90,
    },
    {
        116, 28, 84, 22, 85, 82,
        49, 0, 28, 82, 80, 18,
        85, 0, 0, 26, 50, 82,
        17, 84, 28, 24, 0, 18,
    },
    {
        23, 0, 87, 0, 21, 30,
        0, 80, 0, 80, 0, 80,
        80, 112, 0, 0, 80, 0,
        0, 91, 0, 91, 29, 26,
    },
    {
        21, 0, 84, 84, 48, 50,
        19, 0, 81, 80, 83, 0,
        83, 112, 89, 88, 0, 0,
        89, 90, 0, 120, 120, 48,
    },
    {
        20, 84, 84, 84, 84, 20,
        82, 0, 80, 16, 80, 80,
        82, 88, 80, 93, 0, 80,
        16, 80, 80, 88, 124, 16,
    },
    {
        112, 84, 0, 112, 0, 118,
        0, 16, 0, 0, 17, 82,
        0, 0, 91, 0, 118, 120,
        112, 120, 120, 48, 16, 82,
    },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int svlScore;
int svlRemainCount;
int svlCurrentStage;
int svlStageTime;
int svlMaxTime;
int svlStageIndex;
int svlMonsterNum;
int svlTimeDenom;
int svlMonsterClock;

#define SVL_MAX_TIME_DENOM ( 50 / ( 8 / SVL_COORD_RATE ) )
#define SVL_BONUS_RATE 3

int svlVVram[SVL_VVRAM_HEIGHT][SVL_VVRAM_WIDTH];
int svlPanelMap[SVL_PANEL_MAP_SIZE];
int svlStarCount;

int svlDirDx[4] = { -1, 1, 0, 0 };
int svlDirDy[4] = { 0, 0, -1, 1 };

SvlSprite svlSprites[SVL_SPRITE_END];

SvlMovable svlMan;
int svlManDir;
bool svlKeyOn;
bool svlPendingContinue;

int svlMonsterCount;
SvlMovable svlMonsters[SVL_MAX_MONSTER_COUNT];
#define SVL_MONSTER_CONFUSE 0x10
#define SVL_MONSTER_THROUGH 0x20

int svlMovingPanel, svlPanelX, svlPanelY, svlPanelDirection;

int svlRndIndex;

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into svlAsciiPattern (0 = space) per cell.
//
// **Widened from an original, wrong `[8][8]` after a real user-supplied
// hardware photo of the sibling Cracky game's own title screen proved that
// narrow model was flatly incorrect - see this file's own header comment,
// and gameCracky.c's own `crkStatusChar` comment, for the full story.**
int svlStatusChar[8][32];

// Set true only while on the title screen (SVL_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its initial
// ClearScreen(), and instead drives the ENTIRE screen (not just the status
// zone) through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range. When true, svlComposeRawByte()
// reads svlStatusChar across the full width instead of just columns
// 24-31, matching Cracky's own crkFullWidthText exactly.
bool svlFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintTimeUp()/PrintGameOver() Vram-direct writes - same mechanism as
// Cracky's own crkOverlayActive/etc (see this file's own header comment).
bool svlOverlayActive;
int svlOverlayText[10];
int svlOverlayLen;
int svlOverlayPage;
int svlOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real engine frame regardless of the coarser
// SVL_TICK_DIVISOR gameplay tick - matches Cracky's own design exactly.
int svlSeqMelody[3];
int svlSeqPos[3];
int svlSeqWait[3];
int svlSeqActive[3];

#define SVL_TICK_DIVISOR 8
int svlTickCounter;

#define SVL_STATE_TITLE 0
#define SVL_STATE_START_JINGLE 1
#define SVL_STATE_PLAYING 2
#define SVL_STATE_LOSE_ANIM 3
#define SVL_STATE_GAMEOVER_JINGLE 4
#define SVL_STATE_CLEAR_WAIT 5
#define SVL_STATE_CLEAR_JINGLE 6
#define SVL_STATE_BONUS_TALLY 7
int svlState;
int svlWaitFrames;
int svlAnimStep;
int svlSelection;
bool svlSelectionChanged;
bool svlPrevLeft;
bool svlPrevRight;
bool svlPrevUp;
bool svlPrevDown;
bool svlPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int svlRnd()
{
    int r;
    r = svlRndNumbers[ svlRndIndex ];
    svlRndIndex = svlRndIndex + 1;
    if( svlRndIndex >= 32 )
      svlRndIndex = 0;
    return r & 0x0f;
}

int svlAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - low-level map helpers
// -----------------------------------------------------------------------------

int svlToCoord( int a )
{
    return ( a << 2 ) + 1;
}

int svlMapIndex( int column, int row )
{
    return row * SVL_COLUMN_COUNT + column;
}

int svlEdge( int column, int row )
{
    int edge, idx;
    edge = 0;
    idx = svlMapIndex( column, row );
    if( column != 0 && svlPanelMap[ idx - 1 ] == 0 )
      edge = edge | SVL_PANEL_LEFT;
    if( column != SVL_COLUMN_COUNT - 1 && svlPanelMap[ idx + 1 ] == 0 )
      edge = edge | SVL_PANEL_RIGHT;
    if( row != 0 && svlPanelMap[ idx - SVL_COLUMN_COUNT ] == 0 )
      edge = edge | SVL_PANEL_TOP;
    if( row != SVL_ROW_COUNT - 1 && svlPanelMap[ idx + SVL_COLUMN_COUNT ] == 0 )
      edge = edge | SVL_PANEL_BOTTOM;
    return edge;
}

// Pure function of (panel byte, edge, relative cell within the 4x4 panel) -
// a direct re-derivation of upstream's own DrawPanel(), which instead wrote
// these same 16 cells via raw VVram pointer arithmetic. See this file's own
// header comment for why a pure function works cleanly here.
int svlPanelGlyph( int b, int edge, int relX, int relY )
{
    if( relY == 0 )
    {
        if( relX == 0 ) return SVL_CHAR_PANEL + 0;
        if( relX == 3 ) return SVL_CHAR_PANEL + 1;
        if( ( b & SVL_PANEL_TOP ) != 0 ) return SVL_CHAR_PANEL + 4;
        if( ( edge & SVL_PANEL_TOP ) == 0 ) return SVL_CHAR_SPACE;
        return SVL_CHAR_ARROW + 4 + ( relX - 1 );
    }
    if( relY == 3 )
    {
        if( relX == 0 ) return SVL_CHAR_PANEL + 2;
        if( relX == 3 ) return SVL_CHAR_PANEL + 3;
        if( ( b & SVL_PANEL_BOTTOM ) != 0 ) return SVL_CHAR_PANEL + 4;
        if( ( edge & SVL_PANEL_BOTTOM ) == 0 ) return SVL_CHAR_SPACE;
        return SVL_CHAR_ARROW + 6 + ( relX - 1 );
    }
    if( relX == 0 )
    {
        if( ( b & SVL_PANEL_LEFT ) != 0 ) return SVL_CHAR_PANEL + 4;
        if( ( edge & SVL_PANEL_LEFT ) == 0 ) return SVL_CHAR_SPACE;
        return SVL_CHAR_ARROW + 0 + ( relY - 1 ) * 2;
    }
    if( relX == 3 )
    {
        if( ( b & SVL_PANEL_RIGHT ) != 0 ) return SVL_CHAR_PANEL + 4;
        if( ( edge & SVL_PANEL_RIGHT ) == 0 ) return SVL_CHAR_SPACE;
        return SVL_CHAR_ARROW + 1 + ( relY - 1 ) * 2;
    }
    if( ( b & SVL_PANEL_STAR ) != 0 )
      return SVL_CHAR_STAR + ( relY - 1 ) * 2 + ( relX - 1 );
    return SVL_CHAR_SPACE;
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void svlLocateMovable( SvlMovable* pMovable, int b )
{
    int column, row;
    column = b >> 4;
    row = b & 0x0f;
    pMovable->x = svlToCoord( column ) << SVL_COORD_SHIFT;
    pMovable->y = svlToCoord( row ) << SVL_COORD_SHIFT;
}

void svlMoveMovable( SvlMovable* pMovable )
{
    pMovable->x = pMovable->x + pMovable->dx;
    pMovable->y = pMovable->y + pMovable->dy;
}

bool svlCanMoveMovable( SvlMovable* pMovable, int dx, int dy )
{
    int x, y, xMod, yMod;
    x = pMovable->x >> SVL_COORD_SHIFT;
    y = pMovable->y >> SVL_COORD_SHIFT;
    xMod = x & 3;
    if( xMod == 0 )
    {
        if( dy != 0 ) return false;
        if( dx < 0 )
        {
            int column, row, left;
            column = x >> 2;
            if( column == 0 ) return false;
            row = y >> 2;
            left = svlPanelMap[ svlMapIndex( column - 1, row ) ];
            return left != 0 && ( left & SVL_PANEL_RIGHT ) == 0;
        }
        return true;
    }
    if( xMod == 2 )
    {
        if( dy != 0 ) return false;
        if( dx > 0 )
        {
            int column, row, right;
            column = x >> 2;
            if( column == SVL_COLUMN_COUNT - 1 ) return false;
            row = y >> 2;
            right = svlPanelMap[ svlMapIndex( column + 1, row ) ];
            return right != 0 && ( right & SVL_PANEL_LEFT ) == 0;
        }
        return true;
    }
    if( xMod == 3 )
      return dy == 0;
    yMod = y & 3;
    if( yMod == 0 )
    {
        if( dx != 0 ) return false;
        if( dy < 0 )
        {
            int row, column, upper;
            row = y >> 2;
            if( row == 0 ) return false;
            column = x >> 2;
            upper = svlPanelMap[ svlMapIndex( column, row - 1 ) ];
            return upper != 0 && ( upper & SVL_PANEL_BOTTOM ) == 0;
        }
        return true;
    }
    if( yMod == 2 )
    {
        if( dx != 0 ) return false;
        if( dy > 0 )
        {
            int row, column, lower;
            row = y >> 2;
            if( row == SVL_ROW_COUNT - 1 ) return false;
            column = x >> 2;
            lower = svlPanelMap[ svlMapIndex( column, row + 1 ) ];
            return lower != 0 && ( lower & SVL_PANEL_TOP ) == 0;
        }
        return true;
    }
    if( yMod == 3 )
      return dx == 0;
    {
        int row, column, panel;
        row = y >> 2;
        column = x >> 2;
        panel = svlPanelMap[ svlMapIndex( column, row ) ];
        if( dx < 0 ) return ( panel & SVL_PANEL_LEFT ) == 0;
        if( dx > 0 ) return ( panel & SVL_PANEL_RIGHT ) == 0;
        if( dy < 0 ) return ( panel & SVL_PANEL_TOP ) == 0;
        if( dy > 0 ) return ( panel & SVL_PANEL_BOTTOM ) == 0;
        return true;
    }
}

bool svlIsNear( SvlMovable* p1, SvlMovable* p2 )
{
    return
        p1->x + SVL_HIT_RANGE >= p2->x && p2->x + SVL_HIT_RANGE >= p1->x &&
        p1->y + SVL_HIT_RANGE >= p2->y && p2->y + SVL_HIT_RANGE >= p1->y;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void svlHideAllSprites()
{
    int i;
    for( i = 0; i < SVL_SPRITE_END; i = i + 1 )
      svlSprites[ i ].code = SVL_INVALID_PATTERN;
}

void svlShowSprite( SvlMovable* pMovable, int pattern )
{
    svlSprites[ pMovable->sprite ].x = pMovable->x;
    svlSprites[ pMovable->sprite ].y = pMovable->y;
    svlSprites[ pMovable->sprite ].code = pattern;
}

void svlHideSprite( int index )
{
    svlSprites[ index ].code = SVL_INVALID_PATTERN;
}


// -----------------------------------------------------------------------------
//   Rendering support - svlComputeMapVVram()/svlDrawSpritesIntoVVram()/
//   svlDrawAll() (see this file's own header comment for the full-recompute
//   design rationale).
// -----------------------------------------------------------------------------

void svlComputeMapVVram()
{
    int row, col;
    for( row = 0; row < SVL_ROW_COUNT; row = row + 1 )
    {
        for( col = 0; col < SVL_COLUMN_COUNT; col = col + 1 )
        {
            int b, rx, ry;
            b = svlPanelMap[ svlMapIndex( col, row ) ];
            if( b == 0 )
            {
                for( ry = 0; ry < 4; ry = ry + 1 )
                  for( rx = 0; rx < 4; rx = rx + 1 )
                    svlVVram[ row * 4 + ry ][ col * 4 + rx ] = SVL_CHAR_SPACE;
            }
            else
            {
                int edge;
                edge = svlEdge( col, row );
                for( ry = 0; ry < 4; ry = ry + 1 )
                  for( rx = 0; rx < 4; rx = rx + 1 )
                    svlVVram[ row * 4 + ry ][ col * 4 + rx ] = svlPanelGlyph( b, edge, rx, ry );
            }
        }
    }
    if( svlMovingPanel != 0 )
    {
        int rx, ry;
        for( ry = 0; ry < 4; ry = ry + 1 )
          for( rx = 0; rx < 4; rx = rx + 1 )
            svlVVram[ svlPanelY + ry ][ svlPanelX + rx ] = svlPanelGlyph( svlMovingPanel, 0, rx, ry );
    }
}

void svlDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < SVL_SPRITE_END; i = i + 1 )
    {
        if( svlSprites[ i ].code != SVL_INVALID_PATTERN )
        {
            x = svlSprites[ i ].x;
            y = svlSprites[ i ].y;
            c = svlSprites[ i ].code;
            if( c < SVL_CHAR_MAN )
              svlVVram[ y ][ x ] = c;
            else
            {
                svlVVram[ y ][ x ] = c; c = c + 1;
                svlVVram[ y ][ x + 1 ] = c; c = c + 1;
                svlVVram[ y + 1 ][ x ] = c; c = c + 1;
                svlVVram[ y + 1 ][ x + 1 ] = c;
            }
        }
    }
}

void svlDrawAll()
{
    svlComputeMapVVram();
    svlDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into svlStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area) -
//   same design and content encoding as Cracky's own crkStatusChar.
// -----------------------------------------------------------------------------

int svlAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV"
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

int svlPrintC( int page, int col, int c )
{
    svlStatusChar[ page ][ col ] = svlAsciiIndex( c );
    return col + 1;
}

int svlPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = svlPrintC( page, col, s[ i ] );
    return col;
}

void svlPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      svlPrintC( page, col, ' ' );
    else
      svlPrintC( page, col, d1 + '0' );
    svlPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void svlPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        svlPrintC( page, col, ' ' );
        if( d2 == 0 )
          svlPrintC( page, col + 1, ' ' );
        else
          svlPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        svlPrintC( page, col, d1 + '0' );
        svlPrintC( page, col + 1, d2 + '0' );
    }
    svlPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void svlPrintNumber5( int page, int col, int w )
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
          svlPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            svlPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    svlPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching this game's own Status.cpp `LeftX=24` constant exactly -
// LeftX itself, LeftX+2, LeftX+5, LeftX+6 - identical to Cracky's own
// LeftX=24), not an arbitrary local 0-7 offset - see svlStatusChar's own
// header comment for why this changed from the original, too-narrow model.
void svlPrintScore()
{
    svlPrintNumber5( 1, 26, svlScore );
    svlPrintC( 1, 31, '0' );
}

void svlPrintTime()
{
    svlPrintByteNumber3( 5, 29, svlStageTime );
}

void svlPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    svlPrintS( 0, 24, sScore, 5 );
    svlPrintS( 3, 24, sStage, 5 );
    svlPrintByteNumber2( 3, 30, svlCurrentStage + 1 );
    svlPrintS( 5, 24, sTime, 4 );

    // upstream draws a real 2x2 Char_Remain icon (Put2C) here - simplified
    // to plain text digits/blanks throughout, reusing Cracky's own already-
    // established simplification for the identical Put2C-icon situation
    // verbatim (see this file's own header comment).
    if( svlRemainCount > 1 )
    {
        i = svlRemainCount - 1;
        if( i > 2 )
        {
            svlPrintC( 7, 24, ' ' );
            svlPrintC( 7, 25, ' ' );
            svlPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < svlRemainCount - 1; i = i + 1 )
              svlPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    svlPrintScore();
    svlPrintTime();
}

void svlBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    svlOverlayActive = true;
    svlOverlayLen = len;
    svlOverlayPage = page;
    svlOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      svlOverlayText[ i ] = s[ i ];
}

void svlPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    svlBeginOverlay( s, 9, 4, 8 );
}

void svlPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    svlBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int svlMelodyLength( int id )
{
    if( id == SVL_MELODY_LOOSE ) return 3;
    if( id == SVL_MELODY_HIT ) return 17;
    if( id == SVL_MELODY_BEEP ) return 3;
    if( id == SVL_MELODY_MOVE ) return 3;
    if( id == SVL_MELODY_START ) return 23;
    if( id == SVL_MELODY_CLEAR ) return 27;
    if( id == SVL_MELODY_GAMEOVER ) return 27;
    if( id == SVL_MELODY_BGM1 ) return 131;
    if( id == SVL_MELODY_BGM2 ) return 229;
    return 0;
}

int svlMelodyValue( int id, int idx )
{
    if( id == SVL_MELODY_LOOSE ) return svlMelodyLoose[ idx ];
    if( id == SVL_MELODY_HIT ) return svlMelodyHit[ idx ];
    if( id == SVL_MELODY_BEEP ) return svlMelodyBeep[ idx ];
    if( id == SVL_MELODY_MOVE ) return svlMelodyMove[ idx ];
    if( id == SVL_MELODY_START ) return svlMelodyStart[ idx ];
    if( id == SVL_MELODY_CLEAR ) return svlMelodyClear[ idx ];
    if( id == SVL_MELODY_GAMEOVER ) return svlMelodyGameOver[ idx ];
    if( id == SVL_MELODY_BGM1 ) return svlMelodyBgm1[ idx ];
    if( id == SVL_MELODY_BGM2 ) return svlMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/SVL_TEMPO = 300/170 = 1.7647058823529411 real 60Hz ticks - a
// different constant from Cracky's own 1.875, re-derived from Sound.cpp's
// own Tempo=170 (not assumed identical to Cracky's Tempo=160) - see header.
int svlNoteFrames( int length )
{
    return (int)( length * 1.7647058823529411 + 0.5 );
}

void svlStartSeq( int channel, int melodyId )
{
    svlSeqMelody[ channel ] = melodyId;
    svlSeqPos[ channel ] = 0;
    svlSeqWait[ channel ] = 0;
    svlSeqActive[ channel ] = 1;
}

void svlStopSeq( int channel )
{
    svlSeqActive[ channel ] = 0;
    svlSeqMelody[ channel ] = SVL_MELODY_NONE;
}

bool svlSeqPlaying( int channel )
{
    return svlSeqActive[ channel ] != 0;
}

void svlAdvanceOneSeq( int channel )
{
    int length, note;

    if( svlSeqActive[ channel ] == 0 ) return;

    if( svlSeqWait[ channel ] > 0 )
    {
        svlSeqWait[ channel ] = svlSeqWait[ channel ] - 1;
        return;
    }

    length = svlMelodyValue( svlSeqMelody[ channel ], svlSeqPos[ channel ] );
    if( length == 0 )
    {
        svlStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        svlSeqPos[ channel ] = 0;
        length = svlMelodyValue( svlSeqMelody[ channel ], 0 );
    }
    note = svlMelodyValue( svlSeqMelody[ channel ], svlSeqPos[ channel ] + 1 );
    svlSeqPos[ channel ] = svlSeqPos[ channel ] + 2;
    svlSeqWait[ channel ] = svlNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)svlFrequencies[ note - 1 ], (float)svlSeqWait[ channel ] / 60.0 );
}

void svlAdvanceSound()
{
    svlAdvanceOneSeq( 0 );
    svlAdvanceOneSeq( 1 );
    svlAdvanceOneSeq( 2 );
}

void svlStartBgm()
{
    svlStartSeq( 1, SVL_MELODY_BGM1 );
    svlStartSeq( 2, SVL_MELODY_BGM2 );
}

void svlStopBgm()
{
    svlStopSeq( 1 );
    svlStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void svlAddScore( int pts )
{
    svlScore = svlScore + pts;
    svlPrintScore();
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void svlShowMan()
{
    int pattern, seq;
    pattern = svlMan.status & SVL_MOVABLE_PATTERN_MASK;
    seq = ( ( svlMan.x + svlMan.y ) >> SVL_COORD_SHIFT ) & 1;
    pattern = pattern + seq;
    svlShowSprite( &svlMan, SVL_CHAR_MAN + ( pattern << 2 ) );
}

void svlInitMan()
{
    svlMan.sprite = SVL_SPRITE_MAN;
    svlMan.status = SVL_MOVABLE_LIVE;
    svlMan.dx = 0;
    svlMan.dy = 0;
    svlManDir = SVL_DIRECTION_LEFT;
    svlLocateMovable( &svlMan, svlStageStart[ svlStageIndex ] );
    svlShowMan();
}

void svlSlideMan( int dx, int dy )
{
    svlMan.x = svlMan.x + dx;
    svlMan.y = svlMan.y + dy;
    svlShowMan();
}


// -----------------------------------------------------------------------------
//   Stage.cpp - panel sliding (StartMovingPanel()/MovePanel()) - see this
//   file's own header comment for the 4-steps-per-engine-tick derivation.
// -----------------------------------------------------------------------------

void svlStartMovingPanel()
{
    int x, column, y, row, idx, panel, xMod;
    x = svlMan.x >> SVL_COORD_SHIFT;
    column = x >> 2;
    y = svlMan.y >> SVL_COORD_SHIFT;
    row = y >> 2;
    idx = svlMapIndex( column, row );
    panel = svlPanelMap[ idx ];
    xMod = x & 3;
    if( xMod == 0 )
    {
        if( column == 0 ) return;
        if( ( panel & SVL_PANEL_LEFT ) != 0 ) return;
        if( svlPanelMap[ svlMapIndex( column - 1, row ) ] != 0 ) return;
        svlPanelDirection = SVL_DIRECTION_LEFT;
    }
    else if( xMod == 2 )
    {
        if( column == SVL_COLUMN_COUNT - 1 ) return;
        if( ( panel & SVL_PANEL_RIGHT ) != 0 ) return;
        if( svlPanelMap[ svlMapIndex( column + 1, row ) ] != 0 ) return;
        svlPanelDirection = SVL_DIRECTION_RIGHT;
    }
    else
    {
        int yMod;
        yMod = y & 3;
        if( yMod == 0 )
        {
            if( row == 0 ) return;
            if( ( panel & SVL_PANEL_TOP ) != 0 ) return;
            if( svlPanelMap[ svlMapIndex( column, row - 1 ) ] != 0 ) return;
            svlPanelDirection = SVL_DIRECTION_UP;
        }
        else if( yMod == 2 )
        {
            if( row == SVL_ROW_COUNT - 1 ) return;
            if( ( panel & SVL_PANEL_BOTTOM ) != 0 ) return;
            if( svlPanelMap[ svlMapIndex( column, row + 1 ) ] != 0 ) return;
            svlPanelDirection = SVL_DIRECTION_DOWN;
        }
        else
          return;
    }
    svlPanelX = column << 2;
    svlPanelY = row << 2;
    svlMovingPanel = panel;
    svlPanelMap[ idx ] = 0;
    svlMan.status = svlMan.status | SVL_MOVABLE_ON_PANEL;
    svlStartSeq( 0, SVL_MELODY_MOVE );
}

void svlMovePanelStep()
{
    int dx, dy;
    if( svlMovingPanel == 0 ) return;
    if( svlPanelDirection == SVL_DIRECTION_LEFT )
    {
        svlPanelX = svlPanelX - 1;
        dx = -SVL_COORD_RATE; dy = 0;
    }
    else if( svlPanelDirection == SVL_DIRECTION_RIGHT )
    {
        svlPanelX = svlPanelX + 1;
        dx = SVL_COORD_RATE; dy = 0;
    }
    else if( svlPanelDirection == SVL_DIRECTION_UP )
    {
        svlPanelY = svlPanelY - 1;
        dx = 0; dy = -SVL_COORD_RATE;
    }
    else if( svlPanelDirection == SVL_DIRECTION_DOWN )
    {
        svlPanelY = svlPanelY + 1;
        dx = 0; dy = SVL_COORD_RATE;
    }
    else
      return;

    svlSlideMan( dx, dy );
    if( ( ( svlPanelX | svlPanelY ) & 3 ) == 0 )
    {
        int column, row, idx;
        column = svlPanelX >> 2;
        row = svlPanelY >> 2;
        idx = svlMapIndex( column, row );
        svlPanelMap[ idx ] = svlMovingPanel;
        svlMovingPanel = 0;
        svlMan.status = svlMan.status & ~SVL_MOVABLE_ON_PANEL;
    }
}


// -----------------------------------------------------------------------------
//   Man.cpp - movement/input (kept below the panel functions, which it calls)
// -----------------------------------------------------------------------------

bool svlManMoved;
int svlManMoveDx, svlManMoveDy;

void svlTryManDirection( int index, bool pressed )
{
    int candDx, candDy, oldDx, oldDy;
    if( svlManMoved ) return;
    if( !pressed ) return;
    candDx = svlDirDx[ index ];
    candDy = svlDirDy[ index ];
    if( svlCanMoveMovable( &svlMan, candDx, candDy ) )
    {
        svlManDir = index;
        svlManMoveDx = candDx;
        svlManMoveDy = candDy;
        svlManMoved = true;
        return;
    }
    oldDx = svlDirDx[ svlManDir ];
    oldDy = svlDirDy[ svlManDir ];
    if( svlCanMoveMovable( &svlMan, oldDx, oldDy ) )
    {
        svlManMoveDx = oldDx;
        svlManMoveDy = oldDy;
        svlManMoved = true;
    }
}

void svlMoveMan()
{
    if( ( svlMan.status & SVL_MOVABLE_ON_PANEL ) != 0 ) return;

    if( ( ( svlMan.x | svlMan.y ) & SVL_COORD_MASK ) == 0 )
    {
        bool left, right, up, down, fire, anyDir;
        left = isLeftPressed();
        right = isRightPressed();
        up = isUpPressed();
        down = isDownPressed();
        fire = isFirePressed();
        anyDir = left || right || up || down;

        svlManMoved = false;
        svlManMoveDx = 0;
        svlManMoveDy = 0;
        if( anyDir )
        {
            svlTryManDirection( SVL_DIRECTION_LEFT, left );
            svlTryManDirection( SVL_DIRECTION_RIGHT, right );
            svlTryManDirection( SVL_DIRECTION_UP, up );
            svlTryManDirection( SVL_DIRECTION_DOWN, down );
        }

        if( svlManMoved )
        {
            svlMan.dx = svlManMoveDx;
            svlMan.dy = svlManMoveDy;
            svlMan.status = ( svlMan.status & ~SVL_MOVABLE_PATTERN_MASK ) | ( svlManDir << 1 );
        }
        else
        {
            svlMan.dx = 0;
            svlMan.dy = 0;
        }

        if( fire )
        {
            if( !svlKeyOn )
            {
                svlStartMovingPanel();
                svlKeyOn = true;
            }
        }
        else
          svlKeyOn = false;
    }

    svlMoveMovable( &svlMan );
    svlShowMan();

    if( ( ( svlMan.x | svlMan.y ) & SVL_COORD_MASK ) == 0 )
    {
        int x, xMod, y, yMod;
        x = svlMan.x >> SVL_COORD_SHIFT;
        xMod = x & 3;
        y = svlMan.y >> SVL_COORD_SHIFT;
        yMod = y & 3;
        if( xMod == 1 && yMod == 1 )
        {
            int column, row, idx, mapVal;
            column = x >> 2;
            row = y >> 2;
            idx = svlMapIndex( column, row );
            mapVal = svlPanelMap[ idx ];
            if( ( mapVal & SVL_PANEL_STAR ) != 0 )
            {
                svlPanelMap[ idx ] = mapVal & ~SVL_PANEL_STAR;
                svlAddScore( 5 );
                svlStarCount = svlStarCount - 1;
                svlStartSeq( 0, SVL_MELODY_HIT );
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void svlShowMonster( SvlMovable* pMonster )
{
    int status, pattern, seq;
    status = pMonster->status;
    pattern = status & SVL_MOVABLE_PATTERN_MASK;
    if( ( status & SVL_MONSTER_THROUGH ) != 0 )
      pattern = SVL_CHAR_MONSTER_REV + ( pattern << 1 );
    else
    {
        seq = ( ( pMonster->x + pMonster->y ) >> SVL_COORD_SHIFT ) & 1;
        pattern = SVL_CHAR_MONSTER + ( ( pattern + seq ) << 2 );
    }
    svlShowSprite( pMonster, pattern );
}

void svlDecideDirection( SvlMovable* pMonster )
{
    int dirOrder[4];
    int verticalIdx, horizontalIdx;
    int i, dx, dy;
    bool throughable;
    int column, row, panelIdx, panel;

    if( svlAbs( svlMan.x, pMonster->x ) > svlAbs( svlMan.y, pMonster->y ) )
    {
        if( svlMan.x < pMonster->x )
        {
            if( pMonster->dx <= 0 )
            {
                dirOrder[ 0 ] = SVL_DIRECTION_LEFT;
                dirOrder[ 3 ] = SVL_DIRECTION_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                dirOrder[ 2 ] = SVL_DIRECTION_RIGHT;
                dirOrder[ 3 ] = SVL_DIRECTION_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                dirOrder[ 0 ] = SVL_DIRECTION_RIGHT;
                dirOrder[ 3 ] = SVL_DIRECTION_LEFT;
                verticalIdx = 1;
            }
            else
            {
                dirOrder[ 2 ] = SVL_DIRECTION_LEFT;
                dirOrder[ 3 ] = SVL_DIRECTION_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( svlMan.y < pMonster->y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            dirOrder[ verticalIdx ] = SVL_DIRECTION_UP;
            verticalIdx = verticalIdx + 1;
            dirOrder[ verticalIdx ] = SVL_DIRECTION_DOWN;
        }
        else
        {
            dirOrder[ verticalIdx ] = SVL_DIRECTION_DOWN;
            verticalIdx = verticalIdx + 1;
            dirOrder[ verticalIdx ] = SVL_DIRECTION_UP;
        }
    }
    else
    {
        if( svlMan.y < pMonster->y )
        {
            if( pMonster->dy <= 0 )
            {
                dirOrder[ 0 ] = SVL_DIRECTION_UP;
                dirOrder[ 3 ] = SVL_DIRECTION_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                dirOrder[ 2 ] = SVL_DIRECTION_DOWN;
                dirOrder[ 3 ] = SVL_DIRECTION_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                dirOrder[ 0 ] = SVL_DIRECTION_DOWN;
                dirOrder[ 3 ] = SVL_DIRECTION_UP;
                horizontalIdx = 1;
            }
            else
            {
                dirOrder[ 2 ] = SVL_DIRECTION_UP;
                dirOrder[ 3 ] = SVL_DIRECTION_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares `Man.x < pMonster->y` here too (a real upstream
        // quirk, not a transcription slip - kept exactly as-is, matching
        // gameCracky.c's own identical note for its own crkDecideDirection,
        // which has the exact same odd comparison).
        if( ( svlMan.x < pMonster->y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            dirOrder[ horizontalIdx ] = SVL_DIRECTION_LEFT;
            horizontalIdx = horizontalIdx + 1;
            dirOrder[ horizontalIdx ] = SVL_DIRECTION_RIGHT;
        }
        else
        {
            dirOrder[ horizontalIdx ] = SVL_DIRECTION_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            dirOrder[ horizontalIdx ] = SVL_DIRECTION_LEFT;
        }
    }

    throughable = ( pMonster->status & SVL_MONSTER_THROUGH ) != 0;
    column = pMonster->x >> ( SVL_COORD_SHIFT + 2 );
    row = pMonster->y >> ( SVL_COORD_SHIFT + 2 );
    panelIdx = svlMapIndex( column, row );
    panel = svlPanelMap[ panelIdx ];

    for( i = 0; i < 4; i = i + 1 )
    {
        int index, nextColumn, nextRow, confuse, pattern;
        bool canMove;
        index = dirOrder[ i ];
        dx = svlDirDx[ index ];
        dy = svlDirDy[ index ];
        canMove = false;

        if( dx < 0 )
        {
            if( column != 0 && ( panel & SVL_PANEL_LEFT ) == 0 )
            {
                int left;
                left = svlPanelMap[ panelIdx - 1 ];
                canMove = left != 0 && ( left & SVL_PANEL_RIGHT ) == 0;
            }
        }
        else if( dx > 0 )
        {
            if( column != SVL_COLUMN_COUNT - 1 && ( panel & SVL_PANEL_RIGHT ) == 0 )
            {
                int right;
                right = svlPanelMap[ panelIdx + 1 ];
                canMove = right != 0 && ( right & SVL_PANEL_LEFT ) == 0;
            }
        }
        else if( dy < 0 )
        {
            if( row != 0 && ( panel & SVL_PANEL_TOP ) == 0 )
            {
                int upper;
                upper = svlPanelMap[ panelIdx - SVL_COLUMN_COUNT ];
                canMove = upper != 0 && ( upper & SVL_PANEL_BOTTOM ) == 0;
            }
        }
        else if( dy > 0 )
        {
            if( row != SVL_ROW_COUNT - 1 && ( panel & SVL_PANEL_BOTTOM ) == 0 )
            {
                int lower;
                lower = svlPanelMap[ panelIdx + SVL_COLUMN_COUNT ];
                canMove = lower != 0 && ( lower & SVL_PANEL_TOP ) == 0;
            }
        }

        if( canMove )
        {
            if( throughable && i == 0 && panel != 0 )
              pMonster->status = pMonster->status & ~SVL_MONSTER_THROUGH;
        }
        else if( throughable )
        {
            // Real AVR uint8_t-wraparound bug fixed here, not reproduced -
            // see this file's own header comment for the full explanation.
            nextColumn = column + dx;
            nextRow = row + dy;
            canMove = nextColumn >= 0 && nextColumn < SVL_COLUMN_COUNT &&
                      nextRow >= 0 && nextRow < SVL_ROW_COUNT;
        }

        if( canMove )
        {
            pMonster->dx = dx;
            pMonster->dy = dy;
            pattern = index << 1;
            confuse = 0;
            if( i >= 2 ) confuse = SVL_MONSTER_CONFUSE;
            pMonster->status = ( pMonster->status & ~SVL_MOVABLE_PATTERN_MASK ) | pattern | confuse;
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void svlInitMonsters()
{
    int i, sprite;
    svlMonsterCount = svlStageMonsterCount[ svlStageIndex ];
    sprite = SVL_SPRITE_MONSTER;
    for( i = 0; i < svlMonsterCount; i = i + 1 )
    {
        svlMonsters[ i ].status = SVL_MOVABLE_LIVE;
        svlMonsters[ i ].sprite = sprite;
        svlMonsters[ i ].dx = 0;
        svlMonsters[ i ].dy = 0;
        svlLocateMovable( &svlMonsters[ i ], svlStageMonsters[ svlStageIndex ][ i ] );
        svlDecideDirection( &svlMonsters[ i ] );
        svlShowMonster( &svlMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = svlMonsterCount; i < SVL_MAX_MONSTER_COUNT; i = i + 1 )
    {
        svlMonsters[ i ].status = 0;
        svlMonsters[ i ].sprite = sprite;
        svlHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void svlMoveMonsters()
{
    int i;
    svlMonsterClock = svlMonsterClock + 1;
    for( i = 0; i < svlMonsterCount; i = i + 1 )
    {
        int status;
        status = svlMonsters[ i ].status;
        if( ( status & SVL_MOVABLE_LIVE ) != 0 )
        {
            int x, y;
            x = svlMonsters[ i ].x;
            y = svlMonsters[ i ].y;
            if( ( ( x | y ) & SVL_COORD_MASK ) == 0 )
            {
                int cx, cy;
                cx = x >> SVL_COORD_SHIFT;
                cy = y >> SVL_COORD_SHIFT;
                if( ( cx & 3 ) == 1 && ( cy & 3 ) == 1 )
                {
                    if( ( status & SVL_MONSTER_CONFUSE ) != 0 && ( svlRnd() << 2 ) < svlCurrentStage )
                      svlMonsters[ i ].status = ( status & ~SVL_MONSTER_CONFUSE ) | SVL_MONSTER_THROUGH;
                    svlDecideDirection( &svlMonsters[ i ] );
                }
            }
            if( ( svlMonsterClock & 1 ) == 0 || ( svlMonsters[ i ].status & SVL_MONSTER_THROUGH ) == 0 )
              svlMoveMovable( &svlMonsters[ i ] );
            if( ( svlMan.status & SVL_MOVABLE_ON_PANEL ) == 0 && svlIsNear( &svlMonsters[ i ], &svlMan ) )
              svlMan.status = svlMan.status & ~SVL_MOVABLE_LIVE;
            svlShowMonster( &svlMonsters[ i ] );
        }
    }
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage()/InitTrying()
// -----------------------------------------------------------------------------

void svlInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never actually stops the player from continuing past stage 8),
    // reducing MaxTime by 5 (floored at 30) every full wrap - preserved via
    // the same wrap loop upstream uses, matching Cracky's own crkInitStage()
    // precedent for the identical structural shape.
    int i, j;
    svlMaxTime = 60;
    i = 0;
    j = 0;
    while( i < svlCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= SVL_STAGE_COUNT )
        {
            j = 0;
            if( svlMaxTime >= 30 )
              svlMaxTime = svlMaxTime - 5;
        }
    }
    svlStageIndex = j;
}

void svlInitTrying()
{
    int i;

    svlRndIndex = 0;

    svlStageTime = svlMaxTime;
    for( i = 0; i < svlStageMonsterCount[ svlStageIndex ]; i = i + 1 )
      svlStageTime = svlStageTime + 30;

    for( i = 0; i < SVL_PANEL_MAP_SIZE; i = i + 1 )
      svlPanelMap[ i ] = svlStageBytes[ svlStageIndex ][ i ];

    svlHideAllSprites();
    // ClearScreen() upstream - the map area needs no explicit clear here
    // (svlComputeMapVVram() overwrites all 384 cells unconditionally every
    // frame), but svlStatusChar is different: svlPrintStatus() only ever
    // WRITES its own specific label/digit cells, never clears the whole
    // grid first - matching Cracky's own identical fix (see gameCracky.c's
    // own header for the full "stale status text" story), applied here
    // proactively from the start instead of needing a later bug report.
    {
        int p, q;
        for( p = 0; p < 8; p = p + 1 )
          for( q = 0; q < 32; q = q + 1 )
            svlStatusChar[ p ][ q ] = 0;
    }
    svlOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in svlUpdateTitle()) - matches svlOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches svlInitTrying() without going through that transition first.
    // Matches Cracky's own identical defensive reset for crkFullWidthText.
    svlFullWidthText = false;
    svlPrintStatus();

    svlStarCount = 0;
    for( i = 0; i < SVL_PANEL_MAP_SIZE; i = i + 1 )
    {
        if( ( svlPanelMap[ i ] & SVL_PANEL_STAR ) != 0 )
          svlStarCount = svlStarCount + 1;
    }

    svlInitMan();
    svlInitMonsters();
    svlMovingPanel = 0;

    svlDrawAll();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly,
// byte-for-byte matching Cracky's own crkComposeRawByte() (both derived
// from the identical upstream Vram.cpp) - see this file's own header
// comment for why no hardware-orientation transform is applied.
//
// **OR-combines the map layer (mapByte) with the status-text layer
// (textByte) rather than choosing one exclusively**, matching Cracky's own
// identical fix exactly: during the title screen (svlFullWidthText), the
// "SVELLAS" logo bitmap drawn into svlVVram (see svlBeginTitle()) occupies
// real hardware pages 1-2 only, while every status-text element (SCORE/
// MINI/START/CONTINUE/credit) is printed on pages 0/3/5/6/7 - entirely
// disjoint page ranges, so this can never actually blend two real,
// distinct pieces of content together. It just lets the logo (mapByte,
// non-zero only on its own 2 pages) and the text (textByte, non-zero only
// on its own 5 pages) coexist within one composed byte instead of one
// silently excluding the other.
int svlComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < SVL_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = svlVVram[ rawPage * 2 ][ mapX ];
        lower = svlVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = svlCharPattern[ upper * 2 + 0 ];
            lowerByte = svlCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = svlCharPattern[ upper * 2 + 0 ];
            lowerByte = svlCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = svlCharPattern[ upper * 2 + 1 ];
            lowerByte = svlCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = svlCharPattern[ upper * 2 + 1 ];
            lowerByte = svlCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !svlFullWidthText && rawCol < SVL_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // svlStatusChar's own full-width indexing directly - no more "subtract
    // the map width" local-offset math needed, since rawCol/4 already
    // lands on the correct real column either way (whether this is the
    // svlFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96) - matching Cracky's own
    // identical fix exactly.
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = svlStatusChar[ rawPage ][ charCol ];
            textByte = svlAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void svlRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( svlOverlayActive && page == svlOverlayPage &&
                col >= svlOverlayCol * 4 && col < svlOverlayCol * 4 + svlOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - svlOverlayCol * 4 ) / 4;
                sub = ( col - svlOverlayCol * 4 ) % 4;
                value = svlAsciiPattern[ svlAsciiIndex( svlOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = svlComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after a real user-supplied photo of the sibling Cracky game
// running on actual hardware proved the previous version of this function
// was simply wrong.** The earlier version believed upstream's own title-
// screen text collided with the SCORE/STAGE/TIME status labels and had to
// be trimmed/relocated/dropped to fit - "INUFUTO 2026" was dropped
// entirely, "CONTINUE" was truncated to "CONT", and "START" was relocated
// off its real page. Re-reading upstream's real `Status.cpp` (`Title()`)
// line by line shows this diagnosis was backwards: none of that text ever
// collides with anything upstream, because upstream's own Vram address
// space is a genuinely wide 32-char-cell-per-page canvas (see
// svlStatusChar's own header comment) - the status labels occupy only
// columns 24-31 (this game's own `LeftX=24`), and every piece of title-
// screen text sits at columns 8-23, well clear of them. The ROOT problem
// was this port's own `svlStatusChar` being modeled as an 8-column-wide
// grid in the first place - now fixed there, this function is rewritten
// to place everything at upstream's real, literal columns, with
// `svlFullWidthText=true` so svlComposeRawByte() renders the full canvas
// instead of just the narrow status zone.
void svlBeginTitle()
{
    int i, j;

    for( i = 0; i < SVL_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < SVL_VVRAM_WIDTH; j = j + 1 )
        svlVVram[ i ][ j ] = SVL_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        svlStatusChar[ i ][ j ] = 0;
    svlOverlayActive = false;
    svlFullWidthText = true;
    svlHideAllSprites();
    svlStageTime = 0;
    svlPrintStatus();

    // **Restored, after the same real user-supplied hardware photo (of the
    // sibling Cracky game) that proved Cracky's own earlier "purely
    // decorative, drop it" call was wrong - see gameCracky.c's own header
    // comment for the full story.** This is upstream's own real 5-group
    // "SVELLAS" logo bitmap (`Status.cpp`'s `Title()`), drawn directly into
    // svlVVram from svlTitleBytes[] at its own real position: upstream's
    // `VVramFront + VVramWidth*2 + TitleLeft`, where `TitleLeft =
    // (VVramWidth - 4*TitleLength) / 2 = (24 - 20) / 2 = 2` for this game's
    // own `TitleLength=5` (a different TitleLeft than Cracky's own 0, since
    // Cracky's TitleLength=6 makes its own TitleLeft degenerate to 0) - so
    // VVram row 2-5 (matching Cracky's own row placement exactly, both
    // games start their logo at `VVramWidth*2`), starting at VVram column
    // `TitleLeft`(2), not column 0. It's the actual title wordmark, the
    // single biggest, most prominent element on the whole screen, not a
    // throwaway detail - the earlier version of this function replaced it
    // with plain small text reasoning it was "purely decorative", which is
    // exactly the wrong call this whole family of ports made before a real
    // hardware photo corrected it. `svlComposeRawByte()` was updated to
    // OR-combine this VVram content with svlStatusChar's own text layer
    // rather than choosing one exclusively, since the two occupy disjoint
    // page ranges by construction (see that function's own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                svlVVram[ 2 + row ][ 2 + ch * 4 + col ] = svlTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // upstream's own real "MINI" text (separate from the logo bitmap above)
    // - real upstream column: `TitleLeft + 4*TitleLength - 5` where
    // `TitleLeft = (VVramWidth - 4*TitleLength) / 2` and `TitleLength = 5`
    // for this game (a different TitleLength than Cracky's own, so a
    // different literal column: `(24-20)/2 + 20 - 5 = 17`, not Cracky's 18)
    // - re-derived directly from this game's own Status.cpp, not assumed
    // identical to Cracky's.
    {
        int sMini[4] = { 'M', 'I', 'N', 'I' };
        svlPrintS( 3, 17, sMini, 4 );
    }

    // Real upstream column: `Vram + VramRowSize*7 + 12*VramStep` - page 7,
    // col 12, the full 12-character "INUFUTO 2026" (columns 12-23, clear
    // of the status zone's own columns 24-31). Both 'E' (needed for
    // CONTINUE below) and every letter in "INUFUTO 2026" itself ARE
    // present in svlAsciiPattern - the earlier draft's "doesn't fit"/
    // "font can't spell it" reasoning was based on the wrong (8-column)
    // grid model, not a real font or space limitation.
    {
        int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        svlPrintS( 7, 12, sCredit, 12 );
    }

    // Real upstream columns: `ArrowX=8`, START/CONTINUE both at
    // `ArrowX+1=9` (page 5 / page 6 respectively), the cursor itself at
    // col 8 (see svlUpdateTitle() below) - genuinely clear of the status
    // labels' own columns 24-31, so neither string needs truncating or
    // relocating anymore.
    {
        int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        svlPrintS( 5, 9, sStart, 5 );
        svlPrintS( 6, 9, sContinue, 8 );
    }

    svlSelection = 0;
    svlSelectionChanged = true;
    svlPrevLeft = false; svlPrevRight = false; svlPrevUp = false; svlPrevDown = false; svlPrevFire = false;
    svlState = SVL_STATE_TITLE;
}

void svlUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !svlPrevLeft ) || ( right && !svlPrevRight ) ||
                ( up && !svlPrevUp ) || ( down && !svlPrevDown ) );
    justFire = ( fire && !svlPrevFire );
    svlPrevLeft = left; svlPrevRight = right; svlPrevUp = up; svlPrevDown = down; svlPrevFire = fire;

    if( svlSelectionChanged )
    {
        svlSelectionChanged = false;
        // Real upstream columns - page 5 col 8 (START's own cursor), page 6
        // col 8 (CONTINUE's own cursor), matching ArrowX=8 exactly (see
        // svlBeginTitle()'s own header comment).
        if( svlSelection == 0 )
          svlPrintC( 5, 8, '>' );
        else
          svlPrintC( 5, 8, ' ' );
        if( svlSelection == 1 )
          svlPrintC( 6, 8, '>' );
        else
          svlPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        svlFullWidthText = false;
        svlPendingContinue = ( svlSelection == 1 );
        svlScore = 0;
        if( !svlPendingContinue )
          svlCurrentStage = 0;
        svlRemainCount = 3;
        svlInitStage();
        svlInitTrying();
        svlDrawAll();
        svlStartSeq( 1, SVL_MELODY_START );
        svlState = SVL_STATE_START_JINGLE;
        svlRender();
        return;
    }
    if( justDir )
    {
        svlSelection = svlSelection ^ 1;
        svlSelectionChanged = true;
    }
    svlRender();
}

void svlUpdateStartJingle()
{
    // Note: svlMonsterNum/svlTimeDenom are deliberately NOT reset here -
    // see this file's own header comment for the real C++ static-local-
    // initializer-runs-once semantic this preserves.
    if( !svlSeqPlaying( 1 ) )
    {
        svlStartBgm();
        svlState = SVL_STATE_PLAYING;
    }
    svlRender();
}

void svlBeginLose()
{
    svlStopBgm();
    svlAnimStep = 0;
    svlWaitFrames = 0;
    svlState = SVL_STATE_LOSE_ANIM;
}

void svlUpdateLoseAnim()
{
    int patterns[4];
    patterns[ 0 ] = SVL_CHAR_MAN_LEFT;
    patterns[ 1 ] = SVL_CHAR_MAN_DOWN;
    patterns[ 2 ] = SVL_CHAR_MAN_RIGHT;
    patterns[ 3 ] = SVL_CHAR_MAN_UP;

    if( svlWaitFrames > 0 )
    {
        svlWaitFrames = svlWaitFrames - 1;
        svlRender();
        return;
    }

    svlShowSprite( &svlMan, patterns[ svlAnimStep & 3 ] );
    svlDrawAll();
    svlStartSeq( 0, SVL_MELODY_LOOSE );
    svlAnimStep = svlAnimStep + 1;
    svlWaitFrames = svlNoteFrames( 1 );

    if( svlAnimStep >= 8 )
    {
        svlRemainCount = svlRemainCount - 1;
        if( svlRemainCount > 0 )
        {
            svlInitTrying();
            svlDrawAll();
            svlOverlayActive = false;
            svlStartSeq( 1, SVL_MELODY_START );
            svlState = SVL_STATE_START_JINGLE;
        }
        else
        {
            svlPrintGameOver();
            svlStartSeq( 1, SVL_MELODY_GAMEOVER );
            svlState = SVL_STATE_GAMEOVER_JINGLE;
        }
    }
    svlRender();
}

void svlUpdateGameOverJingle()
{
    if( !svlSeqPlaying( 1 ) )
      svlBeginTitle();
    else
      svlRender();
}

void svlBeginClearWait()
{
    svlStopBgm();
    svlWaitFrames = 10;
    svlState = SVL_STATE_CLEAR_WAIT;
}

void svlUpdateClearWait()
{
    if( svlWaitFrames > 0 )
    {
        svlWaitFrames = svlWaitFrames - 1;
        svlRender();
        return;
    }
    svlStartSeq( 1, SVL_MELODY_CLEAR );
    svlState = SVL_STATE_CLEAR_JINGLE;
    svlRender();
}

void svlUpdateClearJingle()
{
    if( !svlSeqPlaying( 1 ) )
    {
        svlWaitFrames = 0;
        svlState = SVL_STATE_BONUS_TALLY;
    }
    svlRender();
}

void svlUpdateBonusTally()
{
    if( svlWaitFrames > 0 )
    {
        svlWaitFrames = svlWaitFrames - 1;
        svlRender();
        return;
    }

    if( svlStageTime >= SVL_BONUS_RATE )
    {
        svlAddScore( 3 );
        svlStageTime = svlStageTime - SVL_BONUS_RATE;
        svlPrintTime();
        svlStartSeq( 0, SVL_MELODY_BEEP );
        // Sound_Beep() itself blocks for the beep's own note duration, and
        // upstream's own outer loop calls a REAL, separate WaitTimer(6) on
        // top of that - reproduced as one combined wait rather than as two
        // separate states, since svlAdvanceSound() runs the beep to
        // completion regardless of exactly how the surrounding wait is
        // structured - see this file's own header comment.
        svlWaitFrames = svlNoteFrames( 1 ) + 6;
        svlRender();
        return;
    }

    svlStageTime = 0;
    svlPrintStatus();
    svlCurrentStage = svlCurrentStage + 1;
    svlInitStage();
    svlInitTrying();
    svlDrawAll();
    svlStartSeq( 1, SVL_MELODY_START );
    svlState = SVL_STATE_START_JINGLE;
    svlRender();
}

// **A real animation-pacing bug, found via live-play verification (a
// Puppeteer panel-slide test using a temporary debug hook to place the man
// at a known slide-trigger cell), not just by inspection.** An earlier
// draft of this function ran all 4 svlMovePanelStep() calls back-to-back
// within the SAME tick a move was freshly started (via svlMoveMan()'s own
// call into svlStartMovingPanel()), then rendered once - showing the
// panel already fully slid into its new position the very first frame the
// slide becomes visible at all. Upstream does NOT do this: tracing
// Main.cpp's own do-while loop precisely (Clock is a plain incrementing
// counter, `(Clock&3)==0` gates MoveMan()/MoveMonsters()/the real
// DrawAll()+WaitTimer() redraw to once every 4 loop iterations, but
// MovePanel() itself is called UNCONDITIONALLY on *every* iteration) shows
// that a freshly-started move's own FIRST step happens in the SAME
// iteration as the tick that started it (right after MoveMan(), before
// that iteration's own render) - so the very first rendered frame of a
// slide only ever shows it 1-of-4 steps in. The remaining 3 steps happen
// immediately afterward, but "quietly" (no WaitTimer() call gates them, so
// they run back-to-back in real time with no frame displayed in between) -
// their effect (a FULLY completed slide) only becomes visible on the
// *next* rendered frame, together with whatever else that next tick's own
// MoveMan()/MoveMonsters() does. So a real player sees a slide as a real,
// if brief, 2-frame animation (1/4-done, then fully complete) - never an
// instant single-frame "snap" the way this function used to render it.
// **Fixed** by restructuring to match that exact interleaving: each call
// first finishes off any move's own 3 leftover "quiet" steps left over
// from whatever the *previous* tick's own single step started, BEFORE
// this tick's own MoveMan()/MoveMonsters()/timer logic runs (exactly
// mirroring upstream's own Clock=4k+1/4k+2/4k+3 iterations, which always
// complete before Clock=4k+4's own logic block) - then this tick runs its
// own logic, then takes exactly ONE new panel step (matching upstream's
// own single per-iteration MovePanel() call bundled with that tick's
// logic), before finally rendering. The alive-check after the 3 leftover
// steps is still just one check for the whole group (not 3 individual
// ones) - safe for the exact same reason already documented for the
// single-step check below: Movable_Live can only change inside
// MoveMonsters(), which only ever runs once per tick, strictly BEFORE any
// of that tick's own panel steps - so it cannot change *during* a run of
// leftover steps either, making one check equivalent to checking after
// each step individually. On a fresh game/life/stage (svlMovingPanel==0
// already), the 3 leftover-step calls are harmless no-ops, matching
// upstream's own first few Clock iterations before any move has ever
// started.
void svlUpdatePlayingTick()
{
    // Finish off whatever's left of a move that was freshly started on the
    // *previous* tick's own single step (see this function's own header
    // comment above for the full derivation).
    svlMovePanelStep();
    svlMovePanelStep();
    svlMovePanelStep();
    if( ( svlMan.status & SVL_MOVABLE_LIVE ) == 0 )
    {
        svlDrawAll();
        svlRender();
        svlBeginLose();
        return;
    }

    svlMoveMan();
    if( svlMonsterNum >= 0 )
    {
        svlMoveMonsters();
        svlMonsterNum = svlMonsterNum - 7;
    }
    svlMonsterNum = svlMonsterNum + 3;

    svlTimeDenom = svlTimeDenom - 1;
    if( svlTimeDenom == 0 )
    {
        svlStageTime = svlStageTime - 1;
        svlTimeDenom = SVL_MAX_TIME_DENOM;
        svlPrintTime();
        if( svlStageTime == 0 )
        {
            svlPrintTimeUp();
            svlDrawAll();
            svlRender();
            svlBeginLose();
            return;
        }
    }

    // This tick's own single panel step - matches upstream's own
    // per-iteration alive-check, which runs after every single
    // MovePanel() call, not just once per group of 4 (see this function's
    // own header comment for why checking once here already yields the
    // identical final answer).
    svlMovePanelStep();
    if( ( svlMan.status & SVL_MOVABLE_LIVE ) == 0 )
    {
        svlDrawAll();
        svlRender();
        svlBeginLose();
        return;
    }

    svlDrawAll();

    if( svlStarCount == 0 )
    {
        svlRender();
        svlBeginClearWait();
        return;
    }

    svlRender();
}

void svlUpdatePlaying()
{
    svlTickCounter = svlTickCounter + 1;
    if( svlTickCounter < SVL_TICK_DIVISOR )
    {
        svlRender();
        return;
    }
    svlTickCounter = 0;
    svlUpdatePlayingTick();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameSvellas_init()
{
    int i;

    svlScore = 0;
    svlCurrentStage = 0;
    svlRemainCount = 3;
    svlStageTime = 0;
    svlRndIndex = 0;

    // These two mirror upstream's own `static sbyte monsterNum = 0;`/
    // `static byte timeDenom = MaxTimeDenom;` locals inside Main() - real
    // C++ function-local statics whose own initializer only ever runs
    // once, ever, in the whole program's lifetime. Initialized here, once,
    // and deliberately never reset anywhere else - see this file's own
    // header comment for the full explanation.
    svlMonsterNum = 0;
    svlTimeDenom = SVL_MAX_TIME_DENOM;
    svlMonsterClock = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        svlSeqActive[ i ] = 0;
        svlSeqMelody[ i ] = SVL_MELODY_NONE;
    }
    svlOverlayActive = false;
    svlTickCounter = 0;
    svlKeyOn = false;
    svlMovingPanel = 0;

    svlBeginTitle();
}

void gameSvellas_update()
{
    svlAdvanceSound();

    if( svlState == SVL_STATE_TITLE )
      svlUpdateTitle();
    else if( svlState == SVL_STATE_START_JINGLE )
      svlUpdateStartJingle();
    else if( svlState == SVL_STATE_PLAYING )
      svlUpdatePlaying();
    else if( svlState == SVL_STATE_LOSE_ANIM )
      svlUpdateLoseAnim();
    else if( svlState == SVL_STATE_GAMEOVER_JINGLE )
      svlUpdateGameOverJingle();
    else if( svlState == SVL_STATE_CLEAR_WAIT )
      svlUpdateClearWait();
    else if( svlState == SVL_STATE_CLEAR_JINGLE )
      svlUpdateClearJingle();
    else if( svlState == SVL_STATE_BONUS_TALLY )
      svlUpdateBonusTally();
}
