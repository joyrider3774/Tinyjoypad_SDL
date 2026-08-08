#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// Tiny Blocks (RobotMasterC, license "None specified" - confirmed via the
// GitHub API, `"license": null`, no LICENSE file anywhere in the repo).
// Credited "ROBOTMASTERC" in the menu (the repo owner's own GitHub
// handle, no real name stated anywhere in the repo or its README) - the
// same "known-but-unstated-license, credit the real handle" treatment
// already used for Datacute/Sunpazed/Hoangminh5210119.
//
// **The menu title, this file's own name, and every mention in this
// project's own documentation deliberately avoid the trademarked genre
// name the upstream repo itself is titled after** (the trademarked name
// itself, deliberately not spelled out even here) - that name is a
// registered trademark, the same reasoning and treatment already applied
// to Falling Blocks and Blocks Gold. Unlike those two, this game's own
// code never actually draws that word on screen at all (confirmed by
// reading the full source - no font/text-drawing calls exist anywhere in
// it), so there was no on-screen string to rename, only the repo/file/menu
// naming itself.
//
// Found via a driver-header-based discovery approach, at direct user
// request: rather than searching by genre name, enumerate the actual
// `#include` driver-header filenames already used across every game
// staged in `more games/` (`FastTinyDriver.h`, `ssd1306xled.h`,
// `Tiny4kOLED.h`, etc) and search for those specific filenames instead -
// this repo turned up via a `"FastTinyDriver.h" github` search that
// surfaced an unrelated Hack Club "Summer of Making" student devlog
// mentioning it, which led to the real repo (github.com/RobotMasterC/
// <repo name omitted here, same reasoning as above>) once traced through.
// Confirmed by reading the full 457-line source directly (not assumed
// from the devlog's own summary) to be a genuine, complete, independently-
// written clone - its own from-scratch piece table, board representation,
// rotation/wall-kick logic, and level/line-clear system share no code with
// any of this project's other three already-shipped games of the same
// genre (Falling Blocks, Blocks Gold - both Andy-Jackson-lineage; ATtiny
// Tetromino - sunpazed/jfoucher lineage) - the same "verify via real diff/
// reading before dismissing on genre alone" discipline this project has
// applied to every prior candidate. A 4th entry in an already-well-covered
// genre, ported specifically because the user confirmed it was worth it
// after reviewing this exact distinction (own codebase, plus a genuinely
// unique hybrid input scheme - see below).
//
// Real hardware: ATtiny85 + a 128x32 I2C OLED (not 128x64 - confirmed
// via the repo's own README BOM) + 3 physical buttons, built on
// `Tiny4kOLED.h` (the same datacute library already used by TinyBullsAndCows
// and ATtiny Tetromino) - needed no new shim.
//
// **The board is genuinely rendered rotated 90 degrees from a normal
// top-down view - the exact same structural shape already solved for
// ATtiny Tetromino** (itself also a 128x32/Tiny4kOLED-family game).
// Traced the real byte layout directly from `drawBoard()`/`drawNext()`
// rather than assumed: the board's own Y axis (gravity, `current.y`,
// 32 rows tall) maps to real screen COLUMNS (each logical row occupies
// 3 consecutive physical columns, starting at physical column 31), while
// the board's own X axis (native left/right, `current.x`) maps to real
// screen PAGE+BIT position (the vertical axis, via `screenX = col*3-2`
// then split into `page=screenX/8`/`bit=screenX%8`). On an unrotated
// Vircon32 screen, this means a piece genuinely falls LEFT-TO-RIGHT
// across the screen, and upstream's own `LEFT_BTN`/`RIGHT_BTN` (which
// respectively decrement/increment `current.x`) visually move a piece
// UP/DOWN, not left/right (increasing `current.x` increases `screenX`,
// which increases the page/bit position - and increasing page/bit
// position is visually further DOWN the screen in standard SSD1306
// addressing).
//
// **Controls were deliberately remapped to match what the player
// actually sees, exactly mirroring ATtiny Tetromino's own already-
// proven solution to the identical rotation** rather than re-deriving a
// new scheme from scratch: Up/Down trigger upstream's own `LEFT_BTN`/
// `RIGHT_BTN` logic respectively (`isUpPressed()` -> `canMoveLeft()`/
// `current.x--`, since that's what visually moves the piece up on this
// unrotated screen; `isDownPressed()` -> the mirror), Fire rotates
// (`ROTATE_BTN`), and Left/Right both trigger the fast-drop gesture
// described below (matching ATtiny Tetromino's own "Left/Right both
// soft-drop" choice for the same reason - there's no single obviously-
// correct button for accelerating a sideways-visual fall, so both
// directions do it).
//
// Upstream's own `analogRead(0) < 950` check (fed directly into
// `fallDelay`, bypassing the level-based table entirely whenever true)
// is genuinely unusual - the BOM states only 3 physical buttons, and no
// 4th button pin is ever configured anywhere in the file, yet this
// reads a raw analog value on a pin with no declared button behind it.
// Read literally as intended: a real, if minimally-documented, "hold to
// drop faster" gesture via *some* physical input wired to that pin in
// the real hardware (unclear from the repo alone whether it's a 4th
// button, a pot, or something else) - ported as a genuine gesture rather
// than dropped as inert, mapped to Left/Right per the paragraph above.
//
// **Upstream's own bit-packed board (`boardPixels`, 1 bit per cell,
// addressed via `pixelPos/8`/`pixelPos%8`) was ported as a plain
// `int[32][16]` grid instead** (`tnbBoard`), matching this project's own
// established precedent for every prior game of this genre with a bit-
// packed board (Falling Blocks' `bool[10][24]`, ATtiny Tetromino's
// `bool[16][24]`) - no benefit to bit-packing on this platform, and it
// avoids re-deriving whether this specific packing scheme carries any of
// the AVR-implicit-shift risks already documented extensively elsewhere
// in this project, by simply not needing any shift arithmetic on the
// board data at all.
//
// A genuine, faithfully-preserved upstream rendering quirk, not
// "corrected": `drawBoard()` only ever draws the LEFT border of the
// playfield as a real visible line (a solid column at physical position
// 30) - the RIGHT border (upstream's own `col >= 11` cells, 5 columns
// wide) is never rendered at all, even though it's real, solid,
// collision-active board data (`boardPixels` is initialized with those
// cells set to 1 in `setup()`, and every collision check reads them
// correctly) - just invisible on screen. Ported exactly as observed:
// the right wall stops pieces correctly, but nothing is drawn there.
//
// Upstream's own 7-piece bag-pick trick (`nextId = random(8); if
// (nextId==7) nextId = random(7);`, and a fuller `... || nextId==
// current.id ...` variant at the mid-game refill site) is ported
// verbatim via the shared `arand()` helper rather than simplified to a
// direct `arand(7)` - preserves upstream's own real (if slightly
// unusual) distribution technique exactly, including the fact that the
// two call sites use genuinely different conditions (the initial setup
// pick has no "don't repeat the current piece" guard, only the mid-game
// refill does).
//
// Real `millis()`-gated timers (the 100ms move-repeat cooldown, the
// 150ms rotate-repeat cooldown, and the full per-level `fallDelay`
// table, 10ms-720ms) were all converted to plain frame-counted
// equivalents (`60fps` throughout, e.g. 100ms -> 6 frames, 150ms -> 9
// frames) rather than an accumulator - exact real-time correspondence
// isn't critical for these arbitrary hobby-project tuning values, and a
// plain frame-count comparison is simpler and lower-risk than porting a
// real-millisecond `unsigned long` timer model. The very fastest fall-
// speed tiers (10ms/20ms) both round down to a 1-frame minimum, which is
// the fastest this engine's own 60fps tick rate can represent regardless
// - functionally still "drops every tick," matching upstream's own
// near-instant intent at those extreme levels.
//
// No score exists in this game at all (only `linesCount`/`gameLevel`,
// confirmed via a full read - no `score` variable anywhere in the
// source) and no EEPROM usage exists upstream either - left fully
// session-only, matching this project's own precedent for other
// beyond-scope ports with nothing meaningful to persist.
//
// A genuine attract screen and a real game-over screen (with a restart
// gesture) were added - upstream has neither: it starts playing
// immediately from `setup()`, and its own real game-over path is a
// literal AVR `sleep_mode()` call with no wake/restart path at all other
// than a hardware reset, which has no meaningful equivalent on this
// engine. Both new screens stay confined to the same physical pages 0-3
// (32 rows) the real gameplay itself occupies, for visual consistency -
// pages 4-7 are implicitly left blank every frame (the shared
// `tnbDisplayClear()` call already zeroes the whole buffer, and nothing
// ever writes into those pages), the same "confine everything to the
// game's own real physical footprint" approach already used for ATtiny
// Tetromino/Tiny Bulls And Cows/Laser Pong.

