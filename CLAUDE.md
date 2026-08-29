# TinyJoypad → SDL3 Port

## Goal

A native SDL3 port of the sibling `tinyjoypad_vircon32` project (41
TinyJoypad games behind one shared menu, originally targeting the
Vircon32 fantasy console) - same games, same menu shape, but as a plain
desktop executable with no emulator dependency. This file is project
context for Claude Code, mirroring the sibling project's own `CLAUDE.md`
in spirit: read it before making changes, keep it updated as decisions
get made.

**This project's own history is short relative to the sibling one on
purpose.** The actual game logic - every bug found and fixed, every
per-game optimization - lives in `tinyjoypad_vircon32`'s own `CLAUDE.md`/
`OPTIMIZATIONS.md`, and this port reused those files essentially
unchanged (only a mechanical dialect conversion touched them - see
below). Don't duplicate that history here; link to it. What belongs in
*this* file is what's actually specific to the SDL3 platform layer: the
architecture, the dialect-conversion process itself, and the bugs/design
decisions that only exist because this is a different target than
Vircon32.

## Relationship to sibling projects

- **`tinyjoypad_vircon32`** - the source of every game's own C logic
  (already ported once from the original ATtiny85 `.ino` sources, already
  bug-fixed through extensive real play across a long session history).
  This project's `src/gameworld/games/*.c` files are that project's own
  files, mechanically converted (see "Dialect conversion" below) - not a
  re-port from the original upstream `.ino` sources.
- **`crisp-game-lib-portable-sdl`** (same author, a different game
  collection with its own Vircon32-ported sibling too - the same kind of
  "port a Vircon32 cartridge to native SDL3" job, done once before) - the
  source of the CInput keyboard/gamepad abstraction (used near-verbatim),
  the single-voice audio oscillator shape, and the original inspiration
  for the glow/CRT/pixel-grid presentation effects (NOT ported verbatim -
  see "Presentation effects" below for why the actual implementations
  differ).

## Architecture

### Translation-unit boundary

Two separate compiled halves, communicating only through
`machineDependent.h`'s plain declarations - mirrors exactly how
`tinyjoypad_vircon32` separates `machineDependent.h` from
`portVircon32.c`:

- **"Game world"** (`src/gameworld/`) - `avrCompat.h` + `tinyJoypadShim`/
  `obonoCoreShim` + `menu.c`/`menuGameList.c` + all 41 `games/*.c` files.
  **Never includes `SDL.h`** (or anything that would pull in `<stdint.h>`,
  which would conflict with `avrCompat.h`'s own `uint8_t`-aliased-to-`int`
  typedefs if both were ever visible in the same translation unit).

  Each shim/menu/game `.c` file is its own separately-compiled translation
  unit (an ordinary multi-file C project, one `.c` -> one `.o`, linked
  together at the end) - **not** the single-TU-via-`#include` shape the
  sibling Vircon32 build's own `main.c` uses (an earlier revision of this
  project did mirror that shape; changed on direct user request, since it
  meant every one of the 33 games effectively had to recompile on any
  single-game edit, and no game could be edited/compiled in isolation).
  Two things had to change to make that split safe:
  - `avrCompat.h`/`biosFont.h`/`menuFont.h` are still header-only (no
    matching `.c`), but every array/function they define is now `static` -
    when the whole game world was one TU, each was only ever `#include`d
    once, so a plain (non-`static`) definition was safe; now that up to 35
    different TUs can each `#include` the same header, `static` gives each
    TU its own private copy instead of 35 conflicting external
    definitions ("multiple definition of ..." at link time). See each
    file's own comment at the point `static` was added for the specific
    reasoning (`avrCompat.h`'s own `itoa()` needed an extra trick - a
    `#define itoa avrCompatItoa` redirect - since MinGW's `<stdlib.h>`
    already declares a *non-static* `itoa()` of its own, and C won't let a
    later `static` declaration narrow an already-non-static one in the
    same TU).
  - **No per-game header.** Every game's own public surface is exactly
    `gameXxx_init`/`gameXxx_update`/(`gameXxx_forceRedraw`, only for the
    ~18 games that have their own - see `menu.h`'s `Game.onResume`) - three
    functions, no shared types, no cross-game calls (every game already
    has fully-unique prefixed names, so there's nothing else for a
    per-game header to usefully declare). Instead, `games/games.h` is ONE
    shared header declaring that same tiny surface for all 41 games -
    `menuGameList.c`'s own `addGames()` is the only file that includes it
    (it's the only place that needs every game's address at once, to
    build the `Game` table `menu_getGame()` indexes into). Individual
    `games/gameXxx.c` files don't include it themselves - each one's own
    `_init`/`_update`/`_forceRedraw` definitions need no prior prototype
    in their own file, only `avrCompat.h` + `machineDependent.h` +
    whichever one of `tinyJoypadShim.h`/`obonoCoreShim.h` matches that
    game's own lineage (see each game's own header comment for which,
    exactly as it already documented before this change - only *how* that
    dependency was satisfied changed, from implicit `#include`-order
    visibility to an explicit `#include` at the top of the file).
- **"SDL platform backend"** (`src/sdl3/` - the SDL3 *port*, see "Directory
  layout / multi-port structure" below) - `sdlBackend.c` (every
  `machineDependent.h` function), `main.c` (entry point, CLI parsing, the
  frame loop), `CInput.c/.h` (input), and the presentation-effect modules
  (`glowEffect.*`, `crtEffect.*`, `pixelGridEffect.*`). Freely includes
  `SDL.h`.

### Directory layout / multi-port structure

```
SDL3/              <- vendored SDL3 checkout (repo root, not inside src/sdl3/)
SDL2/              <- vendored SDL2 checkout (repo root, not inside src/sdl2/)
assets/            <- thumbnails etc - port-agnostic content (SDL ports only);
                      thumbnails/thumbnailData.h here is generated, embedded
                      into both SDL ports' own exe at compile time (see
                      "Thumbnails" below), not shipped/read at runtime
tools/             <- gen_thumbnails.py - regenerates that header from the
                      checked-in thumb_NN.bmp source files
src/
  gameworld/       <- the "game world" side above - every port reuses this
                      almost entirely UNCHANGED (see "The Playdate port"
                      below for the two genuine exceptions); it never
                      #includes SDL.h/pd_api.h and knows nothing about any
                      specific port
    games/
  sdl3/            <- the SDL3 port: its own *.c/*.h, its own standalone
                      CMakeLists.txt, own build/ output dir
  sdl2/            <- the SDL2 port: same shape as sdl3/, own *.c/*.h/
                      CMakeLists.txt/build/
  playdate/        <- the Playdate port: one main.c (SDK convention - see
                      its own header comment), its own CMakeLists.txt (SDK-
                      template-shaped, not this repo's other two ports' own
                      shape - see that file's own comment), Source/pdxinfo
```

**No top-level `CMakeLists.txt`.** Each port under `src/` is a fully
independent CMake project (its own `project()`, its own `add_executable()`/
`add_library()`) built by `cd`-ing into it directly (`cd src/sdl3 && cmake
-B build ...`, `cd src/sdl2 && ...`, `cd src/playdate && ...`) - not a
subdirectory pulled in by a repo-wide orchestrator. This is a direct,
deliberate mirror of the same-author sibling `crisp-game-lib-portable-sdl`
project's own layout (`src/games/` + `src/lib/` shared, then one directory
per port - `src/cglpSDL3/`, `src/cglpSDL2/`, `src/cglpPlaydate/`,
`src/cglpPyBadge/`, ... - each with its own standalone `CMakeLists.txt`),
adopted here on direct user request specifically so a second (and third)
port could reuse `src/gameworld/` (all 41 games included) with minimal or
no changes, by just adding a new `src/<portname>/` directory alongside
`src/sdl3/`, with its own `CMakeLists.txt` globbing `../gameworld` the same
way `src/sdl3/CMakeLists.txt` already does - no shared root build file to
edit or fight over, and no existing port's own build is affected by adding
a new one. `src/sdl2/` is the cleanest proof: built entirely by copying
`src/sdl3/`'s own files and porting the SDL3->SDL2 API differences, with
*zero* changes anywhere under `src/gameworld/`. `src/playdate/` (below)
needed exactly two, both genuine latent bugs unrelated to Playdate itself
- found only because that port happened to be the first one to compile
under different-enough conditions to expose them.

An earlier revision of this project had one repo-root `CMakeLists.txt` and
`src/platform/` instead of `src/sdl3/` (single-port, no multi-port
provision) - `src/platform/` was renamed to `src/sdl3/` and the
`CMakeLists.txt` moved down into it essentially unchanged (only the
relative paths reaching `../../SDL3`, `../gameworld`, and `../../assets`
are new) as part of this restructuring. `README.md`'s own Building section
was updated to match - the build command now starts with `cd src/sdl3`
(or `cd src/sdl2`).

#### The SDL2 port (`src/sdl2/`)

Built by copying every file from `src/sdl3/` and mechanically porting each
SDL3 API call to its SDL2 equivalent, cross-checked against the sibling
`crisp-game-lib-portable-sdl` project's own `src/cglpSDL2/` (both its
`CInput.c` - button/axis/event-constant names - and `cglpSDL2.c` - window/
renderer/audio setup) wherever a design choice, not just a rename, was
needed. Verified functionally identical to the SDL3 port afterward, not
just "it compiles": every numeric constant (audio amplitude/sample rate,
glow/CRT intensities, joystick deadzones, the 0.05 volume step) diffed
equal, every button/keyboard/axis/mouse-event field mapping table diffed
identical once enum names are stripped out, `main.c` and `sdlBackend.h`
diff to *only* comment/include-line changes (zero logic differences), and
both ports independently pass the full 33-game `-ms` batch-screenshot
regression (every game's real `init()`/`update()`, not just a smoke test)
with no crashes. The one deliberate non-literal choice - `SDL_WINDOW_FULLSCREEN_DESKTOP`
or `0` for the SDL2 port's fullscreen flag, vs. the SDL3 port's plain
`SDL_WINDOW_FULLSCREEN`/`SDL_SetWindowFullscreen(win, bool)` - still
produces the same real behavior on both: SDL3 changed `SDL_WINDOW_FULLSCREEN`'s
own default meaning to "borderless, current desktop resolution" (an actual
exclusive display-mode change needs an explicit `SDL_SetWindowFullscreenMode()`
call this project never makes), which is exactly what SDL2's
`FULLSCREEN_DESKTOP` flag has always meant - so despite the different flag
name, neither port ever does a real display-mode switch.

Real API differences found doing this port (useful reference for any
*future* port too, not just this pair):
- **Letter-key keycodes are cased differently**: SDL2's `SDLK_x`/`SDLK_c`/
  `SDLK_s`/`SDLK_g`/`SDLK_d` are lowercase (ASCII-value-derived, SDL2's
  original convention); SDL3 renamed every letter keycode to uppercase
  (`SDLK_X`/etc, matching the physical key label instead). Non-letter keys
  (arrows, Escape, PageUp/Down, F-keys) are unaffected - only the 5 letter-
  key `BUTTON_*` macros in `tinyjoypadSDL2.h` needed the case flipped back;
  found immediately as a build error (`'SDLK_X' undeclared`), not a silent
  bug, but worth knowing before writing a future port's own keybind header
  from scratch instead of copying this one.
- **`SDL_ScaleMode` enum naming**: SDL2's `SDL_ScaleModeNearest`/
  `SDL_ScaleModeLinear` (mixed-case, no underscore) vs SDL3's
  `SDL_SCALEMODE_NEAREST`/`SDL_SCALEMODE_LINEAR` (all-caps with
  underscore) - same values, different spelling convention.
  `SDL_SetTextureScaleMode()` itself has the same name/signature in both.
- **Surface creation/color-mapping needs the legacy long form**: SDL3's
  `SDL_CreateSurface(w,h,format)` -> SDL2's `SDL_CreateRGBSurfaceWithFormat(0,w,h,32,format)`
  (flags+depth never got dropped in SDL2); SDL3's `SDL_MapSurfaceRGBA(surface,...)`
  -> SDL2's `SDL_MapRGBA(surface->format,...)` (routes through the
  surface's own `SDL_PixelFormat*`, not the surface pointer directly).
  `SDL_FillSurfaceRect`/`SDL_DestroySurface` -> SDL2's `SDL_FillRect`/
  `SDL_FreeSurface`.
- **Texture-to-renderer draw call renamed**: SDL3's `SDL_RenderTexture()`
  -> SDL2's `SDL_RenderCopy()` (same "whole texture to whole target" shape
  with NULL/NULL source+dest for this project's own always-fullscreen
  draws); SDL3's rect args are `SDL_FRect*` (float), SDL2's are plain
  `SDL_Rect*` (int) - no precision loss here since every value involved is
  already a whole pixel position.
- **No logical-presentation-mode API**: SDL3's `SDL_SetRenderLogicalPresentation()`
  (configurable letterbox/stretch/overscan/integer-scale modes) has no
  SDL2 equivalent - SDL2's own `SDL_RenderSetLogicalSize()` (present since
  SDL 2.0.0) does letterboxed, aspect-preserving scaling unconditionally,
  which is the only mode this project ever asked SDL3's own version for
  anyway, so the behavior carries over exactly despite the API being
  simpler/older.
- **No `SDL_AudioStream` convenience API**: SDL3's `SDL_OpenAudioDeviceStream()`
  (hand it a callback, get back a stream object, call `SDL_PutAudioStreamData()`
  from inside the callback to supply samples) is an SDL3-only addition.
  SDL2's classic `SDL_OpenAudioDevice()` model is actually simpler for this
  project's own needs: the callback (`void(void*, Uint8*, int)`, not
  SDL3's `void(void*, SDL_AudioStream*, int, int)`) fills the destination
  buffer directly in-place, no separate "put" call needed. SDL2 devices
  also start **paused** (needing an explicit `SDL_PauseAudioDevice(dev, 0)`
  to actually hear anything), unlike SDL3's stream-based devices which
  start already running.
- **Gamepad API family fully renamed**: SDL2's `SDL_GameController*`
  types/functions (`SDL_GameControllerOpen`/`Close`, `SDL_IsGameController`,
  `SDL_GameControllerEventState`, `SDL_CONTROLLER_BUTTON_*`/`_AXIS_*`) all
  became SDL3's `SDL_Gamepad*` family (`SDL_OpenGamepad`/`SDL_CloseGamepad`,
  `SDL_IsGamepad`, `SDL_SetGamepadEventsEnabled`, `SDL_GAMEPAD_BUTTON_*`/
  `_AXIS_*`) - same physical-button mapping either way (SDL2's own
  label-based `_Y`/`_X`/`_B`/`_A` button names line up with SDL3's
  position-based `NORTH`/`WEST`/`EAST`/`SOUTH` renaming exactly, standard
  Xbox-pad layout both ways), just a different name per symbol. SDL2 also
  has no `SDL_GetJoysticks()`-style array+count enumeration (an SDL3
  addition) - `SDL_NumJoysticks()` + an index loop is SDL2's own
  equivalent. One easy-to-transpose gotcha: SDL2's trigger axis names are
  `_TRIGGERLEFT`/`_TRIGGERRIGHT` (LEFT/RIGHT *after* TRIGGER), SDL3's are
  `_LEFT_TRIGGER`/`_RIGHT_TRIGGER` (the reverse word order).
- **Event-type constants dropped the `SDL_EVENT_` scheme**: SDL2 keeps its
  own historical per-event names (`SDL_QUIT`, `SDL_KEYDOWN`/`KEYUP`,
  `SDL_CONTROLLERBUTTONDOWN`/`UP`, `SDL_CONTROLLERAXISMOTION`,
  `SDL_JOYDEVICEADDED`/`REMOVED`, `SDL_RENDER_TARGETS_RESET`) rather than
  SDL3's unified `SDL_EVENT_CATEGORY_DETAIL` naming - a straight rename
  per constant, same semantics. The keycode field also nests one level
  deeper: SDL2's `event.key.keysym.sym`, not SDL3's flattened
  `event.key.key`.
- **`SDL_GetTicks()` return width**: `Uint32` (wraps after ~49 days) in
  SDL2 vs `Uint64` in SDL3 (added specifically to remove that wraparound)
  - only used here for a single per-frame delta (the CRT scroll speed), so
  the eventual wraparound is a one-frame glitch at worst, not a
  correctness issue worth working around.

#### The Playdate port (`src/playdate/`)

A genuinely different kind of port from `src/sdl2/`'s own mechanical API
translation: Playdate is fixed 400x240 1-bit hardware with its own C SDK
(`pd_api.h` - windowless, event-callback-driven, no CLI), not another
desktop windowing library, so this port is built directly against the
official Playdate SDK, cross-checked against the same-author sibling
`crisp-game-lib-portable-sdl` project's own `src/cglpPlaydate/src/cglpPlaydate.c`
(per direct user request) for the parts that are genuine *design*
questions (how `pd->display->setScale()`/`setOffset()` map a small logical
canvas onto the real panel, the synth-based audio approach, the SDK's own
`eventHandler()`/`update`-callback shape) rather than a line-by-line
translation the way `src/sdl2/` was.

