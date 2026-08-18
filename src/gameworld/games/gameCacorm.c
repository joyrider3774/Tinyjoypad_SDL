// =============================================================================
// CACORM mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_cacorm`, same author/engine
// lineage as this project's own already-shipped gameCracky.c) - a Qix-style
// area-capture game: walk around the border of a 12x7-cell maze (LEFT/RIGHT/
// UP/DOWN), draw a line out into open space and back to any already-drawn
// line to close off a region; any stars ("items") enclosed inside the newly-
// closed region are captured for points. Clear every item to advance to the
// next of 8 hand-authored stages. Touching a chasing monster costs a life
// (3 lives per game); an occasional "Increaser" bonus pickup grants a spare
// life. No hi-score is tracked at all - confirmed directly from upstream's
// own `Main.cpp` (`word Score; // word HiScore;` - HiScore is commented out,
// genuinely unused anywhere in the source), unlike gameCracky.c's own game,
// so this port has no equivalent to `crkHiScore`.
//
// Same real 60Hz SysTick-based frame limiter as gameCracky.c
// (`Timer.cpp`'s own `kTimerHz=60`), and the exact same `Clock&3`/`Clock&1`/
// `WaitTimer(4)` main-loop timing shape - working out the real elapsed time
// between logic ticks (2 of every 4 `Clock` increments call `WaitTimer(4)`,
// i.e. 8 real 60Hz ticks = ~133ms per logic tick) lands on the same
// `CAC_TICK_DIVISOR=8` gameCracky.c already uses for its own, differently-
// timed main loop - not assumed identical, independently derived and only
// coincidentally the same value (both games share the same "cate" engine
// author/template, so a shared real-time constant isn't a total surprise).
//
// **No hardware display-orientation transform is applied, matching
// gameCracky.c's own hard-won conclusion exactly** - `cacComposeRawByte()`
// is drawn directly at its own `(col,page)`, no mirroring, no per-bit
// reversal. See gameCracky.c's own header comment for the full story of why
// an SSD1306 `SegRemap`/`ComScanDec` pair does NOT imply a software
// transform is needed here.
//
// **Rendering reuses gameCracky.c's own two-level VVram tile system
// verbatim** - `VVram` is a 24x16 logical glyph-index grid (`cacVVram`,
// same dimensions as gameCracky.c's own `crkVVram`); `cacComposeRawByte()`
// packs each pair of vertically-stacked VVram cells into 4 real output
// bytes via the identical 2-byte-per-glyph `CharPattern` lookup and nibble-
// interleaving gameCracky.c's own `crkComposeRawByte()` already established
// and proved correct - not re-derived from scratch, since both games'
// upstream `VVramToVram()`/`SendUL()` functions are byte-for-byte the same
// shared "cate" engine code. Upstream's own `Backup[]` dirty-tracking
// buffer (a real-hardware I2C-bandwidth optimization) is dropped, matching
// gameCracky.c's own precedent of always redrawing the full frame instead.
//
// **A second VRAM-persistence case handled the identical way as
// gameCracky.c's own overlay mechanism**: `PrintGameOver()`/`PrintTimeUp()`
// write their message text directly into real Vram bytes *inside the map
// area* (page4, column 8 or 9 respectively - confirmed via direct reading
// of `Status.cpp` to be the *exact same* page/column values gameCracky.c's
// own `crkPrintGameOver()`/`crkPrintTimeUp()` already use, since both games
// share the same font/screen-layout convention), bypassing the VVram grid
// entirely. Reproduced with the same `cacOverlayActive`/`cacOverlayText`/
// `cacOverlayPage`/`cacOverlayCol` overlay scheme gameCracky.c already
// built and proved - during rendering, any raw (col,page) landing inside
// the overlay's own footprint draws from `cacAsciiPattern` instead of the
// VVram-derived byte.
//
// **Status text (SCORE/STAGE/TIME/lives) is written via raw direct Vram
// addressing upstream, at real char-cell columns 24-31 (`LeftX=24`) - the
// exact same real-hardware address space gameCracky.c's own title screen
// bug (see that file's own header comment on `crkStatusChar`) was found
// against.** `PrintC()`/`PrintS()` upstream write to a genuinely WHOLE-
// SCREEN-WIDE Vram address space (32 real 4-pixel-wide char cells per
// page row, `VramRowSize`/`VramStep`), not a narrow sidebar-only grid -
// the status labels just happen to live at the high end of it (columns
// 24-31), while the title screen's own text (see below) lives at the LOW
// end (columns 2-23), well inside what during real gameplay is the
// VVram-composited map area. **This file originally modeled
// `cacStatusChar` as only `int[8][8]` (just the status-label slice) and
// then tried to cram ALL of the title screen's own text into that same
// narrow 8-column grid** (see the "genuine, visible title-screen
// rendering bug" entry further below, and gameCracky.c's own identical
// mistake and fix) - **widened to `int[8][32]`, matching gameCracky.c's
// own fix exactly**, with every print call below now using upstream's
// real, literal columns directly (no more "local 0-7 offset from the
// status zone" indirection) - `cacComposeRawByte()` reads it back across
// the FULL real column range whenever `cacFullWidthText` is set (title
// screen), or just the narrow 24-31 status-label slice otherwise (normal
// gameplay), the identical architecture gameCracky.c's own
// `crkFullWidthText`/`crkComposeRawByte()` already established and
// proved correct.
//
// **Title screen now draws upstream's own real pixel-art "Cacorm" logo
// bitmap, matching gameCracky.c's own identical fix** (see the dated
// section at the very end of this header comment for the full story -
// this paragraph previously said the logo was "simplified to plain text,
// matching gameCracky.c's own established simplification", which was true
// at the time but is now stale: gameCracky.c's own logo was itself later
// restored to its real bitmap after a direct user report, and this game
// was fixed the identical way in the same follow-up pass). Upstream's
// real `Title()` draws the logo (a 5-cell-wide dithered-gradient bitmap
// built from the shared "logo" glyph set, glyphs 0-15) directly into the
// map/VVram area (real columns 2-21, pages 1-2), then separately writes
// real raw-Vram text for "MINI" (col 17, page 3), "INUFUTO 2026" (col 12,
// page 7), "START"/">" (col 9/8, page 5), and "CONTINUE"/">" (col 9/8,
// page 6) - all of it via the exact same whole-screen-wide `PrintC()`/
// `PrintS()` mechanism `PrintStatus()` itself uses, just at different,
// non-overlapping columns. This port now draws the real logo bitmap
// (`cacTitleBytes[]`, byte-diff-verified against upstream) directly into
// `cacVVram` at its own real position, exactly matching upstream's pixel
// output, and restores every other piece of title text - MINI, the full
// "INUFUTO 2026" credit (not the previously-cropped "INUFUTO"), START,
// and the full "CONTINUE" (not the previously-cropped "CONTINU") - to
// upstream's own real, literal columns, since none of them collide with
// the status labels' own columns 24-31 (or with each other) once the grid
// is genuinely full-width. `cacComposeRawByte()` OR-combines the VVram-
// derived logo byte with the status/title-text byte (the two occupy
// disjoint page ranges by construction, so this can never blend two real
// pieces of content together). See `cacBeginTitle()`'s own header comment
// for the exact column values.
//
// **A genuine AVR-unsigned-byte-wraparound reliance, fixed rather than
// ported literally** - upstream's `GetCell(byte x, byte y)` bounds-checks
// only `x>=MapWidth||y>=MapHeight`, relying on `byte` (uint8_t) wraparound
// to turn a would-be-negative `x-1` (reached whenever `CanMove()` probes
// one cell left of the map's own left edge) into a large positive value
// that the `>=MapWidth` check then correctly rejects as "off the map,
// treat as a wall". Vircon32's plain, non-wrapping `int` doesn't do this -
// the exact same "AVR-implicit-narrow-type-reliance" bug class already
// documented at length in this project's own CLAUDE.md (byte truncation,
// signed-sentinel comparison, shift wraparound, `int8_t` overflow
// reliance) - so `cacGetCell()` adds an explicit `x<0||y<0` guard,
// reproducing the *intended* "off the map counts as a wall" behavior by
// construction instead of by an AVR-specific side effect.
//
// **Every genuinely blocking upstream `WaitMelody()` call converted to
// this project's own established frame-stepped state machine**, matching
// gameCracky.c's own state shape closely: CAC_STATE_TITLE (`Title()`'s own
// internal key-poll loop, though upstream's own `while(ScanKeys()!=0);`
// busy-wait-for-release on a direction change is replaced with a plain
// edge-detected toggle, matching gameCracky.c's own precedent),
// CAC_STATE_START_JINGLE (the blocking `Sound_Start()` held before play
// begins), CAC_STATE_PLAYING (the main tick-gated loop), CAC_STATE_LOSE_ANIM
// (`LooseMan()`'s own 8-step spin-and-beep loop), CAC_STATE_GAMEOVER_JINGLE,
// CAC_STATE_CLEAR_WAIT (the real `WaitTimer(10)` pause),
// CAC_STATE_CLEAR_JINGLE, CAC_STATE_BONUS_TALLY (the real
// `while(StageTime>=BonusRate){...Sound_Beep();}` bonus-countdown loop).
// One upstream blocking sound call, `Sound_Hit()` (fired once per item
// inside `EraseItems()`'s own loop - which can process *multiple* items
// in a single call, each blocking for its own melody duration on real
// hardware, giving a sequential multi-beep combo sound), is instead fired
// non-blocking/fire-and-forget per item - matching this project's own
// established "collapse a real-hardware sequential-blocking sound burst
// into a single async trigger" precedent (Vircon32's `md_playTone()` is
// already genuinely multi-voice, so this isn't the queueless-single-
// channel collapse-to-last-tone bug class, just a deliberate simplification
// of a purely cosmetic multi-beep pacing effect).
//
// **Sound**: upstream's own `Sound.cpp` is the same real 3-tone-channel
// software mixer/tracker as gameCracky.c's own game (`Tempo=180` here, not
// gameCracky.c's `160`) - every call routed through `md_playTone()` the
// same way, via a small `cacMelodyLength()`/`cacMelodyValue()` id-based
// resolver and three independent frame-stepped sequencer slots
// (0=one-shot SFX, 1=jingle/BGM-voice-A - reused for both, exactly like
// upstream's own channel 1 and gameCracky.c's own identical channel reuse,
// 2=BGM-voice-B). Every melody table was extracted from upstream's real
// `Sound.cpp` via a small Python script (resolving the `NoteLength`/
// `Scale` enum symbols and the one computed `N8+N2` length expression to
// plain numbers), not hand-transcribed - each note's own real duration is
// derived from `SoundHandler()`'s own tempo formula
// (`cacNoteFrames(length) = round(length * (300/180))`), matching
// gameCracky.c's own `crkNoteFrames()` derivation shape exactly, just with
// CACORM's own real Tempo value.
//
// Movable coordinates (`Movable.x/y`) are, like gameCracky.c's own
// finding, already expressed directly in VVram-cell-grid units (no extra
// scaling needed to composite a sprite/tile), confirmed by tracing
// `LocateMovable()`'s own formula. Unlike gameCracky.c, this game
// introduces a second coordinate resolution above the base "coord" unit -
// `MapShift`/`MapRate`/`MapMask` (Movable.h) - since the man/monsters only
// ever draw a line segment or make a movement decision once every *half*
// of a map cell's own width (`CoordRate=1`, `MapRate=2`), giving the
// walk-to-the-midpoint-then-commit rhythm the line-drawing mechanic
// depends on. Kept as the real expressions (not resolved to their current
// literal values) matching gameCracky.c's own "#defines should stay
// exactly as upstream wrote them" preference - this also preserves one
// genuinely degenerate-but-load-bearing upstream quirk: `CoordMask` (used
// in `MoveMonsters()`'s own per-tick gate for `DecideDirection()`)
// evaluates to a literal `0`, making that gate always true (`DecideDirection`
// re-evaluates every single tick, not just on cell-boundary ticks) - kept
// exactly as upstream has it rather than "corrected" into a real gate.
//
// `struct CacActor { CacMovable mv; int dx, dy, status; }` - a genuine
// struct-of-struct composition (not a flattened C++ class hierarchy the
// way Tiny Missile's/Tiny Pipe's own class ports needed), matching
// upstream's real `struct Actor { Movable _; ... }` shape directly.
// Confirmed safe before use: this project's own `gameBootskell.c`
// (`struct BskMonster { BskMovable m; ... }`) already ships this exact
// nested-struct-field pattern, including `&x.m`/`pMonster->m.field`-style
// addressing - not a first-of-its-kind risk. Field renamed from upstream's
// own bare `_` to `mv` for clarity; `Actor.clock` (declared upstream,
// confirmed via a project-wide grep to be read/written nowhere at all) is
// dropped as confirmed-dead, matching this project's own "confirm dead
// code via grep before dropping it" precedent.
//
// One further, genuinely dead upstream helper was found and NOT ported,
// matching that same precedent: `Line.cpp`'s own `Trace(byte old, bool
// erase)` takes an `old` parameter that is only ever assigned `0` inside
// the function body and never read again afterward (a real, harmless,
// vestigial leftover) - `cacTrace()` drops the parameter entirely
// (`bool erase` only), with every one of its 3 real call sites (both
// inside `cacGrowEnteringLine()`) preserved exactly, including the one
// whose own return value is genuinely discarded (`cacTrace(true)` called
// purely for its walk-and-erase side effect, matching upstream's own
// `Trace(cell, false)` structure, whose own discarded-return-value call
// is otherwise a complete no-op given the confirmed-dead `old` parameter -
// kept as a literal, safe, zero-risk translation rather than guessed-at
// as removable, since an intricate loop-closing algorithm like this one
// is exactly the kind of code this project's own precedent says to
// mirror structurally rather than re-derive).
//
// `Line.cpp`'s own `MaskBytes[]` table (`~Line_Right` etc, real bitwise-
// NOT of a small constant) is ported as pre-computed literal negative
// decimal values (`-3`, `-2`, `-9`, `-5`) rather than using the `~`
// operator directly inside a global array initializer, since no existing
// port in this project was confirmed to exercise that specific
// combination - Vircon32's own documented two's-complement `int`
// semantics (matching this project's own signed-sentinel bug findings
// elsewhere) confirm a negative mask value ANDs correctly regardless of
// how it's spelled, so this is a zero-risk, zero-behavior-change spelling
// choice, not a workaround for a real problem.
//
// -----------------------------------------------------------------------------
// A later, meticulous re-verification pass (this game had originally only
// been test-compiled, never played/screenshot-tested) - re-traced every
// upstream `.h`/`.cpp` file line by line, byte-diffed every data table
// against a fresh Python extraction (all matched on the first attempt: the
// two font/tile tables, all 9 melody tables including both 119/133-note BGM
// tracks, the 32-entry RNG table, and all 8 stages' own start/item/enemy/
// wall-byte data), and actually launched and played the built ROM via a
// Puppeteer/WebGL harness. Two real bugs were found and fixed:
//
// 1) **A real, visible title-screen rendering bug** (original finding,
//    later fully superseded - see the "wrong text-layout model" section
//    at the very end of this header comment, added after a real user-
//    supplied hardware photo overturned the diagnosis below) - confirmed
//    via a live screenshot showing garbled text: page3 read "MINIE  1"
//    instead of "MINI", and page5 read ">START 0" instead of ">START".
//    Original root-cause diagnosis: `cacBeginTitle()` called the FULL
//    `cacPrintStatus()` (which writes "STAGE"+the stage-number digit into
//    page3, and "TIME"+the countdown digits into page5) and THEN wrote
//    this port's own 5-line title text ("MINI" into page3, "START" into
//    page5) over the SAME two rows, without clearing either one first -
//    the title text only partially overwrote what PrintStatus had just
//    written, leaving stray leftover characters bleeding through (the "E"
//    from "STAGE", the stage-number "1", and a leftover ones-digit of
//    `cacStageTime`). **Original (now-superseded) fix**: a
//    `cacPrintStatusForTitle()` stripped-down status print used only by
//    the title screen, showing just SCORE and the lives digit, in place
//    of the full `cacPrintStatus()` - this "worked" only by accident, by
//    happening to never print the two colliding rows at all, not by
//    actually placing anything at its real, correct column. **This
//    diagnosis was wrong** - see the final section of this header comment
//    for the real bug (a `cacStatusChar` grid modeled far too narrow) and
//    the real fix (widen the grid, use upstream's own real, literal
//    columns, restore the full `cacPrintStatus()`).
//
// 2) **A real rendering-fidelity gap: the "you just closed a loop" red
//    line flash was computed but never actually visible.** Traced by
//    reading `GrowEnteringLine()`'s real control flow against
//    `VVramToVram()`/`DrawAll()`: upstream sets `LineRed = true;
//    DrawAll();` (a genuine hardware flush - the red line really is on
//    screen at that instant), then calls the *blocking* `Sound_Beep()`
//    (holding that red frame visible for real wall-clock time while it
//    plays), then `EraseItems()` (each captured item's own blocking
//    `Sound_Hit()` holding it further still), and only resets
//    `LineRed = false` at the very end with no further `DrawAll()` -
//    meaning the red highlight stays on real hardware until whatever
//    redraw happens next. This port's own non-blocking Sound_Beep
//    simplification (a deliberate, otherwise-correct choice - see this
//    file's own note on it further above) removed the only thing that
//    was ever holding that red frame visible: since nothing here blocks,
//    `cacLineRed` got set true then immediately false again within the
//    SAME real engine tick, and since `cacRender()` (the actual screen
//    push) only ever fires once per tick, at the very end, the red flash
//    was computed into `cacVVram` and then unconditionally overwritten
//    before the frame was ever actually shown - completely invisible,
//    even though item capture/scoring/wall-formation all still worked
//    correctly underneath it. **Fixed** by deferring the final cleanup
//    (`cacLineRed = false; cacTrace( true ); cacLinePrevX/Y = ...;`)
//    instead of running it synchronously: `cacGrowEnteringLine()` now
//    leaves `cacLineRed` true and arms `cacLineFlashHold` (a new global,
//    `CAC_LINE_FLASH_FRAMES = 18` real frames ~= 300ms) instead;
//    `cacUpdatePlaying()` holds the game genuinely paused for that many
//    real frames (matching upstream's own real single-threaded freeze
//    during its blocking sound calls - no monster movement, no time
//    decrement, no death/clear checks run during the hold) with the red
//    line actually rendered, then performs the deferred cleanup on the
//    frame the hold expires. The fixed duration is a deliberate,
//    documented approximation of upstream's own variable-length real-
//    hardware pause (rather than trying to reproduce it note-for-note),
//    matching this project's own established "downsample a variable
//    real-hardware-timed sequence to a fixed, clearly-visible hold"
//    precedent (e.g. `CAC_STATE_CLEAR_WAIT`'s own fixed 10-frame pause).
//    Verified two ways: first with a throwaway debug hook (forcing the
//    hold immediately at gameplay start) that confirmed the deferred
//    cleanup path genuinely executes and doesn't itself crash the engine
//    - it DID crash, but only because the hook injected a raw cell value
//    without maintaining the `cacLineFirstX/Y`/`cacLineLastX/Y` invariant
//    `cacTrace()` depends on, an artifact of the crude hook rather than
//    the real fix (removed once its diagnostic purpose was served); then,
//    properly, by computing an exact loop-closing move sequence offline
//    against stage 1's real wall data (`LEFT,LEFT,UP,LEFT,DOWN,RIGHT`
//    from the man's own start cell) and playing it out live - the loop
//    closed with no crash, a wide-area pixel diff confirmed the whole
//    drawn line path's own rendered pattern changed at the moment of
//    closure (consistent with `cacLineRed` flipping true and affecting
//    every line-glyph cell at once, not just the newly-closed segment),
//    `cacStageTime` correctly stayed frozen throughout the hold and
//    resumed counting down normally right after, and movement/rendering
//    continued working correctly afterward with no corruption.
//
// Everything else audited and confirmed correct, not just skipped: every
// position/offset formula in `cacComposeRawByte()`/`cacMapToVVram()`/
// `cacDrawSpritesIntoVVram()` traced against upstream's own `SendUL()`/
// `VPut2C()`/`DrawSprites()` byte-for-byte; the entire state machine
// (title -> start jingle -> playing -> lose-anim -> retry-or-gameover ->
// clear-wait -> clear-jingle -> bonus-tally -> next stage) traced against
// `Main()`'s real goto-chain label by label, including which resets
// (`Score`/`CurrentStage`/`RemainCount`/`StageTime`/`Clock`/`monsterNum`/
// `timeDenom`) happen at which shared label and in which order; a project-
// wide grep confirmed no upstream global or struct member anywhere in this
// game has a non-zero initializer needing an explicit match (unlike Tiny
// Gilbert's own `visible=1` bug elsewhere in this project); and the
// `Cell_Wall`/`x<0`/`y<0` bounds-guard fix already documented above was
// re-confirmed still correct and still the only place needing it.
//
// -----------------------------------------------------------------------------
// **A real user-supplied hardware photo of the sibling gameCracky.c
// overturned the "title text collides with status labels" diagnosis
// above, and the identical wrong-model bug was found and fixed here
// too.** Upstream's real `PrintC()`/`PrintS()` write to a genuinely
// whole-screen-wide Vram address space (32 real 4-pixel char cells per
// page row), not a narrow 8-cell sidebar - `Status.cpp`'s own `LeftX=24`
// constant really does confine SCORE/STAGE/TIME/lives to columns 24-31,
// but every piece of the title screen's own text (checked directly
// against this game's own real `Status.cpp::Title()`) lives at DIFFERENT,
// LOWER columns: the simplified logo replacement at (an arbitrary, non-
// colliding) col 2/page 2, "MINI" at real col 17/page 3, "INUFUTO 2026"
// at real col 12/page 7, "START" at real col 9/page 5 (cursor col 8), and
// "CONTINUE" at real col 9/page 6 (cursor col 8) - all genuinely clear of
// columns 24-31, using the exact same shared `PrintC()`/`PrintS()`
// mechanism at different column arguments, not a separate, narrower grid.
// This port's own `cacStatusChar` had been modeled as only `int[8][8]`
// (just the status-label columns) and then had ALL of the title screen's
// own text crammed into that same 8-cell-wide grid too - reusing the same
// columns 24-31 the status labels also use - which is what caused the
// real collision found in section 1) above; the original fix
// (`cacPrintStatusForTitle()`, skipping the two colliding status rows
// entirely) treated the symptom, not the cause, and also meant
// "INUFUTO"/"CONTINUE" had to stay cropped to "INUFUTO"/"CONTINU" since
// there was genuinely no room left in the cramped 8-column model.
// **Fixed** the identical way as gameCracky.c: `cacStatusChar` widened to
// `int[8][32]`, every status-label print call switched to upstream's
// real, literal columns (`+24` from the old local offsets - a pure
// relabeling, the underlying values were already correct), every title-
// screen print call moved to upstream's own real, literal columns
// (restoring the full, uncropped "INUFUTO 2026" and "CONTINUE" text), a
// new `cacFullWidthText` flag (true only during `CAC_STATE_TITLE`) makes
// `cacComposeRawByte()` read the full 32-column range from
// `cacStatusChar` instead of just the narrow 24-31 status slice, and
// `cacPrintStatusForTitle()` is removed entirely - the title screen now
// calls the same, full `cacPrintStatus()` gameplay uses, since nothing it
// prints collides with the title's own text anymore. Proactively applied
// gameCracky.c's own separately-documented stale-status-text lesson here
// too, rather than waiting for a report: `cacBeginTitle()` now resets
// `cacStageTime = 0` before printing status (upstream's own `StageTime`
// can be genuinely nonzero at the `goto title;` reached via a monster-
// collision death mid-countdown, not just the `StageTime==0` time-up
// path, which would otherwise leave a stale TIME value on the title
// screen the exact same way gameCracky.c's own TIME display once did).
// Verified via a clean rebuild and a live screenshot (see the commit/PR
// history for this fix) showing every title-screen element - SCORE,
// STAGE, TIME, the CACORM logo text, MINI, the full INUFUTO 2026 credit,
// START/CONTINUE with their own cursor, and the lives digit - all visible
// simultaneously with no overlap and no truncation.
//
// -----------------------------------------------------------------------------
// **A second architectural issue found in the sibling gameCracky.c, and
// fixed here too by direct mirror.** After the wide-grid fix above shipped,
// re-reading gameCracky.c's own `Status.cpp::Title()` for that game found
// its own logo was never actually a "purely decorative, safe to simplify"
// element - it's the single largest, most prominent piece of content on
// the whole title screen, and the small 27-glyph ASCII font used for the
// text substitute is missing several letters (no L/H/Y/W/D/K), which is
// why gameCracky.c's own earlier "CRACKY" text substitute rendered as
// "CRAC" with gaps. gameCracky.c was fixed to draw its own real
// `TitleBytes[]` pixel-art bitmap directly into `VVram` instead of
// substituting text, with `crkComposeRawByte()` updated to OR-combine the
// VVram-derived logo byte with the status-text byte (safe since the two
// occupy disjoint hardware pages by construction).
//
// This game's own logo text substitute ("CACORM", all ASCII, no missing
// glyphs) never actually rendered with gaps the way gameCracky.c's did,
// but the underlying design flaw is identical: upstream's own real
// `Title()` (see `more games/UIAPduino_cacorm/Status.cpp`) draws a
// genuine pixel-art "Cacorm" wordmark (5 letter-cells x 4x4 VVram-cell
// glyphs, `TitleBytes[]`, 80 values) as the biggest element on the title
// screen, not a plain-text label - simplifying it to text was the same
// unwarranted judgment call gameCracky.c's own header comment now
// documents as wrong. **Fixed the identical way**: added `cacTitleBytes`
// (byte-diff-verified via a small Python script against the real
// `Status.cpp::Title()` source - 80 values, all in range 0-15, confirmed
// valid indices into `cacCharPattern`'s own already-verified "logo" range,
// which was independently re-checked against `Chars.cpp` and confirmed
// byte-identical to gameCracky.c's own logo range - no fix needed there),
// drew it directly into `cacVVram` at upstream's own real position
// (`VVram + VVramWidth*2 + TitleLeft`, i.e. VVram row 2, columns
// `2..21` - `TitleLeft = (24 - 4*5)/2 = 2`, matching upstream's own
// `TitleLength=5` exactly, real hardware pages 1-2), and removed the
// `cacPrintS(2,2,sCacorm,6)` plain-text substitute and its now-unused
// `sCacorm` local entirely. `cacComposeRawByte()` was restructured from
// an exclusive if/else (map byte OR status-text byte, never both) into
// the same OR-combine gameCracky.c's own `crkComposeRawByte()` uses -
// `MINI`'s own real column (17, already correct pre-fix: upstream's
// `TitleLeft + 4*TitleLength - 5 = 2+20-5 = 17`) needed no change.
// Verified via a compile-only check (`compile src/main.c`, per explicit
// instruction not to play-test this change) confirming a clean build with
// no errors; the real visual result (the logo bitmap rendering correctly
// alongside SCORE/STAGE/TIME/MINI/INUFUTO 2026/START/CONTINUE/lives with
// no overlap) was not independently screenshot-verified this pass and is
// worth a direct check.
//
// Entry points: `gameCacorm_init()` / `gameCacorm_update()`.
//
// -----------------------------------------------------------------------------
// Dialect conversion notes (standard-C port, see this project's own
// CLAUDE.md for the full porting/bug-fix history behind every finding
// documented above) - only `TYPE[N] name` -> `TYPE name[N]` array-
// declaration syntax (including the `CacSprite[N]`/`CacActor[N]`/etc
// custom-typedef-array forms, and the 2D `int[H][W]` forms) and the 5 bare
// `struct` tags (CacMovable/CacActor/CacSprite/CacItem/CacPoint) ->
// `typedef struct` needed changing; every string-shaped array in this file
// (cacOverlayText, the various `sScore`/`sStage`/`sMini`/`sInufuto`/etc
// local buffers) holds individual `'A'`-style char constants, never a
// `"..."` string literal, matching gameCracky.c's own identical finding -
// so none of them needed `int[]`->`char[]` widening; every value still
// round-trips through cacAsciiIndex()'s own int-comparison lookup exactly
// as upstream wrote it. No EEPROM/high-score persistence here, matching
// upstream having none at all (confirmed dead/commented-out `HiScore` -
// see above). No forceRedraw needed either: every one of this state
// machine's own branches calls cacRender() unconditionally on every real
// tick it runs (including the CAC_LINE_FLASH_FRAMES hold in
// cacUpdatePlaying(), which still renders every tick even while gameplay
// logic itself is frozen), so there's no "last frame's pixels are still
// correct, skip redrawing" case anywhere in this file for a resumed
// screen to miss.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into cacCharPattern (map tiles) / cacAsciiPattern
//   (status/overlay text)
// -----------------------------------------------------------------------------

