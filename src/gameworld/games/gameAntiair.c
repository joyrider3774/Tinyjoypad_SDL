#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// =============================================================================
// AntiAir mini (inufuto, UIAPduino+SSD1306+CH32V003 "Cate engine" edition,
// license "None specified" - GitHub reports no LICENSE file for
// `UIAPduino_antiair`, same author/situation as this project's own already-
// shipped CRACKY mini) - a fixed-position shooter: shift a cannon left/right
// along the ground, shoot upward to destroy a small grid of UFOs before their
// falling bombs/blocks bury you. Blocks that fall to the ground stack up and
// can block the cannon's own movement; bombs explode into a "bang" that also
// destroys any block piled at that spot. Clearing every UFO in the current
// wave (after a short pause) reveals a fresh wave on the SAME life, cycling
// through 10 hand-authored wave layouts forever; 3 lives, no persisted
// hi-score (upstream has none at all - confirmed by grep, unlike CRACKY).
//
// Picked as the next Cate-engine port after CRACKY, following that file's own
// architecture directly: `Movable`/`VVram`/`Vram`/nibble-packed `CharPattern`
// glyphs are all the exact same shapes as CRACKY's own (same author, same
// shared display/print plumbing), and - just like CRACKY - **no display-
// orientation transform of any kind is used**: `aarComposeRawByte()` is drawn
// directly at its own (col,page) via `md_drawColumn()`, matching CRACKY's own
// hard-won conclusion (see that file's own header) that the two SSD1306
// hardware remap commands some UIAPduino modules issue exist to compensate
// for a physical panel-mounting quirk with no equivalent to correct for in a
// software recreation.
//
// **A genuine, real circular function-call dependency between 3 modules**
// (Cannon needs Bullet's `CanFire()`/`StartBullet()`; Bullet needs Ufo's
// `HitUfo()` and Block's `HitBlock()`; Block needs Cannon's `HitCannon()`) -
// resolved upstream via separate translation units + header declarations,
// which this dialect's single-pass, top-to-bottom file has no direct
// equivalent for. Broken with the minimum number of real forward
// declarations needed (4: `aarCanFire`/`aarStartBullet` ahead of the Cannon
// section, `aarHitUfo`/`aarHitBlock` ahead of the Bullet section) - matching
// this project's own established precedent that plain forward declarations
// ARE supported by this dialect (confirmed already working in
// `gameTinyPipe.c`'s own `tpipeRunR`/`tpipeRunL`), used here specifically
// because physical reordering alone cannot resolve a genuine 3-way cycle.
// `aarGround[]` (Block's own ground-occupancy array, needed by Cannon's
// `CanMove()`) is declared early as shared global data instead, alongside
// `aarCannon`/`aarCannonLive` (needed by Bullet's `StartBullet()`) - a
// data-only dependency needs no forward declaration at all, just an early
// declaration site.
//
// **VVram/Vram plumbing is structurally identical to CRACKY's own** (same
// `VVramWidth=24`/`VVramHeight=16` grid, same `CharPatternSize=2`-bytes/glyph
// nibble-interleaved packing via `SendUL()`, same `LeftX=24`-cell-offset
// 8-char status column at raw columns 96-127) - reused `crkComposeRawByte()`'s
// exact sub-column math (renamed `aarGlyphSubByte()`, factored into a small
// shared helper since this port also needs the identical math for the "2x2
// icon printed inline with text" case below, not duplicated per Cracky's own
// slightly more repetitive inline version). One real difference from CRACKY:
// AntiAir's own `VVram[]` is declared upstream as a genuine flat 1D byte
// array (`byte VVram[VVramWidth*VVramHeight]`), not row/column 2D - and
// `DrawUfos()` walks it with raw pointer arithmetic that can advance a
// pointer *past* one logical VVram row into the next during normal drawing
// (never actually observed to matter given the formation's own bounded
// oscillation range, but a real characteristic of the original code) -
// ported as a flat `int aarVVram[VVramWidth*VVramHeight]` with an explicit
// flat index instead of Cracky's own `int[16][24]` 2D array, to stay
// faithful to that flat-pointer-arithmetic shape rather than force it into a
// 2D model it wasn't written for.
//
// **Status text placement, traced precisely against upstream's own real
// `LeftX=24`/`VramStep=4` addressing** - `aarStatusChar`/`aarComposeRawByte()`
// now model upstream's own real, genuinely wide 32-char-cell-per-page Vram
// canvas directly (matching CRACKY's own equivalent, already-corrected fix
// - see that file's own `crkStatusChar`/`crkFullWidthText` header comments
// for the full story of the real user-supplied hardware photo that proved
// an earlier, narrower 8-column model wrong for this whole family). SCORE/
// STAGE labels, the score/stage digit values, and the remain-count icons
// all live at their own real, literal upstream columns (24-31, i.e.
// `LeftX`/`LeftX+2`/`LeftX+6`/`LeftX+1`) - no local-offset remapping needed
// anymore, the array index IS the real column. `PrintGameOver()`'s own raw
// column (`8*VramStep=32`) lands *inside* the map area, not the status
// column - the same "message burned directly over the map, relying on real
// VRAM persistence" pattern CRACKY's own `PrintTimeUp`/`PrintGameOver`
// already needed a `crkOverlay*` mechanism for - reused here identically as
// `aarOverlay*`/`aarBeginOverlay`.
//
// **Title() screen text placed at upstream's own real, literal columns**
// (matching CRACKY's own already-corrected fix exactly) - upstream's own
// Title() prints "MINI"/"INUFUTO 2026"/"START"/"CONTINUE" at real columns
// 8-23, genuinely clear of the status labels' own columns 24-31, using the
// exact same shared `PrintC()`/`PrintS()` mechanism the status labels use,
// just at different column arguments - not a separate, narrower canvas at
// all. `aarFullWidthText` (see its own header comment) is what lets
// `aarComposeRawByte()` actually reach those columns instead of being
// artificially confined to the narrow status zone. AntiAir's real title
// ("ANTIAIR") is fully spellable in this game's own 26-glyph font (space,
// digits, '>', A C E F G I M N O R S T U V), and so is the real author
// credit "INUFUTO 2026" in full (both the space and every digit are in that
// same glyph set) - unlike CRACKY, which genuinely needed character
// substitution for its own game name/credit to fit the available glyphs.
// Upstream's own real 5-block bitmap logo is simplified to plain text via
// the same status-text machinery, matching CRACKY's own established
// precedent for that game's own bitmap logo - see `aarBeginTitle()`'s own
// header comment for the exact column placements and the two-round bug
// history (a first, wrong "narrow 8-column grid" model, then this fix).
//
// **The "remain count" lives indicator uses `Put2C()` upstream - a genuine
// 2x2-icon-glyph inline print (not a plain ASCII character)**, traced
// through by hand: it interleaves 4 consecutive `CharPattern` glyph indices
// (c, c+1, c+2, c+3 - the exact same 2x2-composited-sprite convention
// `aarShowSprite()`'s own `AarSprite` compositing uses) into 8 real output
// columns via the identical nibble-pack math as every other glyph draw in
// this engine. Reproduced with a small dedicated dispatch inside
// `aarComposeRawByte()`'s own status-column branch (gated to the exact
// column range `Put2C()` would occupy, using `aarGlyphSubByte()`) rather
// than inventing a general "icon overlay" system for what upstream only
// ever uses in this one place. `RemainCount` only ever starts at 3 and
// decrements (confirmed via a full-project grep - nothing ever increments
// it), so upstream's own `i>2` digit-mode branch (a fallback for 4+ lives)
// is genuinely unreachable dead code here - not ported, noted instead.
//
// **Sound**: the exact same real 3-tone-channel tracker as CRACKY's own
// `Sound.cpp` (same `ToneChannel`/`EffectChannel` classes, same
// `SoundHandler()` structure, same real 60Hz `SysTick`-driven call rate -
// confirmed directly via this game's own `Timer.cpp`, `kTimerHz=60`) - only
// `Tempo` differs (200 here vs CRACKY's 160), giving a different real note-
// duration formula: a channel advances once every `(600/2)/200 = 1.5` real
// 60Hz ticks (vs CRACKY's own 1.875), so `aarNoteFrames(length) = round(
// length * 1.5)`. `Sound_Fire()` (a short one-shot melody, channel 0) and
// `Sound_Start()`/`Sound_GameOver()`/`StartBGM()` (channels 1/2, the latter
// two originally blocking `WaitMelody()` calls converted to explicit
// AAR_STATE_START_JINGLE/GAMEOVER_JINGLE frame-stepped waits, the same
// treatment CRACKY's own CRK_STATE_START_JINGLE/GAMEOVER_JINGLE needed) all
// route through the same `aarStartSeq`/`aarAdvanceOneSeq` 3-slot sequencer
// CRACKY established, advanced every real engine frame independent of
// `AAR_TICK_DIVISOR` - matching upstream's own real SysTick-ISR-independent-
// of-the-main-loop's-`WaitTimer()` structure. `Sound_SmallBang()`/
// `Sound_LargeBang()` are a *different* upstream sound source entirely - a
// genuine white-noise `EffectChannel`, not a `ToneChannel` melody - with no
// equivalent primitive in this project's own `md_playTone()` model;
// approximated as a short, one-shot plain tone at the same nominal
// frequency (3000Hz small bang / 1500Hz large bang) called directly
// (bypassing the 3-slot sequencer entirely, since `md_playTone()` is itself
// already genuinely multi-voice - see this project's own CLAUDE.md write-up
// on that fix - so a direct call here can't collide with a concurrently
// sequenced melody note any more than upstream's own separate Effect
// channel could collide with its own ToneChannels).
//
// **Main.cpp's own real per-tick sub-throttling, ported literally rather
// than collapsed**: the main loop's own `Clock` byte (incrementing once per
// `WaitTimer(3)` cycle = once per `AAR_TICK_DIVISOR`-gated logic tick, i.e.
// a genuine 60/3=20Hz rate) gates `MoveFighter()`/`MoveBlocks()`/
// `UpdateBangs()` to every OTHER logic tick (`Clock&1==0`, ~10Hz) and
// `MoveUfos()` to every 8th (`Clock&7==0`, 2.5Hz), while `MoveBullets()`/
// `UpdateBlocks()`/the redraw run every logic tick (20Hz) - ported as one
// flat `aarClock` counter with the identical bitmask gates, rather than
// trying to further decompose this into separate states the way CRACKY's
// own more elaborate blocking-animation-heavy loop needed; AntiAir's own
// loop has only 2 genuinely blocking calls (`Sound_Start`/`Sound_GameOver`),
// both already covered by the jingle states above.
//
// **A shared `linger` byte with two entirely independent purposes,
// preserved exactly as one shared counter rather than split into two**:
// upstream increments the SAME local `linger` in two separate, non-
// exclusive `if` blocks each real tick (once if `UfoCount==0`, again -
// genuinely a *second* increment on the same tick - if `!CannonLive`, since
// both conditions really can be true simultaneously) - ported as one
// persistent `aarLinger` global with the identical two independent `if`
// blocks, not merged into an `else if`, since a literal replication of this
// double-increment quirk (whatever perceptible effect it has, likely none
// in practice) costs nothing extra to preserve faithfully. Clearing every
// UFO in a wave (`aarLinger>31`, matching upstream's own `250*CoordRate/8`)
// advances `aarCurrentStage` and reloads a fresh wave via `aarInitStage()`
// *without* leaving `AAR_STATE_PLAYING` at all - cannon/bullets/blocks/bangs
// all persist untouched across the transition, exactly matching upstream's
// real "seamless wave-to-wave" structure (no `goto try_`, no jingle). Losing
// the cannon (`aarLinger>25`, matching `200*CoordRate/8`) either restarts
// the current life via `aarBeginTry()` (which, matching `InitPlaying()`
// exactly, resets bullets/cannon/blocks/bangs and calls the FULL
// `aarResetUfos()` - reloading direction/sweep-position but deliberately
// *not* `aarInitUfos()`, so any UFOs already destroyed in the current wave
// stay destroyed across a life loss - a genuine, deliberate upstream design
// choice, not an oversight) or ends the game (`AAR_STATE_GAMEOVER_JINGLE`,
// `RemainCount==0`).
//
// **A genuine AVR/CH32-implicit-byte-wraparound-reliance audit, the same
// bug family this whole project has documented repeatedly** (byte
// truncation/shift wraparound/signed sentinels/rand()-range mismatches/
// logical-vs-arithmetic shifts - see this project's own CLAUDE.md) - found
// and explicitly masked with `& 0xFF` at every site actually load-bearing:
// (1) `Bullet.y` (`byte`) is decremented toward 0 and relies on wrapping to
// 0xFF (255) to trigger its own `y >= Range` removal check once it goes
// negative - without an explicit mask after `--`, this port's own
// non-wrapping `int` would instead go permanently negative, silently
// stranding that bullet slot forever (confirmed by tracing: `CoordMask=0`
// here means the "sub-pixel aligned" gate `(y & CoordMask)==0` is
// unconditionally true, so `Hit()` would still fire on a negative y every
// tick with no meaningful geometry match - the bullet just never frees
// its own slot). (2) `UfoRowCenterX` and its own derived `center`/`x`
// values in `MoveUfos()`/`DrawUfos()`/`HitUfo()` are *designed* to swing
// slightly negative (the formation's own left-bounce math) and rely on byte
// wraparound to stay a small, consistently-representable value either way -
// masked at each assignment, reproducing upstream's own literal
// `static_cast<byte>(...)` casts exactly (confirmed via a script-verified
// derivation that masking at each step reproduces the same true modular
// value the real hardware byte would hold, not just "doesn't crash").
// (3) `Bang::Start()`'s own `x - Size*2`/`y - Size*2` offsets (used for the
// "large bang" 4-piece explosion) can genuinely go negative when the
// cannon/a bomb is destroyed right at the left edge (`x=0`) - masked at the
// single shared `aarBangStartOne()` entry point both `aarStartSmallBang`/
// `aarStartLargeBang` funnel through, so a wrapped value correctly fails
// the existing `x >= AAR_BANG_RANGE_X` off-screen check instead of
// underflowing into a genuine negative array-adjacent write.
// `Ground[]`/`Sprite[]`/`FallingBlock[]` array-neighbor accesses that
// upstream itself leaves entirely unguarded (`pGround[-1]`/`pGround[1]`/
// etc, harmless-on-real-flat-AVR/RISC-V-memory but a genuine out-of-bounds
// read/write risk here) were given the same kind of defensive bounds guard
// this project has added at equivalent sites elsewhere (e.g. Tiny Arena's
// own `Lvl1` fix) - `aarMakeBang()`/`aarMoveBlocks()`/`aarUpdateBlocks()`
// each gate their own `[x-1]`/`[x+1]`-style neighbor lookups against the
// real `0..AAR_WINDOW_WIDTH-1` array bounds before touching them.
//
// **`RndIndex`/`Numbers[32]` (Math.cpp) is byte-identical to CRACKY's own
// `crkRndNumbers[]`** (same author, same shared utility table) - ported the
// same way, a fixed pre-shuffled cycle through 0-31 (reset to index 0 at
// the start of every life via `aarBeginTry()`, matching upstream's own
// `InitPlaying()`), **not** routed through this project's own shared
// `arand()` helper the way most other ports' *real* `rand()`/`random()`
// calls are - this game (like CRACKY) never calls a real RNG at all,
// deterministic-by-design. One difference from CRACKY's own `crkRnd()`:
// this game's `Rnd()` returns the *full* 0-31 value (no `&0x0f` mask),
// confirmed directly against the real `Math.cpp` source.
//
// Dead/unused upstream code, confirmed via grep before being dropped rather
// than guessed: `Movable.h`'s own `LocateMovable`/`IsNear`/`IsOnCellGrid`/
// `IsOnCoordGrid` (declared, never defined or called anywhere in this game -
// this game's own `Movable` struct doesn't even carry the `dx`/`dy` fields
// those functions would need) and its `MapShift`/`MapRate`/`MapMask`
// constants (declared, never referenced); `Ufo.cpp`'s own `RowHeight`
// constant (declared, but every real use of "3 VVram rows per UFO row" in
// the file uses a bare literal `3` instead of the constant); `Bang.h`'s own
// `StartBang()` (declared, never defined - only `StartSmallBang`/
// `StartLargeBang` exist); `Print.cpp`'s own `PrintByteNumber3` (defined,
// never called - `PrintByteNumber2` is the only one AntiAir's own
// `Status.cpp` actually uses).
//
// Ported mechanically from the sibling tinyjoypad_vircon32 project's own
// already-verified-correct `gameAntiair.c` (see that file's own extensive
// header comment there for its full bug-hunt/Puppeteer-verification
// history) - dialect conversion only (int[N] name -> TYPE name[N]/[N][M],
// int*/int[] string literals -> char*/char[]), no game-logic changes made
// here. forceRedraw not needed: aarRender() runs unconditionally on every
// real state (title/start-jingle/playing/gameover-jingle), so there's no
// dirty-flag-skip-frame case a resume hook would need to force through.
// =============================================================================

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into aarCharPattern (map tiles / sprites / icons)
// -----------------------------------------------------------------------------