// -----------------------------------------------------------------------------
// Data tables (piece shapes extracted via a small Python script directly
// against the real source, byte-count verified against the 7x16=112
// values actually present before use; font data reused verbatim from
// this project's own already-verified myfont-family extraction, since
// upstream itself draws no text of its own at all)
// -----------------------------------------------------------------------------

int tnbPieces[112] = {
0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,
0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,
0,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,
0,0,0,0,0,1,1,0,1,1,0,0,0,0,0,0,
0,0,0,0,1,1,1,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,1,0,0,1,1,0,0,0,0,0,0,
0,1,0,0,0,1,0,0,0,1,1,0,0,0,0,0,
};

int tnbFontData[6144] = {
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,1,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,1,0,
1,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,1,0,1,0,0,0,
0,1,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,0,1,1,0,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,1,0,1,0,
0,0,1,0,1,0,0,1,0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,
0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,1,0,0,0,
0,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,1,1,1,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,1,0,0,
0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,1,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,0,1,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,1,1,1,1,0,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,
0,1,1,0,0,0,1,1,0,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,
0,0,1,0,0,1,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,1,1,1,1,0,0,0,
0,0,1,1,1,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,1,1,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,1,0,0,1,0,1,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,1,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,1,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,1,
0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,
0,1,0,0,0,0,0,1,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,0,0,
0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,0,0,
0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,1,1,0,0,0,
0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,0,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,1,0,
0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,1,1,0,1,0,0,
0,0,1,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,1,0,0,
0,0,0,1,0,0,0,0,0,1,1,0,0,1,1,0,0,0,1,0,1,1,1,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,1,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,1,0,1,1,0,1,0,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,
0,1,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,
0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,1,1,1,0,1,0,0,1,0,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,1,1,1,0,1,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,1,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,0,1,1,1,0,0,0,0,1,1,1,0,1,0,0,0,1,0,1,1,0,0,0,0,1,0,0,0,0,0,
0,0,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,1,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,1,0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,1,1,0,0,1,0,0,0,0,1,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,
0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,0,1,1,0,0,0,1,0,0,1,0,0,0,1,0,1,1,0,1,0,
0,0,0,1,1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,
0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,
0,0,0,1,1,0,0,0,0,0,0,1,1,0,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,1,0,
0,1,1,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// -----------------------------------------------------------------------------
// Font index (same 16x6-glyph, 8x8-cell layout/table as every other
// myfont-family port in this project)
// -----------------------------------------------------------------------------

int tnbFontIndex( int ch )
{
    if( ch == 32 ) return 99; // space
    if( ch == 33 ) return 0; if( ch == 34 ) return 1; if( ch == 35 ) return 2;
    if( ch == 36 ) return 3; if( ch == 37 ) return 4; if( ch == 38 ) return 5;
    if( ch == 39 ) return 6; if( ch == 40 ) return 7; if( ch == 41 ) return 8;
    if( ch == 42 ) return 9; if( ch == 43 ) return 10; if( ch == 44 ) return 11;
    if( ch == 45 ) return 12; if( ch == 46 ) return 13; if( ch == 47 ) return 14;
    if( ch == 60 ) return 27; if( ch == 61 ) return 28; if( ch == 62 ) return 28;
    if( ch >= 48 && ch <= 57 ) return 15 + ( ch - 48 );
    if( ch >= 65 && ch <= 90 ) return 32 + ( ch - 65 );
    if( ch >= 97 && ch <= 122 ) return 65 + ( ch - 97 );
    return 0; // '!' fallback, matching every other port's own default
}

// -----------------------------------------------------------------------------
// Pixel framebuffer + draw primitives
// -----------------------------------------------------------------------------

int tnbFrameBuffer[1024];

void tnbDisplayClear( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) tnbFrameBuffer[ i ] = 0;
}

