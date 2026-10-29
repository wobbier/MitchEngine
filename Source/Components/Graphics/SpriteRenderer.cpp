#include "PCH.h"
#include "SpriteRenderer.h"
#include "Components/Transform.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( Moonlight::SpriteBlend, { { "Alpha", Moonlight::SpriteBlend::Alpha }, { "Additive", Moonlight::SpriteBlend::Additive } } )

ME_REFLECT_BEGIN( SpriteRenderer )
    ME_FIELD( Texture ).Asset( "Texture" ).Tooltip( "The image or atlas; empty draws a white quad" );
    ME_FIELD( Region ).Tooltip( "Atlas rectangle in texels: x, y (from the top left), width, height. Zero width or height uses the whole image" );
    ME_FIELD( Pivot ).Speed( 0.01f ).Tooltip( "The point the transform holds: 0, 0 bottom left, 1, 1 top right" );
    ME_FIELD( PixelsPerUnit ).Range( 0.01f, 4096.f ).Tooltip( "Texels per world unit (sets the size unless Size overrides it)" );
    ME_FIELD( Size ).Speed( 0.01f ).Tooltip( "World size; zero uses the region's size / Pixels Per Unit. 9-slice sprites stretch their middle to it" );
    ME_FIELD( FlipX ).Display( "Flip X" );
    ME_FIELD( FlipY ).Display( "Flip Y" );
    ME_FIELD( Tint ).Color();
    ME_FIELD( Opacity ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( SortingLayer ).Category( "Sorting" ).Display( "Sorting Layer" ).Tooltip( "Higher layers draw on top" );
    ME_FIELD( OrderInLayer ).Category( "Sorting" ).Display( "Order In Layer" ).Tooltip( "Within a layer, higher draws on top; equal sprites sort back to front" );
    ME_FIELD( NineSlice ).Category( "9-Slice" ).Display( "Borders" ).Tooltip( "Left, top, right, bottom borders in texels: corners keep their size, the middle stretches (zero = off)" );
    ME_FIELD( Lit ).Category( "Rendering" ).Tooltip( "Shaded by the scene's lights like a rough surface facing the viewer" );
    ME_FIELD( PointFilter ).Category( "Rendering" ).Display( "Point Filter" ).Tooltip( "Nearest-texel sampling for pixel art" );
    ME_FIELD( Blend ).Category( "Rendering" );
ME_REFLECT_END()


SpriteRenderer::SpriteRenderer()
    : Component( "SpriteRenderer" )
{
}


bool SpriteRenderer::Resolve()
{
    const std::string& path = Texture.GetLocalPathString();
    if( path != m_loadedTexture )
    {
        m_loadedTexture = path;
        m_texture = ( !path.empty() && Texture.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Texture ) : nullptr;
    }
    return !m_texture || !m_texture->IsLoading();
}


Vector2 SpriteRenderer::GetTextureSize() const
{
    if( m_texture && !m_texture->IsLoading() && !m_texture->HasLoadFailed() )
    {
        return Vector2( static_cast<float>( std::max( m_texture->mWidth, 1 ) ), static_cast<float>( std::max( m_texture->mHeight, 1 ) ) );
    }
    return Vector2( 1.f, 1.f );
}


Vector2 SpriteRenderer::GetWorldSize() const
{
    if( Size.x > 0.f && Size.y > 0.f )
    {
        return Size;
    }
    const Vector2 texture = GetTextureSize();
    const bool region = Region.z > 0.f && Region.w > 0.f;
    const float ppu = std::max( PixelsPerUnit, 0.01f );
    // Without a texture, a unit square.
    if( !m_texture )
    {
        return Vector2( 1.f, 1.f );
    }
    return Vector2( ( region ? Region.z : texture.x ) / ppu, ( region ? Region.w : texture.y ) / ppu );
}


Moonlight::SpriteCommand SpriteRenderer::BuildCommand( Transform& InTransform ) const
{
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    Moonlight::SpriteCommand command;
    command.World = InTransform.GetLocalToWorldMatrix().GetInternalMatrix();
    const Vector2 texture = GetTextureSize();
    const bool region = Region.z > 0.f && Region.w > 0.f;
    if( region && m_texture )
    {
        command.UvRect = glm::vec4( Region.x / texture.x, Region.y / texture.y, ( Region.x + Region.z ) / texture.x, ( Region.y + Region.w ) / texture.y );
    }
    command.Color = glm::vec4( toLinear( Tint.x ), toLinear( Tint.y ), toLinear( Tint.z ), std::clamp( Opacity, 0.f, 1.f ) );
    const Vector2 size = GetWorldSize();
    command.Size = glm::vec2( size.x, size.y );
    command.Pivot = glm::vec2( Pivot.x, Pivot.y );
    if( m_texture && ( NineSlice.x > 0.f || NineSlice.y > 0.f || NineSlice.z > 0.f || NineSlice.w > 0.f ) )
    {
        const float ppu = std::max( PixelsPerUnit, 0.01f );
        command.Border = glm::vec4( NineSlice.x, NineSlice.y, NineSlice.z, NineSlice.w ) / ppu;
        command.BorderUv = glm::vec4( NineSlice.x / texture.x, NineSlice.y / texture.y, NineSlice.z / texture.x, NineSlice.w / texture.y );
    }
    command.FlipX = FlipX;
    command.FlipY = FlipY;
    command.Lit = Lit;
    command.PointFilter = PointFilter;
    command.Blend = Blend;
    command.Layer = SortingLayer;
    command.Order = OrderInLayer;
    command.Texture = m_texture && !m_texture->IsLoading() && !m_texture->HasLoadFailed() ? m_texture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );

    // World bounds of the quad's corners.
    const glm::vec2 lo( -command.Pivot.x * command.Size.x, -command.Pivot.y * command.Size.y );
    const glm::vec2 hi = lo + command.Size;
    glm::vec3 minimum( std::numeric_limits<float>::max() );
    glm::vec3 maximum( -std::numeric_limits<float>::max() );
    for( int corner = 0; corner < 4; ++corner )
    {
        const glm::vec3 point = glm::vec3( command.World * glm::vec4( corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, 0.f, 1.f ) );
        minimum = glm::min( minimum, point );
        maximum = glm::max( maximum, point );
    }
    command.Bounds = AABB( Vector3( minimum ), Vector3( maximum ) );
    return command;
}


#if USING( ME_EDITOR )
void SpriteRenderer::OnEditorInspect()
{
    Resolve();
}
#endif
