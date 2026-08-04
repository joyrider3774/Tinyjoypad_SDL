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
    SDL_Surface* stripeSurface = SDL_CreateSurface( screenWidth, screenHeight, SDL_PIXELFORMAT_RGBA32 );
    if( !stripeSurface )
    {
        SDL_free( effect );
        return NULL;
    }

    SDL_FillSurfaceRect( stripeSurface, NULL, SDL_MapSurfaceRGBA( stripeSurface, 0, 0, 0, 0 ) );

    SDL_Rect lineRect = { 0, 0, screenWidth, scanlineThickness };
    Uint32 lineColor = SDL_MapSurfaceRGBA( stripeSurface, r, g, b, a );

    for( int y = 0; y < screenHeight; y += scanlineSpacing )
    {
        lineRect.y = y;
        SDL_FillSurfaceRect( stripeSurface, &lineRect, lineColor );
    }

    effect->scanlineTexture = SDL_CreateTextureFromSurface( renderer, stripeSurface );
    SDL_DestroySurface( stripeSurface );

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

    SDL_FRect srcRect1 = { 0, (float)offsetY, (float)effect->screenWidth, (float)( effect->screenHeight - offsetY ) };
    SDL_FRect dstRect1 = { 0, 0, (float)effect->screenWidth, (float)( effect->screenHeight - offsetY ) };
    SDL_RenderTexture( renderer, effect->scanlineTexture, &srcRect1, &dstRect1 );

    if( offsetY > 0 )
    {
        SDL_FRect srcRect2 = { 0, 0, (float)effect->screenWidth, (float)offsetY };
        SDL_FRect dstRect2 = { 0, (float)( effect->screenHeight - offsetY ), (float)effect->screenWidth, (float)offsetY };
        SDL_RenderTexture( renderer, effect->scanlineTexture, &srcRect2, &dstRect2 );
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
