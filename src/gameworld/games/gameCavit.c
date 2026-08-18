#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// CAVIT mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_cavit`) - a Boulder-Dash-
// flavored cave digger: walk a 12-column x 6-row cave, digging through soil,
// collecting every treasure box to clear the stage, while dropping rocks on
// chasing "Chaser"/"Ghost" monsters to kill them. Ghosts can occasionally
// phase through un-dug walls to follow you (a real, deliberate mechanic, not
// a bug - see cavGhostCanMove()). 10 hand-authored stages, 3 lives, no
// hi-score at all (upstream's own Main.h/Main.cpp declares no such field,
// not even commented out - unlike the sibling `gameOsotos.c` port, which
// found a genuinely commented-out one; this game never had one to begin
// with, so none is added here either).
//
// Built the same way as the sibling `gameCracky.c`/`gameOsotos.c` ports from
// this same upstream author/engine ("Cate engine") - see gameCracky.c's own
// header for the shared methodology (real SSD1306 streamed one raw byte at a
// time via `Locate()`+`SendOledData()`, no framebuffer; a real 60Hz SysTick
// frame limiter, `Timer.cpp`'s `WaitTimer(t)`; only 4 directions + 1 action
// button, `ScanKeys.h`'s `Keys_Button0`, mapped onto `tinyJoypadShim.h`'s
// existing isXPressed() functions with no new shim needed - isFire2Pressed()
// goes unused here, matching every sibling in this family).
//
// **No hardware display-orientation transform, on purpose** - the same
// already-settled finding as Cracky/Osotos (see gameCracky.c's own header
// for the real investigation and the user-supplied reference photo that
// settled it): `Oled.cpp` here uses the identical `SegRemap`/`ComScanDec`
// commands for the identical real-panel-mounting reason, with nothing to
// correct for in a from-scratch software recreation. `cavComposeRawByte()`
// draws directly at its own `(col,page)` - no mirror, no flip, no bit
// reversal, no lookup table.
//
// **Rendering takes a real shortcut past upstream's own incremental VVram
// Back/Front dual-buffer + dirty-diff `Backup[]` scheme**, matching this
// project's own standing "always redraw the full frame" rule harder than
// Cracky's own port did: rather than porting the incremental buffer, this
// port keeps `cavTerrainMap[]` (a `MapWidth*MapHeight` byte array, exactly
// mirroring upstream's own `TerrainMap[]`) as the single source of truth,
// mutated at the exact same real gameplay sites upstream mutates it (man
// digging through a cell, a rock passing through/landing, a box/rock
// occupying a cell), and *recomputes the whole VVram grid fresh from it
// every single frame* (`cavRenderVVramFromState()`) - the same "faithfully
// copy an intricate stateful *algorithm*, not the stateful *cache*" choice
// already used for Cracky's own `crkMapToVVram()`. This is possible because
// `DrawTerrain(x,y,terrain)` in the real source is a pure function of the
// terrain byte (a glyph-lookup table keyed by the byte's own wall-bits
// nibble) - it never needs to know what was drawn last frame. Every call
// site that upstream uses purely to *push a display update* in response to
// a terrain mutation (there are many - MoveMan, TestRocks, MoveRocks, ...)
// is therefore simply dropped in this port: only the *state* mutation is
// kept, since the fresh per-frame recompute already reflects it automatically.
//
// **One real, deliberate cosmetic simplification within that same
// shortcut**: `MoveMan()`'s own mid-cell-transition branch calls
// `DrawTerrainBytes(x,y, MidChars+(direction<<1))` - a small 16-entry table
// of "half-dug" corner glyphs shown for exactly one movement-event
// (~8 raw ticks) while the player's sprite visually overlaps a soil cell
// it hasn't fully entered yet. Traced through by hand: this glyph is purely
// decorative (the *terrain state* driving collision/wall-bit logic is
// identical either way - only which glyph gets drawn for that one brief
// transitional frame differs), and reproducing it would need a second,
// short-lived overlay mechanism layered on top of the terrain-recompute
// shortcut above for a cosmetic detail visible for well under a quarter
// second. Dropped - the cell just renders as still-solid-soil for that one
// beat instead of the special "you're digging in" glyph, then flips to the
// correct dug-out look exactly on schedule once the player reaches full-
// cell alignment (the same "effort/fidelity tradeoff for a purely
// decorative, non-gameplay detail" precedent used throughout this whole
// project, e.g. Space Attack's dropped attract-screen slide-in).
//
// **The title screen's own text now uses upstream's real, wide 32-char-cell
// canvas, matching gameCracky.c's own already-fixed reference design** -
// see `cavStatusChar`'s own header comment for the full story of why an
// earlier version of this port (like nearly every sibling in this whole
// Cate-engine batch) modeled the status-text grid as a too-narrow `[8][8]`,
// confining ALL title-screen text into the same 8-column zone the live
// SCORE/STAGE/TIME labels also use. That earlier model was **meticulously
// re-verified this session and found to still have two real, undisclosed
// rendering bugs**, on top of the one (STAGE/MINI) it had already
// documented and accepted:
// 1. **"START" silently overwrote the live "TIME" value's own first
//    digit.** Confirmed via live testing (Puppeteer/WebGL): the title
//    screen showed "START 0" on one row instead of a clean "START" /
//    "TIME  0" split. Root cause: with everything crammed into the old
//    8-column-wide grid, "START" (5 chars starting at local col1) and
//    `cavPrintTime()`'s own value (3 digits starting at local col5)
//    shared the exact same page (5) and column range purely as an
//    artifact of the cramped model - not present upstream, whose own real
//    `LeftX=24`-based status columns (24-31) never share a page/column
//    with `ArrowX=8`-based menu text (cols 8-16) at all.
// 2. **A genuine out-of-bounds array read**, found by direct inspection
//    once the OOB-read audit below turned attention to every array index
//    derived from user-controllable/state-derived values in this file:
//    a fixed (landed) rock's own terrain cell ORs `CAV_TERRAIN_ROCK`(4)
//    onto whatever base terrain type was already there, giving a combined
//    type value of 4-7 - upstream's own `DrawTerrain()` is *never* actually
//    invoked with the rock bit already combined into the type (its own
//    wall-bits-computation loop runs before rocks/boxes are ever applied
//    to the map, and a rock's own icon is drawn via a separate `VPut2CXY`
//    overlay that bypasses the solid-codes lookup entirely) - but this
//    port's own "recompute the whole grid fresh every frame" shortcut
//    (see above) re-evaluates *every* cell's terrain type every frame,
//    including rock-occupied ones, and would read `cavSolidCodes[]` up to
//    16 elements past its real 12-entry bound for such a cell. See
//    `cavRenderVVramFromState()`'s own inline comment for the fix (masking
//    the rock bit out of the type used for glyph selection) - the visible
//    result was never actually wrong (the rock's own icon, drawn moments
//    later in the same function, always overwrote the garbage before it
//    was ever displayed), but the out-of-bounds read itself was real.
//
// **Fixed the same way as gameCracky.c**: `cavStatusChar` widened to
// `[8][32]` (matching upstream's real Vram address space - VramStep=4 real
// pixels/cell, 128/4=32 cells/page), a new `cavFullWidthText` flag (true
// only during `CAV_STATE_TITLE`) lets `cavComposeRawByte()` read the status
// grid across the *entire* screen width instead of just columns 96-127
// while the title screen is showing, and `cavBeginTitle()` places every
// piece of text at upstream's own real, literal columns (`Status.cpp`'s
// `Title()`: the "CAVIT" logo at VVram-derived column 2, "MINI" at column
// 17, "START"/"CONTINUE" at column 9 with the shared cursor at column 8,
// the "INUFUTO 2026" credit at column 12) - none of which collide with the
// status labels' own real columns 24-31 anymore, so nothing needs
// trimming, relocating, or dropping: "CONTINUE" is spelled out in full
// (not truncated to "CONTINU"), and the credit line - dropped entirely by
// an earlier version of this port for lack of room - is restored.
// `cavInitTrying()` (the real gameplay-entry point) clears the full
// 32-column grid and resets `cavFullWidthText` to false as a defensive
// belt-and-suspenders guard, matching `crkInitTrying()`'s own identical
// pattern - without the full-width clear, stale title-screen text from
// columns 8-23 would otherwise bleed into gameplay the same way this exact
// bug class has already been found and fixed in Cracky itself.
//
// **A second, later architectural fix - restoring the real "CAVIT" logo
// bitmap itself, matching the identical fix subsequently applied to
// gameCracky.c's own "CRACKY" wordmark**: the widening above was real and
// correct, but it still only carried a plain-text "CAVIT" substitute
// (`sCavit`, reasoned at the time as "purely decorative, matching Cracky's
// own identical simplification") - the same wrong judgment call
// independently made (and then corrected) in Cracky's own port: upstream's
// real `Title()` draws a genuine 20x4-VVram-cell hand-authored pixel-art
// wordmark (`TitleBytes[]`), not throwaway filler - it's the single
// biggest, most prominent element on the whole title screen. Restored in
// `cavBeginTitle()` by drawing `cavTitleBytes[]` (byte-diff-verified
// against the real upstream table via a small script) directly into
// `cavVVram` at VVram rows 2-5, columns 2-21 (`TitleLeft=(24-4*5)/2=2`,
// matching upstream's own `VVramFront + VVramWidth*2 + TitleLeft` offset
// exactly - real hardware pages 1-2). `cavComposeRawByte()` was updated to
// OR-combine this VVram/map byte with `cavStatusChar`'s own text byte
// instead of choosing one exclusively (previously: during
// `cavFullWidthText`, the map layer was skipped entirely, which is exactly
// why the earlier plain-text substitute was needed in the first place) -
// safe since the logo and every status-text element occupy disjoint
// column ranges even on the one page (1) they'd otherwise share (the logo
// spans columns 2-21, `cavPrintScore()`'s own value starts at column 26).
// Confirmed `cavCharPattern[]`'s own "logo" range (indices 0-15, the first
// 32 bytes of the table) is already byte-identical to upstream's own
// `CharPattern[]` "logo" section via the same script - no data-table fix
// was needed there, only the drawing loop and the compose-byte function.
//
// **A related, deliberately-restored fidelity fix**: `cavBeginTitle()` now
// resets `cavStageTime = 0` before drawing the title screen, matching a
// real, user-confirmed bug fix already applied to Cracky ("on game over,
// the time value remains visible on titlescreen") - upstream's own
// `Title()` never touches `StageTime` at all, so on real hardware the
// post-game-over title screen would show whatever stale countdown value
// was left over from the moment of death; Cracky's own fix (backed by a
// direct user report) established that this reads as a bug rather than a
// faithful quirk worth preserving, so the same explicit reset is applied
// here for consistency across the whole family rather than deferring to
// upstream's own literal (but user-disliked) behavior.
//
// **The blocking upstream control flow rewritten as an explicit frame-
// stepped state machine**, the usual treatment: CAV_STATE_TITLE, CAV_STATE_
// START_JINGLE (the blocking `Sound_Start()` held before play begins, then
// `StartBGM()`), CAV_STATE_PLAYING, CAV_STATE_LOSE_ANIM (`LooseMan()`'s own
// 8-step blink-and-beep loop), CAV_STATE_GAMEOVER_JINGLE, CAV_STATE_
// CLEAR_JINGLE (upstream calls `Sound_Clear()` immediately after `StopBGM()`
// with no separate wait state first, unlike Cracky's own sibling port which
// needed one - confirmed directly against this game's own real `Main.cpp`,
// not assumed identical to Cracky), and CAV_STATE_BONUS_TALLY (the real
// `while(StageTime>=BonusRate){AddScore(3);...Sound_Beep();}` loop,
// converted to one decrement+beep per real tick, matching this project's
// own HollowSeeker/Ardumania bonus-tally precedent).
//
// **A genuinely two-tier real-time model, not flattened to one rate** -
// the single riskiest structural decision in this port, and a real
// departure from every prior Cate-engine port in this project (which all
// only ever needed one flat tick divisor). Upstream's own main loop runs
// `MoveRocks()` on *every* raw iteration, completely unthrottled, while
// `MoveMan()`/`MoveChasers()`/`MoveGhosts()`/the whole timer/scoring logic
// only run when `(Clock&3)==0`, and the real `WaitTimer(1)` pacing calls
// only fire on that same `(Clock&3)==0` iteration (once) plus a *second*
// extra wait specifically on `(Clock&7)==0` - alternating real-tick costs
// of 2,1,2,1,... per "logic interval". Traced through by hand (see the
// exact iteration-by-iteration derivation this session worked out): between
// any two consecutive logic-intervals, `MoveRocks()` always gets called
// exactly 4 times, regardless of which of the two alternating real-tick
// costs that interval itself needed - so rocks fall a real, deliberate 4x
// faster (in raw step-accumulator terms) than the player/monsters move,
// the core Boulder-Dash "falling rocks are fast and dangerous" feel.
// Reproduced with `cavIntervalIndex`/`cavFrameAccum`, an accumulator
// requiring 2 real engine frames on even interval indices and 1 on odd
// ones (averaging 1.5 real frames/interval, ~40Hz at this engine's native
// 60fps - matching the derived real hardware rate), with each interval
// running the full logic body once and `cavMoveRocks()` a flat
// `CAV_ROCK_SUBTICKS`(4) times in a row. A deliberate, documented
// simplification of upstream's own more intricate real interleaving
// (which splits those 4 rock-move calls as 1-before-this-interval's-draw
// plus 3-trailing-into-the-next-interval's-own-logic) - functionally
// indistinguishable for gameplay purposes, since `MoveRocks()` doesn't
// interact with same-tick ordering in any way a player could notice.
//
// **A real byte-wraparound-reliant comparison, ported explicitly rather
// than silently changed** - the same "AVR-implicit-narrow-type-reliance"
// bug family already extensively documented in this project's own
// CLAUDE.md, just via yet another distinct mechanism (an explicit
// `static_cast<byte>` truncation this time, not an implicit one).
// `TestRocks()`'s own "is the player standing directly under this rock"
// check is `static_cast<byte>(man.x - pFixedRock->x + Size/2) < Size` -
// the subtraction can go negative, and upstream deliberately casts it to
// a real 8-bit byte first, relying on the wraparound to produce the
// intended asymmetric 2-unit test window. Reproduced with an explicit
// `& 0xFF` mask (`cavTestRocks()`) rather than a plain signed comparison,
// which would silently test a different (symmetric, and wrong) window.
//
// **A second, unrelated real bug found and fixed by inspection before
// ever compiling**: `Ghost.cpp`'s own `CanMove()` bounds-checks a
// candidate cell with `if (mapX >= MapWidth || mapY >= MapHeight) return
// false;` - correct on real hardware only because `mapX`/`mapY` are
// unsigned `byte`s, so a leftward/upward move off the map edge wraps to a
// *large positive* value that the `>=` check still catches. This port's
// own `cavCurrentTerrain(x,y)` is the single, centralized place every
// terrain lookup in the whole file funnels through - given a genuinely
// negative coordinate (Vircon32 ints don't wrap the way AVR's unsigned
// byte does), the original `x < MapWidth` check alone would have
// incorrectly treated it as in-bounds. Fixed once, centrally, by adding
// the missing `x >= 0 && y >= 0` half of the check directly inside
// `cavCurrentTerrain()` itself - covers every caller in the file
// (Chaser's own `CanMove()` never had an explicit bounds check of its own
// at all, relying entirely on `CurrentTerrain()`'s own, so it benefits
// from this same central fix automatically). Ghost's own explicit
// early-return bounds check was *also* kept (now redundant but harmless)
// for structural fidelity with the real source.
//
// **A third real bug, in `Rock.cpp`'s own `OnHitRock()` combo-rate
// escalation, deliberately preserved rather than "fixed" except at the one
// genuinely unsafe point**: `rate` can escalate to 4 (one past the real
// 4-entry `Values[]`/`Char_Point`-offset table upstream indexes with it,
// via `if (rate < MaxRate+1)` - an upstream off-by-one that lets a single
// falling rock chain-kill enough monsters in one continuous fall to reach
// index 4). On real AVR hardware this silently over-reads a few bytes of
// adjacent PROGMEM (harmless); on Vircon32 it's a genuine out-of-bounds
// array read. The *glyph* selection (`Char_Point + (rate<<2)`) is left
// completely unclamped, since `rate=4` there just happens to land on
// `Char_Loose`'s own valid CharPattern glyph data (an in-bounds, if
// visually "wrong", reuse - a real, harmless upstream quirk, kept
// faithfully). Only the `Values[]`-equivalent score-table lookup
// (`cavPointValues[]`) is clamped to index 3, the smallest possible fix
// that removes the genuine memory-safety risk without changing anything
// a player could ever see (the score simply stops escalating past the
// table's own real 80-point cap instead of reading garbage).
//
// **Sound**: same real 3-tone-channel tracker as Cracky/Osotos
// (`Sound.cpp`), routed the same way onto `md_playTone(freqHz,
// durationSeconds)` (this engine's own genuinely multi-voice channel -
// see this project's CLAUDE.md for why that's safe here). Each melody is
// upstream's own literal [duration,note] byte-pair data, byte-diff-
// extracted via a small Python script (evaluating the real `NoteLength`/
// `Scale` enum expressions directly, not hand-computed) rather than
// transcribed by hand - `Frequencies[]`/the `Scale` enum values are
// byte-for-byte identical to Cracky's own copy (confirmed via the same
// script), reused verbatim. This game's own `Tempo` is 180 (not Cracky's
// 160), giving a slightly faster real note-advance rate
// (`(600/2)/180 ≈ 1.667` real 60Hz ticks/note, vs Cracky's `1.875`) -
// `cavNoteFrames()` uses this game's own real derived constant, not
// Cracky's. Three independent frame-stepped sequencer slots (0=one-shot
// SFX, 1=jingle/BGM-voice-A, 2=BGM-voice-B), advancing every real engine
// frame regardless of the coarser gameplay throttle above - matching
// upstream's own real structure (`SoundHandler()` runs off the same 60Hz
// SysTick as gameplay, never itself throttled by `Clock&3`).
// =============================================================================