#define AAR_CHAR_SPACE 0x00
#define AAR_CHAR_LOGO 0x00
#define AAR_CHAR_CANNON 0x10
#define AAR_CHAR_CANNON_ON 0x10
#define AAR_CHAR_CANNON_OFF 0x14
#define AAR_CHAR_BULLET 0x18
#define AAR_CHAR_BLOCK 0x1C
#define AAR_CHAR_BLOCK_A 0x1C
#define AAR_CHAR_BLOCK_B 0x20
#define AAR_CHAR_BLOCK_C 0x24
#define AAR_CHAR_BANG 0x28
#define AAR_CHAR_SMALL_BANG 0x28
#define AAR_CHAR_LARGE_BANG 0x2C
#define AAR_CHAR_UFO 0x3C
#define AAR_CHAR_END 0x44

// remain-count ("lives") icon reuses the cannon-on glyph, matching upstream
#define AAR_CHAR_REMAIN AAR_CHAR_CANNON

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define AAR_COORD_SHIFT 0
#define AAR_COORD_RATE ( 1 << AAR_COORD_SHIFT )
#define AAR_COORD_MASK ( AAR_COORD_RATE - 1 )

typedef struct
{
    int x, y;
    int sprite;
} AarMovable;

// -----------------------------------------------------------------------------
//   Stage.h / Stages.h
// -----------------------------------------------------------------------------

#define AAR_ROW_COUNT 3
#define AAR_COLUMN_COUNT 4
#define AAR_STAGE_COUNT 10
#define AAR_UFO_BYTES_PER_ROW 1

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define AAR_VVRAM_WIDTH 24
#define AAR_VVRAM_HEIGHT 16
#define AAR_WINDOW_WIDTH AAR_VVRAM_WIDTH
#define AAR_WINDOW_HEIGHT AAR_VVRAM_HEIGHT

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define AAR_SPRITE_CANNON 0
#define AAR_SPRITE_BULLET 1
#define AAR_MAX_BULLET_COUNT 2
#define AAR_SPRITE_FALLING_BLOCK 3
#define AAR_MAX_FALLING_BLOCK_COUNT 8
#define AAR_SPRITE_BANG 11
#define AAR_MAX_BANG_COUNT 12
#define AAR_SPRITE_END 23
#define AAR_INVALID_PATTERN 0xff
#define AAR_INVALID_Y 0xff

// -----------------------------------------------------------------------------
//   Ufo.h
// -----------------------------------------------------------------------------

#define AAR_UNIT_WIDTH 4
#define AAR_UFO_TYPE_MASK 0x03
#define AAR_UFO_TIME 0x04
#define AAR_UFO_TIME_MASK 0x1c

// -----------------------------------------------------------------------------
//   Block.h
// -----------------------------------------------------------------------------

#define AAR_TYPE_BLOCK 0x01
#define AAR_TYPE_BOMB 0x02
#define AAR_TYPE_TIME_BOMB 0x03
#define AAR_BLOCK_TYPE_MASK 0x03

#define AAR_GROUND_LEFT_BLOCK 0x01
#define AAR_GROUND_RIGHT_BLOCK 0x02
#define AAR_GROUND_BLOCK_MASK 0x03
#define AAR_GROUND_LEFT_BOMB 0x04
#define AAR_GROUND_RIGHT_BOMB 0x08
#define AAR_GROUND_BOMB_MASK 0x0c
#define AAR_GROUND_TIME 0x10
#define AAR_GROUND_TIME_MASK 0xf0

// -----------------------------------------------------------------------------
//   Bang.h
// -----------------------------------------------------------------------------

#define AAR_BANG_SIZE ( 1 * AAR_COORD_RATE )
#define AAR_BANG_TIME 8
#define AAR_BANG_RANGE_X ( AAR_WINDOW_WIDTH * AAR_COORD_RATE - 1 )

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching upstream's own enum exactly.
// -----------------------------------------------------------------------------

#define AAR_N8 6
#define AAR_N8L 8
#define AAR_N8R 4
#define AAR_N8P ( AAR_N8 * 3 / 2 )
#define AAR_N4 ( AAR_N8 * 2 )
#define AAR_N4P ( AAR_N4 * 3 / 2 )
#define AAR_N2 ( AAR_N4 * 2 )
#define AAR_N2P ( AAR_N2 * 3 / 2 )
#define AAR_N1 ( AAR_N2 * 2 )
#define AAR_N16 ( AAR_N8 / 2 )

#define AAR_E2 1
#define AAR_F2 2
#define AAR_F2S 3
#define AAR_G2 4
#define AAR_G2S 5
#define AAR_A2 6
#define AAR_A2S 7
#define AAR_B2 8
#define AAR_C3 9
#define AAR_C3S 10
#define AAR_D3 11
#define AAR_D3S 12
#define AAR_E3 13
#define AAR_F3 14
#define AAR_F3S 15
#define AAR_G3 16
#define AAR_G3S 17
#define AAR_A3 18
#define AAR_A3S 19
#define AAR_B3 20
#define AAR_C4 21
#define AAR_C4S 22
#define AAR_D4 23
#define AAR_D4S 24
#define AAR_E4 25
#define AAR_F4 26
#define AAR_F4S 27
#define AAR_G4 28
#define AAR_G4S 29
#define AAR_A4 30
#define AAR_A4S 31
#define AAR_B4 32
#define AAR_C5 33
#define AAR_C5S 34
#define AAR_D5 35
#define AAR_D5S 36
#define AAR_E5 37
#define AAR_F5 38
#define AAR_F5S 39
#define AAR_G5 40

