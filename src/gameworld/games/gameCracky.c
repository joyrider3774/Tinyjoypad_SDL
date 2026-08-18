// =============================================================================
// CRACKY mini (inufuto, UIAPduino+SSD1306 edition, license "None specified" -
// GitHub reports no LICENSE file for `UIAPduino_cracky`) - a platformer:
// climb ladders and cross floors across a 12-column x 4-floor stage,
// collecting every star ("item") to clear it while dodging chasing
// monsters; cracked floor tiles vanish (become impassable) the one time you
// step off them. 10 hand-authored stages, 3 lives, real persistent hi-score
// tracked in-session (upstream has no EEPROM at all - a CH32V003 RISC-V
// microcontroller, not AVR, so this project's own `eeprom_*` shim/avrCompat
// widening aren't relevant here the way they are for every ATtiny85 port).
//
// Picked directly at user request after confirming feasibility: real
// SSD1306 display (streamed one raw byte at a time, `SendOledData()`, no
// framebuffer - the exact same model `md_drawColumn()` already handles) and
// a real, explicit 60Hz SysTick-based frame limiter (`Timer.cpp`'s own
// `kTimerHz=60`, `WaitTimer(t)`) rather than a bare loop whose speed is a
// direct function of the CH32V003's own 48MHz clock - so no Tiny-Arkanoid-
// style "silently N times too fast/slow" risk here despite the unrelated
// host architecture (RISC-V, not AVR). Only 4 directions + 1 action button
// (`ScanKeys.h`'s own `Keys_Button0`) - a strict subset of what
// `tinyJoypadShim.h` already exposes, so no new shim primitive was needed
// (isFire2Pressed() simply goes unused here).
//
// **A hardware display-orientation transform was tried, then reverted on
// direct user report ("game is running inverted")** - `InitOled()` sends
// `OledCmd::RightToLeft` (SegRemap=0xA1) and `OledCmd::BottomToTop`
// (ComScanDec=0xC8), the exact same two register settings already found to
// need a real mirror-plus-bit-reversal fix once before in this project
// (SnakeGame85's own 180-degree-rotation bug) - a first attempt here
// applied that same fix by analogy. That turned out to be the wrong call:
// unlike SnakeGame85's case, these two settings on a lot of real SSD1306
// breakout modules exist to *compensate* for a physical panel-mounting
// quirk specific to that module (the glass panel bonded rotated 180
// degrees relative to the driver chip's own SEG0/COM0 pins) - a real-
// hardware correction with no equivalent thing to correct for in a
// software recreation, meaning the "fix" would reproduce a flip that was
// never actually visible on real UIAPduino hardware in the first place.
// Removed entirely (confirmed via live user testing, not just reverted on
// theory) - `crkComposeRawByte(col,page)` is now drawn directly at its own
// `(col,page)`, no mirroring, no per-bit reversal.
//
// **Rendering is a genuine two-level tile system, ported as a direct
// structural mirror rather than re-derived into a closed form** (the same
// "faithfully copy an intricate stateful algorithm's own shape" reasoning
// already used for Frogger's own row-buffer compositing in this project):
// `VVram` is a 24x16 logical glyph-index grid (`crkVVram`); `VVramToVram()`
// packs each *pair* of vertically-stacked VVram cells (`upper`/`lower`)
// into 4 real output bytes via a 2-byte-per-glyph `CharPattern` lookup and
// a specific nibble-interleaving (`SendUL()`'s own `(upper&0xF)|(lower<<4)`
// / `(upper>>4)|(lower&0xF0)` split) - reproduced in `crkComposeRawByte()`
// exactly as upstream computes it, not simplified, since getting the
// nibble packing subtly wrong would corrupt every tile on screen. Upstream
// additionally keeps a dirty-tracking `Backup[]` buffer to skip re-sending
// unchanged real I2C bytes (a real-hardware bandwidth optimization with no
// analogue here) - dropped, matching this project's own standing
// "always redraw the full frame, don't replicate a VRAM-persistence-
// reliant partial-redraw trick" precedent (the same class of bug already
// found and fixed in Pinball/Doc/Bert/Tris/Pipe/Plaque/SQuest/Frogger).
//
// **A related VRAM-persistence case handled deliberately, not by omission**:
// `PrintTimeUp()`/`PrintGameOver()` write their message text directly into
// real Vram bytes *inside the map area* (columns 32-95, bypassing the VVram
// grid entirely) - on real hardware this persists precisely because nothing
// else re-touches those exact bytes while `Backup[]`'s own dirty-check sees
// no change there. Since this port has no such passive persistence at all
// (a full VVram-driven redraw runs every tick), a literal port of those two
// functions would have the message appear for exactly one frame before the
// next full redraw silently erases it. Fixed with a small explicit overlay
// (`crkOverlayActive`/`crkOverlayText`/`crkOverlayPage`/`crkOverlayCol`) -
// during rendering, any raw (col,page) landing inside the overlay's own
// footprint draws from `crkAsciiPattern` instead of the VVram-derived byte,
// reproducing the same *visible* persistence upstream gets for free from
// real VRAM, without needing to replicate the dirty-tracking mechanism
// itself.
//
// **The blocking upstream control flow (goto-chained labels around one big
// do-while, several real `WaitMelody()`/`WaitTimer()` blocking waits)
// rewritten as an explicit frame-stepped state machine**, the same
// treatment every port in this project needs: CRK_STATE_TITLE (Title()'s
// own internal `while(true)` key-poll loop), CRK_STATE_START_JINGLE (the
// blocking `Sound_Start()` held before play begins), CRK_STATE_PLAYING (the
// main tick-gated loop), CRK_STATE_LOSE_ANIM (LooseMan()'s own 8-step
// blink-and-beep loop), CRK_STATE_GAMEOVER_JINGLE, CRK_STATE_CLEAR_WAIT
// (the real `WaitTimer(10)` pause), CRK_STATE_CLEAR_JINGLE, and
// CRK_STATE_BONUS_TALLY (the real `while(StageTime>=BonusRate){...
// Sound_Beep();}` bonus-countdown loop, each blocked by its own note -
// converted to one decrement+beep per real tick, matching this project's
// own HollowSeeker/Ardumania bonus-tally precedent).
//
// **Sound**: upstream's own `Sound.cpp` is a real 3-tone-channel + 1-noise-
// channel software mixer (a genuine tracker, `StartMelody()`/`StartBGM()`
// playing two independent melodies simultaneously) - far richer than this
// shim's own single-call `Sound(freq,dur)` AVR-buzzer wrapper, so every
// call here goes straight to `md_playTone(freqHz, durationSeconds)`
// instead (matching Astro Barrier's own precedent for a game with a real
// derived Hz formula) - safe to do since `md_playTone()` is itself already
// genuinely multi-voice (see this project's own CLAUDE.md write-up on why
// that fix was needed project-wide), so three simultaneous logical voices
// (one SFX cue + two BGM parts) never actually fight over one shared
// channel. Each melody is ported as upstream's own literal [duration,note]
// byte-pair data (byte-diff-extracted via script, not hand-copied) behind
// a small `crkMelodyLength()`/`crkMelodyValue()` id-based resolver (the
// same "resolve by id instead of storing a real pointer" pattern already
// established for Tiny Dungeon's own bitmap-array resolver) rather than an
// array of raw pointers, and three independent frame-stepped sequencer
// slots (0=one-shot SFX, 1=jingle/BGM-voice-A - reused for both, exactly
// like upstream's own channel 1, since the two uses never overlap in time;
// 2=BGM-voice-B) advance every real engine frame, completely independent
// of the coarser `CRK_TICK_DIVISOR`-gated gameplay tick - matching
// upstream's own real structure, where `SoundHandler()` runs off the same
// 60Hz SysTick interrupt as gameplay but is never itself throttled by the
// `Clock&7` gate. Each note's own real duration is derived directly from
// `SoundHandler()`'s own tempo formula (`Tempo=160`, a channel advances
// once every `(600/2)/160 = 1.875` real 60Hz ticks) rather than guessed -
// `crkNoteFrames(length) = round(length * 1.875)`.
//
// Movable coordinates (`Movable.x/y`) turned out to already be expressed in
// VVram-cell-grid units directly (`CoordShift=0`/`CoordRate=1` make
// upstream's own generic sub-cell-precision parameterization degenerate
// here - confirmed by tracing `LocateMovable()`'s own formula), not real
// pixel coordinates - so sprites compositing directly into `crkVVram` (the
// same grid the map itself uses) needed no extra scaling at all, simpler
// than it first looked from the class layout alone. Every genuinely
// degenerate-but-still-present upstream formula (`CoordShift`/`CoordRate`/
// `CoordMask`, `MaxTimeDenom`'s own `50/(8/CoordRate)`) is kept as the real
// expression rather than resolved to its current literal value, matching
// this project's own established "#defines should stay exactly as upstream
// wrote them, not silently pre-computed" preference.
//
// **This is the reference implementation for the whole inufuto "Cate
// engine" family** (Ultimate Mission, and every other CH32V003/UIAPduino
// game sharing this same VVram/Vram two-level tile renderer and frame-
// stepped sound sequencer) - ported first specifically so later Cate-
// engine games in this batch have an already-correct example to be
// diffed/cross-checked against, matching this project's own established
// practice of porting a family's first member carefully before batching
// the rest. Mechanically dialect-converted from the sibling
// tinyjoypad_vircon32 project's own already-correct, already user-
// verified-on-real-hardware gameCracky.c (see that project's own
// CLAUDE.md for the full porting/bug-fix history behind every finding
// documented above) - only `int[N] name` -> `int name[N]` array-
// declaration syntax and the two bare `struct` tags -> `typedef struct`
// needed changing; every string-shaped array in this file (crkOverlayText,
// the various `sScore`/`sStage`/`sMini`/`sContinue`/etc local buffers)
// holds individual `'A'`-style char constants, never a `"..."` string
// literal, so - unlike several earlier ports in this project's own
// history - none of them needed `int[]`->`char[]` widening; every value
// still round-trips through crkAsciiIndex()'s own int-comparison lookup
// exactly as upstream wrote it. No EEPROM/high-score persistence here,
// matching upstream having none at all (a CH32V003 RISC-V board, not
// AVR - see above). No forceRedraw needed either: every one of this
// state machine's own branches calls crkRender() unconditionally on
// every real tick it runs, so there's no "last frame's pixels are still
// correct, skip redrawing" case anywhere in this file for a resumed
// screen to miss.
// =============================================================================

#include "avrCompat.h"
#include "machineDependent.h"
#include "tinyJoypadShim.h"

// -----------------------------------------------------------------------------
//   Chars.h - glyph indices into crkCharPattern (map tiles) / crkAsciiPattern (text)
// -----------------------------------------------------------------------------