// -----------------------------------------------------------------------------
//   Direction / Movable / Terrain / Chars constants
// -----------------------------------------------------------------------------

#define CAV_DIR_RIGHT 0
#define CAV_DIR_LEFT 2
#define CAV_DIR_DOWN 4
#define CAV_DIR_UP 6

#define CAV_MOVABLE_LIVE 0x01
#define CAV_MOVABLE_DIR_MASK 0x06
#define CAV_MOVABLE_CAN_MOVE 0x08
#define CAV_STEP_MASK 7

#define CAV_MAP_SHIFT 1
#define CAV_MAP_RATE 2
#define CAV_MAP_MASK ( CAV_MAP_RATE - 1 )

#define CAV_TERRAIN_SPACE 0
#define CAV_TERRAIN_SOIL 1
#define CAV_TERRAIN_WALL 2
#define CAV_TERRAIN_ROCK 4
#define CAV_TERRAIN_MASK 0x0f
#define CAV_WALL_LEFT 0x10
#define CAV_WALL_RIGHT 0x20
#define CAV_WALL_TOP 0x40
#define CAV_WALL_BOTTOM 0x80
#define CAV_WALL_ALL 0xf0

#define CAV_MAP_WIDTH 12
#define CAV_MAP_HEIGHT 7

#define CAV_STAGE_COUNT 10
#define CAV_MAX_BOX_COUNT 5
#define CAV_MAX_ROCK_COUNT 4
#define CAV_MAX_CHASER_COUNT 4
#define CAV_MAX_GHOST_COUNT 2
#define CAV_MAX_FALLING_ROCKS 4
#define CAV_MAX_POINTS 4

#define CAV_CHAR_SPACE 0x00
#define CAV_CHAR_SOIL 0x10
#define CAV_CHAR_BEDROCK 0x12
#define CAV_CHAR_WALL 0x13
#define CAV_CHAR_MAN 0x1C
#define CAV_CHAR_GHOST 0x5C
#define CAV_CHAR_CHASER 0x9C
#define CAV_CHAR_ROCK 0xA0
#define CAV_CHAR_POINT 0xA4
#define CAV_CHAR_LOOSE 0xB4
#define CAV_CHAR_BOX 0xBC
#define CAV_CHAR_END 0xC0

#define CAV_CHAR_WALL_SPACE ( CAV_CHAR_WALL + 0 )
#define CAV_CHAR_WALL_LEFT ( CAV_CHAR_WALL + 1 )
#define CAV_CHAR_WALL_RIGHT ( CAV_CHAR_WALL + 2 )
#define CAV_CHAR_WALL_TOP ( CAV_CHAR_WALL + 3 )
#define CAV_CHAR_WALL_BOTTOM ( CAV_CHAR_WALL + 4 )
#define CAV_CHAR_WALL_LEFTTOP ( CAV_CHAR_WALL + 5 )
#define CAV_CHAR_WALL_RIGHTTOP ( CAV_CHAR_WALL + 6 )
#define CAV_CHAR_WALL_LEFTBOTTOM ( CAV_CHAR_WALL + 7 )
#define CAV_CHAR_WALL_RIGHTBOTTOM ( CAV_CHAR_WALL + 8 )

#define CAV_GHOST_THROUGH 0x10
#define CAV_GHOST_WAIT 0x20

#define CAV_VVRAM_WIDTH 24
#define CAV_VVRAM_HEIGHT 16
#define CAV_STAGE_TOP 1

#define CAV_SPRITE_GHOST 0
#define CAV_SPRITE_CHASER 2
#define CAV_SPRITE_MAN 6
#define CAV_SPRITE_ROCK 7
#define CAV_SPRITE_POINT 11
#define CAV_SPRITE_COUNT 15
#define CAV_INVALID_CODE 255

#define CAV_FIXED_NONE 0
#define CAV_FIXED_EXIST 1

#define CAV_HIT_RANGE 1
#define CAV_SHORT_RANGE 1

#define CAV_MAN_START_X 22

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (matching
//   upstream's own enum exactly, same convention as gameCracky.c).
// -----------------------------------------------------------------------------

#define CAV_N8 6
#define CAV_N8L 8
#define CAV_N8R 4
#define CAV_N8P ( CAV_N8 * 3 / 2 )
#define CAV_N4 ( CAV_N8 * 2 )
#define CAV_N4P ( CAV_N4 * 3 / 2 )
#define CAV_N2 ( CAV_N4 * 2 )
#define CAV_N2P ( CAV_N2 * 3 / 2 )
#define CAV_N1 ( CAV_N2 * 2 )
#define CAV_N16 ( CAV_N8 / 2 )

#define CAV_E2 1
#define CAV_F2 2
#define CAV_F2S 3
#define CAV_G2 4
#define CAV_G2S 5
#define CAV_A2 6
#define CAV_A2S 7
#define CAV_B2 8
#define CAV_C3 9
#define CAV_C3S 10
#define CAV_D3 11
#define CAV_D3S 12
#define CAV_E3 13
#define CAV_F3 14
#define CAV_F3S 15
#define CAV_G3 16
#define CAV_G3S 17
#define CAV_A3 18
#define CAV_A3S 19
#define CAV_B3 20
#define CAV_C4 21
#define CAV_C4S 22
#define CAV_D4 23
#define CAV_D4S 24
#define CAV_E4 25
#define CAV_F4 26
#define CAV_F4S 27
#define CAV_G4 28
#define CAV_G4S 29
#define CAV_A4 30
#define CAV_A4S 31
#define CAV_B4 32
#define CAV_C5 33
#define CAV_C5S 34
#define CAV_D5 35
#define CAV_D5S 36
#define CAV_E5 37
#define CAV_F5 38
#define CAV_F5S 39
#define CAV_G5 40

#define CAV_MELODY_NONE 0
#define CAV_MELODY_LOOSE 1
#define CAV_MELODY_HIT 2
#define CAV_MELODY_BEEP 3
#define CAV_MELODY_GET 4
#define CAV_MELODY_START 5
#define CAV_MELODY_CLEAR 6
#define CAV_MELODY_GAMEOVER 7
#define CAV_MELODY_BGM1 8
#define CAV_MELODY_BGM2 9

#define CAV_MAX_TIME_DENOM 50
#define CAV_BONUS_RATE 3
#define CAV_ROCK_SUBTICKS 4

#define CAV_STATE_TITLE 0
#define CAV_STATE_START_JINGLE 1
#define CAV_STATE_PLAYING 2
#define CAV_STATE_LOSE_ANIM 3
#define CAV_STATE_GAMEOVER_JINGLE 4
#define CAV_STATE_CLEAR_JINGLE 5
#define CAV_STATE_BONUS_TALLY 6

// -----------------------------------------------------------------------------
//   Structs
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y;
    int dx, dy;
    int sprite;
    int status;
    int step;
} CavMovable;

typedef struct
{
    int x, y;
    int status;
} CavFixed;

typedef struct
{
    int x, y;
    int code;
} CavSprite;

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied.
// -----------------------------------------------------------------------------

int cavDirectionBytes[8] = { 1, 0, -1, 0, 0, 1, 0, -1 };
int cavWallBits[4] = { CAV_WALL_RIGHT, CAV_WALL_LEFT, CAV_WALL_BOTTOM, CAV_WALL_TOP };

int cavWallSpaceCodes[64] = {
    CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_SPACE,
    CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_SPACE,
    CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_RIGHT, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_RIGHT,
    CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_RIGHT, CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_RIGHT,
    CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_SPACE,
    CAV_CHAR_WALL_LEFTTOP, CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_SPACE,
    CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_RIGHTTOP, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_RIGHT,
    CAV_CHAR_WALL_LEFTTOP, CAV_CHAR_WALL_RIGHTTOP, CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_RIGHT,
    CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_BOTTOM, CAV_CHAR_WALL_BOTTOM,
    CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_LEFTBOTTOM, CAV_CHAR_WALL_BOTTOM,
    CAV_CHAR_WALL_SPACE, CAV_CHAR_WALL_RIGHT, CAV_CHAR_WALL_BOTTOM, CAV_CHAR_WALL_RIGHTBOTTOM,
    CAV_CHAR_WALL_LEFT, CAV_CHAR_WALL_RIGHT, CAV_CHAR_WALL_LEFTBOTTOM, CAV_CHAR_WALL_RIGHTBOTTOM,
    CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_BOTTOM, CAV_CHAR_WALL_BOTTOM,
    CAV_CHAR_WALL_LEFTTOP, CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_LEFTBOTTOM, CAV_CHAR_WALL_BOTTOM,
    CAV_CHAR_WALL_TOP, CAV_CHAR_WALL_RIGHTTOP, CAV_CHAR_WALL_BOTTOM, CAV_CHAR_WALL_RIGHTBOTTOM,
    CAV_CHAR_WALL_LEFTTOP, CAV_CHAR_WALL_RIGHTTOP, CAV_CHAR_WALL_LEFTBOTTOM, CAV_CHAR_WALL_RIGHTBOTTOM,
};

int cavSolidCodes[12] = {
    CAV_CHAR_SOIL + 0, CAV_CHAR_SOIL + 1, CAV_CHAR_SOIL + 1, CAV_CHAR_SOIL + 0,
    CAV_CHAR_BEDROCK, CAV_CHAR_BEDROCK, CAV_CHAR_BEDROCK, CAV_CHAR_BEDROCK,
    0, 0, 0, 0,
};

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph - confirmed
// byte-for-byte identical to gameCracky.c's own crkAsciiPattern via script.
int cavAsciiPattern[108] = {
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

// CharPattern - 192 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int cavCharPattern[384] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00, 0x00, 0x33,
    0x33, 0x33, 0xcc, 0x33, 0xff, 0x33, 0x00, 0xcc, 0x33, 0xcc,
    0xcc, 0xcc, 0xff, 0xcc, 0x00, 0xff, 0x33, 0xff, 0xcc, 0xff,
    0xff, 0xff, 0xdf, 0xf7, 0xf7, 0xfb, 0xe7, 0xbd, 0x00, 0x00,
    0x05, 0x00, 0x00, 0xa0, 0x10, 0x10, 0x08, 0x08, 0x05, 0x01,
    0x10, 0xb0, 0x0d, 0x08, 0x08, 0x58, 0x00, 0xee, 0x0a, 0x00,
    0xc0, 0x34, 0x8c, 0x00, 0x00, 0xee, 0x0a, 0x00, 0x20, 0xbd,
    0x20, 0x00, 0x00, 0xa0, 0xee, 0x00, 0x00, 0xc8, 0x43, 0x0c,
    0x00, 0xa0, 0xee, 0x00, 0x00, 0x02, 0xdb, 0x02, 0xc0, 0xcc,
    0x88, 0x26, 0x10, 0x10, 0x00, 0x31, 0xc0, 0xcc, 0xac, 0xc4,
    0x10, 0x10, 0x20, 0x00, 0x80, 0x80, 0x00, 0xc8, 0x30, 0x33,
    0x11, 0x46, 0x80, 0x80, 0x40, 0x00, 0x30, 0x33, 0x53, 0x32,
    0x00, 0xee, 0x1a, 0x1f, 0xc0, 0x34, 0x9d, 0x00, 0x00, 0xe0,
    0xae, 0x00, 0x00, 0xc0, 0x29, 0x72, 0xf1, 0xa1, 0xee, 0x00,
    0x00, 0xd9, 0x43, 0x0c, 0x00, 0xea, 0x0e, 0x00, 0x27, 0x92,
    0x0c, 0x00, 0xc0, 0xcc, 0x88, 0x26, 0x5e, 0x54, 0x03, 0x31,
    0x80, 0x88, 0x00, 0x88, 0x30, 0x31, 0xe9, 0x18, 0xa7, 0xa2,
    0x0c, 0xc8, 0x30, 0x33, 0x11, 0x46, 0xc0, 0xc8, 0x79, 0x81,
    0x10, 0x11, 0x00, 0x11, 0x80, 0xfe, 0xfa, 0x8a, 0x00, 0xfb,
    0x37, 0x01, 0x00, 0xec, 0xe4, 0x04, 0x20, 0xff, 0x7f, 0x23,
    0xa8, 0xaf, 0xef, 0x08, 0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e,
    0xce, 0x00, 0x32, 0xf7, 0xff, 0x02, 0x80, 0xbe, 0xbe, 0x8e,
    0x00, 0x73, 0x7f, 0x03, 0x00, 0x6c, 0x6c, 0x0c, 0x10, 0xf7,
    0xff, 0x17, 0xe8, 0xef, 0xef, 0x08, 0x30, 0xf7, 0x37, 0x00,
    0xc0, 0xce, 0xce, 0x00, 0x71, 0xff, 0x7f, 0x01, 0x7f, 0x01,
    0x05, 0x75, 0xff, 0x06, 0xc8, 0xfe, 0xff, 0x13, 0x1b, 0xfb,
    0xdf, 0x00, 0x80, 0xdc, 0x57, 0x50, 0x10, 0xf7, 0xef, 0x8c,
    0x60, 0xff, 0xbf, 0xb1, 0x31, 0xff, 0xcd, 0x08, 0x00, 0xfd,
    0x17, 0x14, 0x14, 0xf7, 0xcf, 0x08, 0xc8, 0xff, 0x3f, 0x39,
    0x39, 0xff, 0x8d, 0x00, 0x80, 0xfd, 0x17, 0x10, 0x10, 0xf7,
    0xcf, 0x08, 0xc8, 0xff, 0x3f, 0x31, 0x31, 0xff, 0x8d, 0x00,
    0x80, 0xfd, 0x60, 0x69, 0x68, 0x69, 0xc0, 0x76, 0x7f, 0xc6,
    0xc0, 0xd6, 0xef, 0x84, 0xb7, 0xf7, 0xb6, 0x35, 0xe4, 0xc0,
    0xc2, 0x00, 0x32, 0x02, 0x61, 0x69, 0x24, 0xcc, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x8c, 0xce, 0xc2, 0x00, 0x00, 0x03,
    0x61, 0x69, 0xa4, 0xc4, 0xc2, 0x00, 0x21, 0x01, 0x61, 0x69,
    0x00, 0x31, 0x2c, 0x03, 0x00, 0x50, 0x77, 0x00, 0x62, 0x88,
    0xcc, 0x0c, 0x13, 0x00, 0x01, 0x01, 0xc4, 0x6a, 0xff, 0xce,
    0x63, 0x0d, 0xdd, 0x64,
};

