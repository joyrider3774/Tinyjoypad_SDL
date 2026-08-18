// =============================================================================
// ASCEND mini (UIAPduino+SSD1306 edition, Cate engine, license "None specified"
// - no LICENSE file in the upstream repo) - a vertically-scrolling climbing
// platformer: climb ladders/floors up a tall multi-floor tower, dodging
// falling "Fire" hazards (some carrying a 1-up or a temporary invincibility
// "Power" pickup) and patrolling ground Monsters, reaching the top floor
// (world Y < 2) to clear the stage. Jumping (Fire button) pushes a nearby
// Block up one row for ~2 real ticks, which can wall off a Monster's own
// path (CanMove() treats a raised block as a wall) - the "push blocks up to
// trap monsters" mechanic from the upstream readme. 10 hand-authored stages
// (heights 4-10 floors), 3 lives, real persistent hi-score tracked in-
// session (upstream has no EEPROM at all - a CH32V003 RISC-V microcontroller,
// not AVR, so this project's own `eeprom_*` shim/avrCompat widening aren't
// relevant here, matching this project's own sibling Cate-engine ports).
//
// Same Cate-engine lineage as this project's own already-shipped Cracky
// port (real SSD1306 display streamed one raw byte at a time, `SendOledData`,
// no framebuffer - the exact model `md_drawColumn()` already handles; a
// real explicit 60Hz SysTick-based frame limiter, `Timer.cpp`'s own
// `kTimerHz=60`/`WaitTimer(t)`, so no Tiny-Arkanoid-style speed-mismatch
// risk despite the unrelated RISC-V host). Only 4 directions + 1 action
// button (`ScanKeys.h`'s own `Keys_Button0`) - a strict subset of what
// `tinyJoypadShim.h` already exposes, so no new shim primitive was needed
// (isFire2Pressed() simply goes unused here, matching Cracky's own port).
//
// **No hardware display-orientation transform is used, matching Cracky's
// own hard-won, directly-confirmed lesson** - `InitOled()` sends the exact
// same `OledCmd::RightToLeft`(SegRemap 0xA1)/`OledCmd::BottomToTop`
// (ComScanDec 0xC8) pair Cracky's own `InitOled()` does, and Cracky's own
// header comment already documents (after a real live-tested revert) that
// these settings exist to compensate for a physical panel-mounting quirk on
// specific real breakout modules, with nothing to compensate for in a
// software recreation. `ascComposeMapByte(col,page)`/`ascComposeStatusByte`
// are drawn directly at their own (col,page) - no column mirroring, no page
// reordering, no bit-reversal.
//
// **A genuinely new architectural piece versus Cracky: a scrolling window.**
// Unlike Cracky's fixed, non-scrolling 24x16 VVram, Ascend's own stage can
// be up to 10 floors (40 world-rows) tall while only 16 VVram rows are ever
// visible at once - `topY`/`topRow`/`yMod`/`topYRange` track the window's
// current vertical scroll offset (world-Y of the window's own top edge),
// updated by `ascScroll()`/`ascDrawBackground()` whenever the player's own Y
// changes. Upstream's real `byte topY`/`byte topRow` are BOTH capable of
// going genuinely negative near the very top of a stage (confirmed by
// tracing the real math: `newTopY = y - (WindowHeight/2) - FloorHeight`
// with `WindowHeight=16`/`FloorHeight=4` gives 12, and the win condition is
// reachable at world-Y as low as 0-1) - upstream relies on AVR `byte`
// wraparound for this ("if newTopY would go negative, wrap to a huge
// positive value >= topYRange, so the scroll-ratchet clamp correctly
// rejects it"), the exact byte-wraparound-as-a-bounds-trick pattern this
// whole project's own history has repeatedly had to re-derive as explicit
// signed-int logic instead (see CLAUDE.md's own documented bug family).
// Ported with `ascTopY`/`ascTopRow` as genuine signed ints allowed to go
// negative directly - no wraparound needed at all, since a real negative
// comparison against `topYRange` (itself always a small positive int)
// already produces the exact same accept/reject outcome the wraparound
// trick was faking. `ascGetCellType()`'s own `row = (y>>2)-1` can likewise
// go negative (reachable at low world-Y) - explicitly guarded with
// `row<0 -> return 0` (matching what the real AVR byte-wrap-then-
// `row>=height` check already produced) rather than trusting a wrap.
// `ascMapToBackground()`'s own `row` (derived from `ascTopRow`) is guarded
// the same way. `DrawSprites()`'s own `y = pSprite->y - topY` (again a real
// byte-wraparound-as-a-visibility-reject trick upstream, when a sprite sits
// above the current scroll window) is ported as a plain signed subtraction
// with an explicit `y>=0 && y<VVramHeight` bounds check instead of relying
// on wraparound to push an out-of-window sprite's `y` up past `VVramHeight`.
//
// **Two-level tile compositing, ported as a direct structural mirror rather
// than re-derived into a closed form** (the same "faithfully copy an
// intricate stateful algorithm's own shape" reasoning already used for
// Frogger's/Cracky's own compositing in this project): `MapToBackGround()`
// walks the decompressed `StageMap` (2-bit cell types, RLE-decompressed
// once per stage entry by `ascDecompressStageMap()`, mirroring `InitStage()`'s
// own real accumulator-based unpack) and, for each stage cell, combines
// *this* floor's cell type with the *next* floor's cell type (the shared
// floor-boundary tile) to pick one of a small number of fixed 4-row glyph
// patterns - re-derived here with fixed 0..3 row offsets per branch (every
// upstream branch, once traced by hand, always writes exactly
// `FloorHeight`(4) rows total regardless of which sub-branch fires - a
// genuine, confirmed invariant, not an assumption), which is considerably
// simpler than porting the original's own pointer-walking/y-position-
// bookkeeping version literally. `VVramToVram()`'s own nibble-interleaved
// 2-VVram-row-per-page packing (`SendUL()`) is the same math already
// proven in Cracky's own `crkComposeRawByte()` - reproduced here as a
// shared `ascComposeGlyphByte(topGlyph,bottomGlyph,sub)` helper, reused for
// both the scrolling map itself and the lives-icon overlay below (see next
// paragraph) rather than duplicated. Upstream's own `Backup[]` dirty-
// tracking (skip re-sending unchanged real I2C bytes) is dropped, matching
// this whole project's standing "always redraw the full frame" precedent
// (Cracky's own header comment lists half a dozen other ports that needed
// this same fix once discovered the hard way).
//
// **The status-bar "remaining lives" display needed a genuinely new
// mechanism beyond Cracky's own ascii-only status grid.** Upstream's
// `PrintStatus()` draws SCORE/STAGE/TIME labels and digits via the plain
// `AsciiPattern`-based `PrintC()`/`PrintS()` (a full-page-height, CharWidth
// -wide glyph per call - ported here as `ascPrintC`/`ascPrintS`, writing
// into an 8-page x 8-char-cell `ascStatusChar[][]` grid covering raw
// columns 96-127, the exact same "status area" convention already
// established by Cracky), but the lives count (when > 2) draws a real
// `Put2C(Char_Remain)` 2x2-icon-from-4-consecutive-glyph-codes composite -
// the exact same `CharPattern`/nibble-interleave format the *map* uses, not
// the simpler `AsciiPattern` format text uses elsewhere in the status bar.
// Traced `Put2C()`'s own byte-emission loop by hand to confirm it's really
// just this project's already-proven "2x2 icon from 4 consecutive glyph
// indices" sprite convention (`c`,`c+1` on top, `c+2`,`c+3` on the bottom),
// squeezed into one page (8px) x 8 raw columns via the identical
// `ascComposeGlyphByte()` nibble math already used for the map - so a
// dedicated `ascComposeLivesByte(relCol)`, gated to page 7 / raw columns
// 96-111 only, computes the icon(s) (`RemainCount-1` of them, up to 2) or,
// for `RemainCount>3`, one icon + a plain ascii space + an ascii digit
// (matching `PrintStatus()`'s own exact branching) directly from
// `ascComputeLivesDisplay()`'s precomputed `ascLivesMode`/`ascLivesIconCount`/
// `ascLivesDigit`, entirely bypassing the ascii-only status grid for that
// one sub-region rather than trying to force an icon into a text-only
// representation.
//
// **A second genuinely new mechanism versus Cracky: multiple simultaneous
// overlay slots, not just one.** Cracky's own single-slot
// `crkOverlayActive`/`crkOverlayText` overlay (for text drawn directly over
// the map area, matching upstream's own real-VRAM-persistence trick for
// `PrintTimeUp()`/`PrintGameOver()`) works because Cracky never needs two
// such off-status-grid texts on screen at once. Ascend's own title screen
// needs *five* simultaneously: the "MINI" sub-title (page3, raw col76,
// inside the map area under the big "ASCEND" logo), the "INUFUTO 2026"
// credit line (page7, raw col48), the static "START"/"CONTINUE" labels
// (page5/6, raw col36), and the blinking '>' selection cursor (page5/6, raw
// col32 - toggled between '>' and ' ' as two of the six slots, not a
// separate mechanism, so a selection change is just two `ascBeginOverlay()`
// calls). Generalized Cracky's own single-slot design into a fixed
// `ASC_MAX_OVERLAYS`(6) array, checked in sequence inside `ascRender()`'s
// per-pixel compose step - a small, constant per-pixel cost (6 bounds
// checks) rather than a real correctness risk, and the GAME OVER/TIME UP
// cues (which, like Cracky's own single-slot original, only ever need one
// slot each, never simultaneously with the title screen's own five) simply
// reuse slot 0. A minor, deliberate deviation from upstream's own literal
// VRAM-persistence behavior: on the rare path where TIME UP was the actual
// death cause *and* it was also the player's last life, upstream would
// visually show the two texts genuinely overlapping (`PrintTimeUp()`'s and
// `PrintGameOver()`'s real VRAM writes land on different, overlapping raw
// columns, with no clear-screen between them) - ported instead as `slot 0`
// being cleanly *replaced* (GAME OVER fully supersedes TIME UP, no visual
// garbling), matching this whole project's standing precedent of not
// reproducing a pure VRAM-persistence side effect that has no gameplay
// significance of its own.
//
// **Sound**: same real 3-tone-channel software mixer/tracker as Cracky
// (`Sound.cpp`, byte-for-byte identical `Tempo`(160)/`NoteLength`/`Scale`
// enums and `Frequencies[]` table - confirmed via direct comparison, not
// assumed from the shared engine alone) - every call routed to
// `md_playTone(freqHz, durationSeconds)` instead, the same reasoning as
// Cracky's own header comment (this shim's single-call AVR-buzzer-style
// `Sound()` wrapper is far thinner than upstream's real multi-voice
// tracker, and `md_playTone()` is itself already genuinely multi-voice
// project-wide, so 3 independent logical sequencer slots never actually
// fight over one shared channel). Each melody is ported as upstream's own
// literal `[duration,note]` byte-pair data (byte-diff-extracted via a
// small Python script parsing the real source directly, not hand-copied),
// resolved "by id" through `ascMelodyLength()`/`ascMelodyValue()` - the
// same "resolve by id instead of storing a real pointer" pattern already
// established project-wide (e.g. Tiny Dungeon's own bitmap-array
// resolver, Cracky's own melody resolver). `Sound_Item()` (the pickup
// jingle - genuinely new versus Cracky, no equivalent cue there) is the
// *only* upstream `Sound_*` call that isn't a blocking `WaitMelody()` -
// it's a real fire-and-forget `StartMelody(0,...)`, so `ascHitMan()` just
// starts it on channel 0 with no accompanying wait-state, unlike every
// other cue in this file which pairs a `ascStartSeq()` call with an
// `ascWaitFrames = ascNoteFrames(...)` state-machine pause to reproduce
// upstream's real blocking wait. Confirmed the two melodies shared between
// `Sound_Start()`/`StartBGM()`'s own channel-1 tune and Cracky's own
// identical-looking start jingle really are byte-for-byte the same 27-
// value sequence (both games apparently reuse this one composed intro) -
// re-extracted independently from Ascend's own real source rather than
// assumed, since `Sound_Clear()`/`Sound_GameOver()`/`StartBGM()`'s own
// channel-2 bass line all turned out to have real, if structurally
// similar, *different* note data than Cracky's own versions once actually
// compared value-by-value.
//
// **Movable coordinates are already expressed in VVram-cell-grid units
// directly** (`CoordShift=0`/`CoordRate=1` make upstream's own generic
// sub-cell-precision parameterization degenerate here, exactly like
// Cracky - confirmed by tracing `LocateMovable`-equivalent math, though
// Ascend has no such helper of its own; `ToX()`/`ToY()` already produce
// VVram-grid coordinates directly from a packed stage-data byte) - so
// sprites compositing directly into `ascVVram` (`ascDrawSpritesIntoVVram`,
// offset by the scroll window's own `ascTopY`) needed no extra scaling.
// Every genuinely degenerate-but-still-present upstream formula
// (`CoordRate`/`CoordMask`, `MaxTimeDenom`'s own `50/(8/CoordRate)`,
// `FireInterval`'s own `CoordRate*12`) is kept as the real expression
// rather than resolved to its current literal value, matching Cracky's
// own established "#defines should stay exactly as upstream wrote them"
// preference.
//
// **A genuinely intentional bit-value collision, preserved rather than
// "fixed"**: `Man_Jump`(0x40) and `Item_1Up`(0x40) share the exact same
// numeric value in upstream (two independently-purposed status flags on
// two different actor "kinds" that happen to collide) - `CanMove()`'s own
// `if((pActor->status & Man_Jump)!=0) --y;` check, when called with a Fire
// actor whose status has `Item_1Up` set instead, spuriously matches and
// offsets that fire's own collision-check row by one, purely from this bit
// coincidence. Confirmed by tracing every real call site that this is
// upstream's own actual, reachable behavior (not a porting slip) - ported
// with the literal same `0x40` constant reused for both flags, exactly as
// upstream does, rather than defining two textually-distinct-but-still-
// equal constants that might invite "fixing" the collision later.
//
// **Several intra-function `goto`s used purely as structured-control-flow
// shortcuts (jumping into a shared tail of an if/else-if chain from an
// earlier branch) were rewritten as duplicated inline blocks instead of
// literal gotos** - `TestMoveY()`'s own `up:`/`down:` labels (each reached
// from two different branches), `FallMan()`'s own `fall:`/`stop:` labels,
// and `HitMan()`'s own `get:` label (reached from both the 1-up and the
// Power item branches) - matching this project's own established
// preference for plain duplicated `if`/`else` blocks over intra-function
// goto, and `MoveMan()`'s own single `goto moved;` (skipping the Y-axis
// test entirely once the X-axis test succeeds against *fresh* input, but
// NOT when only the X-axis retry-with-stale-input succeeds - a real,
// subtle distinction traced carefully from the exact goto placement) is
// reproduced with a plain `bool skipRest` flag instead.
//
// A `JumpHeights[]` table (Man.cpp) and `Direction_Left`/`Direction_Right`
// constants (Actor.h) and a `DrawBlocks()` declaration (Block.h) are all
// confirmed dead by a project-wide grep (declared, never referenced
// anywhere) - not ported, matching this project's own established
// practice of dropping confirmed-dead code rather than translating it
// unused. `PrintPerfect()` (Status.cpp) is likewise declared and defined
// but never called from anywhere - also dropped.
// =============================================================================