#define CAC_CHAR_SPACE 0x00
#define CAC_CHAR_FENCE 0x10
#define CAC_CHAR_LINE_NORMAL 0x12
#define CAC_CHAR_LINE_RIGHTBOTTOM 0x12
#define CAC_CHAR_LINE_LEFTBOTTOM 0x13
#define CAC_CHAR_LINE_RIGHTTOP 0x14
#define CAC_CHAR_LINE_LEFTTOP 0x15
#define CAC_CHAR_LINE_LEFT 0x16
#define CAC_CHAR_LINE_RIGHT 0x17
#define CAC_CHAR_LINE_TOP 0x18
#define CAC_CHAR_LINE_BOTTOM 0x19
#define CAC_CHAR_LINE_RED 0x1A
#define CAC_CHAR_MAN 0x22
#define CAC_CHAR_MONSTER 0x42
#define CAC_CHAR_POINT 0x62
#define CAC_CHAR_INCREASER 0x72
#define CAC_CHAR_BLOCK 0x76
#define CAC_CHAR_ITEM 0x7A
#define CAC_CHAR_END 0x7E

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define CAC_COORD_SHIFT 0
#define CAC_COORD_RATE ( 1 << CAC_COORD_SHIFT )
#define CAC_COORD_MASK ( CAC_COORD_RATE - 1 )