// TitleBytes - upstream's own real "CAVIT" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 letters x 4x4 VVram-cell glyph indices each
// (80 values total), byte-diff-verified against the real upstream source.
// Every value here is a valid index into cavCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table, confirmed
// byte-identical to upstream's own CharPattern[]'s "logo" section) - the
// exact same shared block-pattern palette every other map tile in this
// game already draws through, just reused here to build a big pixel-art
// wordmark instead of a wall/floor tile. See cavBeginTitle()'s own comment
// for why this replaces the earlier plain-text "CAVIT" substitute
// (mirroring the identical fix already applied to gameCracky.c's own
// "CRACKY" logo).
int cavTitleBytes[80] = {
    0x00, 0x0e, 0x05, 0x0b, 0x0c, 0x03, 0x00, 0x00,
    0x04, 0x0b, 0x00, 0x0a, 0x00, 0x04, 0x05, 0x01,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x03, 0x00, 0x0f, 0x04, 0x0b, 0x08, 0x07,
    0x00, 0x0d, 0x0e, 0x01, 0x00, 0x04, 0x05, 0x00,
    0x04, 0x0d, 0x07, 0x01, 0x00, 0x0c, 0x03, 0x00,
    0x00, 0x0c, 0x03, 0x00, 0x04, 0x05, 0x05, 0x01,
    0x04, 0x0d, 0x07, 0x01, 0x00, 0x0c, 0x03, 0x00,
    0x00, 0x0c, 0x03, 0x00, 0x00, 0x04, 0x01, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40)
// - byte-for-byte identical to gameCracky.c's own crkFrequencies.
int cavFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int cavMelodyLoose[3] = { 1, CAV_A3, 0 };

int cavMelodyHit[17] = {
    1, CAV_F4, 1, CAV_G4, 1, CAV_A4, 1, CAV_B4, 1, CAV_C5,
    1, CAV_D5, 1, CAV_E5, 1, CAV_F5, 0,
};

int cavMelodyBeep[3] = { 1, CAV_A4, 0 };

int cavMelodyGet[13] = {
    1, CAV_C4, 1, CAV_C4S, 1, CAV_D4, 1, CAV_F4, 1, CAV_A4,
    1, CAV_C5, 0,
};

int cavMelodyStart[23] = {
    CAV_N4, CAV_C4, CAV_N4, CAV_E4, CAV_N8, CAV_G4, CAV_N4, CAV_E4, CAV_N4, CAV_F4,
    CAV_N8, CAV_F4, CAV_N4, CAV_A4, CAV_N8, CAV_C5, CAV_N4P, CAV_A4, CAV_N2P, CAV_C5,
    CAV_N4, 0, 0,
};

int cavMelodyClear[29] = {
    CAV_N8, CAV_A4, CAV_N8, CAV_A4, CAV_N8, CAV_G4, CAV_N8, CAV_F4, CAV_N8, CAV_G4,
    CAV_N4, CAV_A4, CAV_N4, CAV_B4, CAV_N8, CAV_B4, CAV_N8, CAV_A4, CAV_N8, CAV_G4,
    CAV_N8, CAV_A4, CAV_N4, CAV_B4, ( CAV_N8 + CAV_N2 ), CAV_C5, CAV_N2, 0,
    0,
};

int cavMelodyGameOver[21] = {
    CAV_N8, CAV_C5, CAV_N8, CAV_F4, CAV_N8, CAV_A4, CAV_N8, CAV_E4, CAV_N8, CAV_G4,
    CAV_N8, CAV_A4, CAV_N8, CAV_B4, CAV_N8, CAV_C5, CAV_N2P, CAV_C5, CAV_N4, 0,
    0,
};

int cavMelodyBgm1[119] = {
    CAV_N4, CAV_C4, CAV_N4, CAV_G4, CAV_N8, CAV_C4, CAV_N4, CAV_G4, CAV_N4, CAV_A4,
    CAV_N8, CAV_A4, CAV_N8, CAV_G4, CAV_N8, CAV_G4, CAV_N8, CAV_F4, CAV_N8, CAV_F4,
    CAV_N8, CAV_E4, CAV_N8, CAV_E4, CAV_N4, CAV_D4, CAV_N4, CAV_D4, CAV_N8, CAV_D4,
    CAV_N4, CAV_E4, CAV_N4P, CAV_D4, CAV_N2P, 0, CAV_N4, CAV_C4, CAV_N4, CAV_G4,
    CAV_N8, CAV_C4, CAV_N4, CAV_G4, CAV_N4, CAV_A4, CAV_N8, CAV_A4, CAV_N8, CAV_G4,
    CAV_N8, CAV_G4, CAV_N8, CAV_F4, CAV_N8, CAV_F4, CAV_N8, CAV_E4, CAV_N8, CAV_E4,
    CAV_N4, CAV_F4, CAV_N4, CAV_F4, CAV_N8, CAV_F4, CAV_N4, CAV_A4, CAV_N4P, CAV_G4,
    CAV_N2P, 0, CAV_N8, CAV_E4, CAV_N8, CAV_E4, CAV_N8, CAV_E4, CAV_N4, CAV_E4,
    CAV_N8, CAV_E4, CAV_N4, CAV_A4, CAV_N8, CAV_D4, CAV_N8, CAV_D4, CAV_N8, CAV_D4,
    CAV_N4, CAV_D4, CAV_N8, CAV_D4, CAV_N4, CAV_G4, CAV_N8, 0, CAV_N8, CAV_A4,
    CAV_N8, 0, CAV_N8, CAV_G4, CAV_N8, 0, CAV_N8, CAV_F4, CAV_N8, 0,
    CAV_N8, CAV_E4, CAV_N4, CAV_D4, CAV_N4, CAV_E4, CAV_N2, CAV_C4, 255,
};

int cavMelodyBgm2[173] = {
    CAV_N4, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4,
    CAV_N8, 0, CAV_N8, CAV_C4, CAV_N4, CAV_A3, CAV_N8, 0, CAV_N8, CAV_A3,
    CAV_N8, 0, CAV_N8, CAV_A3, CAV_N8, 0, CAV_N8, CAV_A3, CAV_N4, CAV_D4,
    CAV_N8, 0, CAV_N8, CAV_D4, CAV_N8, 0, CAV_N8, CAV_D4, CAV_N8, 0,
    CAV_N8, CAV_D4, CAV_N4, CAV_G3, CAV_N8, 0, CAV_N8, CAV_G3, CAV_N8, 0,
    CAV_N8, CAV_G3, CAV_N8, 0, CAV_N8, CAV_G3, CAV_N4, CAV_C4, CAV_N8, 0,
    CAV_N8, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4,
    CAV_N4, CAV_A3, CAV_N8, 0, CAV_N8, CAV_A3, CAV_N8, 0, CAV_N8, CAV_A3,
    CAV_N8, 0, CAV_N8, CAV_A3, CAV_N4, CAV_F3, CAV_N8, 0, CAV_N8, CAV_F3,
    CAV_N8, 0, CAV_N8, CAV_G3, CAV_N8, 0, CAV_N8, CAV_G3, CAV_N4, CAV_C4,
    CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0,
    CAV_N8, CAV_C4, CAV_N4, CAV_C4, CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0,
    CAV_N8, CAV_A3, CAV_N8, 0, CAV_N8, CAV_A3, CAV_N4, CAV_D4, CAV_N8, 0,
    CAV_N8, CAV_D4, CAV_N8, 0, CAV_N8, CAV_G3, CAV_N8, 0, CAV_N8, CAV_G3,
    CAV_N8, 0, CAV_N8, CAV_F3, CAV_N8, 0, CAV_N8, CAV_F3, CAV_N8, 0,
    CAV_N8, CAV_G3, CAV_N8, 0, CAV_N8, CAV_G3, CAV_N8, CAV_C4, CAV_N8, 0,
    CAV_N8, CAV_E3, CAV_N8, 0, CAV_N8, CAV_C4, CAV_N8, 0, CAV_N8, CAV_E3,
    CAV_N8, 0, 255,
};

int cavPointValues[4] = { 10, 20, 40, 80 };