void tnbDrawChar( int ch, int x, int y )
{
    int col, row, glyph, idx, bit, py, line, gcol, rowBase, pageBase;
    glyph = tnbFontIndex( ch );
    if( glyph == 99 ) return; // space - a real no-op, nothing to draw
    line = ( glyph / 16 ) * 8;
    gcol = ( glyph % 16 ) * 8;
    for( row = 0; row < 8; row++ )
    {
        py = y + row;
        if( py < 0 || py > 63 ) continue;
        bit = 1 << ( py & 7 );
        rowBase = ( line + row ) * 128 + gcol;
        pageBase = ( py >> 3 ) * 128 + x;
        for( col = 0; col < 8; col++ )
        {
            if( x + col < 0 || x + col > 127 ) continue;
            if( !tnbFontData[ rowBase + col ] ) continue;
            idx = pageBase + col;
            tnbFrameBuffer[ idx ] = tnbFrameBuffer[ idx ] | bit;
        }
    }
}

void tnbDrawString( char* text, int x, int y )
{
    int i = 0;
    while( text[ i ] != 0 )
    {
        tnbDrawChar( text[ i ], x + ( 8 * i ), y );
        i = i + 1;
    }
}

void tnbDrawCentered( char* text, int y )
{
    int len = 0;
    while( text[ len ] != 0 ) len = len + 1;
    int x = ( 128 - len * 8 ) / 2;
    tnbDrawString( text, x, y );
}