#define CAC_MAP_SHIFT ( CAC_COORD_SHIFT + 1 )
#define CAC_MAP_RATE ( CAC_COORD_RATE * 2 )
#define CAC_MAP_MASK ( CAC_MAP_RATE - 1 )

#define CAC_HIT_RANGE ( CAC_COORD_RATE * 4 / 3 )

// -----------------------------------------------------------------------------
//   Actor.h
// -----------------------------------------------------------------------------

#define CAC_ACTOR_SEQ_MASK 0x01
#define CAC_ACTOR_DIRECTION_MASK 0x06
#define CAC_ACTOR_PATTERN_MASK ( CAC_ACTOR_DIRECTION_MASK | CAC_ACTOR_SEQ_MASK )
#define CAC_ACTOR_LIVE 0x80

#define CAC_DIR_LEFT 0
#define CAC_DIR_RIGHT 2
#define CAC_DIR_UP 4
#define CAC_DIR_DOWN 6

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define CAC_MAP_WIDTH 12
#define CAC_MAP_HEIGHT 7
#define CAC_STAGE_WIDTH ( CAC_MAP_WIDTH * 2 )
#define CAC_STAGE_HEIGHT ( CAC_MAP_HEIGHT * 2 )
#define CAC_STAGE_TOP 1

#define CAC_LINE_LEFT 0x01
#define CAC_LINE_RIGHT 0x02
#define CAC_LINE_TOP 0x04
#define CAC_LINE_BOTTOM 0x08
#define CAC_CELL_SPACE 0x00
#define CAC_CELL_WALL 0x10
#define CAC_CELL_ITEM 0x20
#define CAC_CELL_MASK 0xf0

#define CAC_STAGE_COUNT 8
#define CAC_MAX_ITEM_COUNT 8
#define CAC_MONSTER_SLOT_COUNT 4

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define CAC_VVRAM_WIDTH 24
#define CAC_VVRAM_HEIGHT 16

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define CAC_SPRITE_MAN 0
#define CAC_SPRITE_MONSTER 1
#define CAC_SPRITE_POINT 5
#define CAC_SPRITE_INCREASER 13
#define CAC_SPRITE_COUNT 14
#define CAC_INVALID_CODE 255

#define CAC_POINT_COUNT ( CAC_SPRITE_COUNT - CAC_SPRITE_POINT )
#define CAC_POINT_INVALID_Y 0xe0
#define CAC_POINT_TIME 6

// -----------------------------------------------------------------------------
//   Sound sequencer melody ids - resolved by cacMelodyLength()/cacMelodyValue()
//   instead of a real pointer-per-channel, matching gameCracky.c's own
//   established "resolve by id" pattern.
// -----------------------------------------------------------------------------

#define CAC_MELODY_NONE 0
#define CAC_MELODY_LOOSE 1
#define CAC_MELODY_HIT 2
#define CAC_MELODY_BEEP 3
#define CAC_MELODY_UP 4
#define CAC_MELODY_START 5
#define CAC_MELODY_CLEAR 6
#define CAC_MELODY_GAMEOVER 7
#define CAC_MELODY_BGM1 8
#define CAC_MELODY_BGM2 9

#define CAC_SEQ_SFX 0
#define CAC_SEQ_JINGLE 1
#define CAC_SEQ_BGM_A 1
#define CAC_SEQ_BGM_B 2

#define CAC_BONUS_RATE 8
#define CAC_MAX_TIME_DENOM ( 50 / ( 8 / CAC_COORD_RATE ) )

// -----------------------------------------------------------------------------
//   Data tables - extracted programmatically from the real upstream source
//   (Chars.cpp / Sound.cpp / Stages.cpp / Math.cpp) via a small Python
//   script, not hand-transcribed.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph, used for all
// status/overlay text - byte-identical to gameCracky.c's own crkAsciiPattern
// (both games share the same font).
int cacAsciiPattern[108] = {
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

// CharPattern - 126 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
// Shares its own first 16 "logo" glyphs byte-for-byte with gameCracky.c's
// own crkCharPattern (a common "cate" engine dither-gradient asset), then
// diverges (fence/line/man/monster/point/increaser/block/item are all
// CACORM-specific sprites, not shared with gameCracky.c's own set).
int cacCharPattern[252] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0x88, 0x88, 0x11, 0x11, 0x88, 0xf8, 0x8f, 0x88,
    0x11, 0xf1, 0x1f, 0x11, 0x0f, 0x00, 0x00, 0xf0,
    0x11, 0x11, 0x88, 0x88, 0xcc, 0xff, 0xff, 0xcc,
    0x33, 0xff, 0xff, 0x33, 0xff, 0x00, 0x00, 0xff,
    0x33, 0x33, 0xcc, 0xcc, 0xa0, 0xbf, 0xef, 0x00,
    0xf8, 0x37, 0xf7, 0x0b, 0xa0, 0xbf, 0xef, 0x00,
    0x32, 0xb7, 0x7f, 0x21, 0x00, 0xfe, 0xfb, 0x0a,
    0xb0, 0x7f, 0x73, 0x8f, 0x00, 0xfe, 0xfb, 0x0a,
    0x12, 0xf7, 0x7b, 0x23, 0xe0, 0xff, 0xef, 0x00,
    0xf1, 0x3f, 0x77, 0x03, 0xe0, 0xff, 0xef, 0x00,
    0x73, 0x37, 0xff, 0x01, 0x00, 0xbe, 0xbf, 0x0e,
    0x30, 0x77, 0xf3, 0x1f, 0x00, 0xbe, 0xbf, 0x0e,
    0x10, 0xff, 0x73, 0x37, 0xa8, 0xaf, 0xef, 0x08,
    0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e, 0xce, 0x00,
    0x32, 0xf7, 0xff, 0x02, 0x80, 0xfe, 0xfa, 0x8a,
    0x00, 0xfb, 0x37, 0x01, 0x00, 0xec, 0xe4, 0x04,
    0x20, 0xff, 0x7f, 0x23, 0xe8, 0xef, 0xef, 0x08,
    0x30, 0xf7, 0x37, 0x00, 0xc0, 0xce, 0xce, 0x00,
    0x71, 0xff, 0x7f, 0x01, 0x80, 0xbe, 0xbe, 0x8e,
    0x00, 0x73, 0x7f, 0x03, 0x00, 0x6c, 0x6c, 0x0c,
    0x10, 0xf7, 0xff, 0x17, 0xe4, 0xc0, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x24, 0xcc, 0xc2, 0x00,
    0x32, 0x02, 0x61, 0x69, 0x8c, 0xce, 0xc2, 0x00,
    0x00, 0x03, 0x61, 0x69, 0xa4, 0xc4, 0xc2, 0x00,
    0x21, 0x01, 0x61, 0x69, 0x1e, 0x1f, 0xb3, 0xe3,
    0x63, 0x66, 0x64, 0x36, 0x1f, 0x11, 0x11, 0xf1,
    0x8f, 0x88, 0x88, 0xf8, 0xc4, 0xfc, 0xcc, 0x04,
    0x40, 0x13, 0x43, 0x00,
};