// Stage data - each stage's own real byte stream (terrain + boxes + fixed
// rocks + chasers + ghosts, self-describing counts, self-terminating well
// before the padding), padded to 40 with harmless trailing zeros. Values
// transcribed directly from `Stages.cpp`'s own hex literals, cross-checked
// against the upstream repo's own `stage_data_out.txt` (a pre-decoded
// decimal dump of the exact same bytes) rather than hand-converted.
int cavStageBytes[10][40] = {
    {
        85, 85, 85, 21, 85, 68, 85, 85, 69, 85,
        85, 69, 21, 89, 68, 21, 24, 68, 4, 33,
        145, 131, 37, 2, 49, 129, 2, 69, 133, 1,
        164, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    {
        85, 85, 85, 0, 21, 81, 85, 8, 21, 80,
        25, 17, 85, 25, 80, 85, 0, 85, 4, 65,
        21, 133, 181, 3, 145, 66, 98, 3, 1, 113,
        69, 2, 3, 179, 0, 0, 0, 0, 0, 0,
    },
    {
        85, 85, 85, 0, 5, 84, 149, 68, 81, 149,
        85, 85, 101, 88, 17, 21, 4, 17, 5, 67,
        115, 179, 20, 52, 3, 66, 98, 146, 4, 1,
        69, 117, 149, 2, 97, 181, 0, 0, 0, 0,
    },
    {
        85, 85, 84, 80, 85, 69, 168, 84, 69, 144,
        85, 84, 85, 84, 80, 1, 24, 65, 4, 35,
        179, 5, 133, 2, 128, 66, 3, 68, 21, 165,
        2, 17, 162, 0, 0, 0, 0, 0, 0, 0,
    },
    {
        85, 85, 85, 170, 2, 69, 1, 168, 21, 170,
        85, 21, 85, 85, 21, 21, 168, 85, 4, 2,
        130, 5, 181, 2, 161, 66, 2, 81, 53, 2,
        18, 180, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    {
        85, 81, 84, 169, 101, 85, 0, 144, 85, 105,
        149, 69, 4, 162, 65, 101, 129, 81, 4, 83,
        20, 133, 181, 2, 80, 128, 2, 34, 85, 2,
        36, 149, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    {
        85, 69, 81, 133, 85, 85, 137, 2, 2, 161,
        0, 86, 148, 85, 84, 164, 102, 21, 4, 17,
        36, 85, 133, 3, 96, 144, 132, 3, 34, 82,
        178, 2, 4, 181, 0, 0, 0, 0, 0, 0,
    },
    {
        69, 85, 85, 85, 24, 145, 84, 25, 150, 0,
        88, 150, 21, 25, 85, 1, 17, 1, 4, 34,
        115, 180, 5, 3, 32, 65, 145, 4, 114, 116,
        53, 85, 2, 37, 181, 0, 0, 0, 0, 0,
    },
    {
        69, 84, 68, 85, 85, 85, 153, 136, 8, 85,
        85, 85, 137, 136, 9, 69, 85, 85, 4, 34,
        83, 132, 181, 4, 32, 64, 128, 160, 2, 178,
        36, 2, 180, 37, 0, 0, 0, 0, 0, 0,
    },
    {
        69, 90, 169, 149, 69, 73, 128, 89, 8, 149,
        89, 153, 128, 81, 149, 144, 6, 84, 4, 81,
        177, 35, 37, 4, 32, 97, 130, 84, 3, 34,
        36, 133, 2, 178, 5, 0, 0, 0, 0, 0,
    },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int cavScore;
int cavCurrentStage;
int cavRemainCount;
int cavStageTime;
int cavMonsterNum;
int cavTimeDenom;
int cavStageIndex;
int cavStageOffset;
int cavBoxCount;

int cavTerrainMap[CAV_MAP_WIDTH * CAV_MAP_HEIGHT];
int cavVVram[CAV_VVRAM_HEIGHT][CAV_VVRAM_WIDTH];
CavSprite cavSprites[CAV_SPRITE_COUNT];

CavMovable cavMan;
int cavPrevMapX, cavPrevMapY, cavNextMapX, cavNextMapY, cavCharOffset;

CavMovable cavChasers[CAV_MAX_CHASER_COUNT];
CavMovable cavGhosts[CAV_MAX_GHOST_COUNT];
int cavChaserPosOffset, cavGhostPosOffset;
int cavGhostRndIndex;
bool cavThroughable;

int cavGhostRndTable[16] = { 11, 8, 9, 4, 4, 12, 0, 12, 13, 11, 0, 6, 13, 12, 5, 15 };

CavFixed cavBoxes[CAV_MAX_BOX_COUNT];
CavFixed cavFixedRocks[CAV_MAX_ROCK_COUNT];
CavMovable cavFallingRocks[CAV_MAX_FALLING_ROCKS];
CavMovable cavPoints[CAV_MAX_POINTS];

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space exactly (VramRowSize=0x100 selects the page in the
// high byte, VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32
// cells per row) - a pattern index into cavAsciiPattern (0 = space) per
// cell. **Widened from an original, wrong `[8][8]`** - see gameCracky.c's
// own header comment on `crkStatusChar` for the full story: a real user-
// supplied hardware photo of Cracky's own title screen proved that narrow
// model wrong for this whole Cate-engine batch, not just Cracky. Upstream's
// own status labels (SCORE/STAGE/TIME/lives) really are confined to columns
// 24-31 (`Status.cpp`'s own `LeftX=24`), but the title screen's own text
// (the logo, "MINI", "START"/"CONTINUE", the "INUFUTO 2026" credit) lives
// at upstream's real columns 2-23, via the exact same shared PrintC()/
// PrintS() mechanism at different column arguments - not a separate,
// narrower grid. Cramming everything into the old 8-cell-wide grid (reusing
// columns that the live status labels ALSO used) is exactly what caused
// this port's own real, empirically-confirmed bugs: "START" silently
// overwriting the live "TIME" value's own first digit, and "MINI"
// overwriting "STAGE" - see cavComposeRawByte()'s own header for how this
// wider grid actually reaches the screen.
int cavStatusChar[8][32];

// Set true only while on the title screen (CAV_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its initial
// ClearScreen(), and instead drives the ENTIRE screen (not just the status
// zone) through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range. When true, cavComposeRawByte()
// reads cavStatusChar across the full width instead of just columns 24-31,
// letting the title screen use that same wide real estate instead of being
// artificially confined to the narrow status-only zone.
bool cavFullWidthText;

bool cavRemainIcon1Active, cavRemainIcon2Active;

bool cavOverlayActive;
int cavOverlayText[10];
int cavOverlayLen, cavOverlayPage, cavOverlayCol;

int cavSeqMelody[3];
int cavSeqPos[3];
int cavSeqWait[3];
int cavSeqActive[3];

int cavIntervalIndex, cavFrameAccum;

int cavState, cavWaitFrames, cavAnimStep, cavSelection;
bool cavSelectionChanged, cavPendingContinue;
bool cavPrevLeft, cavPrevRight, cavPrevUp, cavPrevDown, cavPrevFire;

int cavPrintByteValue, cavPrintWordValue;
bool cavPrintZeroVisibleFlag;

// -----------------------------------------------------------------------------
//   Math / direction helpers
// -----------------------------------------------------------------------------

int cavAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}

int cavDirDx( int direction )
{
    return cavDirectionBytes[ direction ];
}

int cavDirDy( int direction )
{
    return cavDirectionBytes[ direction + 1 ];
}

int cavDecodeX( int b )
{
    return ( b >> 3 ) & 0xfe;
}

int cavDecodeY( int b )
{
    return ( ( b & 15 ) << 1 ) + 2;
}

// Centralized terrain lookup - see header comment for the real bounds-check
// bug this fixes once here (upstream's own unsigned-byte-wraparound
// reliance for negative coordinates, which Vircon32's plain signed ints
// don't replicate).
int cavCurrentTerrain( int x, int y )
{
    if( x >= 0 && x < CAV_MAP_WIDTH && y >= 0 && y < CAV_MAP_HEIGHT )
      return cavTerrainMap[ y * CAV_MAP_WIDTH + x ];
    return CAV_TERRAIN_WALL;
}

// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void cavSetDirection( CavMovable* pMovable, int direction )
{
    pMovable->dx = cavDirDx( direction );
    pMovable->dy = cavDirDy( direction );
    pMovable->status = ( pMovable->status & ~CAV_MOVABLE_DIR_MASK ) | direction;
}

bool cavCanChangeDirection( CavMovable* pMovable )
{
    return ( pMovable->step & CAV_STEP_MASK ) == 0 && ( ( pMovable->x | pMovable->y ) & CAV_MAP_MASK ) == 0;
}

bool cavMoveMovable( CavMovable* pMovable )
{
    if( ( pMovable->status & CAV_MOVABLE_CAN_MOVE ) != 0 )
    {
        pMovable->step = pMovable->step + 1;
        if( ( pMovable->step & CAV_STEP_MASK ) == 0 )
        {
            pMovable->x = pMovable->x + pMovable->dx;
            pMovable->y = pMovable->y + pMovable->dy;
            return true;
        }
    }
    return false;
}

bool cavIsNearXY( CavMovable* pMovable, int x, int y )
{
    return
        pMovable->x + CAV_HIT_RANGE >= x && x + CAV_HIT_RANGE >= pMovable->x &&
        pMovable->y + CAV_HIT_RANGE >= y && y + CAV_HIT_RANGE >= pMovable->y;
}

bool cavIsNear( CavMovable* p1, CavMovable* p2 )
{
    return cavIsNearXY( p1, p2->x, p2->y );
}

bool cavIsVeryNear( CavMovable* p1, CavMovable* p2 )
{
    return
        p1->x + CAV_SHORT_RANGE >= p2->x && p2->x + CAV_SHORT_RANGE >= p1->x &&
        p1->y + CAV_SHORT_RANGE >= p2->y && p2->y + CAV_SHORT_RANGE >= p1->y;
}

// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void cavHideAllSprites()
{
    int i;
    for( i = 0; i < CAV_SPRITE_COUNT; i = i + 1 )
      cavSprites[ i ].code = CAV_INVALID_CODE;
}

void cavShowSprite( CavMovable* pMovable, int code )
{
    cavSprites[ pMovable->sprite ].x = pMovable->x;
    cavSprites[ pMovable->sprite ].y = pMovable->y + CAV_STAGE_TOP;
    cavSprites[ pMovable->sprite ].code = code;
}

void cavHideSprite( int index )
{
    cavSprites[ index ].code = CAV_INVALID_CODE;
}

void cavPutIcon( int x, int y, int c )
{
    int vy, vx;
    vy = y + CAV_STAGE_TOP;
    vx = x;
    cavVVram[ vy ][ vx ] = c; c = c + 1;
    cavVVram[ vy ][ vx + 1 ] = c; c = c + 1;
    cavVVram[ vy + 1 ][ vx ] = c; c = c + 1;
    cavVVram[ vy + 1 ][ vx + 1 ] = c;
}

// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int cavMelodyLength( int id )
{
    if( id == CAV_MELODY_LOOSE ) return 3;
    if( id == CAV_MELODY_HIT ) return 17;
    if( id == CAV_MELODY_BEEP ) return 3;
    if( id == CAV_MELODY_GET ) return 13;
    if( id == CAV_MELODY_START ) return 23;
    if( id == CAV_MELODY_CLEAR ) return 29;
    if( id == CAV_MELODY_GAMEOVER ) return 21;
    if( id == CAV_MELODY_BGM1 ) return 119;
    if( id == CAV_MELODY_BGM2 ) return 173;
    return 0;
}

int cavMelodyValue( int id, int idx )
{
    if( id == CAV_MELODY_LOOSE ) return cavMelodyLoose[ idx ];
    if( id == CAV_MELODY_HIT ) return cavMelodyHit[ idx ];
    if( id == CAV_MELODY_BEEP ) return cavMelodyBeep[ idx ];
    if( id == CAV_MELODY_GET ) return cavMelodyGet[ idx ];
    if( id == CAV_MELODY_START ) return cavMelodyStart[ idx ];
    if( id == CAV_MELODY_CLEAR ) return cavMelodyClear[ idx ];
    if( id == CAV_MELODY_GAMEOVER ) return cavMelodyGameOver[ idx ];
    if( id == CAV_MELODY_BGM1 ) return cavMelodyBgm1[ idx ];
    if( id == CAV_MELODY_BGM2 ) return cavMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/180 = 1.6666.. real 60Hz ticks (Tempo=180 here, vs Cracky's 160)
// - see header comment.
int cavNoteFrames( int length )
{
    return (int)( length * 1.6666667 + 0.5 );
}

void cavStartSeq( int channel, int melodyId )
{
    cavSeqMelody[ channel ] = melodyId;
    cavSeqPos[ channel ] = 0;
    cavSeqWait[ channel ] = 0;
    cavSeqActive[ channel ] = 1;
}

void cavStopSeq( int channel )
{
    cavSeqActive[ channel ] = 0;
    cavSeqMelody[ channel ] = CAV_MELODY_NONE;
}

bool cavSeqPlaying( int channel )
{
    return cavSeqActive[ channel ] != 0;
}

void cavAdvanceOneSeq( int channel )
{
    int length, note;

    if( cavSeqActive[ channel ] == 0 ) return;

    if( cavSeqWait[ channel ] > 0 )
    {
        cavSeqWait[ channel ] = cavSeqWait[ channel ] - 1;
        return;
    }

    length = cavMelodyValue( cavSeqMelody[ channel ], cavSeqPos[ channel ] );
    if( length == 0 )
    {
        cavStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        cavSeqPos[ channel ] = 0;
        length = cavMelodyValue( cavSeqMelody[ channel ], 0 );
    }
    note = cavMelodyValue( cavSeqMelody[ channel ], cavSeqPos[ channel ] + 1 );
    cavSeqPos[ channel ] = cavSeqPos[ channel ] + 2;
    cavSeqWait[ channel ] = cavNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)cavFrequencies[ note - 1 ], (float)cavSeqWait[ channel ] / 60.0 );
}

void cavAdvanceSound()
{
    cavAdvanceOneSeq( 0 );
    cavAdvanceOneSeq( 1 );
    cavAdvanceOneSeq( 2 );
}

void cavStartBgm()
{
    cavStartSeq( 1, CAV_MELODY_BGM1 );
    cavStartSeq( 2, CAV_MELODY_BGM2 );
}

void cavStopBgm()
{
    cavStopSeq( 1 );
    cavStopSeq( 2 );
    md_stopTone();
}

void cavSoundLoose() { cavStartSeq( 0, CAV_MELODY_LOOSE ); }
void cavSoundHit() { cavStartSeq( 0, CAV_MELODY_HIT ); }
void cavSoundBeep() { cavStartSeq( 0, CAV_MELODY_BEEP ); }
void cavSoundGet() { cavStartSeq( 0, CAV_MELODY_GET ); }

// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp
// -----------------------------------------------------------------------------

int cavAsciiIndex( int c )
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

int cavPrintC( int page, int col, int c )
{
    cavStatusChar[ page ][ col ] = cavAsciiIndex( c );
    return col + 1;
}

int cavPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = cavPrintC( page, col, s[ i ] );
    return col;
}

int cavPrintDigitB( int page, int col, int n )
{
    int c;
    c = cavPrintByteValue / n;
    cavPrintByteValue = cavPrintByteValue % n;
    if( c == 0 )
    {
        if( cavPrintZeroVisibleFlag ) c = '0';
        else c = ' ';
    }
    else
    {
        cavPrintZeroVisibleFlag = true;
        c = c + '0';
    }
    return cavPrintC( page, col, c );
}

int cavPrintByteNumber3( int page, int col, int b )
{
    cavPrintZeroVisibleFlag = false;
    cavPrintByteValue = b;
    col = cavPrintDigitB( page, col, 100 );
    col = cavPrintDigitB( page, col, 10 );
    col = cavPrintC( page, col, cavPrintByteValue + '0' );
    return col;
}

int cavPrintByteNumber2( int page, int col, int b )
{
    cavPrintZeroVisibleFlag = false;
    cavPrintByteValue = b;
    col = cavPrintDigitB( page, col, 10 );
    col = cavPrintC( page, col, cavPrintByteValue + '0' );
    return col;
}

int cavPrintDigitW( int page, int col, int n )
{
    int c;
    c = cavPrintWordValue / n;
    cavPrintWordValue = cavPrintWordValue % n;
    if( c == 0 )
    {
        if( cavPrintZeroVisibleFlag ) c = '0';
        else c = ' ';
    }
    else
    {
        cavPrintZeroVisibleFlag = true;
        c = c + '0';
    }
    return cavPrintC( page, col, c );
}

int cavPrintNumber5( int page, int col, int w )
{
    cavPrintZeroVisibleFlag = false;
    cavPrintWordValue = w;
    col = cavPrintDigitW( page, col, 10000 );
    col = cavPrintDigitW( page, col, 1000 );
    col = cavPrintDigitW( page, col, 100 );
    col = cavPrintDigitW( page, col, 10 );
    col = cavPrintC( page, col, cavPrintWordValue + '0' );
    return col;
}

// All column arguments below are real upstream character-cell columns
// (Status.cpp's own `LeftX=24` constant, plus LeftX+2/+5/+6 etc - matching
// gameCracky.c's own already-fixed crkPrintScore/crkPrintTime/
// crkPrintStatus exactly), not an arbitrary local 0-7 offset into a
// too-narrow grid - see cavStatusChar's own header comment for why this
// changed from the original, too-narrow model.
void cavPrintScore()
{
    int col;
    col = cavPrintNumber5( 1, 26, cavScore );
    cavPrintC( 1, col, '0' );
}

void cavPrintTime()
{
    cavPrintByteNumber3( 5, 29, cavStageTime );
}

void cavPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int cnt;

    cavPrintS( 0, 24, sScore, 5 );
    cavPrintS( 3, 24, sStage, 5 );
    cavPrintByteNumber2( 3, 30, cavCurrentStage + 1 );
    cavPrintS( 5, 24, sTime, 4 );

    cavRemainIcon1Active = false;
    cavRemainIcon2Active = false;
    if( cavRemainCount > 1 )
    {
        cnt = cavRemainCount - 1;
        if( cnt > 2 )
        {
            // upstream's own dead branch - RemainCount never exceeds 3 (it's
            // only ever reset to 3, then decremented), so cnt=RemainCount-1
            // never actually exceeds 2. Kept as an unreachable safety
            // fallback rather than removed.
            cavPrintC( 7, 26, cnt + '0' );
        }
        else
        {
            if( cnt >= 1 ) cavRemainIcon1Active = true;
            if( cnt >= 2 ) cavRemainIcon2Active = true;
        }
    }

    cavPrintScore();
    cavPrintTime();
}

void cavBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    cavOverlayActive = true;
    cavOverlayLen = len;
    cavOverlayPage = page;
    cavOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      cavOverlayText[ i ] = s[ i ];
}

void cavPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    cavBeginOverlay( s, 9, 4, 8 );
}

void cavPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    cavBeginOverlay( s, 7, 4, 9 );
}

// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void cavAddScore( int pts )
{
    cavScore = cavScore + pts;
    cavPrintScore();
}

// -----------------------------------------------------------------------------
//   Point.cpp
// -----------------------------------------------------------------------------

void cavInitPoints()
{
    int i;
    for( i = 0; i < CAV_MAX_POINTS; i = i + 1 )
    {
        cavPoints[ i ].status = 0;
        cavPoints[ i ].sprite = CAV_SPRITE_POINT + i;
        cavHideSprite( CAV_SPRITE_POINT + i );
    }
}

void cavStartPoint( int x, int y, int rate )
{
    int i, rateIdx;
    rateIdx = rate;
    if( rateIdx > 3 ) rateIdx = 3;
    cavAddScore( cavPointValues[ rateIdx ] );
    for( i = 0; i < CAV_MAX_POINTS; i = i + 1 )
    {
        if( ( cavPoints[ i ].status & CAV_MOVABLE_LIVE ) != 0 ) continue;
        cavPoints[ i ].status = cavPoints[ i ].status | CAV_MOVABLE_LIVE;
        cavPoints[ i ].x = x;
        cavPoints[ i ].y = y;
        cavPoints[ i ].dx = 6;
        cavShowSprite( &cavPoints[ i ], CAV_CHAR_POINT + ( rate << 2 ) );
        return;
    }
}

