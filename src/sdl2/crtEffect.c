#include "crtEffect.h"

CrtEffect* CrtEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight,
    int scanlineSpacing, int scanlineThickness, float scanlineFps,
    Uint8 r, Uint8 g, Uint8 b, Uint8 a )
{
    CrtEffect* effect = (CrtEffect*)SDL_malloc( sizeof( CrtEffect ) );
    if( !effect )
      return NULL;

    effect->screenWidth    = screenWidth;
    effect->screenHeight   = screenHeight;
    effect->scanlineSpacing = scanlineSpacing;
    effect->scanlineFps    = scanlineFps;
    effect->scrollOffset   = 0.0f;

    // A one-time CPU-side surface, only ever used to build the texture
    // below - never touched again afterward, unlike glowEffect.h's own
    // downscaleSurface (which really does need re-blitting every frame).
    // SDL_CreateRGBSurfaceWithFormat, not SDL3's SDL_CreateSurface - see
    // glowEffect.c's own note on the same call.
    SDL_Surface* stripeSurface = SDL_CreateRGBSurfaceWithFormat( 0, screenWidth, screenHeight, 32, SDL_PIXELFORMAT_RGBA32 );
    if( !stripeSurface )
    {
        SDL_free( effect );
        return NULL;
    }

    // SDL_FillRect/SDL_MapRGBA(surface->format, ...), not SDL3's
    // SDL_FillSurfaceRect/SDL_MapSurfaceRGBA(surface, ...) - SDL2 still
    // routes color-mapping through the surface's own SDL_PixelFormat*
    // (surface->format), rather than resolving format details from the
    // surface pointer directly the way SDL3's newer API does.
    SDL_FillRect( stripeSurface, NULL, SDL_MapRGBA( stripeSurface->format, 0, 0, 0, 0 ) );

    SDL_Rect lineRect = { 0, 0, screenWidth, scanlineThickness };
    Uint32 lineColor = SDL_MapRGBA( stripeSurface->format, r, g, b, a );

    for( int y = 0; y < screenHeight; y += scanlineSpacing )
    {
        lineRect.y = y;
        SDL_FillRect( stripeSurface, &lineRect, lineColor );
    }

    effect->scanlineTexture = SDL_CreateTextureFromSurface( renderer, stripeSurface );
    SDL_FreeSurface( stripeSurface );

    if( !effect->scanlineTexture )
    {
        SDL_free( effect );
        return NULL;
    }

    SDL_SetTextureBlendMode( effect->scanlineTexture, SDL_BLENDMODE_BLEND );

    return effect;
}

void CrtEffect_Update( CrtEffect* effect, float deltaTime )
{
    if( !effect )
      return;

    effect->scrollOffset += effect->scanlineFps * deltaTime;
    if( effect->scrollOffset >= effect->scanlineSpacing )
      effect->scrollOffset = 0.0f;
}

void CrtEffect_Render( SDL_Renderer* renderer, CrtEffect* effect )
{
    if( !effect || !renderer )
      return;

    int offsetY = (int)effect->scrollOffset;

    // Plain int SDL_Rect, not SDL3's float SDL_FRect - every value here is
    // already a whole pixel position (offsetY is cast to int above), so
    // there's no sub-pixel precision to preserve; SDL_RenderCopy (SDL2's
    // SDL_RenderTexture equivalent) takes SDL_Rect for both src and dst.
    SDL_Rect srcRect1 = { 0, offsetY, effect->screenWidth, effect->screenHeight - offsetY };
    SDL_Rect dstRect1 = { 0, 0, effect->screenWidth, effect->screenHeight - offsetY };
    SDL_RenderCopy( renderer, effect->scanlineTexture, &srcRect1, &dstRect1 );

    if( offsetY > 0 )
    {
        SDL_Rect srcRect2 = { 0, 0, effect->screenWidth, offsetY };
        SDL_Rect dstRect2 = { 0, effect->screenHeight - offsetY, effect->screenWidth, offsetY };
        SDL_RenderCopy( renderer, effect->scanlineTexture, &srcRect2, &dstRect2 );
    }
}

void CrtEffect_Destroy( CrtEffect* effect )
{
    if( !effect )
      return;

    if( effect->scanlineTexture )
      SDL_DestroyTexture( effect->scanlineTexture );

    SDL_free( effect );
}
