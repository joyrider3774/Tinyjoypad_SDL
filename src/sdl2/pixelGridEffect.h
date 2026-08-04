#ifndef PIXEL_GRID_EFFECT_H
#define PIXEL_GRID_EFFECT_H

// -----------------------------------------------------------------------------
// A static "LCD pixel grid" overlay - a thin opaque black outline drawn
// around every source pixel's own boundary, so each of the original low-
// res pixels reads as its own distinct visible cell (like a real LCD's
// pixel structure) once scaled up, instead of blending into one smooth
// block. Inspired by crisp-game-lib-portable-sdl's own cglpSDL2.c (the
// `overlay == 1` branch, ~line 1295) but NOT a literal port of its exact
// spacing formula: that code draws a full `pixelSize`-wide opaque line
// every `pixelSize * 2` pixels, which - at cglp's own typical small
// `wscale` values - reads as a thin separator between characters, but
// applied unchanged against this project's own GAME_SCALE (5, much
// larger than cglp's typical scale) it instead blacked out every other
// entire source pixel outright (confirmed directly: a test capture showed
// a checkerboard block-out, not a grid of thin lines) - not what a "black
// outlined" pixel grid means. This version instead draws a thin fixed-
// width line at EVERY source-pixel boundary (spacing == GAME_SCALE, not
// GAME_SCALE*2), so each individual pixel gets its own visible outline
// regardless of how large GAME_SCALE happens to be.
//
// Unlike glowEffect.h/crtEffect.h, this needs no per-frame recomputation
// at all beyond the one draw call - the pattern is fixed for a given
// screen size/spacing/thickness, so (like crtEffect.h's own scanline
// texture) it's pre-rendered once, at Create time, into a texture that's
// just blitted unchanged every subsequent frame.
//
// Self-contained platform-side module (freely includes SDL.h), reusable
// by any future SDL project independent of this one's own gameworld/
// machineDependent split - the only project-specific assumption is that
// the caller renders through an SDL_Renderer.
// -----------------------------------------------------------------------------

#include <SDL.h>

typedef struct
{
    SDL_Texture* gridTexture; // screenWidth x screenHeight, RGBA with alpha
} PixelGridEffect;

// pixelSpacing should match the on-screen size of one original/source
// pixel (this project's own GAME_SCALE) - a lineThickness-wide opaque
// black line is drawn at every multiple of pixelSpacing, in both
// directions, outlining each individual source pixel rather than
// blacking out alternating ones. Returns NULL on allocation failure.
PixelGridEffect* PixelGridEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight,
    int pixelSpacing, int lineThickness );

// Draws the pre-rendered grid pattern to fill the renderer's current
// render target - call after the frame's real content is already drawn,
// so the grid lines end up on top.
void PixelGridEffect_Render( SDL_Renderer* renderer, PixelGridEffect* effect );

void PixelGridEffect_Destroy( PixelGridEffect* effect );

#endif