// Sound sequencer melody ids, resolved by aarMelodyLength()/aarMelodyValue()
// instead of a real pointer-per-channel (this project's own established
// "resolve by id" pattern, e.g. Tiny Dungeon's own bitmap-array resolver).
#define AAR_MELODY_NONE 0
#define AAR_MELODY_FIRE 1
#define AAR_MELODY_START 2
#define AAR_MELODY_GAMEOVER 3
#define AAR_MELODY_BGM1 4
#define AAR_MELODY_BGM2 5

// -----------------------------------------------------------------------------
//   Data tables - script-extracted/verified from the real upstream source,
//   not hand-copied (this project's own established anti-transcription-bug
//   discipline - see CLAUDE.md's own "byte-diff transcribed tables" lesson).
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNORSTUV" (26 glyphs, note: no 'P',
// unlike CRACKY's own copy of this table), 4 bytes/glyph.
int aarAsciiPattern[104] = {
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
    0x0e, 0x11, 0x0e, 0x00, 0x1f, 0x05, 0x1a, 0x00,
    0x16, 0x15, 0x0d, 0x00, 0x01, 0x1f, 0x01, 0x00,
    0x1f, 0x10, 0x1f, 0x00, 0x0f, 0x10, 0x0f, 0x00,
};

// CharPattern - 68 glyphs (logo/dither variants, cannon, bullet, blocks,
// bangs, ufo), 2 bytes/glyph (a 4x4-pixel block, nibble-interleaved).
int aarCharPattern[136] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x00, 0xfc, 0x0c, 0x00, 0xba, 0xb9, 0xb9, 0x0a,
    0x00, 0x00, 0x00, 0x00, 0x88, 0x88, 0x88, 0x08,
    0x00, 0xfc, 0x0c, 0x00, 0x32, 0xb5, 0x35, 0x02,
    0x1e, 0xdd, 0xdd, 0xe1, 0x87, 0xbb, 0xbb, 0x78,
    0xeb, 0xfc, 0xec, 0x0b, 0x53, 0xdf, 0x5f, 0x03,
    0x80, 0xec, 0x9d, 0x12, 0x73, 0xff, 0x5b, 0x03,
    0xe4, 0xb6, 0x4a, 0x4e, 0x72, 0xe2, 0x25, 0x17,
    0x00, 0xc2, 0x84, 0x7c, 0x62, 0xff, 0xe9, 0x6f,
    0xce, 0xec, 0x07, 0x88, 0xdd, 0x36, 0x3f, 0x01,
    0x64, 0xdb, 0x7b, 0xe2, 0x00, 0x36, 0x11, 0xe2,
    0x97, 0xab, 0x58, 0x46, 0x12, 0x31, 0x23, 0x00,
    0xc0, 0xee, 0xff, 0xf9, 0x9f, 0xff, 0xee, 0x0c,
    0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
};

// TitleBytes - upstream's own real title-screen logo bitmap (Status.cpp's
// `Title()`), 5 4x4-glyph blocks (80 values total), script-extracted and
// verified against the real upstream source byte-for-byte. Every value here
// is a valid index into aarCharPattern[]'s own "logo" range (indices 0-15,
// the first 32 bytes of that table, byte-identical to CRACKY's own copy of
// the same shared block-pattern palette) - reused here to build the game's
// own pixel-art wordmark, matching CRACKY's own crkTitleBytes exactly in
// shape (just 5 blocks instead of 6). See aarBeginTitle()'s own comment for
// why this replaces the earlier plain-text "ANTIAIR" substitute.
int aarTitleBytes[80] = {
    0x0e, 0x05, 0x0b, 0x0c, 0x0f, 0x00, 0x0f, 0x0c,
    0x0f, 0x05, 0x0f, 0x04, 0x05, 0x00, 0x05, 0x00,
    0x0b, 0x0c, 0x03, 0x05, 0x07, 0x0f, 0x03, 0x00,
    0x01, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x05, 0x0c, 0x03, 0x0f, 0x00, 0x0c, 0x03,
    0x05, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x07, 0x0d, 0x02, 0x0c, 0x03, 0x0c, 0x03,
    0x0c, 0x07, 0x0d, 0x03, 0x04, 0x01, 0x04, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x0c, 0x07, 0x0b,
    0x0f, 0x0c, 0x0b, 0x07, 0x05, 0x04, 0x01, 0x05,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int aarFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

// Sound_Fire() - 6 rapid one-shot notes, literal length "1" (not one of the
// NoteLength constants) each, matching upstream exactly.
int aarMelodyFire[13] = {
    1, AAR_F5, 1, AAR_D5S, 1, AAR_C5S, 1, AAR_B4, 1, AAR_A4,
    1, AAR_G5, 0,
};

// Sound_Start()
int aarMelodyStart[23] = {
    AAR_N8, AAR_A4, AAR_N4, AAR_B4, AAR_N4, AAR_C5, AAR_N4, AAR_C5, AAR_N8, AAR_A4,
    AAR_N4, AAR_D5, AAR_N4, AAR_D5, AAR_N8, AAR_C5, AAR_N4P, AAR_D5, AAR_N2P, AAR_E5,
    AAR_N4, 0, 0,
};

// Sound_GameOver()
int aarMelodyGameOver[19] = {
    AAR_N4, AAR_A4, AAR_N8, AAR_E4, AAR_N8, AAR_A4, AAR_N8, AAR_G4, AAR_N8, AAR_F4,
    AAR_N8, AAR_E4, AAR_N8, AAR_D4, AAR_N2P, AAR_E4, AAR_N4, 0, 0,
};

// StartBGM() voice A (channel 1) - loops via the trailing 255 (Repeat) marker.
int aarMelodyBgm1[105] = {
    AAR_N4P, AAR_A4, AAR_N4P, AAR_B4, AAR_N2, AAR_C5, AAR_N4, AAR_C5, AAR_N4, AAR_B4,
    AAR_N4, AAR_C5, AAR_N4P, AAR_B4, AAR_N4P, AAR_G4, AAR_N2P, AAR_G4, AAR_N2, 0,
    AAR_N4P, AAR_A4, AAR_N4P, AAR_B4, AAR_N2, AAR_C5, AAR_N4, AAR_C5, AAR_N4, AAR_B4,
    AAR_N4, AAR_C5, AAR_N4P, AAR_G5, AAR_N4P, AAR_D5, AAR_N2P, AAR_D5, AAR_N2, 0,
    AAR_N4P, AAR_F5, AAR_N4P, AAR_E5, AAR_N2, AAR_F5, AAR_N4, AAR_F5, AAR_N4, AAR_E5,
    AAR_N4, AAR_F5, AAR_N4P, AAR_E5, AAR_N4P, AAR_C5, AAR_N2P, AAR_C5, AAR_N2, 0,
    AAR_N8, AAR_A4, AAR_N8, AAR_A4, AAR_N8, AAR_B4, AAR_N4, AAR_C5, AAR_N4, AAR_C5,
    AAR_N8, AAR_C5, AAR_N8, AAR_B4, AAR_N8, AAR_B4, AAR_N8, AAR_C5, AAR_N4, AAR_D5,
    AAR_N4, AAR_D5, AAR_N8, AAR_D5, AAR_N8, AAR_C5, AAR_N8, AAR_C5, AAR_N8, AAR_D5,
    AAR_N4, AAR_E5, AAR_N4, AAR_E5, AAR_N8, AAR_E5, AAR_N8, AAR_F5, AAR_N4, AAR_F5,
    AAR_N4P, AAR_E5, AAR_N4, 0, 0xff,
};

// StartBGM() voice B (channel 2) - loops via the trailing 255 (Repeat) marker.
int aarMelodyBgm2[227] = {
    AAR_N4, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2,
    AAR_N8, 0, AAR_N8, AAR_C3S, AAR_N4, AAR_D3, AAR_N8, 0, AAR_N8, AAR_D3,
    AAR_N8, 0, AAR_N8, AAR_D3, AAR_N8, 0, AAR_N8, AAR_D3, AAR_N4, AAR_E3,
    AAR_N8, 0, AAR_N8, AAR_E3, AAR_N8, 0, AAR_N8, AAR_E3, AAR_N8, 0,
    AAR_N8, AAR_G3S, AAR_N4, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, 0,
    AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N4, AAR_A2, AAR_N8, 0,
    AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_E3,
    AAR_N4, AAR_F3, AAR_N8, 0, AAR_N8, AAR_F3, AAR_N8, 0, AAR_N8, AAR_F3,
    AAR_N8, 0, AAR_N8, AAR_F3S, AAR_N4, AAR_G3, AAR_N8, 0, AAR_N8, AAR_G3,
    AAR_N8, 0, AAR_N8, AAR_G3, AAR_N8, 0, AAR_N8, AAR_G3, AAR_N4, AAR_E3,
    AAR_N8, 0, AAR_N8, AAR_E3, AAR_N8, 0, AAR_N8, AAR_E3, AAR_N8, 0,
    AAR_N8, AAR_E3, AAR_N4, AAR_D3, AAR_N8, 0, AAR_N8, AAR_D3, AAR_N8, 0,
    AAR_N8, AAR_D3, AAR_N8, 0, AAR_N8, AAR_A2S, AAR_N4, AAR_G3, AAR_N8, 0,
    AAR_N8, AAR_G3, AAR_N8, 0, AAR_N8, AAR_G3, AAR_N8, 0, AAR_N8, AAR_G3,
    AAR_N4, AAR_C3, AAR_N8, 0, AAR_N8, AAR_C3, AAR_N8, 0, AAR_N8, AAR_C3,
    AAR_N8, 0, AAR_N8, AAR_C3, AAR_N4, AAR_C3, AAR_N8, 0, AAR_N8, AAR_C3,
    AAR_N8, 0, AAR_N8, AAR_C3, AAR_N8, 0, AAR_N8, AAR_C3, AAR_N8, AAR_A2,
    AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2,
    AAR_N8, 0, AAR_N8, AAR_C3S, AAR_N8, AAR_D3, AAR_N8, AAR_D3, AAR_N8, 0,
    AAR_N8, AAR_D3, AAR_N8, 0, AAR_N8, AAR_D3, AAR_N8, 0, AAR_N8, AAR_G3S,
    AAR_N8, AAR_A2, AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, 0,
    AAR_N8, AAR_A2, AAR_N8, 0, AAR_N8, AAR_A2, AAR_N8, AAR_F3, AAR_N8, AAR_F3,
    AAR_N8, 0, AAR_N4P, AAR_E3, AAR_N4, 0, 0xff,
};

// Stages.cpp - 10 waves, 1 byte/row (AAR_UFO_BYTES_PER_ROW=1, since
// (ColumnCount+3)/4*RowCount / RowCount == 1 given ColumnCount=4), each byte
// packing 4x 2-bit UFO-type values (0=empty, 1-3=UFO type).
int aarStageBytes[10][3] = {
    { 0x3f, 0x14, 0x00 },
    { 0x36, 0x1a, 0x00 },
    { 0x1d, 0x26, 0x00 },
    { 0x18, 0x2d, 0x00 },
    { 0x1f, 0x3a, 0x00 },
    { 0xf9, 0x9a, 0x00 },
    { 0x79, 0xb9, 0x18 },
    { 0x6e, 0xae, 0x16 },
    { 0xae, 0xae, 0x16 },
    { 0xbf, 0x57, 0xbe },
};

// Points awarded for destroying a type-1/2/3 UFO or block (shared table,
// matches upstream's own two separately-declared-but-identical copies).
int aarPoints[3] = { 10, 4, 6 };

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int aarScore;
int aarCurrentStage;
int aarRemainCount;
int aarStageIndex;

int aarVVram[AAR_VVRAM_WIDTH * AAR_VVRAM_HEIGHT];
// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize selects the page, VramStep=4 real pixels
// per char-cell, 128 real pixels / 4 = 32 cells per row) - widened from an
// original, wrong `[8][8]` after a real user-supplied hardware photo of the
// sibling CRACKY port's own title screen proved that narrow model flatly
// incorrect for this whole family. See aarFullWidthText's own header
// comment for the full story and gameCracky.c's own matching fix.
int aarStatusChar[8][32];
int aarRemainIconCount;

// Set true only while on the title screen (AAR_STATE_TITLE) - upstream's
// real Title() drives the ENTIRE screen (not just the narrow status column)
// through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range (the logo, "MINI", "START"/
// "CONTINUE", the credit line all live at columns 8-23, well inside what
// during gameplay is the map area). When true, aarComposeRawByte() reads
// aarStatusChar across the full width instead of just the narrow status
// zone (columns 24-31), letting the title screen use that real estate
// instead of being artificially confined to the status-only zone.
bool aarFullWidthText;

// message overlay burned directly over the map area, matching upstream's own
// Vram-direct PrintGameOver() writes - see header comment.
bool aarOverlayActive;
int aarOverlayText[10];
int aarOverlayLen;
int aarOverlayPage;
int aarOverlayCol;

typedef struct
{
    int x, y;
    int pattern;
} AarSprite;
AarSprite aarSprites[AAR_SPRITE_END];

// Cannon + Ground declared early (shared data only, no function-ordering
// dependency) - see this file's own header comment on the real Cannon <->
// Bullet <-> Block <-> Cannon circular dependency this resolves.
AarMovable aarCannon;
bool aarCannonLive;
int aarGround[AAR_WINDOW_WIDTH];

AarMovable aarBullets[AAR_MAX_BULLET_COUNT];
int aarBulletIntervalCount;
int aarBulletCount;

typedef struct
{
    AarMovable m;
    int flags;
} AarFallingBlock;
AarFallingBlock aarFallingBlocks[AAR_MAX_FALLING_BLOCK_COUNT];

typedef struct
{
    AarMovable m;
    int pattern;
    int clock;
} AarBang;
AarBang aarBangs[AAR_MAX_BANG_COUNT];

int aarUfoColumns[AAR_ROW_COUNT][AAR_COLUMN_COUNT];
int aarUfoRowMemberCount[AAR_ROW_COUNT];
int aarUfoCount;
int aarUfoRowCenterX;
int aarUfoDirection;
int aarUfoLeftSpace;
int aarUfoRightEnd;
int aarUfoBombClock;

int aarRndIndex;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of AAR_TICK_DIVISOR.
int aarSeqMelody[3];
int aarSeqPos[3];
int aarSeqWait[3];
int aarSeqActive[3];

#define AAR_TICK_DIVISOR 3
int aarTickCounter;
int aarClock;
int aarLinger;

#define AAR_STATE_TITLE 0
#define AAR_STATE_START_JINGLE 1
#define AAR_STATE_PLAYING 2
#define AAR_STATE_GAMEOVER_JINGLE 3
int aarState;
int aarSelection;
bool aarSelectionChanged;
int aarPrevLeft, aarPrevRight, aarPrevUp, aarPrevDown, aarPrevFire;
bool aarPendingContinue;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int aarRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

int aarRnd()
{
    int r;
    r = aarRndNumbers[ aarRndIndex ];
    aarRndIndex = aarRndIndex + 1;
    if( aarRndIndex >= 32 )
      aarRndIndex = 0;
    return r;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites directly into aarVVram (a flat WxH buffer,
//   matching upstream's own flat byte array rather than a 2D grid).
// -----------------------------------------------------------------------------

void aarHideAllSprites()
{
    int i;
    for( i = 0; i < AAR_SPRITE_END; i = i + 1 )
      aarSprites[ i ].pattern = AAR_INVALID_PATTERN;
}

void aarShowSprite( AarMovable* pMovable, int pattern )
{
    aarSprites[ pMovable->sprite ].x = pMovable->x;
    aarSprites[ pMovable->sprite ].y = pMovable->y;
    aarSprites[ pMovable->sprite ].pattern = pattern;
}

void aarHideSprite( int index )
{
    aarSprites[ index ].pattern = AAR_INVALID_PATTERN;
}

void aarDrawSprites()
{
    int i;
    for( i = 0; i < AAR_SPRITE_END; i = i + 1 )
    {
        if( aarSprites[ i ].pattern != AAR_INVALID_PATTERN )
        {
            int x, y, c, row;
            x = aarSprites[ i ].x;
            y = aarSprites[ i ].y;
            c = aarSprites[ i ].pattern;
            for( row = 0; row < 2; row = row + 1 )
            {
                if( y < AAR_VVRAM_HEIGHT )
                {
                    int col;
                    for( col = 0; col < 2; col = col + 1 )
                    {
                        if( x < AAR_VVRAM_WIDTH )
                          aarVVram[ y * AAR_VVRAM_WIDTH + x ] = c;
                        c = c + 1;
                        x = x + 1;
                    }
                    x = x - 2;
                }
                else
                  c = c + 2;
                y = y + 1;
            }
        }
    }
}


// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status text written into aarStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area).
// -----------------------------------------------------------------------------

int aarAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNORSTUV" - direct port of PrintC()'s
    // own linear search (only 26 entries, no cost concern doing this live).
    int table[26] = {
        ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '>',
        'A', 'C', 'E', 'F', 'G', 'I', 'M', 'N', 'O', 'R', 'S', 'T', 'U', 'V',
    };
    int i;
    for( i = 0; i < 26; i = i + 1 )
    {
        if( table[ i ] == c )
          return i;
    }
    return 0;
}

int aarPrintC( int page, int col, int c )
{
    // Defensive bounds guard, NOT present upstream (real hardware PrintC()
    // writes to an arbitrary raw VRAM column with no bounds to overflow at
    // all) - aarStatusChar is now a real 32-wide array matching upstream's
    // own genuine per-page character-cell width, so this guard is a pure
    // safety net rather than something any real call site should ever
    // actually hit - kept as a second layer of protection against the same
    // overflow shape found once before under the original, wrong 8-wide
    // model (see aarFullWidthText's own header comment for the full story).
    if( col >= 0 && col < 32 )
      aarStatusChar[ page ][ col ] = aarAsciiIndex( c );
    return col + 1;
}

int aarPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = aarPrintC( page, col, s[ i ] );
    return col;
}

int aarPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      aarPrintC( page, col, ' ' );
    else
      aarPrintC( page, col, d1 + '0' );
    return aarPrintC( page, col + 1, ( b % 10 ) + '0' );
}