Deliberately out of scope, per direct user request ("we don't need the
effects, we don't need color to pattern stuff... we don't need all the
command line and SDL specific stuff"):
- The glow and CRT-scanline presentation effects (`glowEffect.*`/
  `crtEffect.*`) - meaningless on a fixed 1-bit panel, not built at all for
  this port (`src/playdate/` has no equivalent files). The pixel-grid
  effect was later added anyway, on direct user follow-up request after
  the rest of the port was otherwise complete - see "Pixel-grid effect +
  system menu" below; it reads fine on a 1-bit panel since it's just black
  grid lines, unlike the other two which assume color/blur.
- Any color-to-dither-pattern conversion - unlike crisp-game-lib's own
  RGB-per-object model (cglpPlaydate.c's own `md_drawRect()` converts an
  arbitrary `r,g,b` into an `LCDPattern` via bit-rotation), every TinyJoypad
  game's own pixel data is already pure black/white (an SSD1306 OLED byte),
  so `md_drawColumn()` just picks `kColorBlack` for a set bit, no pattern
  math needed.
- Every SDL-port CLI flag (`-w`/`-h`/`-f`/`-ns`/`-fps`/`-nd`/`-list`/`-g`/
  `-ms`/`-joy`) and the volume/mute/fullscreen keybinds added to the SDL
  ports this same session (`BUTTON_VOLUP`/`VOLDOWN`/`SOUNDSWITCH`/
  `GLOWSWITCH`/F3) - no shell to launch from, no window to un-fullscreen,
  no keyboard, and system volume is the player's own hardware controls.

**A brand-new, Playdate-native menu, not `gameworld/menu.c`'s own** - the
single largest design departure, and flagged as likely necessary by the
user themselves before any code was written. `gameworld/menu.c`'s own
`menu_update()` assumes a real 640x360 canvas and draws through the BIOS
font (`biosFont.h`, 10x20px glyphs) with thumbnail images sized for that
canvas - none of that fits a 400x240 1-bit panel, and downscaling a virtual
640x360 canvas to 400x240 on every frame would be exactly the kind of
manual scale/offset math this project's own SDL ports deliberately avoid
(see "Rendering model" above). `src/playdate/main.c`'s own `menuUpdate()`
is a from-scratch replacement instead, but ended up matching
`gameworld/menu.c`'s own *feature set* almost exactly (per direct user
follow-up requests after the first working version) even though the
rendering underneath is entirely different: Playdate's own system font
(`/System/Fonts/Roobert-10-Bold.pft`, loaded by path - every Playdate
already ships every `/System/Fonts/*` font, nothing to bundle) drawn via
`pd->graphics->drawText()`, a real title line, a keybind hint line, a
zero-padded-numbered ("01.", "02.", ...) alphabetized list (a local
`gDisplayOrder[]` - the exact same selection-sort-by-title algorithm as
`gameworld/menu.c`'s own private `displayOrder[]`, just re-implemented
here rather than exposing that array through a new cross-port menu.h
accessor), genuine LEFT/RIGHT-jumps-a-page pagination (a `PAGE 1/4`-style
indicator, not the first version's own continuous-scroll list - fewer
games fit per page here than `gameworld/menu.c`'s own `GAMES_PER_PAGE==9`,
since this port's own header/hint/page-indicator lines and the thumbnail
column below eat more vertical/horizontal room), and a real gameplay
thumbnail + "BY <author>" caption for the currently-selected game. It
still walks the exact same `menu.h` `Game` table (`menu_getGame()`/
`gameCount`, populated by the completely unmodified `menuGameList.c`'s own
`addGames()`) - only the rendering/navigation code around that table is
new, not the registration data itself. Also skips `gamesMain.c`'s own
quit-*confirmation* dialog entirely (no BIOS font to draw it with either,
and per direct user confirmation that no confirmation step is wanted here)
in favor of matching `cglpPlaydate.c`'s own simpler precedent exactly:
holding A+B+Up+Right together returns to the menu immediately, no YES/NO
prompt - an unlikely-to-happen-by-accident chord none of these 33 games'
own real controls use simultaneously.

**Thumbnails**: `src/playdate/Source/thumbnails/thumb_00.png` ..
`thumb_32.png`, generated from the exact same `assets/thumbnails/*.bmp`
every SDL port already uses (`magick thumb_NN.bmp -sample 128x64
thumb_NN.png` - point-sampled, not a blurring resize filter, to keep the
already near-monochrome source crisp before Playdate's own `pdc` build
tool dithers it to 1-bit) - downscaled exactly 2x from their original
256x128, which lands precisely on TinyJoypad's own real OLED resolution,
not a coincidence-free choice. Loaded once at startup via
`pd->graphics->loadBitmap()` (a Playdate asset is referenced by path minus
extension - `pdc` itself converts a bundled `Source/*.png` into its own
runtime bitmap format at build time), probed sequentially the same way
`md_getThumbnailCount()` does on every other port, and drawn/captioned via
the exact same registration-index indirection `gameworld/menu.c`'s own
thumbnail code uses (`gDisplayOrder[selection]`, not `selection` directly -
the thumbnail *set* is keyed by registration order, matching how the files
were generated, not by alphabetized display position).

**Video**: `md_drawColumn()` scales manually - `GAME_SCALE` (3) times the
native OLED resolution (128x64) lands almost exactly on the real panel
(384x192, centered with an 8px/24px border via `GAME_ORIGIN_X`/`_Y`) -
matching every SDL port's own identical `GAME_SCALE` multiply, just
emitting `pd->graphics->fillRect()` calls instead of
`SDL_FillSurfaceRect()` ones. This is a deliberate *reversal* of this
port's own first working design, which tried using
`pd->display->setScale()`/`setOffset()` instead (see "Bugs found" below
for why that had to be abandoned) - `md_setInGame()` is consequently a
near-no-op now (nothing left to toggle, since there's no longer any
mode-dependent display transform at all), and `md_beginFrame()`/
`md_endFrame()` don't touch `pd->display` in any way, only
`pd->graphics->clear()`. `kColorWhite` for an "on" bit against a
`kColorBlack` background - direct user choice, overriding an earlier
revision of this port's own reasoning here (which had the two swapped:
`kColorBlack` "on"/`kColorWhite` background, on the theory that Playdate's
reflective panel - dark ink on a light background, like paper - is the
opposite polarity from a real SSD1306 OLED, where a "lit" pixel is the
bright foreground, matching `cglpPlaydate.c`'s own default non-"dark-
color" convention). The current choice matches the original OLED's own
polarity directly instead (bright "on" pixels on a dark background), not
inverted for the panel.

**Pixel-grid effect + system menu** - the one SDL-port presentation effect
this port *does* get an equivalent of, added on direct user request after
the rest of the port was otherwise complete ("we don't need the effects" in
the original scope note above was about glow/CRT specifically, not this).
Same "pre-baked once, composited every frame" design as
`pixelGridEffect.c`'s own SDL version (see "Presentation effects" above) -
`pixelGridEffectInit()` (called once from `init()`) draws a `GAME_SCALE`-
spaced 1px black grid into one `GAME_SCALE*OLED_WIDTH` x
`GAME_SCALE*OLED_HEIGHT` `LCDBitmap`, and `pixelGridEffectRender()` (called
from `update()`'s gameplay branch, after that game's own `update()`, gated
to gameplay only exactly like every SDL port's own three effects) just
`drawBitmap()`s it at `GAME_ORIGIN_X`/`_Y` every frame - no runtime
per-pixel drawing, matching the CRT/pixel-grid SDL modules' own "bake once,
scroll/composite thereafter" lesson from earlier in this project. Unlike
the SDL ports (one shared `G`/West-button cycle across all three effects),
this port has no equivalent spare physical button, so it's exposed through
Playdate's own **system menu** instead (`pd->system->addCheckmarkMenuItem()`,
opened by the player's own physical Menu button during gameplay) - default
OFF, state persists across games/menu visits within the same session (the
checkbox is seeded from `gPixelGridEnabled` itself, not a hardcoded 0, each
time a game launches and re-adds the menu items). The same system menu also
gained a `"Menu"` entry (`pd->system->addMenuItem()`) as a second way to
reach `returnToMenu()`, alongside the existing A+B+Up+Right chord - both
call the same function, so there's exactly one place that resets
`gCurrentGameIndex`/tears down the menu items. Both entries are added when
a game launches and removed via `pd->system->removeAllMenuItems()` when
returning to the in-app menu (either path), since neither makes sense while
already in the game-select menu.

Toggling the checkbox needed one more fix, found by direct user report
before it shipped: flipping it OFF could leave the grid lines "burned in"
on screen instead of disappearing, for the same reason `menu.h`'s own
`onResume` hook exists at all (see its own comment) - a game that skips its
own redraw on frames where nothing changed (an isInvalid-style dirty-flag
optimization, only some games have it) won't naturally paint over pixels
nothing else is touching, so a grid line drawn on a now-static frame just
stays there once the overlay stops being redrawn on top of it, exactly the
same "frozen screen with stale pixels" failure `gamesMain.c`'s own quit-
dialog-resume path already forces a redraw to avoid. Fixed the same way:
`pixelGridMenuCallback()` calls the current game's own `onResume()` (if it
has one) right after reading back the new checkbox value, forcing one real
full redraw regardless of which direction the toggle went - cheap enough
(a rare, player-initiated toggle) not to bother special-casing ON
(which never had the staleness problem, since it only adds pixels) vs OFF.

**Audio**: one `PDSynth` (not `cglpPlaydate.c`'s own `SYNTH_COUNT==4`
round-robin pool, needed there for crisp-game-lib's own overlapping multi-
note effects - TinyJoypad's original hardware is a single piezo buzzer, so
every shim's `Sound()`/`playTone()` call already expects exactly one tone
at a time, matching every other port's own single-voice choice) with
`setWaveform(synth, kWaveformSquare)` - matching every other port's own
square-wave finding (see the SDL3 bug entry below) for free, as a real
built-in oscillator shape rather than hand-written sample math. Playdate's
own `playNote(synth, freq, vel, len, when)` takes a duration and auto-stops
itself - `md_playTone()` just calls it directly with `when=0` ("now"), no
manual `gFrameCounter`/`gToneStopFrame` bookkeeping needed at all (unlike
every SDL port's own `md_updateAudio()`, which exists *only* for that
bookkeeping there). `gFrameCounter` itself still has to keep advancing
regardless, though: `obonoCoreShim.c`'s own note-sequencer (the 3
`obonoCoreShim`-lineage games) schedules its *own* next-note timing as
`md_getFrameCounter() + (durationMs/1000)*MD_FRAMES_PER_SECOND` - a shared
gameworld constant fixed at 60 (correct for the SDL ports, which really do
run at 60fps) this port can't change. Real Playdate hardware caps out at
**50fps** for full-screen refreshes (a hardware/panel limit, confirmed by
direct user correction after an earlier revision of this port requested
`MD_FRAMES_PER_SECOND` directly) - `pd->display->setRefreshRate(50)`
(a new `PLAYDATE_REFRESH_RATE` constant, not `MD_FRAMES_PER_SECOND`) asks
for the real achievable rate instead of the wrong one, but that alone
would leave a 60-vs-50 mismatch: advancing `gFrameCounter` by a flat +1
per real `update()` callback would make `obonoCoreShim`'s own music/timing
run `60/50 = 1.2x` too *slow* in real wall-clock time, since its own math
still assumes 60 "frames" happen per real second. `md_updateAudio()`
compensates with a fractional accumulator instead (`gFrameCounterAccumulator
+= MD_FRAMES_PER_SECOND / PLAYDATE_REFRESH_RATE` every real callback,
incrementing the real integer counter only once that accumulates past a
whole number) - `md_getFrameCounter()` ends up advancing at the same real-
time rate `obonoCoreShim`'s own math already assumes, regardless of the
mismatch between the two frame rates.

