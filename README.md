# TinyJoypad → SDL3

A native SDL3 port of [tinyjoypad_vircon32](../tinyjoypad_vircon32) - the
same **33 games**, originally written for the
[TinyJoypad](https://www.tinyjoypad.com/) ATtiny85 + SSD1306 128x64 OLED
handheld, behind the same shared game-select menu, but running as a plain
desktop executable instead of a [Vircon32](https://www.vircon32.com/)
cartridge - no emulator required.

Two more ports also live in this same repo, alongside the primary SDL3
port in `src/sdl3/`, all three sharing one `src/gameworld/` codebase (all
33 games, the menu, the shims):
- **SDL2** (`src/sdl2/`) - built by porting `src/sdl3/`'s own platform
  files API-call-by-API-call, then verified functionally identical (every
  behavioral constant, every input mapping table, both passing the same
  33-game regression pass).
- **Playdate** (`src/playdate/`) - a genuinely different kind of port
  (fixed 400x240 1-bit hardware, its own C SDK, no CLI/window/desktop
  concept at all), so a from-scratch platform layer instead - a brand-new
  Playdate-native menu (alphabetized, numbered, paginated, with a live
  gameplay thumbnail + author caption for the selected game), manual
  `GAME_SCALE`-based rendering (128x64 native OLED content scaled 3x,
  388x192, to fit the real panel), and Playdate's own square-wave synth
  for audio - see `CLAUDE.md`'s own "The Playdate port" section for the
  full design writeup, including a couple of real SDK gotchas found doing
  it (an invalid `pd->display->setScale()` value that silently did
  nothing, a MinGW-vs-MSVC DLL-export difference).

See `CLAUDE.md`'s "Directory layout / multi-port structure" section for
the full writeup on how this multi-port layout works, and for what a
future fourth port would need to add.

This is a **sibling port**, not a fork: it reuses the sibling
`tinyjoypad_vircon32` project's already-ported, already-bug-fixed C game
logic wholesale, converting only the small amount of nonstandard Vircon32
C-dialect syntax that logic depended on back into standard C (array
declaration order, `int[]`-as-string → `char*`, bare `struct Tag` →
`typedef struct`) - the actual game code, and the whole bug-fix history
behind it, is unchanged. Every per-game bug already found and fixed in
that project (the byte-truncation/shift-wraparound/signed-sentinel/RNG-
range/logical-shift family of AVR-vs-Vircon32 dialect bugs, plus dozens of
genuine game-logic bugs found via real play) applies here too, since it's
the same C.

The platform layer itself - window/input/audio/rendering, the menu, the
CLI, and the presentation effects below - is new, built directly against
SDL3, reusing infrastructure and technique from the same author's sibling
`crisp-game-lib-portable-sdl` project (`cglpSDL3.c`) where it fit: the
CInput abstraction, the audio oscillator shape, and the glow/CRT/pixel-
grid presentation effects (rebuilt around GPU textures rather than ported
verbatim - see `CLAUDE.md` for why).

This port was built with the help of Claude AI (Anthropic) - the platform
layer, the dialect conversion, every bug found during the port, and the
optimization/verification passes documented in `CLAUDE.md`/
`OPTIMIZATIONS.md` were all done through an AI-assisted development
workflow.

## Building

Requires CMake and a C compiler (developed against MinGW-w64/GCC on
Windows; nothing in the source is Windows-specific beyond the `-mwindows`
linker flag, which only suppresses the console window and is skipped on
non-Windows).

This repo has no top-level `CMakeLists.txt` - each port under `src/`
(`src/sdl3/` and `src/sdl2/`) is its own standalone CMake project, built
from inside its own directory:

```sh
cd src/sdl3          # or: cd src/sdl2
cmake -B build -G Ninja -DUSE_VENDORED_SDL=1
cmake --build build
```

- `USE_VENDORED_SDL=1` builds SDL from the matching vendored checkout at
  the repo root (`SDL3/` for `src/sdl3/`, `SDL2/` for `src/sdl2/`) as a
  static library and statically links everything into one self-contained
  executable (no SDL DLL needed alongside it).
- Omit `-DUSE_VENDORED_SDL=1` (or set it to `0`) to link against a system-
  installed SDL instead (`find_package(SDL3 CONFIG)` / `find_package(SDL2 CONFIG)`
  respectively).
- `assets/` (currently just the menu's thumbnail images) is copied next to
  the built executable automatically after every build, since thumbnails
  are resolved relative to the executable's own directory
  (`SDL_GetBasePath()`), not the launching shell's working directory - see
  `CLAUDE.md` for why. This makes the build output directory (`src/sdl3/build/`
  or `src/sdl2/build/`) a complete, runnable, relocatable copy - copy the
  whole folder to distribute it.
- The resulting binary ends up at `src/sdl3/build/TinyjoypadSDL3` (or
  `src/sdl2/build/TinyjoypadSDL2`, or `.exe` on Windows either way). See
  `CLAUDE.md`'s "Directory layout / multi-port structure" section for why
  the build entry point is per-port rather than a single repo-root command.
- The two ports are functionally identical (see the intro above) - pick
  whichever matches what's actually available to link against on your
  system/platform. SDL3 is the primary, most-exercised port.

## Running

Identical CLI/behavior on both ports - examples below use the SDL3 binary,
substitute `TinyjoypadSDL2` (from `src/sdl2/build/`) for the SDL2 one.

```sh
./TinyjoypadSDL3           # opens the menu
./TinyjoypadSDL3 -g "TINY INVADERS"   # launches a game directly by title
./TinyjoypadSDL3 -list     # prints every game's title, then exits
./TinyjoypadSDL3 -help     # full command-line reference
```

See `-help` for the complete flag list (window size/fullscreen, `-ns` to
skip audio, `-fps` for a live FPS overlay, `-nd` to uncap the framerate,
`-ms` to batch-capture a gameplay screenshot of every game, `-joy` to
write a `.joy` stub file per game for external frontends, and a `.joy`
file itself as a positional argument to launch straight into that game).

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Move / navigate menu | Arrow keys | D-pad or left stick |
| Fire / confirm / A | X | South button (A on Xbox) |
| Fire2 / B *(only Tiny Minez - instant flag toggle)* | C | East button (B on Xbox) |
| Start / pause / quit-confirmation dialog | Escape | Start or Back |
| Cycle glow / CRT scanlines / pixel-grid effect | G | West button (X on Xbox) |
| Toggle fullscreen | F3 | - |
| Volume down / up | PageDown / PageUp | Left/Right shoulder |
| Mute / unmute sound | S | North button (Y on Xbox) |

The quit-confirmation dialog (Start, mid-game) freezes the current game
and asks YES/NO before returning to the menu - the same behavior as the
sibling Vircon32 build. The glow/CRT/pixel-grid cycle is a single button
stepping through 5 combinations (none → pixel-grid+glow → pixel-grid →
CRT scanlines → glow alone → back to none) and only has any visible
effect during actual gameplay, not on the menu screen. Fullscreen/volume/
mute work everywhere (menu and in-game) and are logged to the console on
every change, since there's no on-screen indicator for them; `-f`/`-ns`
on the command line still cover fullscreen/no-sound at launch time.

## Games

Same 33 games, same credits/licenses, as the sibling Vircon32 build - see
[tinyjoypad_vircon32/README.md](../tinyjoypad_vircon32/README.md#games)
for the full per-game table (original author, license, upstream source
link). Not duplicated here since it's identical either way.

## Credits

- [Vircon32](https://www.vircon32.com/) - the fantasy console
  `tinyjoypad_vircon32` (this project's own sibling, the source of every
  game's own C logic - see the intro above) targets. This project's own
  menu/dialog text is drawn with a faithful reproduction of Vircon32's
  real BIOS font (`src/gameworld/biosFont.h` - 10x20px, codepage 1252, all
  256 glyphs extracted directly from the actual BIOS font asset, not
  hand-transcribed), authored by **Carra** (Vircon32's own author) and
  published on OpenGameArt.org as *"Pixel Art Outlined Text Fonts"* under
  **CC-BY 4.0** - a separate license from this project's own GPLv3 below;
  this credit is that license's own attribution requirement.
- `crisp-game-lib-portable-sdl` (same author, a different game collection
  with its own Vircon32-ported sibling too) - the source of the `CInput`
  keyboard/gamepad abstraction, the single-voice audio oscillator shape,
  the original inspiration for the glow/CRT/pixel-grid presentation
  effects, and (for the SDL2 and Playdate ports) the direct API-porting
  reference - see `CLAUDE.md` for exactly where each was reused versus
  redesigned.
- Every individual game's own original author/license is preserved
  unmodified in its own header comment under `src/gameworld/games/` (see
  "Games" above) - not repeated here since it's identical to the sibling
  Vircon32 build's own per-game credit table.

## License

This project is **GPLv3** (`LICENSE.txt`, mirrored from the sibling
Vircon32 build), for the same reason: at least one ported game (Tiny
Invaders v4.2) is itself GPLv3, and combining GPLv3 code into one
executable makes the whole executable a GPLv3 combined work. This covers
this project's own new code (both the SDL3 and SDL2 platform layers, the
shared menu, the presentation effects) - each individual game's own
original license/attribution is preserved unmodified in its own header
comment in `src/gameworld/games/`.

## See also

- [CLAUDE.md](CLAUDE.md) - this project's own architecture, the dialect-
  conversion process, every SDL3-specific bug found during the port (not
  game-logic bugs - those are the sibling project's own history), and the
  presentation-effects design.
- [OPTIMIZATIONS.md](OPTIMIZATIONS.md) - performance notes specific to
  this SDL3 build (a real desktop CPU/GPU, not Vircon32's fixed 250,000-
  cycle/frame budget - a genuinely different cost model).
- [tinyjoypad_vircon32/CLAUDE.md](../tinyjoypad_vircon32/CLAUDE.md) - the
  full per-game porting history (every game-logic bug found and fixed,
  the AVR-vs-Vircon32 dialect bug family, per-game optimization passes)
  that this project inherited along with the ported C code itself.