int aarPrintNumber5( int page, int col, int w )
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
          aarPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            aarPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    return aarPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+6 etc), not a narrow local 0-7 offset - see
// aarStatusChar's/aarFullWidthText's own header comments for why this
// changed from the original, too-narrow model.
void aarPrintScore()
{
    aarPrintNumber5( 1, 26, aarScore );
    aarPrintC( 1, 31, '0' );
}

void aarPrintStage()
{
    aarPrintByteNumber2( 3, 30, aarCurrentStage + 1 );
}

void aarPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };

    aarPrintS( 0, 24, sScore, 5 );
    aarPrintS( 3, 24, sStage, 5 );

    // RemainCount only ever starts at 3 and decrements (confirmed via grep -
    // nothing ever increments it), so it can only ever be 1, 2, or 3 here -
    // upstream's own "digit mode" fallback for RemainCount>3 is unreachable
    // dead code, not ported (see this file's own header comment).
    if( aarRemainCount > 1 )
      aarRemainIconCount = aarRemainCount - 1;
    else
      aarRemainIconCount = 0;

    aarPrintScore();
    aarPrintStage();
}

void aarBeginOverlay( char* s, int len, int page, int col )
{
    int i;
    aarOverlayActive = true;
    aarOverlayLen = len;
    aarOverlayPage = page;
    aarOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      aarOverlayText[ i ] = s[ i ];
}

void aarPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    aarBeginOverlay( s, 9, 4, 8 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int aarMelodyLength( int id )
{
    if( id == AAR_MELODY_FIRE ) return 13;
    if( id == AAR_MELODY_START ) return 23;
    if( id == AAR_MELODY_GAMEOVER ) return 19;
    if( id == AAR_MELODY_BGM1 ) return 105;
    if( id == AAR_MELODY_BGM2 ) return 227;
    return 0;
}

int aarMelodyValue( int id, int idx )
{
    if( id == AAR_MELODY_FIRE ) return aarMelodyFire[ idx ];
    if( id == AAR_MELODY_START ) return aarMelodyStart[ idx ];
    if( id == AAR_MELODY_GAMEOVER ) return aarMelodyGameOver[ idx ];
    if( id == AAR_MELODY_BGM1 ) return aarMelodyBgm1[ idx ];
    if( id == AAR_MELODY_BGM2 ) return aarMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/AAR_TEMPO(200) = 1.5 real 60Hz ticks - see header comment.
int aarNoteFrames( int length )
{
    return (int)( length * 1.5 + 0.5 );
}

void aarStartSeq( int channel, int melodyId )
{
    aarSeqMelody[ channel ] = melodyId;
    aarSeqPos[ channel ] = 0;
    aarSeqWait[ channel ] = 0;
    aarSeqActive[ channel ] = 1;
}

void aarStopSeq( int channel )
{
    aarSeqActive[ channel ] = 0;
    aarSeqMelody[ channel ] = AAR_MELODY_NONE;
}

bool aarSeqPlaying( int channel )
{
    return aarSeqActive[ channel ] != 0;
}

void aarAdvanceOneSeq( int channel )
{
    int length, note;

    if( aarSeqActive[ channel ] == 0 ) return;

    if( aarSeqWait[ channel ] > 0 )
    {
        aarSeqWait[ channel ] = aarSeqWait[ channel ] - 1;
        return;
    }

    length = aarMelodyValue( aarSeqMelody[ channel ], aarSeqPos[ channel ] );
    if( length == 0 )
    {
        aarStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        aarSeqPos[ channel ] = 0;
        length = aarMelodyValue( aarSeqMelody[ channel ], 0 );
    }
    note = aarMelodyValue( aarSeqMelody[ channel ], aarSeqPos[ channel ] + 1 );
    aarSeqPos[ channel ] = aarSeqPos[ channel ] + 2;
    aarSeqWait[ channel ] = aarNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)aarFrequencies[ note - 1 ], (float)aarSeqWait[ channel ] / 60.0 );
}

void aarAdvanceSound()
{
    aarAdvanceOneSeq( 0 );
    aarAdvanceOneSeq( 1 );
    aarAdvanceOneSeq( 2 );
}

void aarStartBgm()
{
    aarStartSeq( 1, AAR_MELODY_BGM1 );
    aarStartSeq( 2, AAR_MELODY_BGM2 );
}

