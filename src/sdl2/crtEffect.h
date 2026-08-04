#ifndef CRT_EFFECT_H
#define CRT_EFFECT_H

// -----------------------------------------------------------------------------
// A scrolling CRT-scanline overlay - a periodic pattern of semi-transparent
// horizontal stripes, drawn over a rendered frame and slowly scrolling
// downward over real time. Inspired by crisp-game-lib-portable-sdl's own
// cglpSDL2.c (CreateCRTEffect/UpdateCRTEffect/RenderCRTEffect), but drawn
// via a GPU texture + SDL_Renderer draw calls here rather than cglp's own
// CPU SDL_Surface-to-SDL_Surface blits - see glowEffect.h's own header
// comment for why a full-screen-resolution CPU blit every frame turned out
// to be a real, measured performance problem in this project (the same
// underlying SDL_Surface-blits-are-always-software-bound issue applies
// here too, just less severely since this pattern is blitted unscaled
// rather than scaled - moved to the same GPU-backed approach as glowEffect
// preemptively, rather than waiting to hit the same measured slowdown a
// second time). The stripe pattern itself is still only rendered once, at
// Create time - only the per-frame scroll-position bookkeeping and the
// couple of draw calls to actually present it are new.
//
// Self-contained platform-side module (freely includes SDL.h) so it can be
// reused by any future SDL project the same way cglpSDL2.c's own version
// already was here - the only project-specific assumption is that the
// caller renders through an SDL_Renderer.
// -----------------------------------------------------------------------------

#include <SDL.h>

typedef struct
{
    SDL_Texture* scanlineTexture; // pre-rendered stripe pattern, screenWidth x screenHeight, RGBA with alpha
    int   screenWidth, screenHeight;
    int   scanlineSpacing;              // pixels between stripe starts
    float scanlineFps;                  // scroll speed, in pixels/second
    float scrollOffset;                 // current scroll position, wraps at scanlineSpacing
} CrtEffect;

// Pre-renders the stripe pattern once (a fixed-size RGBA texture with
// alpha-blended horizontal lines every scanlineSpacing pixels, each
// scanlineThickness pixels tall, built via a throwaway CPU surface just
// for this one-time upload) - the same texture is reused, just scrolled,
// on every subsequent CrtEffect_Render() call. Returns NULL on allocation
// failure.
CrtEffect* CrtEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight,
    int scanlineSpacing, int scanlineThickness, float scanlineFps,
    Uint8 r, Uint8 g, Uint8 b, Uint8 a );

// Advances the scroll offset - call once per real frame with the real
// elapsed time (seconds) since the last call.
void CrtEffect_Update( CrtEffect* effect, float deltaTime );

// Draws the (possibly-scrolled) stripe pattern to fill the renderer's
// current render target - call after the frame's real content is already
// drawn, so the stripes end up on top.
void CrtEffect_Render( SDL_Renderer* renderer, CrtEffect* effect );

void CrtEffect_Destroy( CrtEffect* effect );

#endif