// -----------------------------------------------------------------------------
// Game state - direct port of upstream's own globals
// -----------------------------------------------------------------------------

enum TnbState
{
    TNB_STATE_ATTRACT = 0,
    TNB_STATE_PLAY = 1,
    TNB_STATE_GAME_OVER = 2
};

#define TNB_BOARD_ROWS 32
#define TNB_BOARD_COLS 16
#define TNB_MOVE_COOLDOWN 6   // 100ms @ 60fps, matching upstream's own lastMove+100 gate
#define TNB_ROTATE_COOLDOWN 9 // 150ms @ 60fps, matching upstream's own lastRotate+150 gate
#define TNB_TICK_DIVISOR 2     // 30fps cap, added at direct user request

int tnbState;
int tnbTickCounter;
int tnbKeyUp, tnbKeyDown, tnbKeyFastDrop, tnbKeyFire, tnbFireEdge, tnbPrevFire;

int tnbBoard[ TNB_BOARD_ROWS ][ TNB_BOARD_COLS ];

int tnbCurX, tnbCurY, tnbCurId;
int tnbCurShape[16];
int tnbNextShape[16];
int tnbNextId;

int tnbGameLevel;
int tnbLinesCount;
int tnbFallTimer;
int tnbMoveCooldown;
int tnbRotateCooldown;

// -----------------------------------------------------------------------------
// Game logic - direct port of loadPiece()/canMoveDown/Left/Right()/
// rotatePiece()/updateFallDelay()
// -----------------------------------------------------------------------------