void aarStopBgm()
{
    // Upstream's own StopBGM() resets ALL 3 ToneChannels (including channel
    // 0, the one-shot Fire SFX), not just the 2 BGM voices - stop channel 0's
    // sequencer bookkeeping too so a Fire jingle that happened to still be
    // mid-playback the instant the cannon dies doesn't keep advancing/
    // re-triggering md_playTone() afterward.
    aarStopSeq( 0 );
    aarStopSeq( 1 );
    aarStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void aarAddScore( int pts )
{
    // upstream has no hi-score tracking at all (confirmed via grep) -
    // unlike CRACKY, plain session score only.
    aarScore = aarScore + pts;
    aarPrintScore();
}


// -----------------------------------------------------------------------------
//   Bang.cpp
// -----------------------------------------------------------------------------

void aarInitBangs()
{
    int i, sprite;
    sprite = AAR_SPRITE_BANG;
    for( i = 0; i < AAR_MAX_BANG_COUNT; i = i + 1 )
    {
        aarBangs[ i ].m.y = AAR_INVALID_Y;
        aarBangs[ i ].m.sprite = sprite;
        aarHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void aarBangShow( AarBang* pBang )
{
    aarShowSprite( &pBang->m, pBang->pattern );
}

// Shared entry point for both StartSmallBang/StartLargeBang - masks x/y with
// & 0xFF right here so a negative offset (a bang started right at the left
// edge, x=0) wraps exactly like a real AVR/RISC-V byte would, correctly
// failing the off-screen check below instead of underflowing into a real
// negative array-adjacent access (see this file's own header comment).
void aarBangStartOne( int x, int y, int pattern )
{
    int i;
    x = x & 0xFF;
    y = y & 0xFF;
    if( x >= AAR_BANG_RANGE_X ) return;
    for( i = 0; i < AAR_MAX_BANG_COUNT; i = i + 1 )
    {
        if( aarBangs[ i ].m.y != AAR_INVALID_Y ) continue;
        aarBangs[ i ].m.x = x;
        aarBangs[ i ].m.y = y;
        aarBangs[ i ].clock = 0;
        aarBangs[ i ].pattern = pattern;
        aarBangShow( &aarBangs[ i ] );
        return;
    }
}

void aarStartSmallBang( int x, int y )
{
    aarBangStartOne( x - AAR_BANG_SIZE, y - AAR_BANG_SIZE, AAR_CHAR_SMALL_BANG );
}

void aarStartLargeBang( int x, int y )
{
    aarBangStartOne( x - AAR_BANG_SIZE * 2, y - AAR_BANG_SIZE * 2, AAR_CHAR_LARGE_BANG + 0 * 4 );
    aarBangStartOne( x, y - AAR_BANG_SIZE * 2, AAR_CHAR_LARGE_BANG + 1 * 4 );
    aarBangStartOne( x - AAR_BANG_SIZE * 2, y, AAR_CHAR_LARGE_BANG + 2 * 4 );
    aarBangStartOne( x, y, AAR_CHAR_LARGE_BANG + 3 * 4 );
}

void aarUpdateBangs()
{
    int i;
    for( i = 0; i < AAR_MAX_BANG_COUNT; i = i + 1 )
    {
        if( aarBangs[ i ].m.y == AAR_INVALID_Y ) continue;
        aarBangs[ i ].clock = aarBangs[ i ].clock + 1;
        if( aarBangs[ i ].clock >= AAR_BANG_TIME )
        {
            aarHideSprite( aarBangs[ i ].m.sprite );
            aarBangs[ i ].m.y = AAR_INVALID_Y;
        }
        else
          aarBangShow( &aarBangs[ i ] );
    }
}


// -----------------------------------------------------------------------------
//   Forward declarations needed to break the real Cannon <-> Bullet <->
//   {Ufo,Block} <-> Cannon circular dependency - see this file's own header
//   comment. Confirmed working syntax, already proven in this project by
//   gameTinyPipe.c's own tpipeRunR/tpipeRunL forward declarations.
// -----------------------------------------------------------------------------

bool aarCanFire();
void aarStartBullet();


// -----------------------------------------------------------------------------
//   Cannon.cpp
// -----------------------------------------------------------------------------

#define AAR_CANNON_INITIAL_X ( ( AAR_WINDOW_WIDTH / 2 - 1 ) * AAR_COORD_RATE )
#define AAR_CANNON_INITIAL_Y ( ( AAR_WINDOW_HEIGHT - 2 ) * AAR_COORD_RATE )

void aarCannonShow()
{
    int pattern;
    if( !aarCannonLive ) return;
    if( aarCanFire() )
      pattern = AAR_CHAR_CANNON_ON;
    else
      pattern = AAR_CHAR_CANNON_OFF;
    aarShowSprite( &aarCannon, pattern );
}

void aarInitCannon()
{
    aarCannon.sprite = AAR_SPRITE_CANNON;
    aarCannon.x = AAR_CANNON_INITIAL_X;
    aarCannon.y = AAR_CANNON_INITIAL_Y;
    aarCannonLive = true;
    aarCannonShow();
}

bool aarCannonCanMove( int dx )
{
    int x;
    if( ( aarCannon.x & AAR_COORD_MASK ) != 0 ) return true;
    x = aarCannon.x >> AAR_COORD_SHIFT;
    if( dx > 0 )
      x = x + 2;
    else
      x = x - 1;
    return aarGround[ x ] == 0;
}

void aarMoveFighter()
{
    if( !aarCannonLive ) return;
    if( isLeftPressed() && aarCannon.x > 0 && aarCannonCanMove( -1 ) )
      aarCannon.x = aarCannon.x - 1;
    if( isRightPressed() && aarCannon.x < ( AAR_WINDOW_WIDTH - 2 ) * AAR_COORD_RATE && aarCannonCanMove( 1 ) )
      aarCannon.x = aarCannon.x + 1;
    aarCannonShow();
    if( isFirePressed() )
      aarStartBullet();
}

bool aarHitCannon( int x, int y, int width )
{
    if( !aarCannonLive ) return false;
    if(
        x < aarCannon.x + AAR_COORD_RATE * 2 && aarCannon.x < x + width &&
        y < aarCannon.y + AAR_COORD_RATE * 2 && aarCannon.y < y + AAR_COORD_RATE * 2
    )
    {
        aarHideSprite( AAR_SPRITE_CANNON );
        aarCannonLive = false;
        md_playTone( 1500.0, 0.15 );
        aarStartLargeBang( aarCannon.x + AAR_COORD_RATE, aarCannon.y + AAR_COORD_RATE );
        return true;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Forward declarations needed by Bullet.cpp (Ufo/Block modules are
//   defined later, after Bullet, to keep Block able to call Cannon directly)
// -----------------------------------------------------------------------------

bool aarHitUfo( int bulletX, int bulletY );
bool aarHitBlock( int bulletX, int bulletY );


// -----------------------------------------------------------------------------
//   Bullet.cpp
// -----------------------------------------------------------------------------

#define AAR_BULLET_INTERVAL ( 9 * AAR_COORD_RATE )
#define AAR_BULLET_RANGE ( AAR_WINDOW_HEIGHT * AAR_COORD_RATE )

void aarInitBullets()
{
    int i, sprite;
    sprite = AAR_SPRITE_BULLET;
    for( i = 0; i < AAR_MAX_BULLET_COUNT; i = i + 1 )
    {
        aarBullets[ i ].sprite = sprite;
        aarBullets[ i ].y = AAR_INVALID_Y;
        aarHideSprite( sprite );
        sprite = sprite + 1;
    }
    aarBulletIntervalCount = 0;
    aarBulletCount = 0;
}

void aarBulletShow( AarMovable* pBullet )
{
    aarShowSprite( pBullet, AAR_CHAR_BULLET );
}

void aarBulletEnd( AarMovable* pBullet )
{
    pBullet->y = AAR_INVALID_Y;
    aarHideSprite( pBullet->sprite );
    aarBulletCount = aarBulletCount - 1;
}

bool aarCanFire()
{
    return aarBulletIntervalCount == 0 && aarBulletCount < AAR_MAX_BULLET_COUNT;
}

void aarStartBullet()
{
    int i;
    if( aarBulletIntervalCount != 0 ) return;
    for( i = 0; i < AAR_MAX_BULLET_COUNT; i = i + 1 )
    {
        if( aarBullets[ i ].y < AAR_BULLET_RANGE ) continue;
        aarStartSeq( 0, AAR_MELODY_FIRE );
        aarBullets[ i ].x = aarCannon.x;
        aarBullets[ i ].y = aarCannon.y;
        aarBulletShow( &aarBullets[ i ] );
        aarBulletIntervalCount = AAR_BULLET_INTERVAL;
        aarBulletCount = aarBulletCount + 1;
        return;
    }
}

bool aarBulletHit( AarMovable* pBullet )
{
    if( aarHitUfo( pBullet->x, pBullet->y ) ) return true;
    if( aarHitBlock( pBullet->x, pBullet->y ) ) return true;
    return false;
}

void aarMoveBullets()
{
    int i;
    for( i = 0; i < AAR_MAX_BULLET_COUNT; i = i + 1 )
    {
        if( aarBullets[ i ].y >= AAR_BULLET_RANGE ) continue;
        // Real byte-wraparound reliance: y decrementing past 0 must wrap to
        // 0xff (255) to correctly trigger the >= Range removal check below -
        // see this file's own header comment.
        aarBullets[ i ].y = ( aarBullets[ i ].y - 1 ) & 0xFF;
        if(
            aarBullets[ i ].y >= AAR_BULLET_RANGE ||
            ( ( aarBullets[ i ].y & AAR_COORD_MASK ) == 0 && aarBulletHit( &aarBullets[ i ] ) )
        )
          aarBulletEnd( &aarBullets[ i ] );
        else
          aarBulletShow( &aarBullets[ i ] );
    }
    if( aarBulletIntervalCount != 0 )
      aarBulletIntervalCount = aarBulletIntervalCount - 1;
}


// -----------------------------------------------------------------------------
//   Block.cpp
// -----------------------------------------------------------------------------

void aarInitBlocks()
{
    int i, sprite;
    sprite = AAR_SPRITE_FALLING_BLOCK;
    for( i = 0; i < AAR_MAX_FALLING_BLOCK_COUNT; i = i + 1 )
    {
        aarFallingBlocks[ i ].flags = 0;
        aarFallingBlocks[ i ].m.sprite = sprite;
        aarHideSprite( sprite );
        sprite = sprite + 1;
    }
    for( i = 0; i < AAR_WINDOW_WIDTH; i = i + 1 )
      aarGround[ i ] = 0;
}

void aarBlockShow( AarFallingBlock* pBlock )
{
    aarShowSprite( &pBlock->m, AAR_CHAR_BLOCK - 4 + ( pBlock->flags << 2 ) );
}

void aarBlockHide( AarFallingBlock* pBlock )
{
    pBlock->flags = 0;
    aarHideSprite( pBlock->m.sprite );
}

bool aarStartFallingBlock( int x, int y, int type )
{
    int i;
    x = ( x + AAR_COORD_RATE / 2 ) & ~AAR_COORD_MASK;
    for( i = 0; i < AAR_MAX_FALLING_BLOCK_COUNT; i = i + 1 )
    {
        if( aarFallingBlocks[ i ].flags == 0 )
        {
            aarFallingBlocks[ i ].flags = type;
            aarFallingBlocks[ i ].m.x = x;
            aarFallingBlocks[ i ].m.y = y;
            aarBlockShow( &aarFallingBlocks[ i ] );
            return true;
        }
    }
    return false;
}

void aarMakeBang( int bombX )
{
    aarStartLargeBang( ( bombX + 1 ) << AAR_COORD_SHIFT, ( AAR_WINDOW_HEIGHT - 1 ) * AAR_COORD_RATE );
    md_playTone( 1500.0, 0.15 );
    aarHitCannon( ( bombX - 1 ) << AAR_COORD_SHIFT, ( AAR_WINDOW_HEIGHT - 2 ) * AAR_COORD_RATE, 4 * AAR_COORD_RATE );
    if( bombX >= 2 )
    {
        if(
            ( aarGround[ bombX - 2 ] & AAR_GROUND_BLOCK_MASK ) == AAR_GROUND_LEFT_BLOCK ||
            ( aarGround[ bombX - 1 ] & AAR_GROUND_BLOCK_MASK ) == AAR_GROUND_RIGHT_BLOCK
        )
          aarGround[ bombX - 2 ] = aarGround[ bombX - 2 ] & ~AAR_GROUND_BLOCK_MASK;
    }
    if( bombX >= 1 )
      aarGround[ bombX - 1 ] = aarGround[ bombX - 1 ] & ~AAR_GROUND_BLOCK_MASK;
    aarGround[ bombX ] = aarGround[ bombX ] & ~AAR_GROUND_BLOCK_MASK;
    // upstream's own pGround[1] write is unconditional (no bounds guard) -
    // guarded here since an out-of-bounds write is a genuine risk on this
    // platform, unlike upstream's own flat AVR/RISC-V memory (see header).
    if( bombX + 1 < AAR_WINDOW_WIDTH )
      aarGround[ bombX + 1 ] = aarGround[ bombX + 1 ] & ~AAR_GROUND_BLOCK_MASK;
    if( bombX < AAR_WINDOW_WIDTH - 3 )
    {
        if(
            ( aarGround[ bombX + 2 ] & AAR_GROUND_BLOCK_MASK ) == AAR_GROUND_LEFT_BLOCK ||
            ( aarGround[ bombX + 3 ] & AAR_GROUND_BLOCK_MASK ) == AAR_GROUND_RIGHT_BLOCK
        )
          aarGround[ bombX + 3 ] = aarGround[ bombX + 3 ] & ~AAR_GROUND_BLOCK_MASK;
    }
    if( bombX < AAR_WINDOW_WIDTH - 2 )
      aarGround[ bombX + 2 ] = aarGround[ bombX + 2 ] & ~AAR_GROUND_BLOCK_MASK;
}

void aarMoveBlocks()
{
    int i;
    for( i = 0; i < AAR_MAX_FALLING_BLOCK_COUNT; i = i + 1 )
    {
        if( aarFallingBlocks[ i ].flags != 0 )
        {
            if( ( aarFallingBlocks[ i ].m.y & AAR_COORD_MASK ) == 0 )
            {
                int x, y;
                x = aarFallingBlocks[ i ].m.x >> AAR_COORD_SHIFT;
                y = aarFallingBlocks[ i ].m.y >> AAR_COORD_SHIFT;
                if( y >= AAR_WINDOW_HEIGHT - 2 )
                {
                    int type;
                    type = aarFallingBlocks[ i ].flags & AAR_BLOCK_TYPE_MASK;
                    if( type == AAR_TYPE_BLOCK )
                    {
                        aarGround[ x ] = ( aarGround[ x ] & AAR_GROUND_BOMB_MASK ) | AAR_GROUND_LEFT_BLOCK;
                        if( x + 1 < AAR_WINDOW_WIDTH )
                          aarGround[ x + 1 ] = ( aarGround[ x + 1 ] & AAR_GROUND_BOMB_MASK ) | AAR_GROUND_RIGHT_BLOCK;
                    }
                    else if( type == AAR_TYPE_BOMB )
                      aarMakeBang( x );
                    else if( type == AAR_TYPE_TIME_BOMB )
                    {
                        aarGround[ x ] = ( aarGround[ x ] & AAR_GROUND_BLOCK_MASK ) | AAR_GROUND_LEFT_BOMB | AAR_GROUND_TIME_MASK;
                        if( x + 1 < AAR_WINDOW_WIDTH )
                          aarGround[ x + 1 ] = ( aarGround[ x + 1 ] & AAR_GROUND_BLOCK_MASK ) | AAR_GROUND_RIGHT_BOMB | AAR_GROUND_TIME_MASK;
                    }
                    aarBlockHide( &aarFallingBlocks[ i ] );
                    continue;
                }
                else
                {
                    if( aarHitCannon( aarFallingBlocks[ i ].m.x, aarFallingBlocks[ i ].m.y, 2 * AAR_COORD_RATE ) )
                    {
                        aarBlockHide( &aarFallingBlocks[ i ] );
                        continue;
                    }
                }
            }
            aarFallingBlocks[ i ].m.y = aarFallingBlocks[ i ].m.y + 1;
            aarBlockShow( &aarFallingBlocks[ i ] );
        }
    }
}

bool aarHitBlock( int bulletX, int bulletY )
{
    int i;
    for( i = 0; i < AAR_MAX_FALLING_BLOCK_COUNT; i = i + 1 )
    {
        if( aarFallingBlocks[ i ].flags != 0 )
        {
            int x, y;
            x = aarFallingBlocks[ i ].m.x;
            y = aarFallingBlocks[ i ].m.y;
            if(
                bulletX < x + AAR_COORD_RATE * 2 && x < bulletX + AAR_COORD_RATE * 2 &&
                bulletY < y + AAR_COORD_RATE * 2 && y < bulletY + AAR_COORD_RATE * 2
            )
            {
                int type;
                type = aarFallingBlocks[ i ].flags & AAR_BLOCK_TYPE_MASK;
                if( type != AAR_TYPE_BLOCK )
                {
                    aarAddScore( aarPoints[ type - 1 ] );
                    aarBlockHide( &aarFallingBlocks[ i ] );
                    aarStartSmallBang( x + AAR_COORD_RATE, y + AAR_COORD_RATE );
                    md_playTone( 3000.0, 0.12 );
                }
                return true;
            }
        }
    }
    return false;
}

void aarUpdateBlocks()
{
    int x;
    x = 0;
    while( x < AAR_WINDOW_WIDTH )
    {
        int b;
        b = aarGround[ x ];
        if( ( b & ( AAR_GROUND_BOMB_MASK | AAR_GROUND_TIME_MASK ) ) != 0 )
        {
            b = b - AAR_GROUND_TIME;
            if( ( b & AAR_GROUND_TIME_MASK ) == 0 )
            {
                int bombX;
                bombX = x;
                if( ( b & AAR_GROUND_BOMB_MASK ) == AAR_GROUND_RIGHT_BOMB )
                {
                    bombX = bombX - 1;
                    if( bombX >= 0 && ( aarGround[ bombX ] & AAR_GROUND_BOMB_MASK ) == AAR_GROUND_LEFT_BOMB )
                      aarGround[ bombX ] = aarGround[ bombX ] & AAR_GROUND_BLOCK_MASK;
                }
                else
                {
                    if( x + 1 < AAR_WINDOW_WIDTH && ( aarGround[ x + 1 ] & AAR_GROUND_BOMB_MASK ) == AAR_GROUND_RIGHT_BOMB )
                      aarGround[ x + 1 ] = aarGround[ x + 1 ] & AAR_GROUND_BLOCK_MASK;
                }
                aarGround[ x ] = b & AAR_GROUND_BLOCK_MASK;
                aarMakeBang( bombX );
            }
            else
              aarGround[ x ] = b;
        }
        x = x + 1;
    }
}


// -----------------------------------------------------------------------------
//   Ufo.cpp
// -----------------------------------------------------------------------------

void aarUfoRecount()
{
    int pRow, colIdx, x, count;
    aarUfoCount = 0;
    aarUfoRightEnd = 0;
    aarUfoLeftSpace = AAR_UNIT_WIDTH * AAR_COLUMN_COUNT;
    for( pRow = 0; pRow < AAR_ROW_COUNT; pRow = pRow + 1 )
    {
        count = 0;
        x = 0;
        for( colIdx = 0; colIdx < AAR_COLUMN_COUNT; colIdx = colIdx + 1 )
        {
            int b;
            b = aarUfoColumns[ pRow ][ colIdx ];
            if( b != 0 )
            {
                count = count + 1;
                if( x < aarUfoLeftSpace )
                  aarUfoLeftSpace = x;
                if( x > aarUfoRightEnd )
                  aarUfoRightEnd = x;
            }
            x = x + AAR_UNIT_WIDTH;
        }
        aarUfoRowMemberCount[ pRow ] = count;
        aarUfoCount = aarUfoCount + count;
    }
    aarUfoRightEnd = aarUfoRightEnd + AAR_UNIT_WIDTH;
}

void aarInitUfos()
{
    int pRow, bitIdx, colIdx, source;
    aarUfoCount = 0;
    for( pRow = 0; pRow < AAR_ROW_COUNT; pRow = pRow + 1 )
    {
        colIdx = 0;
        source = aarStageBytes[ aarStageIndex ][ pRow ];
        for( bitIdx = 0; bitIdx < 4; bitIdx = bitIdx + 1 )
        {
            aarUfoColumns[ pRow ][ colIdx ] = source & 3;
            colIdx = colIdx + 1;
            source = source >> 2;
        }
    }
}

void aarResetUfos()
{
    aarUfoDirection = 1;
    aarUfoRowCenterX = 0;
    aarUfoRecount();
}

void aarDrawUfos()
{
    int leftCoord, pUpperCursor, pRow;

    leftCoord = ( aarUfoRowCenterX + ( AAR_WINDOW_WIDTH / 2 - AAR_UNIT_WIDTH * AAR_COLUMN_COUNT / 2 ) + aarUfoLeftSpace ) & 0xFF;
    pUpperCursor = leftCoord;

    for( pRow = 0; pRow < AAR_ROW_COUNT; pRow = pRow + 1 )
    {
        int n;
        n = aarUfoRowMemberCount[ pRow ];
        if( n != 0 )
        {
            int pUpper, x, colIdx;
            pUpper = pUpperCursor;
            x = 0;
            for( colIdx = 0; colIdx < AAR_COLUMN_COUNT; colIdx = colIdx + 1 )
            {
                int source, type;
                source = aarUfoColumns[ pRow ][ colIdx ];
                if( x > aarUfoRightEnd ) break;
                if( x >= aarUfoLeftSpace )
                {
                    type = source & AAR_UFO_TYPE_MASK;
                    if( type != 0 )
                    {
                        int pLower, c;
                        pLower = pUpper + AAR_VVRAM_WIDTH;
                        c = AAR_CHAR_UFO;
                        aarVVram[ pUpper ] = c; c = c + 1;
                        aarVVram[ pUpper + 1 ] = c; c = c + 1;
                        aarVVram[ pUpper + 2 ] = c; c = c + 1;
                        aarVVram[ pUpper + 3 ] = c;
                        pUpper = pUpper + 4;
                        aarVVram[ pLower ] = AAR_CHAR_UFO + 4; pLower = pLower + 1;
                        if( ( source & AAR_UFO_TIME_MASK ) == 0 )
                        {
                            c = AAR_CHAR_BLOCK + 2 + ( ( type - 1 ) << 2 );
                            aarVVram[ pLower ] = c; pLower = pLower + 1; c = c + 1;
                            aarVVram[ pLower ] = c; pLower = pLower + 1;
                        }
                        else
                        {
                            aarVVram[ pLower ] = AAR_CHAR_SPACE; pLower = pLower + 1;
                            aarVVram[ pLower ] = AAR_CHAR_SPACE; pLower = pLower + 1;
                        }
                        aarVVram[ pLower ] = AAR_CHAR_UFO + 7;
                    }
                    else
                      pUpper = pUpper + AAR_UNIT_WIDTH;
                }
                x = x + AAR_UNIT_WIDTH;
            }
        }
        pUpperCursor = pUpperCursor + AAR_VVRAM_WIDTH * 3;
    }
}

void aarMoveUfos()
{
    int center;

    aarUfoRowCenterX = ( aarUfoRowCenterX + aarUfoDirection ) & 0xFF;
    center = ( aarUfoRowCenterX + AAR_WINDOW_WIDTH / 2 - AAR_UNIT_WIDTH * AAR_COLUMN_COUNT / 2 ) & 0xFF;
    if( ( ( center + aarUfoLeftSpace ) & 0xFF ) == 0 )
      aarUfoDirection = 1;
    else if( ( ( center + aarUfoRightEnd ) & 0xFF ) >= AAR_WINDOW_WIDTH )
      aarUfoDirection = -1;

    {
        int pRow, y, colIdx, x;
        y = 0;
        for( pRow = 0; pRow < AAR_ROW_COUNT; pRow = pRow + 1 )
        {
            x = ( aarUfoRowCenterX + ( AAR_WINDOW_WIDTH / 2 - AAR_UNIT_WIDTH * AAR_COLUMN_COUNT / 2 ) + 1 ) & 0xFF;
            for( colIdx = 0; colIdx < AAR_COLUMN_COUNT; colIdx = colIdx + 1 )
            {
                int b, type;
                b = aarUfoColumns[ pRow ][ colIdx ];
                type = b & AAR_UFO_TYPE_MASK;
                if( type != 0 )
                {
                    if( ( b & AAR_UFO_TIME_MASK ) != 0 )
                      b = b - AAR_UFO_TIME;
                    else if( ( aarUfoBombClock & 7 ) == 0 )
                    {
                        int threshold;
                        threshold = aarCurrentStage + 2;
                        if( aarRnd() < threshold && aarStartFallingBlock( x, y, type ) )
                          b = b | AAR_UFO_TIME_MASK;
                    }
                    aarUfoBombClock = aarUfoBombClock + 1;
                    aarUfoColumns[ pRow ][ colIdx ] = b;
                }
                // Real bug found via direct user report ("sometimes bombs
                // don't drop, bullets sometimes pass through UFOs"):
                // upstream's own `x` is a genuine byte here (Ufo.cpp,
                // `byte x;`), which truncates/wraps back into 0-255 after
                // EVERY `+=` step, not just at its initial assignment.
                // `aarUfoRowCenterX` legitimately spans the full 0-255
                // range as the UFO formation bounces left/right (its own
                // bounce-direction check is itself wraparound-reliant,
                // `((center+LeftSpace)&0xFF)==0`), so a row's starting x
                // can legitimately sit anywhere near the top of that
                // range - and this port's own plain (non-wrapping) `int x`
                // only had `&0xFF` applied to its STARTING value, not to
                // each of the 4 per-column increments inside this loop,
                // letting x silently exceed 255 for the later columns of
                // an affected row instead of wrapping back to a small
                // value the way upstream's real byte does. Since a bomb
                // is started (`aarStartFallingBlock`) using this same,
                // now-wrong x, and `aarHitUfo()` below has the identical
                // gap in its own per-column loop, this desyncs both the
                // bomb-drop position and the bullet hit-test box from
                // where the UFO is actually rendered (aarDrawUfos() is
                // unaffected - it walks a VVram pointer directly rather
                // than recomputing x from aarUfoRowCenterX, so its own
                // narrow 0-16-range local x never approaches the
                // wraparound boundary in the first place). Fixed by
                // re-masking after every increment, matching upstream's
                // real per-step truncation exactly.
                x = ( x + AAR_UNIT_WIDTH * AAR_COORD_RATE ) & 0xFF;
            }
            y = y + AAR_COORD_RATE * 3;
        }
    }
}

bool aarHitUfo( int bulletX, int bulletY )
{
    int pRow, y;
    y = 0;
    for( pRow = 0; pRow < AAR_ROW_COUNT; pRow = pRow + 1 )
    {
        if( bulletY < y + AAR_COORD_RATE * 2 && y < bulletY + AAR_COORD_RATE )
        {
            int x, colIdx;
            x = ( aarUfoRowCenterX + ( AAR_WINDOW_WIDTH / 2 - AAR_UNIT_WIDTH * AAR_COLUMN_COUNT / 2 ) ) & 0xFF;
            for( colIdx = 0; colIdx < AAR_COLUMN_COUNT; colIdx = colIdx + 1 )
            {
                int b;
                b = aarUfoColumns[ pRow ][ colIdx ];
                if( b != 0 )
                {
                    if(
                        bulletX < x + ( AAR_COORD_RATE * AAR_UNIT_WIDTH ) &&
                        x + AAR_COORD_RATE / 2 < bulletX + AAR_COORD_RATE * 2
                    )
                    {
                        int type;
                        type = b & AAR_UFO_TYPE_MASK;
                        if( ( b & AAR_UFO_TIME_MASK ) == 0 )
                          aarStartFallingBlock( x + AAR_COORD_RATE, y, type );
                        aarAddScore( aarPoints[ type - 1 ] );
                        aarStartSmallBang( x + AAR_COORD_RATE * 2, y + AAR_COORD_RATE * 2 );
                        md_playTone( 3000.0, 0.12 );
                        aarUfoColumns[ pRow ][ colIdx ] = 0;
                        aarUfoRecount();
                        return true;
                    }
                }
                // Same wraparound bug/fix as aarMoveUfos() above - see its
                // own comment for the full derivation. Without this,
                // a bullet's hit-test box (bulletX, already correctly
                // 0-255-ranged) gets compared against an unwrapped x that
                // can exceed 255 for the later columns of a row whose
                // starting x sits near the top of the byte range, making
                // the hit-test silently fail for UFOs the player's own
                // bullet visually reaches.
                x = ( x + AAR_UNIT_WIDTH * AAR_COORD_RATE ) & 0xFF;
            }
        }
        y = y + AAR_COORD_RATE * 3;
    }
    return false;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

void aarInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=9 (the
    // game never actually stops the player from continuing past wave 10) -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching CRACKY's own identical structure/precedent.
    int i, j;
    i = 0;
    j = 0;
    while( i < aarCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= AAR_STAGE_COUNT )
          j = 0;
    }
    aarStageIndex = j;
    aarInitUfos();
    aarResetUfos();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void aarDrawBackground()
{
    int i;
    int groundBlockMask, groundBombMask;
    groundBlockMask = 0x03;
    groundBombMask = 0x0c;

    for( i = 0; i < AAR_VVRAM_WIDTH * ( AAR_VVRAM_HEIGHT - 2 ); i = i + 1 )
      aarVVram[ i ] = AAR_CHAR_SPACE;

    for( i = 0; i < AAR_VVRAM_WIDTH; i = i + 1 )
    {
        int b, c;
        bool useIt;
        b = aarGround[ i ];
        useIt = false;
        c = b & groundBombMask;
        if( c != 0 )
        {
            c = ( c >> 2 ) - 1 + AAR_CHAR_BLOCK_C;
            useIt = true;
        }
        else
        {
            c = b & groundBlockMask;
            if( c != 0 )
            {
                c = c + AAR_CHAR_BLOCK_A - 1;
                useIt = true;
            }
        }
        if( useIt )
        {
            aarVVram[ ( AAR_VVRAM_HEIGHT - 2 ) * AAR_VVRAM_WIDTH + i ] = c;
            aarVVram[ ( AAR_VVRAM_HEIGHT - 1 ) * AAR_VVRAM_WIDTH + i ] = c + 2;
        }
        else
        {
            aarVVram[ ( AAR_VVRAM_HEIGHT - 2 ) * AAR_VVRAM_WIDTH + i ] = AAR_CHAR_SPACE;
            aarVVram[ ( AAR_VVRAM_HEIGHT - 1 ) * AAR_VVRAM_WIDTH + i ] = AAR_CHAR_SPACE;
        }
    }
}

void aarDrawAll()
{
    aarDrawBackground();
    aarDrawUfos();
    aarDrawSprites();
}

// Shared nibble-pack math for a 4-column-wide slice of 2 stacked CharPattern
// glyphs (upperGlyph over lowerGlyph) - used both by the main map compose
// function and by the "2x2 icon printed inline with status text" case
// (upstream's own Put2C(), used only for the lives indicator - see header).
int aarGlyphSubByte( int upperGlyph, int lowerGlyph, int sub )
{
    int upperByte, lowerByte;
    if( sub == 0 || sub == 1 )
    {
        upperByte = aarCharPattern[ upperGlyph * 2 + 0 ];
        lowerByte = aarCharPattern[ lowerGlyph * 2 + 0 ];
    }
    else
    {
        upperByte = aarCharPattern[ upperGlyph * 2 + 1 ];
        lowerByte = aarCharPattern[ lowerGlyph * 2 + 1 ];
    }
    if( sub == 0 || sub == 2 )
      return ( upperByte & 0x0f ) | ( lowerByte << 4 );
    return ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
}

// rawCol/rawPage are in upstream's own (unmirrored) GDDRAM coordinate space -
// no hardware remap is applied at all, matching CRACKY's own hard-won
// conclusion (see this file's own header comment).
//
// **Rewritten to use ABSOLUTE upstream columns (0-31) for the text branch,
// after a real user-supplied hardware photo of the sibling CRACKY port's
// own title screen proved the previous narrow "subtract the map width"
// model was flatly wrong for this whole family.** Upstream's real Vram
// address space is one shared 32-char-cell-wide canvas per page - the
// status labels (SCORE/STAGE/remain-icons) really are confined to columns
// 24-31 (upstream's own `LeftX=24`), but the *title screen's* own text
// (the logo, "MINI", "START"/"CONTINUE", the "INUFUTO 2026" credit) lives
// at columns 8-23, well clear of them, using the exact same PrintC()/
// PrintS() mechanism at different column arguments - not a separate,
// narrower grid at all. See aarStatusChar's/aarFullWidthText's own header
// comments for the full story, and gameCracky.c's own matching fix.
// **OR-combines the map/VVram layer with the status-text layer instead of
// choosing one exclusively** - mirrors CRACKY's own identical fix (see that
// file's own crkComposeRawByte() header comment). The earlier if/else shape
// here computed the map byte only while aarFullWidthText was false, which
// was harmless while the title screen only ever wrote plain text into
// aarStatusChar (nothing meaningful ever sat in aarVVram during the title
// state) - but now that aarBeginTitle() draws the real upstream logo bitmap
// directly into aarVVram (see that function's own comment), that byte must
// actually reach the screen during the title state too. Safe to OR
// unconditionally: the logo bitmap occupies only real hardware pages 1-2
// (VVram rows 2-5) at columns 2-21, while every status-text element placed
// during the title screen (SCORE/STAGE labels+digits, MINI, START/CONTINUE,
// the credit line, remain-count icons) sits at columns 24-31 or, for MINI,
// at column 18 on page 3 - never page 1 or 2 at the logo's own columns - so
// the two layers can never actually blend two distinct pieces of real
// content together, only coexist.
int aarComposeRawByte( int rawCol, int rawPage )
{
    int mapByte;

    mapByte = 0;
    if( rawCol < AAR_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = aarVVram[ ( rawPage * 2 ) * AAR_VVRAM_WIDTH + mapX ];
        lower = aarVVram[ ( rawPage * 2 + 1 ) * AAR_VVRAM_WIDTH + mapX ];
        mapByte = aarGlyphSubByte( upper, lower, sub );
    }

    if( !aarFullWidthText && rawCol < AAR_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // aarStatusChar's own full-width indexing directly - no more "subtract
    // the map width" local-offset math needed, since rawCol/4 already lands
    // on the correct real column either way (whether this is the
    // aarFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >=96).
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol >= 32 ) return mapByte;

        // Put2C()-style "remain count" lives icon - a 2x2 glyph block, 8
        // real columns wide, starting at absolute char-cell 25 (LeftX+1)
        // on page 7 - see this file's own header comment.
        if( rawPage == 7 && rawCol >= 100 && rawCol < 100 + aarRemainIconCount * 8 )
        {
            int rel, cellIdx, iconSub;
            rel = ( rawCol - 100 ) % 8;
            cellIdx = rel / 4;
            iconSub = rel % 4;
            return mapByte | aarGlyphSubByte( AAR_CHAR_REMAIN + cellIdx, AAR_CHAR_REMAIN + 2 + cellIdx, iconSub );
        }

        c = aarStatusChar[ rawPage ][ charCol ];
        return mapByte | aarAsciiPattern[ c * 4 + sub ];
    }
}

void aarRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if(
                aarOverlayActive && page == aarOverlayPage &&
                col >= aarOverlayCol * 4 && col < aarOverlayCol * 4 + aarOverlayLen * 4
            )
            {
                int i, sub;
                i = ( col - aarOverlayCol * 4 ) / 4;
                sub = ( col - aarOverlayCol * 4 ) % 4;
                value = aarAsciiPattern[ aarAsciiIndex( aarOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = aarComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after the same real user-supplied hardware photo that fixed
// CRACKY's own title screen proved this function's previous version was
// simply wrong.** The earlier version believed upstream's own title-screen
// text collided with the SCORE/STAGE status labels and had to be trimmed/
// relocated/dropped to fit - "MINI" was dropped entirely, "CONTINUE" was
// truncated to "CONTINU", and the credit line lost its "2026". Re-reading
// upstream's real `Status.cpp` (`Title()`) line by line shows this
// diagnosis was backwards: none of that text ever collides with anything
// upstream, because upstream's own Vram address space is a genuinely wide
// 32-char-cell-per-page canvas (see aarStatusChar's own header comment) -
// the status labels occupy only columns 24-31 (upstream's own `LeftX=24`),
// and every piece of title-screen text sits at columns 8-23, well clear of
// them. The ROOT problem was this port's own `aarStatusChar` being modeled
// as an 8-column-wide grid in the first place - now fixed there, this
// function places everything at upstream's real, literal columns, with
// `aarFullWidthText=true` so aarComposeRawByte() renders the full canvas
// instead of just the narrow status zone.
void aarBeginTitle()
{
    int i;
    for( i = 0; i < AAR_VVRAM_WIDTH * AAR_VVRAM_HEIGHT; i = i + 1 )
      aarVVram[ i ] = AAR_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          aarStatusChar[ i ][ j ] = 0;
    }
    aarOverlayActive = false;
    aarFullWidthText = true;
    aarHideAllSprites();
    aarPrintStatus();

    // **Restored, after the sibling CRACKY port was fixed the same way (see
    // that file's own header comment for the full story)**: this is
    // upstream's own real title-screen logo bitmap, drawn directly into
    // aarVVram from aarTitleBytes[] at its own real position (VVram rows
    // 2-5, starting at VVram column 2 - matching upstream's `Status.cpp`
    // `Title()`'s own `VVram + VVramWidth*2 + 2` starting offset exactly,
    // real hardware pages 1-2). The earlier version of this function
    // replaced this with plain "ANTIAIR" text on page 2, reasoning it was
    // "purely decorative" (matching what was, at the time, also CRACKY's own
    // since-corrected simplification for the same reason) - wrong: it's the
    // actual title wordmark, meant to be the single biggest, most prominent
    // element on the whole screen, not a throwaway detail.
    // `aarComposeRawByte()` was updated to OR-combine this aarVVram content
    // with aarStatusChar's own text layer rather than choosing one
    // exclusively, since the two occupy disjoint (page,column) footprints by
    // construction (see that function's own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                aarVVram[ ( 2 + row ) * AAR_VVRAM_WIDTH + ( 2 + ch * 4 + col ) ] = aarTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 18, START/CONTINUE at col 9 with
    // the cursor at col 8, the credit line - the full "INUFUTO 2026", both
    // digits and the space are representable in this game's own 26-glyph
    // font - at col 12) - all genuinely clear of both the status labels' own
    // columns 24-31 and the logo's own columns 2-21/pages 1-2, so nothing
    // here needs trimming, relocating, or dropping.
    {
        char sMini[4] = { 'M', 'I', 'N', 'I' };
        char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
        char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
        char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
        aarPrintS( 3, 18, sMini, 4 );
        aarPrintS( 7, 12, sCredit, 12 );
        aarPrintS( 5, 9, sStart, 5 );
        aarPrintS( 6, 9, sContinue, 8 );
    }

    aarSelection = 0;
    aarSelectionChanged = true;
    aarPrevLeft = 0; aarPrevRight = 0; aarPrevUp = 0; aarPrevDown = 0; aarPrevFire = 0;
    aarState = AAR_STATE_TITLE;
}

// Matches InitPlaying()/"try_:" exactly - reused both for the very first
// game start and every life retry. Does NOT reload the UFO wave itself
// (aarResetUfos(), not aarInitUfos()) - any UFOs already destroyed in the
// current wave stay destroyed across a life loss, a deliberate upstream
// design choice - see this file's own header comment.
void aarBeginTry()
{
    int i;
    aarRndIndex = 0;
    for( i = 0; i < AAR_VVRAM_WIDTH * AAR_VVRAM_HEIGHT; i = i + 1 )
      aarVVram[ i ] = AAR_CHAR_SPACE;
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          aarStatusChar[ i ][ j ] = 0;
    }
    aarOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in aarUpdateTitle()) - matches aarOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches aarBeginTry() without going through that transition first.
    aarFullWidthText = false;
    aarPrintStatus();
    aarInitBullets();
    aarInitCannon();
    aarInitBlocks();
    aarInitBangs();
    aarResetUfos();
    aarDrawAll();
    aarStartSeq( 1, AAR_MELODY_START );
    aarLinger = 0;
    // NOTE: aarClock is deliberately NOT reset here - upstream's own
    // `Clock` (Main.cpp) is only zeroed once, right after Title() returns
    // (i.e. only for a genuinely fresh match), and keeps its running value
    // across every `goto try_` life retry within that same match. See the
    // reset in aarUpdateTitle()'s justFire branch below.
    aarTickCounter = 0;
    aarState = AAR_STATE_START_JINGLE;
    aarRender();
}

void aarUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !aarPrevLeft ) || ( right && !aarPrevRight ) ||
                ( up && !aarPrevUp ) || ( down && !aarPrevDown ) );
    justFire = ( fire && !aarPrevFire );
    aarPrevLeft = left; aarPrevRight = right; aarPrevUp = up; aarPrevDown = down; aarPrevFire = fire;

    if( aarSelectionChanged )
    {
        aarSelectionChanged = false;
        // ArrowX=8 upstream - the cursor column sits one cell left of
        // START/CONTINUE's own real col 9 (see aarBeginTitle()'s own
        // comment), not col 0.
        if( aarSelection == 0 )
          aarPrintC( 5, 8, '>' );
        else
          aarPrintC( 5, 8, ' ' );
        if( aarSelection == 1 )
          aarPrintC( 6, 8, '>' );
        else
          aarPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        aarFullWidthText = false;
        // Matches Main.cpp's own `Clock = 0;` right after Title() returns -
        // a fresh match always starts Clock at 0, but a later life retry
        // (aarBeginTry() called again from aarUpdatePlaying()) deliberately
        // leaves it running, matching upstream's own goto try_ (no Clock
        // reset there) - see aarBeginTry()'s own comment.
        aarClock = 0;
        aarPendingContinue = ( aarSelection == 1 );
        aarScore = 0;
        if( !aarPendingContinue )
          aarCurrentStage = 0;
        aarRemainCount = 3;
        aarInitStage();
        aarBeginTry();
        return;
    }
    if( justDir )
    {
        aarSelection = aarSelection ^ 1;
        aarSelectionChanged = true;
    }
    aarRender();
}

void aarUpdateStartJingle()
{
    if( !aarSeqPlaying( 1 ) )
    {
        aarStartBgm();
        aarState = AAR_STATE_PLAYING;
    }
    aarRender();
}

void aarUpdateGameOverJingle()
{
    if( !aarSeqPlaying( 1 ) )
      aarBeginTitle();
    else
      aarRender();
}

// Main.cpp's own real per-tick loop body, tick-gated to AAR_TICK_DIVISOR
// (matching the real WaitTimer(3) call, 60/3=20Hz) - see this file's own
// header comment for the full Clock&1/Clock&7 sub-throttle + shared-linger
// mapping.
void aarUpdatePlaying()
{
    aarTickCounter = aarTickCounter + 1;
    if( aarTickCounter < AAR_TICK_DIVISOR )
    {
        aarRender();
        return;
    }
    aarTickCounter = 0;

    if( ( aarClock & 0x01 ) == 0 )
    {
        aarMoveFighter();
        aarMoveBlocks();
        aarUpdateBangs();
    }
    aarMoveBullets();
    if( ( aarClock & 0x07 ) == 0 )
      aarMoveUfos();
    aarUpdateBlocks();
    aarDrawAll();
    aarClock = aarClock + 1;

    if( aarUfoCount == 0 )
    {
        aarLinger = aarLinger + 1;
        if( aarLinger > 31 )   // 250 * AAR_COORD_RATE / 8 == 31 (int division)
        {
            aarCurrentStage = aarCurrentStage + 1;
            aarPrintStage();
            aarInitStage();
            aarLinger = 0;
        }
    }
    if( !aarCannonLive )
    {
        aarLinger = aarLinger + 1;
        if( aarLinger > 25 )   // 200 * AAR_COORD_RATE / 8 == 25 (int division)
        {
            aarStopBgm();
            aarRemainCount = aarRemainCount - 1;
            if( aarRemainCount != 0 )
            {
                aarBeginTry();
                return;
            }
            aarPrintGameOver();
            aarStartSeq( 1, AAR_MELODY_GAMEOVER );
            aarState = AAR_STATE_GAMEOVER_JINGLE;
            aarRender();
            return;
        }
    }

    aarRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameAntiair_init()
{
    int i;

    aarScore = 0;
    aarCurrentStage = 0;
    aarRemainCount = 3;
    aarRndIndex = 0;
    aarUfoBombClock = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        aarSeqActive[ i ] = 0;
        aarSeqMelody[ i ] = AAR_MELODY_NONE;
    }
    aarOverlayActive = false;
    aarTickCounter = 0;

    aarBeginTitle();
}

void gameAntiair_update()
{
    aarAdvanceSound();

    if( aarState == AAR_STATE_TITLE )
      aarUpdateTitle();
    else if( aarState == AAR_STATE_START_JINGLE )
      aarUpdateStartJingle();
    else if( aarState == AAR_STATE_PLAYING )
      aarUpdatePlaying();
    else if( aarState == AAR_STATE_GAMEOVER_JINGLE )
      aarUpdateGameOverJingle();
}