// TitleBytes - upstream's own real "Cacorm" title-screen logo bitmap
// (Status.cpp's `Title()`), 5 letter-cells x 4x4 VVram-cell glyph indices
// each (80 values total, TitleLength=5 upstream - a stylized/dithered
// wordmark image, not a literal one-glyph-per-ASCII-letter rendering, so
// 5 cells for a 6-letter word is upstream's own real, deliberate design,
// not a transcription mismatch), byte-diff-verified against the real
// upstream source via a small Python script. Every value here is a valid
// index into cacCharPattern[]'s own "logo" range (indices 0-15, the first
// 32 bytes of that table - confirmed byte-identical to gameCracky.c's own
// crkCharPattern logo range) - the exact same shared dither-gradient
// palette used to build gameCracky.c's own "CRACKY" wordmark, just with a
// different bitmap here. See cacBeginTitle()'s own comment for why this
// replaces the earlier plain-text "CACORM" substitute.
int cacTitleBytes[80] = {
    0x08, 0x07, 0x0d, 0x02, 0x0f, 0x00, 0x00, 0x00,
    0x0d, 0x02, 0x08, 0x02, 0x00, 0x05, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x0d, 0x02, 0x0e,
    0x0e, 0x0d, 0x03, 0x0f, 0x04, 0x05, 0x01, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x0d, 0x02, 0x0e, 0x0d,
    0x08, 0x02, 0x0f, 0x0c, 0x05, 0x00, 0x04, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x0f, 0x06, 0x01,
    0x03, 0x0f, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x0d, 0x0d, 0x02,
    0x0f, 0x0c, 0x0c, 0x03, 0x05, 0x04, 0x04, 0x01,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values
// 1-40) - byte-identical to gameCracky.c's own crkFrequencies.
int cacFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

// Melody tables - raw [duration,note] byte pairs (matching upstream's own
// ToneChannel::Next() encoding: duration=0 ends the sequence, duration=255
// wraps back to the start; note=0 is a rest) - extracted programmatically
// from Sound.cpp, resolving the NoteLength/Scale enum symbols to numbers.
int cacMelodyLoose[3] = {
    1, 18, 0,
};

int cacMelodyHit[17] = {
    1, 26, 1, 28, 1, 30, 1, 32, 1, 33,
    1, 35, 1, 37, 1, 38, 0,
};

int cacMelodyBeep[3] = {
    1, 30, 0,
};

// Non-blocking (upstream fires this via StartMelody, not WaitMelody).
int cacMelodyUp[13] = {
    1, 21, 1, 22, 1, 23, 1, 26, 1, 30,
    1, 33, 0,
};

int cacMelodyStart[23] = {
    12, 21, 12, 25, 6, 28, 12, 25, 12, 26,
    6, 26, 12, 30, 6, 33, 18, 30, 36, 33,
    12, 0, 0,
};

int cacMelodyClear[29] = {
    6, 30, 6, 30, 6, 28, 6, 26, 6, 28,
    12, 30, 12, 32, 6, 32, 6, 30, 6, 28,
    6, 30, 12, 32, 30, 33, 24, 0, 0,
};

int cacMelodyGameOver[21] = {
    6, 33, 6, 26, 6, 30, 6, 25, 6, 28,
    6, 30, 6, 32, 6, 33, 36, 33, 12, 0,
    0,
};

int cacMelodyBgm1[119] = {
    12, 21, 12, 28, 6, 21, 12, 28, 12, 30,
    6, 30, 6, 28, 6, 28, 6, 26, 6, 26,
    6, 25, 6, 25, 12, 23, 12, 23, 6, 23,
    12, 25, 18, 23, 36, 0, 12, 21, 12, 28,
    6, 21, 12, 28, 12, 30, 6, 30, 6, 28,
    6, 28, 6, 26, 6, 26, 6, 25, 6, 25,
    12, 26, 12, 26, 6, 26, 12, 30, 18, 28,
    36, 0, 6, 25, 6, 25, 6, 25, 12, 25,
    6, 25, 12, 30, 6, 23, 6, 23, 6, 23,
    12, 23, 6, 23, 12, 28, 6, 0, 6, 30,
    6, 0, 6, 28, 6, 0, 6, 26, 6, 0,
    6, 25, 12, 23, 12, 25, 24, 21, 255,
};

int cacMelodyBgm2[133] = {
    6, 21, 12, 0, 18, 25, 6, 28, 6, 0,
    6, 18, 12, 0, 18, 21, 6, 25, 6, 0,
    6, 23, 12, 0, 18, 26, 6, 18, 6, 0,
    6, 16, 12, 0, 18, 20, 6, 23, 6, 0,
    6, 21, 12, 0, 18, 25, 6, 28, 6, 0,
    6, 18, 12, 0, 18, 21, 6, 25, 6, 0,
    6, 26, 12, 0, 6, 26, 6, 16, 12, 0,
    6, 16, 6, 21, 12, 0, 18, 25, 6, 28,
    6, 0, 6, 21, 12, 0, 6, 21, 6, 18,
    12, 0, 6, 18, 6, 23, 12, 0, 6, 23,
    6, 16, 12, 0, 6, 16, 6, 0, 6, 14,
    6, 0, 6, 14, 6, 0, 6, 16, 6, 0,
    6, 16, 6, 21, 12, 0, 18, 25, 6, 28,
    6, 0, 255,
};

// Math.cpp - same shape RNG as gameCracky.c's own crkRnd, but with fully
// independent state (a separate table + a separate cacRndIndex global,
// not shared with any other game).
int cacRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

// Direction -> (dx,dy) table (Actor.cpp's own DirectionElements).
int cacDirectionElements[8] = { -1, 0, 1, 0, 0, -1, 0, 1 };

// Point.cpp's own score-popup values, indexed by combo "rate" (0-3).
int cacPointValues[4] = { 10, 20, 40, 80 };

// Line.cpp's own direction-indexed line-bit tables.
int cacLeavingBytes[4] = { CAC_LINE_LEFT, CAC_LINE_RIGHT, CAC_LINE_TOP, CAC_LINE_BOTTOM };
int cacEnteringBytes[4] = { CAC_LINE_RIGHT, CAC_LINE_LEFT, CAC_LINE_BOTTOM, CAC_LINE_TOP };
// = { ~Line_Right, ~Line_Left, ~Line_Bottom, ~Line_Top } - pre-computed as
// literal two's-complement decimal values rather than using the `~`
// operator inside a global initializer (see this file's own header
// comment for why).
int cacMaskBytes[4] = { -3, -2, -9, -5 };

// VVram.cpp's own MapToVVram() line-rendering lookup table - which of the
// 4 glyphs (top-left, top-right, bottom-left, bottom-right) to show for
// each of the 16 possible Line_Left|Line_Right|Line_Top|Line_Bottom
// combinations.
int cacLineChars[16][4] = {
    { CAC_CHAR_SPACE, CAC_CHAR_SPACE, CAC_CHAR_SPACE, CAC_CHAR_SPACE },
    { CAC_CHAR_LINE_BOTTOM, CAC_CHAR_SPACE, CAC_CHAR_LINE_TOP, CAC_CHAR_SPACE },
    { CAC_CHAR_SPACE, CAC_CHAR_LINE_BOTTOM, CAC_CHAR_SPACE, CAC_CHAR_LINE_TOP },
    { CAC_CHAR_LINE_BOTTOM, CAC_CHAR_LINE_BOTTOM, CAC_CHAR_LINE_TOP, CAC_CHAR_LINE_TOP },
    { CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFT, CAC_CHAR_SPACE, CAC_CHAR_SPACE },
    { CAC_CHAR_LINE_RIGHTBOTTOM, CAC_CHAR_LINE_LEFT, CAC_CHAR_LINE_TOP, CAC_CHAR_SPACE },
    { CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFTBOTTOM, CAC_CHAR_SPACE, CAC_CHAR_LINE_TOP },
    { CAC_CHAR_LINE_RIGHTBOTTOM, CAC_CHAR_LINE_LEFTBOTTOM, CAC_CHAR_LINE_TOP, CAC_CHAR_LINE_TOP },
    { CAC_CHAR_SPACE, CAC_CHAR_SPACE, CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFT },
    { CAC_CHAR_LINE_BOTTOM, CAC_CHAR_SPACE, CAC_CHAR_LINE_RIGHTTOP, CAC_CHAR_LINE_LEFT },
    { CAC_CHAR_SPACE, CAC_CHAR_LINE_BOTTOM, CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFTTOP },
    { CAC_CHAR_LINE_BOTTOM, CAC_CHAR_LINE_BOTTOM, CAC_CHAR_LINE_RIGHTTOP, CAC_CHAR_LINE_LEFTTOP },
    { CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFT, CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFT },
    { CAC_CHAR_LINE_RIGHTBOTTOM, CAC_CHAR_LINE_LEFT, CAC_CHAR_LINE_RIGHTTOP, CAC_CHAR_LINE_LEFT },
    { CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFTBOTTOM, CAC_CHAR_LINE_RIGHT, CAC_CHAR_LINE_LEFTTOP },
    { CAC_CHAR_LINE_RIGHTBOTTOM, CAC_CHAR_LINE_LEFTBOTTOM, CAC_CHAR_LINE_RIGHTTOP, CAC_CHAR_LINE_LEFTTOP },
};

// Stages.cpp - flattened from upstream's own `struct Stage{start,itemCount,
// pItems,enemyCount,pEnemies,bytes[]}` array + separate Items0-7/Enemies0-7
// arrays into parallel fixed arrays (matching gameCracky.c's own precedent
// for flattening a struct-with-a-real-pointer-member).
int cacStageStart[8] = { 100, 101, 33, 182, 84, 84, 166, 99 };
int cacStageItemCount[8] = { 4, 5, 4, 5, 6, 6, 6, 8 };
int cacStageEnemyCount[8] = { 1, 1, 1, 2, 2, 2, 3, 4 };

// Each stage's own item start bytes, padded to CAC_MAX_ITEM_COUNT(8) with
// unused trailing zeros (never read - bounded by cacStageItemCount).
int cacStageItems[8][8] = {
    { 17, 164, 53, 149, 0, 0, 0, 0 },
    { 81, 161, 131, 21, 149, 0, 0, 0 },
    { 162, 19, 83, 53, 0, 0, 0, 0 },
    { 49, 35, 99, 164, 165, 0, 0, 0 },
    { 17, 129, 131, 147, 132, 37, 0, 0 },
    { 98, 20, 52, 164, 53, 133, 0, 0 },
    { 49, 145, 163, 84, 21, 37, 0, 0 },
    { 17, 33, 114, 83, 21, 53, 117, 133 },
};

// Each stage's own enemy start bytes, padded to CAC_MONSTER_SLOT_COUNT(4).
int cacStageEnemies[8][4] = {
    { 178, 0, 0, 0 },
    { 97, 0, 0, 0 },
    { 180, 0, 0, 0 },
    { 32, 22, 0, 0 },
    { 177, 182, 0, 0 },
    { 177, 38, 0, 0 },
    { 160, 17, 36, 0 },
    { 0, 80, 176, 118 },
};

// Each stage's own packed wall-bit map (84 bits = 11 bytes, 1 bit/cell).
int cacStageBytes[8][11] = {
    { 128, 128, 98, 136, 0, 2, 132, 0, 14, 224, 0 },
    { 0, 224, 16, 0, 192, 66, 40, 4, 0, 176, 0 },
    { 7, 4, 6, 0, 128, 0, 0, 35, 18, 96, 8 },
    { 0, 34, 14, 0, 0, 17, 17, 33, 6, 8, 1 },
    { 0, 140, 6, 64, 16, 5, 81, 8, 0, 32, 1 },
    { 129, 16, 1, 20, 4, 16, 0, 0, 0, 64, 0 },
    { 0, 16, 70, 0, 176, 16, 0, 1, 1, 0, 8 },
    { 0, 0, 3, 0, 64, 96, 0, 0, 0, 0, 0 },
};

// -----------------------------------------------------------------------------
//   Struct definitions
// -----------------------------------------------------------------------------

typedef struct
{
    int x, y;
    int sprite;
} CacMovable;

typedef struct
{
    CacMovable mv;
    int dx, dy;
    int status;
} CacActor;

typedef struct
{
    int x, y;
    int code;
} CacSprite;

typedef struct
{
    int x, y;
} CacItem;

typedef struct
{
    CacMovable mv;
    int clock;
} CacPoint;

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int cacScore;
int cacRemainCount;
int cacCurrentStage;
int cacStageTime;
int cacItemCount;
int cacMonsterNum;
int cacTimeDenom;
int cacStageIndex;
int cacMonsterClock;

int cacVVram[CAC_VVRAM_HEIGHT][CAC_VVRAM_WIDTH];
int cacCellMap[CAC_MAP_WIDTH * CAC_MAP_HEIGHT];

CacSprite cacSprites[CAC_SPRITE_COUNT];

CacActor cacMan;
int cacLineFirstX, cacLineFirstY;
int cacLineLastX, cacLineLastY;
int cacLinePrevX, cacLinePrevY;
bool cacLineRed;

int cacMonsterCount;
CacActor cacMonsters[CAC_MONSTER_SLOT_COUNT];

CacItem cacItems[CAC_MAX_ITEM_COUNT];
CacPoint cacPoints[CAC_POINT_COUNT];
CacMovable cacIncreaser;

int cacRndIndex;

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (32 real 4-pixel-wide char cells per page row) - a
// pattern index into cacAsciiPattern (0 = space) per cell.
//
// **Widened from an original, wrong `[8][8]` after a real user-supplied
// hardware photo of the sibling gameCracky.c's own title screen proved
// that narrow model was flatly incorrect - the identical bug was found
// and fixed here too.** The original design only ever modeled upstream's
// own status-label columns (SCORE/STAGE/TIME/lives, which upstream's
// `LeftX=24` constant genuinely does confine to columns 24-31, 8 cells) -
// but the title screen's own text (the logo, "MINI", "INUFUTO 2026",
// "START"/"CONTINUE") lives at upstream's real columns 2-23, well to the
// LEFT of the status zone, using the exact same shared `PrintC()`/
// `PrintS()` mechanism at different column arguments - not a separate,
// narrower grid at all. See this file's own header comment for the full
// story and `cacComposeRawByte()`/`cacBeginTitle()` for how this wider
// grid actually reaches the screen.
int cacStatusChar[8][32];

// Set true only while on the title screen (CAC_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its
// initial ClearScreen(), and instead drives the ENTIRE screen (not just
// the status zone) through the same PrintC()/PrintS() text mechanism, at
// real columns spanning the whole 0-31 char-cell range. When true,
// cacComposeRawByte() reads cacStatusChar across the full width instead
// of just columns 24-31, letting the title screen use that same wide
// real estate instead of being artificially confined to the narrow
// status-only zone.
bool cacFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintTimeUp()/PrintGameOver() Vram-direct writes - see header.
bool cacOverlayActive;
int cacOverlayText[10];
int cacOverlayLen;
int cacOverlayPage;
int cacOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of CAC_TICK_DIVISOR.
int cacSeqMelody[3];
int cacSeqPos[3];
int cacSeqWait[3];
int cacSeqActive[3];

#define CAC_TICK_DIVISOR 8
int cacTickCounter;

// How many real engine frames to hold the just-closed line visibly red
// before reverting - see cacGrowEnteringLine()'s own header comment for
// why this exists at all (a real rendering-fidelity gap found and fixed
// during a later verification pass).
#define CAC_LINE_FLASH_FRAMES 18
int cacLineFlashHold;

#define CAC_STATE_TITLE 0
#define CAC_STATE_START_JINGLE 1
#define CAC_STATE_PLAYING 2
#define CAC_STATE_LOSE_ANIM 3
#define CAC_STATE_GAMEOVER_JINGLE 4
#define CAC_STATE_CLEAR_WAIT 5
#define CAC_STATE_CLEAR_JINGLE 6
#define CAC_STATE_BONUS_TALLY 7
int cacState;
int cacWaitFrames;
int cacAnimStep;
int cacSelection;
bool cacSelectionChanged;
bool cacPrevLeft, cacPrevRight, cacPrevUp, cacPrevDown, cacPrevFire;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int cacRnd()
{
    int r;
    r = cacRndNumbers[ cacRndIndex ];
    cacRndIndex = cacRndIndex + 1;
    if( cacRndIndex >= 32 )
      cacRndIndex = 0;
    return r & 0x0f;
}

int cacAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - GetCell()
// -----------------------------------------------------------------------------

int cacGetCell( int x, int y )
{
    // Explicit x<0/y<0 guard - see this file's own header comment for why
    // upstream's own unsigned-byte-wraparound-reliant version can't be
    // ported literally.
    if( x < 0 || y < 0 || x >= CAC_MAP_WIDTH || y >= CAC_MAP_HEIGHT )
      return CAC_CELL_WALL;
    return cacCellMap[ y * CAC_MAP_WIDTH + x ];
}


// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

void cacLocateMovable( CacMovable* pMovable, int b )
{
    pMovable->x = ( ( b >> 3 ) & 0xfe ) << CAC_COORD_SHIFT;
    pMovable->y = ( b & 15 ) << ( 1 + CAC_COORD_SHIFT );
}

bool cacIsNear( CacMovable* p1, CacMovable* p2 )
{
    return
        p1->x + CAC_HIT_RANGE >= p2->x && p2->x + CAC_HIT_RANGE >= p1->x &&
        p1->y + CAC_HIT_RANGE >= p2->y && p2->y + CAC_HIT_RANGE >= p1->y;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp
// -----------------------------------------------------------------------------

void cacHideAllSprites()
{
    int i;
    for( i = 0; i < CAC_SPRITE_COUNT; i = i + 1 )
      cacSprites[ i ].code = CAC_INVALID_CODE;
}

void cacShowSprite( CacMovable* pMovable, int code )
{
    cacSprites[ pMovable->sprite ].x = pMovable->x;
    cacSprites[ pMovable->sprite ].y = pMovable->y + CAC_STAGE_TOP;
    cacSprites[ pMovable->sprite ].code = code;
}

void cacHideSprite( int index )
{
    cacSprites[ index ].code = CAC_INVALID_CODE;
}

void cacDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < CAC_SPRITE_COUNT; i = i + 1 )
    {
        if( cacSprites[ i ].code != CAC_INVALID_CODE )
        {
            x = cacSprites[ i ].x;
            y = cacSprites[ i ].y;
            c = cacSprites[ i ].code;
            cacVVram[ y ][ x ] = c; c = c + 1;
            cacVVram[ y ][ x + 1 ] = c; c = c + 1;
            cacVVram[ y + 1 ][ x ] = c; c = c + 1;
            cacVVram[ y + 1 ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into cacStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area).
// -----------------------------------------------------------------------------

int cacAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct linear search
    // (only 27 entries, no cost concern doing this live), matching
    // gameCracky.c's own crkAsciiIndex.
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

int cacPrintC( int page, int col, int c )
{
    cacStatusChar[ page ][ col ] = cacAsciiIndex( c );
    return col + 1;
}

int cacPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = cacPrintC( page, col, s[ i ] );
    return col;
}

void cacPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      cacPrintC( page, col, ' ' );
    else
      cacPrintC( page, col, d1 + '0' );
    cacPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void cacPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        cacPrintC( page, col, ' ' );
        if( d2 == 0 )
          cacPrintC( page, col + 1, ' ' );
        else
          cacPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        cacPrintC( page, col, d1 + '0' );
        cacPrintC( page, col + 1, d2 + '0' );
    }
    cacPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void cacPrintNumber5( int page, int col, int w )
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
          cacPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            cacPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    cacPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// cacStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void cacPrintScore()
{
    // Matches upstream's own PrintScore() exactly: a 5-digit number
    // followed by one always-'0' trailing digit (the same "arcade extra
    // zero" convention gameCracky.c's own crkPrintScore already uses).
    cacPrintNumber5( 1, 26, cacScore );
    cacPrintC( 1, 31, '0' );
}

void cacPrintTime()
{
    cacPrintByteNumber3( 5, 29, cacStageTime );
}

void cacPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };

    cacPrintS( 0, 24, sScore, 5 );
    cacPrintS( 3, 24, sStage, 5 );
    cacPrintByteNumber2( 3, 30, cacCurrentStage + 1 );
    cacPrintS( 5, 24, sTime, 4 );

    // Upstream draws a real 2x2 Char_Remain (Man) icon here, repeated
    // (RemainCount-1) times via Put2C - simplified to a single plain text
    // digit showing the remaining-life count directly, matching this
    // project's own gameCracky.c precedent for an identical upstream
    // icon-based lives display. Clamped to a single digit (upstream's own
    // RemainCount can reach 10 via the Increaser bonus's own `<10` gate).
    if( cacRemainCount <= 0 )
      cacPrintC( 7, 24, ' ' );
    else if( cacRemainCount >= 10 )
      cacPrintC( 7, 24, '9' );
    else
      cacPrintC( 7, 24, cacRemainCount + '0' );

    cacPrintScore();
    cacPrintTime();
}

void cacBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    cacOverlayActive = true;
    cacOverlayLen = len;
    cacOverlayPage = page;
    cacOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      cacOverlayText[ i ] = s[ i ];
}

void cacPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    cacBeginOverlay( s, 9, 4, 8 );
}

void cacPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    cacBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void cacAddScore( int pts )
{
    // Upstream's own HiScore tracking is entirely commented out (dead) -
    // see this file's own header comment - so there is no equivalent here.
    cacScore = cacScore + pts;
    cacPrintScore();
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int cacMelodyLength( int id )
{
    if( id == CAC_MELODY_LOOSE ) return 3;
    if( id == CAC_MELODY_HIT ) return 17;
    if( id == CAC_MELODY_BEEP ) return 3;
    if( id == CAC_MELODY_UP ) return 13;
    if( id == CAC_MELODY_START ) return 23;
    if( id == CAC_MELODY_CLEAR ) return 29;
    if( id == CAC_MELODY_GAMEOVER ) return 21;
    if( id == CAC_MELODY_BGM1 ) return 119;
    if( id == CAC_MELODY_BGM2 ) return 133;
    return 0;
}

int cacMelodyValue( int id, int idx )
{
    if( id == CAC_MELODY_LOOSE ) return cacMelodyLoose[ idx ];
    if( id == CAC_MELODY_HIT ) return cacMelodyHit[ idx ];
    if( id == CAC_MELODY_BEEP ) return cacMelodyBeep[ idx ];
    if( id == CAC_MELODY_UP ) return cacMelodyUp[ idx ];
    if( id == CAC_MELODY_START ) return cacMelodyStart[ idx ];
    if( id == CAC_MELODY_CLEAR ) return cacMelodyClear[ idx ];
    if( id == CAC_MELODY_GAMEOVER ) return cacMelodyGameOver[ idx ];
    if( id == CAC_MELODY_BGM1 ) return cacMelodyBgm1[ idx ];
    if( id == CAC_MELODY_BGM2 ) return cacMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/CAC_TEMPO(180) = 1.6667 real 60Hz ticks - see header comment.
int cacNoteFrames( int length )
{
    return (int)( length * 1.666667 + 0.5 );
}

void cacStartSeq( int channel, int melodyId )
{
    cacSeqMelody[ channel ] = melodyId;
    cacSeqPos[ channel ] = 0;
    cacSeqWait[ channel ] = 0;
    cacSeqActive[ channel ] = 1;
}

void cacStopSeq( int channel )
{
    cacSeqActive[ channel ] = 0;
    cacSeqMelody[ channel ] = CAC_MELODY_NONE;
}

bool cacSeqPlaying( int channel )
{
    return cacSeqActive[ channel ] != 0;
}

void cacAdvanceOneSeq( int channel )
{
    int length, note;

    if( cacSeqActive[ channel ] == 0 ) return;

    if( cacSeqWait[ channel ] > 0 )
    {
        cacSeqWait[ channel ] = cacSeqWait[ channel ] - 1;
        return;
    }

    length = cacMelodyValue( cacSeqMelody[ channel ], cacSeqPos[ channel ] );
    if( length == 0 )
    {
        cacStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        cacSeqPos[ channel ] = 0;
        length = cacMelodyValue( cacSeqMelody[ channel ], 0 );
    }
    note = cacMelodyValue( cacSeqMelody[ channel ], cacSeqPos[ channel ] + 1 );
    cacSeqPos[ channel ] = cacSeqPos[ channel ] + 2;
    cacSeqWait[ channel ] = cacNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)cacFrequencies[ note - 1 ], (float)cacSeqWait[ channel ] / 60.0 );
}

void cacAdvanceSound()
{
    cacAdvanceOneSeq( 0 );
    cacAdvanceOneSeq( 1 );
    cacAdvanceOneSeq( 2 );
}

void cacStartBgm()
{
    cacStartSeq( CAC_SEQ_BGM_A, CAC_MELODY_BGM1 );
    cacStartSeq( CAC_SEQ_BGM_B, CAC_MELODY_BGM2 );
}