void tnbLoadPiece( int* dest, int pieceIdx )
{
    int i;
    for( i = 0; i < 16; i++ ) dest[ i ] = tnbPieces[ pieceIdx * 16 + i ];
}

int tnbFallDelayFrames( void )
{
    if( tnbGameLevel == 1 ) return 19;
    else if( tnbGameLevel == 2 ) return 18;
    else if( tnbGameLevel == 3 ) return 17;
    else if( tnbGameLevel == 4 ) return 16;
    else if( tnbGameLevel == 5 ) return 14;
    else if( tnbGameLevel == 6 ) return 13;
    else if( tnbGameLevel == 7 ) return 12;
    else if( tnbGameLevel == 8 ) return 11;
    else if( tnbGameLevel == 9 ) return 10;
    else if( tnbGameLevel == 10 ) return 5;
    else if( tnbGameLevel == 13 ) return 2;
    else if( tnbGameLevel == 16 ) return 1;
    else if( tnbGameLevel >= 19 ) return 1;
    else return 22;
}

int tnbCanMoveDown( void )
{
    int r, c, row, col;
    for( r = 0; r < 4; r++ )
      for( c = 0; c < 4; c++ )
      {
        if( !tnbCurShape[ r * 4 + c ] ) continue;
        row = tnbCurY + r + 1;
        col = tnbCurX + c;
        if( row < 0 || row >= TNB_BOARD_ROWS || col < 0 || col >= TNB_BOARD_COLS ) return 0;
        if( tnbBoard[ row ][ col ] ) return 0;
      }
    return 1;
}

int tnbCanMoveLeft( void )
{
    int r, c, row, col;
    for( r = 0; r < 4; r++ )
      for( c = 0; c < 4; c++ )
      {
        if( !tnbCurShape[ r * 4 + c ] ) continue;
        row = tnbCurY + r;
        col = tnbCurX + c - 1;
        if( row < 0 || row >= TNB_BOARD_ROWS || col < 0 || col >= TNB_BOARD_COLS ) return 0;
        if( tnbBoard[ row ][ col ] ) return 0;
      }
    return 1;
}

int tnbCanMoveRight( void )
{
    int r, c, row, col;
    for( r = 0; r < 4; r++ )
      for( c = 0; c < 4; c++ )
      {
        if( !tnbCurShape[ r * 4 + c ] ) continue;
        row = tnbCurY + r;
        col = tnbCurX + c + 1;
        if( row < 0 || row >= TNB_BOARD_ROWS || col < 0 || col >= TNB_BOARD_COLS ) return 0;
        if( tnbBoard[ row ][ col ] ) return 0;
      }
    return 1;
}

void tnbRotateShape( int* oldShape, int* newShape )
{
    int r, c;
    for( r = 0; r < 4; r++ )
      for( c = 0; c < 4; c++ )
        newShape[ c * 4 + 3 - r ] = oldShape[ r * 4 + c ];
}

void tnbInitBoard( void )
{
    int row, col;
    for( row = 0; row < TNB_BOARD_ROWS; row++ )
      for( col = 0; col < TNB_BOARD_COLS; col++ )
        if( col == 0 || col >= 11 || row == TNB_BOARD_ROWS - 1 ) tnbBoard[ row ][ col ] = 1;
        else tnbBoard[ row ][ col ] = 0;
}

// -----------------------------------------------------------------------------
// Rendering - direct port of drawBoard()/drawNext()'s own real byte-
// layout math (see header comment for the full rotation derivation),
// writing straight into tnbFrameBuffer instead of an intermediate
// oledBuffer + hardware blit.
// -----------------------------------------------------------------------------