// =============================================================================
// A dedicated, line-by-line re-verification pass against every real upstream
// source file (this game was originally ported in a large parallel batch and
// had only ever been test-compiled, never played) - found and fixed three
// real bugs, two of them genuinely serious.
//
// **Bug 1 - the scroll-window ratchet, exactly the piece this port's own
// header comment above had flagged as the highest-risk area, and exactly
// the piece it had gotten wrong.** The header's own claim ("a real negative
// comparison against topYRange...already produces the exact same accept/
// reject outcome the wraparound trick was faking") was checked by directly
// simulating upstream's real byte-wraparound arithmetic in Python across a
// full climb, and it does NOT hold: upstream's `if (newTopY != topY &&
// newTopY < topYRange)` (Man.cpp's `Scroll()`), with `newTopY` a real
// wrapping `byte`, REJECTS the update the instant the true (signed) result
// would go negative (which happens for every stage, once Man's world-Y
// drops below 12, well before the win condition at Y<2) - freezing `topY`
// at its last valid value (always exactly 0) for the remainder of the
// climb, because the window has by then already scrolled far enough to
// show the whole top of the tower. The ported signed-int version had NO
// such freeze - `newTopY` just kept counting down through genuinely
// negative values (as low as -11 on the tallest stage), and a negative
// `ascTopY` corrupts `ascMapToBackground()`'s own row/yPos bookkeeping
// (`ascYMod` grows away from 0 instead of freezing), leaving most of the
// visible window unrefreshed right as the player closes in on winning -
// exactly the kind of "certain games don't draw well" symptom this whole
// verification pass was commissioned to hunt for. **Fixed** by adding the
// missing `newTopY >= 0` guard to `ascScroll()`'s own condition,
// reproducing the wraparound-reject outcome exactly (verified byte-for-
// byte against a Python simulation before ever touching the port, and
// again live in the emulator afterward - see below). See `ascScroll()`'s
// own comment for the full derivation.
//
// **Bug 2 - a real, confirmed out-of-bounds array write.** The title
// screen's own "INUFUTO 2026" credit-line overlay (slot 1) is 12
// characters, but `ascOverlayTextTable` was declared `[ASC_MAX_OVERLAYS][10]`
// - 2 words too narrow. `ascBeginOverlay()`'s own copy loop had no bounds
// check, so the last 2 characters were written straight into the next
// slot's own row (silently clobbered again moments later by that slot's
// own real content, so no *lasting* corruption there) while leaving
// `ascOverlayLen[1]` itself set to the real, unclamped 12 - meaning
// `ascRender()` kept trying to read 2 genuinely out-of-bounds table
// entries every single frame the title screen was shown. **Fixed** by
// widening the array to 12 (the real known maximum across every string
// this file ever puts through it), matching this project's own "widen
// with real margin, not an arbitrary guess" precedent.
//
// **Bug 3 - a severe, previously entirely unaddressed CPU-budget overrun,
// found immediately while live-testing bug 2's own fix.** Confirmed via
// the WebGL perf overlay: `ascRender()` pegged CPU at a sustained,
// saturated 100% even on the completely static title screen (10fps
// instead of 60), with a directly visible consequence - a genuine,
// reproducible frame truncation (this project's own well-documented "CPU
// budget exceeded, execution literally stops mid-instruction-stream"
// signature) caught losing the trailing "E" of "CONTINUE" and the entire
// credit line on individual captured frames, independent of and in
// addition to bug 2's own array-bounds corruption. This function had
// never received any of the optimization techniques this project has
// applied to nearly every other game's own render loop - no per-row call-
// site gating, no composited buffers, no dirty-flag caching, a genuine
// O(pixels x overlay-slots) shape - because the whole file had only ever
// been compile-tested before, never actually run. Fixed in three layers,
// each measured before moving to the next (see each function's own
// comment for the full detail): (1) inlined `ascComposeGlyphByte()`
// directly into `ascComposeMapByte()`, its single hottest call site; (2)
// restructured `ascRender()`'s own overlay-matching loop to filter which
// of the (up to 6) overlay slots are even active on the CURRENT page
// once per page instead of re-scanning all 6 for every one of 1024
// pixels; (3) added a persistent per-pixel cache (`ascPixelCache`) plus a
// `ascFrameDirty` flag set at every real mutation site (`ascWriteCell`,
// `ascDrawSpritesIntoVVram`, `ascPrintC`, `ascComputeLivesDisplay`,
// `ascBeginOverlay`/`ascClearOverlays`, and defensively at the top of
// `ascBeginTrying()`/`ascBeginTitle()` for their own direct VVram/
// StatusChar writes) so the expensive composition work is only redone
// when something could actually have changed - **deliberately not** the
// "skip the whole draw call, previous frame just persists" shape several
// other games in this project use (e.g. Tiny Tris's own attract screen),
// since this game registers no `onResume` hook in `menuGameList.c` (its
// own `addGame()` call passes `NULL`) and fixing that is out of scope for
// this file alone - every real frame still issues all 1024
// `md_drawColumn()` calls unconditionally (cheap, and already proven fine
// at this volume by every other game in this project), reading from the
// cache instead of recomputing it, so a quit-confirmation-dialog resume
// (which can't force a redraw here) still reproduces the correct picture
// on its very next frame exactly like the original always-recompute
// version did - verified directly, see below. Measured before/after:
// static title screen 100%/10fps -> 17-71%/60fps; active gameplay
// (movement + climbing) 100%(implied, same saturated symptom observed) ->
// 64-65%/60fps.
//
// **Every other file/mechanism was checked and confirmed correct**,
// cross-verified line-by-line against the real upstream source (not just
// re-read from this port's own prior reasoning) rather than skipped:
// `MapToBackGround()`/`ascMapToBackground()`'s own tile-compositing
// (including the RLE decompression, byte-diffed against a from-scratch
// Python re-implementation of `InitStage()`'s real algorithm for all 10
// stages' real `Bytes/Enemies/Blocks` tables - all matched exactly, zero
// transcription errors this time); `DrawSprites()`/`ascDrawSpritesIntoVVram()`'s
// own byte-wraparound-as-visibility-reject trick (confirmed the signed
// `y>=0 && y<VVramHeight` translation is exactly equivalent for every
// realistic sprite/window-offset delta); `VVramToVram()`'s own nibble-
// interleaved `SendUL()` byte emission (traced by hand against
// `ascComposeGlyphByte()`'s own 4-way sub dispatch, confirmed identical
// column-by-column and page-by-page); `GetCellType()`/`IsOnFloor()`'s own
// AVR-wraparound-as-a-bounds-trick (confirmed the port's explicit
// `row<0`/negative-arithmetic handling reproduces both exactly, including
// the one case - `IsOnFloor()`'s own `(y-1)&3` at y=0 - where a genuinely
// negative signed int and a wrapped byte happen to agree by construction,
// not by luck); `Title()`'s own real layout (title-logo placement,
// "MINI"/credit/START/CONTINUE/cursor overlay positions, the `AsciiTable[]`
// character-index order) and its busy-wait-for-release debounce (correctly
// converted to edge-detection); `LooseMan()`'s own 8-step death loop and
// the exact point `Actor_Live`/`RemainCount` get updated relative to it;
// `Sound.cpp`'s entire note-data set (all 8 melodies, including the two
// 105-byte BGM tracks) byte-diffed against a fresh Python extraction -
// all matched exactly; every status-bar/lives-icon composite
// (`PrintStatus()`/`Put2C()`'s own 2x2-icon-from-4-glyphs convention,
// traced byte-for-byte against `ascComposeLivesByte()`). The apparent
// "lives icons visible on the attract screen" (2 small man-icons next to
// the credit line) is faithful to upstream, not a bug - `Title()` itself
// calls the same `PrintStatus()` gameplay uses, which upstream's own
// design never suppresses there either.
//
// **Verified live**, using an isolated Puppeteer/WebGL instance (own
// server, own copy of the WebBuild folder, cleaned up afterward): the
// menu (found and selected "ASCEND", confirmed "BY INUFUTO" credit), the
// attract screen (logo, MINI subtitle, START/CONTINUE + blinking cursor,
// and - after the fix - the complete, correctly-rendered "INUFUTO 2026"
// credit line), real input-driven gameplay (moving right along a floor,
// climbing a real ladder, an item pickup registering a score), and the
// quit-confirmation dialog opening over active gameplay and correctly
// resuming with no leftover dialog pixels (the specific risk the caching
// design above was built to avoid). The scroll-ratchet fix itself was
// proven two ways: mathematically, via a standalone Python re-
// implementation of both upstream's real byte-wraparound arithmetic and
// the port's exact fix, run across a full climb on the tallest (10-floor)
// stage; and live, via a temporary, fully-removed-afterward debug hook in
// `ascMoveMan()`/`ascFallMan()` that free-climbed Man straight up
// (bypassing maze/ladder navigation, which real monsters/fires made
// impractical to script reliably) - confirmed the window scrolled
// correctly and coherently through the entire danger zone (world-Y
// crossing 12, then 11, down to the win condition), reached the win
// condition cleanly, played the level-clear/bonus-tally sequence with
// correct score/time updates every step, and advanced to a fully-correct,
// newly-rendered Stage 2. `ASC_TICK_DIVISOR` was verified back at its
// real value (8) and the file compiles with zero warnings before/after
// every debug hook was removed.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into ascCharPattern (map/sprite tiles, 2 bytes
//   each) - Char_End(0x74=116) confirms 116 total glyphs, byte-diff-verified.
// -----------------------------------------------------------------------------

#define ASC_CHAR_SPACE 0x00
#define ASC_CHAR_LADDER_LEFT 0x10
#define ASC_CHAR_LADDER_RIGHT 0x11
#define ASC_CHAR_FLOOR 0x12
#define ASC_CHAR_WALL 0x13
#define ASC_CHAR_MAN 0x14
#define ASC_CHAR_MAN_LEFT 0x14
#define ASC_CHAR_MAN_LEFT_STOP 0x14
#define ASC_CHAR_MAN_LEFT0 0x18
#define ASC_CHAR_MAN_LEFT1 0x1C
#define ASC_CHAR_MAN_LEFT2 0x20
#define ASC_CHAR_MAN_RIGHT 0x24
#define ASC_CHAR_MAN_RIGHT_STOP 0x24
#define ASC_CHAR_MAN_RIGHT0 0x28
#define ASC_CHAR_MAN_RIGHT1 0x2C
#define ASC_CHAR_MAN_RIGHT2 0x30
#define ASC_CHAR_MAN_CLIMB 0x34
#define ASC_CHAR_MAN_CLIMB0 0x34
#define ASC_CHAR_MAN_CLIMB1 0x38
#define ASC_CHAR_MAN_LOOSE0 0x3C
#define ASC_CHAR_MAN_LOOSE1 0x40
#define ASC_CHAR_MAN_LOOSE2 0x44
#define ASC_CHAR_FIRE 0x48
#define ASC_CHAR_FIRE_LEFT 0x48
#define ASC_CHAR_FIRE_RIGHT 0x50
#define ASC_CHAR_MONSTER 0x58
#define ASC_CHAR_MONSTER_LEFT 0x58
#define ASC_CHAR_MONSTER_RIGHT 0x60
#define ASC_CHAR_ITEM_1UP 0x68
#define ASC_CHAR_ITEM_POWER 0x6C
#define ASC_CHAR_BLOCK 0x70
#define ASC_MAN_WALK_OFFSET 4

// -----------------------------------------------------------------------------
//   Movable.h / Actor.h / Block.h / Sprite.h status-bit + slot constants
// -----------------------------------------------------------------------------

#define ASC_COORD_SHIFT 0
#define ASC_COORD_RATE ( 1 << ASC_COORD_SHIFT )
#define ASC_COORD_MASK ( ASC_COORD_RATE - 1 )

#define ASC_ACTOR_SEQMASK 0x03
#define ASC_ACTOR_LIVE 0x80
#define ASC_MAN_JUMP 0x40
#define ASC_MAN_FALL 0x20
#define ASC_ITEM_1UP 0x40
#define ASC_ITEM_POWER 0x20

#define ASC_BLOCK_LIVE 0x80
#define ASC_BLOCK_UP 0x20

#define ASC_SPRITE_MAN 0
#define ASC_SPRITE_BLOCK 1
#define ASC_MAX_BLOCK_COUNT 5
#define ASC_SPRITE_FIRE 8
#define ASC_MAX_FIRE_COUNT 4
#define ASC_SPRITE_MONSTER 12
#define ASC_MAX_MONSTER_COUNT 6
#define ASC_SPRITE_COUNT 18
#define ASC_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Stage.h / VVram.h
// -----------------------------------------------------------------------------

#define ASC_CELLTYPE_MASK 0x03
#define ASC_CELLTYPE_SPACE 0x00
#define ASC_CELLTYPE_LADDER 0x01
#define ASC_CELLTYPE_WALL 0x02
#define ASC_CELLTYPE_HOLE 0x03

#define ASC_COLUMN_COUNT 12
#define ASC_FLOOR_HEIGHT 4
#define ASC_FLOOR_MASK ( ASC_FLOOR_HEIGHT - 1 )
#define ASC_FLOOR_SHIFT 2
#define ASC_OVERHEAD ( ASC_FLOOR_HEIGHT - 3 )
#define ASC_COLUMN_WIDTH 2
#define ASC_STAGE_WIDTH ( ASC_COLUMN_WIDTH * ASC_COLUMN_COUNT )
#define ASC_MAP_WIDTH ( ASC_COLUMN_COUNT / 4 )

#define ASC_VVRAM_WIDTH 24
#define ASC_VVRAM_HEIGHT 16
#define ASC_VISIBLE_FLOOR_COUNT ( ( ASC_VVRAM_HEIGHT + ASC_FLOOR_HEIGHT - 1 ) / ASC_FLOOR_HEIGHT + 1 )

