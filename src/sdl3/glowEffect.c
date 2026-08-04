#include "glowEffect.h"

GlowEffect* GlowEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight, int downscaleFactor )
{
    GlowEffect* effect = (GlowEffect*)SDL_malloc( sizeof( GlowEffect ) );
    if( !effect )
      return NULL;

    int dsW = screenWidth / downscaleFactor;
    if( dsW < 1 ) dsW = 1;
    int dsH = screenHeight / downscaleFactor;
    if( dsH < 1 ) dsH = 1;

    effect->downscaleSurface = SDL_CreateSurface( dsW, dsH, SDL_PIXELFORMAT_RGBA32 );
    effect->glowTexture = SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, dsW, dsH );

    if( !effect->downscaleSurface || !effect->glowTexture )
    {
        if( effect->downscaleSurface ) SDL_DestroySurface( effect->downscaleSurface );
        if( effect->glowTexture )      SDL_DestroyTexture( effect->glowTexture );
        SDL_free( effect );
        return NULL;
    }

    // Linear (not the crisp-pixel-art NEAREST every other texture in this
    // project uses) is deliberate here - the whole point of this texture
    // is to look soft/blurred once the GPU scales it up to screen size.
    SDL_SetTextureScaleMode( effect->glowTexture, SDL_SCALEMODE_LINEAR );

    // Additive: brightens the sharp frame underneath without ever
    // darkening it - a glow can only ever add light, never subtract it.
    SDL_SetTextureBlendMode( effect->glowTexture, SDL_BLENDMODE_ADD );

    return effect;
}

void GlowEffect_Render( SDL_Renderer* renderer, SDL_Surface* sourceSurface, GlowEffect* effect,
    Uint8 r, Uint8 g, Uint8 b, Uint8 alpha )
{
    if( !effect || !renderer || !sourceSurface )
      return;

    // The only CPU-bound per-pixel step - cheap because the destination
    // (downscaleSurface) is small regardless of the source's own
    // resolution; SDL's linear filtering during this shrink is what
    // actually produces the blur (each destination pixel becomes an
    // average of many source pixels).
    SDL_BlitSurfaceScaled( sourceSurface, NULL, effect->downscaleSurface, NULL, SDL_SCALEMODE_LINEAR );

    SDL_UpdateTexture( effect->glowTexture, NULL,
        effect->downscaleSurface->pixels, effect->downscaleSurface->pitch );

    SDL_SetTextureColorMod( effect->glowTexture, r, g, b );
    SDL_SetTextureAlphaMod( effect->glowTexture, alpha );

    // GPU-scaled draw to fill the current render target - hardware
    // bilinear filtering makes this close to free regardless of how much
    // larger the target is than the source texture, unlike the CPU-side
    // SDL_Surface scaled blit this module used at first (see this file's
    // own header comment for the measured cost of that approach).
    SDL_RenderTexture( renderer, effect->glowTexture, NULL, NULL );
}

void GlowEffect_Destroy( GlowEffect* effect )
{
    if( !effect )
      return;

    if( effect->downscaleSurface )
      SDL_DestroySurface( effect->downscaleSurface );

    if( effect->glowTexture )
      SDL_DestroyTexture( effect->glowTexture );

    SDL_free( effect );
}