**Bugs found doing this port** (both genuine latent bugs in shared
`gameworld/` code, not anything Playdate-specific - just the first port to
expose them):
- **`obonoCoreShim.h`/`tinyJoypadShim.h` used `bool` without their own
  `#include <stdbool.h>`**, relying on it arriving transitively from
  `machineDependent.h` (included *after* them in some `.c` files, e.g.
  `obonoCoreShim.c` itself: `avrCompat.h` -> `obonoCoreShim.h` ->
  `machineDependent.h`). This was silently fine under both SDL ports'
  default `-std=` (new enough GCC defaults make `bool`/`true`/`false` real
  keywords needing no header at all) - it broke immediately, as a real
  compile error (`unknown type name 'bool'`), the moment this port's own
  CMakeLists.txt set `CMAKE_C_STANDARD 11` (matching the official Playdate
  SDK template's own line), which brings back the classic C11 rule that
  `bool` is only a `<stdbool.h>` macro. Fixed by giving both headers their
  own `#include <stdbool.h>` rather than reordering any one `.c` file's own
  includes, which would've just left the same fragility for the next file
  that happens to include either of them first.
- The DLL-export guard (`main.c`'s `#ifdef _WINDLL __declspec(dllexport)`,
  needed for the Playdate Simulator to find `eventHandler` by name in the
  built `pdex.dll`) depends on `_WINDLL` being defined - the SDK's own
  `playdate.cmake` only defines it automatically for MSVC, not MinGW
  (this repo's own toolchain throughout, unlike the sibling cglpPlaydate
  project's own Visual-Studio-generator build). Fixed in this port's own
  `CMakeLists.txt` (`if(MINGW) target_compile_definitions(... _WINDLL)`),
  not by touching the SDK's own file.
- **`pd->display->setScale(3)` silently did nothing** - the very first
  working version of this port's own video code set scale/offset via
  `pd->display`, matching how it had first read about the API, and the
  scaled game canvas simply never appeared (reported directly: "the scale
  stuff seems to have no effect"). Root cause, found in the SDK's own docs
  (`Inside Playdate with C.html`): `setScale()`'s own doc comment states
  "Valid values for scale are 1, 2, 4, and 8" - 3 (the obvious choice,
  since `128*3=384`/`64*3=192` fits neatly under 400x240) was never a
  legal value at all. Worse, even a legal value wouldn't have behaved the
  way this port originally assumed: `setScale()` doesn't transform
  drawing-call *coordinates* the way SDL's own logical-presentation
  feature does - per the same doc comment, it re-samples a small **top-
  left region** of the real, always-400x240 framebuffer and magnifies
  just that region to fill the display ("the pixels in rectangle
  [0,100]x[0,60] are drawn on the screen as 4x4 squares" is the docs' own
  example, at scale 4). The one legal value that doesn't overflow (2,
  exposing a 200x120 sample region) would still only have used 128x64 of
  that available space, wasting most of the screen; scale 4 overflows
  outright (512x256 > 400x240) - no legal `setScale()` value could have
  hit the intended 384x192 fit at all. Fixed by abandoning
  `pd->display->setScale()`/`setOffset()` entirely in favor of manual
  per-draw-call scaling in `md_drawColumn()` (`GAME_SCALE`/`GAME_ORIGIN_X`/
  `_Y`, emitting pre-scaled `fillRect()` calls) - the same approach every
  SDL port already uses, just with no power-of-2 restriction to work
  around, so it can use scale 3 freely. Confirmed by screenshot: a
  launched game (2048) now fills the panel correctly at the intended
  scale, not the pre-fix "no visible effect at all" state.

**Verified working**, not just "it compiles": built and linked cleanly
(simulator DLL, `TARGET_SIMULATOR`/MinGW), packed into a `.pdx` (including
the bundled `thumbnails/*.png`, auto-converted by `pdc`) by the SDK's own
`pdc` tool, and confirmed via real screenshots of `PlaydateSimulator.exe`
actually running it end to end: the menu (title, hint line, the
alphabetized/numbered/paginated list - "01. 2048" first, digits sort
before letters, matching `gameworld/menu.c`'s own alphabetization
convention exactly - a live "PAGE 1/4" indicator, the selection cursor,
and the selected game's own real gameplay thumbnail + "BY OBONO" author
caption, vertically centered in the list area, updating live as the
selection moves between games) and actual gameplay (2048, launched via
the A button, its board correctly filling the panel at `GAME_SCALE`).

### `avrCompat.h` - the dialect shim that makes reusing the ported C tractable

`uint8_t`/`int8_t`/`uint16_t`/etc are deliberately aliased to plain `int`,
exactly as in the Vircon32 build - **load-bearing**, not a simplification
to revert: dozens of already-fixed truncation/wraparound/sentinel bugs
across the ported games' own history depend on no implicit byte-narrowing
ever happening. `PROGMEM`/`pgm_read_*`/`memcpy_P` are ordinary flat-memory
access (same as the Vircon32 build). Also provides `max`/`min` macros, a
portable `itoa()` (not standard/not on every libc), and the shared
`arand()` helper (non-negative-clamped `rand() % n`, matching every ported
game's own RNG-range-mismatch fix).

### Rendering model

**One persistent 640x360 `SDL_Surface` (`gScreen`)** - not a small 128x64
"OLED" framebuffer scaled up at present time. This matches the Vircon32
build's own real-screen-space model (that build's own `md_drawColumn()`
draws directly at final scaled screen coordinates too, never through a
separate small framebuffer) rather than the more "obvious" tiny-canvas
design, for two concrete reasons found while building this:

1. `md_drawSolidRect()`'s callers (the quit-confirmation dialog) and the
   BIOS-font menu text both already assume real 640x360 screen-space
   coordinates, matching the Vircon32 build's own dialog code exactly -
   using one shared canvas for game columns, dialog rects, and menu text
   avoids a second coordinate space entirely.
2. Skipping a frame's redraw and having the *previous* frame's pixels
   simply still be on screen (`obonoCoreShim`'s own `isInvalid`-gated
   skip; the quit-dialog's "game `update()` not called this frame"
   behavior) requires a genuinely *persistent* surface - an `SDL_Renderer`
   backbuffer's contents aren't guaranteed to survive across
   `SDL_RenderPresent()` calls on every backend, so this has to be real
   CPU-side memory the game world keeps alive itself.

`md_drawColumn(col, page, value)` masks `value &= 0xFF` (the same byte-
truncation fix the Vircon32 build needed, since `avrCompat.h`'s no-
narrowing `int`s mean upstream shift/OR sprite-compositing code can leave
stray high bits set) then draws each set bit as a real `GAME_SCALE x
GAME_SCALE` (5x5) filled rect - no texture atlas needed (SDL has neither
of Vircon32's GPU constraints: no CPU-writable-framebuffer restriction, no
1024x1024 texture-size cap that forced that build's own atlas design).

Final window-fit scaling is handled entirely by
`SDL_SetRenderLogicalPresentation()` (set once, at init) rather than any
manual scale/offset math or a resize-event watcher - it re-derives the
fit on every present.

### Menu font: the real Vircon32 BIOS font, not a game's own font

`biosFont.h` reproduces the actual Vircon32 BIOS font (10x20px, codepage
1252, 256 glyphs) as raw glyph-column data, extracted from the real
Vircon32 BIOS asset (`Font 10x20 Bios Vircon32.png`, authored by "Carra",
the Vircon32 project's own author - also published on OpenGameArt.org)
via a one-off Python/ImageMagick extraction, not hand-transcribed - per
direct user request to match the Vircon32 BIOS specifically rather than
reuse one of the in-project game fonts (`menuFont.h`'s 6x8 font, still
used by the handful of games - Oroboros/Run Dude Run/Dino Game - that
need their own page-aligned text). Drawn via `md_drawColumnPixels()` (an
absolute-pixel-row primitive), since 20px-tall glyphs don't line up with
an 8px SSD1306 "page" the way `md_drawColumn()` assumes.

### Thumbnails

`assets/thumbnails/thumb_00.bmp` .. `thumb_32.bmp` (256x128, cropped from
a real gameplay screenshot's 640x320 game area, indexed by registration
order - the same order `menuGameList.c`'s own `addGame()` calls happen
in, which is also what `menu.c`'s `displayOrder[]` indirection resolves
an alphabetized menu position back to before calling
`md_drawGameThumbnail()`). Generated via `-ms` (see "CLI parameters"
below) plus an ImageMagick crop/resize pass.

**Embedded in the binary, not loaded from disk at runtime.**
`tools/gen_thumbnails.py` reads every `assets/thumbnails/thumb_NN.bmp` and
emits `assets/thumbnails/thumbnailData.h` - a generated (not hand-edited)
header holding each file's raw bytes as a `static const unsigned char[]`,
plus a `gThumbnailBlobs[]` lookup table of `{data, len}` pairs.
`sdlBackend.c` on both SDL ports `#include`s it directly (each port's own
`CMakeLists.txt` adds `assets/thumbnails/` to its include path) and
decodes each blob in memory at first use via `SDL_IOFromConstMem()` +
`SDL_LoadBMP_IO()` (SDL3) / `SDL_RWFromConstMem()` + `SDL_LoadBMP_RW()`
(SDL2) - same BMP decoder either way, just fed a memory buffer instead of
a file path. `thumbnailsProbeIfNeeded()` still stops at the first blob
that fails to decode, preserving the old disk-probe's "future 34th game
with no thumbnail yet is a silent no-op" behavior, even though the blob
count is now compile-time-known rather than discovered by a missing-file
check. This replaced an earlier design (loading `SDL_LoadBMP()` from
`<exe-dir>/assets/thumbnails/`, resolved via `SDL_GetBasePath()`, with
`CMakeLists.txt` copying `assets/` next to the built exe after every
build) on direct user request - the built exe is now fully self-contained
with no sibling `assets/` directory to ship or find, so that post-build
copy step was removed from both ports' own `CMakeLists.txt`. The source
`.bmp` files themselves are unchanged and still live in `assets/
thumbnails/` (also still what `tools/gen_thumbnails.py` reads from) - only
how the platform layer gets the bytes into memory changed. Re-run the
script whenever a thumbnail is added, removed, or regenerated; its output
is derived, so it isn't meant to be hand-edited.

Unlike the Vircon32 build (which needed a 2nd atlas texture once its
first 4x8 grid hit the 1024x1024 texture-size cap), there's no size
constraint here to design around - each thumbnail is just its own
independently-decoded `SDL_Surface`.

### Presentation effects: glow, CRT scanlines, pixel-grid

Three self-contained, reusable platform-side modules
(`glowEffect.h/.c`, `crtEffect.h/.c`, `pixelGridEffect.h/.c` - each
freely reusable by any future SDL3 project, no dependency on this
project's own gameworld/machineDependent split beyond "renders through an
`SDL_Renderer`"), cycled through via a single button
(`BUTTON_GLOWSWITCH`/`G`/gamepad West) in the exact same 5-state sequence
as `crisp-game-lib-portable-sdl`'s own `cglpSDL3.c` (`ButX` handler):
**none → pixel-grid+glow → pixel-grid alone → CRT alone → glow alone →
back to none.** Pixel-grid and CRT are mutually exclusive by design
(both are "what kind of display is this" choices); glow is independent
and can combine with pixel-grid but, in this same cycle, never with CRT -
this is inherited unchanged from cglp's own state machine, not a new
design. All three only ever apply during actual gameplay, not the menu
screen (`md_setInGame()`, called once/frame from
`gamesMain_dispatchFrame()`, reports this to the platform side, matching
cglp's own `!isInMenu` gate on the same three effects).

**None of the three are literal ports of cglp's own implementations** -
cglp's own glow (`applyGlowToRect`/`applyGlowToCharacterPixel`) and pixel-
grid code both assume its own per-character-rect rendering model (a glow
border drawn around each individual on-screen "character" object, a grid
line spaced relative to its own `wscale` window-zoom factor) which
doesn't translate to this project's column-blit rendering model (there is
no per-object rect list to hook a per-object glow into) or its own much
larger fixed `GAME_SCALE` (applying cglp's literal pixel-grid spacing
formula unchanged at `GAME_SCALE=5` produces a checkerboard block-out, not
thin per-pixel outlines - confirmed directly via a test capture, see
"Bugs found" below). Each was redesigned around what this project's own
rendering model actually provides:

- **Glow** - a whole-screen "poor man's bloom": downscale the rendered
  frame (SDL's own linear filtering blurs it in the process), then let
  the **GPU** scale that small blurred texture back up to screen size via
  a hardware-accelerated, additively-blended `SDL_RenderTexture()` call.
  A first version did the upscale on the **CPU** (`SDL_Surface`-to-
  `SDL_Surface`, both at full 640x360) - measured directly (see "Bugs
  found" below) at a ~40x per-frame slowdown, since `SDL_Surface` blits
  are always software-bound regardless of renderer backend. Rewritten
  around a small GPU-scaled texture instead; re-measured back to a ~20%
  overhead.
- **CRT scanlines** - a periodic horizontal-stripe pattern, pre-baked
  into a texture *once* (not every frame, learning directly from the
  glow finding above) and scrolled over real time via two
  `SDL_RenderTexture()` calls/frame (a wraparound-scroll split, same
  shape as cglp's own version, just GPU-texture-backed instead of CPU-
  surface-blitted).
- **Pixel-grid** - a static "LCD pixel grid" overlay, also a one-time-
  baked texture: a thin (1px) black outline drawn at *every* source-
  pixel boundary (`GAME_SCALE` spacing), rather than cglp's own
  `pixelSize`-wide line every `pixelSize*2` pixels (which, again, only
  reads as a thin separator at cglp's own typically-small `wscale`
  values, not this project's much larger fixed scale).

## Dialect conversion

The mechanical fixes every `games/*.c`/shim file needed, converting from
Vircon32's dialect (see `tinyjoypad_vircon32`'s own `VIRCON32_C_DIALECT.md`
for the full list of what that dialect restricts/extends) back to
standard C:

1. `int[N] name;` → `int name[N];` (and the 2D form) - the single most
   common fix.
2. `struct Foo {...};` used bare afterward as `Foo x;` → `typedef struct
   {...} Foo;` (drop the redundant tag) - affected roughly 19 of 33 game
   files.
3. **`int*`/`int[N]` used for text strings → `char*`/`char[N]`** -
   Vircon32 strings are `int[]` (one 32-bit word per character); this is
   a genuine runtime-correctness fix, not just a compile nicety (a raw
   `int*` receiving a C string literal reinterprets its bytes as garbage
   ints) - see "Bugs found" below, this was found the hard way across
   several games in the first porting wave before later waves' own
   agents learned to self-check for it.
4. Vircon32-dialect function-pointer typedefs (`typedef void(int,int*)
   Name;`, with the `*` re-added at each *use* site) → real C typedefs
   (`typedef void (*Name)(int,int*);`, with the `*` living in the typedef
   itself, not at each use site) - affects `GameFunc`/`DrawFunc` in
   `menu.h`/`obonoCoreShim.h`.
5. No forward-declaration-ordering changes needed - Vircon32 requires
   strict definition-before-use (no linker); standard C is *more*
   permissive, so the existing carefully-ordered code compiles as-is.
6. No un-doing of already-applied Vircon32 restrictions (avoided
   ternary/`switch`/binary-literals/etc in various spots because
   Vircon32's dialect doesn't support them) - all of that is also valid
   standard C, so left alone rather than "restored" for no benefit.

## Porting process

Phases 0-3 (skeleton, SDL backend, a handful of proof-of-concept games,
the menu) were built by hand, proving the dialect-conversion recipe and
the rendering-model decisions above against real, varied code before
scaling. Phase 4 (the remaining ~30 games) was dispatched to background
agents in waves, grouped by shim lineage - per the user's explicit
go-ahead once the recipe was proven - with every agent's claimed
verification independently re-checked afterward (line counts, structural
greps for leftover Vircon32-dialect syntax, numeric data-table diffs
against the Vircon32 source, a `class`-keyword-absence check for the
hardest C++-flattened files) rather than trusted at face value - this is
what actually caught the bugs below, not the agents' own self-reports.

## Bugs found during this port (SDL3-specific - see the sibling project for game-logic bugs)

These are bugs in the *porting process itself* or the *new platform
layer* - not in any game's own logic, which was already correct C
inherited from the Vircon32 build. Every game-logic bug (the AVR-vs-
Vircon32 dialect bug family, real gameplay bugs found via play) lives in
`tinyjoypad_vircon32/CLAUDE.md` instead, since it's the same C in both
projects.

- **Systemic string-literal-to-`int*` bug (wave 1)** - several porting
  agents left `int* str`-typed parameters/variables receiving literal C
  strings (valid under Vircon32's `int[]`-string dialect, a real garbage-
  bytes-as-ints bug in standard C) - found and fixed across 4 games
  (Pong, UFO, WrenRollercoaster, Stacker) by direct inspection; later
  waves' agents were briefed on the exact pattern and self-caught/self-
  fixed it via named `int[]`-character-code array replacements instead.
- **`gameTinySQuest.c`'s `tsqBackgroundData` transcription bug** - the
  porting agent's own claimed "MD5-hash-verified all 23 tables identical"
  turned out to be a false negative: its own numeric-diff script used the
  wrong search marker against the *still-Vircon32-dialect* original file
  (`name+'['` doesn't match `int[1024] tsqBackgroundData =`, since type
  comes first in that dialect), silently comparing against an unrelated
  later expression instead of the real array. A real mismatch (1024 vs
  1004 values, a dropped row) was found only via an independent re-check
  with a corrected marker - fixed by regenerating the whole array from
  the verified-correct original rather than hand-patching, matching this
  project's own "don't try to eyeball-fix a large data table" precedent.
- **Windows console-attach stdout-clobbering bug** - `main.c`'s console-
  attach block (needed since the executable links `-mwindows`, a GUI-
  subsystem binary with no console by default) was modeled on
  `cglpSDL3.c`'s own identical-looking block, which unconditionally
  `freopen("CON", ...)`s stdout onto the console the instant
  `AttachConsole()` succeeds - this silently discards the shell's own
  explicit redirection (`-list > file.txt` produced an empty file) even
  though `cglpSDL3.c` has the identical bug. Fixed (a genuine improvement
  over the reference code, not just a port) by checking
  `GetStdHandle(STD_OUTPUT_HANDLE) == NULL` first - only reattach when
  the process genuinely has no stdout handle at all, leaving explicit
  redirection alone.
- **`-ms` batch-screenshot scripts needing real per-game tuning** - a
  generic "tap Fire a few times, wait" script left several games showing
  something other than real gameplay when captured, each for a different
  reason found by reading that specific game's own source rather than
  guessing:
  - **UFO** dies within ~1-2 seconds unless up/down is genuinely held the
    whole time (matching a real player) - fixed by holding Up for a
    short window after the single confirming tap.
  - **Dino Game** needs precisely-timed jumps to survive at all; even
    holding Up continuously (auto-jump whenever grounded) still dies
    around frame ~70-90 - fixed by capturing shortly after the run
    begins instead of trying to survive.
  - **Four in a Row**'s AI opponent uses randomized Monte Carlo rollouts,
    so a longer script risked completing (and resetting, via
    `firoNewGame()`) a whole match purely by chance before the capture -
    fixed with a single player move plus a bounded wait for the AI's
    reply, confirmed to reliably show a mid-game board.
  - **Tiny Pipe**'s title→gameplay transition is a long sequential state
    chain (fade in → title → fade out → level-load x2 → level fade-in →
    playing) - the default script's budget just needed a longer final
    wait to fully resolve, not different input.
  - **Tiny Minez** needs *exactly* 2 taps (intro/rules → difficulty-
    select → confirm) and genuinely no more: a 3rd tap while already
    `PLAYING` isn't a harmless no-op, it's Fire-release, which uncovers
    whatever cell the (never-moved) cursor sits on - if that's a mine, it
    triggers a boom → game-over → title-reset cycle, exactly matching a
    real user report of an unexpected "logo" screenshot. Fixed by capping
    the script at exactly 2 taps for this one game.
  - Two apparent failures (**Tiny Bomber**'s "TINY BOMBER" banner, **Tiny
    Dungeon**'s logo+heart-icon overlay) turned out to be **false
    alarms** on inspection - both are genuine, permanent HUD/decorative
    art baked into the real gameplay background, not evidence of being
    stuck on a title screen; confirmed via a live frame-state trace
    (`bomState`/`bomInGame`/`bomFrame` printed every frame) showing the
    game had, in fact, already reached `PLAYING` and was rendering
    correctly the whole time.
- **`SDL_RenderReadPixels()`-after-`Present()` intermittently returns a
  blank/stale buffer** - a debug-only capture hook (verifying the glow/
  CRT/pixel-grid cycle) called `SDL_RenderReadPixels()` from *outside*
  the render loop, after several frames' worth of `SDL_RenderPresent()`
  calls had already happened - worked for the very first state captured,
  then returned a fully blank image for every subsequent one, even though
  adding error logging around every render call showed zero errors and
  confirmed the actual drawing was succeeding every frame. Root cause:
  `SDL_RenderPresent()` can swap to a different physical backbuffer
  whose previous contents aren't guaranteed to survive being read back
  afterward - some backends/swap-chain configurations preserve it (which
  is why the very first capture happened to work), others don't. Fixed
  by moving the capture to *inside* `md_endFrame()`, arming a "capture on
  the next call" flag that reads pixels immediately before that same
  frame's own `SDL_RenderPresent()`, never after it.
- **The CPU-side glow-effect ~40x slowdown** - see "Presentation
  effects" above and `OPTIMIZATIONS.md` for the full writeup; the single
  biggest performance finding in this project so far, and specific to
  this platform's own cost model (`SDL_Surface` blits are always
  software-bound) rather than anything resembling the Vircon32 build's
  own per-pixel-recompute lessons.
- **Pure-sine oscillator made several games sound wrong or silent** -
  `sdlAudioCallback()`'s single-voice oscillator originally generated a
  pure sine (`sinf(gTonePhase)`). Both reference implementations this
  port is meant to sound like are harmonically rich, not pure tones: the
  original ATtiny hardware bit-bangs a real square wave (documented in
  `tinyJoypadShim.c`'s own `Sound()` comment), and the sibling Vircon32
  build's PlayNote library, while technically wavetable-shape-agnostic,
  ships and uses a **saw** wavetable (`sounds/wt_saw.wav`) specifically
  because a sine has none of a buzzer's buzz. A sine isn't just a timbre
  mismatch, it's frequently inaudible where those aren't: several games
  (e.g. `gameTinyPacman.c`'s siren/pellet cues) call `md_playTone()` with
  very low nominal frequencies (2-20Hz) for very short durations - a sine
  barely completes a fraction of one cycle in that time (near-zero
  amplitude throughout, genuinely silent), whereas a square wave's hard
  0-to-±amplitude edge at tone-start/stop is itself an audible click
  regardless of the nominal frequency, same as a real piezo being
  switched on for a moment. Found from a direct user report ("no sound in
  Pacman/Frogger, other games sound off") that named the sine/square
  mismatch as the likely cause. Fixed by switching the oscillator to a
  square wave (`gTonePhase < PI ? +amplitude : -amplitude`) - the
  simpler of the two harmonically-rich options, and the one that matches
  the *original* hardware exactly rather than Vircon32's own approximation
  of it.
- **Same-frame tone-cancellation off-by-one (the actual "Frogger has NO
  sound at all" root cause - the square-wave fix above wasn't enough by
  itself)** - `main.c`'s own loop calls `gamesMain_dispatchFrame()` (where
  a game's own `Sound()`/`md_playTone()` call actually happens) *before*
  `md_updateAudio()`, every real frame. `md_playTone()` was computing
  `gToneStopFrame = gFrameCounter + durationFrames` with a 1-frame
  minimum for any duration that rounds down to less than one frame at
  60fps - the single most common case, since most games' own short SFX
  durations are only a handful of milliseconds. For that 1-frame-minimum
  case, `md_updateAudio()` (running moments later, same real frame)
  immediately increments `gFrameCounter` to exactly `gToneStopFrame` and
  expires the tone right then - cancelling it before the audio callback
  thread has had virtually any real chance to render it, not after an
  actual frame of playback. Confirmed by tracing the exact call order and
  duration math for the reported games: **every one** of Frogger's own
  note durations (`frgJumpNotes`/`frgDeath*Notes`/etc, all AVR-cycle-count
  pairs at high nominal frequencies) round-trips to a sub-frame real
  duration and hits this exact case, explaining total silence rather than
  a partial/wrong sound; HollowSeeker's move-blip (`660.0, 0.02` = 1.2
  frames, also 1-frame-minimum) hits it too; Pacman's own calls span a
  much wider duration range, so only its shorter calls were silently
  dropped while longer ones (0.2-0.5s) survived, giving a garbled/wrong-
  sounding *subset* rather than clean silence - matching the original "no
  sound (Pacman, Frogger) / sound way off (other games)" report exactly.
  Notably, `obonoCoreShim`'s own scored-music playback
  (`obonoCoreShimAdvanceScore()`, called from `obonoCoreShimUpdateSound()`,
  which already runs *after* `md_updateAudio()` in `main.c`) was never
  affected - only sound triggered from a game's own per-frame `update()`
  (dispatched *before* `md_updateAudio()`) was. Fixed with a 1-frame
  margin (`gToneStopFrame = gFrameCounter + durationFrames + 1`) in
  `md_playTone()` itself, guaranteeing at least one full
  `md_updateAudio()` tick always elapses after the tone starts regardless
  of which of the two same-frame calls actually started it - a self-
  contained fix that doesn't depend on `main.c`'s own call order staying
  what it is today.
- **Presentation effects (glow/CRT/pixel-grid) rendered on top of the
  quit-confirmation dialog** - a `gameworld/gamesMain.c` bug, not SDL3-
  specific despite this section's own header, since `gamesMain.c` is
  shared unmodified by `src/sdl2/` too (confirmed present on both once
  looked for). `gamesMain_dispatchFrame()`'s own `md_setInGame(
  currentGameIndex != -1 )` call - the signal the SDL ports' own
  `md_endFrame()` gates all three presentation effects on - ran at the
  *top* of the function, before checking `confirmingQuit`. Since
  `currentGameIndex` stays non -1 for the entire time the dialog is
  frozen on screen (it only clears if the player actually selects YES),
  `gInGame` stayed `true` the whole time the dialog was up, so whatever
  effect combination was active during that game's own play kept
  compositing on top of the dialog box instead of turning off for it -
  visually wrong regardless of which effect, and a real correctness bug,
  not just a preference (the effects are meant to be a "how does actual
  gameplay look" toggle, not a whole-app-except-the-menu one). A first fix
  gated `md_setInGame()` itself on `!confirmingQuit` too - correct for the
  dialog box, but it went too far: it also turned the effects off across
  the *entire* frozen game screen still visible behind/around the dialog,
  which is supposed to keep looking exactly like it did the instant
  before the dialog opened (confirmed by direct user report after seeing
  the first fix in action). Replaced with a more surgical second signal
  instead: `md_setDialogShowing( confirmingQuit )` (new, called alongside
  `md_setInGame()`, both moved to the *end* of `gamesMain_dispatchFrame()`
  so they reflect this frame's fully-updated state, including the dialog
  opening/closing on this exact frame) tells the SDL backends' own
  `md_endFrame()` to re-composite just `MD_DIALOG_X`/`_Y`/`_W`/`_H` (the
  dialog's own rect, now a shared `machineDependent.h` constant instead of
  a value local to `drawConfirmQuitDialog()`, so the two sides can't drift
  out of sync) crisply on top of the effects - `gInGame` itself goes back
  to a plain `currentGameIndex != -1`, so the rest of the frozen screen
  keeps whatever effect combination was already active.
- **`avrCompat.h`'s `uint8_t`/etc typedefs conflicted with Apple's own SDK
  headers, macOS-only** - found via a real macOS CI run (`build.yml`'s
  `macos-14`/`15`/`26` matrix entries), not locally (this project's own
  dev machine is Windows/MinGW, which never hit this). `avrCompat.h`
  deliberately typedefs `uint8_t`/`int8_t`/`uint16_t`/`int16_t`/`uint32_t`
  to plain `int` (see that file's own comment - load-bearing, not a
  simplification to revert) *after* including `<stdlib.h>`/`<string.h>`/
  `<math.h>`. On MinGW/glibc that ordering never mattered, because none of
  those three headers transitively define those type names at all there.
  Apple's SDK does: each has its own individual leaf header
  (`_types/_uint8_t.h`, `sys/_types/_int8_t.h`, `_types/_uint16_t.h`,
  `sys/_types/_int16_t.h`, `_types/_uint32_t.h` - real 1-/2-byte-width
  typedefs, e.g. `typedef unsigned char uint8_t;`) that those three
  headers pull in transitively, and C has no way to "undo" an earlier
  typedef - so `avrCompat.h`'s own, later, differently-typed redefinition
  is a hard compile error (`gameDinoGame.c` was simply the first `.c` file
  Ninja happened to compile, not anything game-specific - every "game
  world" file hits it identically).
  A first fix attempt pre-defined each leaf header's own include guard
  (`_UINT8_T`/`_INT8_T`/`_UINT16_T`/`_INT16_T`/`_UINT32_T`) as an
  `if(APPLE)`-scoped `target_compile_definitions()` in both SDL ports' own
  `CMakeLists.txt`, reasoning that a suppressed leaf header just makes the
  redefinition a no-op. That broke *worse*, on the very next CI run: those
  same real types aren't only used by `avrCompat.h`'s own redefinition -
  Apple's own deeper system headers genuinely need them for their own
  internal declarations too (`sys/resource.h`'s `uint8_t ri_uuid[16];`;
  `<stdint.h>`'s own `typedef uint32_t uint_least32_t;`), and suppressing
  the leaf header starves those of the real type as well, turning one
  clean "redefinition" error into a cascade of "unknown type name"
  errors instead. Reverted.
  Fixed for real by changing `avrCompat.h`'s own six lines from `typedef`
  to `#define` (`#define uint8_t int`, etc) - the same technique already
  used for `itoa` a little further down the same file, and for the same
  reason: a macro doesn't redeclare the real `uint8_t` typedef at all, it
  just rewrites the raw *token* `uint8_t` to `int` for every line of
  source from that point forward in the translation unit. Ordering is
  what makes this safe: `<stdlib.h>`/`<string.h>`/`<math.h>` are still
  included *first*, so Apple's own deep system headers fully resolve
  using the real narrow types before the `#define`s ever take effect: only
  code coming after them in the same translation unit (the rest of
  `avrCompat.h`, then `machineDependent.h`, the shim headers, and the
  actual game source) sees `uint8_t`/etc rewritten to `int`. Confirmed
  nothing later in a "game world" translation unit needs the real names
  back (`machineDependent.h` only adds `<stdbool.h>`/`<stddef.h>`, neither
  of which touches these types). No `CMakeLists.txt` changes needed for
  this version - the fix is entirely inside the already-platform-oblivious
  `avrCompat.h`, and is a no-op change in effect on MinGW/glibc (same
  `int` meaning either way, just via a different preprocessor mechanism).

## CLI parameters

Modeled on `cglpSDL3.c`'s own flag set (see its `printHelp()`), adapted:
`-w`/`-h` (window size), `-f` (fullscreen), `-ns` (no audio), `-fps` (live
FPS overlay, BIOS-font-rendered top-left corner), `-nd` (uncapped
framerate - toggles vsync off, rather than cglp's own manual
`SDL_Delay`-based pacing, since vsync already provides correct pacing
when enabled and this project had no reason to duplicate that logic),
`-s` (force software rendering - added later, on direct user request; see
below), `-list`, `-g <NAME>` (direct launch, case-insensitive), `-ms`
(batch screenshot every game, plus one `./menu.bmp` of the menu's own
default first-page/first-selection state, captured first before any
game's own launch overwrites it - also the mechanism thumbnails are
generated from, and the source of `metadata/menu.png` at the top of
`README.md`, matching the sibling project's own `effdb2f`), `-joy`
(write a `.joy` title-stub file per game,
mirroring cglp's own `.cgl` files under this project's own extension),
and a `.joy` file itself as a positional argument (extracts the game
title from the filename, matching cglp's own filename-only convention -
never reads the file's actual content). Deliberately **not** carried
over: `-nsd` (cglp's own "no scaled drawing" toggle ties into its
glow/CRT pipeline in a way this project's own effect design has no
equivalent knob for). cglp's own `-a` (force hardware-accelerated) also
has no equivalent, but for the opposite reason from `-s`'s own existence:
hardware-accelerated is already this backend's own default (`SDL_CreateRenderer(
window, NULL)` on SDL3 auto-picks its best-available driver;
`SDL_RENDERER_ACCELERATED` is requested explicitly on SDL2), so there's
nothing left for `-a` to force.

`-s` requests SDL's own built-in CPU rasterizer instead of that default -
`SDL_SOFTWARE_RENDERER` (`SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER)`,
SDL3's own defined driver-name string, literally `"software"`) on SDL3;
`SDL_RENDERER_SOFTWARE` swapped in for `SDL_RENDERER_ACCELERATED` in the
renderer-creation flags (SDL2 has no NULL/"best available" shorthand the
way SDL3 does) on SDL2. Both routed through a new `sdlBackend_setSoftwareRendering()`
setter (matching the existing `setWindowSize()`/`setFullscreen()`/
`setVsync()` pattern - CLI parsing/ownership stays in `main.c`, `sdlBackend.c`
only exposes setters called before `sdlBackend_init()`). Confirmed via the
existing renderer-name startup log line (`sdlBackend initialized:
renderer=...`) - reports `software` with `-s` on both ports, `direct3d11`/
`direct3d` (unchanged) without it.

### Start quits instead of returning to the menu, when launched via -g/.joy

Cross-checked directly against `cglpSDL3.c`'s own Back-button handling
(`if (!isInMenu && (startgame[0] == 0)) goToMenu(); else quit = 1;`,
`startgame` being the parsed `-g`/`.cgl`-file title, never cleared again
once a launch succeeds) on direct user request - confirmed cglp has no
quit-confirmation dialog at all, and quits immediately in this case rather
than offering any way back to a menu that was never shown in the first
place. This project's own dialog exists specifically because there IS
normally a menu to protect an accidental Start-press from leaving
(matching the sibling Vircon32 build's own dialog) - but when a game was
reached via `-g <NAME>` or a `.joy` file argument instead of the menu,
there's no menu state to protect; skipping straight to quit (no dialog)
on the first Start press is the correct equivalent here, not "show the
dialog, then quit instead of returning to the menu" (which would still
interrupt gameplay with an extra confirmation step cglp's own reference
behavior never has either).

Implemented as a new `gLaunchedDirectly` flag in `gamesMain.c`, set once
via `gamesMain_setLaunchedDirectly()` (`gamesMain.h`) - called from each
SDL port's own `main.c`, right after the real `gamesMain_launchGameDirect()`
call in the `-g`/`.joy` branch specifically, NOT the other call site
`-ms`'s `runBatchScreenshots()` uses (that path never reaches
`gamesMain_dispatchFrame()`'s own interactive loop at all, so the flag
would be meaningless there). `gamesMain_dispatchFrame()`'s own Start-
button branch checks it before falling through to the normal
`confirmingQuit` path; never cleared back to `false` afterward, matching
`startgame`'s own permanence in cglp - quitting is the only way out of
this mode by design, so there's no scenario needing to un-set it.

Quitting itself needed one genuinely new cross-cutting piece:
`machineDependent.h` had no "game world asks the platform to exit"
concept at all before this (Vircon32 never needed one - no real OS
process to quit). Added `md_requestQuit()`, implemented on both SDL ports
by setting the same `gQuit` flag `sdlBackend_pollEvents()` already sets on
a real window-close/`ButQuit` event - `sdlBackend_shouldQuit()` can't tell
the two causes apart and doesn't need to. The Playdate port needed a
real (no-op) stub too, same reasoning as `md_setFpsOverlayShowing()`
above: it compiles shared `gamesMain.c` regardless of whether its own
`main.c` ever calls into this particular path (no CLI/`-g`/`.joy` there
at all, so it never will) - real Playdate hardware has no "quit the app"
concept to begin with, so the stub is a genuine no-op, not a stand-in for
some equivalent that port is missing.

## `md_playTone()` became genuinely multi-voice in `sdl3`/`sdl2` (matching the same fix in the sibling Vircon32 build)

Ported directly from the Vircon32 sibling project's own fix, made in
response to a direct request there ("make pacman eating pills audible
while the alarm sounds goes off... really don't gate it to single
channel calls") - `md_playTone()` in both `src/sdl3/sdlBackend.c` and
`src/sdl2/sdlBackend.c` used a single shared `gToneActive`/`gToneFreq`/
`gTonePhase` scalar, so any two genuinely concurrent cues (Tiny Pacman's
continuously-retriggered power-pellet siren vs. its own dot-eaten/
ghost-eaten SFX, or any other game's own overlapping sounds) always cut
each other off instead of both being audible - a property of TinyJoypad's
*original* single-piezo-buzzer hardware that `md_playTone()` was
enforcing unnecessarily, not something every game's own call site
actually needs.

Unlike Vircon32 (whose fix turned out to be almost free - PlayNote's own
`playnote_start()` already picks a free hardware channel internally, so
the fix there was just to stop overriding that with a manually-tracked
single voice), this backend's own audio model has no channel abstraction
at all - one plain software square-wave oscillator computed sample-by-
sample in `sdlAudioCallback()`. Fixed by turning the single
`gToneActive`/`gToneFreq`/`gTonePhase`/`gToneStopFrame` scalars into
16-element arrays (`AUDIO_MAX_VOICES`, matching Vircon32's own PlayNote
voice count for parity, though nothing here is hardware-limited to that
number) - `md_playTone()` finds the first free slot (or steals slot 0 if
all 16 are somehow busy, rather than silently dropping the note),
`md_updateAudio()` expires each slot independently, and the audio
callback sums every active voice's own square wave per sample instead of
reading one. `AUDIO_AMPLITUDE` (10000, well under `Sint16`'s ±32767
range) already left enough headroom for several simultaneous voices to
sum without clipping in the common case (this cartridge rarely has more
than 2 genuinely concurrent cues) - a final hard clamp handles the rare
case where more do coincide, the same "accept some clipping on overlap"
tradeoff already made for this being a harmonically-buzzy square wave in
the first place, rather than building dynamic per-sample rescaling for
an edge case none of the 33 games actually hits.

A `freq<=0` "rest" call is now a genuine no-op (matches the Vircon32
fix's own reasoning) - it doesn't grab a voice or stop anything, so
whatever's independently playing on other voices is unaffected; the
previous single-voice version used a rest to deliberately silence
whatever was currently playing, which no longer makes sense once "what's
currently playing" can mean several independent things at once.

Verified with a full clean rebuild of both `src/sdl3/` and `src/sdl2/`
(`cmake -B build -G Ninja -DUSE_VENDORED_SDL=1 && cmake --build build`
from each port's own directory - both linked successfully, no new
warnings) - not verified by ear this session (no audio-capable run
performed), so worth a real play-test to confirm Tiny Pacman's siren and
eating-SFX are actually both audible together as intended. The Playdate
port (`src/playdate/main.c`) was not touched - it uses a single
`PDSynth*`, "one synth, not a pool" by its own existing comment, a
different fix shape again (building a synth pool, which the SDK supports
and a sibling project's own `cglpPlaydate.c` already demonstrates) - out
of scope unless asked.

**Note on repo location**: this fix was first mistakenly applied to
`C:\github\tinyjoypad_SDL3` (lowercase, "3" suffix) - a different,
apparently-stale directory with an identical-looking codebase, not this
project. Caught and corrected directly by the user. That other
directory still has the same edits applied (harmless, but not the real
project) - worth clarifying with the user whether it should be cleaned
up or left alone, since its actual status/purpose wasn't established
this session.

## Tiny Missile: `ATTACK_WEAPON()`'s burst-fire bug traced and fixed (ported from the sibling Vircon32 build)

A direct question on the Vircon32 sibling project ("verify what happened
related to available bullets/rockets in tiny missile when main base got
hit") led to re-tracing `ARMY_TMISSILE::ATTACK_WEAPON()`
(`CLASS_TMISSILE.cpp:44-54`, triggered from `DOME_COLLISION()` when an
incoming missile's `END_X` lands in the crosshair/main-base zone
`(54,73)`) character-by-character against the real upstream source -
this port's own existing code comment (and the fix it justified,
inherited from the same earlier session as the Vircon32 build) turned
out to have the upstream behavior backwards.

Upstream: `if(ROCKET>0){while(1){if(ROCKET>0){USE_WEAPON();...}else{goto
Exit_;}}}else{if(SPARE>0){SPARE--;SNDBOX(3);}}`. The `while(1)` loop's own
`ROCKET>0` check runs *before* every `USE_WEAPON()` call, so
`USE_WEAPON()` is only ever invoked while `ROCKET` is already nonzero -
its own internal `SPARE`-refill branch (taken only when `ROCKET` is
*already* 0 at the moment it's called) is unreachable from this specific
loop. The burst just drains whatever's left in the *current* clip (up to
10 shots) and stops - it never reaches into `SPARE` when the clip had
rounds in it. Only if the clip was already empty at the moment of the
hit does upstream take a single defensive shot straight from `SPARE`, no
refill loop at all.

This port's `tmisAttackWeaponStep()` (`gameTinyMissile.c`) instead called
`tmisArmyUseWeapon()` unconditionally every tick once a burst with a
nonzero starting clip began, with no per-tick `tmisArmyRocket > 0` gate
of its own - so once the clip actually drained to 0 mid-burst,
`tmisArmyUseWeapon()`'s own refill branch fired and the burst kept going,
draining the *entire* arsenal (current clip + every spare clip) on a
single hit, rather than just the current clip. **Fixed** by adding back
the per-tick `tmisArmyRocket > 0` check before calling
`tmisArmyUseWeapon()` inside the burst-active branch, matching upstream's
own loop gating exactly - the burst now correctly stops the instant the
starting clip is exhausted, never touching `SPARE` in that path.

Ported directly from the identical fix already verified in the sibling
Vircon32 build (same shared-origin bug, `tmisAttackWeaponStep()` is
essentially byte-identical between the two projects). Verified with a
clean rebuild of both `src/sdl3/` and `src/sdl2/` (both link successfully
with no new warnings) - this fix lives in the shared `src/gameworld/`
code, so `src/playdate/` picks it up automatically too, though that port
wasn't independently rebuilt this session. Not verified by actual
play-test on this project (no audio/graphics-capable run performed) -
the Vircon32 sibling's own version of this same fix *was* Puppeteer-
verified there (crosshair movement, firing, rocket-trail rendering, no
crash after repeated hits), which is reasonable indirect confidence given
the logic is identical, but worth a direct play-test here too if time
allows.

## Tiny Bike: same "flattened loop bound" bug found on the sibling Vircon32 build's own project-wide audit, ported here too

The Vircon32 sibling project ran a 4-agent, 32-game audit for the same
bug class as the Tiny Missile `ATTACK_WEAPON()` fix above (an upstream
`while`/`goto`-shaped loop whose own condition is genuinely re-checked
every iteration, ported into a form that drops that per-iteration
recheck). 31 of 32 games came back clean; Tiny Bike had a real instance.

Upstream's own per-tick movement loop:
```c
for (t=0; t<CHECK_SPEED_ADJ(ACCEL); t++){
  INCREMENTE_SCROLL(); if (DIV1==3) {...TRACK_RUN_ADJ();...} else{DIV1++;}
}
```
A plain C `for` loop re-evaluates its bound *every iteration* -
`CHECK_SPEED_ADJ(ACCEL)` (which also has a side effect, `Higher_adj(ret)`,
updating the jump-arc physics constant) is called fresh each pass against
whatever `ACCEL` currently is. `ACCEL` is not read-only inside the loop
body: `INCREMENTE_SCROLL()` -> `RefreshPosSprite()` -> `CheckCollision()`
-> `analise_minutieuse()` can reduce it mid-loop (an oil-slick hit,
`ACCEL-=0.20`), and `Break_Gravity()` (a hard ramp landing) can too - so a
same-tick collision correctly shortens the *remaining* iterations
upstream, and re-derives the jump-height constant against the new,
slower speed.

This port's `gameworld/games/gameTinyBike.c` had instead hoisted this to
`int speedTicks = bikCheckSpeedAdj( bikAccel ); for(t=0;t<speedTicks;t++)`
- computed once, before the loop, even though the same reachable mid-loop
`bikAccel` reduction paths exist here too. Effect: after a same-tick
oil-slick hit or hard landing, the port kept running for the *original*
(higher, pre-collision) iteration count instead of correctly shortening
it, and left `bikHigherJump` stale instead of refreshed against the new
speed. **Fixed** by moving `bikCheckSpeedAdj( bikAccel )` directly into
the loop condition, matching upstream's genuine per-iteration
re-evaluation.

Ported directly from the identical fix in the sibling Vircon32 build
(same shared-origin bug - `gameTinyBike.c`'s own movement loop is
essentially byte-identical between the two projects). Verified with a
clean rebuild of both `src/sdl3/` and `src/sdl2/` (both link successfully,
no new warnings) - this fix lives in the shared `src/gameworld/` code, so
`src/playdate/` picks it up automatically too, though that port wasn't
independently rebuilt this session. Not verified by actual play-test on
this project - the Vircon32 sibling's own version was Puppeteer-verified
(launched, played - acceleration, wheelie tilt, track scrolling, HUD -
with no crash), but didn't specifically force an oil-slick-hit-mid-loop
frame to visually confirm the corrected iteration-count behavior itself,
so worth a direct play-test focused on that exact scenario here too if
time allows.

## Tiny Bike: no motor sound exists upstream (confirmed, not a bug); a "pedaling" animation gap found and fixed on the sibling Vircon32 build, ported here too

A direct question on the Vircon32 sibling project ("is it possible in
tiny bike the motorsound and the player on the moving bike animation is
not playing as intended") had two different answers there, both
applicable here since this port shares the same `gameworld` game logic.

**Motor/engine sound**: confirmed upstream has none at all (checked
every real `Sound()` call site in `Tiny-Bike.ino` - only one-shot event
cues: intro jingle, race-start confirm, a collision cue, bonus-life -
nothing tied to acceleration/movement itself). Not a gap in either port.

**Player animation**: a real gap. Upstream's `t` (`uint8_t t=0;`,
declared once right before the race begins) is checked at the top of
every real tick (`if(TRIG_OK==0 && Wheel_up==1 && t>0) {animBike =
(animBike==1)?6:1;}`, the "pedaling" animation toggle) *before* that same
tick's own `for(t=0;t<CHECK_SPEED_ADJ(ACCEL);t++)` speed-loop overwrites
it - so `t>0` really means "did the *previous* tick's speed-loop run at
least once," deliberately suppressing the pedaling animation while
nearly stationary (freshly spawned, right after a crash, or before
accelerating). This port's own `t` is a plain per-call local (the same
variable touched by the `speedTicks` fix above), which can't carry a
value across real ticks - and the port's own pedaling-toggle condition
was simply missing the third condition entirely, so it animated pedaling
unconditionally whenever not mid-wheelie-adjustment, including while
stationary.

**Fixed** with a new persistent global, `bikPrevSpeedTicks` (reset to 0
in `bikBeginPlaying()`, matching where upstream's `t=0` sits relative to
the race starting; captured as `bikPrevSpeedTicks = t;` right after the
speed-loop finishes each tick) - the pedaling-toggle condition now reads
`bikTrigOk==0 && bikWheelUp==1 && bikPrevSpeedTicks>0`, matching
upstream's real three-condition gate.

Ported directly from the identical fix in the sibling Vircon32 build,
same session, same shared file. Verified with a clean rebuild of both
`src/sdl3/` and `src/sdl2/` (both link successfully, no new warnings) -
this fix lives in `src/gameworld/`, so `src/playdate/` picks it up
automatically too, though not independently rebuilt this session. Not
verified by actual play-test on this project - the Vircon32 sibling's
own version was Puppeteer-verified (launched, idle, then accelerated, no
crash), but a still screenshot can't prove an *animation* behaves
correctly (holds still vs. cycles) the way it can prove a crash, so worth
a direct play-test here too if time allows.

## Tiny Bike locked to 30fps (whole-tick), confirmed by direct user play-test on the sibling Vircon32 build

Investigating the pedaling-animation gap above (on the Vircon32 sibling
project) led to a static cycle-count of the real I2C bit-bang driver -
inconclusive (suggested upstream might run faster than 60fps, not
slower, which didn't clearly explain a "too fast" perception). The user
asked directly to try locking the game to 30fps as a test ("it seems the
game may be running too fast judging from arduboy port"), tried it
there, played it, and confirmed it as the right fix.

Implemented as a whole-function tick-skip (`BIK_TICK_DIVISOR = 60/30 =
2`), the same shape as Tiny Pipe's own "limit to 30fps including its
logic" fix - gates input reads, physics, animation, and redraw together.
Every existing wait-frame constant in the file is deliberately left
unrescaled (this project's own standing "one divisor, no dual
bookkeeping" practice) - they simply now take twice as long in real
time.

Ported directly from the confirmed Vircon32 fix, same session, same
shared `gameworld/games/gameTinyBike.c` file. Verified with a clean
rebuild of both `src/sdl3/` and `src/sdl2/` (both link successfully, no
new warnings) - this fix lives in `src/gameworld/`, so `src/playdate/`
picks it up automatically too, though not independently rebuilt this
session. Not verified by actual play-test on this project specifically -
the Vircon32 sibling's own version *was* directly play-tested and
confirmed by the user, which is reasonable confidence given the logic is
byte-identical, but worth a direct play-test here too if time allows.

## Game logic decoupled from the display's own real refresh rate (100Hz/144Hz monitor speed-up bug)

Direct user question: "what would happen if people had 100 hz refresh
rate monitors and enable vsync?" With vsync on, `SDL_RenderPresent()`
(called once per real loop iteration) paces the whole loop to the
display's own actual refresh rate - true 60Hz only by accident on an
old fixed-60Hz display. Every game's own timing (movement/physics/
animation tick-divisors, and `md_updateAudio()`'s own `gFrameCounter`-
based tone-duration/note-sequencer scheduling) assumes exactly
`MD_FRAMES_PER_SECOND==60` `gamesMain_dispatchFrame()` calls happen per
real second - a 100Hz/144Hz monitor would call it 100/144 times a
second instead, running every game proportionally too fast. The exact
same class of bug (display refresh rate leaking into game-logic rate)
was already found and fixed for the Playdate port's own hardware-capped
50fps refresh rate (see "The Playdate port" above, its own
`md_updateAudio()` fractional accumulator).

Fixed in both `src/sdl3/main.c` and `src/sdl2/main.c` with a real
fixed-timestep game loop: real elapsed wall-clock time
(`SDL_GetPerformanceCounter()`, immune to the display's own refresh
rate) accumulates into `logicAccumulator`, and `gamesMain_dispatchFrame()`
+ `md_updateAudio()` + `obonoCoreShimUpdateSound()` only run once a full
`1/60`s has accumulated - possibly more than once per real loop
iteration on a *slower*-than-60Hz display (each extra tick's own
drawing is harmlessly overwritten by the next before anything is
presented, the standard "catch up, then show only the latest state"
shape). `elapsedSeconds` is clamped to 5 ticks' worth before
accumulating, to avoid a spiral-of-death catch-up storm after a real
stall (window drag, alt-tab, a debugger breakpoint). See `main.c`'s own
inline comment at the loop itself for the full reasoning - not
duplicated here.

## Real persistent high-score saving, `eepromShim` ported from the sibling Vircon32 build

Direct user request, after checking the sibling `tinyjoypad_vircon32`
project's own `eepromShim` (see that project's own CLAUDE.md "Real
persistent high-score saving, restored via a fake eeprom.h shim backed
by Vircon32's memory card" section): port the same fake-`<avr/eeprom.h>`
shim to this project, but back it with a real dotfile
(`.tinyjoypad_highscores`) in the user's home directory instead of
Vircon32's own memory-card hardware, working on Windows/Mac/Linux -
checked `crisp-game-lib-portable-sdl`'s own SDL port for home-directory
detection precedent (`SDL_getenv("HOME")` only, no Windows fallback).

New shared files `src/gameworld/eepromShim.h`/`.c` reproduce the same
public API and on-disk slot-format/hashing scheme as the sibling's own
shim (`eeprom_read_byte`/`_write_byte`/`_update_byte`/`_read_word`/
`_write_word`/`_read_dword`/`_write_dword`/`_read_block`/`_write_block`/
`_busy_wait`), with `title` a real `char*` instead of Vircon32's own
`int[]`-string dialect, and `eepromSelectGame()` called automatically by
`gamesMain.c` right before a newly-chosen game's `init()` runs (not
something any individual game calls itself). Backed via new
`md_card*()` primitives in `machineDependent.h`
(`md_cardIsConnected`/`md_cardHasOurSignature`/`md_cardWriteSignature`/
`md_cardReadData`/`md_cardWriteData`), implemented per-port:
- **SDL3/SDL2** (`sdlBackend.c`, both ports) - a real file at
  `$HOME/.tinyjoypad_highscores`, with a genuine improvement over the
  `crisp-game-lib-portable-sdl` reference: `USERPROFILE` is checked as a
  fallback when `HOME` is unset (common on Windows - a plain terminal
  session or an Explorer-double-clicked `.exe` usually has no `HOME` set
  at all, but Windows itself always sets `USERPROFILE`), falling back to
  `.` (cwd) only if neither exists.
- **Playdate** (`src/playdate/main.c`) - `pd->file`, writing to
  `highscores.dat` in the game's own sandboxed Data folder. Playdate's
  file API has no true random-access read-modify-write, so this reads
  the whole file into memory, patches the relevant slot, and writes the
  whole file back - added at direct user follow-up request ("also add to
  the todo to add an implementation for playdate by saving a file to the
  data folder of the port"), not left as a stub the way some other
  Playdate-irrelevant SDL-only features are.

`EEPROM_TAG_CHARS` (the per-slot title-tag buffer size) is 32, bumped
from an initial 24 once a real title needed the headroom (see the 3-game
batch below).

17 already-ported games had their own upstream EEPROM save/load restored
(Pipe Bird, Frogger, Tiny Tris, UFO, Stacker, Wren Rollercoaster, Astro
Barrier, ATtiny Snake, Blocks Gold, ATtiny Tetromino, Tiny Invaders,
Space Attack, Falling Blocks, Breakout, Oroboros, Run Dude Run) plus one
deliberate extension beyond upstream (**Tiny Bert** - a new dword-based
5-digit score, upstream never had persistence at all, matching this
project's own established "deliberate extension, not restoration"
precedent, later followed for Helicopter/Nohzdyve/Car Race too - see
below). `README.md` gained a "Save"/"High Score Saved" column with ✅
markers for every EEPROM-enabled game.

**Note on repo location**: this fix was first mistakenly applied to
`C:\github\tinyjoypad_SDL3` (lowercase, "3" suffix) - a different,
apparently-stale directory with an identical-looking codebase, not this
project. Caught and corrected directly by the user; that other directory
still has the same edits applied (harmless, but not the real project).

## Playdate system-menu crash fixed: `getMenuItemValue` called with a NULL menu item

Direct user bug report: "in the playdate version there is a bug with the
system menu's if i am inside a game call the system menu from playdate
then check pixel grid, then uncheck pixel grid and then choose return to
menu i get this error 'pd_getMenuItemValue: non-NULL value required for
argument 'menuItem''". Root-caused against the official Playdate SDK
docs: `addCheckmarkMenuItem()`'s own callback is deferred until the
system menu closes if the item was interacted with while the menu was
still open ("If this menu item is interacted with while the system menu
is open, callback will be called when the menu is closed" - the SDK's
own doc comment). The reported sequence (open system menu -> toggle
Pixel Grid off -> immediately choose "Menu" to return, all before
closing the system menu) fires `returnToMenu()` - which tears down every
system menu item via `pd->system->removeAllMenuItems()`, setting
`gPixelGridMenuItem` back to `NULL` - *before* the deferred pixel-grid
checkbox callback (`pixelGridMenuCallback()`) actually runs, so it reads
back a value from a menu item that no longer exists.

Fixed with a defensive guard at the top of `pixelGridMenuCallback()`
(`src/playdate/main.c`): `if( gPixelGridMenuItem == NULL ) return;`
before the `pd->system->getMenuItemValue()` call. Verified with a clean
rebuild (`cmake --build build`) - needed killing a locked
`PlaydateSimulator.exe` process first (its own open handle on the
built `pdex.dll` blocked the copy step); confirmed via `AskUserQuestion`
before doing so, since killing a running process the user might still
be using is exactly the kind of action worth confirming rather than
doing unilaterally.

## Three more games backported: Nohzdyve, Gilbert in the Downland, Ardumania

The sibling `tinyjoypad_vircon32` project added 3 more games in its own
history (all 3 originally Arduboy-exclusive titles, staged from Daniel
C's own ESP8285/ESP8266 "MEGA TinyJoypad" combined-cartridge port rather
than the raw Arduboy originals). Ported the same established way:
background porting agents given the sibling's own already-correct
Vircon32-dialect source plus this project's own dialect-conversion
recipe, independently re-verified afterward (including a Python
data-table diff script proving byte-exact matches against the sibling
source: 33/33, 57/57, 53/53 array elements for the 3 games
respectively) rather than trusted at face value. None of the 3 needed
`forceRedraw` (each one's own `_update()` redraws unconditionally in
every state, same reasoning already established for Tiny Fi/Tiny
Mania). `EEPROM_TAG_CHARS` bumped 24->32 (see above) specifically for
"GILBERT IN THE DOWNLAND", a 23-character title that didn't fit the old
buffer.

**A real mid-task correction**: the porting agent for Nohzdyve was
initially told "no EEPROM needed", based on a stale header comment in
the sibling source - the actual code had real `eeprom_read_word`/
`eeprom_write_word` calls (a later addition upstream, inconsistent with
its own now-stale header). Caught by direct user instruction ("do port
eeprom from noyzdive also") after the file had already been created;
fixed by patching the EEPROM calls in directly (including correcting
the stale header comment to explain the situation accurately, matching
the Tiny Bert "deliberate extension, not restoration" precedent above).

Ardumania's own default `-ms` screenshot script landed mid-way through
`AMANIA_STATE_LEVEL_TRANSITION` - a real several-second animated
sequence (the player walks off-screen, reverses, walks back trailing the
level's own ghost count, against a scrolling border and a repeating
decorative railing texture) between the menu and real gameplay, not a
bug (confirmed via a temporary debug trace of `amaniaState`, since the
captured frame - a screen-filling diagonal-hatch pattern with no
player/dots/ghosts visible - looked exactly like a broken render at
first glance). Fixed with a longer `finalWaitFrames=150` override in
`screenshotScriptFor()`, both SDL ports.

Thumbnails/screenshots/`.joy` files added for all 3 (registered after
the existing games, per this project's own standing "only ADD new asset
files, never regenerate existing ones" practice).

## Seven more games backported: Road Rush, DFlight, MRunnr, Asteroid, Helicopter, Car Race, Tiny Blocks

The sibling project's own newest batch, all from real ESP8266/ESP8285
hardware ports rather than the original ATtiny85 lineage: Road Rush,
DFlight, and MRunnr are all from Tony M's (tonym128) own `BFlight`
repo; Asteroid is from his earlier `ESP8266GameOn`; Helicopter is Finn
Harms' (innif) `Arduino-Game-System`; Car Race is hoangminh5210119's
`Esp8266OledGame`; Tiny Blocks is RobotMasterC's `TinyTetris` (renamed
to avoid the trademarked genre name in this project's own title/source
files, matching the existing Falling Blocks/Blocks Gold precedent).
Helicopter and Car Race needed EEPROM restored (word-based scores,
ported the same way as the 17-game EEPROM batch above); the other 5
have none upstream, none added.

Dispatched to 7 parallel background porting agents, each given the
sibling's own dialect-correct source plus the established recipe, then
**independently re-verified rather than trusted at face value** - this
batch is exactly why that discipline exists:

- **Three separate data-table transcription bugs**, all the same shape
  and all in different games' own 6144-value font tables
  (`heliFontData`, `flyFontData`, `tnbFontData` - Helicopter, DFlight,
  Tiny Blocks respectively): each agent's own manual transcription
  silently dropped 32-64 values (1-2 lines) despite every agent
  self-reporting a clean byte-for-byte verification. All three compiled
  without error or warning (C silently zero-fills a short array
  initializer) and would **not** have been caught without the dedicated
  Python data-table-diff script this project already relies on for
  exactly this failure mode (see the TinySQuest/Tiny Bomber precedent
  earlier in this file). Fixed identically each time: locate the exact
  line range of the real array literal in the sibling's own source,
  extract it verbatim, and splice it into the ported file in place of
  the corrupted range (never hand-retype a large data table) - then
  recompile and re-run the diff script to confirm zero mismatches.
  Road Rush and Car Race's own porting agents *also* independently hit
  and self-corrected the identical bug class in their own largest
  tables (`drvFontData`/`drvFlag`, `crCrashData`/`crFontData`) before
  ever reporting back, using the same numeric-diff-not-eyeball
  technique - all 7 games' full data-table sets were still independently
  re-diffed afterward regardless, given how often this exact bug
  recurred in this one batch.

Registered in `games.h`/`menuGameList.c` afterward (all 7 needing
`forceRedraw` wired, unlike the 3-game batch above - each has a real
attract-screen wait state).

**`-ms` screenshot-script tuning needed for 5 of the 7** (only
Helicopter and Tiny Blocks - see below re: Helicopter - looked right on
inspection; Tiny Blocks' default script genuinely worked first try):

- **Road Rush, DFlight, MRunnr, Asteroid** (all sharing tonym128's own
  scroller/level-slider chain shape) all have an unusually long,
  entirely fixed-duration title->gameplay chain (a waving-flag/level-bar
  animation, then a multi-line scrolling intro story, sometimes a
  level-slider too) that the default script's own budget reliably lands
  mid-way through - confirmed via a temporary debug trace of each game's
  own state/tick counters, not assumed. Fixed with a longer
  `finalWaitFrames` per game (350/270/290/340 respectively), keeping the
  default tap/gap shape (already proven reliable for leaving each
  game's own initial ATTRACT state).
- **Car Race** throttles its own input sampling to once every
  `CR_TICK_DIVISOR==12` real frames via `md_recentlyPressed()`, true
  only while Fire is *currently* held and has been for `<=12` frames -
  the default script's single-real-frame Fire pulse is essentially never
  still "down" at the next 1-in-12 sampled dispatch. Two off-by-one
  attempts at a coprime-gap phase-sweep fix (see `screenshotScriptFor()`'s
  own comment for the exact arithmetic mistake each time, confirmed via a
  direct debug trace of `crTickCounter`/fire state showing pulses landing
  on the *identical* phase every single time) were needed before landing
  on the real fix: `gapFrames=9` (real distance between pulses = 11,
  coprime with 12) swept every phase, but a single 12-tap sweep's own
  lucky hit still landed *during* `CR_STATE_SPLASH` (which ignores Fire
  entirely for a fixed ~120-real-frame hold) - needing a **second** full
  12-phase sweep (25 taps total) to guarantee a hit lands after splash
  ends, inside `CR_STATE_TITLE` where it actually matters.
- **Helicopter** initially looked fine (a plain default-script capture
  showed real cave terrain) but a later direct user report caught it
  showing `GAME_OVER` instead - confirmed non-deterministic run-to-run,
  not a fixed timing miss (some runs even landed on a bare ATTRACT
  screen that, without a HUD, superficially looked like real gameplay at
  a glance). Root-caused via a debug trace of `md_inputFireFrames()`:
  `gInputFireGateActive` (armed at every game launch to keep the
  menu-selecting Fire press from bleeding into the game) only disarms
  once some call samples the button already released - this game's own
  every-2nd-real-frame input sampling lines up its very *first* such
  sample with its own first scripted press frame by construction, so the
  gate never disarms and silently eats that whole first attempt, every
  time. Fixed with 6 short taps 3 frames apart (real distance 5, odd
  relative to the divisor-2 sampling, sweeping both parities within a
  couple of taps) landing inside `heliBeginPlay()`'s own fixed
  `HELI_READY_TICKS==30`-tick physics-free hold (helicopter drawn at
  rest, no gravity/collision/score) rather than trying to survive real,
  RNG-seeded physics at all - confirmed via 5 independent full runs of
  the real (non-debug) build, pixel-diffed byte-identical.

New thumbnails/screenshots/`.joy` files for all 7 (indices 53-59),
generated the same crop/resize pipeline as every prior batch (`-crop
640x320+0+20 +repage -resize 256x128`, confirmed pixel-exact against an
existing checked-in thumbnail before trusting it on the new 7).

**README.md's own "Games" section replaced verbatim** with the
sibling's own current one (direct user request: "match exactly"),
including its own 7-column format (`Game | Author | MCU | License |
Save | Source | Screenshot`, thumbnail `width="80"`) rather than this
project's own previously-different column set/width - a deliberate
divergence from this file's own general "don't duplicate the sibling's
own docs" policy, since the user explicitly wants this one section
byte-identical rather than independently maintained. One real
discrepancy found doing this: the sibling's own table links
`TINY BULLS AND COWS.png`, but its own registered in-game title (and
every other screenshot's own naming convention, filename == title) is
`BULLS AND COWS` - a pre-existing inconsistency in the sibling's own
repo (its file legitimately exists under the mismatched name, so the
link itself wasn't broken there, just inconsistent). Fixed in *both*
repos: renamed the sibling's own screenshot file to match its
convention (`git mv`, confirmed no other reference to the old name
anywhere in that repo) and updated both READMEs' own table rows to
match - rather than only patching around it in this project alone.

## Eight more games backported from upstream (and two follow-up fixes)

The sibling `tinyjoypad_vircon32` project added 7 new games in its own
history after this project's initial 33-game port was complete
(SnakeGame85, Jump Slime, TinyRoG, TinY Fi, Breakout, Space Attack,
Falling Blocks - see that project's own `CLAUDE.md` for each one's real
porting history, author research, and dialect-conversion notes, per this
file's own "history lives in the sibling project" policy above). Brought
over here the same way Phase 4's original ~30 games were: dispatched to
7 parallel background agents (one per game, given the sibling's own
already-correct Vircon32-dialect source plus this project's own
established dialect-conversion recipe above), then independently
re-verified afterward rather than trusted at face value - a single clean
sequential rebuild (the 7 parallel agents' own build attempts collided
with each other on the shared `build/` directory mid-flight, an expected
side effect of running that many concurrent `cmake`/`ninja` invocations,
not a problem with any port itself) confirmed all 7 compile clean with no
leftover Vircon32-dialect syntax. Registered centrally in `games.h`/
`menuGameList.c` afterward (each agent's own task explicitly excluded
touching those two shared files, to avoid 7-way merge conflicts).

Two of the 7 needed their own `-ms` screenshot-script tuning
(`screenshotScriptFor()` in each SDL port's own `main.c`), found the same
way every prior per-game screenshot fix in this project was found -
capturing, then actually looking at the result:

- **SnakeGame85** never reads Fire at all (see the sibling's own header
  comment - all 4 directions double as "any button" to start/restart) -
  the default Fire-tap script never left the attract screen. Fixed with
  `tapCount=0` (skips the Fire-tap loop entirely) plus `holdUp=true` -
  Up alone both starts the game and keeps the snake moving during real
  play afterward.
- **Breakout** hit `GAME OVER` before the capture, with only a single
  Fire tap - a genuine, non-obvious interaction bug, not just "needs a
  longer wait" (confirmed by raising the wait budget repeatedly with no
  change: it never left the attract screen at all, at any length).
  Breakout runs its own real game logic on a throttled ~40/60 fixed-
  timestep accumulator (`brkTickAccum`), meaning **not every real engine
  frame calls `isFirePressed()`** - roughly 1 real frame in 3 is a no-op
  for this game's own purposes. `gamesMain_launchGameDirect()`'s own
  `md_armInputFireGate()` (see machineDependent.h) only disarms the next
  time `md_inputFireFrames()` is actually *called* while released - and
  the screenshot script's own single-frame Fire-press pulse (one
  `simulateFireFrame(true)` call, immediately followed by `false`) landed
  on the *one* real engine frame that happened to be a throttled-away
  no-op for Breakout specifically, so the game's own code never saw
  `fireDown=1` at all - confirmed directly by a temporary debug print
  (`gameBreakout.c`'s own `isFirePressed()` call site, removed again
  afterward) showing `fireDown=0` for the entire capture window, every
  single time, regardless of how long the window was. Fixed by widening
  the odds instead of the wait: `tapCount=4` (back to the shared
  default, giving 4 independent Fire-press pulses instead of 1 - each a
  fresh independent chance to land on a real tick after the gate's
  already disarmed) with a short `gapFrames`/`finalWaitFrames` budget
  (so the capture still lands soon after reaching `PLAYING`, before an
  unmoved paddle lets the ball fall past - this game's own paddle only
  reads Left/Right, which the screenshot-script infrastructure has no
  "hold" support for the way it does for Up).

Also ported the sibling's own `b75fccf` fix ("clear screen to black once
after game launch") into `gamesMain.c`: `md_beginFrame()` now runs once,
immediately after a game is selected/launched and before that game's own
`init()` runs, both in `gamesMain_dispatchFrame()`'s own menu-selection
branch (matching the sibling's exact fix) and in
`gamesMain_launchGameDirect()` (the `-g`/`.joy`/`-ms` direct-launch path
the sibling has no equivalent of, added here for the same reason -
`-ms`'s own batch mode launches every game back-to-back into the same
persistent `gScreen`, so a game whose `init()` doesn't draw a full frame
right away would otherwise have its very first screenshot capture the
*previous* game's leftover frame instead of a clean black start).

New thumbnails (`thumb_33.bmp`..`thumb_39.bmp`, regenerated into
`thumbnailData.h` via `tools/gen_thumbnails.py`) and Playdate PNG
thumbnails (`thumb_33.png`..`thumb_39.png`, same `magick -sample 128x64`
point-sample process) were generated from real `-ms` captures of actual
gameplay, not placeholder art - verified by eye (`Read`-ing each
converted screenshot) before cropping, the same "check the actual
screenshot" standard every other thumbnail in this project was held to.

**The boot-clear fix above was missing its own Playdate equivalent** -
found on direct user follow-up, not caught the first time through. The
Playdate port has its own from-scratch menu (`src/playdate/main.c`, see
"The Playdate port" above) that never goes through `gamesMain.c`'s own
`gamesMain_dispatchFrame()` at all, so porting the sibling's fix into
`gamesMain.c` alone didn't cover this port. Fixed by adding the identical
`md_beginFrame()` call to `main.c`'s own menu-selection branch, right
before its own `menu_getGame(chosen)->init()` call - same placement,
same reasoning, just a second call site since this port's menu is a
separate implementation, not a shared one.

**Bat Bonanza's own screenshot/thumbnail showed just the score, no
bats or ball** - found on direct user report, then confirmed by reading
the shipped screenshot. Root cause: the default `-ms` script's long total
wait (4 taps, 90-frame gaps, no cap on `PLAYING` time) reliably outlives
a real point being scored in this game specifically, landing on
`PONG_STATE_ROUND_FLASH` instead (which only blinks the two score
digits - no bats/ball drawn at all). Compounding this, the left paddle
only reads Up/Down, which the default script never sends, so it sits
wherever it clamps to and a round resolves quickly. Fixed with a
per-game override (`BAT BONANZA` in `screenshotScriptFor()`): one tap to
leave `ATTRACT`, then a wait tuned to clear `PONG_STATE_COUNTDOWN`'s own
fixed 180-real-frame duration (a 60-frame wait *before* the first digit
even decrements, then 60 more each for "3"->"2"->"1" - a first attempt
budgeted only 120, missing that leading wait, and still landed on
"GET READY -- 1") plus a small buffer, short enough to land in real
`PLAYING` before the stuck paddle can miss. `thumb_10.bmp`, its Playdate
`thumb_10.png` counterpart, `thumbnailData.h`, and
`metadata/screenshots/BAT BONANZA.png` were all regenerated from the
corrected capture.

**An eighth game, Tiny Mania**, was added to the sibling project shortly
after the seven above and ported here the same way (a single background
agent this time, one file being large enough - 2147 lines, the biggest
in this project - to warrant its own dedicated task rather than batching
it with anything else). A Pac-Man-style maze/ghost-chase game by Daniel C
with a jump mechanic (Fire triggers a fixed jump-height animation letting
the player pass safely over a ghost mid-air), reusing Tiny Arena's own
half-resolution-buffer rendering technique directly rather than a new
one. No `forceRedraw` needed (redraws unconditionally every tick, same
reasoning as Tiny Fi). Unlike the batch above, this one needed no
screenshot-script tuning at all - the default script landed on real
gameplay (maze, dots, the Pac sprite, score) on the first try. Registered
as index 40 (`thumb_40.bmp`/`thumb_40.png`), verified the same way as
every prior addition: independently re-built from a clean tree (not just
trusting the agent's own report), grepped for leftover Vircon32-dialect
syntax, smoke-tested via `-g`, and the resulting screenshot checked by
eye before cropping.

## A real D-pad input-bleed bug in the menu's own quit-to-menu transition, ported from the sibling tinyjoypad_vircon32 project

Found and fixed first in the sibling `tinyjoypad_vircon32` project (which
this project's own `menu.c` was originally dialect-converted from), then
applied here too since both projects share this exact `menu.c` shape:
`menu_init()` used to reset every `prevX` unconditionally to `false`,
regardless of whatever the D-pad's own real physical state already was at
that exact moment. `menu_init()` is called right after confirming Quit in
the quit-confirmation dialog (`gamesMain.c`'s own `currentGameIndex = -1;
menu_init();`), and Fire itself was already safe on this exact path -
`md_armInputFireGate()` (armed immediately before this, in the same
`confirmingQuit` block) makes `md_inputFire()` itself report "released"
until the physical button genuinely is, so `menu.c`'s own `fire =
md_inputFire()` read already couldn't see a leftover press - but no
equivalent gate exists anywhere for Up/Down/Left/Right. A player still
holding, say, Right (moving their character) at the exact moment they
confirmed Quit would have had `prevRight` forced to `false` while the
real button stayed `true` - manufacturing a false "just pressed" edge on
the very next `menu_update()` tick and instantly paging the just-reopened
menu sideways, with no new input from the player at all (the bug this
section's own title refers to: "menu not always staying on last played
game when confirming quit game").

**Fixed** by having `menu_init()` sample each button's own real current
state (`md_inputUp()`/`Down()`/`Left()`/`Right()`/`Fire()`) instead of
assuming released - the same "arm against whatever's already held" idea
`md_armInputFireGate()` already uses for Fire on this exact path, just
applied to the menu's own direction buttons too. `menu_init()`'s other
call site (app startup, before any game has ever run) is unaffected in
practice - a player could technically be holding a direction at the exact
moment the app finishes loading, but that's a far narrower window than
the every-single-quit case this was actually found for.

Ported directly from the identical fix already made in the sibling
project (same shared-origin `menu.c`, structurally identical code) -
not independently re-discovered here. The Playdate port is unaffected:
it has its own from-scratch menu (`src/playdate/main.c`'s own
`menuUpdate()`/`returnToMenu()`, see "The Playdate port" above) that
never calls `menu.c`'s own `menu_init()`/`menu_update()` at all (compiled
in but genuinely dead code on that port, confirmed by real
`-Wunused-function` warnings for both on a Playdate rebuild). Verified
with a clean rebuild of all three ports (`src/sdl3/`, `src/sdl2/`,
`src/playdate/`, all link successfully, no new warnings beyond the
pre-existing Playdate ones just mentioned) and a smoke run of the SDL3
build - not verified by an actual reproduce-then-fix play-test on this
project specifically (the bug requires a precisely-timed Start-press
while holding a direction key, awkward to script), but this is a direct,
mechanical port of an already-proven fix applied to structurally
identical code, matching this project's own established precedent for
porting sibling fixes (Tiny Missile, Tiny Bike, etc. above).

## Twenty-two more games backported: Cracky and inufuto's whole "Cate engine" family (CH32V003/RISC-V, a new hardware class)

The sibling `tinyjoypad_vircon32` project added 22 new games in its own
history, all from inufuto's own "Cate engine" - a small self-contained
C++ framework (`Uncopyable.h`/`Timer.cpp`/`ScanKeys.h`/`Oled.cpp`/
`Vram.cpp`/`Sound.cpp`/`Print.cpp`, flattened to C the same way every
other class-shaped source in this project has been) shared across dozens
of small `github.com/inufuto/UIAPduino_*` repos. This is the first
**CH32V003 RISC-V** hardware in this whole project - not AVR, not
ESP8266/ESP8285 - though it makes no difference to the porting recipe
itself: same SSD1306 128x64 display model, same Vircon32-dialect source
shape, same standard-C dialect-conversion rules already established for
every prior batch. All 22 credited "INUFUTO" (the shared GitHub handle
across every one of these repos - no individual real name stated
anywhere), license "None specified" for all (no LICENSE file in any
upstream repo) - the same "known handle, unstated license" treatment
already established for Datacute/Sunpazed/RobotMasterC. None of the 22
needed EEPROM (none had any upstream) or `forceRedraw` (every one
redraws its own full frame unconditionally every tick, confirmed
per-game rather than assumed).

Games: Cracky, Aerial, Antiair, Ascend, Awass, Battlot, Bootskell,
Cacorm, Cavit, Guntus, Hopman, Impetus, Lift, Mazy, Mazy2, Mieyen,
Neuras, Osotos, Ruptus, Svellas, Sword, Yewdow. See the sibling
project's own `CLAUDE.md` ("Cracky, and a whole new source family" and
its own follow-up sections) for the real porting history behind this
family on that side - the display-orientation debugging saga (concluded
"no transform needed at all", a real finding this project's own porting
agents were briefed on verbatim to avoid re-deriving it), the
`crkStatusChar` full-width redesign fixing a title-screen text-collision
bug found via a real hardware photo, and the specific declaration-order/
array-by-value/missing-`#define` bugs found and fixed in individual
games during that project's own central-registration pass. This
project's own port started from that already-fixed, final sibling
source throughout - none of that discovery needed to happen again here.

**Dispatched to 22 parallel background porting agents** (one per game,
`Agent` tool, `run_in_background: true`), each given: the shared
dialect-conversion recipe; an explicit instruction to read the sibling's
own `gameCracky.c` first as the reference implementation (shared
rendering/shim architecture, the no-orientation-transform finding
repeated verbatim so no agent would "helpfully" re-add one); a note that
some sibling games in this family build status/title text from
individual char-literal constants rather than real string literals (no
live string-corruption bug there, but this project's own established
convention - confirmed by cross-checking already-shipped sibling ports
mid-batch - is still to type those `char[]`, not `int[]`); and an
explicit instruction not to touch `games.h`/`menuGameList.c` (reserved
for one central registration pass, to avoid a 22-way merge race) and not
to play-test, only compile-verify.

**The dispatch itself needed real recovery, the same pattern the sibling
project's own 21-agent batch already hit and documented**: several
individual agents failed mid-run with transient server-side API errors
(unrelated to this project's own code), sometimes after already writing
a large, apparently-complete file. Handled case-by-case rather than
blanket-retrying everything: `gameBootskell.c` and `gameRuptus.c` both
crashed moments before their own agent could report its verification
results, but each file was already fully written and syntactically
complete (confirmed by checking for the closing `gameXxx_update()` brace
and both required public functions) - independently re-verified
directly (compile + the data-table diff script) rather than re-dispatched,
both passing clean. `gameCacorm.c`, `gameHopman.c` (twice), and
`gameAscend.c` (which had failed to even *launch* in the very first
dispatch batch, distinct from a mid-run crash) had genuinely written
nothing yet when their own failure arrived - straightforwardly
re-dispatched fresh each time until each one completed and passed its
own verification.

**Independently re-verified every one of the 22 files afterward**,
project-wide, rather than trusting any individual agent's own
self-report at face value - the same discipline this project has relied
on since the very first multi-agent porting wave: a project-wide
standalone compile check (all 22 files, exit 0 across the board); a
project-wide run of the data-table-diff script against each file's own
sibling source (**0 mismatches, on all 22**, run directly rather than
just re-reading each agent's own claimed script output); a grep sweep
for leftover Vircon32-dialect array syntax and un-typedef'd `struct`
tags (clean - the only two hits were inside comment prose describing an
`int[27]` design choice in English, not real code); confirmation every
file declares exactly the required `avrCompat.h`/`machineDependent.h`/
`tinyJoypadShim.h` include set and exactly the two required public
functions with no stray `forceRedraw`; and confirmation no real
`eeprom_*()` call exists anywhere in the batch (only explanatory
comments stating why EEPROM doesn't apply to this non-AVR hardware).

**Capacity constants needed bumping before registration** - 60 existing
+ 22 new = 82 total, above every 64-slot cap in the codebase:
`MAX_GAMES` in `gameworld/menu.c` (64->128, matching the sibling
project's own identical bump for the identical reason), `THUMBNAIL_MAX_COUNT`
in both `sdl3/sdlBackend.c` and `sdl2/sdlBackend.c` (64->128, kept in
sync with `MAX_GAMES` per each file's own standing comment), and
`MENU_MAX_GAMES` in `playdate/main.c` (64->128, its own from-scratch
`gDisplayOrder[]` reimplementation of the same array, not a shared
constant - kept in sync manually, matching that file's own existing
"bumped 48->64 alongside gameworld/menu.c" comment precedent, updated to
say 64->128 instead).

**All 22 confirmed showing real gameplay in their own screenshots** -
platformers, shooters, maze/matching games, block-push puzzles, a
climbing game, a rock-mining cave scene, a sliding-panel puzzle, and
more - each visually inspected (not just captured and assumed correct)
via the standard `-ms -nd` batch-screenshot pass, no per-game
`screenshotScriptFor()` tuning needed for any of the 22 (unlike several
games in the immediately-prior 7-game batch) - every one of them reaches
real, visible gameplay under this project's own default script. New
thumbnails/screenshots/`.joy` files generated the same way as every
prior batch (`-crop 640x320+0+20 +repage -resize 256x128` for the
in-menu atlas BMPs, plain BMP->PNG for `metadata/screenshots/`, `-sample
128x64` for the Playdate PNG set), registration indices 60 (Cracky)
through 81 (Yewdow).

**`README.md`'s own "Games" section re-synced verbatim** with the
sibling project's own current table (89 lines, byte-identical,
confirmed via direct comparison) - all 22 new rows merged in
alphabetical order among the existing 60, `82 games`/MCU column intro
text updated to mention the CH32V003/RISC-V hardware class alongside
ESP8266/ESP8285, matching this project's own now-established "re-copy
the sibling's Games section verbatim rather than hand-editing it"
practice.

Verified with a clean sequential rebuild of all three ports (`src/sdl3/`,
`src/sdl2/`, `src/playdate/` all link successfully with no new errors,
only the same class of pre-existing benign warnings - `avrCompat.h`'s
own unused-helper warnings, a couple of `-Wstrict-prototypes` notes on
bare `()` function declarations - every other game file in this project
already produces) and a `-g`-based smoke test of every one of the 22 new
games on both SDL3 and SDL2 (all ran their full test window with no
crash).

## Osotos: a pushed block sliding off the left edge wrapped to the right side of the screen, ported from the sibling Vircon32 project

Found and fixed first in the sibling `tinyjoypad_vircon32` project (direct
user report there: "push the lower left block... it ends up at right
part of screen"), then applied here too since both projects share this
exact `gameOsotos.c` shape. `osoMoveBlocksOnce()`'s own per-tick boundary
check only tested the *upper* bound (`nextColumn >= OSO_COLUMN_COUNT ||
nextRow >= OSO_ROW_COUNT`), matching upstream's real `Block.cpp`
literally - but upstream's own `nextColumn`/`nextRow` are `byte`
(unsigned), so a block sliding left off column 0 there computes a
genuine unsigned wraparound (`0 + -1 = 255`), which that same
upper-bound-only check already catches correctly, settling the block
right at the edge - the same unsigned-byte-boundary reliance already
documented extensively elsewhere in this project (e.g. Lift's own
`CanMoveTo()` fix). This port's `nextColumn`/`nextRow` are plain,
non-wrapping signed `int`s, so a leftward-sliding block's column just
stays `-1` and never satisfies the upper-bound-only check - the block
keeps "moving" one column further left every tick, forever, with its own
on-screen column going increasingly negative. Once that negative column
feeds into the sprite/VVram positioning math elsewhere (which has no
equivalent negative guard of its own, since upstream never needed one
here), it wraps back into a large positive on-screen position - the
reported "ends up on the right side of the screen".

Unlike Lift's own fix (a single boundary already reachable from either
direction), this game's own `osoPushBlock()` already guards the *first*
push against a block already sitting at column 0/`ColumnCount-1`
(destroying it instead) - this exact gap is reachable only once a block
is already sliding continuously, on the tick it would cross the
boundary.

**Fixed** with an explicit lower-bound check alongside the existing
upper bound (`nextColumn < 0 || nextColumn >= OSO_COLUMN_COUNT ||
nextRow < 0 || nextRow >= OSO_ROW_COUNT`), reproducing upstream's real
two-sided boundary behavior directly rather than relying on wraparound -
matching the sibling project's own identical fix exactly.

Ported directly from the identical fix already made in the sibling
project (same shared-origin `gameOsotos.c`, structurally identical code)
- not independently re-discovered here. Verified with a clean rebuild of
all three ports (`src/sdl3/`, `src/sdl2/`, `src/playdate/`, all link
successfully, no new warnings beyond the same pre-existing benign class
every game file in this project already produces) - not verified by an
actual reproduce-then-fix play-test on this project specifically (the
bug requires pushing a specific block into a specific corner), but this
is a direct, mechanical port of an already-proven fix applied to
structurally identical code, matching this project's own established
precedent for porting sibling fixes.

## Awass: a "Throgh" monster could phase off the left/top map edge, ported from the sibling Vircon32 project

Found and fixed first in the sibling `tinyjoypad_vircon32` project (by
direct inspection while auditing that codebase for the same bug shape as
the Lift/Osotos fixes above, not from a live report), then applied here
too since both projects share this exact `gameAwass.c` shape. Upstream's
own `InRange()` (`Movable.cpp`) takes `column`/`row` as real `byte`
locals: `byte column = (pMovable->x >> ColumnCoordShift) + dx;` - when a
monster sits at column 0 and is offered `dx=-1` (one of
`DecideDirection()`'s own 4 candidate directions, tried whenever the
monster's normal `CanMove()` check just failed and its "Throgh"/
phase-through-walls state is active), that addition wraps a real AVR
byte to 255, and the very next line's own `column >= ColumnCount` check
correctly catches it and returns false - exactly the "off the left/top
edge" result upstream relies on the wraparound to detect. This port's
`column`/`row` are plain, non-wrapping signed ints (per `avrCompat.h`'s
own widening convention), so the same dx=-1-at-column-0 case (and the
equivalent dy=-1-at-row-0 case for the row check) instead left
`column`/`row` genuinely negative - `column >= AWA_COLUMN_COUNT` never
fired, and a negative `row` is always `< AWA_ROW_COUNT - 1` too, so both
halves of the function would silently report "in range" instead of "off
the edge". In play this would let a "Throgh" monster phase straight off
the left/top edge of the map the instant it's standing on the boundary
column/row and every normal direction is blocked -
`DecideDirection()` would set `pMonster->dx`/`dy` to walk it there, and
its own coordinate would then keep going further negative every tick -
the same "silently wanders off the intended edge, potentially
reappearing elsewhere via unrelated downstream coordinate math with no
negative guard of its own" symptom class already found and fixed in
Lift/Osotos above.

**Fixed** with explicit `< 0` checks on both axes in `awaInRange()`
(`column < 0 || column >= AWA_COLUMN_COUNT` / `row >= 0 && row <
AWA_ROW_COUNT - 1`), reproducing upstream's real two-sided boundary
behavior directly rather than relying on wraparound - matching the
sibling project's own identical fix exactly.

Ported directly from the identical fix already made in the sibling
project (same shared-origin `gameAwass.c`, structurally identical code)
- not independently re-discovered here. Verified with a clean rebuild of
all three ports (`src/sdl3/`, `src/sdl2/`, `src/playdate/`, all link
successfully, no new warnings beyond the same pre-existing benign class
every game file in this project already produces) - not verified by an
actual reproduce-then-fix play-test on this project specifically (the
bug requires a "Throgh"-state monster to reach a boundary column/row
with every normal direction blocked, an awkward state to script), but
this is a direct, mechanical port of an already-proven fix applied to
structurally identical code, matching this project's own established
precedent for porting sibling fixes.

## Menu status badges + toggle toasts, ported from the sibling gamebuino_classic_sdl project

Direct user request: port that project's own "menu sound, effect, gray
indicators + toasts" feature (its own commit `87ddaff`) - a permanent row
of badges in the menu's own top-right corner showing every global
toggle's current state, and a short-lived toast drawn over whatever's
already on screen the moment any of them flips. That project's own third
toggle - a three-state real-hardware gray-dithering/LCD-persistence
simulation, its own badge/toast included - is Gamebuino-Classic-specific
and has no equivalent on this project's own plain monochrome SSD1306
OLED, so only its other two toggles were ported: sound (already existed,
`gMuted`/ButY) and the glow/CRT/pixel-grid effect cycle (already existed,
`gOverlayMode`/`gGlowEnabled`/ButX).

**A real color had to be added to make the badges look right.** The
sibling project's own badges tell "on" from "off" by brightness (bright
white vs a dim gray) - a color this project genuinely didn't have before
this feature (`MD_COLOR_BLACK`/`WHITE` were the only two, matching the
real monochrome SSD1306 OLED every game was authored for). A first
attempt substituted a bordered box around "on" labels instead, reasoning
that was a reasonable monochrome-only equivalent - **rejected directly by
the user** ("why the fuck did you add rects around the status the status
need to display white when enabled and gray color when disabled like in
sibling project"), who wanted the real thing, not a substitute. Fixed for
real: `machineDependent.h` grew `MD_COLOR_DARKGRAY` and a color-aware
`md_drawColumnPixelsColor()` primitive (both SDL ports add a real
`gDarkGrayPixel` - RGB 90,90,90, the sibling's own exact value - alongside
the existing `gWhitePixel`/`gBlackPixel`), and `biosFont.h` grew
`biosDrawCharColor()`/`biosDrawTextColor()` on top of that, appended at
the very end of the file rather than modifying anything above (matching
the sibling project's own identical append-only approach to its own copy
of this exact file, whose own comment states it as "everything above this
point is copied verbatim... these two route through
md_drawColumnPixelsColor() instead of md_drawColumnPixels() for that one
reason"). `biosDrawChar()`/`biosDrawText()` themselves are untouched -
still always white, so every other menu string in this project keeps
working exactly as it always has. Every actual GAME still only ever draws
real `MD_COLOR_BLACK`/`WHITE` - the new color exists purely for this one
piece of platform-level menu UI.

**The effect-cycle toggle (ButX) used to be gated to `gInGame`** - toggling
it from the menu, where the effects never render anyway, was an
intentional no-op (see the CLAUDE.md's own "Presentation effects" section
above for why that gate existed in the first place). Now that the menu
itself shows the badge row live, that gate no longer makes sense - toggling
from the menu is genuinely visible now, so it was dropped, matching the
sibling project's own identical un-gating once it grew the same feature.
The effects still only ever *render* during actual gameplay (unchanged);
only the toggle input itself is no longer restricted. `showEffectToast()`
builds its own message from whichever of the three are actually active
("GRID+GLOW", "ALL OFF", etc, matching the sibling's own exact wording),
and the mute toggle shows "SOUND ON"/"SOUND OFF".

**A real staleness bug, caught by direct user follow-up** ("what did i
tell you about redrawing the screen when the toasts hide"): the toast is
drawn straight onto the persistent `gScreen` surface (same as the dialog
box and FPS overlay), not re-derived fresh every frame - so once its own
60-frame countdown reaches zero and it stops being drawn, whatever game is
currently running needs to genuinely repaint that exact screen region
itself, or the toast's own pixels stay burned in forever on any game whose
own `update()` skips redrawing on ticks where nothing else changed (an
`isInvalid`-style dirty-flag optimization, several games in this project
have one) - the identical "frozen screen with stale pixels" failure class
already found and fixed for the Playdate port's own pixel-grid toggle (see
that port's own `pixelGridMenuCallback()` comment) and the quit-dialog's
own resume path. Fixed with a new `md_statusToastJustExpired()` query
(true on exactly the one real tick the countdown reached zero, both SDL
ports), polled once per tick from `gamesMain_dispatchFrame()` - if true and
a game is actually running, that game's own `onResume()` hook (if it has
one) is called once, forcing a real redraw over the stale region. The menu
itself needs no equivalent handling - it already redraws unconditionally
every tick regardless.

**A second, genuine improvement over the sibling's own reference
implementation, found while answering a direct user question** ("why
arent the toast message drawn on top of the screen? does the sibling
project also not do that?"): confirmed by directly reading the sibling's
own `md_endFrame()` that its own toast, once baked into `gScreen`, gets
whatever glow/CRT/pixel-grid effect is currently active washed over it
too, unlike its own dialog box and FPS overlay, both of which get a crisp
re-composite on top of those same effects. This project's own toast now
gets that same crisp treatment: `drawStatusToast()` records the exact rect
it drew into (`gToastRectX`/`Y`/`W`/`H` - width varies with the current
message's own length, the same reason the FPS overlay tracks its own
width in variables rather than a fixed `#define`) and a `gToastDrawnThisFrame`
flag (needed because `gStatusToastFrames` itself is already decremented,
possibly to exactly zero on the toast's own last visible frame, by the
time the caller can check it) - `md_endFrame()` re-composites that rect
from `gScreenTexture`, after the glow/CRT/pixel-grid effects render, the
same "restore this one sub-rect's pre-effect pixels" technique already
used for `MD_DIALOG_X`/`Y`/`W`/`H` and the FPS overlay. A system toast
message stays legible regardless of which display effect is active now,
on both SDL ports - not a port of anything, since the sibling doesn't do
this for its own toast either.

Verified with a clean rebuild of both SDL ports (`src/sdl3/`, `src/sdl2/`,
both link successfully, no new warnings) and a direct visual check (a
temporary debug `md_showStatusToast()` call at startup, removed again
afterward): the badge row shows the correct on/off state for every
toggle, in the correct dim-vs-bright colors, and the toast box renders
correctly on top of the menu's own game list. Not independently verified
by ear/eye during actual gameplay with an effect active (the specific
scenario the crisp-recomposite fix targets) - the Playdate port was not
touched, since the user's own request was scoped to "the gamebuino_classic_sdl
code" specifically (an SDL-only sibling) and Playdate's own menu is a
from-scratch implementation that never calls `menu.c`'s own
`menu_update()` at all (see "The Playdate port" above).

## Effect cycle widened from 5 curated states to all 8 combinations, matching the sibling gamebuino_classic_sdl project

Direct user question: does this project's own glow/CRT/pixel-grid effect
cycle reach the same combinations as the sibling `gamebuino_classic_sdl`
project's own identical feature, when both were stepped through their
own single button? Checked directly by reading that project's own
`ButLB` handler - it didn't match. This project's own cycle (`gOverlayMode`,
ported from `crisp-game-lib-portable-sdl`'s own `cglpSDL3.c`) was a
curated 5-state subset - none, pixel-grid+glow, pixel-grid alone, CRT
alone, glow alone - built on the assumption that pixel-grid and CRT are
mutually exclusive "what kind of display is this" choices. The sibling
project's own cycle is a real 3-bit counter enumerating all 8 possible
combinations of the same three effects, "per direct request" (its own
comment's wording) from when glow/CRT were first ported into that project
from this one - deliberately choosing NOT to keep this project's own
curated subset, making every combination reachable instead (including
pixel-grid+CRT together, glow+CRT together, and all three at once, none
of which this project's own 5-state cycle could ever reach).

Since the sibling has strictly more reachable states (its own 8 is a
superset of this project's own 5), fixed by adopting its exact approach
here: `gOverlayMode` (a single int encoding "which one of pixel-grid/CRT,
if either") was replaced with two independent booleans
(`gPixelGridEnabled`/`gCrtEnabled`, alongside the already-independent
`gGlowEnabled`) and a real `gEffectState` 3-bit counter -
`gEffectState = (gEffectState + 1) % 8`, then each bit unpacked into one
of the three booleans, byte-for-byte matching the sibling's own handler.
`md_endFrame()`'s own effect-rendering block changed from `if(grid) ...
else if(crt) ...` to two independent `if`s, since pixel-grid and CRT can
genuinely both be active and drawn in the same frame now. Every other
reader of the old `gOverlayMode == 1`/`== 2` checks (the status badges,
the toast-message builder) was updated to read the new booleans directly.

Verified with a clean rebuild of both SDL ports (`src/sdl3/`, `src/sdl2/`,
both link successfully, no new warnings) and a smoke run of the SDL3
build. The Playdate port is unaffected - it has no equivalent effect
cycle at all (see "The Playdate port" above: glow/CRT were deliberately
out of scope there from the start, only pixel-grid was ever added, toggled
through the system menu rather than a button cycle).

## Playdate: the status badge/toast feature's own machineDependent.h contract, and a real Playdate-native implementation of both

**The Playdate build was silently broken by the badges/toast feature
above** - discovered only once actually attempted, not caught at the time
the SDL-side work landed, since only `src/sdl3/`/`src/sdl2/` were rebuilt
that session. `src/gameworld/gamesMain.c` and `menu.c` are both compiled
into the Playdate binary too (globbed in like every other `gameworld/*.c`
file, even though both files are genuinely dead code there - Playdate has
its own from-scratch dispatch/menu, see "The Playdate port" above), and
both now call the three new `machineDependent.h` functions
(`md_showStatusToast()`/`md_drawToggleStatusIcons()`/
`md_statusToastJustExpired()`) plus a fourth (`md_drawColumnPixelsColor()`,
called from `biosFont.h`'s own new `biosDrawCharColor()`, itself compiled
into every gameworld TU that includes `biosFont.h` - including
`gamesMain.c`, for its own quit-dialog text) - none of which had a
Playdate-side definition, so the link failed with four unresolved
externals the moment a Playdate build was actually attempted.

**`md_drawColumnPixelsColor()`** got the same treatment as the two other
already-established Playdate no-ops right above it
(`md_drawSolidRect()`/`md_drawColumnPixels()`, both from before this
session): a real, callable, parameter-ignoring stub - its only real
caller is `biosDrawCharColor()`, itself only ever reachable from the dead
BIOS-font menu path this port never runs, so the stub exists purely to
satisfy the link.

**`md_showStatusToast()`/`md_drawToggleStatusIcons()` got a genuine
Playdate-native implementation instead, ported directly from the sibling
gamebuino_classic_sdl project's own identical Playdate port** (per direct
user request to match it, "as well drawing the G from grid"):
`md_drawToggleStatusIcons()` itself stays a real no-op (never called -
this port's own from-scratch `menuUpdate()` doesn't go through
`gameworld/menu.c` at all), but a private `menuDrawStatusIcon()` draws
the real thing directly from `menuUpdate()`'s own draw section instead.
Unlike the SDL ports' own four-word badge row (SND/GRID/GLOW/CRT), this
port only ever has ONE of those wired to anything at all - the pixel grid,
flipped from Playdate's own system-menu checkmark (see
`pixelGridMenuCallback()` above) - there's no mute here (no CLI/audio-
toggle infrastructure on this port at all) and no glow/CRT (deliberately
out of scope for this port from the start). So the badge is a single
letter, "G", not a word: 400x240 has nowhere near the width for the SDL
labels, and inventing badges for toggles this port doesn't have would
just be misleading. Boxed (inverted fill) when on, outlined when off,
since a real 1-bit panel has no dim gray to fall back on the way the SDL
ports' own new `MD_COLOR_DARKGRAY` does. `md_showStatusToast()` itself
and the toast box drawing (`drawStatusToast()`, called last from
`update()`, same "lands on top of everything" placement as the SDL ports'
own `md_endFrame()` call site) are both close-to-verbatim ports of the
sibling's own identical functions, adapted from `pd_api.h`'s own
`pd->graphics->` calls in place of `biosDrawText()`/`md_drawSolidRect()`.
Wired into `pixelGridMenuCallback()` (shows "PIXEL GRID ON"/"PIXEL GRID
OFF" the instant the checkmark flips - the toast matters more here than on
the SDL ports, since flipping the checkmark happens inside the system
menu, which then closes over the game, so without a toast there's no
confirmation the press did anything at all).

**`md_statusToastJustExpired()` needed real logic, not a permanently-false
stub, caught by direct user follow-up** ("remember it needs to also
redraw the screen after toasts get hidden") after an initial version
returned `false` unconditionally, reasoned (wrongly) that Playdate's own
toast - drawn directly from `update()`, not routed through
`gamesMain.c`'s own dispatch the way the SDL ports' equivalent is - had
nothing equivalent to the SDL ports' own persistent-`gScreen` staleness
risk to guard against. That reasoning missed that Playdate's own
framebuffer is exactly as persistent as the SDL ports' `gScreen` (real
Gamebuino/SSD1306 VRAM behavior) - this project's own "Pixel-grid effect +
system menu" section above already found and fixed the identical
staleness risk for the pixel-grid toggle itself, on this exact port, for
this exact reason. Fixed by moving the actual redraw-on-expiry logic
directly into `drawStatusToast()` (no need for the public
`md_statusToastJustExpired()` query/poll indirection the SDL ports use -
that exists purely so `gamesMain.c`, in a different translation unit, can
ask about state living in `sdlBackend.c`; this port's own toast is drawn
directly from `update()`, in the same file that already has
`gCurrentGameIndex` in scope, so the check just happens right there): on
the exact tick the countdown reaches zero, if a game is currently running,
its own `onResume()` hook (if it has one) is called once, forcing a real
redraw - the identical fix, and the identical reasoning, already
established for `pixelGridMenuCallback()` a few functions above it.
`md_statusToastJustExpired()` itself stays a real, permanently-`false`
stub (still needed purely to satisfy `gamesMain.c`'s own dead-code-but-
still-linked call to it), since the actual redraw-on-expiry work now
happens locally instead.

Verified with a clean rebuild (`cmake -B build && cmake --build build`,
including the `pdc`-driven `.pdx` packaging step) - confirmed genuinely
broken before this fix (four real unresolved-external linker errors) and
fully clean after, no new warnings beyond the same pre-existing
`-Wstrict-prototypes`/unused-static-helper class already present on this
port. Not verified by an actual Simulator run/screenshot this session.

## Status

All 82 games ported, verified, and wired into the menu on all three ports
(`src/sdl3/`, `src/sdl2/`, `src/playdate/`); the menu shows real gameplay
thumbnails on every port. CLI parameters, FPS display, and the batch-
screenshot tool are in place on both SDL ports; all three presentation
effects (glow, CRT scanlines, pixel-grid) are implemented there and cycle
correctly via a single button, gated to gameplay only and skipped over the
quit-confirmation dialog's own rect specifically (`md_setDialogShowing()`)
and the `-fps` overlay's own rect (`md_setFpsOverlayShowing()`, same
re-composite-on-top technique, added later on direct user request since
the overlay reads a lot less useful blurred/scanlined along with the
actual gameplay) while still applying to the rest of the frozen screen
behind either. Real, persistent high-score saving (`eepromShim`, see its
own section above) is live for every game that ever had one upstream
(plus a few deliberate extensions) on all three ports - a real dotfile
on the SDL ports, a real sandboxed Data-folder file on Playdate - and
game logic on both SDL ports is decoupled from the display's own actual
refresh rate (see the 100Hz/144Hz section above), not just assumed 60Hz.
Packaging
is done: both SDL ports now embed their thumbnails directly into the exe
at compile time (`tools/gen_thumbnails.py` -> `assets/thumbnails/
thumbnailData.h`, see "Thumbnails" above - no more post-build `assets/`
copy step, no sibling directory needed alongside the built exe), and the
Playdate port has its own `pdc`-driven `.pdx` packaging. The `F3`/`PageUp`/
`PageDown`/`S` keybinds (fullscreen, volume, mute - see
`sdlBackend_pollEvents()`) are live-wired on both SDL ports, not just
declared in the keybind table; each change is logged to the console since
there's no on-screen indicator for any of the three. The Playdate port has
its own from-scratch paginated menu (see "The Playdate port" above), its
own pixel-grid-only effect toggle plus a "Menu" entry exposed through
Playdate's own system menu rather than a spare button, and its own 50fps-
compensated audio/frame-counter handling. `README.md`'s own "Games"
section is kept byte-identical to the sibling project's own (see the
7-game batch section above) - re-copy it verbatim rather than hand-editing
it independently whenever the sibling's own table changes. Not yet done:
nothing currently tracked - revisit this section as new work starts.

## References

- [tinyjoypad_vircon32/CLAUDE.md](https://github.com/joyrider3774/tinyjoypad_vircon32/blob/main/CLAUDE.md) -
  the full per-game porting/bug-fix/optimization history this project's
  own game code inherited unchanged.
- [tinyjoypad_vircon32/VIRCON32_C_DIALECT.md](https://github.com/joyrider3774/tinyjoypad_vircon32/blob/main/VIRCON32_C_DIALECT.md) -
  the source dialect this project's own "Dialect conversion" section
  above converts *back out of*.
- https://www.tinyjoypad.com/tinyjoypad_attiny85 - the original games.
- https://www.vircon32.com/ - the platform the sibling project targets.
