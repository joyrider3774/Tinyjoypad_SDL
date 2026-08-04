# Optimizations

This project's cost model is genuinely different from the sibling
`tinyjoypad_vircon32` build's own [OPTIMIZATIONS.md](https://github.com/joyrider3774/tinyjoypad_vircon32/blob/main/OPTIMIZATIONS.md),
so this file isn't a copy of it with the numbers changed - it's a
different set of concerns entirely.

## Why the per-game optimization list from the Vircon32 build doesn't need repeating here

Every per-game rendering optimization documented in the sibling project's
own `OPTIMIZATIONS.md` (row/x-range gating, per-page-row sprite
compositing instead of per-pixel re-scanning, dirty-flag caching, etc.) is
**already present in the ported C code itself** - this project reused
those files essentially unchanged (only the mechanical Vircon32-dialect
conversion touched them), so those optimizations came along for free. A
real desktop CPU running that same code is never going to be the
bottleneck the way Vircon32's fixed 15 MHz / 250,000-cycles-per-frame
budget could be - none of those 33 games have been observed to cost
anything measurable on modern hardware. If that ever changes (a
regression, or a new port with a genuinely heavy render loop), the
sibling project's own `CLAUDE.md`/`OPTIMIZATIONS.md` is still the right
place to look for the *shape* of likely fixes (per-pixel work that didn't
need to run every pixel; a self-gated function that still costs a full
call every time it's invoked) even though the actual bottleneck here, if
one ever appears, will be measured against a completely different budget
(real frame time on real hardware, not a fixed cycle count).

## What this build's own performance work actually was: platform-layer presentation effects, not game logic

The one real, measured performance problem found in this project so far
was **not** in any game - it was in the platform layer's own glow/CRT
presentation effects (see `CLAUDE.md` for the full writeup), and the
lesson is close to the opposite of the Vircon32 build's own recurring
"per-pixel work redone every frame" shape:

- **`SDL_Surface` blits are always CPU/software-bound, regardless of
  which `SDL_Renderer` backend is active.** A first version of the glow
  effect downscaled the rendered frame, then scaled it back up to full
  screen resolution (640x360, 230,400 destination pixels) via a CPU-side
  `SDL_BlitSurfaceScaled` call, once per frame. Measured directly: a
  batch-screenshot run that normally finishes all 33 games in ~4 seconds
  took over 15 seconds to get through just 3 once this was added - a
  roughly **40x** per-frame slowdown, easily enough to blow an entire
  60fps frame budget on the glow alone.
- **The fix**: keep the CPU-side work confined to the cheap *shrink* step
  (a small destination surface, e.g. 80x45 for an 8x downscale factor -
  cheap regardless of the source resolution), upload that small result to
  a GPU texture, and let the renderer's own hardware-accelerated texture
  scaling do the expensive part of stretching it back up to screen size -
  which costs close to nothing regardless of how much larger the
  destination is than the source texture. Re-measured after the fix: back
  to ~5 seconds for all 33 games with glow forced on (vs. ~4.2s with every
  effect off) - a ~20% overhead instead of ~40x.
- The CRT scanline overlay and the pixel-grid overlay were both built
  GPU-texture-based **from the start**, once this lesson was in hand
  (pre-baking each static pattern into a texture once, at Create time,
  then just a couple of cheap `SDL_RenderTexture()` draw calls per frame)
  - never needed their own separate discover-the-hard-way pass.

**Generalizable lesson for this project specifically** (distinct from the
sibling project's own "self-gated function still costs a full call" and
"per-pixel work that didn't need to run every pixel" lessons, though
related in spirit): on this platform, *any* full-screen-resolution
CPU-side `SDL_Surface` operation performed every frame is suspect,
regardless of how simple the operation looks per-pixel - reach for the
GPU (a texture + a scaled/blended `SDL_RenderTexture()` draw call)
instead, and measure before assuming a "just do it on the CPU, it's only
one blit" version is fine.

## Verification method

Same discipline as the sibling project: measured, not assumed.
`-ms -ns -nd` (batch-capture every game, no audio, uncapped framerate) is
a convenient stand-in stress test for wall-clock cost - real user/sys time
for all 33 games together, comparable before/after a change, is what
caught the 40x regression above (and confirmed the fix). No in-process
CPU/GPU-load overlay exists in this build (unlike the Vircon32 build's own
WebGL perf overlay) - real elapsed time via `-ms`, or an OS-level profiler
if finer detail is ever needed, is the tool for this project instead.