void tnbDrawBoardAndPiece( void )
{
    int row, col, p, k, physCol, idx, v, screenX, bitpos, page, bit, shapeIdx, cellSet;
    int byteVal[4];

    for( p = 0; p < 4; p++ ) tnbFrameBuffer[ 30 + p * 128 ] = 0xFF; // left border column

    for( row = 0; row < TNB_BOARD_ROWS; row++ )
    {
        byteVal[0] = 0; byteVal[1] = 0; byteVal[2] = 0; byteVal[3] = 0;
        for( col = 1; col < 11; col++ )
        {
            cellSet = tnbBoard[ row ][ col ];
            if( col >= tnbCurX && row >= tnbCurY && col < tnbCurX + 4 && row < tnbCurY + 4 )
            {
                shapeIdx = ( row - tnbCurY ) * 4 + ( col - tnbCurX );
                if( tnbCurShape[ shapeIdx ] ) cellSet = 1;
            }
            if( !cellSet ) continue;
            screenX = col * 3 - 2;
            for( k = 0; k < 3; k++ )
            {
                bitpos = screenX + k;
                page = bitpos / 8;
                bit = bitpos % 8;
                if( page >= 0 && page < 4 ) byteVal[ page ] = byteVal[ page ] | ( 1 << bit );
            }
        }
        physCol = 31 + row * 3;
        for( p = 0; p < 4; p++ )
        {
            idx = physCol + p * 128;
            v = byteVal[ p ];
            tnbFrameBuffer[ idx ] = v;
            tnbFrameBuffer[ idx + 1 ] = v;
            tnbFrameBuffer[ idx + 2 ] = v;
        }
    }
}

void tnbDrawNext( void )
{
    int row, localCol, p, k, physCol, idx, v, screenX, bitpos, page, bit, shapeIdx;
    int byteVal[4];

    for( row = 0; row < 4; row++ )
    {
        byteVal[0] = 0; byteVal[1] = 0; byteVal[2] = 0; byteVal[3] = 0;
        for( localCol = 0; localCol < 4; localCol++ )
        {
            shapeIdx = row * 4 + localCol;
            if( !tnbNextShape[ shapeIdx ] ) continue;
            screenX = ( localCol + 4 ) * 3 - 2;
            for( k = 0; k < 3; k++ )
            {
                bitpos = screenX + k;
                page = bitpos / 8;
                bit = bitpos % 8;
                if( page >= 0 && page < 4 ) byteVal[ page ] = byteVal[ page ] | ( 1 << bit );
            }
        }
        physCol = 5 + row * 3;
        for( p = 0; p < 4; p++ )
        {
            idx = physCol + p * 128;
            v = byteVal[ p ];
            tnbFrameBuffer[ idx ] = v;
            tnbFrameBuffer[ idx + 1 ] = v;
            tnbFrameBuffer[ idx + 2 ] = v;
        }
    }
}

void tnbDisplayPlaying( void )
{
    tnbDisplayClear();
    tnbDrawBoardAndPiece();
    tnbDrawNext();
}

// -----------------------------------------------------------------------------
// Attract / Game Over screens - added, upstream has neither (see header
// comment)
// -----------------------------------------------------------------------------

void tnbDrawAttract( void )
{
    tnbDisplayClear();
    tnbDrawCentered( "TINY BLOCKS", 4 );
    tnbDrawCentered( "BY ROBOTMASTERC", 14 );
    tnbDrawCentered( "PRESS FIRE", 24 );
}

void tnbDrawGameOver( void )
{
    tnbDisplayClear();
    tnbDrawCentered( "GAME OVER", 4 );

    char numText[16];
    char line[24];
    strcpy( line, "LINES " );
    itoa( tnbLinesCount, numText, 10 );
    strcat( line, numText );
    tnbDrawCentered( line, 14 );

    tnbDrawCentered( "PRESS FIRE", 24 );
}

// -----------------------------------------------------------------------------
// State setup / transitions
// -----------------------------------------------------------------------------

void tnbBeginAttract( void )
{
    tnbState = TNB_STATE_ATTRACT;
}