#define ASC_STAGE_COUNT 10
#define ASC_MAX_MAP_HEIGHT 10
#define ASC_STAGE_MAP_SIZE ( ASC_MAP_WIDTH * ASC_MAX_MAP_HEIGHT )

#define ASC_MAX_TIME_DENOM ( 50 / ( 8 / ASC_COORD_RATE ) )
#define ASC_BONUS_RATE 8
#define ASC_FIRE_CALL_INTERVAL ( ASC_COORD_RATE * 12 )
#define ASC_FIRE_SPAWN_INTERVAL 100

#define ASC_TICK_DIVISOR 8

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, kept as real expressions matching upstream.
// -----------------------------------------------------------------------------

#define ASC_N8 6
#define ASC_N8P ( ASC_N8 * 3 / 2 )
#define ASC_N4 ( ASC_N8 * 2 )
#define ASC_N4P ( ASC_N4 * 3 / 2 )
#define ASC_N2 ( ASC_N4 * 2 )
#define ASC_N2P ( ASC_N2 * 3 / 2 )
#define ASC_N1 ( ASC_N2 * 2 )

#define ASC_C3 9
#define ASC_D3 11
#define ASC_E3 13
#define ASC_F3 14
#define ASC_G3 16
#define ASC_A3 18
#define ASC_B3 20
#define ASC_C4 21
#define ASC_C4S 22
#define ASC_D4 23
#define ASC_E4 25
#define ASC_F4 26
#define ASC_G4 28
#define ASC_A4 30
#define ASC_B4 32
#define ASC_C5 33
#define ASC_D5 35
#define ASC_E5 37
#define ASC_F5 38

#define ASC_TEMPO 160

#define ASC_MELODY_NONE 0
#define ASC_MELODY_LOOSE 1
#define ASC_MELODY_BEEP 2
#define ASC_MELODY_ITEM 3
#define ASC_MELODY_START 4
#define ASC_MELODY_CLEAR 5
#define ASC_MELODY_GAMEOVER 6
#define ASC_MELODY_BGM1 7
#define ASC_MELODY_BGM2 8