#define CRK_CHAR_SPACE 0x00
#define CRK_CHAR_CRACKED_FLOOR 0x10
#define CRK_CHAR_HARD_FLOOR 0x18
#define CRK_CHAR_LADDER_LEFT 0x19
#define CRK_CHAR_LADDER_RIGHT 0x1A
#define CRK_CHAR_MAN 0x1B
#define CRK_CHAR_MAN_LEFT 0x1B
#define CRK_CHAR_MAN_LEFT_STOP 0x1B
#define CRK_CHAR_MAN_LEFT0 0x1F
#define CRK_CHAR_MAN_LEFT1 0x23
#define CRK_CHAR_MAN_LEFT2 0x27
#define CRK_CHAR_MAN_RIGHT 0x2B
#define CRK_CHAR_MAN_RIGHT_STOP 0x2B
#define CRK_CHAR_MAN_RIGHT0 0x2F
#define CRK_CHAR_MAN_RIGHT1 0x33
#define CRK_CHAR_MAN_RIGHT2 0x37
#define CRK_CHAR_MAN_UPDOWN 0x3B
#define CRK_CHAR_MAN_UPDOWN0 0x3B
#define CRK_CHAR_MAN_UPDOWN1 0x3F
#define CRK_CHAR_MAN_LOOSE 0x43
#define CRK_CHAR_MAN_LOOSE0 0x43
#define CRK_CHAR_MAN_LOOSE1 0x47
#define CRK_CHAR_MAN_LOOSE2 0x4B
#define CRK_CHAR_MONSTER 0x4F
#define CRK_CHAR_MONSTER_LEFT 0x4F
#define CRK_CHAR_MONSTER_LEFT0 0x4F
#define CRK_CHAR_MONSTER_LEFT1 0x53
#define CRK_CHAR_MONSTER_RIGHT 0x57
#define CRK_CHAR_MONSTER_RIGHT0 0x57
#define CRK_CHAR_MONSTER_RIGHT1 0x5B
#define CRK_CHAR_MONSTER_UP 0x5F
#define CRK_CHAR_MONSTER_UP0 0x5F
#define CRK_CHAR_MONSTER_UP1 0x63
#define CRK_CHAR_MONSTER_DOWN 0x67
#define CRK_CHAR_MONSTER_DOWN0 0x67
#define CRK_CHAR_MONSTER_DOWN1 0x6B
#define CRK_CHAR_ITEM 0x6F
#define CRK_CHAR_END 0x73

// -----------------------------------------------------------------------------
//   ScanKeys.h - key bitmask constants (kept for structural fidelity, though
//   this port reads isLeftPressed()/etc directly rather than a combined mask)
// -----------------------------------------------------------------------------

#define CRK_KEYS_LEFT 0x01
#define CRK_KEYS_RIGHT 0x02
#define CRK_KEYS_UP 0x04
#define CRK_KEYS_DOWN 0x08
#define CRK_KEYS_BUTTON0 0x10

// -----------------------------------------------------------------------------
//   Movable.h
// -----------------------------------------------------------------------------

#define CRK_COORD_SHIFT 0
#define CRK_COORD_RATE ( 1 << CRK_COORD_SHIFT )
#define CRK_COORD_MASK ( CRK_COORD_RATE - 1 )

#define CRK_MOVABLE_LIVE 0x80
#define CRK_MOVABLE_FALL 0x40

typedef struct
{
    int x, y;
    int sprite;
    int status;
    int dx, dy;
} CrkMovable;

// -----------------------------------------------------------------------------
//   Stage.h
// -----------------------------------------------------------------------------

#define CRK_COLUMN_COUNT 12
#define CRK_FLOOR_COUNT 4
#define CRK_COLUMNS_PER_BYTE 2
#define CRK_COLUMN_WIDTH 2
#define CRK_FLOOR_HEIGHT 4
#define CRK_STAGE_WIDTH ( CRK_COLUMN_COUNT * CRK_COLUMN_WIDTH )
#define CRK_STAGE_HEIGHT ( CRK_FLOOR_COUNT * CRK_FLOOR_HEIGHT )
#define CRK_COLUMN_SHIFT 1
#define CRK_COLUMN_MASK ( CRK_COLUMN_WIDTH - 1 )
#define CRK_FLOOR_SHIFT 2
#define CRK_FLOOR_MASK ( CRK_FLOOR_HEIGHT - 1 )

#define CRK_CELL_SPACE 0x0
#define CRK_CELL_LOWER_MASK 0x3
#define CRK_CELL_CRACKED_FLOOR 0x1
#define CRK_CELL_HARD_FLOOR 0x3
#define CRK_CELL_LADDER_DOWN 0x2
#define CRK_CELL_UPPER_MASK 0xc
#define CRK_CELL_BROKEN_FLOOR 0x4
#define CRK_CELL_LADDER_UP 0x8
#define CRK_CELL_ITEM 0xc

#define CRK_STAGE_COUNT 10
#define CRK_MAX_MONSTER_COUNT 4
#define CRK_CELLMAP_BYTES ( ( CRK_COLUMN_COUNT / CRK_COLUMNS_PER_BYTE ) * CRK_FLOOR_COUNT )

// -----------------------------------------------------------------------------
//   VVram.h
// -----------------------------------------------------------------------------

#define CRK_VVRAM_WIDTH 24
#define CRK_VVRAM_HEIGHT 16

// -----------------------------------------------------------------------------
//   Sprite.h
// -----------------------------------------------------------------------------

#define CRK_SPRITE_MAN 0
#define CRK_SPRITE_MONSTER 1
#define CRK_SPRITE_COUNT 5
#define CRK_INVALID_CODE 255

// -----------------------------------------------------------------------------
//   Sound.h - NoteLength/Scale, ported as real expressions (not resolved to
//   their current literal values), matching upstream's own enum exactly.
// -----------------------------------------------------------------------------

#define CRK_N8 6
#define CRK_N8L 8
#define CRK_N8R 4
#define CRK_N8P ( CRK_N8 * 3 / 2 )
#define CRK_N4 ( CRK_N8 * 2 )
#define CRK_N4P ( CRK_N4 * 3 / 2 )
#define CRK_N2 ( CRK_N4 * 2 )
#define CRK_N2P ( CRK_N2 * 3 / 2 )
#define CRK_N1 ( CRK_N2 * 2 )
#define CRK_N16 ( CRK_N8 / 2 )

#define CRK_E2 1
#define CRK_F2 2
#define CRK_F2S 3
#define CRK_G2 4
#define CRK_G2S 5
#define CRK_A2 6
#define CRK_A2S 7
#define CRK_B2 8
#define CRK_C3 9
#define CRK_C3S 10
#define CRK_D3 11
#define CRK_D3S 12
#define CRK_E3 13
#define CRK_F3 14
#define CRK_F3S 15
#define CRK_G3 16
#define CRK_G3S 17
#define CRK_A3 18
#define CRK_A3S 19
#define CRK_B3 20
#define CRK_C4 21
#define CRK_C4S 22
#define CRK_D4 23
#define CRK_D4S 24
#define CRK_E4 25
#define CRK_F4 26
#define CRK_F4S 27
#define CRK_G4 28
#define CRK_G4S 29
#define CRK_A4 30
#define CRK_A4S 31
#define CRK_B4 32
#define CRK_C5 33
#define CRK_C5S 34
#define CRK_D5 35
#define CRK_D5S 36
#define CRK_E5 37
#define CRK_F5 38
#define CRK_F5S 39
#define CRK_G5 40

#define CRK_TEMPO 160

// Sound sequencer melody ids, resolved by crkMelodyLength()/crkMelodyValue()
// instead of a real pointer-per-channel (this project's own established
// "resolve by id" pattern, e.g. Tiny Dungeon's own bitmap-array resolver).
#define CRK_MELODY_NONE 0
#define CRK_MELODY_LOOSE 1
#define CRK_MELODY_HIT 2
#define CRK_MELODY_BEEP 3
#define CRK_MELODY_START 4
#define CRK_MELODY_CLEAR 5
#define CRK_MELODY_GAMEOVER 6
#define CRK_MELODY_BGM1 7
#define CRK_MELODY_BGM2 8

// -----------------------------------------------------------------------------
//   Data tables - byte-diff-extracted via a small Python script from the real
//   upstream source, not hand-copied.
// -----------------------------------------------------------------------------