void tnbBeginPlay( void )
{
    tnbInitBoard();
    tnbGameLevel = 0;
    tnbLinesCount = 0;
    tnbFallTimer = 0;
    tnbMoveCooldown = 0;
    tnbRotateCooldown = 0;

    tnbNextId = arand( 8 );
    if( tnbNextId == 7 ) tnbNextId = arand( 7 );
    tnbCurId = tnbNextId;
    tnbLoadPiece( tnbCurShape, tnbNextId );
    tnbCurX = 4;
    tnbCurY = 0;

    tnbNextId = arand( 8 );
    if( tnbNextId == 7 ) tnbNextId = arand( 7 );
    tnbLoadPiece( tnbNextShape, tnbNextId );

    tnbState = TNB_STATE_PLAY;
}

void tnbBeginGameOver( void )
{
    tnbState = TNB_STATE_GAME_OVER;
}

// -----------------------------------------------------------------------------
// Per-tick playing update - direct port of loop()'s own real body, real-
// millisecond gates converted to frame counts (see header comment)
// -----------------------------------------------------------------------------

void tnbUpdatePlaying( void )
{
    if( tnbMoveCooldown > 0 ) tnbMoveCooldown = tnbMoveCooldown - 1;
    else
    {
        if( tnbKeyUp && tnbCanMoveLeft() )
        {
            tnbCurX = tnbCurX - 1;
            tnbMoveCooldown = TNB_MOVE_COOLDOWN;
        }
        else if( tnbKeyDown && tnbCanMoveRight() )
        {
            tnbCurX = tnbCurX + 1;
            tnbMoveCooldown = TNB_MOVE_COOLDOWN;
        }
    }

    if( tnbRotateCooldown > 0 ) tnbRotateCooldown = tnbRotateCooldown - 1;
    if( tnbKeyFire && tnbRotateCooldown == 0 )
    {
        int oldShape[16];
        int newShape[16];
        int i;
        for( i = 0; i < 16; i++ ) oldShape[ i ] = tnbCurShape[ i ];
        tnbRotateShape( oldShape, newShape );
        for( i = 0; i < 16; i++ ) tnbCurShape[ i ] = newShape[ i ];

        int oldX = tnbCurX;
        int movedLeft = 0;
        int movedRight = 0;
        if( !tnbCanMoveRight() ) { tnbCurX = tnbCurX - 1; movedLeft = 1; }
        if( !tnbCanMoveLeft() ) { tnbCurX = tnbCurX + 1; movedRight = 1; }

        if( movedLeft && movedRight )
        {
            for( i = 0; i < 16; i++ ) tnbCurShape[ i ] = oldShape[ i ];
            tnbCurX = oldX;
        }
        else
        {
            if( movedLeft ) { while( !tnbCanMoveRight() ) tnbCurX = tnbCurX - 1; tnbCurX = tnbCurX + 1; }
            if( movedRight ) { while( !tnbCanMoveLeft() ) tnbCurX = tnbCurX + 1; tnbCurX = tnbCurX - 1; }
        }

        tnbRotateCooldown = TNB_ROTATE_COOLDOWN;
    }

    int fallFrames = tnbFallDelayFrames();
    if( tnbKeyFastDrop ) fallFrames = 1;

    tnbFallTimer = tnbFallTimer + 1;
    if( tnbFallTimer >= fallFrames )
    {
        tnbFallTimer = 0;

        if( tnbCanMoveDown() )
        {
            tnbCurY = tnbCurY + 1;
        }
        else
        {
            int r, c;
            for( r = 0; r < 4; r++ )
              for( c = 0; c < 4; c++ )
                if( tnbCurShape[ r * 4 + c ] ) tnbBoard[ tnbCurY + r ][ tnbCurX + c ] = 1;

            int numLines = 0;
            int row, col, row2;
            for( row = 0; row < TNB_BOARD_ROWS - 1; row++ )
            {
                int full = 1;
                for( col = 0; col < TNB_BOARD_COLS; col++ )
                  if( !tnbBoard[ row ][ col ] ) { full = 0; break; }
                if( full )
                {
                    numLines = numLines + 1;
                    for( row2 = row; row2 >= 2; row2 = row2 - 1 )
                      for( col = 0; col < TNB_BOARD_COLS; col++ )
                        tnbBoard[ row2 ][ col ] = tnbBoard[ row2 - 1 ][ col ];
                }
            }
            if( numLines > 0 ) tnbLinesCount = tnbLinesCount + numLines;

            if( tnbCurY <= 1 )
            {
                tnbBeginGameOver();
                return;
            }

            tnbCurX = 4;
            tnbCurY = 0;
            tnbCurId = tnbNextId;
            tnbLoadPiece( tnbCurShape, tnbNextId );

            tnbNextId = arand( 8 );
            if( tnbNextId == 7 || tnbNextId == tnbCurId ) tnbNextId = arand( 7 );
            tnbLoadPiece( tnbNextShape, tnbNextId );
        }

        if( tnbLinesCount > ( tnbGameLevel + 1 ) * 10 ) tnbGameLevel = tnbGameLevel + 1;
    }
}

