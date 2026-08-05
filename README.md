# TinyJoypad → SDL

![Menu screenshot](metadata/menu.png)

A native SDL3 port bringing **41 games**, originally written for the
[TinyJoypad](https://www.tinyjoypad.com/) ATtiny85 + SSD1306 128x64 OLED
handheld, behind one shared game-select menu, running as a plain desktop
executable - no emulator required.

Two more ports also live in this same repo, alongside the primary SDL3
port in `src/sdl3/`, all three sharing one `src/gameworld/` codebase (all
41 games, the menu, the shims):
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

The game logic itself is already-ported, already-bug-fixed C, carried over
by converting only the small amount of nonstandard Vircon32 C-dialect
syntax it depended on back into standard C (array declaration order,
`int[]`-as-string → `char*`, bare `struct Tag` → `typedef struct`) - the
actual game code, and the whole bug-fix history behind it, is unchanged.
Every per-game bug already found and fixed there (the byte-truncation/
shift-wraparound/signed-sentinel/RNG-range/logical-shift family of dialect
bugs, plus dozens of genuine game-logic bugs found via real play) applies
here too, since it's the same C.

The platform layer itself - window/input/audio/rendering, the menu, the
CLI, and the presentation effects below - is new, built directly against
SDL3, reusing infrastructure and technique from the same author's
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
- The menu's thumbnail images are compiled directly into the executable
  (`assets/thumbnails/thumbnailData.h`, generated from `assets/thumbnails/
  *.bmp` by `tools/gen_thumbnails.py` - re-run it after changing any
  thumbnail) - there's no `assets/` folder to ship or find alongside the
  built exe. The single resulting binary is the complete, runnable,
  relocatable artifact - copy just the `.exe` to distribute it.
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
`-s` to force software rendering instead of the default GPU-accelerated
one, `-ms` to batch-capture a gameplay screenshot of every game, `-joy` to
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
and asks YES/NO before returning to the menu - unless the game was
launched directly (`-g <NAME>` or a `.joy` file), in which case there's no
menu to return to, so Start quits the app immediately instead, with no
dialog. The glow/CRT/pixel-grid cycle is a single button
stepping through 5 combinations (none → pixel-grid+glow → pixel-grid →
CRT scanlines → glow alone → back to none) and only has any visible
effect during actual gameplay, not on the menu screen. Fullscreen/volume/
mute work everywhere (menu and in-game) and are logged to the console on
every change, since there's no on-screen indicator for them; `-f`/`-ns`
on the command line still cover fullscreen/no-sound at launch time.

## Games

Click a thumbnail for the full-size screenshot.

| Game (in-cartridge title) | Original Author | License | Source | Screenshot |
|---|---|---|---|---|
| 2048 | Obono | MIT | [TinyJoypadWorks](https://github.com/obono/TinyJoypadWorks) | [<img src="metadata/screenshots/2048.png" width="160">](metadata/screenshots/2048.png) |
| Bat Bonanza | Andy Jackson | Non-commercial, with attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/BAT%20BONANZA.png" width="160">](metadata/screenshots/BAT%20BONANZA.png) |
| Breakout | Ilya Titov | Non-commercial, with attribution | [AttinyArcade](https://github.com/webboggles/AttinyArcade) | [<img src="metadata/screenshots/BREAKOUT.png" width="160">](metadata/screenshots/BREAKOUT.png) |
| Dino Game | tiny-handheld project (original) | None specified | [tiny-handheld](https://github.com/Yevgeniy-Olexandrenko/tiny-handheld) | [<img src="metadata/screenshots/DINO%20GAME.png" width="160">](metadata/screenshots/DINO%20GAME.png) |
| Falling Blocks | Andy Jackson | Non-commercial, with or without attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/FALLING%20BLOCKS.png" width="160">](metadata/screenshots/FALLING%20BLOCKS.png) |
| Four in a Row | Unknown | None specified | [tiny-handheld](https://github.com/Yevgeniy-Olexandrenko/tiny-handheld) | [<img src="metadata/screenshots/FOUR%20IN%20A%20ROW.png" width="160">](metadata/screenshots/FOUR%20IN%20A%20ROW.png) |
| Frogger | Andy Jackson (art: @senkunmusashi) | Non-commercial, with attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/FROGGER.png" width="160">](metadata/screenshots/FROGGER.png) |
| HollowSeeker | Obono | MIT | [TinyJoypadWorks](https://github.com/obono/TinyJoypadWorks) | [<img src="metadata/screenshots/HOLLOWSEEKER.png" width="160">](metadata/screenshots/HOLLOWSEEKER.png) |
| Jump Slime | Kondolab | None specified | [note.com](https://note.com/kondolab/n/ndc93ac31e555) | [<img src="metadata/screenshots/JUMP%20SLIME.png" width="160">](metadata/screenshots/JUMP%20SLIME.png) |
| NumberPlace | Obono | MIT | [TinyJoypadWorks](https://github.com/obono/TinyJoypadWorks) | [<img src="metadata/screenshots/NUMBERPLACE.png" width="160">](metadata/screenshots/NUMBERPLACE.png) |
| Oroboros | Ilya Titov | Non-commercial, with attribution | [AttinyArcade](https://github.com/webboggles/AttinyArcade) | [<img src="metadata/screenshots/OROBOROS.png" width="160">](metadata/screenshots/OROBOROS.png) |
| Run Dude Run | Ilya Titov | Non-commercial, with attribution | [AttinyArcade](https://github.com/webboggles/AttinyArcade) | [<img src="metadata/screenshots/RUN%20DUDE%20RUN.png" width="160">](metadata/screenshots/RUN%20DUDE%20RUN.png) |
| SnakeGame85 | Terezaza | GPLv3 | [SnakeGame85](https://github.com/terezaza/SnakeGame85) | [<img src="metadata/screenshots/SNAKEGAME85.png" width="160">](metadata/screenshots/SNAKEGAME85.png) |
| Space Attack | Andy Jackson | Non-commercial, with attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/SPACE%20ATTACK.png" width="160">](metadata/screenshots/SPACE%20ATTACK.png) |
| Stacker | Andy Jackson | Non-commercial, with attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/STACKER.png" width="160">](metadata/screenshots/STACKER.png) |
| Tiny Arena | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20ARENA.png" width="160">](metadata/screenshots/TINY%20ARENA.png) |
| Tiny Arkanoid | Daniel Champagne | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20ARKANOID.png" width="160">](metadata/screenshots/TINY%20ARKANOID.png) |
| Tiny Bert | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20BERT.png" width="160">](metadata/screenshots/TINY%20BERT.png) |
| Tiny Bike | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20BIKE.png" width="160">](metadata/screenshots/TINY%20BIKE.png) |
| Tiny Bomber | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20BOMBER.png" width="160">](metadata/screenshots/TINY%20BOMBER.png) |
| Tiny DDug | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20DDUG.png" width="160">](metadata/screenshots/TINY%20DDUG.png) |
| Tiny Doc | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20DOC.png" width="160">](metadata/screenshots/TINY%20DOC.png) |
| Tiny Dungeon | Sven B / Lorandil | MIT | [TinyDungeon](https://github.com/Lorandil/TinyDungeon) | [<img src="metadata/screenshots/TINY%20DUNGEON.png" width="160">](metadata/screenshots/TINY%20DUNGEON.png) |
| TinY Fi | Kondolab | None specified | [note.com](https://note.com/kondolab/n/n2c96413eaa23) | [<img src="metadata/screenshots/TINY%20FI.png" width="160">](metadata/screenshots/TINY%20FI.png) |
| Tiny Gilbert | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20GILBERT.png" width="160">](metadata/screenshots/TINY%20GILBERT.png) |
| Tiny Invaders | Daniel C / Sven B | GPLv3 | [Tiny-invaders-v4.2](https://github.com/Lorandil/Tiny-invaders-v4.2) | [<img src="metadata/screenshots/TINY%20INVADERS.png" width="160">](metadata/screenshots/TINY%20INVADERS.png) |
| Tiny Lander | Roger Buehler (tscha70) | GPLv3 | [TinyLanderV1.0](https://github.com/tscha70/TinyLanderV1.0) | [<img src="metadata/screenshots/TINY%20LANDER.png" width="160">](metadata/screenshots/TINY%20LANDER.png) |
| Tiny Mania | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20MANIA.png" width="160">](metadata/screenshots/TINY%20MANIA.png) |
| Tiny Minez | Sven B / Lorandil | GPLv3 | [TinyMinez](https://github.com/Lorandil/TinyMinez) | [<img src="metadata/screenshots/TINY%20MINEZ.png" width="160">](metadata/screenshots/TINY%20MINEZ.png) |
| Tiny Missile | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20MISSILE.png" width="160">](metadata/screenshots/TINY%20MISSILE.png) |
| Tiny Morpion | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20MORPION.png" width="160">](metadata/screenshots/TINY%20MORPION.png) |
| Tiny Pacman | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20PACMAN.png" width="160">](metadata/screenshots/TINY%20PACMAN.png) |
| Tiny Pinball | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20PINBALL.png" width="160">](metadata/screenshots/TINY%20PINBALL.png) |
| Tiny Pipe | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20PIPE.png" width="160">](metadata/screenshots/TINY%20PIPE.png) |
| Tiny Plaque | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20PLAQUE.png" width="160">](metadata/screenshots/TINY%20PLAQUE.png) |
| Tiny SQuest | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20SQUEST.png" width="160">](metadata/screenshots/TINY%20SQUEST.png) |
| Tiny Trick | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20TRICK.png" width="160">](metadata/screenshots/TINY%20TRICK.png) |
| Tiny Tris | Daniel C | GPLv3 | [tinyjoypad.com](https://www.tinyjoypad.com/tinyjoypad_attiny85) | [<img src="metadata/screenshots/TINY%20TRIS.png" width="160">](metadata/screenshots/TINY%20TRIS.png) |
| TinyRoG | Kondolab | None specified | [note.com](https://note.com/kondolab/n/n1806e4234495) | [<img src="metadata/screenshots/TINYROG.png" width="160">](metadata/screenshots/TINYROG.png) |
| UFO | Ilya Titov | Non-commercial, with attribution | [AttinyArcade](https://github.com/webboggles/AttinyArcade) | [<img src="metadata/screenshots/UFO.png" width="160">](metadata/screenshots/UFO.png) |
| Wren Rollercoaster | Andy Jackson | Non-commercial, with attribution | [Attiny-Arduino-Games](https://github.com/andyhighnumber/Attiny-Arduino-Games) | [<img src="metadata/screenshots/WREN%20ROLLERCOASTER.png" width="160">](metadata/screenshots/WREN%20ROLLERCOASTER.png) |

## Credits

- [tinyjoypad_vircon32](https://github.com/joyrider3774/tinyjoypad_vircon32) -
  the original Vircon32 port this project is itself ported from: every
  game's own C logic (`src/gameworld/games/`), already ported once from
  the original ATtiny85 sources and already bug-fixed through its own
  extensive play-testing history, was carried over here essentially
  unchanged - only a mechanical dialect conversion (see `CLAUDE.md`)
  touched it to make it build as standard C instead of Vircon32's own
  restricted dialect. This project's own platform layer (SDL3/SDL2/
  Playdate) is new; the game code underneath it is not.
- [Vircon32](https://www.vircon32.com/) - the fantasy console the game
  logic here was originally ported through. This project's own menu/
  dialog text is drawn with a faithful reproduction of Vircon32's real
  BIOS font (`src/gameworld/biosFont.h` - 10x20px, codepage 1252, all 256
  glyphs extracted directly from the actual BIOS font asset, not hand-
  transcribed), authored by **Carra** (Vircon32's own author) and
  published on OpenGameArt.org as *"Pixel Art Outlined Text Fonts"* under
  **CC-BY 4.0** - a separate license from this project's own GPLv3 below;
  this credit is that license's own attribution requirement.
- `crisp-game-lib-portable-sdl` (same author, a different game collection) -
  the source of the `CInput` keyboard/gamepad abstraction, the single-
  voice audio oscillator shape, the original inspiration for the glow/
  CRT/pixel-grid presentation effects, and (for the SDL2 and Playdate
  ports) the direct API-porting reference - see `CLAUDE.md` for exactly
  where each was reused versus redesigned.
- Every individual game's own original author/license is preserved
  unmodified in its own header comment under `src/gameworld/games/` and
  listed per-game in the "Games" table above.

## License

This project is **GPLv3** (`LICENSE.txt`), for the same reason: at least
one ported game (Tiny Invaders v4.2) is itself GPLv3, and combining GPLv3
code into one executable makes the whole executable a GPLv3 combined
work. This covers
this project's own new code (both the SDL3 and SDL2 platform layers, the
shared menu, the presentation effects) - each individual game's own
original license/attribution is preserved unmodified in its own header
comment in `src/gameworld/games/`.

## See also

- [CLAUDE.md](CLAUDE.md) - this project's own architecture, the dialect-
  conversion process, every SDL3-specific bug found during the port, and
  the presentation-effects design.
- [OPTIMIZATIONS.md](OPTIMIZATIONS.md) - performance notes specific to
  this SDL3 build (a real desktop CPU/GPU, not Vircon32's fixed 250,000-
  cycle/frame budget - a genuinely different cost model).
- [tinyjoypad_vircon32/CLAUDE.md](https://github.com/joyrider3774/tinyjoypad_vircon32/blob/main/CLAUDE.md) -
  the full per-game porting history (every game-logic bug found and fixed,
  the AVR-vs-Vircon32 dialect bug family, per-game optimization passes)
  that this project inherited along with the ported C code itself.