void cavUpdatePoints()
{
    int i;
    for( i = 0; i < CAV_MAX_POINTS; i = i + 1 )
    {
        if( ( cavPoints[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        if( cavPoints[ i ].dx == 0 )
        {
            cavPoints[ i ].status = 0;
            cavHideSprite( cavPoints[ i ].sprite );
        }
        else
          cavPoints[ i ].dx = cavPoints[ i ].dx - 1;
    }
}

// -----------------------------------------------------------------------------
//   Box.cpp
// -----------------------------------------------------------------------------

void cavHitBox()
{
    int i;
    for( i = 0; i < CAV_MAX_BOX_COUNT; i = i + 1 )
    {
        if( cavBoxes[ i ].status == 0 ) continue;
        if( cavBoxes[ i ].x == cavMan.x && cavBoxes[ i ].y == cavMan.y )
        {
            cavBoxes[ i ].status = 0;
            cavSoundGet();
            cavStartPoint( cavMan.x, cavMan.y, 0 );
            cavBoxCount = cavBoxCount - 1;
            return;
        }
    }
}

// -----------------------------------------------------------------------------
//   Monster.cpp - adjacency helpers, shared by Chaser and Ghost.
// -----------------------------------------------------------------------------

bool cavAdjacentChaser( int selfIndex, int x, int y )
{
    int i;
    for( i = 0; i < CAV_MAX_CHASER_COUNT; i = i + 1 )
    {
        if( ( cavChasers[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        if( i != selfIndex && cavIsNearXY( &cavChasers[ i ], x, y ) ) return true;
    }
    return false;
}

bool cavAdjacentGhost( int selfIndex, int x, int y )
{
    int i;
    for( i = 0; i < CAV_MAX_GHOST_COUNT; i = i + 1 )
    {
        if( ( cavGhosts[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        if( i != selfIndex && cavIsNearXY( &cavGhosts[ i ], x, y ) ) return true;
    }
    return false;
}

bool cavAdjacentRock( int x, int y )
{
    int i;
    for( i = 0; i < CAV_MAX_FALLING_ROCKS; i = i + 1 )
    {
        if( ( cavFallingRocks[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        if( cavIsNearXY( &cavFallingRocks[ i ], x, y ) ) return true;
    }
    return false;
}

bool cavChaserAdjacentOther( int selfIndex )
{
    CavMovable* p;
    int x, y;
    p = &cavChasers[ selfIndex ];
    if( ( p->status & CAV_MOVABLE_CAN_MOVE ) == 0 ) return false;
    x = p->x + p->dx;
    y = p->y + p->dy;
    return cavAdjacentChaser( selfIndex, x, y ) || cavAdjacentGhost( -1, x, y ) || cavAdjacentRock( x, y );
}

bool cavGhostAdjacentOther( int selfIndex )
{
    CavMovable* p;
    int x, y;
    p = &cavGhosts[ selfIndex ];
    if( ( p->status & CAV_MOVABLE_CAN_MOVE ) == 0 ) return false;
    x = p->x + p->dx;
    y = p->y + p->dy;
    return cavAdjacentChaser( -1, x, y ) || cavAdjacentGhost( selfIndex, x, y ) || cavAdjacentRock( x, y );
}

// -----------------------------------------------------------------------------
//   Rock.cpp - falling-rock management and the rock/monster hit callback.
// -----------------------------------------------------------------------------

bool cavBeginFalling( int x, int y )
{
    int i;
    for( i = 0; i < CAV_MAX_FALLING_ROCKS; i = i + 1 )
    {
        if( ( cavFallingRocks[ i ].status & CAV_MOVABLE_LIVE ) != 0 ) continue;
        cavFallingRocks[ i ].x = x;
        cavFallingRocks[ i ].y = y;
        cavFallingRocks[ i ].status = CAV_MOVABLE_LIVE | CAV_MOVABLE_CAN_MOVE;
        cavFallingRocks[ i ].step = 0;
        cavShowSprite( &cavFallingRocks[ i ], CAV_CHAR_ROCK );
        return true;
    }
    return false;
}

bool cavIsNearRock( int selfIndex, int x, int y )
{
    int i;
    for( i = 0; i < CAV_MAX_FALLING_ROCKS; i = i + 1 )
    {
        if( ( cavFallingRocks[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        if( i != selfIndex && cavIsNearXY( &cavFallingRocks[ i ], x, y ) ) return true;
    }
    return false;
}

bool cavRockIsNearAny( int selfIndex, int x, int y )
{
    if( cavIsNearXY( &cavMan, x, y ) ) return true;
    if( cavIsNearRock( selfIndex, x, y ) ) return true;
    return false;
}

void cavEndFalling( int x, int y )
{
    int i, idx;
    for( i = 0; i < CAV_MAX_ROCK_COUNT; i = i + 1 )
    {
        if( ( cavFixedRocks[ i ].status & CAV_FIXED_EXIST ) != 0 ) continue;
        cavFixedRocks[ i ].status = cavFixedRocks[ i ].status | CAV_FIXED_EXIST;
        cavFixedRocks[ i ].x = x;
        cavFixedRocks[ i ].y = y;
        idx = ( y >> 1 ) * CAV_MAP_WIDTH + ( x >> 1 );
        cavTerrainMap[ idx ] = cavTerrainMap[ idx ] | CAV_TERRAIN_ROCK;
        return;
    }
}

// See header comment - rate can escalate to 4, one past the real 4-entry
// score table (a real upstream off-by-one); the score lookup is clamped,
// the glyph offset (which harmlessly reuses valid CharPattern data one
// past Char_Point) is left exactly as upstream computes it.
void cavOnHitRock( CavMovable* pFallingRock, CavMovable* pMonster )
{
    int rate;
    rate = pFallingRock->status >> 4;
    if( rate < 4 )
    {
        rate = rate + 1;
        pFallingRock->status = ( pFallingRock->status & ~0x30 ) | ( rate << 4 );
    }
    cavStartPoint( pMonster->x, pMonster->y, rate );
    cavSoundHit();
}

// -----------------------------------------------------------------------------
//   Monster.cpp - the shared per-array hit/catch callbacks.
// -----------------------------------------------------------------------------

void cavHitMonsterArray( CavMovable* arr, int count, CavMovable* pRock )
{
    int i;
    for( i = 0; i < count; i = i + 1 )
    {
        if( ( arr[ i ].status & CAV_MOVABLE_LIVE ) != 0 )
        {
            if( arr[ i ].y > pRock->y && cavIsNear( &arr[ i ], pRock ) )
            {
                arr[ i ].status = arr[ i ].status & ~CAV_MOVABLE_LIVE;
                cavHideSprite( arr[ i ].sprite );
                cavOnHitRock( pRock, &arr[ i ] );
            }
        }
    }
}

void cavHitChaser( CavMovable* pRock ) { cavHitMonsterArray( cavChasers, CAV_MAX_CHASER_COUNT, pRock ); }
void cavHitGhost( CavMovable* pRock ) { cavHitMonsterArray( cavGhosts, CAV_MAX_GHOST_COUNT, pRock ); }

void cavCatchMan( CavMovable* pMonster )
{
    if( cavIsVeryNear( pMonster, &cavMan ) )
      cavMan.status = cavMan.status & ~CAV_MOVABLE_LIVE;
}

// -----------------------------------------------------------------------------
//   Rock.cpp - the per-tick scan/fall functions (need cavHitChaser/cavHitGhost).
// -----------------------------------------------------------------------------

void cavTestRocks()
{
    int i, mapX, mapY, below, diff;
    for( i = 0; i < CAV_MAX_ROCK_COUNT; i = i + 1 )
    {
        if( ( cavFixedRocks[ i ].status & CAV_FIXED_EXIST ) == 0 ) continue;
        mapX = cavFixedRocks[ i ].x >> 1;
        mapY = ( cavFixedRocks[ i ].y >> 1 ) + 1;
        below = cavCurrentTerrain( mapX, mapY );
        if( ( below & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE ) continue;
        if( cavMan.y - 2 == cavFixedRocks[ i ].y )
        {
            // static_cast<byte>(...) wraparound trick in the real source -
            // see header comment.
            diff = ( cavMan.x - cavFixedRocks[ i ].x + 1 ) & 0xFF;
            if( diff < 2 ) continue;
        }
        if( cavBeginFalling( cavFixedRocks[ i ].x, cavFixedRocks[ i ].y ) )
        {
            int idx;
            cavFixedRocks[ i ].status = cavFixedRocks[ i ].status & ~CAV_FIXED_EXIST;
            idx = ( cavFixedRocks[ i ].y >> 1 ) * CAV_MAP_WIDTH + ( cavFixedRocks[ i ].x >> 1 );
            cavTerrainMap[ idx ] = cavTerrainMap[ idx ] & ~CAV_TERRAIN_ROCK;
        }
    }
}

void cavMoveRocks()
{
    int i;
    for( i = 0; i < CAV_MAX_FALLING_ROCKS; i = i + 1 )
    {
        CavMovable* pRock;
        pRock = &cavFallingRocks[ i ];
        if( ( pRock->status & CAV_MOVABLE_LIVE ) == 0 ) continue;

        if( cavCanChangeDirection( pRock ) )
        {
            int x, y, idx, idxBelow, below;
            bool landed;
            x = pRock->x >> CAV_MAP_SHIFT;
            y = pRock->y >> CAV_MAP_SHIFT;
            landed = false;
            if( y >= CAV_MAP_HEIGHT - 1 )
              landed = true;
            else
            {
                idxBelow = ( y + 1 ) * CAV_MAP_WIDTH + x;
                below = cavTerrainMap[ idxBelow ];
                if( ( below & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE )
                  landed = true;
            }

            if( landed )
            {
                pRock->status = pRock->status & ~CAV_MOVABLE_LIVE;
                cavHideSprite( pRock->sprite );
                cavEndFalling( pRock->x, pRock->y );
                continue;
            }

            if( cavRockIsNearAny( i, pRock->x, pRock->y + 2 ) )
              continue;

            idx = y * CAV_MAP_WIDTH + x;
            cavTerrainMap[ idx ] = cavTerrainMap[ idx ] & ~CAV_WALL_BOTTOM;
            idxBelow = ( y + 1 ) * CAV_MAP_WIDTH + x;
            cavTerrainMap[ idxBelow ] = cavTerrainMap[ idxBelow ] & ~CAV_WALL_TOP;
        }

        cavHitChaser( pRock );
        cavHitGhost( pRock );
        cavMoveMovable( pRock );
        cavShowSprite( pRock, CAV_CHAR_ROCK );
    }
}

// -----------------------------------------------------------------------------
//   Chaser.cpp
// -----------------------------------------------------------------------------

void cavChaserDraw( int index )
{
    cavShowSprite( &cavChasers[ index ], CAV_CHAR_CHASER );
}

bool cavChaserCanMove( int index, int direction )
{
    CavMovable* pChaser;
    int mapX, mapY, current, next;
    pChaser = &cavChasers[ index ];
    cavSetDirection( pChaser, direction );
    mapX = pChaser->x >> CAV_MAP_SHIFT;
    mapY = pChaser->y >> CAV_MAP_SHIFT;
    current = cavCurrentTerrain( mapX, mapY );
    if( ( current & cavWallBits[ direction >> 1 ] ) != 0 ) return false;
    mapX = mapX + pChaser->dx;
    mapY = mapY + pChaser->dy;
    next = cavCurrentTerrain( mapX, mapY );
    if( ( next & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE ) return false;
    return !cavChaserAdjacentOther( index );
}

void cavChaserDecideDirection( int index )
{
    CavMovable* pChaser;
    int direction;
    bool moved;
    pChaser = &cavChasers[ index ];
    moved = false;
    direction = 0;

    if( cavMan.x != pChaser->x )
    {
        if( cavMan.x > pChaser->x ) direction = CAV_DIR_RIGHT;
        else direction = CAV_DIR_LEFT;
        if( cavChaserCanMove( index, direction ) ) moved = true;
    }
    if( !moved && cavMan.y != pChaser->y )
    {
        if( cavMan.y > pChaser->y ) direction = CAV_DIR_DOWN;
        else direction = CAV_DIR_UP;
        if( cavChaserCanMove( index, direction ) ) moved = true;
    }

    if( moved )
      pChaser->status = pChaser->status | CAV_MOVABLE_CAN_MOVE;
    else
      pChaser->status = pChaser->status & ~CAV_MOVABLE_CAN_MOVE;
}

// -----------------------------------------------------------------------------
//   Ghost.cpp
// -----------------------------------------------------------------------------

void cavGhostDraw( int index )
{
    CavMovable* pGhost;
    int status, c, xy;
    pGhost = &cavGhosts[ index ];
    status = pGhost->status;
    c = CAV_CHAR_GHOST + ( ( status & CAV_MOVABLE_DIR_MASK ) << 2 );
    xy = pGhost->x + pGhost->y;
    c = c + ( ( xy << 2 ) & 4 );
    if( ( status & CAV_GHOST_THROUGH ) != 0 )
      c = c + 32;
    cavShowSprite( pGhost, c );
}

int cavGhostRnd()
{
    int r;
    r = cavGhostRndTable[ cavGhostRndIndex ];
    cavGhostRndIndex = cavGhostRndIndex + 1;
    if( cavGhostRndIndex >= 16 )
      cavGhostRndIndex = 0;
    return r;
}

bool cavGhostCanMove( int index, int direction )
{
    CavMovable* pGhost;
    int mapX, mapY, current, next, nextT, i;
    pGhost = &cavGhosts[ index ];
    cavSetDirection( pGhost, direction );
    mapX = pGhost->x >> CAV_MAP_SHIFT;
    mapY = pGhost->y >> CAV_MAP_SHIFT;
    current = cavCurrentTerrain( mapX, mapY );
    mapX = mapX + pGhost->dx;
    mapY = mapY + pGhost->dy;
    if( mapX < 0 || mapX >= CAV_MAP_WIDTH || mapY < 0 || mapY >= CAV_MAP_HEIGHT ) return false;
    next = cavCurrentTerrain( mapX, mapY );
    nextT = next & CAV_TERRAIN_MASK;
    if( ( current & CAV_TERRAIN_MASK ) == 0 )
    {
        i = direction >> 1;
        pGhost->status = pGhost->status & ~CAV_GHOST_THROUGH;
        if( nextT == CAV_TERRAIN_SPACE && ( current & cavWallBits[ i ] ) == 0 && ( next & cavWallBits[ i ^ 1 ] ) == 0 )
          return !cavGhostAdjacentOther( index );
        if( !cavThroughable ) return false;
    }
    pGhost->status = pGhost->status | CAV_GHOST_THROUGH;
    if( nextT >= CAV_TERRAIN_WALL ) return false;
    return !cavGhostAdjacentOther( index );
}

void cavGhostDecideDirection( int index )
{
    CavMovable* pGhost;
    int directions[4];
    int verticalIndex, horizontalIndex;
    int i, direction;
    bool found;

    pGhost = &cavGhosts[ index ];
    verticalIndex = 0;
    horizontalIndex = 0;

    if( cavAbs( cavMan.x, pGhost->x ) > cavAbs( cavMan.y, pGhost->y ) )
    {
        if( cavMan.x < pGhost->x )
        {
            if( pGhost->dx <= 0 )
            {
                directions[0] = CAV_DIR_LEFT;
                directions[3] = CAV_DIR_RIGHT;
                verticalIndex = 1;
            }
            else
            {
                directions[2] = CAV_DIR_RIGHT;
                directions[3] = CAV_DIR_LEFT;
                verticalIndex = 0;
            }
        }
        else
        {
            if( pGhost->dx >= 0 )
            {
                directions[0] = CAV_DIR_RIGHT;
                directions[3] = CAV_DIR_LEFT;
                verticalIndex = 1;
            }
            else
            {
                directions[2] = CAV_DIR_LEFT;
                directions[3] = CAV_DIR_RIGHT;
                verticalIndex = 0;
            }
        }
        if( ( cavMan.y < pGhost->y && pGhost->dy <= 0 ) || pGhost->dy < 0 )
        {
            directions[ verticalIndex ] = CAV_DIR_UP;
            verticalIndex = verticalIndex + 1;
            directions[ verticalIndex ] = CAV_DIR_DOWN;
        }
        else
        {
            directions[ verticalIndex ] = CAV_DIR_DOWN;
            verticalIndex = verticalIndex + 1;
            directions[ verticalIndex ] = CAV_DIR_UP;
        }
    }
    else
    {
        if( cavMan.y < pGhost->y )
        {
            if( pGhost->dy <= 0 )
            {
                directions[0] = CAV_DIR_UP;
                directions[3] = CAV_DIR_DOWN;
                horizontalIndex = 1;
            }
            else
            {
                directions[2] = CAV_DIR_DOWN;
                directions[3] = CAV_DIR_UP;
                horizontalIndex = 0;
            }
        }
        else
        {
            if( pGhost->dy >= 0 )
            {
                directions[0] = CAV_DIR_DOWN;
                directions[3] = CAV_DIR_UP;
                horizontalIndex = 1;
            }
            else
            {
                directions[2] = CAV_DIR_UP;
                directions[3] = CAV_DIR_DOWN;
                horizontalIndex = 0;
            }
        }
        // upstream compares "man.x < pGhost->y" here too (a real upstream
        // quirk, not a transcription slip - kept exactly as-is, matching
        // this project's own "preserve a faithful, even if odd, upstream
        // comparison rather than silently fixing it" precedent).
        if( ( cavMan.x < pGhost->y && pGhost->dx <= 0 ) || pGhost->dx < 0 )
        {
            directions[ horizontalIndex ] = CAV_DIR_LEFT;
            horizontalIndex = horizontalIndex + 1;
            directions[ horizontalIndex ] = CAV_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalIndex ] = CAV_DIR_RIGHT;
            horizontalIndex = horizontalIndex + 1;
            directions[ horizontalIndex ] = CAV_DIR_LEFT;
        }
    }

    found = false;
    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        if( cavGhostCanMove( index, direction ) )
        {
            found = true;
            i = 4;
        }
    }
    if( found )
      pGhost->status = pGhost->status | CAV_MOVABLE_CAN_MOVE;
    else
      pGhost->status = pGhost->status & ~CAV_MOVABLE_CAN_MOVE;
}

// -----------------------------------------------------------------------------
//   Monster.cpp - StartMonsters (shared start/positioning helper).
// -----------------------------------------------------------------------------

void cavStartMonsters( CavMovable* arr, int count, int startOffset )
{
    int offset, b, i;
    offset = startOffset + 1;
    i = 0;
    while( i < count )
    {
        b = cavStageBytes[ cavStageIndex ][ offset ];
        offset = offset + 1;
        if( ( arr[ i ].status & CAV_MOVABLE_LIVE ) != 0 )
        {
            arr[ i ].x = cavDecodeX( b );
            arr[ i ].y = cavDecodeY( b );
        }
        i = i + 1;
    }
}

void cavStartChasers()
{
    int i;
    cavStartMonsters( cavChasers, CAV_MAX_CHASER_COUNT, cavChaserPosOffset );
    for( i = 0; i < CAV_MAX_CHASER_COUNT; i = i + 1 )
    {
        if( ( cavChasers[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        cavChasers[ i ].step = 0;
        cavChaserDecideDirection( i );
        cavChaserDraw( i );
    }
}

void cavMoveChasers()
{
    int i;
    for( i = 0; i < CAV_MAX_CHASER_COUNT; i = i + 1 )
    {
        CavMovable* pChaser;
        pChaser = &cavChasers[ i ];
        if( ( pChaser->status & CAV_MOVABLE_LIVE ) == 0 ) continue;

        cavCatchMan( pChaser );
        if( ( pChaser->dx | pChaser->dy ) == 0 || cavCanChangeDirection( pChaser ) )
          cavChaserDecideDirection( i );

        if( cavChaserAdjacentOther( i ) )
        {
            pChaser->dx = 0;
            pChaser->dy = 0;
            continue;
        }
        if( cavMoveMovable( pChaser ) )
        {
            cavChaserDraw( i );
            cavCatchMan( pChaser );
        }
    }
}

void cavStartGhosts()
{
    int i;
    cavStartMonsters( cavGhosts, CAV_MAX_GHOST_COUNT, cavGhostPosOffset );
    for( i = 0; i < CAV_MAX_GHOST_COUNT; i = i + 1 )
    {
        if( ( cavGhosts[ i ].status & CAV_MOVABLE_LIVE ) == 0 ) continue;
        cavGhosts[ i ].step = 0;
        cavGhostDecideDirection( i );
        cavGhostDraw( i );
    }
}

void cavMoveGhosts()
{
    int i;
    for( i = 0; i < CAV_MAX_GHOST_COUNT; i = i + 1 )
    {
        CavMovable* pGhost;
        int status;
        pGhost = &cavGhosts[ i ];
        if( ( pGhost->status & CAV_MOVABLE_LIVE ) == 0 ) continue;

        cavCatchMan( pGhost );
        if( ( pGhost->dx | pGhost->dy ) == 0 || cavCanChangeDirection( pGhost ) )
        {
            cavThroughable =
                ( ( cavMan.x | cavMan.y ) & CAV_MAP_MASK ) == 0 &&
                ( cavMan.step & CAV_STEP_MASK ) == 0 &&
                cavGhostRnd() <= cavCurrentStage;
            cavGhostDecideDirection( i );
        }
        status = pGhost->status;
        if( ( status & CAV_GHOST_THROUGH ) != 0 )
        {
            if( ( status & CAV_GHOST_WAIT ) != 0 )
            {
                pGhost->status = pGhost->status & ~CAV_GHOST_WAIT;
                continue;
            }
            pGhost->status = pGhost->status | CAV_GHOST_WAIT;
        }
        if( cavGhostAdjacentOther( i ) )
        {
            pGhost->dx = 0;
            pGhost->dy = 0;
            continue;
        }
        if( cavMoveMovable( pGhost ) )
        {
            cavGhostDraw( i );
            cavCatchMan( pGhost );
        }
    }
}

// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void cavManDraw()
{
    int c;
    c = CAV_CHAR_MAN + cavCharOffset + ( ( cavMan.status & CAV_MOVABLE_DIR_MASK ) << 2 );
    c = c + ( ( ( cavMan.x + cavMan.y ) << 2 ) & 4 );
    cavShowSprite( &cavMan, c );
}

void cavStartMan()
{
    cavMan.x = CAV_MAN_START_X;
    cavMan.y = 0;
    cavMan.sprite = CAV_SPRITE_MAN;
    cavMan.status = CAV_MOVABLE_LIVE | CAV_DIR_LEFT;
    cavCharOffset = 0;
    cavMan.step = 0;
    cavManDraw();
}

int cavMakeNext( int direction )
{
    cavSetDirection( &cavMan, direction );
    cavNextMapX = cavPrevMapX + cavMan.dx;
    cavNextMapY = cavPrevMapY + cavMan.dy;
    return cavCurrentTerrain( cavNextMapX, cavNextMapY );
}

bool cavManIsNearAny()
{
    return cavIsNearRock( -1, cavMan.x + ( cavMan.dx << CAV_MAP_SHIFT ), cavMan.y + ( cavMan.dy << CAV_MAP_SHIFT ) );
}

// A real, genuinely upstream bug (confirmed against Man.cpp line 68 -
// "(man.step & Movable_Status_CanMove) == 0", byte-for-byte the same wrong
// constant, not a porting artifact), found via a direct user report ("the
// level is hard to see... i see left over player drawings on the player
// previous positions"). Every other movable in this game (ghosts/chasers/
// rocks, via cavCanChangeDirection() below, matching upstream's own
// CanChangeDirection()) correctly gates its "can I read new input /
// re-validate direction now" check on (step & CAV_STEP_MASK)==0 - the man's
// own MoveMan() instead reimplements the same shape inline but reaches for
// CAV_MOVABLE_CAN_MOVE (0x08, a STATUS bit) instead of CAV_STEP_MASK (7,
// the actual step-cycle mask) - almost certainly an accidental copy/paste
// mix-up between two similarly-named-and-purposed constants, not a
// deliberate design choice (no other movable in the file does this).
// Effect: (step & 8)==0 only isolates bit 3 of the ever-incrementing step
// counter, true for steps [0-7]/[16-23]/[32-39]/... and false for
// [8-15]/[24-31]/... - so the man's own "read fresh input, re-check walls,
// refresh cavPrevMapX/Y+cavNextMapX/Y" block only actually runs on every
// OTHER real cell-crossing (every 16 steps, not every 8) instead of every
// one. On the skipped crossings, cavMoveMovable() below still silently
// advances the man one more cell in whatever direction was last decided
// (no wall re-check, no new input read) - and the post-move terrain-update
// block further down (clearing cavTerrainMap[]'s wall bits to visually
// "dig" the tunnel) unconditionally uses cavPrevMapX/Y/cavNextMapX/Y, which
// are ONLY refreshed inside this gated block - so on a skipped crossing it
// clears wall bits at whichever STALE (two cycles old) map cell those
// variables still held, not the cell actually just dug through.
// This is far more damaging here than on real hardware: upstream's own
// DrawTerrain() is a genuinely incremental single-cell redraw (a real
// SSD1306 VRAM write to one glyph, easy to miss since later real digging
// nearby redraws over it) - but this port's cavRenderVVramFromState()
// (see its own header comment) deliberately recomputes the WHOLE VVram
// grid fresh from cavTerrainMap[] every single frame, so a wrong cell's
// wall-bit clear here is baked directly into the persistent source-of-
// truth terrain state and reappears in every subsequent frame's redraw -
// exactly matching both reported symptoms (spurious cleared/dug-looking
// cells scattered off the player's real path reading as "leftover
// drawings at previous positions", and the maze's real structure getting
// progressively corrupted making it "hard to see"). Fixed by routing
// through the existing, already-correct cavCanChangeDirection() helper
// instead of the ad-hoc, wrong-constant reimplementation - eliminates the
// duplicated/mistyped logic entirely rather than just swapping the one
// constant inline.
void cavMoveMan()
{
    bool gridReady, skipToDraw;
    gridReady = cavCanChangeDirection( &cavMan );
    skipToDraw = false;

    if( gridReady )
    {
        bool foundKey;
        int direction;

        foundKey = false;
        direction = 0;
        if( isRightPressed() ) { foundKey = true; direction = 0; }
        else if( isLeftPressed() ) { foundKey = true; direction = 2; }
        else if( isDownPressed() ) { foundKey = true; direction = 4; }
        else if( isUpPressed() ) { foundKey = true; direction = 6; }

        if( !foundKey )
        {
            cavMan.status = cavMan.status & ~CAV_MOVABLE_CAN_MOVE;
            return;
        }

        {
            int next, oldDirection;
            bool blocked;

            cavPrevMapX = cavMan.x >> CAV_MAP_SHIFT;
            cavPrevMapY = cavMan.y >> CAV_MAP_SHIFT;
            next = cavMakeNext( direction ) & CAV_TERRAIN_MASK;
            blocked = next >= CAV_TERRAIN_WALL || cavManIsNearAny();
            if( blocked )
            {
                oldDirection = cavMan.status & CAV_MOVABLE_DIR_MASK;
                next = cavMakeNext( oldDirection ) & CAV_TERRAIN_MASK;
                blocked = next >= CAV_TERRAIN_WALL || cavManIsNearAny();
                if( blocked )
                {
                    cavMan.status = cavMan.status & ~CAV_MOVABLE_CAN_MOVE;
                    skipToDraw = true;
                }
            }
            if( !skipToDraw )
            {
                if( next == 0 && ( cavTerrainMap[ cavPrevMapY * CAV_MAP_WIDTH + cavPrevMapX ] & cavWallBits[ direction >> 1 ] ) == 0 )
                  cavCharOffset = 0;
                else
                  cavCharOffset = 32;
                cavMan.status = cavMan.status | CAV_MOVABLE_CAN_MOVE;
            }
        }
    }

    if( skipToDraw )
    {
        cavManDraw();
        return;
    }

    if( cavMoveMovable( &cavMan ) )
    {
        if( ( ( cavMan.x | cavMan.y ) & CAV_MAP_MASK ) == 0 )
        {
            int idx;
            idx = cavNextMapY * CAV_MAP_WIDTH + cavNextMapX;
            cavTerrainMap[ idx ] = cavTerrainMap[ idx ] & ~CAV_TERRAIN_MASK;
            cavHitBox();
        }
        else
        {
            int direction, idxPrev, idxNext;
            direction = cavMan.status & CAV_MOVABLE_DIR_MASK;
            idxPrev = cavPrevMapY * CAV_MAP_WIDTH + cavPrevMapX;
            cavTerrainMap[ idxPrev ] = cavTerrainMap[ idxPrev ] & ~cavWallBits[ direction >> 1 ];
            idxNext = cavNextMapY * CAV_MAP_WIDTH + cavNextMapX;
            cavTerrainMap[ idxNext ] = cavTerrainMap[ idxNext ] & ~cavWallBits[ ( direction >> 1 ) ^ 1 ];
            // (MidChars transitional glyph intentionally dropped - see header)
        }
        cavManDraw();
    }
}

// -----------------------------------------------------------------------------
//   Stage.cpp - stage byte-stream consumption and terrain construction.
// -----------------------------------------------------------------------------

void cavInitFixedsConsume( CavFixed* arr, int maxCount )
{
    int count, i, b;
    count = cavStageBytes[ cavStageIndex ][ cavStageOffset ];
    cavStageOffset = cavStageOffset + 1;
    i = 0;
    while( i < count )
    {
        b = cavStageBytes[ cavStageIndex ][ cavStageOffset ];
        cavStageOffset = cavStageOffset + 1;
        arr[ i ].x = cavDecodeX( b );
        arr[ i ].y = cavDecodeY( b );
        arr[ i ].status = CAV_FIXED_EXIST;
        i = i + 1;
    }
    while( i < maxCount )
    {
        arr[ i ].status = CAV_FIXED_NONE;
        i = i + 1;
    }
}

void cavInitMonstersConsume( CavMovable* arr, int maxCount, int spriteBase )
{
    int count, i, sprite;
    count = cavStageBytes[ cavStageIndex ][ cavStageOffset ];
    cavStageOffset = cavStageOffset + 1;
    sprite = spriteBase;
    i = 0;
    while( i < count )
    {
        cavStageOffset = cavStageOffset + 1;
        arr[ i ].status = CAV_MOVABLE_LIVE;
        arr[ i ].sprite = sprite;
        sprite = sprite + 1;
        i = i + 1;
    }
    while( i < maxCount )
    {
        arr[ i ].status = 0;
        arr[ i ].sprite = sprite;
        sprite = sprite + 1;
        i = i + 1;
    }
}

void cavInitBoxesConsume()
{
    cavInitFixedsConsume( cavBoxes, CAV_MAX_BOX_COUNT );
}

void cavInitRocksConsume()
{
    int i, sprite;
    sprite = CAV_SPRITE_ROCK;
    for( i = 0; i < CAV_MAX_FALLING_ROCKS; i = i + 1 )
    {
        cavFallingRocks[ i ].status = 0;
        cavFallingRocks[ i ].sprite = sprite;
        cavSetDirection( &cavFallingRocks[ i ], CAV_DIR_DOWN );
        sprite = sprite + 1;
    }
    cavInitFixedsConsume( cavFixedRocks, CAV_MAX_ROCK_COUNT );
}

void cavInitChasersConsume()
{
    cavChaserPosOffset = cavStageOffset;
    cavInitMonstersConsume( cavChasers, CAV_MAX_CHASER_COUNT, CAV_SPRITE_CHASER );
}

void cavInitGhostsConsume()
{
    cavGhostRndIndex = 0;
    cavGhostPosOffset = cavStageOffset;
    cavInitMonstersConsume( cavGhosts, CAV_MAX_GHOST_COUNT, CAV_SPRITE_GHOST );
}

void cavParseTerrainFromStage()
{
    int i, row, group, sub, b, idx;
    for( i = 0; i < CAV_MAP_WIDTH; i = i + 1 )
      cavTerrainMap[ i ] = CAV_TERRAIN_SPACE;
    idx = CAV_MAP_WIDTH;
    for( row = 0; row < CAV_MAP_HEIGHT - 1; row = row + 1 )
    {
        for( group = 0; group < CAV_MAP_WIDTH / 4; group = group + 1 )
        {
            b = cavStageBytes[ cavStageIndex ][ cavStageOffset ];
            cavStageOffset = cavStageOffset + 1;
            for( sub = 0; sub < 4; sub = sub + 1 )
            {
                cavTerrainMap[ idx ] = b & 3;
                idx = idx + 1;
                b = b >> 2;
            }
        }
    }
}

// upstream's own `x > MapWidth-1` right-edge check can never be true (x
// maxes out at MapWidth-1) - a real, harmless upstream quirk (the right
// edge is still enforced correctly via cavCurrentTerrain's own x<MapWidth
// bound, just through a different mechanism than the Wall_Right bit).
// Preserved literally rather than "fixed" to `x>=MapWidth-1`.
void cavComputeWallBits()
{
    int y, x, idx, terrain;
    idx = CAV_MAP_WIDTH;
    for( y = 1; y < CAV_MAP_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < CAV_MAP_WIDTH; x = x + 1 )
        {
            terrain = cavTerrainMap[ idx ];
            if( terrain == CAV_TERRAIN_SPACE )
            {
                if( y > 1 && ( cavTerrainMap[ idx - CAV_MAP_WIDTH ] & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE )
                  terrain = terrain | CAV_WALL_TOP;
                if( y >= CAV_MAP_HEIGHT - 1 || ( cavTerrainMap[ idx + CAV_MAP_WIDTH ] & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE )
                  terrain = terrain | CAV_WALL_BOTTOM;
                if( x < 1 || ( cavTerrainMap[ idx - 1 ] & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE )
                  terrain = terrain | CAV_WALL_LEFT;
                if( x > CAV_MAP_WIDTH - 1 || ( cavTerrainMap[ idx + 1 ] & CAV_TERRAIN_MASK ) != CAV_TERRAIN_SPACE )
                  terrain = terrain | CAV_WALL_RIGHT;
            }
            else
              terrain = terrain | CAV_WALL_ALL;
            cavTerrainMap[ idx ] = terrain;
            idx = idx + 1;
        }
    }
}

void cavApplyBoxesTerrain()
{
    int i, mapX, mapY, idx;
    cavBoxCount = 0;
    for( i = 0; i < CAV_MAX_BOX_COUNT; i = i + 1 )
    {
        if( cavBoxes[ i ].status == 0 ) continue;
        mapX = cavBoxes[ i ].x >> 1;
        mapY = cavBoxes[ i ].y >> 1;
        idx = mapY * CAV_MAP_WIDTH + mapX;
        cavTerrainMap[ idx ] = cavTerrainMap[ idx ] | CAV_TERRAIN_SOIL | CAV_WALL_ALL;
        cavBoxCount = cavBoxCount + 1;
    }
}

void cavApplyRocksFixedTerrain()
{
    int i, mapX, mapY, idx;
    for( i = 0; i < CAV_MAX_ROCK_COUNT; i = i + 1 )
    {
        if( ( cavFixedRocks[ i ].status & CAV_FIXED_EXIST ) == 0 ) continue;
        mapX = cavFixedRocks[ i ].x >> 1;
        mapY = cavFixedRocks[ i ].y >> 1;
        idx = mapY * CAV_MAP_WIDTH + mapX;
        cavTerrainMap[ idx ] = cavTerrainMap[ idx ] | CAV_TERRAIN_ROCK;
    }
}

void cavInitStage()
{
    int i, j;
    i = 0; j = 0;
    while( i < cavCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= CAV_STAGE_COUNT ) j = 0;
    }
    cavStageIndex = j;
}

void cavInitTrying()
{
    int i, j;

    cavStageTime = 100;
    // Full 32-column clear (not just the old 8) - without this, whatever
    // the title screen's own wide-canvas text (CAVIT/MINI/START/CONTINUE/
    // the credit line, now spanning columns well outside the old narrow
    // 0-7 range) last left in cavStatusChar would otherwise persist into
    // gameplay, since cavPrintStatus() below only ever WRITES its own
    // specific label/digit cells, never clears the whole grid first -
    // matching gameCracky.c's own identical, already-found-via-user-report
    // "stale title text bleeding into gameplay" fix.
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        cavStatusChar[ i ][ j ] = 0;
    for( i = 0; i < CAV_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < CAV_VVRAM_WIDTH; j = j + 1 )
        cavVVram[ i ][ j ] = CAV_CHAR_SPACE;
    cavOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in cavUpdateTitle()) - belt-and-suspenders in case any
    // future call site ever reaches cavInitTrying() without going through
    // that transition first, matching gameCracky.c's own identical guard.
    cavFullWidthText = false;
    cavHideAllSprites();
    cavPrintStatus();

    cavStageOffset = 0;
    cavParseTerrainFromStage();
    cavInitBoxesConsume();
    cavInitRocksConsume();
    cavInitChasersConsume();
    cavInitGhostsConsume();
    cavComputeWallBits();
    cavApplyBoxesTerrain();
    cavApplyRocksFixedTerrain();
    cavInitPoints();
    cavStartChasers();
    cavStartGhosts();
    cavStartMan();
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// The real bug behind the "trail of graphics garbage" report - confirmed
// via a live Puppeteer reproduction (stacked screenshots across consecutive
// left-moves showed the man's own icon accumulating at every previous
// position, never erased, exactly matching what the user described), not
// the cavMoveMan gate fix above (that one's a genuine, separate upstream
// bug, still worth keeping, but it wasn't the cause of this symptom).
//
// Map row 0 (cavTerrainMap[0..CAV_MAP_WIDTH-1], where the man starts -
// StartX/y=0) is deliberately never touched by cavComputeWallBits() or by
// upstream's own equivalent wall-bit pass, matching upstream's real
// InitTrying()/Stage.cpp exactly: both loops begin at y=1, since row 0 is a
// permanent border with no wall-bit glyph of its own to compute - it's
// simply always CAV_TERRAIN_SPACE and stays that way for the whole stage
// (confirmed: nothing else in this file ever mutates cavTerrainMap[0..11]
// after cavInitTrying()'s own initial `= CAV_TERRAIN_SPACE` loop).
// Upstream gets away with never redrawing that row on its own because its
// real VVram is an incremental Front/Back double-buffer - DrawAll() calls
// VVramBackToFront(), a full-buffer memcpy that unconditionally restores
// row 0's own VVramFront content back to whatever blank glyph VVramBack
// still holds there (set once by ClearVVram() at stage start, never
// touched again), erasing any sprite previously stamped there, *before*
// DrawSprites() re-stamps the current frame's real positions on top - so a
// moving sprite's own trail is wiped every single real frame regardless of
// which row it's in.
// This port's own "recompute the whole grid fresh from cavTerrainMap[]
// every frame" shortcut (see this file's own header comment) is a faithful
// substitute for that *only* for the rows the terrain loop below actually
// visits (map rows 1..CAV_MAP_HEIGHT-1) - it was never extended to also
// cover map row 0, since row 0 never needed a wall-bit *glyph* computed.
// But glyph computation and frame-to-frame VVram refresh are two different
// concerns that upstream's own architecture happens to bundle behind the
// same "row 0 is never DrawTerrain()'d" fact - this port only replicated
// the first half. The result: nothing ever resets whatever a sprite
// (chiefly the man, who starts and often walks along exactly this row)
// last stamped into VVram rows 1-2 (map row 0's own two doubled rows), so
// each new position just accumulates on top of every previous one,
// forever, in a genuinely different mechanism from a per-tick corruption
// (which would need a fresh mistaken write each move) - here nothing writes
// anything wrong at all, something that upstream relies on happening
// (a full-row refresh) simply never happens for this one row.
// Fixed by including map row 0 in this same per-frame terrain recompute
// loop (y starts at 0, not 1) instead of adding a second, separate
// "blank out row 0" step - cavTerrainMap[0..11] is always CAV_TERRAIN_SPACE
// with no wall bits ever set, so codesOffset always resolves to 0 and every
// one of its 4 glyphs is CAV_CHAR_WALL_SPACE (blank) - exactly reproducing
// upstream's own "always blank unless a sprite is currently there" result,
// just via a fresh recompute instead of a buffer-copy.
void cavRenderVVramFromState()
{
    int y, x, idx, terrain, t, codesOffset, base, i;
    int vy, vx;

    idx = 0;
    for( y = 0; y < CAV_MAP_HEIGHT; y = y + 1 )
    {
        for( x = 0; x < CAV_MAP_WIDTH; x = x + 1 )
        {
            int g0, g1, g2, g3;
            terrain = cavTerrainMap[ idx ];
            t = terrain & CAV_TERRAIN_MASK;
            // A fixed (landed) rock ORs CAV_TERRAIN_ROCK (4) onto whatever
            // base type (space/soil/wall) was already there, giving t a
            // value of 4-7 - upstream's own DrawTerrain() is NEVER actually
            // invoked with the rock bit combined into the terrain type (its
            // own wall-bits-computation loop runs before rocks/boxes are
            // ever applied to the map, and the rock's own icon is drawn via
            // a separate VPut2CXY overlay that bypasses DrawTerrain's
            // solid-codes lookup entirely), so t there is always in 0-3.
            // This port's own "recompute the whole grid fresh every frame"
            // shortcut instead re-evaluates every cell's terrain type
            // every frame, including rock-occupied ones - without this
            // mask, t=4..7 would compute base=(t-1)<<2=12..24 and read
            // cavSolidCodes[] up to 16 elements past its real 12-entry
            // bound (a genuine out-of-bounds global read on Vircon32, even
            // though the fixed-rock icon overlay drawn moments later in
            // this same function always overwrites the resulting garbage
            // before it's ever visible). Stripping the rock bit here
            // reproduces upstream's real "what would DrawTerrain have
            // shown here" result exactly, matching what the icon overlay
            // will subsequently sit on top of either way.
            if( ( t & CAV_TERRAIN_ROCK ) != 0 ) t = t & ~CAV_TERRAIN_ROCK;
            if( t == CAV_TERRAIN_SPACE )
            {
                codesOffset = ( terrain >> 2 ) & 0x3c;
                g0 = cavWallSpaceCodes[ codesOffset + 0 ];
                g1 = cavWallSpaceCodes[ codesOffset + 1 ];
                g2 = cavWallSpaceCodes[ codesOffset + 2 ];
                g3 = cavWallSpaceCodes[ codesOffset + 3 ];
            }
            else
            {
                base = ( t - 1 ) << 2;
                g0 = cavSolidCodes[ base + 0 ];
                g1 = cavSolidCodes[ base + 1 ];
                g2 = cavSolidCodes[ base + 2 ];
                g3 = cavSolidCodes[ base + 3 ];
            }
            vy = ( y << 1 ) + CAV_STAGE_TOP;
            vx = x << 1;
            cavVVram[ vy ][ vx ] = g0;
            cavVVram[ vy ][ vx + 1 ] = g1;
            cavVVram[ vy + 1 ][ vx ] = g2;
            cavVVram[ vy + 1 ][ vx + 1 ] = g3;
            idx = idx + 1;
        }
    }

    for( i = 0; i < CAV_MAX_BOX_COUNT; i = i + 1 )
    {
        if( cavBoxes[ i ].status == 0 ) continue;
        cavPutIcon( cavBoxes[ i ].x, cavBoxes[ i ].y, CAV_CHAR_BOX );
    }
    for( i = 0; i < CAV_MAX_ROCK_COUNT; i = i + 1 )
    {
        if( ( cavFixedRocks[ i ].status & CAV_FIXED_EXIST ) == 0 ) continue;
        cavPutIcon( cavFixedRocks[ i ].x, cavFixedRocks[ i ].y, CAV_CHAR_ROCK );
    }
    for( i = 0; i < CAV_SPRITE_COUNT; i = i + 1 )
    {
        if( cavSprites[ i ].code != CAV_INVALID_CODE )
        {
            int sx, sy, c;
            sx = cavSprites[ i ].x;
            sy = cavSprites[ i ].y;
            c = cavSprites[ i ].code;
            cavVVram[ sy ][ sx ] = c; c = c + 1;
            cavVVram[ sy ][ sx + 1 ] = c; c = c + 1;
            cavVVram[ sy + 1 ][ sx ] = c; c = c + 1;
            cavVVram[ sy + 1 ][ sx + 1 ] = c;
        }
    }
}

// OR-combines the VVram-derived map/logo byte with the cavStatusChar-
// derived text/icon byte, rather than exclusively choosing one - mirroring
// gameCracky.c's own identical `crkComposeRawByte()` fix exactly. During
// the title screen (cavFullWidthText), the "CAVIT" logo bitmap drawn into
// cavVVram (see cavBeginTitle()) occupies real hardware pages 1-2 only,
// while every status-text element (SCORE/MINI/START/CONTINUE/credit) is
// printed on pages 0/3/5/6/7 - entirely disjoint page ranges (and, within
// page 1, disjoint column ranges too - the logo's own columns 2-21 never
// reach the SCORE value's own columns 26-30) - so this can never actually
// blend two real, distinct pieces of content together. It just lets the
// logo (mapByte, non-zero only on its own 2 pages/columns) and the text
// (textByte, non-zero only on its own pages/columns) coexist within one
// composed byte instead of one silently excluding the other.
int cavComposeRawByte( int col, int page )
{
    int mapByte, textByte;

    mapByte = 0;
    if( col < CAV_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = col / 4;
        sub = col % 4;
        upper = cavVVram[ page * 2 ][ mapX ];
        lower = cavVVram[ page * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = cavCharPattern[ upper * 2 + 0 ];
            lowerByte = cavCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = cavCharPattern[ upper * 2 + 0 ];
            lowerByte = cavCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = cavCharPattern[ upper * 2 + 1 ];
            lowerByte = cavCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = cavCharPattern[ upper * 2 + 1 ];
            lowerByte = cavCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !cavFullWidthText && col < CAV_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // cavStatusChar's own full-width indexing directly - used both for the
    // normal gameplay path (col already >= 96) and the cavFullWidthText
    // title path (col can be anywhere in 0-127).
    textByte = 0;
    {
        bool iconDrawn;
        int statCol, charCol, sub, c;
        charCol = col / 4;
        sub = col % 4;
        iconDrawn = false;
        if( charCol < 32 )
        {
            // The remain-life icons live at upstream's own real columns
            // 96-111 (LeftX=24 to LeftX+3) regardless of cavFullWidthText -
            // kept as a column offset relative to that fixed 96 origin
            // (statCol) purely for this icon sub-column math, same shape
            // as before the grid widening. During cavFullWidthText, col
            // can be < 96 (giving a negative statCol) - harmlessly
            // excluded by the statCol >= 0 check, since the icon area's
            // own real position hasn't moved.
            statCol = col - 96;
            if( page == 7 && statCol >= 0 && statCol < 16 && ( cavRemainIcon1Active || cavRemainIcon2Active ) )
            {
                bool iconActive;
                int localCol, subCol, upperGlyph, lowerGlyph;
                if( statCol < 8 ) { iconActive = cavRemainIcon1Active; localCol = statCol; }
                else { iconActive = cavRemainIcon2Active; localCol = statCol - 8; }
                if( iconActive )
                {
                    if( localCol < 4 ) { upperGlyph = CAV_CHAR_MAN; lowerGlyph = CAV_CHAR_MAN + 2; subCol = localCol; }
                    else { upperGlyph = CAV_CHAR_MAN + 1; lowerGlyph = CAV_CHAR_MAN + 3; subCol = localCol - 4; }
                    if( subCol == 0 ) textByte = ( cavCharPattern[ upperGlyph * 2 + 0 ] & 0x0f ) | ( cavCharPattern[ lowerGlyph * 2 + 0 ] << 4 );
                    else if( subCol == 1 ) textByte = ( cavCharPattern[ upperGlyph * 2 + 0 ] >> 4 ) | ( cavCharPattern[ lowerGlyph * 2 + 0 ] & 0xf0 );
                    else if( subCol == 2 ) textByte = ( cavCharPattern[ upperGlyph * 2 + 1 ] & 0x0f ) | ( cavCharPattern[ lowerGlyph * 2 + 1 ] << 4 );
                    else textByte = ( cavCharPattern[ upperGlyph * 2 + 1 ] >> 4 ) | ( cavCharPattern[ lowerGlyph * 2 + 1 ] & 0xf0 );
                    iconDrawn = true;
                }
            }
            if( !iconDrawn )
            {
                c = cavStatusChar[ page ][ charCol ];
                textByte = cavAsciiPattern[ c * 4 + sub ];
            }
        }
    }
    return mapByte | textByte;
}

void cavRenderFrame()
{
    int page, col, value;
    md_beginFrame();
    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( cavOverlayActive && page == cavOverlayPage &&
                col >= cavOverlayCol * 4 && col < cavOverlayCol * 4 + cavOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - cavOverlayCol * 4 ) / 4;
                sub = ( col - cavOverlayCol * 4 ) % 4;
                value = cavAsciiPattern[ cavAsciiIndex( cavOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = cavComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}

void cavRender()
{
    if( cavState != CAV_STATE_TITLE )
      cavRenderVVramFromState();
    cavRenderFrame();
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void cavBeginTrying()
{
    cavMonsterNum = 0;
    cavTimeDenom = CAV_MAX_TIME_DENOM;
    cavInitTrying();
    cavStartSeq( 1, CAV_MELODY_START );
    cavState = CAV_STATE_START_JINGLE;
}

// **Rewritten after the same real hardware-photo-driven fix already applied
// to gameCracky.c** (see that file's own header comment on `crkBeginTitle`
// for the full story) - re-reading upstream's real `Status.cpp` (`Title()`)
// line by line shows the original diagnosis (title text colliding with the
// live status labels, so it had to be trimmed/relocated/dropped to fit) was
// backwards: none of that text ever collides with anything upstream,
// because upstream's own Vram address space is a genuinely wide 32-char-
// cell-per-page canvas (see cavStatusChar's own header comment) - the
// status labels occupy only columns 24-31 (upstream's own `LeftX=24`), and
// every piece of title-screen text sits at columns 2-23, well clear of
// them. The ROOT problem was this port's own `cavStatusChar` being modeled
// as an 8-column-wide grid in the first place - now fixed there, this
// function places everything at upstream's real, literal columns, with
// `cavFullWidthText=true` so cavComposeRawByte() renders the full canvas
// instead of just the narrow status zone. This directly fixes two real,
// empirically-confirmed bugs from the old narrow-grid model: "START"
// silently overwriting the live "TIME" value's own first digit (found via
// live testing: showed "START 0" instead of a clean "START" / "TIME  0"
// split), and "MINI" overwriting "STAGE" (both collisions simply don't
// exist once each piece of text sits at its own real, non-overlapping
// column range).
//
// **A second architectural fix, applied afterward, once the same underlying
// mistake was found and corrected in gameCracky.c's own reference port
// first**: the "CAVIT" wordmark below was originally drawn as small plain
// text (`sCavit`, reasoned at the time as "purely decorative, matching
// Cracky's own identical simplification") - wrong, for the exact same
// reason it was wrong there: it's upstream's own real, biggest, most
// prominent title-screen element (a hand-authored 20x4-VVram-cell pixel-art
// bitmap, `Status.cpp`'s `Title()`'s own `TitleBytes[]`), not a throwaway
// detail. Restored here to draw the real bitmap logo directly into
// `cavVVram` from `cavTitleBytes[]`, at its own real position (VVram rows
// 2-5, columns `TitleLeft`..`TitleLeft+19` where `TitleLeft=(24-4*5)/2=2` -
// matching upstream's own `VVramFront + VVramWidth*2 + TitleLeft` starting
// offset exactly, i.e. real hardware pages 1-2). `cavComposeRawByte()` was
// updated to OR-combine this VVram content with `cavStatusChar`'s own text
// layer rather than choosing one exclusively (see that function's own
// comment) - safe since the logo (columns 2-21) and every status-text
// element (columns 24+) occupy disjoint column ranges even on the one page
// (1) they'd otherwise share.
void cavBeginTitle()
{
    // upstream: PrintS(...+(TitleLeft+4*TitleLength-5)*VramStep,"MINI") =
    // col (2+20-5) = 17.
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    // upstream: ArrowX=8; "START"/"CONTINUE" both start at col ArrowX+1=9,
    // the cursor itself at col ArrowX=8 on both their own pages.
    int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    // upstream: PrintS(...+VramRowSize*7+12*VramStep,"INUFUTO 2026") - col12,
    // page7. Previously dropped outright since the old narrow grid had no
    // room for it; restored now that it fits cleanly at its own real
    // column (ending col23, well clear of the remain-icons' own col24-27).
    int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int i, j;

    for( i = 0; i < CAV_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < CAV_VVRAM_WIDTH; j = j + 1 )
        cavVVram[ i ][ j ] = CAV_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 32; j = j + 1 )
        cavStatusChar[ i ][ j ] = 0;
    cavOverlayActive = false;
    cavFullWidthText = true;
    cavHideAllSprites();

    // A real bug, matching the identical one already found and fixed in
    // gameCracky.c via a direct user report ("on game over, the time value
    // remains visible on titlescreen"): cavPrintStatus() below redraws TIME
    // from whatever cavStageTime last held during real gameplay - nothing
    // resets it before reaching the title screen after a game over, so the
    // final countdown value would otherwise stay on screen as part of the
    // title's own status display. Reset here so the title screen always
    // shows a fresh "TIME 000" instead of a stale leftover value.
    cavStageTime = 0;
    cavPrintStatus();

    // Real "CAVIT" logo bitmap - 5 letters x 4x4 VVram cells, starting at
    // VVram row 2, column TitleLeft=2 (matching upstream's own
    // `VVramFront + VVramWidth*2 + TitleLeft` exactly).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                cavVVram[ 2 + row ][ 2 + ch * 4 + col ] = cavTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    cavPrintS( 3, 17, sMini, 4 );
    cavPrintS( 5, 9, sStart, 5 );
    cavPrintS( 6, 9, sContinue, 8 );
    cavPrintS( 7, 12, sCredit, 12 );

    cavSelection = 0;
    cavSelectionChanged = true;
    cavPrevLeft = false; cavPrevRight = false; cavPrevUp = false; cavPrevDown = false; cavPrevFire = false;
    cavState = CAV_STATE_TITLE;
}

void cavUpdateTitle()
{
    bool left, right, up, down, fire, justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( left && !cavPrevLeft ) || ( right && !cavPrevRight ) || ( up && !cavPrevUp ) || ( down && !cavPrevDown );
    justFire = fire && !cavPrevFire;
    cavPrevLeft = left; cavPrevRight = right; cavPrevUp = up; cavPrevDown = down; cavPrevFire = fire;

    if( cavSelectionChanged )
    {
        cavSelectionChanged = false;
        // Cursor column 8 (upstream's own ArrowX), shared by both the
        // START (page5) and CONTINUE (page6) rows.
        if( cavSelection == 0 ) cavPrintC( 5, 8, '>' ); else cavPrintC( 5, 8, ' ' );
        if( cavSelection == 1 ) cavPrintC( 6, 8, '>' ); else cavPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        cavFullWidthText = false;
        cavPendingContinue = ( cavSelection == 1 );
        cavScore = 0;
        if( !cavPendingContinue ) cavCurrentStage = 0;
        cavRemainCount = 3;
        cavInitStage();
        cavBeginTrying();
        cavRender();
        return;
    }
    if( justDir )
    {
        cavSelection = cavSelection ^ 1;
        cavSelectionChanged = true;
    }
    cavRender();
}

void cavUpdateStartJingle()
{
    if( !cavSeqPlaying( 1 ) )
    {
        cavStartBgm();
        cavState = CAV_STATE_PLAYING;
    }
    cavRender();
}

void cavBeginLoseAnim()
{
    cavStopBgm();
    cavAnimStep = 0;
    cavWaitFrames = 0;
    cavState = CAV_STATE_LOSE_ANIM;
}

void cavUpdateLoseAnim()
{
    int patterns[4];
    patterns[0] = CAV_CHAR_MAN + 0 * 4;
    patterns[1] = CAV_CHAR_MAN + 6 * 4;
    patterns[2] = CAV_CHAR_LOOSE + 0 * 4;
    patterns[3] = CAV_CHAR_LOOSE + 1 * 4;

    if( cavWaitFrames > 0 )
    {
        cavWaitFrames = cavWaitFrames - 1;
        cavRender();
        return;
    }

    cavShowSprite( &cavMan, patterns[ cavAnimStep & 3 ] );
    cavSoundLoose();
    cavAnimStep = cavAnimStep + 1;
    cavWaitFrames = cavNoteFrames( 1 );

    if( cavAnimStep >= 8 )
    {
        cavRemainCount = cavRemainCount - 1;
        if( cavRemainCount > 0 )
          cavBeginTrying();
        else
        {
            cavPrintGameOver();
            cavStartSeq( 1, CAV_MELODY_GAMEOVER );
            cavState = CAV_STATE_GAMEOVER_JINGLE;
        }
    }
    cavRender();
}

void cavUpdateGameOverJingle()
{
    if( !cavSeqPlaying( 1 ) )
      cavBeginTitle();
    else
      cavRender();
}

void cavBeginClearJingle()
{
    cavStopBgm();
    cavStartSeq( 1, CAV_MELODY_CLEAR );
    cavState = CAV_STATE_CLEAR_JINGLE;
}

void cavUpdateClearJingle()
{
    if( !cavSeqPlaying( 1 ) )
      cavState = CAV_STATE_BONUS_TALLY;
    cavRender();
}

void cavUpdateBonusTally()
{
    if( cavWaitFrames > 0 )
    {
        cavWaitFrames = cavWaitFrames - 1;
        cavRender();
        return;
    }

    if( cavStageTime >= CAV_BONUS_RATE )
    {
        cavAddScore( 3 );
        cavStageTime = cavStageTime - CAV_BONUS_RATE;
        cavPrintTime();
        cavSoundBeep();
        cavWaitFrames = cavNoteFrames( 1 );
        cavRender();
        return;
    }

    cavStageTime = 0;
    cavPrintStatus();
    cavCurrentStage = cavCurrentStage + 1;
    cavInitStage();
    cavBeginTrying();
    cavRender();
}

void cavUpdatePlaying()
{
    int required, i;

    cavFrameAccum = cavFrameAccum + 1;
    if( ( cavIntervalIndex & 1 ) == 0 ) required = 2;
    else required = 1;
    if( cavFrameAccum < required )
    {
        cavRender();
        return;
    }
    cavFrameAccum = 0;
    cavIntervalIndex = cavIntervalIndex + 1;

    cavUpdatePoints();
    cavMoveMan();
    cavTestRocks();

    cavTimeDenom = cavTimeDenom - 1;
    if( cavTimeDenom == 0 )
    {
        cavStageTime = cavStageTime - 1;
        cavTimeDenom = CAV_MAX_TIME_DENOM;
        cavPrintTime();
        if( cavStageTime == 0 )
        {
            cavPrintTimeUp();
            cavRender();
            cavBeginLoseAnim();
            return;
        }
    }

    if( cavMonsterNum >= 0 )
    {
        cavMoveChasers();
        cavMoveGhosts();
        cavMonsterNum = cavMonsterNum - 4;
    }
    cavMonsterNum = cavMonsterNum + 3;

    for( i = 0; i < CAV_ROCK_SUBTICKS; i = i + 1 )
      cavMoveRocks();

    if( ( cavMan.status & CAV_MOVABLE_LIVE ) == 0 )
    {
        cavRender();
        cavBeginLoseAnim();
        return;
    }

    if( cavBoxCount == 0 )
    {
        cavRender();
        cavBeginClearJingle();
        return;
    }

    cavRender();
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameCavit_init()
{
    cavCurrentStage = 0;
    cavRemainCount = 3;
    cavScore = 0;
    cavStageTime = 0;
    cavGhostRndIndex = 0;

    cavSeqActive[0] = 0; cavSeqActive[1] = 0; cavSeqActive[2] = 0;
    cavSeqMelody[0] = CAV_MELODY_NONE; cavSeqMelody[1] = CAV_MELODY_NONE; cavSeqMelody[2] = CAV_MELODY_NONE;
    cavOverlayActive = false;

    cavIntervalIndex = 0;
    cavFrameAccum = 0;

    cavBeginTitle();
}

void gameCavit_update()
{
    cavAdvanceSound();

    if( cavState == CAV_STATE_TITLE ) cavUpdateTitle();
    else if( cavState == CAV_STATE_START_JINGLE ) cavUpdateStartJingle();
    else if( cavState == CAV_STATE_PLAYING ) cavUpdatePlaying();
    else if( cavState == CAV_STATE_LOSE_ANIM ) cavUpdateLoseAnim();
    else if( cavState == CAV_STATE_GAMEOVER_JINGLE ) cavUpdateGameOverJingle();
    else if( cavState == CAV_STATE_CLEAR_JINGLE ) cavUpdateClearJingle();
    else if( cavState == CAV_STATE_BONUS_TALLY ) cavUpdateBonusTally();
}
