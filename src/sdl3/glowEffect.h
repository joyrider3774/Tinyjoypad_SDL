#ifndef GLOW_EFFECT_H
#define GLOW_EFFECT_H

// -----------------------------------------------------------------------------
// A soft "phosphor bloom" glow around bright pixels, applied as a whole-
// screen post-process. Inspired by crisp-game-lib-portable-sdl's own
// cglpSDL3.c glow effect (createDistanceTable/applyGlowToRect/
// applyGlowToCharacterPixel) but NOT a verbatim port of it - that
// implementation draws a glow border around each individual on-screen
// "character" rect, one call per game object, which only makes sense
// against cglp's own vector-rectangle-per-character rendering model. This
// project instead composites its entire frame as a grid of small filled-
// rect column blits (see machineDependent.h's own header comment) with no
// equivalent per-object rect list to hook into - a whole-frame bloom is
// both cheaper (a handful of draw calls instead of thousands of per-
// object glow blits) and looks more correct anyway (a real phosphor bloom
// is a property of the whole displayed image, not of each individual
// pixel run).
//
// Implementation: a classic "poor man's bloom" - downscale the already-
// rendered sharp frame (SDL's own linear filtering naturally blurs it in
// the process), then let the GPU scale that small blurred image back up
// to full screen size via a hardware-accelerated texture draw with
// additive blending. Bright/white shapes bleed a soft glow into their
// surrounding dark pixels; already-dark areas gain nothing (additive
// blending of near-zero stays near-zero).
//
// A first version of this did the upscale on the CPU too (SDL_Surface-to-
// SDL_Surface, both at full screen resolution) - measured directly (a
// batch-screenshot run that normally took ~4s for all 33 games took over
// 15s for just 3 once that CPU upscale was added, a ~40x per-frame
// slowdown) confirming a full-screen CPU blit-with-scaling is fundamentally
// the wrong tool here: SDL_Surface blits are always software/CPU-bound
// regardless of which SDL_Renderer backend is active, so scaling up to
// 230,400 destination pixels every single frame ate most of a 60fps frame
// budget on its own. Rewritten to keep the CPU-side work confined to the
// cheap downscale step (a small destination surface) and hand the
// expensive upscale off to the GPU via a texture draw instead, where
// hardware bilinear filtering makes it close to free regardless of
// resolution - the whole reason a "streaming small texture, render
// hardware-scaled" design exists at all in this module, rather than
// simply requesting a smaller downscale factor on the original CPU-only
// design (which wouldn't have helped: the destination pixel count for the
// final composite is fixed at the full screen size either way).
//
// Self-contained platform-side module (freely includes SDL.h), reusable
// by any future SDL project independent of this one's own gameworld/
// machineDependent split - the only project-specific assumption is that
// the caller renders through an SDL_Renderer (any project using
// SDL_CreateRenderer already does).
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>

typedef struct
{
    SDL_Surface* downscaleSurface; // screenWidth/factor x screenHeight/factor, CPU-side scratch
    SDL_Texture* glowTexture;      // same size as downscaleSurface, re-uploaded from it each frame
} GlowEffect;

// downscaleFactor controls blur softness: higher values blur more (each
// source pixel spreads over a wider area once the GPU scales it back up).
// Cost is dominated by the GPU draw (cheap regardless of factor) and the
// CPU downscale blit (cheap since its destination is always small) - 8 is
// a reasonable default for a 640x360 screen (80x45 intermediate).
// Returns NULL on allocation failure.
GlowEffect* GlowEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight, int downscaleFactor );

// Reads the current (already fully rendered) contents of sourceSurface
// (a CPU-side SDL_Surface - the actual game content, NOT the renderer's
// own backbuffer), downscales+re-uploads it to the small internal
// texture, then draws that texture hardware-scaled to fill the
// renderer's current render target with additive, tinted, alpha-scaled
// blending. Call after the sharp frame has already been drawn to the
// renderer (e.g. right after the main SDL_RenderTexture() call in a
// typical frame), so the glow composites on top of it.
void GlowEffect_Render( SDL_Renderer* renderer, SDL_Surface* sourceSurface, GlowEffect* effect,
    Uint8 r, Uint8 g, Uint8 b, Uint8 alpha );

void GlowEffect_Destroy( GlowEffect* effect );

#endif