#define ASC_MAX_OVERLAYS 6

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the
//   real upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph (a full-page-
// height text glyph, no nibble interleave needed - used for status text).
int ascAsciiPattern[108] = {
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

// CharPattern - 116 map/sprite-tile glyphs, 2 bytes/glyph (a 4x4 pixel
// block, nibble-interleaved with an adjacent VVram row via ascComposeGlyphByte).
int ascCharPattern[232] = {
    0, 0, 51, 0, 204, 0, 255, 0,
    0, 51, 51, 51, 204, 51, 255, 51,
    0, 204, 51, 204, 204, 204, 255, 204,
    0, 255, 51, 255, 204, 255, 255, 255,
    240, 170, 170, 15, 51, 51, 255, 255,
    128, 245, 125, 8, 16, 60, 195, 1,
    0, 245, 253, 0, 144, 52, 67, 5,
    0, 117, 125, 0, 0, 208, 193, 0,
    0, 245, 125, 0, 16, 45, 67, 5,
    128, 215, 95, 8, 16, 60, 195, 1,
    0, 223, 95, 0, 80, 52, 67, 9,
    0, 215, 87, 0, 0, 28, 13, 0,
    0, 215, 95, 0, 80, 52, 210, 1,
    128, 179, 187, 4, 0, 53, 195, 0,
    64, 187, 59, 8, 0, 60, 83, 0,
    76, 172, 138, 68, 19, 83, 21, 34,
    128, 195, 60, 8, 16, 190, 175, 1,
    68, 168, 202, 200, 34, 81, 53, 50,
    200, 254, 221, 10, 99, 171, 127, 3,
    128, 236, 155, 1, 215, 223, 255, 7,
    160, 221, 239, 140, 48, 247, 186, 54,
    16, 185, 206, 8, 112, 255, 253, 125,
    168, 175, 239, 8, 16, 115, 191, 0,
    64, 78, 206, 0, 50, 247, 255, 2,
    128, 254, 250, 138, 0, 251, 55, 1,
    0, 236, 228, 4, 32, 255, 127, 35,
    30, 31, 179, 227, 99, 102, 100, 54,
    236, 46, 238, 12, 247, 175, 255, 7,
    30, 221, 221, 225, 135, 187, 187, 120,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int ascFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int ascMelodyLoose[3] = { 1, ASC_A3, 0 };
int ascMelodyBeep[3] = { 1, ASC_A4, 0 };
int ascMelodyItem[13] = {
    1, ASC_C4, 1, ASC_C4S, 1, ASC_D4, 1, ASC_F4, 1, ASC_A4,
    1, ASC_C5, 0,
};
int ascMelodyStart[27] = {
    ASC_N8, 0, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5,
    ASC_N4, ASC_G4, ASC_N4, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_D5, ASC_N8, ASC_C5,
    ASC_N4, ASC_D5, ASC_N4, ASC_E5, ASC_N1, ASC_C5, 0,
};
int ascMelodyClear[21] = {
    ASC_N8, ASC_C4, ASC_N8, ASC_E4, ASC_N8, ASC_G4, ASC_N8, ASC_D4, ASC_N8, ASC_F4,
    ASC_N8, ASC_A4, ASC_N8, ASC_E4, ASC_N8, ASC_G4, ASC_N8, ASC_B4, ASC_N4P, ASC_C5,
    0,
};
int ascMelodyGameOver[19] = {
    ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_G4, ASC_N8, ASC_G4, ASC_N8, ASC_A4,
    ASC_N8, ASC_A4, ASC_N8, ASC_B4, ASC_N8, ASC_B4, ASC_N2P, ASC_C5, 0,
};
int ascMelodyBgm1[105] = {
    ASC_N8, 0, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5,
    ASC_N4, ASC_G4, ASC_N4, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_D5, ASC_N8, ASC_C5,
    ASC_N4, ASC_D5, ASC_N4, ASC_E5, ASC_N8, 0, ASC_N8, ASC_C5, ASC_N8, ASC_C5,
    ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N4, ASC_D5, ASC_N4, ASC_F5, ASC_N8, ASC_F5,
    ASC_N8, ASC_E5, ASC_N8, ASC_C5, ASC_N4, ASC_C5, ASC_N4, ASC_D5, ASC_N8, 0,
    ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_C5, ASC_N4, ASC_G4,
    ASC_N4, ASC_C5, ASC_N8, ASC_C5, ASC_N8, ASC_D5, ASC_N8, ASC_C5, ASC_N4, ASC_D5,
    ASC_N4, ASC_E5, ASC_N4, ASC_F5, ASC_N4, ASC_F5, ASC_N4, ASC_E5, ASC_N4, ASC_E5,
    ASC_N4, ASC_D5, ASC_N8, ASC_D5, ASC_N4, ASC_E5, ASC_N8, ASC_E5, ASC_N8, ASC_D5,
    ASC_N8, ASC_C5, ASC_N4, ASC_C5, ASC_N4, ASC_C5, ASC_N8, ASC_D5, ASC_N4, ASC_D5,
    ASC_N4P, ASC_C5, ASC_N2P, 0, 255,
};
int ascMelodyBgm2[105] = {
    ASC_N8, ASC_C4, ASC_N4, 0, ASC_N4P, ASC_E4, ASC_N8, ASC_G4, ASC_N8, 0,
    ASC_N8, ASC_A3, ASC_N4, 0, ASC_N4P, ASC_C4, ASC_N8, ASC_E4, ASC_N8, 0,
    ASC_N8, ASC_D4, ASC_N4, 0, ASC_N4P, ASC_F3, ASC_N8, ASC_A3, ASC_N8, 0,
    ASC_N8, ASC_G3, ASC_N4, 0, ASC_N4P, ASC_B3, ASC_N8, ASC_D4, ASC_N8, 0,
    ASC_N8, ASC_C4, ASC_N4, 0, ASC_N4P, ASC_E4, ASC_N8, ASC_G4, ASC_N8, 0,
    ASC_N8, ASC_A3, ASC_N4, 0, ASC_N4P, ASC_C4, ASC_N8, ASC_E4, ASC_N8, 0,
    ASC_N8, ASC_F3, ASC_N4, 0, ASC_N4P, ASC_A3, ASC_N8, ASC_C4, ASC_N8, 0,
    ASC_N8, ASC_D4, ASC_N4, 0, ASC_N4P, ASC_E4, ASC_N8, ASC_A3, ASC_N8, 0,
    ASC_N8, ASC_F3, ASC_N4, 0, ASC_N8, ASC_F3, ASC_N8, ASC_G3, ASC_N8, 0,
    ASC_N8, ASC_B3, ASC_N8, ASC_D4, ASC_N8, ASC_C4, ASC_N4, 0, ASC_N4P, ASC_E4,
    ASC_N8, ASC_G4, ASC_N8, 0, 255,
};

// "ASCEND" title logo - 6 characters x 4x4 raw glyph-index blocks, indexing
// directly into the first 16 entries of ascCharPattern (its own "logo font"
// sub-range, values 0x0-0xf).
int ascTitleBytes[96] = {
    0, 14, 13, 2, 12, 3, 0, 15,
    12, 7, 5, 15, 4, 1, 0, 5,
    8, 7, 5, 11, 4, 11, 10, 2,
    8, 2, 0, 15, 0, 5, 5, 1,
    0, 14, 5, 11, 12, 3, 0, 0,
    4, 11, 0, 10, 0, 4, 5, 1,
    12, 7, 5, 1, 12, 11, 10, 2,
    12, 3, 0, 0, 4, 5, 5, 1,
    12, 11, 0, 15, 12, 15, 11, 15,
    12, 3, 13, 15, 4, 1, 0, 5,
    12, 7, 13, 2, 12, 3, 0, 15,
    12, 3, 8, 7, 4, 5, 5, 0,
};

// Stage data - flattened from upstream's own `Stage{height,pBytes,
// monsterCount,pMonsters,blockCount,pBlocks}` struct array + 10 separate
// BytesN/EnemiesN/BlocksN[] tables into parallel padded fixed arrays
// (matching this project's own "flatten to plain arrays" precedent) -
// RLE-compressed layout bytes padded to the longest table's width(49) with
// trailing 0 (harmless - ascStageByteCount bounds every real read),
// enemy/block position bytes padded to their own real max width (6/5).
int ascStageHeight[10] = { 4, 5, 5, 6, 7, 7, 8, 9, 9, 10 };
int ascStageByteCount[10] = { 12, 14, 17, 20, 23, 22, 27, 37, 49, 35 };
int ascStageMonsterCount[10] = { 2, 3, 2, 4, 4, 4, 4, 6, 6, 6 };
int ascStageBlockCount[10] = { 2, 2, 3, 3, 3, 2, 3, 5, 3, 5 };

int ascStageBytesTable[10][49] = {
    {5, 36, 17, 19, 4, 5, 27, 5, 3, 13, 21, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {30, 13, 9, 6, 3, 1, 5, 11, 13, 28, 9, 33, 25, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {7, 1, 32, 22, 3, 13, 0, 9, 3, 1, 13, 11, 5, 36, 9, 17, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {21, 19, 0, 9, 26, 7, 13, 28, 1, 25, 2, 11, 3, 1, 17, 13, 3, 9, 21, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {33, 11, 11, 9, 20, 22, 17, 0, 17, 2, 9, 8, 3, 1, 33, 0, 9, 2, 21, 4, 9, 13, 17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {9, 32, 1, 7, 21, 9, 25, 19, 3, 1, 9, 6, 3, 12, 13, 29, 15, 9, 16, 5, 33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {21, 20, 29, 12, 41, 0, 3, 14, 13, 8, 5, 2, 1, 10, 1, 12, 1, 14, 1, 9, 11, 9, 31, 0, 13, 17, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {37, 4, 5, 21, 2, 9, 1, 25, 2, 9, 3, 1, 13, 21, 6, 10, 3, 13, 4, 1, 2, 1, 6, 5, 17, 1, 2, 1, 17, 9, 0, 1, 29, 8, 17, 17, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {30, 9, 0, 3, 13, 2, 1, 2, 13, 17, 2, 1, 2, 9, 0, 3, 9, 6, 1, 2, 13, 5, 14, 1, 2, 9, 0, 13, 6, 1, 2, 13, 13, 6, 1, 2, 9, 0, 17, 2, 1, 2, 1, 8, 13, 6, 1, 13, 0},
    {29, 15, 13, 18, 9, 5, 10, 18, 5, 1, 15, 2, 5, 6, 5, 1, 18, 5, 6, 5, 1, 18, 5, 6, 5, 17, 9, 6, 5, 21, 21, 25, 16, 5, 36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

int ascStageEnemiesTable[10][6] = {
    {5, 32, 0, 0, 0, 0},
    {16, 25, 56, 0, 0, 0},
    {39, 55, 0, 0, 0, 0},
    {23, 43, 50, 73, 0, 0},
    {3, 40, 68, 87, 0, 0},
    {19, 32, 74, 84, 0, 0},
    {20, 57, 90, 102, 0, 0},
    {5, 18, 35, 58, 89, 119},
    {17, 24, 41, 80, 88, 100},
    {3, 18, 50, 98, 112, 136},
};

int ascStageBlocksTable[10][5] = {
    {22, 53, 0, 0, 0},
    {33, 41, 0, 0, 0},
    {57, 67, 69, 0, 0},
    {37, 57, 85, 0, 0},
    {25, 55, 101, 0, 0},
    {56, 101, 0, 0, 0},
    {34, 73, 105, 0, 0},
    {26, 52, 67, 101, 130},
    {57, 105, 114, 0, 0},
    {25, 37, 115, 130, 151},
};

// -----------------------------------------------------------------------------
//   Structs
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y, sprite;
    int dx, dy;
    int status;
    int pattern;
} AscActor;

typedef struct
{
    int x, y, sprite;
    int status;
} AscBlock;

typedef struct
{
    int x, y;
    int code;
} AscSprite;

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int ascScore;
int ascHiScore;
int ascRemainCount;
int ascCurrentStage;
int ascStageTime;
int ascClock;
int ascMonsterNum;
int ascFireCount;
int ascPowerTime;
int ascJumpSeq;
int ascMinY;
bool ascOldLeft, ascOldRight, ascOldUp, ascOldDown;

int ascTopY;
int ascTopRow;
int ascYMod;
int ascTopYRange;
int ascStageIndex;
int ascTimeRate;
int ascRndIndex;
int ascFireSpawnCount;
int ascFireSpawnRate;
int ascTimeDenom;

int ascVVram[ASC_VVRAM_HEIGHT][ASC_VVRAM_WIDTH];
int ascStageMap[ASC_STAGE_MAP_SIZE];

AscActor ascMan;
AscActor ascFires[ASC_MAX_FIRE_COUNT];
AscActor ascMonsters[ASC_MAX_MONSTER_COUNT];
AscBlock ascBlocks[ASC_MAX_BLOCK_COUNT];
AscSprite ascSprites[ASC_SPRITE_COUNT];

int ascStatusChar[8][8];
int ascLivesMode;
int ascLivesIconCount;
int ascLivesDigit;

// Widened from 10 to 12 - the "INUFUTO 2026" credit line overlay (slot 1,
// see ascBeginTitle) is 12 characters, exactly 2 past the original width.
// A real, confirmed bug: ascBeginOverlay()'s own copy loop wrote those last
// 2 characters straight past this array's row bound with no bounds check,
// landing on slot 2's own first 2 words (silently overwritten again moments
// later by slot 2's own real ascBeginOverlay(2,sStart,...) call, so no
// lasting corruption there) - but it also left ascOverlayLen[1] itself
// set to the full unclamped 12, so ascRender()'s per-column loop kept
// trying to index ascOverlayTextTable[1][10]/[11] every frame the title
// screen was shown. Confirmed visually via Puppeteer: the whole credit
// line rendered as fully blank (not just its last 2 characters truncated),
// not merely a 2-character glitch - fixed by widening to the real known
// maximum (12), matching this project's own "widen with real margin, not
// an arbitrary guess" precedent (e.g. the EEPROM nameTag bump).
//
// Retyped `char` (not `int`) - holds real text passed through
// ascBeginOverlay()'s own `char* s` parameter, matching this project's own
// established convention for every text-shaped array in this Cate-engine
// batch (see e.g. gameBootskell.c's own bskOverlayText[]).
char ascOverlayTextTable[ASC_MAX_OVERLAYS][12];
int ascOverlayLen[ASC_MAX_OVERLAYS];
int ascOverlayPage[ASC_MAX_OVERLAYS];
int ascOverlayCol[ASC_MAX_OVERLAYS];

bool ascPZeroVisible;
int ascPByteValue;
int ascPWordValue;

int ascSeqMelody[3];
int ascSeqPos[3];
int ascSeqWait[3];
int ascSeqActive[3];

int ascTickCounter;

#define ASC_STATE_TITLE 0
#define ASC_STATE_START_JINGLE 1
#define ASC_STATE_PLAYING 2
#define ASC_STATE_LOSE_ANIM 3
#define ASC_STATE_GAMEOVER_JINGLE 4
#define ASC_STATE_CLEAR_WAIT 5
#define ASC_STATE_CLEAR_JINGLE 6
#define ASC_STATE_BONUS_TALLY 7
int ascState;
int ascWaitFrames;
int ascAnimStep;
int ascSelection;
bool ascPrevLeft, ascPrevRight, ascPrevUp, ascPrevDown, ascPrevFire;
bool ascPendingContinue;

// A real CPU-load fix, not a fidelity change (see ascRender()'s own header
// comment for the full writeup - a genuine, confirmed-via-live-play frame
// truncation, not a theoretical concern). ascRender() recomputed every one
// of 1024 pixels completely from scratch every single real 60fps frame,
// with no exception even while genuinely nothing on screen had changed
// (the whole static title screen, or a PLAYING-state frame that isn't one
// of the roughly-1-in-8 real frames where ASC_TICK_DIVISOR actually lets
// game logic run). `ascFrameDirty` gates the entire render call - matches
// this project's own established "skip the whole draw call, the previous
// frame simply persists on screen" pattern already used elsewhere (e.g.
// Tiny Tris's own attract-screen dirty flag) - set true at every real
// mutation site (ascWriteCell/ascDrawSpritesIntoVVram for ascVVram,
// ascPrintC for ascStatusChar, ascComputeLivesDisplay for the lives
// display fields, ascBeginOverlay/ascClearOverlays for the overlay
// system) plus, redundantly but safely, at the top of ascBeginTrying()/
// ascBeginTitle() themselves (both also write ascVVram/ascStatusChar
// directly, bypassing those helpers, for their own initial full-screen
// setup). Must start true (not the zero-initialized default) so the very
// first real frame after boot actually renders - set explicitly in
// gameAscend_init().
bool ascFrameDirty;

// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int ascRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

int ascRnd()
{
    int r;
    r = ascRndNumbers[ ascRndIndex ];
    ascRndIndex = ascRndIndex + 1;
    if( ascRndIndex >= 32 )
      ascRndIndex = 0;
    return r & 0x0f;
}

// Sign(from,to): 0 if equal, +1 if from<to, -1 if from>to - ported exactly
// as upstream names/orders the arguments, not "corrected" to a more
// intuitive from-caller's-perspective meaning (see file header comment).
int ascSign( int from, int to )
{
    if( from == to ) return 0;
    if( from < to ) return 1;
    return -1;
}

// -----------------------------------------------------------------------------
//   Movable.cpp - ported as plain signed-int distance checks rather than the
//   real `static_cast<byte>((a-b)+1) < N` byte-wraparound trick upstream
//   uses (behaviorally identical for the small, always-nearby coordinate
//   deltas these are ever called with - see file header comment).
//   ascIsNearXY deliberately keeps upstream's own ASYMMETRIC range (only
//   {-1,0}, not {-1,0,1} like ascIsNear) - a genuine, faithfully-preserved
//   upstream quirk, not a transcription slip.
// -----------------------------------------------------------------------------

bool ascIsNear( int a, int b )
{
    int diff;
    diff = a - b;
    if( diff < 0 ) diff = -diff;
    return diff <= 1;
}

bool ascIsNearXY( int x1, int y1, int x2, int y2 )
{
    int dx, dy;
    dx = x1 - x2;
    dy = y1 - y2;
    return ( dx == -1 || dx == 0 ) && ( dy == -1 || dy == 0 );
}

// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void ascHideAllSprites()
{
    int i;
    for( i = 0; i < ASC_SPRITE_COUNT; i = i + 1 )
      ascSprites[ i ].code = ASC_INVALID_CODE;
}

void ascShowSpriteXY( int x, int y, int spriteIdx, int code )
{
    ascSprites[ spriteIdx ].x = x;
    ascSprites[ spriteIdx ].y = y;
    ascSprites[ spriteIdx ].code = code;
}

void ascHideSprite( int idx )
{
    ascSprites[ idx ].code = ASC_INVALID_CODE;
}

// -----------------------------------------------------------------------------
//   VVram compositing (VVram.cpp's MapToBackGround/DrawSprites/DrawAll)
// -----------------------------------------------------------------------------

void ascWriteCell( int y, int x, int left, int right )
{
    if( y >= 0 && y < ASC_VVRAM_HEIGHT )
    {
        ascVVram[ y ][ x ] = left;
        ascVVram[ y ][ x + 1 ] = right;
        ascFrameDirty = true;
    }
}

void ascMapToBackground()
{
    int row, yPos, col, byteIdx, sub, upperCell, lowerCell, uType, lType, floorIdx, height;

    row = ascTopRow;
    yPos = -ascYMod;
    height = ascStageHeight[ ascStageIndex ];

    for( floorIdx = 0; floorIdx < ASC_VISIBLE_FLOOR_COUNT; floorIdx = floorIdx + 1 )
    {
        if( yPos >= ASC_VVRAM_HEIGHT ) break;

        col = 0;
        for( byteIdx = 0; byteIdx < ASC_MAP_WIDTH; byteIdx = byteIdx + 1 )
        {
            if( row < 0 )
              upperCell = 0;
            else
              upperCell = ascStageMap[ row * ASC_MAP_WIDTH + byteIdx ];
            if( row + 1 >= height )
              lowerCell = 0;
            else
              lowerCell = ascStageMap[ ( row + 1 ) * ASC_MAP_WIDTH + byteIdx ];

            for( sub = 0; sub < 4; sub = sub + 1 )
            {
                uType = upperCell & ASC_CELLTYPE_MASK;
                lType = lowerCell & ASC_CELLTYPE_MASK;

                if( uType == ASC_CELLTYPE_SPACE )
                {
                    ascWriteCell( yPos + 0, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                    if( lType == ASC_CELLTYPE_LADDER )
                    {
                        ascWriteCell( yPos + 1, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                        ascWriteCell( yPos + 2, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                        ascWriteCell( yPos + 3, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                    }
                    else if( lType == ASC_CELLTYPE_WALL )
                    {
                        ascWriteCell( yPos + 1, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                        ascWriteCell( yPos + 2, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                        ascWriteCell( yPos + 3, col, ASC_CHAR_WALL, ASC_CHAR_FLOOR );
                    }
                    else
                    {
                        ascWriteCell( yPos + 1, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                        ascWriteCell( yPos + 2, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                        ascWriteCell( yPos + 3, col, ASC_CHAR_FLOOR, ASC_CHAR_FLOOR );
                    }
                }
                else if( uType == ASC_CELLTYPE_LADDER )
                {
                    ascWriteCell( yPos + 0, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                    ascWriteCell( yPos + 1, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                    ascWriteCell( yPos + 2, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                    if( lType == ASC_CELLTYPE_LADDER )
                      ascWriteCell( yPos + 3, col, ASC_CHAR_LADDER_LEFT, ASC_CHAR_LADDER_RIGHT );
                    else if( lType == ASC_CELLTYPE_WALL )
                      ascWriteCell( yPos + 3, col, ASC_CHAR_WALL, ASC_CHAR_FLOOR );
                    else
                      ascWriteCell( yPos + 3, col, ASC_CHAR_FLOOR, ASC_CHAR_FLOOR );
                }
                else if( uType == ASC_CELLTYPE_WALL )
                {
                    ascWriteCell( yPos + 0, col, ASC_CHAR_WALL, ASC_CHAR_SPACE );
                    ascWriteCell( yPos + 1, col, ASC_CHAR_WALL, ASC_CHAR_SPACE );
                    ascWriteCell( yPos + 2, col, ASC_CHAR_WALL, ASC_CHAR_SPACE );
                    if( lType == ASC_CELLTYPE_WALL )
                      ascWriteCell( yPos + 3, col, ASC_CHAR_WALL, ASC_CHAR_FLOOR );
                    else
                      ascWriteCell( yPos + 3, col, ASC_CHAR_FLOOR, ASC_CHAR_FLOOR );
                }
                else
                {
                    ascWriteCell( yPos + 0, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                    ascWriteCell( yPos + 1, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                    ascWriteCell( yPos + 2, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                    ascWriteCell( yPos + 3, col, ASC_CHAR_SPACE, ASC_CHAR_SPACE );
                }

                col = col + ASC_COLUMN_WIDTH;
                upperCell = upperCell >> 2;
                lowerCell = lowerCell >> 2;
            }
        }
        row = row + 1;
        yPos = yPos + ASC_FLOOR_HEIGHT;
    }
}

void ascDrawSpritesIntoVVram()
{
    int i, x, y, c;
    ascFrameDirty = true;
    for( i = 0; i < ASC_SPRITE_COUNT; i = i + 1 )
    {
        if( ascSprites[ i ].code != ASC_INVALID_CODE )
        {
            x = ascSprites[ i ].x;
            y = ascSprites[ i ].y - ascTopY;
            if( y >= 0 && y < ASC_VVRAM_HEIGHT )
            {
                c = ascSprites[ i ].code;
                ascVVram[ y ][ x ] = c; c = c + 1;
                ascVVram[ y ][ x + 1 ] = c; c = c + 1;
                y = y + 1;
                if( y < ASC_VVRAM_HEIGHT )
                {
                    ascVVram[ y ][ x ] = c; c = c + 1;
                    ascVVram[ y ][ x + 1 ] = c;
                }
            }
        }
    }
}

void ascDrawAll()
{
    ascMapToBackground();
    ascDrawSpritesIntoVVram();
}

void ascDrawBackground()
{
    int newTopRow;
    ascYMod = ascTopY;
    newTopRow = -1;
    while( ascYMod >= ASC_FLOOR_HEIGHT )
    {
        newTopRow = newTopRow + 1;
        ascYMod = ascYMod - ASC_FLOOR_HEIGHT;
    }
    ascTopRow = newTopRow;
    ascDrawAll();
}

// -----------------------------------------------------------------------------
//   Stage.cpp - RLE decompression + cell-type queries. `row` can legitimately
//   go negative near the very top of a stage (see file header comment) -
//   guarded explicitly rather than relying on AVR byte wraparound.
// -----------------------------------------------------------------------------

void ascDecompressStageMap()
{
    int yCount, byteIdx, byteCount, pMapIdx, d, shiftCount, xCount, b, spaceCount;

    yCount = ascStageHeight[ ascStageIndex ];
    byteIdx = 0;
    byteCount = ascStageByteCount[ ascStageIndex ];
    pMapIdx = 0;
    d = 0;
    shiftCount = 0;
    xCount = ASC_COLUMN_COUNT;

    while( true )
    {
        b = ascStageBytesTable[ ascStageIndex ][ byteIdx ];
        byteIdx = byteIdx + 1;
        spaceCount = b >> 2;
        while( spaceCount != 0 )
        {
            d = d >> 2;
            shiftCount = shiftCount + 1;
            if( shiftCount >= 4 )
            {
                ascStageMap[ pMapIdx ] = d;
                pMapIdx = pMapIdx + 1;
                d = 0;
                shiftCount = 0;
            }
            spaceCount = spaceCount - 1;
            xCount = xCount - 1;
        }
        d = d >> 2;
        d = d | ( ( b & ASC_CELLTYPE_MASK ) << 6 );
        shiftCount = shiftCount + 1;
        if( shiftCount >= 4 )
        {
            ascStageMap[ pMapIdx ] = d;
            pMapIdx = pMapIdx + 1;
            d = 0;
            shiftCount = 0;
        }
        xCount = xCount - 1;
        if( xCount == 0 )
        {
            yCount = yCount - 1;
            xCount = ASC_COLUMN_COUNT;
        }
        if( yCount == 0 ) break;
        if( byteIdx >= byteCount ) break;
    }
}

// Mirrors upstream's own wrap-and-decrement stage selection: `ascStageIndex`
// cycles 0..9 as `ascCurrentStage` climbs past 10, and `ascTimeRate` (the
// per-floor time bonus in ascBeginTrying) drops by 1 every full 10-stage
// lap, down to a floor of 0 - a real, deliberate rising-difficulty
// mechanic with no equivalent in this project's own sibling Cracky port.
void ascInitStage()
{
    int idx, i, j;
    idx = 0;
    i = 0;
    j = 0;
    ascTimeRate = 10;
    while( i < ascCurrentStage )
    {
        idx = idx + 1;
        i = i + 1;
        j = j + 1;
        if( j >= ASC_STAGE_COUNT )
        {
            idx = 0;
            j = 0;
            if( ascTimeRate != 0 )
              ascTimeRate = ascTimeRate - 1;
        }
    }
    ascStageIndex = idx;
    ascDecompressStageMap();
}

int ascGetCellType( int x, int y )
{
    int row, offset, b;
    row = ( y >> ASC_FLOOR_SHIFT ) - 1;
    if( row < 0 ) return 0;
    if( row >= ascStageHeight[ ascStageIndex ] ) return 0;
    x = x >> 1;
    offset = ( row * ASC_MAP_WIDTH ) + ( x >> 2 );
    b = ascStageMap[ offset ];
    x = x & 3;
    while( x != 0 )
    {
        b = b >> 2;
        x = x - 1;
    }
    return b & ASC_CELLTYPE_MASK;
}

bool ascIsOnFloor( int y )
{
    return ( ( y - ASC_OVERHEAD ) & ASC_FLOOR_MASK ) == 0;
}

bool ascIsWall( int x, int y )
{
    if( ( x & 1 ) != 0 ) return false;
    return ascGetCellType( x, y ) == ASC_CELLTYPE_WALL;
}

int ascToX( int b )
{
    return ( b & 0x0f ) << 1;
}

int ascToY( int b )
{
    int y;
    y = ( b & 0xf0 ) >> 2;
    return y + ( ASC_OVERHEAD + ASC_FLOOR_HEIGHT );
}

// -----------------------------------------------------------------------------
//   Block.cpp
// -----------------------------------------------------------------------------

void ascInitBlocks()
{
    int i, count, b, x, y;
    count = ascStageBlockCount[ ascStageIndex ];
    for( i = 0; i < count; i = i + 1 )
    {
        b = ascStageBlocksTable[ ascStageIndex ][ i ];
        x = ascToX( b );
        y = ascToY( b ) - 2;
        ascBlocks[ i ].x = x;
        ascBlocks[ i ].y = y;
        ascBlocks[ i ].sprite = ASC_SPRITE_BLOCK + i;
        ascBlocks[ i ].status = ASC_BLOCK_LIVE;
        ascShowSpriteXY( x, y, ascBlocks[ i ].sprite, ASC_CHAR_BLOCK );
    }
    for( i = count; i < ASC_MAX_BLOCK_COUNT; i = i + 1 )
      ascBlocks[ i ].status = 0;
}

bool ascHitBlock( int x, int y )
{
    int i;
    y = y + 2;
    for( i = 0; i < ASC_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( ( ascBlocks[ i ].status & ASC_BLOCK_LIVE ) != 0 )
        {
            if( ( ascBlocks[ i ].status & ASC_BLOCK_UP ) != 0 )
            {
                if( y - 1 == ascBlocks[ i ].y && ascIsNear( ascBlocks[ i ].x, x ) )
                  return true;
            }
        }
    }
    return false;
}

void ascHitUnderBlock()
{
    int i;
    for( i = 0; i < ASC_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( ( ascBlocks[ i ].status & ( ASC_BLOCK_LIVE | ASC_BLOCK_UP ) ) == ASC_BLOCK_LIVE )
        {
            if( ascIsNear( ascBlocks[ i ].x, ascMan.x ) && ascBlocks[ i ].y == ascMan.y - 2 )
            {
                ascBlocks[ i ].status = ascBlocks[ i ].status | ASC_BLOCK_UP;
                ascBlocks[ i ].y = ascBlocks[ i ].y - 1;
                ascShowSpriteXY( ascBlocks[ i ].x, ascBlocks[ i ].y, ascBlocks[ i ].sprite, ASC_CHAR_BLOCK );
                return;
            }
        }
    }
}

void ascHitOverBlock()
{
    int i;
    for( i = 0; i < ASC_MAX_BLOCK_COUNT; i = i + 1 )
    {
        if( ( ascBlocks[ i ].status & ( ASC_BLOCK_LIVE | ASC_BLOCK_UP ) ) == ( ASC_BLOCK_LIVE | ASC_BLOCK_UP ) )
        {
            if( ascIsNear( ascBlocks[ i ].x, ascMan.x ) && ascIsNear( ascBlocks[ i ].y - 2, ascMan.y ) )
            {
                ascBlocks[ i ].status = ascBlocks[ i ].status & ~ASC_BLOCK_UP;
                ascBlocks[ i ].y = ascBlocks[ i ].y + 1;
                ascShowSpriteXY( ascBlocks[ i ].x, ascBlocks[ i ].y, ascBlocks[ i ].sprite, ASC_CHAR_BLOCK );
                return;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Actor.cpp
// -----------------------------------------------------------------------------

void ascMoveActorYOnly( AscActor* pActor )
{
    int seq;
    pActor->y = pActor->y + pActor->dy;
    seq = pActor->x + pActor->y;
    pActor->status = ( pActor->status & ~ASC_ACTOR_SEQMASK ) | ( seq & ASC_ACTOR_SEQMASK );
}

void ascMoveActor( AscActor* pActor )
{
    pActor->x = pActor->x + pActor->dx;
    ascMoveActorYOnly( pActor );
}

// Reuses the exact ASC_MAN_JUMP(0x40) literal upstream's own Man_Jump does -
// a deliberate, faithfully-preserved bit-value collision with Item_1Up, see
// file header comment.
bool ascCanMove( AscActor* pActor, int dx )
{
    int y;
    y = pActor->y;
    if( ( pActor->status & ASC_MAN_JUMP ) != 0 )
      y = y - 1;
    if( dx < 0 )
      return pActor->x > 0 && !ascIsWall( pActor->x - 1, y ) && !ascHitBlock( pActor->x - 1, y );
    if( dx > 0 )
      return pActor->x < ASC_STAGE_WIDTH - 2 && !ascIsWall( pActor->x + 2, y ) && !ascHitBlock( pActor->x + 1, y );
    return true;
}

void ascShowEnemy( AscActor* pActor )
{
    int seq, pattern;
    seq = pActor->status & 1;
    pattern = pActor->pattern + ( seq << 2 );
    ascShowSpriteXY( pActor->x, pActor->y, pActor->sprite, pattern );
}

// -----------------------------------------------------------------------------
//   Sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
//   2=BGM-B), each advances every real frame regardless of ASC_TICK_DIVISOR -
//   matching upstream's own SoundHandler() running off the same real SysTick
//   ISR as gameplay, never itself throttled by the `Clock&3` gate. See file
//   header comment for the Cracky precedent this whole shape reuses.
// -----------------------------------------------------------------------------

int ascMelodyLength( int id )
{
    if( id == ASC_MELODY_LOOSE ) return 3;
    if( id == ASC_MELODY_BEEP ) return 3;
    if( id == ASC_MELODY_ITEM ) return 13;
    if( id == ASC_MELODY_START ) return 27;
    if( id == ASC_MELODY_CLEAR ) return 21;
    if( id == ASC_MELODY_GAMEOVER ) return 19;
    if( id == ASC_MELODY_BGM1 ) return 105;
    if( id == ASC_MELODY_BGM2 ) return 105;
    return 0;
}

int ascMelodyValue( int id, int idx )
{
    if( id == ASC_MELODY_LOOSE ) return ascMelodyLoose[ idx ];
    if( id == ASC_MELODY_BEEP ) return ascMelodyBeep[ idx ];
    if( id == ASC_MELODY_ITEM ) return ascMelodyItem[ idx ];
    if( id == ASC_MELODY_START ) return ascMelodyStart[ idx ];
    if( id == ASC_MELODY_CLEAR ) return ascMelodyClear[ idx ];
    if( id == ASC_MELODY_GAMEOVER ) return ascMelodyGameOver[ idx ];
    if( id == ASC_MELODY_BGM1 ) return ascMelodyBgm1[ idx ];
    if( id == ASC_MELODY_BGM2 ) return ascMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/ASC_TEMPO = 1.875 real 60Hz ticks - identical formula/constant to
// Cracky's own crkNoteFrames (same Sound.cpp Tempo=160 in both games).
int ascNoteFrames( int length )
{
    return (int)( length * 1.875 + 0.5 );
}

void ascStartSeq( int channel, int melodyId )
{
    ascSeqMelody[ channel ] = melodyId;
    ascSeqPos[ channel ] = 0;
    ascSeqWait[ channel ] = 0;
    ascSeqActive[ channel ] = 1;
}

void ascStopSeq( int channel )
{
    ascSeqActive[ channel ] = 0;
    ascSeqMelody[ channel ] = ASC_MELODY_NONE;
}

bool ascSeqPlaying( int channel )
{
    return ascSeqActive[ channel ] != 0;
}

void ascAdvanceOneSeq( int channel )
{
    int length, note;

    if( ascSeqActive[ channel ] == 0 ) return;

    if( ascSeqWait[ channel ] > 0 )
    {
        ascSeqWait[ channel ] = ascSeqWait[ channel ] - 1;
        return;
    }

    length = ascMelodyValue( ascSeqMelody[ channel ], ascSeqPos[ channel ] );
    if( length == 0 )
    {
        ascStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        ascSeqPos[ channel ] = 0;
        length = ascMelodyValue( ascSeqMelody[ channel ], 0 );
    }
    note = ascMelodyValue( ascSeqMelody[ channel ], ascSeqPos[ channel ] + 1 );
    ascSeqPos[ channel ] = ascSeqPos[ channel ] + 2;
    ascSeqWait[ channel ] = ascNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)ascFrequencies[ note - 1 ], (float)ascSeqWait[ channel ] / 60.0 );
}

void ascAdvanceSound()
{
    ascAdvanceOneSeq( 0 );
    ascAdvanceOneSeq( 1 );
    ascAdvanceOneSeq( 2 );
}

void ascStartBgm()
{
    ascStartSeq( 1, ASC_MELODY_BGM1 );
    ascStartSeq( 2, ASC_MELODY_BGM2 );
}

void ascStopBgm()
{
    ascStopSeq( 1 );
    ascStopSeq( 2 );
    md_stopTone();
}

// -----------------------------------------------------------------------------
//   Print.cpp / Status.cpp - status text written into ascStatusChar (an
//   ascii-index grid covering raw columns 96-127 / pages 0-7), plus a
//   dedicated icon compositor for the lives display (see file header
//   comment) and a multi-slot overlay mechanism for off-status-grid text.
// -----------------------------------------------------------------------------

int ascAsciiIndex( int c )
{
    // Kept as int[27], not char[27] - never passed anywhere as a string,
    // only ever compared against a scalar int (matching this project's
    // own established convention for a lookup/comparison table).
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

int ascPrintC( int page, int col, int c )
{
    ascStatusChar[ page ][ col ] = ascAsciiIndex( c );
    ascFrameDirty = true;
    return col + 1;
}

int ascPrintS( int page, int col, char* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = ascPrintC( page, col, s[ i ] );
    return col;
}

int ascPrintDigitByte( int page, int col, int n )
{
    int c;
    c = ascPByteValue / n;
    ascPByteValue = ascPByteValue % n;
    if( c == 0 )
    {
        if( ascPZeroVisible ) c = '0'; else c = ' ';
    }
    else
    {
        ascPZeroVisible = true;
        c = c + '0';
    }
    return ascPrintC( page, col, c );
}

int ascPrintByteNumber3( int page, int col, int b )
{
    ascPZeroVisible = false;
    ascPByteValue = b;
    col = ascPrintDigitByte( page, col, 100 );
    col = ascPrintDigitByte( page, col, 10 );
    col = ascPrintC( page, col, ascPByteValue + '0' );
    return col;
}

int ascPrintByteNumber2( int page, int col, int b )
{
    ascPZeroVisible = false;
    ascPByteValue = b;
    col = ascPrintDigitByte( page, col, 10 );
    col = ascPrintC( page, col, ascPByteValue + '0' );
    return col;
}

int ascPrintDigitWord( int page, int col, int n )
{
    int c;
    c = ascPWordValue / n;
    ascPWordValue = ascPWordValue % n;
    if( c == 0 )
    {
        if( ascPZeroVisible ) c = '0'; else c = ' ';
    }
    else
    {
        ascPZeroVisible = true;
        c = c + '0';
    }
    return ascPrintC( page, col, c );
}

int ascPrintNumber5( int page, int col, int w )
{
    ascPZeroVisible = false;
    ascPWordValue = w;
    col = ascPrintDigitWord( page, col, 10000 );
    col = ascPrintDigitWord( page, col, 1000 );
    col = ascPrintDigitWord( page, col, 100 );
    col = ascPrintDigitWord( page, col, 10 );
    col = ascPrintC( page, col, ascPWordValue + '0' );
    return col;
}

void ascPrintScore()
{
    int col;
    col = ascPrintNumber5( 1, 2, ascScore );
    ascPrintC( 1, col, '0' );
}

void ascPrintTime()
{
    ascPrintByteNumber3( 5, 5, ascStageTime );
}

void ascComputeLivesDisplay()
{
    int i;
    ascFrameDirty = true;
    if( ascRemainCount <= 1 )
    {
        ascLivesMode = 0;
        return;
    }
    i = ascRemainCount - 1;
    if( i > 2 )
    {
        ascLivesMode = 2;
        ascLivesDigit = i + '0';
        return;
    }
    ascLivesMode = 1;
    ascLivesIconCount = i;
}

void ascPrintStatus()
{
    char sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    char sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    char sTime[4] = { 'T', 'I', 'M', 'E' };

    ascPrintS( 0, 0, sScore, 5 );
    ascPrintS( 3, 0, sStage, 5 );
    ascPrintByteNumber2( 3, 6, ascCurrentStage + 1 );
    ascPrintS( 5, 0, sTime, 4 );

    ascComputeLivesDisplay();

    ascPrintScore();
    ascPrintTime();
}

void ascAddScore( int pts )
{
    ascScore = ascScore + pts;
    if( ascScore > ascHiScore )
      ascHiScore = ascScore;
    ascPrintScore();
}

void ascClearOverlays()
{
    int i;
    for( i = 0; i < ASC_MAX_OVERLAYS; i = i + 1 )
      ascOverlayLen[ i ] = 0;
    ascFrameDirty = true;
}

void ascBeginOverlay( int slot, char* s, int len, int page, int col )
{
    int i;
    ascOverlayLen[ slot ] = len;
    ascOverlayPage[ slot ] = page;
    ascOverlayCol[ slot ] = col;
    for( i = 0; i < len; i = i + 1 )
      ascOverlayTextTable[ slot ][ i ] = s[ i ];
    ascFrameDirty = true;
}

void ascPrintGameOver()
{
    char s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    ascBeginOverlay( 0, s, 9, 4, 8 );
}

void ascPrintTimeUp()
{
    char s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    ascBeginOverlay( 0, s, 7, 4, 9 );
}

// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

// Draws Man's own sprite, handling the PowerTime blink (hidden every other
// tick while invincible) and the jump-height 1-row-up display offset -
// ported using a local displayY rather than upstream's own mutate-then-
// restore of Man._.y, avoiding any risk of a stray early return leaving Y
// corrupted (a deliberate, behaviorally-identical simplification).
void ascShowMan()
{
    int pattern, seq, displayY;

    if( ascMan.pattern != ASC_CHAR_MAN_CLIMB )
    {
        if( ascMan.dx == 0 )
          pattern = ascMan.pattern;
        else
        {
            seq = ascMan.status & ASC_ACTOR_SEQMASK;
            if( seq == 3 ) seq = 1;
            pattern = ascMan.pattern + ASC_MAN_WALK_OFFSET + ( seq << 2 );
        }
    }
    else
    {
        seq = ascMan.status & 1;
        pattern = ascMan.pattern + ( seq << 2 );
    }

    if( ascPowerTime == 0 || ( ascPowerTime & 0x02 ) == 0 )
    {
        displayY = ascMan.y;
        if( ( ascMan.status & ASC_MAN_JUMP ) != 0 )
          displayY = displayY - 1;
        ascShowSpriteXY( ascMan.x, displayY, ascMan.sprite, pattern );
    }
    else
      ascHideSprite( ascMan.sprite );
}

void ascInitMan()
{
    int height;
    height = ascStageHeight[ ascStageIndex ];
    ascMan.x = 0;
    ascMan.y = ( height << 2 ) + ASC_OVERHEAD;
    ascMan.sprite = ASC_SPRITE_MAN;
    ascMan.status = ASC_ACTOR_LIVE;
    ascMan.dx = 0;
    ascMan.dy = 0;
    ascMan.pattern = ASC_CHAR_MAN_RIGHT;
    ascPowerTime = 0;
    ascMinY = ascMan.y;
    ascOldLeft = false; ascOldRight = false; ascOldUp = false; ascOldDown = false;
    ascShowMan();
}

// pActor->status's own Item_1Up/Item_Power bits deliberately share a
// numeric value with Man_Jump (see ascCanMove's own comment) - HitMan
// itself doesn't touch that flag, so no interaction here, just noted for
// context. The shared upstream `get:` label (reached from both the 1-up
// and Power branches) is reproduced as a duplicated inline block rather
// than a goto - see file header comment.
void ascHitMan( AscActor* pActor )
{
    if( ascIsNearXY( pActor->x, pActor->y, ascMan.x, ascMan.y ) )
    {
        if( ( pActor->status & ASC_ITEM_1UP ) != 0 )
        {
            if( ascRemainCount < 10 )
            {
                ascRemainCount = ascRemainCount + 1;
                ascPrintStatus();
                ascStartSeq( 0, ASC_MELODY_ITEM );
                pActor->status = pActor->status & ~ASC_ACTOR_LIVE;
                ascHideSprite( pActor->sprite );
            }
            // else: RemainCount already at the cap - upstream leaves the
            // item un-consumed (no sound, no clear) rather than wasting it;
            // preserved exactly, see file header comment.
        }
        else if( ( pActor->status & ASC_ITEM_POWER ) != 0 )
        {
            ascPowerTime = 100 - ascCurrentStage;
            ascAddScore( 10 );
            ascStartSeq( 0, ASC_MELODY_ITEM );
            pActor->status = pActor->status & ~ASC_ACTOR_LIVE;
            ascHideSprite( pActor->sprite );
        }
        else if( ascPowerTime == 0 )
        {
            ascMan.status = ascMan.status & ~ASC_ACTOR_LIVE;
        }
    }
}

// TestMoveX/TestMoveY take the already-decoded direction bools directly
// (isLeftPressed() etc, or the "old"/sticky direction bools on a retry)
// rather than upstream's own combined Keys_DirX/Keys_DirY bitmask -
// behaviorally identical, matching Cracky's own established approach of
// bypassing ScanKeys()'s raw bitmask entirely.
bool ascTestMoveX( bool left, bool right )
{
    int dx;
    dx = 0;
    if( ascIsOnFloor( ascMan.y ) || ascGetCellType( ascMan.x, ascMan.y + 2 ) != ASC_CELLTYPE_LADDER )
    {
        if( left && ascCanMove( &ascMan, -1 ) )
        {
            dx = -1;
            ascMan.pattern = ASC_CHAR_MAN_LEFT;
        }
        else if( right && ascCanMove( &ascMan, 1 ) )
        {
            dx = 1;
            ascMan.pattern = ASC_CHAR_MAN_RIGHT;
        }
    }
    ascMan.dx = dx;
    return ascMan.dx != 0;
}

// The `up:`/`down:` shared-tail gotos (each reached from two branches
// upstream) are reproduced as duplicated inline blocks - see file header
// comment.
bool ascTestMoveY( bool up, bool down )
{
    int dy;
    if( ( ascMan.status & ( ASC_MAN_FALL | ASC_MAN_JUMP ) ) == 0 )
    {
        dy = 0;
        if( ( ascMan.status & ASC_MAN_JUMP ) == 0 )
        {
            if( ( ascMan.x & 1 ) == 0 )
            {
                if( up )
                {
                    if( ascIsOnFloor( ascMan.y ) )
                    {
                        if( ascGetCellType( ascMan.x, ascMan.y + 2 ) == ASC_CELLTYPE_LADDER )
                        {
                            dy = -1;
                            ascMan.dx = 0;
                            ascMan.pattern = ASC_CHAR_MAN_CLIMB;
                        }
                    }
                    else
                    {
                        if( ascGetCellType( ascMan.x, ascMan.y + 2 ) == ASC_CELLTYPE_LADDER ||
                            ascGetCellType( ascMan.x, ascMan.y + ASC_FLOOR_HEIGHT ) == ASC_CELLTYPE_LADDER )
                        {
                            dy = -1;
                            ascMan.dx = 0;
                            ascMan.pattern = ASC_CHAR_MAN_CLIMB;
                        }
                    }
                }
                else if( down )
                {
                    if( ascIsOnFloor( ascMan.y ) )
                    {
                        if( ascGetCellType( ascMan.x, ascMan.y + ASC_FLOOR_HEIGHT ) == ASC_CELLTYPE_LADDER )
                        {
                            dy = 1;
                            ascMan.dx = 0;
                            ascMan.pattern = ASC_CHAR_MAN_CLIMB;
                        }
                    }
                    else
                    {
                        if( ascGetCellType( ascMan.x, ascMan.y + 2 ) == ASC_CELLTYPE_LADDER )
                        {
                            dy = 1;
                            ascMan.dx = 0;
                            ascMan.pattern = ASC_CHAR_MAN_CLIMB;
                        }
                    }
                }
            }
        }
        ascMan.dy = dy;
    }
    return ascMan.dy != 0;
}

// Upstream's real `byte newTopY` wraps to a huge positive value whenever the
// true (signed) result would be negative (reachable once Man climbs within
// 12 rows of world-Y 0, which happens on every stage well before the win
// condition at Man.y<2) - since that wrapped value is always >= topYRange,
// upstream's own `newTopY < topYRange` check REJECTS the update in that
// case, freezing topY at its last valid (non-negative) value for the rest
// of the climb to the top. A genuine signed int has no such wraparound, so
// the direct translation of this comparison alone (without an explicit
// `>= 0` guard) let ascTopY keep decreasing arbitrarily far into negative
// territory instead of freezing - verified via a byte-accurate Python
// simulation before fixing: for the tallest (10-floor) stage, upstream
// freezes topY at 0 from y=11 down to the win condition, while the
// unguarded signed version drifted to -11 by y=1. A negative ascTopY
// corrupts ascMapToBackground()'s own row/yPos bookkeeping (ascTopRow stays
// -1, but ascYMod - and therefore the render loop's starting `yPos` - keeps
// growing away from 0), so most of the visible window stops being
// refreshed at all near the top of every stage, leaving stale background
// content on screen exactly when the player is closest to winning. Fixed
// with an explicit `newTopY >= 0` guard, reproducing the wraparound-reject
// outcome exactly.
void ascScroll()
{
    int y, newTopY;
    y = ascMan.y;
    newTopY = y - ( ASC_VVRAM_HEIGHT / 2 ) - ASC_FLOOR_HEIGHT;
    if( newTopY != ascTopY && newTopY >= 0 && newTopY < ascTopYRange )
    {
        ascTopY = newTopY;
        ascDrawBackground();
    }
}

void ascFallMan()
{
    if( ( ascMan.status & ASC_MAN_JUMP ) == 0 )
    {
        if( ascIsOnFloor( ascMan.y ) )
        {
            if( ( ascMan.x & 1 ) == 0 && ascGetCellType( ascMan.x, ascMan.y ) == ASC_CELLTYPE_HOLE )
            {
                ascMan.status = ascMan.status | ASC_MAN_FALL;
                ascMan.dy = 1;
            }
            else
            {
                ascMan.status = ascMan.status & ~ASC_MAN_FALL;
                ascMan.dy = 0;
            }
        }
        else if( ascGetCellType( ascMan.x, ascMan.y + 2 ) != ASC_CELLTYPE_LADDER )
        {
            ascMan.status = ascMan.status | ASC_MAN_FALL;
            ascMan.dy = 1;
        }
        else
        {
            ascMan.status = ascMan.status & ~ASC_MAN_FALL;
            ascMan.dy = 0;
        }
    }
    if( ( ascMan.status & ASC_MAN_FALL ) != 0 )
    {
        ascMoveActorYOnly( &ascMan );
        ascScroll();
    }
}

// The single upstream `goto moved;` (skipping the whole Y-axis test only
// when the FRESH X-axis test succeeds, not when only the stale/"old" retry
// succeeds - a real, subtle distinction traced carefully from the exact
// goto placement) is reproduced with a plain `skipRest` flag.
void ascMoveMan()
{
    bool left, right, up, down, skipRest;

    if( ( ascMan.status & ASC_ACTOR_LIVE ) == 0 ) return;

    left = isLeftPressed();
    right = isRightPressed();
    up = isUpPressed();
    down = isDownPressed();
    skipRest = false;

    if( left || right || up || down )
    {
        if( ascTestMoveX( left, right ) )
        {
            ascOldLeft = left; ascOldRight = right;
            ascOldUp = false; ascOldDown = false;
            skipRest = true;
        }
        else
        {
            if( ascOldLeft || ascOldRight )
            {
                if( ascTestMoveX( ascOldLeft, ascOldRight ) )
                {
                    ascOldUp = false; ascOldDown = false;
                }
            }
        }
        if( !skipRest )
        {
            if( ascTestMoveY( up, down ) )
            {
                ascOldUp = up; ascOldDown = down;
                ascOldLeft = false; ascOldRight = false;
            }
            else
            {
                if( ascOldUp || ascOldDown )
                {
                    if( ascTestMoveY( ascOldUp, ascOldDown ) )
                    {
                        ascOldLeft = false;
                        ascOldRight = false;
                    }
                }
            }
        }
    }
    else
    {
        ascMan.dx = 0;
        ascMan.dy = 0;
    }

    if( ( ascMan.status & ( ASC_MAN_FALL | ASC_MAN_JUMP ) ) == 0 )
    {
        if( isFirePressed() && ascIsOnFloor( ascMan.y ) && ascMan.dy == 0 )
        {
            ascMan.status = ascMan.status | ASC_MAN_JUMP;
            ascJumpSeq = 0;
        }
    }
    ascMoveActor( &ascMan );
    if( ascMan.dy != 0 )
      ascScroll();
    else if( ( ascMan.status & ASC_MAN_JUMP ) != 0 )
    {
        ascJumpSeq = ascJumpSeq + 1;
        if( ascJumpSeq < ASC_COORD_RATE * 2 + 1 )
          ascHitUnderBlock();
        else
        {
            ascHitOverBlock();
            ascMan.status = ascMan.status & ~ASC_MAN_JUMP;
        }
    }
    while( ascMinY > ascMan.y )
    {
        ascAddScore( 1 );
        ascMinY = ascMinY - 1;
    }
    ascShowMan();
}

// -----------------------------------------------------------------------------
//   Fire.cpp - falling hazards spawned from the top of the current scroll
//   window; some carry a 1-up or Power item instead of just being a plain
//   hazard. Two independently-layered timers, both ported faithfully: the
//   outer ascFireCount (Main.cpp's own FireInterval, gates how often
//   ascStartFire() is even CALLED) and the inner ascFireSpawnCount/
//   ascFireSpawnRate (Fire.cpp's own Interval, gates whether a call to
//   ascStartFire() actually spawns anything) - see file header comment.
// -----------------------------------------------------------------------------

void ascInitFires()
{
    int i;
    for( i = 0; i < ASC_MAX_FIRE_COUNT; i = i + 1 )
    {
        ascFires[ i ].status = 0;
        ascFires[ i ].sprite = ASC_SPRITE_FIRE + i;
    }
    ascFireSpawnCount = -ASC_FIRE_SPAWN_INTERVAL;
    ascFireSpawnRate = 2 + ( ascStageHeight[ ascStageIndex ] >> 1 );
}

void ascStartFire()
{
    int y, x, i;
    ascFireSpawnCount = ascFireSpawnCount + ascFireSpawnRate;
    if( ascFireSpawnCount > 0 )
    {
        y = ascTopY & 0x03;
        x = ( ascRnd() & 0x0f ) << 1;
        if( x >= ASC_COLUMN_COUNT )
          x = x - ASC_COLUMN_COUNT;
        if( ascGetCellType( x, y ) != ASC_CELLTYPE_WALL &&
            ascGetCellType( x, y + ASC_FLOOR_HEIGHT ) != ASC_CELLTYPE_LADDER )
        {
            for( i = 0; i < ASC_MAX_FIRE_COUNT; i = i + 1 )
            {
                if( ( ascFires[ i ].status & ASC_ACTOR_LIVE ) == 0 )
                {
                    ascFires[ i ].x = x;
                    ascFires[ i ].y = y;
                    ascFires[ i ].dx = 0;
                    ascFires[ i ].dy = 1;
                    ascFires[ i ].pattern = ASC_CHAR_FIRE;
                    ascFires[ i ].status = ASC_ACTOR_LIVE;
                    if( ( ascRnd() & 0x07 ) == 0 )
                      ascFires[ i ].status = ascFires[ i ].status | ASC_ITEM_POWER;
                    else if( ( ascRnd() & 0x0f ) == 0 )
                      ascFires[ i ].status = ascFires[ i ].status | ASC_ITEM_1UP;
                    ascShowEnemy( &ascFires[ i ] );
                    ascFireSpawnCount = ascFireSpawnCount - ASC_FIRE_SPAWN_INTERVAL;
                    return;
                }
            }
        }
    }
}

void ascMoveFires()
{
    int i;
    for( i = 0; i < ASC_MAX_FIRE_COUNT; i = i + 1 )
    {
        if( ( ascFires[ i ].status & ASC_ACTOR_LIVE ) != 0 )
        {
            ascHitMan( &ascFires[ i ] );
            if( ascIsOnFloor( ascFires[ i ].y ) )
            {
                if( ( ascFires[ i ].x & 1 ) == 0 &&
                    ( ascGetCellType( ascFires[ i ].x, ascFires[ i ].y ) == ASC_CELLTYPE_HOLE ||
                      ascGetCellType( ascFires[ i ].x, ascFires[ i ].y + ASC_FLOOR_HEIGHT ) == ASC_CELLTYPE_LADDER ) )
                {
                    ascFires[ i ].dx = 0;
                    ascFires[ i ].dy = 1;
                }
                else
                {
                    ascFires[ i ].dy = 0;
                    if( ascFires[ i ].dx == 0 )
                      ascFires[ i ].dx = ascSign( ascMan.x, ascFires[ i ].x );
                    if( ascFires[ i ].dx == 0 )
                      ascFires[ i ].dx = ascSign( ASC_STAGE_WIDTH / 2, ascFires[ i ].x );
                    if( ascFires[ i ].dx < 0 )
                    {
                        if( !ascCanMove( &ascFires[ i ], -1 ) )
                        {
                            ascFires[ i ].dx = 1;
                            ascFires[ i ].pattern = ASC_CHAR_FIRE_RIGHT;
                        }
                        else
                          ascFires[ i ].pattern = ASC_CHAR_FIRE_LEFT;
                    }
                    else if( ascFires[ i ].dx > 0 )
                    {
                        if( !ascCanMove( &ascFires[ i ], 1 ) )
                        {
                            ascFires[ i ].dx = -1;
                            ascFires[ i ].pattern = ASC_CHAR_FIRE_LEFT;
                        }
                        else
                          ascFires[ i ].pattern = ASC_CHAR_FIRE_RIGHT;
                    }
                }
            }
            ascMoveActor( &ascFires[ i ] );
            if( ascFires[ i ].y > ascTopY && ascFires[ i ].y - ascTopY >= ASC_VVRAM_HEIGHT )
            {
                ascFires[ i ].status = 0;
                ascHideSprite( ascFires[ i ].sprite );
            }
            else
            {
                if( ( ascFires[ i ].status & ASC_ITEM_1UP ) != 0 )
                  ascShowSpriteXY( ascFires[ i ].x, ascFires[ i ].y, ascFires[ i ].sprite, ASC_CHAR_ITEM_1UP );
                else if( ( ascFires[ i ].status & ASC_ITEM_POWER ) != 0 )
                  ascShowSpriteXY( ascFires[ i ].x, ascFires[ i ].y, ascFires[ i ].sprite, ASC_CHAR_ITEM_POWER );
                else
                  ascShowEnemy( &ascFires[ i ] );
                ascHitMan( &ascFires[ i ] );
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Monster.cpp - ground-patrolling enemies, confined to floors (never fall/
//   climb) and reversing at walls, holes, or a raised Block (via ascCanMove).
// -----------------------------------------------------------------------------

void ascInitMonsters()
{
    int i, count, b, sprite;
    count = ascStageMonsterCount[ ascStageIndex ];
    sprite = ASC_SPRITE_MONSTER;
    for( i = 0; i < count; i = i + 1 )
    {
        b = ascStageEnemiesTable[ ascStageIndex ][ i ];
        ascMonsters[ i ].x = ascToX( b );
        ascMonsters[ i ].y = ascToY( b );
        ascMonsters[ i ].dx = -1;
        ascMonsters[ i ].dy = 0;
        ascMonsters[ i ].pattern = ASC_CHAR_MONSTER;
        ascMonsters[ i ].status = ASC_ACTOR_LIVE;
        ascMonsters[ i ].sprite = sprite;
        ascShowEnemy( &ascMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = count; i < ASC_MAX_MONSTER_COUNT; i = i + 1 )
    {
        ascMonsters[ i ].status = 0;
        ascHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void ascMoveMonsters()
{
    int i;
    for( i = 0; i < ASC_MAX_MONSTER_COUNT; i = i + 1 )
    {
        if( ( ascMonsters[ i ].status & ASC_ACTOR_LIVE ) != 0 )
        {
            ascHitMan( &ascMonsters[ i ] );
            if( ascIsOnFloor( ascMonsters[ i ].y ) )
            {
                ascMonsters[ i ].dy = 0;
                if( ascMonsters[ i ].dx < 0 )
                {
                    if( !ascCanMove( &ascMonsters[ i ], -1 ) ||
                        ( ( ascMonsters[ i ].x & 1 ) == 0 &&
                          ascGetCellType( ascMonsters[ i ].x - 2, ascMonsters[ i ].y ) == ASC_CELLTYPE_HOLE ) )
                    {
                        ascMonsters[ i ].dx = 1;
                        ascMonsters[ i ].pattern = ASC_CHAR_MONSTER_RIGHT;
                    }
                    else
                      ascMonsters[ i ].pattern = ASC_CHAR_MONSTER_LEFT;
                }
                else if( ascMonsters[ i ].dx > 0 )
                {
                    if( !ascCanMove( &ascMonsters[ i ], 1 ) ||
                        ( ( ascMonsters[ i ].x & 1 ) == 0 &&
                          ascGetCellType( ascMonsters[ i ].x + 2, ascMonsters[ i ].y ) == ASC_CELLTYPE_HOLE ) )
                    {
                        ascMonsters[ i ].dx = -1;
                        ascMonsters[ i ].pattern = ASC_CHAR_MONSTER_LEFT;
                    }
                    else
                      ascMonsters[ i ].pattern = ASC_CHAR_MONSTER_RIGHT;
                }
            }
            ascMoveActor( &ascMonsters[ i ] );
            ascShowEnemy( &ascMonsters[ i ] );
            ascHitMan( &ascMonsters[ i ] );
        }
    }
}

// -----------------------------------------------------------------------------
//   Stage.cpp's InitTrying() - resets everything for one stage attempt
//   (called on a fresh stage AND on a same-stage retry after death, unlike
//   ascInitStage() which only runs on a genuinely new stage - matches
//   upstream's own `stage:`/`try_:` label split exactly, see file header).
// -----------------------------------------------------------------------------

void ascBeginTrying()
{
    int i, j;

    ascFrameDirty = true;
    ascStageTime = 100;
    i = ascStageHeight[ ascStageIndex ];
    while( i != 0 )
    {
        ascStageTime = ascStageTime + ascTimeRate;
        i = i - 1;
    }

    ascHideAllSprites();
    // Clear the status-text grid + overlays before redrawing - without
    // this, whatever a previous screen (the title, or a stale TIME UP/GAME
    // OVER overlay) last left in cells PrintStatus() doesn't itself
    // overwrite would silently persist, exactly the class of bug Cracky's
    // own header comment documents finding and fixing here first.
    for( i = 0; i < 8; i = i + 1 )
    {
        for( j = 0; j < 8; j = j + 1 )
          ascStatusChar[ i ][ j ] = 0;
    }
    ascClearOverlays();

    ascPrintStatus();

    ascTopY = ( ascStageHeight[ ascStageIndex ] << 2 ) + ASC_FLOOR_HEIGHT - ASC_VVRAM_HEIGHT;
    ascTopYRange = ascTopY + 1;
    ascRndIndex = 0;

    ascInitMan();
    ascInitFires();
    ascInitMonsters();
    ascInitBlocks();

    ascDrawBackground();
    ascDrawAll();
}

// Main.cpp's own `try_:` label body (InitTrying() + the Clock/monsterNum/
// fireCount/timeDenom resets + kicking off the blocking Sound_Start()) -
// shared by both a fresh stage (after ascInitStage()) and a same-stage
// retry (called alone).
void ascBeginNewTry()
{
    ascBeginTrying();
    ascClock = 0;
    ascMonsterNum = 0;
    ascFireCount = ASC_FIRE_CALL_INTERVAL;
    ascTimeDenom = ASC_MAX_TIME_DENOM;
    ascStartSeq( 1, ASC_MELODY_START );
    ascState = ASC_STATE_START_JINGLE;
}

// -----------------------------------------------------------------------------
//   Rendering - reproduces VVramToVram()'s own SendUL() nibble-interleaving
//   exactly (see file header comment for the full derivation, already
//   proven once in this project's own Cracky port). No hardware-orientation
//   transform of any kind - drawn directly at each byte's own (col,page).
// -----------------------------------------------------------------------------

int ascComposeGlyphByte( int topGlyph, int bottomGlyph, int sub )
{
    int topByte, bottomByte;
    if( sub == 0 )
    {
        topByte = ascCharPattern[ topGlyph * 2 + 0 ];
        bottomByte = ascCharPattern[ bottomGlyph * 2 + 0 ];
        return ( topByte & 0x0f ) | ( bottomByte << 4 );
    }
    if( sub == 1 )
    {
        topByte = ascCharPattern[ topGlyph * 2 + 0 ];
        bottomByte = ascCharPattern[ bottomGlyph * 2 + 0 ];
        return ( topByte >> 4 ) | ( bottomByte & 0xf0 );
    }
    if( sub == 2 )
    {
        topByte = ascCharPattern[ topGlyph * 2 + 1 ];
        bottomByte = ascCharPattern[ bottomGlyph * 2 + 1 ];
        return ( topByte & 0x0f ) | ( bottomByte << 4 );
    }
    topByte = ascCharPattern[ topGlyph * 2 + 1 ];
    bottomByte = ascCharPattern[ bottomGlyph * 2 + 1 ];
    return ( topByte >> 4 ) | ( bottomByte & 0xf0 );
}

// A CPU-load fix, not a fidelity change: this is the single hottest
// per-pixel path in the whole render loop (called for up to 96 of every
// 128 columns, on every one of 8 pages, every real frame - this game has
// no per-tick throttle on rendering itself, only on PLAYING-state game
// logic). Confirmed via the WebGL perf overlay that the original
// unoptimized ascRender() pegs CPU at a sustained 100% even on the
// completely static title screen, with a real, visible consequence: a
// truncated frame (this project's own well-documented "CPU budget
// exceeded, execution stops mid-instruction-stream" signature) was
// caught losing the trailing "E" of "CONTINUE" and the entire "INUFUTO
// 2026" credit line (page 7, drawn last) on individual captured frames.
// Originally this function called the separate, more general
// ascComposeGlyphByte() (itself an extra function-call layer, doing the
// exact same 4-way sub-branch every time) - inlined directly here since
// this is by far its most frequent call site; ascComposeGlyphByte() is
// kept as its own function for ascComposeLivesByte()'s own much lower-
// frequency use (at most 16 columns/page, only page 7).
int ascComposeMapByte( int col, int page )
{
    int mapX, sub, top, bottom, topByte, bottomByte;
    mapX = col / 4;
    sub = col % 4;
    top = ascVVram[ page * 2 ][ mapX ];
    bottom = ascVVram[ page * 2 + 1 ][ mapX ];
    if( sub == 0 )
    {
        topByte = ascCharPattern[ top * 2 + 0 ];
        bottomByte = ascCharPattern[ bottom * 2 + 0 ];
        return ( topByte & 0x0f ) | ( bottomByte << 4 );
    }
    if( sub == 1 )
    {
        topByte = ascCharPattern[ top * 2 + 0 ];
        bottomByte = ascCharPattern[ bottom * 2 + 0 ];
        return ( topByte >> 4 ) | ( bottomByte & 0xf0 );
    }
    if( sub == 2 )
    {
        topByte = ascCharPattern[ top * 2 + 1 ];
        bottomByte = ascCharPattern[ bottom * 2 + 1 ];
        return ( topByte & 0x0f ) | ( bottomByte << 4 );
    }
    topByte = ascCharPattern[ top * 2 + 1 ];
    bottomByte = ascCharPattern[ bottom * 2 + 1 ];
    return ( topByte >> 4 ) | ( bottomByte & 0xf0 );
}

// Composites the "remaining lives" icon(s)/digit directly from
// ascLivesMode/ascLivesIconCount/ascLivesDigit (see ascComputeLivesDisplay)
// - bypasses the ascii-only status grid entirely for this one sub-region,
// see file header comment for why.
int ascComposeLivesByte( int relCol )
{
    int off, topGlyph, bottomGlyph, sub, iconIndex;

    if( ascLivesMode == 0 ) return 0;

    if( ascLivesMode == 1 )
    {
        iconIndex = relCol / 8;
        if( iconIndex >= ascLivesIconCount ) return 0;
        off = relCol % 8;
    }
    else
    {
        if( relCol < 8 )
          off = relCol;
        else if( relCol < 12 )
          return ascAsciiPattern[ 0 * 4 + ( relCol - 8 ) ];
        else if( relCol < 16 )
          return ascAsciiPattern[ ascAsciiIndex( ascLivesDigit ) * 4 + ( relCol - 12 ) ];
        else
          return 0;
    }

    if( off < 4 )
    {
        topGlyph = ASC_CHAR_MAN;
        bottomGlyph = ASC_CHAR_MAN + 2;
        sub = off;
    }
    else
    {
        topGlyph = ASC_CHAR_MAN + 1;
        bottomGlyph = ASC_CHAR_MAN + 3;
        sub = off - 4;
    }
    return ascComposeGlyphByte( topGlyph, bottomGlyph, sub );
}

int ascComposeStatusByte( int col, int page )
{
    int statCol, charCol, sub, c, relCol;
    if( page == 7 )
    {
        relCol = col - 96;
        if( relCol >= 0 && relCol < 16 )
          return ascComposeLivesByte( relCol );
    }
    statCol = col - 96;
    if( statCol < 0 || statCol >= 32 ) return 0;
    charCol = statCol / 4;
    sub = statCol % 4;
    c = ascStatusChar[ page ][ charCol ];
    return ascAsciiPattern[ c * 4 + sub ];
}

// A real, confirmed CPU-load bug, found via the exact same live-testing
// pass that caught the missing "CONTINUE" trailing "E"/missing credit
// line above: this loop originally re-scanned all ASC_MAX_OVERLAYS(6)
// slots for EVERY one of 1024 (page,col) pixels every single frame,
// regardless of how few of those slots could ever actually match a given
// page (at most 2 slots ever share a page in this file's own real usage,
// and their own column ranges never overlap). Fixed by building the list
// of slots active on the CURRENT page once per page (8x/frame) instead of
// once per pixel (1024x/frame) - `pageSlots`/`pageSlotCount` - and only
// checking that short filtered list per column. This is a pure iteration-
// count reduction: the filtered list is built by walking slots 0..5 in
// the same order the original per-pixel loop did, so a column matching
// more than one active slot on the same page (never happens today, but
// not assumed impossible) still resolves to whichever matching slot has
// the highest index, exactly like the original "keep overwriting `value`
// as later slots match" behavior - no rendering-order change, only cost.
// A persistent per-pixel cache backing ascRender()'s own dirty-flag fix -
// see that function's own header comment for the measured CPU numbers.
// Deliberately NOT the "skip the whole draw call, previous frame just
// persists" shape several other games in this project use (e.g. Tiny
// Tris's own attract screen) - this game registers no `onResume` hook in
// menuGameList.c (its own addGame() call passes NULL, and fixing that is
// out of scope for this file alone), so the quit-confirmation dialog has
// no way to force a redraw when it closes. Skipping real draw calls on a
// "nothing changed" frame would leave the dialog's own leftover pixels on
// screen indefinitely after resuming into a state (e.g. the static TITLE
// screen) that might never naturally re-dirty itself again. Instead,
// every real frame still calls md_beginFrame() and issues all 1024
// md_drawColumn() calls unconditionally (cheap and already proven fine at
// this volume by every other game in this project) - only the genuinely
// expensive part, recomputing what each of those 1024 bytes actually IS,
// is skipped when ascFrameDirty is false, reusing the previous pass's own
// cached results instead. This reproduces the exact same visual output as
// the original always-recompute version on every single frame, dialog-
// resume included, just without redoing the expensive compute work when
// nothing could have changed it.
int ascPixelCache[8][128];

void ascRender()
{
    int page, col, value, slotIdx, i, sub;
    bool found;
    int pageSlots[ASC_MAX_OVERLAYS];
    int pageSlotCount;

    md_beginFrame();

    if( ascFrameDirty )
    {
        ascFrameDirty = false;

        for( page = 0; page < 8; page = page + 1 )
        {
            pageSlotCount = 0;
            for( slotIdx = 0; slotIdx < ASC_MAX_OVERLAYS; slotIdx = slotIdx + 1 )
            {
                if( ascOverlayLen[ slotIdx ] > 0 && ascOverlayPage[ slotIdx ] == page )
                {
                    pageSlots[ pageSlotCount ] = slotIdx;
                    pageSlotCount = pageSlotCount + 1;
                }
            }

            for( col = 0; col < 128; col = col + 1 )
            {
                found = false;
                for( i = 0; i < pageSlotCount; i = i + 1 )
                {
                    slotIdx = pageSlots[ i ];
                    if( col >= ascOverlayCol[ slotIdx ] * 4 &&
                        col < ascOverlayCol[ slotIdx ] * 4 + ascOverlayLen[ slotIdx ] * 4 )
                    {
                        sub = ( col - ascOverlayCol[ slotIdx ] * 4 ) % 4;
                        value = ascAsciiPattern[ ascAsciiIndex( ascOverlayTextTable[ slotIdx ][ ( col - ascOverlayCol[ slotIdx ] * 4 ) / 4 ] ) * 4 + sub ];
                        found = true;
                    }
                }
                if( !found )
                {
                    if( col < 96 )
                      value = ascComposeMapByte( col, page );
                    else
                      value = ascComposeStatusByte( col, page );
                }
                ascPixelCache[ page ][ col ] = value;
            }
        }
    }

    for( page = 0; page < 8; page = page + 1 )
      for( col = 0; col < 128; col = col + 1 )
        md_drawColumn( col, page, ascPixelCache[ page ][ col ] );
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void ascBeginTitle()
{
    char sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    char sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    char sMini[4] = { 'M', 'I', 'N', 'I' };
    char sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    char sCursorOn[1] = { '>' };
    char sCursorOff[1] = { ' ' };
    int i, j, ch, subRow, subCol, idx;

    ascFrameDirty = true;
    for( i = 0; i < ASC_VVRAM_HEIGHT; i = i + 1 )
      for( j = 0; j < ASC_VVRAM_WIDTH; j = j + 1 )
        ascVVram[ i ][ j ] = ASC_CHAR_SPACE;

    for( i = 0; i < 8; i = i + 1 )
      for( j = 0; j < 8; j = j + 1 )
        ascStatusChar[ i ][ j ] = 0;

    ascClearOverlays();
    ascHideAllSprites();
    // Real bug in Cracky, same shape, fixed proactively here: without
    // resetting StageTime before PrintStatus(), a game-over reached via
    // enemy contact (not the clock hitting 0) leaves the last real
    // countdown value showing on the title screen's own TIME field.
    ascStageTime = 0;
    ascPrintStatus();

    idx = 0;
    for( ch = 0; ch < 6; ch = ch + 1 )
      for( subRow = 0; subRow < 4; subRow = subRow + 1 )
        for( subCol = 0; subCol < 4; subCol = subCol + 1 )
        {
            ascVVram[ 2 + subRow ][ ch * 4 + subCol ] = ascTitleBytes[ idx ];
            idx = idx + 1;
        }

    ascBeginOverlay( 0, sMini, 4, 3, 19 );
    ascBeginOverlay( 1, sCredit, 12, 7, 12 );
    ascBeginOverlay( 2, sStart, 5, 5, 9 );
    ascBeginOverlay( 3, sContinue, 8, 6, 9 );
    ascBeginOverlay( 4, sCursorOn, 1, 5, 8 );
    ascBeginOverlay( 5, sCursorOff, 1, 6, 8 );

    ascSelection = 0;
    ascPrevLeft = false; ascPrevRight = false; ascPrevUp = false; ascPrevDown = false; ascPrevFire = false;
    ascState = ASC_STATE_TITLE;
}

void ascUpdateTitle()
{
    bool left, right, up, down, fire, justDir, justFire;
    char sOn[1] = { '>' };
    char sOff[1] = { ' ' };

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( left && !ascPrevLeft ) || ( right && !ascPrevRight ) ||
              ( up && !ascPrevUp ) || ( down && !ascPrevDown );
    justFire = fire && !ascPrevFire;
    ascPrevLeft = left; ascPrevRight = right; ascPrevUp = up; ascPrevDown = down; ascPrevFire = fire;

    if( justFire )
    {
        ascPendingContinue = ( ascSelection == 1 );
        ascScore = 0;
        if( !ascPendingContinue )
          ascCurrentStage = 0;
        ascRemainCount = 3;
        ascInitStage();
        ascBeginNewTry();
        ascRender();
        return;
    }
    if( justDir )
    {
        ascSelection = ascSelection ^ 1;
        if( ascSelection == 0 )
        {
            ascBeginOverlay( 4, sOn, 1, 5, 8 );
            ascBeginOverlay( 5, sOff, 1, 6, 8 );
        }
        else
        {
            ascBeginOverlay( 4, sOff, 1, 5, 8 );
            ascBeginOverlay( 5, sOn, 1, 6, 8 );
        }
    }
    ascRender();
}

void ascUpdateStartJingle()
{
    if( !ascSeqPlaying( 1 ) )
    {
        ascStartBgm();
        ascState = ASC_STATE_PLAYING;
    }
    ascRender();
}

void ascBeginLose()
{
    ascStopBgm();
    ascAnimStep = 0;
    ascWaitFrames = 0;
    ascState = ASC_STATE_LOSE_ANIM;
}

void ascUpdateLoseAnim()
{
    int patterns[4] = { ASC_CHAR_MAN_LEFT_STOP, ASC_CHAR_MAN_LOOSE0, ASC_CHAR_MAN_LOOSE1, ASC_CHAR_MAN_LOOSE2 };

    if( ascWaitFrames > 0 )
    {
        ascWaitFrames = ascWaitFrames - 1;
        ascRender();
        return;
    }

    ascShowSpriteXY( ascMan.x, ascMan.y, ascMan.sprite, patterns[ ascAnimStep & 3 ] );
    ascDrawAll();
    ascStartSeq( 0, ASC_MELODY_LOOSE );
    ascAnimStep = ascAnimStep + 1;
    ascWaitFrames = ascNoteFrames( 1 );

    if( ascAnimStep >= 8 )
    {
        ascMan.status = ascMan.status & ~ASC_ACTOR_LIVE;
        ascRemainCount = ascRemainCount - 1;
        if( ascRemainCount > 0 )
        {
            ascBeginNewTry();
            ascDrawAll();
        }
        else
        {
            ascPrintGameOver();
            ascStartSeq( 1, ASC_MELODY_GAMEOVER );
            ascState = ASC_STATE_GAMEOVER_JINGLE;
        }
    }
    ascRender();
}

void ascUpdateGameOverJingle()
{
    if( !ascSeqPlaying( 1 ) )
      ascBeginTitle();
    else
      ascRender();
}

void ascBeginClearWait()
{
    ascStopBgm();
    ascWaitFrames = 5;
    ascState = ASC_STATE_CLEAR_WAIT;
}

void ascUpdateClearWait()
{
    if( ascWaitFrames > 0 )
    {
        ascWaitFrames = ascWaitFrames - 1;
        ascRender();
        return;
    }
    ascStartSeq( 1, ASC_MELODY_CLEAR );
    ascState = ASC_STATE_CLEAR_JINGLE;
    ascRender();
}

void ascUpdateClearJingle()
{
    if( !ascSeqPlaying( 1 ) )
    {
        ascWaitFrames = 0;
        ascState = ASC_STATE_BONUS_TALLY;
    }
    ascRender();
}

void ascUpdateBonusTally()
{
    if( ascWaitFrames > 0 )
    {
        ascWaitFrames = ascWaitFrames - 1;
        ascRender();
        return;
    }

    if( ascStageTime >= ASC_BONUS_RATE )
    {
        ascAddScore( 5 );
        ascStageTime = ascStageTime - ASC_BONUS_RATE;
        ascPrintTime();
        ascStartSeq( 0, ASC_MELODY_BEEP );
        ascWaitFrames = ascNoteFrames( 1 );
        ascRender();
        return;
    }

    ascStageTime = 0;
    ascPrintStatus();
    ascCurrentStage = ascCurrentStage + 1;
    ascInitStage();
    ascBeginNewTry();
    ascDrawAll();
    ascRender();
}

// Main.cpp's own `do{...}while(Man._.y>=2);` loop body - both the death-
// mid-loop `++Clock; if(dead) goto lose;` check AND the loop's own
// `while(Man._.y>=2)` win check are only ever meaningful right after the
// `Clock&3==0` movement sub-tick (nothing else can change Man's Y or
// aliveness) - so both are checked only there, breaking out of the
// remaining sub-ticks of the current batch exactly the way upstream's own
// goto/loop-exit does when either condition fires mid-batch.
void ascUpdatePlaying()
{
    int subTick, result;

    ascTickCounter = ascTickCounter + 1;
    if( ascTickCounter < ASC_TICK_DIVISOR )
    {
        ascRender();
        return;
    }
    ascTickCounter = 0;

    result = 0;

    for( subTick = 0; subTick < 4; subTick = subTick + 1 )
    {
        ascFireCount = ascFireCount - 1;
        if( ascFireCount == 0 )
        {
            ascStartFire();
            ascFireCount = ASC_FIRE_CALL_INTERVAL;
        }
        if( ( ascClock & 1 ) == 0 )
        {
            if( ascPowerTime > 0 )
              ascPowerTime = ascPowerTime - 1;
        }
        if( ( ascClock & 3 ) == 0 )
        {
            ascFallMan();
            ascMoveMan();
            ascMoveFires();
            if( ascMonsterNum >= 0 )
            {
                ascMoveMonsters();
                ascMonsterNum = ascMonsterNum - 10;
            }
            ascMonsterNum = ascMonsterNum + 6;

            ascTimeDenom = ascTimeDenom - 1;
            if( ascTimeDenom == 0 )
            {
                ascStageTime = ascStageTime - 1;
                ascTimeDenom = ASC_MAX_TIME_DENOM;
                ascPrintTime();
                if( ascStageTime == 0 )
                {
                    ascPrintTimeUp();
                    ascClock = ascClock + 1;
                    result = 2;
                    break;
                }
            }

            ascDrawAll();
            ascClock = ascClock + 1;

            if( ( ascMan.status & ASC_ACTOR_LIVE ) == 0 )
            {
                result = 1;
                break;
            }
            if( ascMan.y < 2 )
            {
                result = 3;
                break;
            }
        }
        else
          ascClock = ascClock + 1;
    }

    if( result == 2 || result == 1 )
    {
        ascRender();
        ascBeginLose();
        return;
    }
    if( result == 3 )
    {
        ascPowerTime = 0;
        ascShowMan();
        ascDrawAll();
        ascRender();
        ascBeginClearWait();
        return;
    }

    ascRender();
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameAscend_init()
{
    int i;

    ascHiScore = 0;
    ascScore = 0;
    ascCurrentStage = 0;
    ascRemainCount = 3;
    ascStageTime = 0;
    ascRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        ascSeqActive[ i ] = 0;
        ascSeqMelody[ i ] = ASC_MELODY_NONE;
    }
    ascTickCounter = 0;

    ascBeginTitle();
}

void gameAscend_update()
{
    ascAdvanceSound();

    if( ascState == ASC_STATE_TITLE )
      ascUpdateTitle();
    else if( ascState == ASC_STATE_START_JINGLE )
      ascUpdateStartJingle();
    else if( ascState == ASC_STATE_PLAYING )
      ascUpdatePlaying();
    else if( ascState == ASC_STATE_LOSE_ANIM )
      ascUpdateLoseAnim();
    else if( ascState == ASC_STATE_GAMEOVER_JINGLE )
      ascUpdateGameOverJingle();
    else if( ascState == ASC_STATE_CLEAR_WAIT )
      ascUpdateClearWait();
    else if( ascState == ASC_STATE_CLEAR_JINGLE )
      ascUpdateClearJingle();
    else if( ascState == ASC_STATE_BONUS_TALLY )
      ascUpdateBonusTally();
}