void cacStopBgm()
{
    cacStopSeq( CAC_SEQ_JINGLE );
    cacStopSeq( CAC_SEQ_BGM_B );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Increaser.cpp - a spare-life bonus pickup
// -----------------------------------------------------------------------------

void cacInitIncreaser()
{
    cacIncreaser.sprite = CAC_SPRITE_INCREASER;
    cacIncreaser.y = CAC_POINT_INVALID_Y;
    cacHideSprite( cacIncreaser.sprite );
}

void cacShowIncreaser( int x, int y )
{
    cacIncreaser.x = x;
    cacIncreaser.y = y;
    cacShowSprite( &cacIncreaser, CAC_CHAR_INCREASER );
}

void cacHideIncreaser()
{
    cacIncreaser.y = CAC_POINT_INVALID_Y;
    cacHideSprite( cacIncreaser.sprite );
}

bool cacIsIncreaserVisible()
{
    return cacIncreaser.y < CAC_POINT_INVALID_Y;
}


// -----------------------------------------------------------------------------
//   Point.cpp - transient score-value popups
// -----------------------------------------------------------------------------

void cacInitPoints()
{
    int i, sprite;
    sprite = CAC_SPRITE_POINT;
    for( i = 0; i < CAC_POINT_COUNT; i = i + 1 )
    {
        cacPoints[ i ].mv.y = CAC_POINT_INVALID_Y;
        cacPoints[ i ].mv.sprite = sprite;
        cacHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void cacStartPoint( int x, int y, int rate )
{
    int i;
    cacAddScore( cacPointValues[ rate ] );
    for( i = 0; i < CAC_POINT_COUNT; i = i + 1 )
    {
        if( cacPoints[ i ].mv.y == CAC_POINT_INVALID_Y )
        {
            cacPoints[ i ].mv.x = x;
            cacPoints[ i ].mv.y = y;
            cacPoints[ i ].clock = CAC_POINT_TIME << CAC_COORD_SHIFT;
            cacShowSprite( &cacPoints[ i ].mv, CAC_CHAR_POINT + ( rate << 2 ) );
            return;
        }
    }
}

void cacUpdatePoints()
{
    int i;
    for( i = 0; i < CAC_POINT_COUNT; i = i + 1 )
    {
        if( cacPoints[ i ].mv.y != CAC_POINT_INVALID_Y )
        {
            if( cacPoints[ i ].clock == 0 )
            {
                cacPoints[ i ].mv.y = CAC_POINT_INVALID_Y;
                cacHideSprite( cacPoints[ i ].mv.sprite );
            }
            else
              cacPoints[ i ].clock = cacPoints[ i ].clock - 1;
        }
    }
}


// -----------------------------------------------------------------------------
//   Rendering: map -> VVram, plus the fence border
// -----------------------------------------------------------------------------

void cacMapToVVram()
{
    int mapRow, mapCol, cell, x, y;

    for( mapRow = 0; mapRow < CAC_MAP_HEIGHT; mapRow = mapRow + 1 )
    {
        for( mapCol = 0; mapCol < CAC_MAP_WIDTH; mapCol = mapCol + 1 )
        {
            cell = cacCellMap[ mapRow * CAC_MAP_WIDTH + mapCol ];
            x = mapCol * 2;
            y = CAC_STAGE_TOP + mapRow * 2;

            if( cell == CAC_CELL_WALL )
            {
                cacVVram[ y ][ x ] = CAC_CHAR_BLOCK;
                cacVVram[ y ][ x + 1 ] = CAC_CHAR_BLOCK + 1;
                cacVVram[ y + 1 ][ x ] = CAC_CHAR_BLOCK + 2;
                cacVVram[ y + 1 ][ x + 1 ] = CAC_CHAR_BLOCK + 3;
            }
            else if( cell == CAC_CELL_ITEM )
            {
                cacVVram[ y ][ x ] = CAC_CHAR_ITEM;
                cacVVram[ y ][ x + 1 ] = CAC_CHAR_ITEM + 1;
                cacVVram[ y + 1 ][ x ] = CAC_CHAR_ITEM + 2;
                cacVVram[ y + 1 ][ x + 1 ] = CAC_CHAR_ITEM + 3;
            }
            else
            {
                int idx, addition, c0, c1, c2, c3;
                idx = cell & 0x0f;
                addition = 0;
                if( cacLineRed )
                  addition = CAC_CHAR_LINE_RED - CAC_CHAR_LINE_NORMAL;
                c0 = cacLineChars[ idx ][ 0 ];
                c1 = cacLineChars[ idx ][ 1 ];
                c2 = cacLineChars[ idx ][ 2 ];
                c3 = cacLineChars[ idx ][ 3 ];
                if( c0 != CAC_CHAR_SPACE ) c0 = c0 + addition;
                if( c1 != CAC_CHAR_SPACE ) c1 = c1 + addition;
                if( c2 != CAC_CHAR_SPACE ) c2 = c2 + addition;
                if( c3 != CAC_CHAR_SPACE ) c3 = c3 + addition;
                cacVVram[ y ][ x ] = c0;
                cacVVram[ y ][ x + 1 ] = c1;
                cacVVram[ y + 1 ][ x ] = c2;
                cacVVram[ y + 1 ][ x + 1 ] = c3;
            }
        }
    }
}

void cacDrawFence()
{
    int col;
    for( col = 0; col < CAC_VVRAM_WIDTH; col = col + 1 )
    {
        cacVVram[ 0 ][ col ] = CAC_CHAR_FENCE;
        cacVVram[ CAC_VVRAM_HEIGHT - 1 ][ col ] = CAC_CHAR_FENCE + 1;
    }
}

void cacDrawAll()
{
    cacMapToVVram();
    cacDrawSpritesIntoVVram();
}


// -----------------------------------------------------------------------------
//   Line.cpp - the area-capture ("close the loop") mechanic
// -----------------------------------------------------------------------------

int cacSetLineCell( int x, int y, int bits )
{
    int idx, old, cell;
    idx = y * CAC_MAP_WIDTH + x;
    old = cacCellMap[ idx ];
    cell = old | bits;
    cacCellMap[ idx ] = cell;
    return old;
}

void cacGrowLeavingLine( int direction )
{
    cacSetLineCell( cacLineLastX, cacLineLastY, cacLeavingBytes[ direction >> 1 ] );
}

int cacCellToDirection( int cell )
{
    int direction, i;
    direction = 0;
    for( i = 0; i < 4; i = i + 1 )
    {
        if( ( cell & 1 ) != 0 ) return direction;
        direction = direction + 2;
        cell = cell >> 1;
    }
    return direction;
}

// Walks from cacLineFirstX/Y around to cacLineLastX/Y along the recorded
// line-direction bits, optionally erasing each visited cell - matching
// upstream's own Trace() (with its confirmed-dead `old` parameter
// dropped, see this file's own header comment).
int cacTrace( bool erase )
{
    int x, y, mask, cell, idx, direction;
    x = cacLineFirstX;
    y = cacLineFirstY;
    mask = 0xff;
    do
    {
        idx = y * CAC_MAP_WIDTH + x;
        cell = cacCellMap[ idx ];
        if( erase )
          cacCellMap[ idx ] = CAC_CELL_SPACE;
        cell = cell & mask;
        direction = cacCellToDirection( cell );
        x = x + cacDirectionElements[ direction ];
        y = y + cacDirectionElements[ direction + 1 ];
        mask = cacMaskBytes[ direction >> 1 ];
    } while( x != cacLineLastX || y != cacLineLastY );
    return mask;
}

bool cacIsSurrounded( int itemX, int itemY )
{
    int x, y, topCount, bottomCount, cell;
    x = itemX >> 1;
    y = itemY >> 1;
    topCount = 0;
    bottomCount = 0;
    while( x < CAC_MAP_WIDTH )
    {
        cell = cacGetCell( x, y );
        if( ( cell & CAC_LINE_TOP ) != 0 ) topCount = topCount + 1;
        if( ( cell & CAC_LINE_BOTTOM ) != 0 ) bottomCount = bottomCount + 1;
        x = x + 1;
    }
    return ( topCount & 1 ) != 0 && ( bottomCount & 1 ) != 0;
}


// -----------------------------------------------------------------------------
//   Item.cpp
// -----------------------------------------------------------------------------

void cacInitItems()
{
    int i, b;
    cacItemCount = cacStageItemCount[ cacStageIndex ];
    for( i = 0; i < cacItemCount; i = i + 1 )
    {
        b = cacStageItems[ cacStageIndex ][ i ];
        cacItems[ i ].x = ( b >> 3 ) & 0xfe;
        cacItems[ i ].y = ( b & 15 ) << 1;
    }
    for( i = cacItemCount; i < CAC_MAX_ITEM_COUNT; i = i + 1 )
      cacItems[ i ].y = CAC_STAGE_HEIGHT;
}

void cacDrawItems()
{
    int i, x, y;
    for( i = 0; i < cacItemCount; i = i + 1 )
    {
        x = cacItems[ i ].x;
        y = cacItems[ i ].y;
        if( y < CAC_STAGE_HEIGHT )
          cacCellMap[ ( y >> 1 ) * CAC_MAP_WIDTH + ( x >> 1 ) ] = CAC_CELL_ITEM;
    }
}

void cacEraseItems()
{
    int i, x, y, rate;
    rate = 0;
    for( i = 0; i < CAC_MAX_ITEM_COUNT; i = i + 1 )
    {
        x = cacItems[ i ].x;
        y = cacItems[ i ].y;
        if( y < CAC_STAGE_HEIGHT )
        {
            if( cacIsSurrounded( x, y ) )
            {
                cacStartPoint( x << CAC_COORD_SHIFT, y << CAC_COORD_SHIFT, rate );
                if( rate != 0 && !cacIsIncreaserVisible() &&
                    cacRemainCount < 10 && ( cacRnd() & 7 ) == 0 )
                  cacShowIncreaser( x << CAC_COORD_SHIFT, y << CAC_COORD_SHIFT );
                cacCellMap[ ( y >> 1 ) * CAC_MAP_WIDTH + ( x >> 1 ) ] = CAC_CELL_SPACE;
                cacItems[ i ].y = CAC_STAGE_HEIGHT;
                cacItemCount = cacItemCount - 1;
                if( rate < 3 )
                  rate = rate + 1;
                cacDrawAll();
                // Upstream's own Sound_Hit() blocks (WaitMelody) - fired
                // non-blocking here instead, see this file's own header
                // comment for why that's a safe, deliberate simplification.
                cacStartSeq( CAC_SEQ_SFX, CAC_MELODY_HIT );
            }
        }
    }
}

// GrowEnteringLine (the actual "did the line just close a loop" handler) -
// defined after cacEraseItems since it calls it. See this file's own
// header comment on the confirmed-dead `old` parameter of upstream's own
// Trace() that cacTrace() above already dropped.
//
// **A real rendering-fidelity bug, found and fixed during a later
// verification pass**: upstream sets `LineRed = true; DrawAll();` (a real
// hardware flush - the red line is genuinely visible on the physical OLED
// at that exact moment) then calls the *blocking* `Sound_Beep()`, which
// holds that red frame on screen for real wall-clock time while it plays,
// then processes `EraseItems()` (each captured item's own blocking
// `Sound_Hit()` holding the still-red frame visible further still), and
// only resets `LineRed = false` at the very end - with no further
// `DrawAll()` afterward, so the red line stays visible on real hardware
// until the *next* natural redraw. This port originally fired the beep
// non-blocking (correctly, per this file's own header comment on that
// simplification) but then, since nothing gates *rendering itself* the
// same way a blocking call would, immediately reset `cacLineRed = false`
// and returned - all within the SAME real engine tick whose own single
// `cacRender()` call happens only once, at the very end of
// `cacUpdatePlaying()`. The net effect: the red highlight was computed
// into `cacVVram` and then unconditionally overwritten before the frame
// was ever actually pushed to the screen - the "you just closed a loop"
// visual feedback was completely invisible, even though item capture/
// scoring/wall-formation all still worked correctly underneath it.
// **Fixed** by deferring the final cleanup (`cacLineRed = false;
// cacTrace( true ); cacLinePrevX/Y = ...;`) instead of running it
// synchronously here - `cacLineRed` is left `true` (and the just-closed
// boundary cells are left un-erased, so they keep rendering as red line
// glyphs) and `cacLineFlashHold` is armed instead; `cacUpdatePlaying()`
// holds the game genuinely paused (matching upstream's own real
// single-threaded freeze during its blocking sound calls - no monster
// movement, no time decrement, no death/clear checks) for
// `CAC_LINE_FLASH_FRAMES` real frames with the red line actually
// rendered, then performs the deferred cleanup on the frame the hold
// expires. The fixed hold duration is a deliberate, documented
// approximation of upstream's own variable-length real-hardware pause
// (Sound_Beep's own duration, plus one Sound_Hit-length pause per
// captured item) rather than an attempt to reproduce it exactly note-for-
// note - matching this project's own established "downsample a variable
// real-hardware-timed sequence to a fixed, clearly-visible hold" precedent
// used elsewhere (e.g. CAC_STATE_CLEAR_WAIT's own fixed 10-frame pause).
void cacGrowEnteringLine( int direction )
{
    int old, mask, cell, idx;

    cacLinePrevX = cacLineLastX;
    cacLinePrevY = cacLineLastY;
    cacLineLastX = cacLineLastX + cacDirectionElements[ direction ];
    cacLineLastY = cacLineLastY + cacDirectionElements[ direction + 1 ];

    old = cacSetLineCell( cacLineLastX, cacLineLastY, cacEnteringBytes[ direction >> 1 ] );
    if( old != 0 )
    {
        if( cacLineFirstX != cacLineLastX || cacLineFirstY != cacLineLastY )
          mask = cacTrace( true );
        else
          mask = 0xff;

        idx = cacLineLastY * CAC_MAP_WIDTH + cacLineLastX;
        cell = cacCellMap[ idx ] & mask;
        cacCellMap[ idx ] = cell;
        cacDrawAll();
        cacLineFirstX = cacLineLastX;
        cacLineFirstY = cacLineLastY;
        cacTrace( false );
        cacLineRed = true;
        cacDrawAll();
        cacStartSeq( CAC_SEQ_SFX, CAC_MELODY_BEEP );
        cacEraseItems();
        // Deferred: cacLineRed stays true (and the boundary stays
        // un-erased) so the red flash is genuinely visible for a real
        // held duration - see the cleanup in cacUpdatePlaying().
        cacLineFlashHold = CAC_LINE_FLASH_FRAMES;
    }
}


// -----------------------------------------------------------------------------
//   Actor.cpp
// -----------------------------------------------------------------------------

void cacSetDirection( CacActor* pActor, int direction )
{
    pActor->status = ( pActor->status & ~CAC_ACTOR_DIRECTION_MASK ) | direction;
    pActor->dx = cacDirectionElements[ direction ];
    pActor->dy = cacDirectionElements[ direction + 1 ];
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

void cacShowMan()
{
    int pattern;
    pattern = ( ( cacMan.status & CAC_ACTOR_PATTERN_MASK ) << 2 ) + CAC_CHAR_MAN;
    cacShowSprite( &cacMan.mv, pattern );
}

void cacInitMan()
{
    cacMan.mv.sprite = CAC_SPRITE_MAN;
    cacMan.status = CAC_ACTOR_LIVE | CAC_DIR_RIGHT;
    cacMan.dx = 0;
    cacMan.dy = 0;
    cacLocateMovable( &cacMan.mv, cacStageStart[ cacStageIndex ] );
    cacLineFirstX = cacMan.mv.x >> CAC_MAP_SHIFT;
    cacLineLastX = cacLineFirstX;
    cacLinePrevX = cacLineLastX;
    cacLineFirstY = cacMan.mv.y >> CAC_MAP_SHIFT;
    cacLineLastY = cacLineFirstY;
    cacLinePrevY = cacLineLastY;
    cacShowMan();
}

bool cacManCanMove( int direction )
{
    int dx, dy, x, y, cell;
    dx = cacDirectionElements[ direction ];
    dy = cacDirectionElements[ direction + 1 ];
    x = ( cacMan.mv.x >> CAC_MAP_SHIFT ) + dx;
    y = ( cacMan.mv.y >> CAC_MAP_SHIFT ) + dy;
    if( x == cacLinePrevX && y == cacLinePrevY ) return false;
    cell = cacGetCell( x, y );
    return ( cell & CAC_CELL_MASK ) == 0;
}

void cacMoveMan()
{
    if( ( ( cacMan.mv.x | cacMan.mv.y ) & CAC_MAP_MASK ) == 0 )
    {
        bool heldDir[4];
        int newDirection, i, oldDirection;
        bool committed;

        heldDir[0] = isLeftPressed();
        heldDir[1] = isRightPressed();
        heldDir[2] = isUpPressed();
        heldDir[3] = isDownPressed();

        newDirection = 0;
        committed = false;
        for( i = 0; i < 4; i = i + 1 )
        {
            if( heldDir[ i ] )
            {
                if( cacManCanMove( newDirection ) )
                {
                    cacSetDirection( &cacMan, newDirection );
                    committed = true;
                }
                else
                {
                    oldDirection = cacMan.status & CAC_ACTOR_DIRECTION_MASK;
                    if( cacManCanMove( oldDirection ) )
                      committed = true;
                }
            }
            if( committed ) break;
            newDirection = newDirection + 2;
        }
        if( !committed )
        {
            cacMan.dx = 0;
            cacMan.dy = 0;
        }
    }

    {
        int seq;
        cacMan.mv.x = cacMan.mv.x + cacMan.dx;
        cacMan.mv.y = cacMan.mv.y + cacMan.dy;
        seq = ( ( cacMan.mv.x + cacMan.mv.y + CAC_COORD_RATE / 2 ) >> CAC_COORD_SHIFT ) & 1;
        cacMan.status = ( cacMan.status & ~CAC_ACTOR_SEQ_MASK ) | seq;
    }
    cacShowMan();

    if( cacMan.dx != 0 || cacMan.dy != 0 )
    {
        if( ( ( cacMan.mv.x | cacMan.mv.y ) & CAC_MAP_MASK ) == CAC_COORD_RATE )
          cacGrowLeavingLine( cacMan.status & CAC_ACTOR_DIRECTION_MASK );
        else if( ( ( cacMan.mv.x | cacMan.mv.y ) & CAC_MAP_MASK ) == 0 )
          cacGrowEnteringLine( cacMan.status & CAC_ACTOR_DIRECTION_MASK );
    }

    if( cacIsNear( &cacMan.mv, &cacIncreaser ) )
    {
        cacHideIncreaser();
        cacStartSeq( CAC_SEQ_SFX, CAC_MELODY_UP );
        cacRemainCount = cacRemainCount + 1;
        cacPrintStatus();
    }
}

// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

void cacShowMonster( CacActor* pMonster )
{
    int pattern;
    pattern = ( ( pMonster->status & CAC_ACTOR_PATTERN_MASK ) << 2 ) + CAC_CHAR_MONSTER;
    cacShowSprite( &pMonster->mv, pattern );
}

bool cacIsNearToMonster( CacActor* pActor, int dx, int dy )
{
    int x, y, xx, yy, i, hitRange;
    hitRange = 2;
    x = pActor->mv.x + dx;
    y = pActor->mv.y + dy;
    for( i = 0; i < CAC_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( cacMonsters[ i ].status & CAC_ACTOR_LIVE ) != 0 )
        {
            if( pActor != &cacMonsters[ i ] )
            {
                xx = cacMonsters[ i ].mv.x;
                yy = cacMonsters[ i ].mv.y;
                if( x + hitRange >= xx && xx + hitRange >= x &&
                    y + hitRange >= yy && yy + hitRange >= y )
                  return true;
            }
        }
    }
    return false;
}

bool cacMonsterCanMove( CacActor* pMonster, int direction )
{
    int dx, dy, x, y, xMod, yMod;
    dx = cacDirectionElements[ direction ];
    dy = cacDirectionElements[ direction + 1 ];
    if( cacIsNearToMonster( pMonster, dx, dy ) ) return false;

    x = pMonster->mv.x;
    y = pMonster->mv.y;
    xMod = x & 1;
    yMod = y & 1;
    if( xMod == 0 )
    {
        if( yMod == 0 )
          return ( cacGetCell( ( x >> 1 ) + dx, ( y >> 1 ) + dy ) & CAC_CELL_MASK ) == 0;
        if( dx == 0 )
          return ( cacGetCell( x >> 1, ( y + dy ) >> 1 ) & ( CAC_CELL_MASK | CAC_LINE_LEFT | CAC_LINE_RIGHT ) ) == 0;
        x = x >> 1;
        y = y >> 1;
        x = x + dx;
        return cacGetCell( x, y ) == 0 && cacGetCell( x, y + 1 ) == 0;
    }
    if( yMod == 0 )
    {
        if( dy == 0 )
          return ( cacGetCell( ( x + dx ) >> 1, y >> 1 ) & ( CAC_CELL_MASK | CAC_LINE_TOP | CAC_LINE_BOTTOM ) ) == 0;
        x = x >> 1;
        y = y >> 1;
        y = y + dy;
        return cacGetCell( x, y ) == 0 && cacGetCell( x + 1, y ) == 0;
    }
    x = x >> 1;
    y = y >> 1;
    if( dy == 0 )
    {
        x = x + dx;
        return
            ( cacGetCell( x, y ) & ( CAC_CELL_MASK | CAC_LINE_BOTTOM ) ) == 0 &&
            ( cacGetCell( x, y + 1 ) & ( CAC_CELL_MASK | CAC_LINE_TOP ) ) == 0;
    }
    y = y + dy;
    return
        ( cacGetCell( x, y ) & ( CAC_CELL_MASK | CAC_LINE_RIGHT ) ) == 0 &&
        ( cacGetCell( x + 1, y ) & ( CAC_CELL_MASK | CAC_LINE_LEFT ) ) == 0;
}

void cacDecideDirection( CacActor* pMonster )
{
    int directions[4];
    int verticalDirectionIndex, horizontalDirectionIndex;
    int i, direction;

    verticalDirectionIndex = 0;
    horizontalDirectionIndex = 0;

    if( cacAbs( cacMan.mv.x, pMonster->mv.x ) > cacAbs( cacMan.mv.y, pMonster->mv.y ) )
    {
        if( cacMan.mv.x < pMonster->mv.x )
        {
            if( pMonster->dx <= 0 )
            {
                directions[0] = CAC_DIR_LEFT;
                directions[3] = CAC_DIR_RIGHT;
                verticalDirectionIndex = 1;
            }
            else
            {
                directions[2] = CAC_DIR_RIGHT;
                directions[3] = CAC_DIR_LEFT;
                verticalDirectionIndex = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                directions[0] = CAC_DIR_RIGHT;
                directions[3] = CAC_DIR_LEFT;
                verticalDirectionIndex = 1;
            }
            else
            {
                directions[2] = CAC_DIR_LEFT;
                directions[3] = CAC_DIR_RIGHT;
                verticalDirectionIndex = 0;
            }
        }
        if( ( cacMan.mv.y < pMonster->mv.y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            directions[ verticalDirectionIndex ] = CAC_DIR_UP;
            verticalDirectionIndex = verticalDirectionIndex + 1;
            directions[ verticalDirectionIndex ] = CAC_DIR_DOWN;
        }
        else
        {
            directions[ verticalDirectionIndex ] = CAC_DIR_DOWN;
            verticalDirectionIndex = verticalDirectionIndex + 1;
            directions[ verticalDirectionIndex ] = CAC_DIR_UP;
        }
    }
    else
    {
        if( cacMan.mv.y < pMonster->mv.y )
        {
            if( pMonster->dy <= 0 )
            {
                directions[0] = CAC_DIR_UP;
                directions[3] = CAC_DIR_DOWN;
                horizontalDirectionIndex = 1;
            }
            else
            {
                directions[2] = CAC_DIR_DOWN;
                directions[3] = CAC_DIR_UP;
                horizontalDirectionIndex = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                directions[0] = CAC_DIR_DOWN;
                directions[3] = CAC_DIR_UP;
                horizontalDirectionIndex = 1;
            }
            else
            {
                directions[2] = CAC_DIR_UP;
                directions[3] = CAC_DIR_DOWN;
                horizontalDirectionIndex = 0;
            }
        }
        // Upstream compares `Man._.x < pMonster->_.y` here too (mixing X
        // and Y) - a real upstream quirk, matching gameCracky.c's own
        // already-documented identical quirk in its own DecideDirection
        // (both games share this exact bug, coming from the same "cate"
        // engine template) - kept exactly as-is, matching this project's
        // own "preserve a faithful, even if odd, upstream comparison
        // rather than silently fixing it" precedent.
        if( ( cacMan.mv.x < pMonster->mv.y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            directions[ horizontalDirectionIndex ] = CAC_DIR_LEFT;
            horizontalDirectionIndex = horizontalDirectionIndex + 1;
            directions[ horizontalDirectionIndex ] = CAC_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalDirectionIndex ] = CAC_DIR_RIGHT;
            horizontalDirectionIndex = horizontalDirectionIndex + 1;
            directions[ horizontalDirectionIndex ] = CAC_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        if( cacMonsterCanMove( pMonster, direction ) )
        {
            cacSetDirection( pMonster, direction );
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void cacInitMonsters()
{
    int count, i, sprite;
    count = cacStageEnemyCount[ cacStageIndex ];
    sprite = CAC_SPRITE_MONSTER;
    for( i = 0; i < count; i = i + 1 )
    {
        cacMonsters[ i ].status = CAC_ACTOR_LIVE;
        cacMonsters[ i ].mv.sprite = sprite;
        sprite = sprite + 1;
    }
    for( i = count; i < CAC_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        cacMonsters[ i ].status = 0;
        cacMonsters[ i ].mv.sprite = sprite;
        cacHideSprite( sprite );
        sprite = sprite + 1;
    }
    cacMonsterClock = 0;
}

void cacStartMonsters()
{
    int i;
    cacMonsterCount = 0;
    for( i = 0; i < CAC_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( cacMonsters[ i ].status & CAC_ACTOR_LIVE ) != 0 )
        {
            cacMonsters[ i ].status = CAC_ACTOR_LIVE;
            cacLocateMovable( &cacMonsters[ i ].mv, cacStageEnemies[ cacStageIndex ][ i ] );
            cacDecideDirection( &cacMonsters[ i ] );
            cacShowMonster( &cacMonsters[ i ] );
            cacMonsterCount = cacMonsterCount + 1;
        }
    }
}

void cacMoveMonsters()
{
    int i, seq;
    for( i = 0; i < CAC_MONSTER_SLOT_COUNT; i = i + 1 )
    {
        if( ( cacMonsters[ i ].status & CAC_ACTOR_LIVE ) != 0 )
        {
            if( ( cacMonsters[ i ].mv.x & CAC_COORD_MASK ) == 0 &&
                ( cacMonsters[ i ].mv.y & CAC_COORD_MASK ) == 0 )
              cacDecideDirection( &cacMonsters[ i ] );

            if( cacMonsters[ i ].dx != 0 || cacMonsters[ i ].dy != 0 )
            {
                cacMonsters[ i ].mv.x = cacMonsters[ i ].mv.x + cacMonsters[ i ].dx;
                cacMonsters[ i ].mv.y = cacMonsters[ i ].mv.y + cacMonsters[ i ].dy;
                seq = ( ( cacMonsters[ i ].mv.x + cacMonsters[ i ].mv.y + CAC_COORD_RATE / 2 ) >> CAC_COORD_SHIFT ) & 1;
                cacMonsters[ i ].status = ( cacMonsters[ i ].status & ~CAC_ACTOR_SEQ_MASK ) | seq;
            }
            cacShowMonster( &cacMonsters[ i ] );
            if( cacIsNear( &cacMonsters[ i ].mv, &cacMan.mv ) )
              cacMan.status = cacMan.status & ~CAC_ACTOR_LIVE;
        }
    }
    cacMonsterClock = cacMonsterClock + 1;
}


// -----------------------------------------------------------------------------
//   Stage.cpp - InitStage() / InitTrying()
// -----------------------------------------------------------------------------

void cacInitStage()
{
    // Upstream cycles through Stages[] repeatedly past CurrentStage=7 (the
    // game never stops the player from continuing past the last stage) -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching gameCracky.c's own identical precedent.
    int i, j;
    i = 0;
    j = 0;
    while( i < cacCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= CAC_STAGE_COUNT )
          j = 0;
    }
    cacStageIndex = j;
    cacInitItems();
    cacInitMonsters();
}

void cacInitTrying()
{
    int i, pCell, pByte, b, count, cell;

    // Defensive reset - cacLineFlashHold should always already be 0 by
    // the time a fresh try/stage begins (the hold always fully expires
    // before any state transition can reach here), but reset it
    // explicitly anyway rather than relying on that being true.
    cacLineFlashHold = 0;

    cacStageTime = 60;
    i = cacStageEnemyCount[ cacStageIndex ];
    do
    {
        cacStageTime = cacStageTime + 30;
        i = i - 1;
    } while( i != 0 );

    cacHideAllSprites();
    // ClearScreen() upstream - most of the screen doesn't need an explicit
    // clear here since every real frame is already fully redrawn from
    // cacCellMap/cacVVram - but cacStatusChar (an accumulate-only write
    // grid, same architecture as gameCracky.c's own crkStatusChar) needs
    // an explicit clear here, or leftover title-screen text (CACORM/MINI/
    // INUFUTO 2026/START/CONTINUE) would persist into gameplay - the same
    // real, user-report-driven bug gameCracky.c's own crkInitTrying()
    // already found and fixed once, applied here proactively from the
    // start.
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          cacStatusChar[ i ][ j ] = 0;
    }
    cacOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in cacUpdateTitle()) - matches cacOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches cacInitTrying() without going through that transition first.
    cacFullWidthText = false;
    cacPrintStatus();

    pCell = 0;
    pByte = 0;
    b = cacStageBytes[ cacStageIndex ][ pByte ];
    pByte = pByte + 1;
    count = 8;
    for( i = 0; i < CAC_MAP_HEIGHT * CAC_MAP_WIDTH; i = i + 1 )
    {
        if( ( b & 1 ) != 0 )
          cell = CAC_CELL_WALL;
        else
          cell = CAC_CELL_SPACE;
        cacCellMap[ pCell ] = cell;
        pCell = pCell + 1;
        b = b >> 1;
        count = count - 1;
        if( count == 0 )
        {
            b = cacStageBytes[ cacStageIndex ][ pByte ];
            pByte = pByte + 1;
            count = 8;
        }
    }

    cacInitMan();
    cacDrawItems();
    cacStartMonsters();
    cacInitPoints();
    cacInitIncreaser();
    cacLineRed = false;
    cacDrawFence();
    cacDrawAll();
}


// -----------------------------------------------------------------------------
//   Rendering: VVram/status -> real screen bytes
// -----------------------------------------------------------------------------

// Reproduces upstream's own VVramToVram()/SendUL() nibble-interleaving
// exactly, and gameCracky.c's own crkComposeRawByte() shape exactly - see
// this file's own header comment for the full derivation. No hardware
// orientation transform: drawn directly at its own (col,page).
//
// **OR-combines the VVram-derived map byte with the status/title-text
// byte instead of choosing one exclusively**, matching gameCracky.c's own
// fix exactly: now that cacBeginTitle() draws the real "Cacorm" pixel-art
// logo directly into cacVVram (real hardware pages 1-2 only - see that
// function's own comment), and every status/title-text element (SCORE/
// MINI/INUFUTO 2026/START/CONTINUE/lives) is printed on pages 0/3/5/6/7,
// the two are always disjoint by page - this can never actually blend two
// real, distinct pieces of content together, it just lets both coexist in
// one composed byte instead of one silently excluding the other.
int cacComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < CAC_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = cacVVram[ rawPage * 2 ][ mapX ];
        lower = cacVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = cacCharPattern[ upper * 2 + 0 ];
            lowerByte = cacCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = cacCharPattern[ upper * 2 + 0 ];
            lowerByte = cacCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = cacCharPattern[ upper * 2 + 1 ];
            lowerByte = cacCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = cacCharPattern[ upper * 2 + 1 ];
            lowerByte = cacCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !cacFullWidthText && rawCol < CAC_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // cacStatusChar's own full-width indexing directly - no "subtract the
    // map width" local-offset math needed, since rawCol/4 already lands on
    // the correct real column either way (whether this is the
    // cacFullWidthText title path using the whole range, or the normal
    // gameplay path where rawCol is already >= 96).
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = cacStatusChar[ rawPage ][ charCol ];
            textByte = cacAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

void cacRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( cacOverlayActive && page == cacOverlayPage &&
                col >= cacOverlayCol * 4 && col < cacOverlayCol * 4 + cacOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - cacOverlayCol * 4 ) / 4;
                sub = ( col - cacOverlayCol * 4 ) % 4;
                value = cacAsciiPattern[ cacAsciiIndex( cacOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = cacComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after a real user-supplied photo of the sibling
// gameCracky.c running on actual hardware proved the previous version of
// this function was simply wrong.** The earlier version believed
// upstream's own title-screen text collided with the SCORE/STAGE/TIME
// status labels and had to be trimmed/dropped to fit - "INUFUTO 2026" was
// cropped to "INUFUTO", "CONTINUE" was cropped to "CONTINU", and the full
// status (STAGE/TIME) was skipped entirely via a separate
// cacPrintStatusForTitle(). Re-reading upstream's real `Status.cpp`
// (`Title()`) line by line shows this diagnosis was backwards: none of
// that text ever collides with anything upstream, because upstream's own
// Vram address space is a genuinely wide 32-char-cell-per-page canvas
// (see cacStatusChar's own header comment) - the status labels occupy
// only columns 24-31 (upstream's own `LeftX=24`), and every piece of
// title-screen text sits at columns 2-23, well clear of them. The ROOT
// problem was this port's own `cacStatusChar` being modeled as an
// 8-column-wide grid in the first place - now fixed there, this function
// is rewritten to place everything at upstream's real, literal columns,
// with `cacFullWidthText=true` so cacComposeRawByte() renders the full
// canvas instead of just the narrow status zone.
void cacBeginTitle()
{
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    int sInufuto[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    int i;

    for( i = 0; i < CAC_VVRAM_HEIGHT; i = i + 1 )
    {
        int j;
        for( j = 0; j < CAC_VVRAM_WIDTH; j = j + 1 )
          cacVVram[ i ][ j ] = CAC_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          cacStatusChar[ i ][ j ] = 0;
    }
    cacOverlayActive = false;
    cacFullWidthText = true;
    cacHideAllSprites();

    // Reset stale per-life state from a previous game so the title screen
    // never shows a leftover life count from before a game over - the same
    // class of "stale status text persisting to the title screen" bug
    // gameCracky.c's own header comment documents and fixes for its own
    // TIME display.
    cacRemainCount = 0;
    // Proactively applying the identical lesson gameCracky.c's own TIME
    // display needed: upstream's own StageTime can be genuinely nonzero
    // at the `goto title;` reached via a monster-collision death mid-
    // countdown (not just the StageTime==0 time-up path), which would
    // otherwise leave a stale countdown value showing on a fresh title
    // screen. Reset here so the title screen always shows a fresh
    // "TIME 000" instead.
    cacStageTime = 0;
    cacPrintStatus();

    // **Restored, matching gameCracky.c's own identical fix**: this is
    // upstream's own real 5-cell "Cacorm" logo bitmap, drawn directly into
    // cacVVram from cacTitleBytes[] at its own real position (VVram row 2,
    // columns 2-21, i.e. real hardware pages 1-2 - matching upstream's
    // `Status.cpp` `Title()`'s own `VVram + VVramWidth*2 + TitleLeft`
    // starting offset exactly, TitleLeft=(24-4*5)/2=2). The earlier version
    // of this function replaced this with plain small text ("CACORM"),
    // reasoning it was "purely decorative" - wrong, matching the identical
    // mistake gameCracky.c's own header comment documents: it's the actual
    // title wordmark, meant to be the single biggest, most prominent
    // element on the whole screen, not a throwaway detail.
    // `cacComposeRawByte()` was updated to OR-combine this VVram content
    // with cacStatusChar's own text layer rather than choosing one
    // exclusively, since the two occupy disjoint page ranges by
    // construction (see that function's own comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 5; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                cacVVram[ 2 + row ][ 2 + ch * 4 + col ] = cacTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 17, INUFUTO 2026 at col 12,
    // START/CONTINUE at col 9 with the cursor at col 8) - all genuinely
    // clear of the status labels' own columns 24-31, so nothing here
    // needs cropping or relocating anymore.
    cacPrintS( 3, 17, sMini, 4 );
    cacPrintS( 7, 12, sInufuto, 12 );
    cacPrintS( 5, 9, sStart, 5 );
    cacPrintS( 6, 9, sContinue, 8 );

    cacSelection = 0;
    cacSelectionChanged = true;
    cacPrevLeft = false; cacPrevRight = false; cacPrevUp = false; cacPrevDown = false; cacPrevFire = false;
    cacState = CAC_STATE_TITLE;
}

void cacUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !cacPrevLeft ) || ( right && !cacPrevRight ) ||
                ( up && !cacPrevUp ) || ( down && !cacPrevDown ) );
    justFire = ( fire && !cacPrevFire );
    cacPrevLeft = left; cacPrevRight = right; cacPrevUp = up; cacPrevDown = down; cacPrevFire = fire;

    if( cacSelectionChanged )
    {
        cacSelectionChanged = false;
        if( cacSelection == 0 )
          cacPrintC( 5, 8, '>' );
        else
          cacPrintC( 5, 8, ' ' );
        if( cacSelection == 1 )
          cacPrintC( 6, 8, '>' );
        else
          cacPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        bool continuing;
        cacFullWidthText = false;
        continuing = ( cacSelection == 1 );
        cacScore = 0;
        if( !continuing )
          cacCurrentStage = 0;
        cacRemainCount = 3;
        cacInitStage();
        cacInitTrying();
        cacDrawAll();
        cacStartSeq( CAC_SEQ_JINGLE, CAC_MELODY_START );
        cacState = CAC_STATE_START_JINGLE;
        cacRender();
        return;
    }
    if( justDir )
    {
        cacSelection = cacSelection ^ 1;
        cacSelectionChanged = true;
    }
    cacRender();
}

void cacUpdateStartJingle()
{
    if( !cacSeqPlaying( CAC_SEQ_JINGLE ) )
    {
        cacStartBgm();
        cacMonsterNum = 0;
        cacTimeDenom = CAC_MAX_TIME_DENOM;
        cacState = CAC_STATE_PLAYING;
    }
    cacRender();
}

void cacBeginLose()
{
    cacStopBgm();
    cacAnimStep = 0;
    cacWaitFrames = 0;
    cacState = CAC_STATE_LOSE_ANIM;
}

void cacUpdateLoseAnim()
{
    int patterns[4];
    patterns[0] = CAC_CHAR_MAN + 2 * 4;
    patterns[1] = CAC_CHAR_MAN + 4 * 4;
    patterns[2] = CAC_CHAR_MAN + 0 * 4;
    patterns[3] = CAC_CHAR_MAN + 6 * 4;

    if( cacWaitFrames > 0 )
    {
        cacWaitFrames = cacWaitFrames - 1;
        cacRender();
        return;
    }

    cacShowSprite( &cacMan.mv, patterns[ cacAnimStep & 3 ] );
    cacDrawAll();
    cacStartSeq( CAC_SEQ_SFX, CAC_MELODY_LOOSE );
    cacAnimStep = cacAnimStep + 1;
    cacWaitFrames = cacNoteFrames( 1 );

    if( cacAnimStep >= 8 )
    {
        cacRemainCount = cacRemainCount - 1;
        if( cacRemainCount > 0 )
        {
            cacInitTrying();
            cacDrawAll();
            cacOverlayActive = false;
            cacStartSeq( CAC_SEQ_JINGLE, CAC_MELODY_START );
            cacState = CAC_STATE_START_JINGLE;
        }
        else
        {
            cacPrintGameOver();
            cacStartSeq( CAC_SEQ_JINGLE, CAC_MELODY_GAMEOVER );
            cacState = CAC_STATE_GAMEOVER_JINGLE;
        }
    }
    cacRender();
}

void cacUpdateGameOverJingle()
{
    if( !cacSeqPlaying( CAC_SEQ_JINGLE ) )
      cacBeginTitle();
    else
      cacRender();
}

void cacBeginClearWait()
{
    cacStopBgm();
    cacWaitFrames = 10;
    cacState = CAC_STATE_CLEAR_WAIT;
}

void cacUpdateClearWait()
{
    if( cacWaitFrames > 0 )
    {
        cacWaitFrames = cacWaitFrames - 1;
        cacRender();
        return;
    }
    cacStartSeq( CAC_SEQ_JINGLE, CAC_MELODY_CLEAR );
    cacState = CAC_STATE_CLEAR_JINGLE;
    cacRender();
}

void cacUpdateClearJingle()
{
    if( !cacSeqPlaying( CAC_SEQ_JINGLE ) )
    {
        cacWaitFrames = 0;
        cacState = CAC_STATE_BONUS_TALLY;
    }
    cacRender();
}

void cacUpdateBonusTally()
{
    if( cacWaitFrames > 0 )
    {
        cacWaitFrames = cacWaitFrames - 1;
        cacRender();
        return;
    }

    if( cacStageTime >= CAC_BONUS_RATE )
    {
        cacAddScore( 5 );
        cacStageTime = cacStageTime - CAC_BONUS_RATE;
        cacPrintTime();
        cacStartSeq( CAC_SEQ_SFX, CAC_MELODY_BEEP );
        cacWaitFrames = cacNoteFrames( 1 );
        cacRender();
        return;
    }

    cacStageTime = 0;
    cacPrintStatus();
    cacCurrentStage = cacCurrentStage + 1;
    cacInitStage();
    cacInitTrying();
    cacDrawAll();
    cacStartSeq( CAC_SEQ_JINGLE, CAC_MELODY_START );
    cacState = CAC_STATE_START_JINGLE;
    cacRender();
}

void cacUpdatePlaying()
{
    // Holding the just-closed-loop red-line flash visible - see
    // cacGrowEnteringLine()'s own header comment. Everything else (Man/
    // monster movement, timers, death/clear checks) is genuinely frozen
    // for the duration, matching upstream's own real single-threaded
    // freeze while its equivalent blocking sound calls play.
    if( cacLineFlashHold > 0 )
    {
        cacLineFlashHold = cacLineFlashHold - 1;
        if( cacLineFlashHold == 0 )
        {
            cacLineRed = false;
            cacTrace( true );
            cacLinePrevX = cacLineLastX;
            cacLinePrevY = cacLineLastY;
            cacDrawAll();
        }
        cacRender();
        return;
    }

    cacTickCounter = cacTickCounter + 1;
    if( cacTickCounter < CAC_TICK_DIVISOR )
    {
        cacRender();
        return;
    }
    cacTickCounter = 0;

    cacUpdatePoints();
    cacMoveMan();
    if( cacLineFlashHold > 0 )
    {
        // A loop closed on this exact tick (cacMoveMan() -> ... ->
        // cacGrowEnteringLine() just armed the hold above) - render the
        // now-red frame and defer monster movement/timers/death-or-
        // clear checks to the tick right after the hold finishes,
        // rather than running them this same instant the way a naive
        // port would.
        cacRender();
        return;
    }
    if( cacMonsterNum >= 0 )
    {
        cacMoveMonsters();
        cacMonsterNum = cacMonsterNum - 10;
    }
    cacMonsterNum = cacMonsterNum + 6;

    cacTimeDenom = cacTimeDenom - 1;
    if( cacTimeDenom == 0 )
    {
        cacStageTime = cacStageTime - 1;
        cacTimeDenom = CAC_MAX_TIME_DENOM;
        cacPrintTime();
        if( cacStageTime == 0 )
        {
            cacPrintTimeUp();
            cacDrawAll();
            cacRender();
            cacBeginLose();
            return;
        }
    }

    cacDrawAll();

    if( ( cacMan.status & CAC_ACTOR_LIVE ) == 0 )
    {
        cacRender();
        cacBeginLose();
        return;
    }

    if( cacItemCount == 0 )
    {
        cacRender();
        cacBeginClearWait();
        return;
    }

    cacRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameCacorm_init()
{
    int i;

    cacScore = 0;
    cacCurrentStage = 0;
    cacRemainCount = 3;
    cacStageTime = 0;
    cacRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        cacSeqActive[ i ] = 0;
        cacSeqMelody[ i ] = CAC_MELODY_NONE;
    }
    cacOverlayActive = false;
    cacTickCounter = 0;
    cacLineFlashHold = 0;

    cacBeginTitle();
}

void gameCacorm_update()
{
    cacAdvanceSound();

    if( cacState == CAC_STATE_TITLE )
      cacUpdateTitle();
    else if( cacState == CAC_STATE_START_JINGLE )
      cacUpdateStartJingle();
    else if( cacState == CAC_STATE_PLAYING )
      cacUpdatePlaying();
    else if( cacState == CAC_STATE_LOSE_ANIM )
      cacUpdateLoseAnim();
    else if( cacState == CAC_STATE_GAMEOVER_JINGLE )
      cacUpdateGameOverJingle();
    else if( cacState == CAC_STATE_CLEAR_WAIT )
      cacUpdateClearWait();
    else if( cacState == CAC_STATE_CLEAR_JINGLE )
      cacUpdateClearJingle();
    else if( cacState == CAC_STATE_BONUS_TALLY )
      cacUpdateBonusTally();
}