// -----------------------------------------------------------------------------
// Per-tick state dispatch
// -----------------------------------------------------------------------------

void tnbUpdateAttract( void )
{
    tnbDrawAttract();
    if( tnbFireEdge ) tnbBeginPlay();
}

void tnbUpdatePlay( void )
{
    tnbUpdatePlaying();
    if( tnbState == TNB_STATE_PLAY ) tnbDisplayPlaying();
}

void tnbUpdateGameOver( void )
{
    tnbDrawGameOver();
    if( tnbFireEdge ) tnbBeginAttract();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void tnbRenderFrame( void )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
      for( col = 0; col < 128; col++ )
        md_drawColumn( col, page, tnbFrameBuffer[ col + ( page * 128 ) ] );
}

void gameTinyBlocks_init( void )
{
    InitTinyJoypad();
    tnbPrevFire = 0;
    tnbFireEdge = 0;
    tnbBeginAttract();
    tnbDisplayClear();
}

void gameTinyBlocks_update( void )
{
    // 30fps cap, added at direct user request - upstream itself has no
    // real fixed rate of its own (its own move/rotate/fall gates are all
    // real-millisecond-based, already ported as frame-counted
    // equivalents assuming a 60fps tick - see the header comment on
    // those constants). Gates the whole tick (input, game logic, and
    // the redraw together), matching this project's own generally-
    // preferred "gate everything" shape. Every existing frame-counted
    // constant in the file (TNB_MOVE_COOLDOWN, TNB_ROTATE_COOLDOWN, the
    // whole tnbFallDelayFrames() table) is deliberately left unrescaled,
    // matching this project's own standing "one divisor, no dual
    // bookkeeping" practice - they simply now take twice as long in
    // real time.
    tnbTickCounter = tnbTickCounter + 1;
    if( tnbTickCounter < TNB_TICK_DIVISOR ) return;
    tnbTickCounter = 0;

    int fireNow = isFirePressed();
    tnbFireEdge = fireNow && !tnbPrevFire;
    tnbPrevFire = fireNow;

    tnbKeyUp = isUpPressed();
    tnbKeyDown = isDownPressed();
    tnbKeyFastDrop = isLeftPressed() || isRightPressed();
    tnbKeyFire = isFirePressed();

    if( tnbState == TNB_STATE_ATTRACT ) tnbUpdateAttract();
    else if( tnbState == TNB_STATE_PLAY ) tnbUpdatePlay();
    else tnbUpdateGameOver();

    tnbRenderFrame();
}

void gameTinyBlocks_forceRedraw( void )
{
    if( tnbState == TNB_STATE_ATTRACT ) tnbDrawAttract();
    else if( tnbState == TNB_STATE_PLAY ) tnbDisplayPlaying();
    else tnbDrawGameOver();
    tnbRenderFrame();
}