// AsciiPattern - " 0123456789>ACEFGIMNOPRSTUV", 4 bytes/glyph, used for
// status text (SCORE/STAGE/TIME/lives/TITLE/GAME OVER/etc).
int crkAsciiPattern[108] = {
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

// CharPattern - 115 map-tile glyphs, 2 bytes/glyph (a 4x4 pixel block).
int crkCharPattern[230] = {
    0x00, 0x00, 0x33, 0x00, 0xcc, 0x00, 0xff, 0x00,
    0x00, 0x33, 0x33, 0x33, 0xcc, 0x33, 0xff, 0x33,
    0x00, 0xcc, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xcc,
    0x00, 0xff, 0x33, 0xff, 0xcc, 0xff, 0xff, 0xff,
    0xa6, 0xbd, 0xe5, 0xd3, 0xa6, 0x04, 0x60, 0xd3,
    0x86, 0x04, 0x00, 0xd2, 0x02, 0x00, 0x00, 0x50,
    0xff, 0xff, 0xf0, 0xaa, 0xaa, 0x0f, 0x80, 0xf5,
    0x7d, 0x08, 0x10, 0x3c, 0xc3, 0x01, 0x00, 0xf5,
    0xfd, 0x00, 0x90, 0x34, 0x43, 0x05, 0x00, 0x75,
    0x7d, 0x00, 0x00, 0xd0, 0xc1, 0x00, 0x00, 0xf5,
    0x7d, 0x00, 0x10, 0x2d, 0x43, 0x05, 0x80, 0xd7,
    0x5f, 0x08, 0x10, 0x3c, 0xc3, 0x01, 0x00, 0xdf,
    0x5f, 0x00, 0x50, 0x34, 0x43, 0x09, 0x00, 0xd7,
    0x57, 0x00, 0x00, 0x1c, 0x0d, 0x00, 0x00, 0xd7,
    0x5f, 0x00, 0x50, 0x34, 0xd2, 0x01, 0x80, 0xb3,
    0xbb, 0x04, 0x00, 0x35, 0xc3, 0x00, 0x40, 0xbb,
    0x3b, 0x08, 0x00, 0x3c, 0x53, 0x00, 0x4c, 0xac,
    0x8a, 0x44, 0x13, 0x53, 0x15, 0x22, 0x80, 0xc3,
    0x3c, 0x08, 0x10, 0xbe, 0xaf, 0x01, 0x44, 0xa8,
    0xca, 0xc8, 0x22, 0x51, 0x35, 0x32, 0xa8, 0xaf,
    0xef, 0x08, 0x10, 0x73, 0xbf, 0x00, 0x40, 0x4e,
    0xce, 0x00, 0x32, 0xf7, 0xff, 0x02, 0x80, 0xfe,
    0xfa, 0x8a, 0x00, 0xfb, 0x37, 0x01, 0x00, 0xec,
    0xe4, 0x04, 0x20, 0xff, 0x7f, 0x23, 0xe8, 0xef,
    0xef, 0x08, 0x30, 0xf7, 0x37, 0x00, 0xc0, 0xce,
    0xce, 0x00, 0x71, 0xff, 0x7f, 0x01, 0x80, 0xbe,
    0xbe, 0x8e, 0x00, 0x73, 0x7f, 0x03, 0x00, 0x6c,
    0x6c, 0x0c, 0x10, 0xf7, 0xff, 0x17, 0xc4, 0xfc,
    0xcc, 0x04, 0x40, 0x13, 0x43, 0x00,
};

// TitleBytes - upstream's own real "CRACKY" title-screen logo bitmap
// (Status.cpp's `Title()`), 6 letters x 4x4 VVram-cell glyph indices each
// (96 values total), byte-diff-verified against the real upstream source.
// Every value here is a valid index into crkCharPattern[]'s own "logo"
// range (indices 0-15, the first 32 bytes of that table) - the exact same
// shared block-pattern palette every other map tile in this game already
// draws through, just reused here to build a big pixel-art wordmark
// instead of a wall/floor tile. See crkBeginTitle()'s own comment for why
// this replaces the earlier plain-text "CRACKY" substitute.
int crkTitleBytes[96] = {
    0x00, 0x0e, 0x05, 0x0b, 0x0c, 0x03, 0x00, 0x00,
    0x04, 0x0b, 0x00, 0x0a, 0x00, 0x04, 0x05, 0x01,
    0x0c, 0x07, 0x05, 0x0b, 0x0c, 0x03, 0x08, 0x0f,
    0x0c, 0x07, 0x0f, 0x02, 0x04, 0x01, 0x04, 0x05,
    0x00, 0x0e, 0x0d, 0x02, 0x0c, 0x03, 0x00, 0x0f,
    0x0c, 0x07, 0x05, 0x0f, 0x04, 0x01, 0x00, 0x05,
    0x00, 0x0e, 0x05, 0x0b, 0x0c, 0x03, 0x00, 0x00,
    0x04, 0x0b, 0x00, 0x0a, 0x00, 0x04, 0x05, 0x01,
    0x0c, 0x03, 0x08, 0x07, 0x0c, 0x0b, 0x07, 0x00,
    0x0c, 0x03, 0x0d, 0x02, 0x04, 0x01, 0x00, 0x05,
    0x0c, 0x03, 0x0c, 0x03, 0x04, 0x0b, 0x0e, 0x01,
    0x00, 0x0c, 0x03, 0x00, 0x00, 0x04, 0x01, 0x00,
};

// Standard equal-tempered note frequencies, E2..G5 (Scale enum values 1-40).
int crkFrequencies[40] = {
    82, 87, 92, 98, 104, 110, 117, 123, 131, 139,
    147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440,
    466, 494, 523, 554, 587, 622, 659, 698, 740, 784,
};

int crkMelodyLoose[3] = { 1, CRK_A3, 0 };

int crkMelodyHit[17] = {
    1, CRK_F4, 1, CRK_G4, 1, CRK_A4, 1, CRK_B4, 1, CRK_C5,
    1, CRK_D5, 1, CRK_E5, 1, CRK_F5, 0,
};

int crkMelodyBeep[3] = { 1, CRK_A4, 0 };

int crkMelodyStart[27] = {
    CRK_N8, 0, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5,
    CRK_N4, CRK_G4, CRK_N4, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_D5, CRK_N8, CRK_C5,
    CRK_N4, CRK_D5, CRK_N4, CRK_E5, CRK_N1, CRK_C5, 0,
};

int crkMelodyClear[22] = {
    CRK_N8, CRK_C4, CRK_N8, CRK_E4, CRK_N8, CRK_G4, CRK_N8, CRK_D4, CRK_N8, CRK_F4,
    CRK_N8, CRK_A4, CRK_N8, CRK_E4, CRK_N8, CRK_G4, CRK_N8, CRK_B4, CRK_N4P, CRK_C5,
    0, 0,
};

int crkMelodyGameOver[21] = {
    CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_G4, CRK_N8, CRK_G4, CRK_N8, CRK_A4,
    CRK_N8, CRK_A4, CRK_N8, CRK_B4, CRK_N8, CRK_B4, CRK_N2P, CRK_C5, CRK_N4, 0,
    0,
};

int crkMelodyBgm1[105] = {
    CRK_N8, 0, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5,
    CRK_N4, CRK_G4, CRK_N4, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_D5, CRK_N8, CRK_C5,
    CRK_N4, CRK_D5, CRK_N4, CRK_E5, CRK_N8, 0, CRK_N8, CRK_C5, CRK_N8, CRK_C5,
    CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N4, CRK_D5, CRK_N4, CRK_F5, CRK_N8, CRK_F5,
    CRK_N8, CRK_E5, CRK_N8, CRK_C5, CRK_N4, CRK_C5, CRK_N4, CRK_D5, CRK_N8, 0,
    CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_C5, CRK_N4, CRK_G4,
    CRK_N4, CRK_C5, CRK_N8, CRK_C5, CRK_N8, CRK_D5, CRK_N8, CRK_C5, CRK_N4, CRK_D5,
    CRK_N4, CRK_E5, CRK_N4, CRK_F5, CRK_N4, CRK_F5, CRK_N4, CRK_E5, CRK_N4, CRK_E5,
    CRK_N4, CRK_D5, CRK_N8, CRK_D5, CRK_N4, CRK_E5, CRK_N8, CRK_E5, CRK_N8, CRK_D5,
    CRK_N8, CRK_C5, CRK_N4, CRK_C5, CRK_N4, CRK_C5, CRK_N8, CRK_D5, CRK_N4, CRK_D5,
    CRK_N4P, CRK_C5, CRK_N2P, 0, 255,
};

int crkMelodyBgm2[105] = {
    CRK_N8, CRK_C3, CRK_N4, 0, CRK_N4P, CRK_E3, CRK_N8, CRK_G3, CRK_N8, 0,
    CRK_N8, CRK_A2, CRK_N4, 0, CRK_N4P, CRK_C3, CRK_N8, CRK_E3, CRK_N8, 0,
    CRK_N8, CRK_D3, CRK_N4, 0, CRK_N4P, CRK_F3, CRK_N8, CRK_A3, CRK_N8, 0,
    CRK_N8, CRK_G2, CRK_N4, 0, CRK_N4P, CRK_B2, CRK_N8, CRK_D3, CRK_N8, 0,
    CRK_N8, CRK_C3, CRK_N4, 0, CRK_N4P, CRK_E3, CRK_N8, CRK_G3, CRK_N8, 0,
    CRK_N8, CRK_A2, CRK_N4, 0, CRK_N4P, CRK_C3, CRK_N8, CRK_E3, CRK_N8, 0,
    CRK_N8, CRK_F2, CRK_N4, 0, CRK_N4P, CRK_A2, CRK_N8, CRK_C3, CRK_N8, 0,
    CRK_N8, CRK_D3, CRK_N4, 0, CRK_N4P, CRK_E3, CRK_N8, CRK_A3, CRK_N8, 0,
    CRK_N8, CRK_F2, CRK_N4, 0, CRK_N8, CRK_F2, CRK_N8, CRK_G2, CRK_N8, 0,
    CRK_N8, CRK_B2, CRK_N8, CRK_D3, CRK_N8, CRK_C3, CRK_N4, 0, CRK_N4P, CRK_E3,
    CRK_N8, CRK_G3, CRK_N8, 0, 255,
};

// Stage data - flattened from upstream's own `struct Stage { start,
// itemCount, enemyCount, pEnemies, bytes[24] }` array + separate Enemies0-9
// arrays into parallel fixed arrays (avoids porting a struct-with-a-real-
// pointer-member, matching this project's own "flatten to plain arrays"
// precedent elsewhere).
int crkStageStart[10] = {
    ( 1 << 4 ) | 3, ( 9 << 4 ) | 3, ( 11 << 4 ) | 0, ( 1 << 4 ) | 0, ( 11 << 4 ) | 0,
    ( 2 << 4 ) | 3, ( 0 << 4 ) | 0, ( 2 << 4 ) | 3, ( 1 << 4 ) | 0, ( 3 << 4 ) | 0,
};
int crkStageItemCount[10] = { 10, 14, 11, 10, 15, 11, 13, 10, 11, 5 };
int crkStageEnemyCount[10] = { 1, 2, 2, 2, 3, 3, 3, 3, 4, 4 };

// Each stage's own enemy start bytes, packed the same ( pos << 4 ) | floor
// way as crkStageStart - up to CRK_MAX_MONSTER_COUNT(4) per stage.
int crkStageEnemies[10][4] = {
    { ( 10 << 4 ) | 0, 0, 0, 0 },
    { ( 8 << 4 ) | 0, ( 1 << 4 ) | 1, 0, 0 },
    { ( 5 << 4 ) | 0, ( 4 << 4 ) | 1, 0, 0 },
    { ( 11 << 4 ) | 0, ( 5 << 4 ) | 2, 0, 0 },
    { ( 0 << 4 ) | 0, ( 5 << 4 ) | 1, ( 6 << 4 ) | 2, 0 },
    { ( 2 << 4 ) | 0, ( 4 << 4 ) | 1, ( 10 << 4 ) | 1, 0 },
    { ( 10 << 4 ) | 0, ( 10 << 4 ) | 1, ( 8 << 4 ) | 3, 0 },
    { ( 9 << 4 ) | 0, ( 11 << 4 ) | 0, ( 0 << 4 ) | 2, 0 },
    { ( 6 << 4 ) | 0, ( 8 << 4 ) | 0, ( 11 << 4 ) | 0, ( 11 << 4 ) | 2 },
    { ( 6 << 4 ) | 0, ( 11 << 4 ) | 0, ( 9 << 4 ) | 1, ( 10 << 4 ) | 2 },
};

int crkStageBytes[10][24] = {
    {
        0xf2, 0x31, 0xf3, 0x11, 0x31, 0x23,
        0xfa, 0xff, 0x13, 0x13, 0x1f, 0xa1,
        0x3a, 0x3f, 0x13, 0x11, 0xf1, 0xa3,
        0x3b, 0x33, 0x33, 0xf3, 0x33, 0xbf,
    },
    {
        0xff, 0x12, 0x33, 0x33, 0x33, 0xff,
        0x31, 0x3a, 0x2f, 0x23, 0xff, 0x31,
        0xf2, 0xfb, 0xa3, 0xaf, 0x21, 0x1f,
        0xfb, 0xf3, 0xb3, 0xb3, 0xb3, 0xf3,
    },
    {
        0x2f, 0x11, 0x33, 0xf3, 0x3f, 0x31,
        0xa1, 0x32, 0x23, 0x3f, 0x21, 0x21,
        0xb1, 0xfa, 0xb3, 0x30, 0xb1, 0xa3,
        0x33, 0xfb, 0xff, 0xf3, 0x3f, 0xbf,
    },
    {
        0x30, 0xf1, 0x10, 0x12, 0xf1, 0x33,
        0xf3, 0x32, 0xf1, 0x3a, 0xf2, 0x12,
        0x11, 0x0a, 0x3f, 0x0b, 0x0b, 0x2b,
        0xff, 0x3b, 0xf3, 0x33, 0xf3, 0xb3,
    },
    {
        0x13, 0xff, 0xf3, 0x32, 0x11, 0x3f,
        0xff, 0x12, 0x31, 0xfa, 0x12, 0x1f,
        0x13, 0x0a, 0xff, 0x1b, 0x3a, 0x10,
        0xff, 0x3b, 0x3f, 0x33, 0xfb, 0xf3,
    },
    {
        0x12, 0x33, 0x2f, 0x13, 0xf1, 0x1f,
        0x3a, 0xf2, 0xa3, 0xf1, 0x31, 0x13,
        0x19, 0xfa, 0xa1, 0xf1, 0x10, 0x30,
        0x3f, 0x3b, 0xb3, 0xf3, 0xf3, 0x3f,
    },
    {
        0x33, 0x11, 0xf1, 0xf1, 0x11, 0x23,
        0x12, 0x21, 0xff, 0x31, 0x21, 0xb3,
        0xfa, 0xbf, 0x11, 0x33, 0xaf, 0xf1,
        0x3b, 0xff, 0xff, 0x33, 0xb3, 0xf3,
    },
    {
        0x10, 0x32, 0x11, 0x11, 0x31, 0x3f,
        0xf1, 0xfa, 0x2f, 0xf1, 0xf2, 0x10,
        0x33, 0x09, 0xa0, 0xf3, 0x0b, 0x13,
        0x33, 0x33, 0xb3, 0xf3, 0xf3, 0x3f,
    },
    {
        0x3f, 0x11, 0x10, 0x13, 0x33, 0x33,
        0x11, 0xff, 0xf3, 0xff, 0x1f, 0x21,
        0x33, 0x00, 0x11, 0xf2, 0xf1, 0xb1,
        0x33, 0xff, 0x33, 0x3b, 0x33, 0x33,
    },
    {
        0x1f, 0x31, 0x10, 0x13, 0xf1, 0x32,
        0x1f, 0x32, 0x10, 0x20, 0x31, 0x1b,
        0x30, 0x1a, 0x20, 0xb3, 0x31, 0x13,
        0x3f, 0x3b, 0xb3, 0xf3, 0x33, 0x33,
    },
};

// -----------------------------------------------------------------------------
//   Global state
// -----------------------------------------------------------------------------

int crkScore;
int crkHiScore;
int crkRemainCount;
int crkCurrentStage;
int crkStageTime;
int crkItemCount;
int crkClock;
int crkMonsterNum;
int crkTimeDenom;
int crkStageIndex;

#define CRK_MAX_TIME_DENOM ( 50 / ( 8 / CRK_COORD_RATE ) )
#define CRK_BONUS_RATE 8

int crkVVram[CRK_VVRAM_HEIGHT][CRK_VVRAM_WIDTH];
int crkCellMap[CRK_CELLMAP_BYTES];

typedef struct
{
    int x, y;
    int code;
} CrkSprite;
CrkSprite crkSprites[CRK_SPRITE_COUNT];

CrkMovable crkMan;
int crkManOldDirDx;
int crkManOldDirDy;
int crkManOldDirPattern;

int crkMonsterCount;
CrkMovable crkMonsters[CRK_MAX_MONSTER_COUNT];

#define CRK_DESTRUCTION_MAX_COUNT 3
#define CRK_DESTRUCTION_MAX_TIME 3
int crkDestructionColumn[CRK_DESTRUCTION_MAX_COUNT];
int crkDestructionFloor[CRK_DESTRUCTION_MAX_COUNT];
int crkDestructionTime[CRK_DESTRUCTION_MAX_COUNT];
int crkDestructionClock;

int crkRndIndex;

// Real text grid - 8 pages x 32 char-cells, matching upstream's own real
// Vram address space (VramRowSize=0x100 selects the page in the high byte,
// VramStep=4 real pixels per char-cell, 128 real pixels / 4 = 32 cells per
// row) - a pattern index into crkAsciiPattern (0 = space) per cell.
//
// **This width was widened from an original, wrong `[8][8]` after a real
// user-supplied hardware photo of Cracky's own title screen proved the
// original narrow model was flatly incorrect.** The original design only
// ever modeled upstream's own status-label columns (SCORE/STAGE/TIME/lives,
// which upstream's `LeftX=24` constant genuinely does confine to columns
// 24-31, 8 cells) - but the title screen's own text (the logo, "MINI",
// "START"/"CONTINUE", the "INUFUTO 2026" credit) lives at upstream's real
// columns 8-23, well to the LEFT of the status zone, using the exact same
// shared PrintC()/PrintS() mechanism at different column arguments - not a
// separate, narrower grid at all. Cramming all of that title-screen text
// into the same 8-cell-wide status grid (reusing columns 24-31 that the
// status labels ALSO use) is what caused the exact recurring bug pattern
// found across nearly every sibling Cate-engine game in this project's own
// batch verification pass: "CONTINUE" (8 chars) overflowing an 8-cell grid,
// title text silently overwriting SCORE/STAGE/TIME mid-word, and "MINI"
// being dropped outright by several ports since there was genuinely no
// room left for it in the cramped model. See `crkComposeRawByte()`'s own
// header for how this wider grid actually reaches the screen.
int crkStatusChar[8][32];

// Set true only while on the title screen (CRK_STATE_TITLE) - upstream's
// real Title() never touches the VVram/map system again after its initial
// ClearScreen(), and instead drives the ENTIRE screen (not just the status
// zone) through the same PrintC()/PrintS() text mechanism, at real columns
// spanning the whole 0-31 char-cell range (the logo, "MINI", "START"/
// "CONTINUE", the credit line all live at columns 8-23, well inside what
// during gameplay is the map area). When true, crkComposeRawByte() reads
// crkStatusChar across the full width instead of just columns 24-31,
// letting the title screen use that same wide real estate instead of being
// artificially confined to the narrow status-only zone.
bool crkFullWidthText;

// message overlay burned directly over the map area, matching upstream's
// own PrintTimeUp()/PrintGameOver() Vram-direct writes - see header.
bool crkOverlayActive;
int crkOverlayText[10];
int crkOverlayLen;
int crkOverlayPage;
int crkOverlayCol;

// sound sequencer - 3 independent voices (0=one-shot SFX, 1=jingle/BGM-A,
// 2=BGM-B), each advances every real frame regardless of CRK_TICK_DIVISOR.
int crkSeqMelody[3];
int crkSeqPos[3];
int crkSeqWait[3];
int crkSeqActive[3];

#define CRK_TICK_DIVISOR 8
int crkTickCounter;

#define CRK_STATE_TITLE 0
#define CRK_STATE_START_JINGLE 1
#define CRK_STATE_PLAYING 2
#define CRK_STATE_LOSE_ANIM 3
#define CRK_STATE_GAMEOVER_JINGLE 4
#define CRK_STATE_CLEAR_WAIT 5
#define CRK_STATE_CLEAR_JINGLE 6
#define CRK_STATE_BONUS_TALLY 7
int crkState;
int crkWaitFrames;
int crkAnimStep;
int crkSelection;
bool crkSelectionChanged;
int crkPrevLeft;
int crkPrevRight;
int crkPrevUp;
int crkPrevDown;
int crkPrevFire;
bool crkPendingContinue;


// -----------------------------------------------------------------------------
//   Math.cpp
// -----------------------------------------------------------------------------

int crkRndNumbers[32] = {
    26, 30, 1, 16, 9, 13, 12, 5,
    14, 15, 27, 7, 4, 3, 24, 20,
    8, 18, 22, 10, 19, 21, 23, 6,
    2, 29, 28, 11, 31, 0, 17, 25,
};

int crkRnd()
{
    int r;
    r = crkRndNumbers[ crkRndIndex ];
    crkRndIndex = crkRndIndex + 1;
    if( crkRndIndex >= 32 )
      crkRndIndex = 0;
    return r & 0x0f;
}

int crkAbs( int a, int b )
{
    if( a < b )
      return b - a;
    return a - b;
}


// -----------------------------------------------------------------------------
//   Stage.cpp
// -----------------------------------------------------------------------------

int crkCellMapPtr( int column, int floor )
{
    return ( floor * ( CRK_COLUMN_COUNT / CRK_COLUMNS_PER_BYTE ) ) + ( column >> 1 );
}

int crkGetCell( int column, int floor )
{
    int b;
    b = crkCellMap[ crkCellMapPtr( column, floor ) ];
    if( ( column & 1 ) == 0 )
      return b & 0x0f;
    return b >> 4;
}

void crkSetCell( int column, int floor, int cell )
{
    int idx;
    int mask;
    idx = crkCellMapPtr( column, floor );
    if( ( column & 1 ) == 0 )
    {
        mask = 0xf0;
        cell = cell & 0x0f;
    }
    else
    {
        mask = 0x0f;
        cell = cell << 4;
    }
    crkCellMap[ idx ] = ( crkCellMap[ idx ] & mask ) | cell;
}

// -----------------------------------------------------------------------------
//   Movable.cpp
// -----------------------------------------------------------------------------

#define CRK_HIT_RANGE ( CRK_COORD_RATE * 4 / 3 )

void crkLocateMovable( CrkMovable* pMovable, int b )
{
    pMovable->x = ( b & 0xf0 ) >> ( 3 - CRK_COORD_SHIFT );
    pMovable->y = ( ( ( b & 15 ) << 2 ) << CRK_COORD_SHIFT ) + CRK_COORD_RATE;
}

bool crkIsNear( CrkMovable* p1, CrkMovable* p2 )
{
    return
        p1->x + CRK_HIT_RANGE >= p2->x && p2->x + CRK_HIT_RANGE >= p1->x &&
        p1->y + CRK_HIT_RANGE >= p2->y && p2->y + CRK_HIT_RANGE >= p1->y;
}

void crkMoveMovable( CrkMovable* pMovable )
{
    pMovable->x = pMovable->x + pMovable->dx;
    pMovable->y = pMovable->y + pMovable->dy;
}

bool crkCanMove( CrkMovable* pMovable, int dx, int dy )
{
    int x;
    if( dx < 0 && pMovable->x == 0 ) return false;
    if( dx > 0 && pMovable->x >= ( CRK_STAGE_WIDTH - 2 ) * CRK_COORD_RATE ) return false;
    if( ( pMovable->status & CRK_MOVABLE_FALL ) == 0 )
    {
        int y;
        y = pMovable->y >> CRK_COORD_SHIFT;
        if( dx != 0 )
          return ( y & CRK_FLOOR_MASK ) == 1;
        x = pMovable->x >> CRK_COORD_SHIFT;
        if( dy < 0 )
        {
            if( ( x & CRK_COLUMN_MASK ) != 0 ) return false;
            return ( crkGetCell( x >> CRK_COLUMN_SHIFT, ( y + 2 ) >> CRK_FLOOR_SHIFT ) & CRK_CELL_UPPER_MASK ) == CRK_CELL_LADDER_UP;
        }
        if( dy > 0 )
        {
            if( ( x & CRK_COLUMN_MASK ) != 0 ) return false;
            if( ( y & CRK_FLOOR_MASK ) != 1 ) return true;
            return ( crkGetCell( x >> CRK_COLUMN_SHIFT, y >> CRK_FLOOR_SHIFT ) & CRK_CELL_LOWER_MASK ) == CRK_CELL_LADDER_DOWN;
        }
    }
    return true;
}

bool crkFallMovable( CrkMovable* pMovable )
{
    int x, y;
    x = pMovable->x >> CRK_COORD_SHIFT;
    y = pMovable->y >> CRK_COORD_SHIFT;
    if( ( x & CRK_COLUMN_MASK ) == 0 && ( y & CRK_FLOOR_MASK ) == 1 )
    {
        int lower;
        lower = crkGetCell( x >> CRK_COLUMN_SHIFT, y >> CRK_FLOOR_SHIFT ) & CRK_CELL_LOWER_MASK;
        if( lower == CRK_CELL_SPACE )
        {
            pMovable->status = pMovable->status | CRK_MOVABLE_FALL;
            return true;
        }
        pMovable->status = pMovable->status & ~CRK_MOVABLE_FALL;
        return false;
    }
    return ( pMovable->status & CRK_MOVABLE_FALL ) != 0;
}


// -----------------------------------------------------------------------------
//   Sprite.cpp - composites directly into crkVVram, matching upstream exactly
//   (Movable.x/y are already VVram-grid-cell coordinates, no scaling needed).
// -----------------------------------------------------------------------------

void crkHideAllSprites()
{
    int i;
    for( i = 0; i < CRK_SPRITE_COUNT; i = i + 1 )
      crkSprites[ i ].code = CRK_INVALID_CODE;
}

void crkShowSprite( CrkMovable* pMovable, int code )
{
    crkSprites[ pMovable->sprite ].x = pMovable->x;
    crkSprites[ pMovable->sprite ].y = pMovable->y;
    crkSprites[ pMovable->sprite ].code = code;
}

void crkHideSprite( int index )
{
    crkSprites[ index ].code = CRK_INVALID_CODE;
}

void crkDrawSpritesIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < CRK_SPRITE_COUNT; i = i + 1 )
    {
        if( crkSprites[ i ].code != CRK_INVALID_CODE )
        {
            x = crkSprites[ i ].x;
            y = crkSprites[ i ].y;
            c = crkSprites[ i ].code;
            crkVVram[ y ][ x ] = c; c = c + 1;
            crkVVram[ y ][ x + 1 ] = c; c = c + 1;
            crkVVram[ y + 1 ][ x ] = c; c = c + 1;
            crkVVram[ y + 1 ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Destruction.cpp
// -----------------------------------------------------------------------------

void crkInitDestructions()
{
    int i;
    for( i = 0; i < CRK_DESTRUCTION_MAX_COUNT; i = i + 1 )
      crkDestructionTime[ i ] = 0;
    crkDestructionClock = 0;
}

void crkStartDestruction( int column, int floor )
{
    int i;
    for( i = 0; i < CRK_DESTRUCTION_MAX_COUNT; i = i + 1 )
    {
        if( crkDestructionTime[ i ] == 0 )
        {
            crkDestructionColumn[ i ] = column;
            crkDestructionFloor[ i ] = floor;
            crkDestructionTime[ i ] = CRK_DESTRUCTION_MAX_TIME;
            crkSetCell( column, floor, CRK_CELL_BROKEN_FLOOR | CRK_CELL_HARD_FLOOR );
            return;
        }
    }
}

void crkUpdateDestructions()
{
    int i, t;
    crkDestructionClock = crkDestructionClock + 1;
    if( ( crkDestructionClock & CRK_COORD_MASK ) != 0 ) return;
    for( i = 0; i < CRK_DESTRUCTION_MAX_COUNT; i = i + 1 )
    {
        if( crkDestructionTime[ i ] != 0 )
        {
            t = crkDestructionTime[ i ] - 1;
            crkDestructionTime[ i ] = t;
            if( t == 0 )
              crkSetCell( crkDestructionColumn[ i ], crkDestructionFloor[ i ], CRK_CELL_BROKEN_FLOOR );
        }
    }
}

void crkDrawDestructionsIntoVVram()
{
    int i, x, y, c;
    for( i = 0; i < CRK_DESTRUCTION_MAX_COUNT; i = i + 1 )
    {
        if( crkDestructionTime[ i ] != 0 )
        {
            x = crkDestructionColumn[ i ] << 1;
            y = ( crkDestructionFloor[ i ] << 2 ) + ( CRK_FLOOR_HEIGHT - 1 );
            c = CRK_CHAR_CRACKED_FLOOR + ( ( CRK_DESTRUCTION_MAX_TIME - crkDestructionTime[ i ] ) << 1 );
            crkVVram[ y ][ x ] = c; c = c + 1;
            crkVVram[ y ][ x + 1 ] = c;
        }
    }
}


// -----------------------------------------------------------------------------
//   Status.cpp / Print.cpp - status text written into crkStatusChar (a
//   pattern-index grid covering the real columns 96-127 / pages 0-7 area).
// -----------------------------------------------------------------------------

int crkAsciiIndex( int c )
{
    // AsciiTable = " 0123456789>ACEFGIMNOPRSTUV" - direct port of PrintC()'s
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

int crkPrintC( int page, int col, int c )
{
    crkStatusChar[ page ][ col ] = crkAsciiIndex( c );
    return col + 1;
}

int crkPrintS( int page, int col, int* s, int len )
{
    int i;
    for( i = 0; i < len; i = i + 1 )
      col = crkPrintC( page, col, s[ i ] );
    return col;
}

int crkPrintDigitB( int page, int col, int n, bool zeroVisible, int value )
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
    return crkPrintC( page, col, c );
}

void crkPrintByteNumber2( int page, int col, int b )
{
    int d1;
    d1 = b / 10;
    if( d1 == 0 )
      crkPrintC( page, col, ' ' );
    else
      crkPrintC( page, col, d1 + '0' );
    crkPrintC( page, col + 1, ( b % 10 ) + '0' );
}

void crkPrintByteNumber3( int page, int col, int b )
{
    int d1, d2, rem;
    d1 = b / 100;
    rem = b % 100;
    d2 = rem / 10;
    if( d1 == 0 )
    {
        crkPrintC( page, col, ' ' );
        if( d2 == 0 )
          crkPrintC( page, col + 1, ' ' );
        else
          crkPrintC( page, col + 1, d2 + '0' );
    }
    else
    {
        crkPrintC( page, col, d1 + '0' );
        crkPrintC( page, col + 1, d2 + '0' );
    }
    crkPrintC( page, col + 2, ( rem % 10 ) + '0' );
}

void crkPrintNumber5( int page, int col, int w )
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
          crkPrintC( page, col + i, ' ' );
        else
        {
            zeroVisible = true;
            crkPrintC( page, col + i, d + '0' );
        }
        div = div / 10;
    }
    crkPrintC( page, col + 4, rem + '0' );
}

// All column arguments below are now REAL upstream character-cell columns
// (matching Status.cpp's own `LeftX=24` constant exactly - LeftX itself,
// LeftX+2, LeftX+5, LeftX+6 etc), not an arbitrary local 0-7 offset - see
// crkStatusChar's own header comment for why this changed from the
// original, too-narrow model.
void crkPrintScore()
{
    crkPrintNumber5( 1, 26, crkScore );
    crkPrintC( 1, 31, '0' );
}

void crkPrintTime()
{
    crkPrintByteNumber3( 5, 29, crkStageTime );
}

void crkPrintStatus()
{
    int sScore[5] = { 'S', 'C', 'O', 'R', 'E' };
    int sStage[5] = { 'S', 'T', 'A', 'G', 'E' };
    int sTime[4] = { 'T', 'I', 'M', 'E' };
    int i;

    crkPrintS( 0, 24, sScore, 5 );
    crkPrintS( 3, 24, sStage, 5 );
    crkPrintByteNumber2( 3, 30, crkCurrentStage + 1 );
    crkPrintS( 5, 24, sTime, 4 );

    if( crkRemainCount > 1 )
    {
        i = crkRemainCount - 1;
        if( i > 2 )
        {
            // upstream draws a real 2x2 Char_Remain icon (Put2C) here, then
            // a space, then the remaining digit - simplified to plain text
            // digits throughout (matching this port's own status-text-only
            // lives display), so just show the count directly.
            crkPrintC( 7, 24, ' ' );
            crkPrintC( 7, 25, ' ' );
            crkPrintC( 7, 26, i + '0' );
        }
        else
        {
            for( i = 0; i < crkRemainCount - 1; i = i + 1 )
              crkPrintC( 7, 24 + i * 2, ' ' );
        }
    }

    crkPrintScore();
    crkPrintTime();
}

void crkBeginOverlay( int* s, int len, int page, int col )
{
    int i;
    crkOverlayActive = true;
    crkOverlayLen = len;
    crkOverlayPage = page;
    crkOverlayCol = col;
    for( i = 0; i < len; i = i + 1 )
      crkOverlayText[ i ] = s[ i ];
}

void crkPrintGameOver()
{
    int s[9] = { 'G', 'A', 'M', 'E', ' ', 'O', 'V', 'E', 'R' };
    crkBeginOverlay( s, 9, 4, 8 );
}

void crkPrintTimeUp()
{
    int s[7] = { 'T', 'I', 'M', 'E', ' ', 'U', 'P' };
    crkBeginOverlay( s, 7, 4, 9 );
}


// -----------------------------------------------------------------------------
//   Sound sequencer
// -----------------------------------------------------------------------------

int crkMelodyLength( int id )
{
    if( id == CRK_MELODY_LOOSE ) return 3;
    if( id == CRK_MELODY_HIT ) return 17;
    if( id == CRK_MELODY_BEEP ) return 3;
    if( id == CRK_MELODY_START ) return 27;
    if( id == CRK_MELODY_CLEAR ) return 22;
    if( id == CRK_MELODY_GAMEOVER ) return 21;
    if( id == CRK_MELODY_BGM1 ) return 105;
    if( id == CRK_MELODY_BGM2 ) return 105;
    return 0;
}

int crkMelodyValue( int id, int idx )
{
    if( id == CRK_MELODY_LOOSE ) return crkMelodyLoose[ idx ];
    if( id == CRK_MELODY_HIT ) return crkMelodyHit[ idx ];
    if( id == CRK_MELODY_BEEP ) return crkMelodyBeep[ idx ];
    if( id == CRK_MELODY_START ) return crkMelodyStart[ idx ];
    if( id == CRK_MELODY_CLEAR ) return crkMelodyClear[ idx ];
    if( id == CRK_MELODY_GAMEOVER ) return crkMelodyGameOver[ idx ];
    if( id == CRK_MELODY_BGM1 ) return crkMelodyBgm1[ idx ];
    if( id == CRK_MELODY_BGM2 ) return crkMelodyBgm2[ idx ];
    return 0;
}

// SoundHandler()'s own real tempo: a channel advances once every
// (600/2)/CRK_TEMPO = 1.875 real 60Hz ticks - see header comment.
int crkNoteFrames( int length )
{
    return (int)( length * 1.875 + 0.5 );
}

void crkStartSeq( int channel, int melodyId )
{
    crkSeqMelody[ channel ] = melodyId;
    crkSeqPos[ channel ] = 0;
    crkSeqWait[ channel ] = 0;
    crkSeqActive[ channel ] = 1;
}

void crkStopSeq( int channel )
{
    crkSeqActive[ channel ] = 0;
    crkSeqMelody[ channel ] = CRK_MELODY_NONE;
}

bool crkSeqPlaying( int channel )
{
    return crkSeqActive[ channel ] != 0;
}

void crkAdvanceOneSeq( int channel )
{
    int length, note;

    if( crkSeqActive[ channel ] == 0 ) return;

    if( crkSeqWait[ channel ] > 0 )
    {
        crkSeqWait[ channel ] = crkSeqWait[ channel ] - 1;
        return;
    }

    length = crkMelodyValue( crkSeqMelody[ channel ], crkSeqPos[ channel ] );
    if( length == 0 )
    {
        crkStopSeq( channel );
        return;
    }
    if( length == 255 )
    {
        crkSeqPos[ channel ] = 0;
        length = crkMelodyValue( crkSeqMelody[ channel ], 0 );
    }
    note = crkMelodyValue( crkSeqMelody[ channel ], crkSeqPos[ channel ] + 1 );
    crkSeqPos[ channel ] = crkSeqPos[ channel ] + 2;
    crkSeqWait[ channel ] = crkNoteFrames( length );
    if( note != 0 )
      md_playTone( (float)crkFrequencies[ note - 1 ], (float)crkSeqWait[ channel ] / 60.0 );
}

void crkAdvanceSound()
{
    crkAdvanceOneSeq( 0 );
    crkAdvanceOneSeq( 1 );
    crkAdvanceOneSeq( 2 );
}

void crkStartBgm()
{
    crkStartSeq( 1, CRK_MELODY_BGM1 );
    crkStartSeq( 2, CRK_MELODY_BGM2 );
}

void crkStopBgm()
{
    crkStopSeq( 1 );
    crkStopSeq( 2 );
    md_stopTone();
}


// -----------------------------------------------------------------------------
//   Score / stage progression
// -----------------------------------------------------------------------------

void crkAddScore( int pts )
{
    crkScore = crkScore + pts;
    if( crkScore > crkHiScore )
      crkHiScore = crkScore;
    crkPrintScore();
}


// -----------------------------------------------------------------------------
//   Man.cpp
// -----------------------------------------------------------------------------

#define CRK_PATTERN_MASK 0x0f
#define CRK_PATTERN_LEFT ( ( CRK_CHAR_MAN_LEFT - CRK_CHAR_MAN ) / 4 )
#define CRK_PATTERN_RIGHT ( ( CRK_CHAR_MAN_RIGHT - CRK_CHAR_MAN ) / 4 )
#define CRK_PATTERN_UPDOWN ( ( CRK_CHAR_MAN_UPDOWN - CRK_CHAR_MAN ) / 4 )

void crkShowMan()
{
    int pattern, seq;
    pattern = crkMan.status & CRK_PATTERN_MASK;
    if( ( crkMan.status & CRK_MOVABLE_FALL ) == 0 )
    {
        if( pattern == CRK_PATTERN_UPDOWN )
        {
            seq = ( crkMan.y >> CRK_COORD_SHIFT ) & 1;
            pattern = pattern + seq;
        }
        else if( crkMan.dx != 0 )
        {
            seq = ( crkMan.x >> CRK_COORD_SHIFT ) & 3;
            if( seq == 3 )
              seq = 1;
            pattern = pattern + seq + 1;
        }
    }
    crkShowSprite( &crkMan, ( pattern << 2 ) + CRK_CHAR_MAN );
}

void crkInitMan()
{
    crkMan.sprite = CRK_SPRITE_MAN;
    crkMan.status = CRK_MOVABLE_LIVE | CRK_PATTERN_LEFT;
    crkMan.dx = 0;
    crkMan.dy = 0;
    crkManOldDirDx = -1;
    crkManOldDirDy = 0;
    crkManOldDirPattern = CRK_PATTERN_LEFT;
    crkLocateMovable( &crkMan, crkStageStart[ crkStageIndex ] );
    crkShowMan();
}

void crkFallMan()
{
    if( ( crkMan.status & CRK_MOVABLE_FALL ) != 0 )
    {
        if( ( ( crkMan.x | crkMan.y ) & CRK_COORD_MASK ) == 0 )
        {
            if( crkFallMovable( &crkMan ) )
            {
                crkMan.dy = 1;
                crkMan.dx = 0;
            }
        }
        crkMoveMovable( &crkMan );
        crkShowMan();
    }
}

void crkMoveMan()
{
    int dx, dy, pattern;
    int column, floor, cell;

    if( ( ( crkMan.x | crkMan.y ) & CRK_COORD_MASK ) == 0 )
    {
        dx = 0; dy = 0;
        pattern = crkMan.status & CRK_PATTERN_MASK;
        if( ( crkMan.status & CRK_MOVABLE_FALL ) == 0 )
        {
            bool left, right, up, down;
            left = isLeftPressed();
            right = isRightPressed();
            up = isUpPressed();
            down = isDownPressed();

            if( left )
            {
                dx = -1; dy = 0; pattern = CRK_PATTERN_LEFT;
            }
            else if( right )
            {
                dx = 1; dy = 0; pattern = CRK_PATTERN_RIGHT;
            }
            else if( up )
            {
                dx = 0; dy = -1; pattern = CRK_PATTERN_UPDOWN;
            }
            else if( down )
            {
                dx = 0; dy = 1; pattern = CRK_PATTERN_UPDOWN;
            }

            if( left || right || up || down )
            {
                if( crkCanMove( &crkMan, dx, dy ) )
                {
                    crkManOldDirDx = dx;
                    crkManOldDirDy = dy;
                    crkManOldDirPattern = pattern;
                }
                else if( crkCanMove( &crkMan, crkManOldDirDx, crkManOldDirDy ) )
                {
                    dx = crkManOldDirDx;
                    dy = crkManOldDirDy;
                    pattern = crkManOldDirPattern;
                }
                else
                {
                    dx = 0;
                    dy = 0;
                }
            }
        }
        crkMan.dx = dx;
        crkMan.dy = dy;
        crkMan.status = ( crkMan.status & ~CRK_PATTERN_MASK ) | pattern;
    }

    crkMoveMovable( &crkMan );
    crkShowMan();

    if( ( ( crkMan.x | crkMan.y ) & CRK_COORD_MASK ) == 0 )
    {
        int x, y;
        x = crkMan.x >> CRK_COORD_SHIFT;
        y = crkMan.y >> CRK_COORD_SHIFT;
        if( ( x & CRK_COLUMN_MASK ) == 0 && ( y & CRK_FLOOR_MASK ) == 1 )
        {
            column = x >> CRK_COLUMN_SHIFT;
            floor = y >> CRK_FLOOR_SHIFT;
            cell = crkGetCell( column, floor );
            if( ( cell & CRK_CELL_LOWER_MASK ) == CRK_CELL_CRACKED_FLOOR )
            {
                crkStartDestruction( column, floor );
                crkAddScore( 1 );
            }
            else if( ( cell & CRK_CELL_UPPER_MASK ) == CRK_CELL_ITEM )
            {
                crkSetCell( column, floor, cell & CRK_CELL_LOWER_MASK );
                crkItemCount = crkItemCount - 1;
                crkAddScore( 5 );
                crkStartSeq( 0, CRK_MELODY_HIT );
            }
        }
        crkFallMovable( &crkMan );
    }
}


// -----------------------------------------------------------------------------
//   Monster.cpp
// -----------------------------------------------------------------------------

#define CRK_MONSTER_PATTERN_MASK 0x07
#define CRK_DIR_LEFT 0
#define CRK_DIR_RIGHT 2
#define CRK_DIR_UP 4
#define CRK_DIR_DOWN 6

int crkMonsterDirTable[8] = { -1, 0, 1, 0, 0, -1, 0, 1 };

void crkShowMonster( CrkMovable* pMonster )
{
    int pattern, seq;
    pattern = pMonster->status & CRK_MONSTER_PATTERN_MASK;
    seq = ( ( pMonster->x + pMonster->y ) >> CRK_COORD_SHIFT ) & 1;
    pattern = pattern + seq;
    crkShowSprite( pMonster, ( pattern << 2 ) + CRK_CHAR_MONSTER );
}

void crkDecideDirection( CrkMovable* pMonster )
{
    int directions[4];
    int verticalIdx, horizontalIdx;
    int i, direction, dx, dy;

    if( crkAbs( crkMan.x, pMonster->x ) > crkAbs( crkMan.y, pMonster->y ) )
    {
        if( crkMan.x < pMonster->x )
        {
            if( pMonster->dx <= 0 )
            {
                directions[ 0 ] = CRK_DIR_LEFT;
                directions[ 3 ] = CRK_DIR_RIGHT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = CRK_DIR_RIGHT;
                directions[ 3 ] = CRK_DIR_LEFT;
                verticalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dx >= 0 )
            {
                directions[ 0 ] = CRK_DIR_RIGHT;
                directions[ 3 ] = CRK_DIR_LEFT;
                verticalIdx = 1;
            }
            else
            {
                directions[ 2 ] = CRK_DIR_LEFT;
                directions[ 3 ] = CRK_DIR_RIGHT;
                verticalIdx = 0;
            }
        }
        if( ( crkMan.y < pMonster->y && pMonster->dy <= 0 ) || pMonster->dy < 0 )
        {
            directions[ verticalIdx ] = CRK_DIR_UP;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = CRK_DIR_DOWN;
        }
        else
        {
            directions[ verticalIdx ] = CRK_DIR_DOWN;
            verticalIdx = verticalIdx + 1;
            directions[ verticalIdx ] = CRK_DIR_UP;
        }
    }
    else
    {
        if( crkMan.y < pMonster->y )
        {
            if( pMonster->dy <= 0 )
            {
                directions[ 0 ] = CRK_DIR_UP;
                directions[ 3 ] = CRK_DIR_DOWN;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = CRK_DIR_DOWN;
                directions[ 3 ] = CRK_DIR_UP;
                horizontalIdx = 0;
            }
        }
        else
        {
            if( pMonster->dy >= 0 )
            {
                directions[ 0 ] = CRK_DIR_DOWN;
                directions[ 3 ] = CRK_DIR_UP;
                horizontalIdx = 1;
            }
            else
            {
                directions[ 2 ] = CRK_DIR_UP;
                directions[ 3 ] = CRK_DIR_DOWN;
                horizontalIdx = 0;
            }
        }
        // upstream compares `Man.x < pMonster->y` here too (a real upstream
        // quirk, not a transcription slip - kept exactly as-is, matching
        // this project's own "preserve a faithful, even if odd, upstream
        // comparison rather than silently fixing it" precedent).
        if( ( crkMan.x < pMonster->y && pMonster->dx <= 0 ) || pMonster->dx < 0 )
        {
            directions[ horizontalIdx ] = CRK_DIR_LEFT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = CRK_DIR_RIGHT;
        }
        else
        {
            directions[ horizontalIdx ] = CRK_DIR_RIGHT;
            horizontalIdx = horizontalIdx + 1;
            directions[ horizontalIdx ] = CRK_DIR_LEFT;
        }
    }

    for( i = 0; i < 4; i = i + 1 )
    {
        direction = directions[ i ];
        dx = crkMonsterDirTable[ direction ];
        dy = crkMonsterDirTable[ direction + 1 ];
        if( crkCanMove( pMonster, dx, dy ) )
        {
            pMonster->dx = dx;
            pMonster->dy = dy;
            pMonster->status = ( pMonster->status & ~CRK_MONSTER_PATTERN_MASK ) | direction;
            return;
        }
    }
    pMonster->dx = 0;
    pMonster->dy = 0;
}

void crkInitMonsters()
{
    int i, sprite;
    crkMonsterCount = crkStageEnemyCount[ crkStageIndex ];
    sprite = CRK_SPRITE_MONSTER;
    for( i = 0; i < crkMonsterCount; i = i + 1 )
    {
        crkMonsters[ i ].status = CRK_MOVABLE_LIVE;
        crkMonsters[ i ].sprite = sprite;
        crkLocateMovable( &crkMonsters[ i ], crkStageEnemies[ crkStageIndex ][ i ] );
        crkDecideDirection( &crkMonsters[ i ] );
        crkShowMonster( &crkMonsters[ i ] );
        sprite = sprite + 1;
    }
    for( i = crkMonsterCount; i < CRK_MAX_MONSTER_COUNT; i = i + 1 )
    {
        crkMonsters[ i ].status = 0;
        crkMonsters[ i ].sprite = sprite;
        crkHideSprite( sprite );
        sprite = sprite + 1;
    }
}

void crkFallMonsters()
{
    int i;
    for( i = 0; i < crkMonsterCount; i = i + 1 )
    {
        if( ( crkMonsters[ i ].status & ( CRK_MOVABLE_LIVE | CRK_MOVABLE_FALL ) ) == ( CRK_MOVABLE_LIVE | CRK_MOVABLE_FALL ) )
        {
            if( ( crkMonsters[ i ].x & CRK_COORD_MASK ) == 0 && ( crkMonsters[ i ].y & CRK_COORD_MASK ) == 0 )
            {
                if( crkFallMovable( &crkMonsters[ i ] ) )
                {
                    crkMonsters[ i ].dy = 1;
                    crkMonsters[ i ].dx = 0;
                }
                crkMoveMovable( &crkMonsters[ i ] );
                crkShowMonster( &crkMonsters[ i ] );
            }
        }
    }
}

void crkMoveMonsters()
{
    int i;
    for( i = 0; i < crkMonsterCount; i = i + 1 )
    {
        if( ( crkMonsters[ i ].status & CRK_MOVABLE_LIVE ) != 0 )
        {
            if( ( crkMonsters[ i ].x & CRK_COORD_MASK ) == 0 && ( crkMonsters[ i ].y & CRK_COORD_MASK ) == 0 )
            {
                if( ( crkMonsters[ i ].status & CRK_MOVABLE_FALL ) == 0 )
                  crkDecideDirection( &crkMonsters[ i ] );
            }
            crkMoveMovable( &crkMonsters[ i ] );
            crkShowMonster( &crkMonsters[ i ] );
            if( crkIsNear( &crkMonsters[ i ], &crkMan ) )
              crkMan.status = crkMan.status & ~CRK_MOVABLE_LIVE;
            if( ( crkMonsters[ i ].x & CRK_COORD_MASK ) == 0 && ( crkMonsters[ i ].y & CRK_COORD_MASK ) == 0 )
              crkFallMovable( &crkMonsters[ i ] );
        }
    }
}


void crkInitStage()
{
    // upstream cycles through Stages[] repeatedly past CurrentStage=9 (the
    // game never actually stops the player from continuing past stage 10) -
    // preserved via the same wrap loop upstream uses instead of a plain
    // modulo, matching its own structure exactly.
    int i, j;
    i = 0;
    j = 0;
    while( i < crkCurrentStage )
    {
        i = i + 1;
        j = j + 1;
        if( j >= CRK_STAGE_COUNT )
          j = 0;
    }
    crkStageIndex = j;
}

void crkInitTrying()
{
    int i;
    crkStageTime = 25;
    i = crkStageItemCount[ crkStageIndex ];
    while( i != 0 )
    {
        crkStageTime = crkStageTime + 5;
        i = i - 1;
    }
    crkItemCount = crkStageItemCount[ crkStageIndex ];

    crkHideAllSprites();
    // ClearScreen() upstream - most of the screen doesn't need an explicit
    // clear here since every real frame is already redrawn fully from
    // crkVVram (crkMapToVVram() overwrites all 384 cells unconditionally).
    // crkStatusChar is different: crkPrintStatus() below only ever WRITES
    // its own specific label/digit cells, it never clears the whole 8x8
    // grid first - so without this, whatever the title screen (or a
    // previous stage's own HUD) last left in cells it doesn't happen to
    // overwrite (CRACKY/INUFU/START/CONTINUE, the title cursor) silently
    // persists into gameplay, overlapping the real SCORE/STAGE/TIME text
    // wherever both land on the same page. A real, found-via-user-report
    // bug, not an orientation issue - matches this project's own repeated
    // "clear cache/overlay state that doesn't get naturally overwritten"
    // lesson from several other ports.
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          crkStatusChar[ i ][ j ] = 0;
    }
    crkOverlayActive = false;
    // Defensive reset (already set false at the one real title->gameplay
    // transition in crkUpdateTitle()) - matches crkOverlayActive's own
    // belt-and-suspenders reset here, in case any future call site ever
    // reaches crkInitTrying() without going through that transition first.
    crkFullWidthText = false;

    for( i = 0; i < CRK_CELLMAP_BYTES; i = i + 1 )
      crkCellMap[ i ] = crkStageBytes[ crkStageIndex ][ i ];

    crkInitMan();
    crkInitMonsters();
    crkInitDestructions();
    crkPrintStatus();
}


// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void crkMapToVVram()
{
    int floor, colGroup, sub, b, upper, y, x, i;

    for( floor = 0; floor < CRK_FLOOR_COUNT; floor = floor + 1 )
    {
        for( colGroup = 0; colGroup < CRK_COLUMN_COUNT / CRK_COLUMNS_PER_BYTE; colGroup = colGroup + 1 )
        {
            b = crkCellMap[ floor * ( CRK_COLUMN_COUNT / CRK_COLUMNS_PER_BYTE ) + colGroup ];
            for( sub = 0; sub < CRK_COLUMNS_PER_BYTE; sub = sub + 1 )
            {
                int cellByte;
                int column;
                cellByte = ( b >> ( sub * 4 ) ) & 0x0f;
                column = colGroup * CRK_COLUMNS_PER_BYTE + sub;
                x = column * CRK_COLUMN_WIDTH;
                y = floor * CRK_FLOOR_HEIGHT;
                upper = cellByte & CRK_CELL_UPPER_MASK;

                if( upper == CRK_CELL_BROKEN_FLOOR )
                {
                    for( i = 0; i < CRK_FLOOR_HEIGHT - 1; i = i + 1 )
                    {
                        crkVVram[ y + i ][ x ] = CRK_CHAR_SPACE;
                        crkVVram[ y + i ][ x + 1 ] = CRK_CHAR_SPACE;
                    }
                    crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x ] = CRK_CHAR_CRACKED_FLOOR + 6;
                    crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x + 1 ] = CRK_CHAR_CRACKED_FLOOR + 7;
                }
                else if( upper == CRK_CELL_ITEM )
                {
                    crkVVram[ y ][ x ] = CRK_CHAR_SPACE;
                    crkVVram[ y ][ x + 1 ] = CRK_CHAR_SPACE;
                    crkVVram[ y + 1 ][ x ] = CRK_CHAR_ITEM + 0;
                    crkVVram[ y + 1 ][ x + 1 ] = CRK_CHAR_ITEM + 1;
                    crkVVram[ y + 2 ][ x ] = CRK_CHAR_ITEM + 2;
                    crkVVram[ y + 2 ][ x + 1 ] = CRK_CHAR_ITEM + 3;
                    crkVVram[ y + 3 ][ x ] = CRK_CHAR_HARD_FLOOR;
                    crkVVram[ y + 3 ][ x + 1 ] = CRK_CHAR_HARD_FLOOR;
                }
                else if( upper == CRK_CELL_LADDER_UP )
                {
                    for( i = 0; i < CRK_FLOOR_HEIGHT - 1; i = i + 1 )
                    {
                        crkVVram[ y + i ][ x ] = CRK_CHAR_LADDER_LEFT;
                        crkVVram[ y + i ][ x + 1 ] = CRK_CHAR_LADDER_RIGHT;
                    }
                    if( ( cellByte & CRK_CELL_LOWER_MASK ) == CRK_CELL_LADDER_DOWN )
                    {
                        crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x ] = CRK_CHAR_LADDER_LEFT;
                        crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x + 1 ] = CRK_CHAR_LADDER_RIGHT;
                    }
                    else
                    {
                        crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x ] = CRK_CHAR_HARD_FLOOR;
                        crkVVram[ y + CRK_FLOOR_HEIGHT - 1 ][ x + 1 ] = CRK_CHAR_HARD_FLOOR;
                    }
                }
                else
                {
                    int lower;
                    for( i = 0; i < CRK_FLOOR_HEIGHT / 2; i = i + 1 )
                    {
                        crkVVram[ y + i ][ x ] = CRK_CHAR_SPACE;
                        crkVVram[ y + i ][ x + 1 ] = CRK_CHAR_SPACE;
                    }
                    lower = cellByte & CRK_CELL_LOWER_MASK;
                    if( lower == CRK_CELL_LADDER_DOWN )
                    {
                        for( i = CRK_FLOOR_HEIGHT / 2; i < CRK_FLOOR_HEIGHT; i = i + 1 )
                        {
                            crkVVram[ y + i ][ x ] = CRK_CHAR_LADDER_LEFT;
                            crkVVram[ y + i ][ x + 1 ] = CRK_CHAR_LADDER_RIGHT;
                        }
                    }
                    else
                    {
                        crkVVram[ y + 2 ][ x ] = CRK_CHAR_SPACE;
                        crkVVram[ y + 2 ][ x + 1 ] = CRK_CHAR_SPACE;
                        if( lower == CRK_CELL_CRACKED_FLOOR )
                        {
                            crkVVram[ y + 3 ][ x ] = CRK_CHAR_CRACKED_FLOOR + 0;
                            crkVVram[ y + 3 ][ x + 1 ] = CRK_CHAR_CRACKED_FLOOR + 1;
                        }
                        else if( lower == CRK_CELL_HARD_FLOOR )
                        {
                            crkVVram[ y + 3 ][ x ] = CRK_CHAR_HARD_FLOOR;
                            crkVVram[ y + 3 ][ x + 1 ] = CRK_CHAR_HARD_FLOOR;
                        }
                        else
                        {
                            crkVVram[ y + 3 ][ x ] = CRK_CHAR_SPACE;
                            crkVVram[ y + 3 ][ x + 1 ] = CRK_CHAR_SPACE;
                        }
                    }
                }
            }
        }
    }
}

