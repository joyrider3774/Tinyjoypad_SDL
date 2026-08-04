#include "pixelGridEffect.h"

PixelGridEffect* PixelGridEffect_Create( SDL_Renderer* renderer, int screenWidth, int screenHeight,
    int pixelSpacing, int lineThickness )
{
    PixelGridEffect* effect = (PixelGridEffect*)SDL_malloc( sizeof( PixelGridEffect ) );
    if( !effect )
      return NULL;

    if( pixelSpacing < 1 )  pixelSpacing = 1;
    if( lineThickness < 1 ) lineThickness = 1;

    // SDL_CreateRGBSurfaceWithFormat, not SDL3's SDL_CreateSurface - see
    // glowEffect.c's own note on the same call.
    SDL_Surface* gridSurface = SDL_CreateRGBSurfaceWithFormat( 0, screenWidth, screenHeight, 32, SDL_PIXELFORMAT_RGBA32 );
    if( !gridSurface )
    {
        SDL_free( effect );
        return NULL;
    }

    // SDL_FillRect/SDL_MapRGBA(surface->format, ...), not SDL3's
    // SDL_FillSurfaceRect/SDL_MapSurfaceRGBA(surface, ...) - see
    // crtEffect.c's own note on the same pair of calls.
    SDL_FillRect( gridSurface, NULL, SDL_MapRGBA( gridSurface->format, 0, 0, 0, 0 ) );

    Uint32 lineColor = SDL_MapRGBA( gridSurface->format, 0, 0, 0, 255 );
    SDL_Rect dst;

    // A thin line at every source-pixel boundary (every pixelSpacing
    // pixels), not every other one - see this file's own header comment
    // for why cglp's own formula (spacing == pixelSpacing*2, thickness ==
    // full pixelSpacing) doesn't translate directly to this project's own
    // much larger GAME_SCALE.
    for( int x = 0; x < screenWidth; x += pixelSpacing )
    {
        dst.x = x;
        dst.y = 0;
        dst.w = lineThickness;
        dst.h = screenHeight;
        SDL_FillRect( gridSurface, &dst, lineColor );
    }

    for( int y = 0; y < screenHeight; y += pixelSpacing )
    {
        dst.x = 0;
        dst.y = y;
        dst.w = screenWidth;
        dst.h = lineThickness;
        SDL_FillRect( gridSurface, &dst, lineColor );
    }

    effect->gridTexture = SDL_CreateTextureFromSurface( renderer, gridSurface );
    SDL_FreeSurface( gridSurface );

    if( !effect->gridTexture )
    {
        SDL_free( effect );
        return NULL;
    }

    SDL_SetTextureBlendMode( effect->gridTexture, SDL_BLENDMODE_BLEND );

    return effect;
}

void PixelGridEffect_Render( SDL_Renderer* renderer, PixelGridEffect* effect )
{
    if( !effect || !renderer )
      return;

    // SDL_RenderCopy, not SDL3's SDL_RenderTexture - see glowEffect.c's own
    // note on the same call.
    SDL_RenderCopy( renderer, effect->gridTexture, NULL, NULL );
}

void PixelGridEffect_Destroy( PixelGridEffect* effect )
{
    if( !effect )
      return;

    if( effect->gridTexture )
      SDL_DestroyTexture( effect->gridTexture );

    SDL_free( effect );
}