void crkDrawAll()
{
    crkMapToVVram();
    crkDrawDestructionsIntoVVram();
    crkDrawSpritesIntoVVram();
}

// Reproduces VVramToVram()'s own SendUL() nibble-interleaving exactly - see
// header comment for the full derivation. rawCol/rawPage are in upstream's
// own (unmirrored) GDDRAM coordinate space; the hardware remap is applied
// by the caller.
int crkComposeRawByte( int rawCol, int rawPage )
{
    int mapByte, textByte;

    mapByte = 0;
    if( rawCol < CRK_VVRAM_WIDTH * 4 )
    {
        int mapX, sub, upper, lower, upperByte, lowerByte;
        mapX = rawCol / 4;
        sub = rawCol % 4;
        upper = crkVVram[ rawPage * 2 ][ mapX ];
        lower = crkVVram[ rawPage * 2 + 1 ][ mapX ];
        if( sub == 0 )
        {
            upperByte = crkCharPattern[ upper * 2 + 0 ];
            lowerByte = crkCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else if( sub == 1 )
        {
            upperByte = crkCharPattern[ upper * 2 + 0 ];
            lowerByte = crkCharPattern[ lower * 2 + 0 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
        else if( sub == 2 )
        {
            upperByte = crkCharPattern[ upper * 2 + 1 ];
            lowerByte = crkCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte & 0x0f ) | ( lowerByte << 4 );
        }
        else
        {
            upperByte = crkCharPattern[ upper * 2 + 1 ];
            lowerByte = crkCharPattern[ lower * 2 + 1 ];
            mapByte = ( upperByte >> 4 ) | ( lowerByte & 0xf0 );
        }
    }

    if( !crkFullWidthText && rawCol < CRK_VVRAM_WIDTH * 4 )
      return mapByte;

    // charCol/sub are real upstream char-cell coordinates (0-31), matching
    // crkStatusChar's own full-width indexing directly. OR-combined with
    // mapByte rather than chosen exclusively: during the title screen
    // (crkFullWidthText), the "CRACKY" logo bitmap drawn into crkVVram
    // (see crkBeginTitle()) occupies real hardware pages 1-2 only, while
    // every status-text element (SCORE/MINI/START/CONTINUE/credit) is
    // printed on pages 0/3/5/6/7 - entirely disjoint page ranges, so this
    // can never actually blend two real, distinct pieces of content
    // together. It just lets the logo (mapByte, non-zero only on its own
    // 2 pages) and the text (textByte, non-zero only on its own 5 pages)
    // coexist within one composed byte instead of one silently excluding
    // the other.
    textByte = 0;
    {
        int charCol, sub, c;
        charCol = rawCol / 4;
        sub = rawCol % 4;
        if( charCol < 32 )
        {
            c = crkStatusChar[ rawPage ][ charCol ];
            textByte = crkAsciiPattern[ c * 4 + sub ];
        }
    }
    return mapByte | textByte;
}

// This port went through several wrong hardware-orientation transforms
// (a full mirror+flip, then vertical-only, then various half-fixes) before
// the user supplied a genuine reference screenshot of correct output:
// SCORE at the top, STAGE in the middle, TIME near the bottom, status text
// on the right, the map on the left. Checked directly against upstream's
// own raw addressing rather than guessed again: SCORE is upstream's own
// page 0, STAGE page 3, TIME page 5 - already top-to-bottom in that exact
// order with NO transform at all. Status text lives at upstream's own raw
// columns 96-127, already the right side with no mirroring needed. Every
// flip/mirror/bit-reversal this file went through was chasing a problem
// that never existed - the real, actual bug the whole time was the stale
// status-text-grid-not-cleared defect (see crkInitTrying()'s own comment),
// which made every orientation look broken regardless of whether the
// transform itself was right, and led to misdiagnosing the transform. The
// composed byte is now drawn directly at its own (col,page) - no column
// mirror, no page reorder, no bit-reversal, no lookup table at all.
void crkRender()
{
    int page, col, value;

    md_beginFrame();

    for( page = 0; page < 8; page = page + 1 )
    {
        for( col = 0; col < 128; col = col + 1 )
        {
            if( crkOverlayActive && page == crkOverlayPage &&
                col >= crkOverlayCol * 4 && col < crkOverlayCol * 4 + crkOverlayLen * 4 )
            {
                int i, sub;
                i = ( col - crkOverlayCol * 4 ) / 4;
                sub = ( col - crkOverlayCol * 4 ) % 4;
                value = crkAsciiPattern[ crkAsciiIndex( crkOverlayText[ i ] ) * 4 + sub ];
            }
            else
              value = crkComposeRawByte( col, page );
            md_drawColumn( col, page, value );
        }
    }
}


// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

// **Rewritten after a real user-supplied photo of Cracky running on actual
// hardware proved the previous version of this function was simply wrong.**
// The earlier version believed upstream's own title-screen text collided
// with the SCORE/STAGE/TIME status labels and had to be trimmed/relocated/
// dropped to fit - "MINI" was cut entirely, "CONTINUE" was truncated to
// "CONTINU", and the credit line was dropped outright. Re-reading upstream's
// real `Status.cpp` (`Title()`) line by line shows this diagnosis was
// backwards: none of that text ever collides with anything upstream,
// because upstream's own Vram address space is a genuinely wide 32-char-
// cell-per-page canvas (see crkStatusChar's own header comment) - the
// status labels occupy only columns 24-31 (upstream's own `LeftX=24`), and
// every piece of title-screen text sits at columns 8-23, well clear of
// them. The ROOT problem was this port's own `crkStatusChar` being modeled
// as an 8-column-wide grid in the first place - now fixed there, this
// function is rewritten to place everything at upstream's real, literal
// columns, with `crkFullWidthText=true` so crkComposeRawByte() renders the
// full canvas instead of just the narrow status zone.
void crkBeginTitle()
{
    int sMini[4] = { 'M', 'I', 'N', 'I' };
    int sStart[5] = { 'S', 'T', 'A', 'R', 'T' };
    int sContinue[8] = { 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E' };
    int sCredit[12] = { 'I', 'N', 'U', 'F', 'U', 'T', 'O', ' ', '2', '0', '2', '6' };
    int i;

    for( i = 0; i < CRK_VVRAM_HEIGHT; i = i + 1 )
    {
        int j;
        for( j = 0; j < CRK_VVRAM_WIDTH; j = j + 1 )
          crkVVram[ i ][ j ] = CRK_CHAR_SPACE;
    }
    for( i = 0; i < 8; i = i + 1 )
    {
        int j;
        for( j = 0; j < 32; j = j + 1 )
          crkStatusChar[ i ][ j ] = 0;
    }
    crkOverlayActive = false;
    crkFullWidthText = true;
    crkHideAllSprites();
    // A real bug found via direct user report ("on game over, the time
    // value remains visible on titlescreen"): crkPrintStatus() below
    // redraws TIME from whatever crkStageTime last held during real
    // gameplay - nothing resets it before reaching the title screen after
    // a game over, so the final countdown value stays on screen as part
    // of the title's own status display. Reset here so the title screen
    // always shows a fresh "TIME 000" instead of a stale leftover value.
    crkStageTime = 0;
    crkPrintStatus();

    // **Restored, after a direct user report that the earlier plain-text
    // substitute was wrong**: this is upstream's own real 6-glyph "CRACKY"
    // logo bitmap, drawn directly into crkVVram from crkTitleBytes[] at
    // its own real position (VVram rows 2-5, i.e. real hardware pages 1-2
    // - matching upstream's `Status.cpp` `Title()`'s own `VVram +
    // VVramWidth*2` starting offset exactly). The earlier version of this
    // function replaced this with plain small text, reasoning it was
    // "purely decorative" - wrong: it's the actual title wordmark, meant
    // to be the single biggest, most prominent element on the whole
    // screen, not a throwaway detail. `crkComposeRawByte()` was updated
    // to OR-combine this VVram content with crkStatusChar's own text
    // layer rather than choosing one exclusively, since the two occupy
    // disjoint page ranges by construction (see that function's own
    // comment).
    {
        int ch, row, col, idx;
        idx = 0;
        for( ch = 0; ch < 6; ch = ch + 1 )
          for( row = 0; row < 4; row = row + 1 )
            for( col = 0; col < 4; col = col + 1 )
            {
                crkVVram[ 2 + row ][ ch * 4 + col ] = crkTitleBytes[ idx ];
                idx = idx + 1;
            }
    }
    // Everything below is at upstream's own real, literal columns
    // (Status.cpp's Title(): MINI at col 18, START/CONTINUE at col 9 with
    // the cursor at col 8, the credit line at col 12) - all genuinely clear
    // of the status labels' own columns 24-31, so nothing here needs
    // trimming, relocating, or dropping anymore.
    crkPrintS( 3, 18, sMini, 4 );
    crkPrintS( 5, 9, sStart, 5 );
    crkPrintS( 6, 9, sContinue, 8 );
    crkPrintS( 7, 12, sCredit, 12 );
    crkSelection = 0;
    crkSelectionChanged = true;
    crkPrevLeft = 0; crkPrevRight = 0; crkPrevUp = 0; crkPrevDown = 0; crkPrevFire = 0;
    crkState = CRK_STATE_TITLE;
}

void crkUpdateTitle()
{
    bool left, right, up, down, fire;
    bool justDir, justFire;

    left = isLeftPressed(); right = isRightPressed();
    up = isUpPressed(); down = isDownPressed();
    fire = isFirePressed();

    justDir = ( ( left && !crkPrevLeft ) || ( right && !crkPrevRight ) ||
                ( up && !crkPrevUp ) || ( down && !crkPrevDown ) );
    justFire = ( fire && !crkPrevFire );
    crkPrevLeft = left; crkPrevRight = right; crkPrevUp = up; crkPrevDown = down; crkPrevFire = fire;

    if( crkSelectionChanged )
    {
        crkSelectionChanged = false;
        if( crkSelection == 0 )
          crkPrintC( 5, 8, '>' );
        else
          crkPrintC( 5, 8, ' ' );
        if( crkSelection == 1 )
          crkPrintC( 6, 8, '>' );
        else
          crkPrintC( 6, 8, ' ' );
    }

    if( justFire )
    {
        crkFullWidthText = false;
        crkPendingContinue = ( crkSelection == 1 );
        crkScore = 0;
        if( !crkPendingContinue )
          crkCurrentStage = 0;
        crkRemainCount = 3;
        crkInitStage();
        crkInitTrying();
        crkDrawAll();
        crkStartSeq( 1, CRK_MELODY_START );
        crkState = CRK_STATE_START_JINGLE;
        crkRender();
        return;
    }
    if( justDir )
    {
        crkSelection = crkSelection ^ 1;
        crkSelectionChanged = true;
    }
    crkRender();
}

void crkUpdateStartJingle()
{
    if( !crkSeqPlaying( 1 ) )
    {
        crkStartBgm();
        crkClock = 0;
        crkMonsterNum = 0;
        crkTimeDenom = CRK_MAX_TIME_DENOM;
        crkState = CRK_STATE_PLAYING;
    }
    crkRender();
}

void crkBeginLose()
{
    crkStopBgm();
    crkAnimStep = 0;
    crkWaitFrames = 0;
    crkState = CRK_STATE_LOSE_ANIM;
}

void crkUpdateLoseAnim()
{
    int patterns[4] = { CRK_CHAR_MAN_LEFT, CRK_CHAR_MAN_LOOSE0, CRK_CHAR_MAN_LOOSE1, CRK_CHAR_MAN_LOOSE2 };

    if( crkWaitFrames > 0 )
    {
        crkWaitFrames = crkWaitFrames - 1;
        crkRender();
        return;
    }

    crkShowSprite( &crkMan, patterns[ crkAnimStep & 3 ] );
    crkDrawAll();
    crkStartSeq( 0, CRK_MELODY_LOOSE );
    crkAnimStep = crkAnimStep + 1;
    crkWaitFrames = crkNoteFrames( 1 );

    if( crkAnimStep >= 8 )
    {
        crkRemainCount = crkRemainCount - 1;
        if( crkRemainCount > 0 )
        {
            crkInitTrying();
            crkDrawAll();
            crkOverlayActive = false;
            crkStartSeq( 1, CRK_MELODY_START );
            crkState = CRK_STATE_START_JINGLE;
        }
        else
        {
            crkPrintGameOver();
            crkStartSeq( 1, CRK_MELODY_GAMEOVER );
            crkState = CRK_STATE_GAMEOVER_JINGLE;
        }
    }
    crkRender();
}

void crkUpdateGameOverJingle()
{
    if( !crkSeqPlaying( 1 ) )
      crkBeginTitle();
    else
      crkRender();
}

void crkBeginClearWait()
{
    crkStopBgm();
    crkWaitFrames = 10;
    crkState = CRK_STATE_CLEAR_WAIT;
}

void crkUpdateClearWait()
{
    if( crkWaitFrames > 0 )
    {
        crkWaitFrames = crkWaitFrames - 1;
        crkRender();
        return;
    }
    crkStartSeq( 1, CRK_MELODY_CLEAR );
    crkState = CRK_STATE_CLEAR_JINGLE;
    crkRender();
}

void crkUpdateClearJingle()
{
    if( !crkSeqPlaying( 1 ) )
    {
        crkWaitFrames = 0;
        crkState = CRK_STATE_BONUS_TALLY;
    }
    crkRender();
}

void crkUpdateBonusTally()
{
    if( crkWaitFrames > 0 )
    {
        crkWaitFrames = crkWaitFrames - 1;
        crkRender();
        return;
    }

    if( crkStageTime >= CRK_BONUS_RATE )
    {
        crkAddScore( 5 );
        crkStageTime = crkStageTime - CRK_BONUS_RATE;
        crkPrintTime();
        crkStartSeq( 0, CRK_MELODY_BEEP );
        crkWaitFrames = crkNoteFrames( 1 );
        crkRender();
        return;
    }

    crkStageTime = 0;
    crkPrintStatus();
    crkCurrentStage = crkCurrentStage + 1;
    crkInitStage();
    crkInitTrying();
    crkDrawAll();
    crkStartSeq( 1, CRK_MELODY_START );
    crkState = CRK_STATE_START_JINGLE;
    crkRender();
}

void crkUpdatePlaying()
{
    crkTickCounter = crkTickCounter + 1;
    if( crkTickCounter < CRK_TICK_DIVISOR )
    {
        crkRender();
        return;
    }
    crkTickCounter = 0;

    crkUpdateDestructions();
    crkFallMan();
    crkMoveMan();
    if( crkMonsterNum >= 0 )
    {
        crkFallMonsters();
        crkMoveMonsters();
        crkMonsterNum = crkMonsterNum - 10;
    }
    crkMonsterNum = crkMonsterNum + 6;

    crkTimeDenom = crkTimeDenom - 1;
    if( crkTimeDenom == 0 )
    {
        crkStageTime = crkStageTime - 1;
        crkTimeDenom = CRK_MAX_TIME_DENOM;
        crkPrintTime();
        if( crkStageTime == 0 )
        {
            crkPrintTimeUp();
            crkDrawAll();
            crkRender();
            crkBeginLose();
            return;
        }
    }

    crkDrawAll();

    if( ( crkMan.status & CRK_MOVABLE_LIVE ) == 0 )
    {
        crkRender();
        crkBeginLose();
        return;
    }

    if( crkItemCount == 0 )
    {
        crkRender();
        crkBeginClearWait();
        return;
    }

    crkRender();
}


// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameCracky_init()
{
    int i;

    crkHiScore = 0;
    crkScore = 0;
    crkCurrentStage = 0;
    crkRemainCount = 3;
    crkStageTime = 0;
    crkRndIndex = 0;

    for( i = 0; i < 3; i = i + 1 )
    {
        crkSeqActive[ i ] = 0;
        crkSeqMelody[ i ] = CRK_MELODY_NONE;
    }
    crkOverlayActive = false;
    crkTickCounter = 0;

    crkBeginTitle();
}

void gameCracky_update()
{
    crkAdvanceSound();

    if( crkState == CRK_STATE_TITLE )
      crkUpdateTitle();
    else if( crkState == CRK_STATE_START_JINGLE )
      crkUpdateStartJingle();
    else if( crkState == CRK_STATE_PLAYING )
      crkUpdatePlaying();
    else if( crkState == CRK_STATE_LOSE_ANIM )
      crkUpdateLoseAnim();
    else if( crkState == CRK_STATE_GAMEOVER_JINGLE )
      crkUpdateGameOverJingle();
    else if( crkState == CRK_STATE_CLEAR_WAIT )
      crkUpdateClearWait();
    else if( crkState == CRK_STATE_CLEAR_JINGLE )
      crkUpdateClearJingle();
    else if( crkState == CRK_STATE_BONUS_TALLY )
      crkUpdateBonusTally();
}
