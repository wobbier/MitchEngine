#include "PCH.h"
#include "SpriteRenderer.h"
#include "Components/Transform.h"
#include "Graphics/SpriteSheet.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( Moonlight::SpriteBlend, { { "Alpha", Moonlight::SpriteBlend::Alpha }, { "Additive", Moonlight::SpriteBlend::Additive } } )

ME_REFLECT_BEGIN( SpriteRenderer )
    ME_FIELD( Texture ).Asset( "Texture" ).Tooltip( "The image or atlas; empty draws a white quad (or the sheet's image)" );
    ME_FIELD( Sheet ).Asset( "SpriteSheet" ).Tooltip( "A .spritesheet: its sprite named below sets the region, pivot and borders" );
    ME_FIELD( SpriteName ).Display( "Sprite" ).Tooltip( "The sheet's sprite to draw" );
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
    ME_FIELD( NormalMap ).Category( "Rendering" ).Asset( "Texture" ).Display( "Normal Map" ).Tooltip( "Lit sprites: a tangent-space normal map over the texture (green = up), so lights show the relief" );
    ME_FIELD( PointFilter ).Category( "Rendering" ).Display( "Point Filter" ).Tooltip( "Nearest-texel sampling for pixel art" );
    ME_FIELD( Blend ).Category( "Rendering" );
ME_REFLECT_END()


SpriteRenderer::SpriteRenderer()
    : Component( "SpriteRenderer" )
{
}


bool SpriteRenderer::Resolve()
{
    const std::string& sheet = Sheet.GetLocalPathString();
    if( sheet != m_loadedSheet )
    {
        m_loadedSheet = sheet;
        m_sheet = ( !sheet.empty() && Sheet.Exists ) ? ResourceCache::GetInstance().Get<Moonlight::SpriteSheet>( Sheet ) : nullptr;
    }
    // The sheet's image unless Texture names one.
    const std::string path = !Texture.GetLocalPathString().empty() || !m_sheet ? Texture.GetLocalPathString() : m_sheet->Texture;
    if( path != m_loadedTexture )
    {
        m_loadedTexture = path;
        const Path texture( path );
        m_texture = ( !path.empty() && texture.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( texture ) : nullptr;
    }
    const std::string& normal = NormalMap.GetLocalPathString();
    if( normal != m_loadedNormalMap )
    {
        m_loadedNormalMap = normal;
        m_normalMap = ( !normal.empty() && NormalMap.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( NormalMap ) : nullptr;
    }
    return ( !m_texture || !m_texture->IsLoading() ) && ( !m_normalMap || !m_normalMap->IsLoading() );
}


void SpriteRenderer::GetFrame( Vector4& OutRegion, Vector2& OutPivot, Vector4& OutBorder ) const
{
    OutRegion = Region;
    OutPivot = Pivot;
    OutBorder = NineSlice;
    const Moonlight::SpriteFrame* frame = m_sheet && !SpriteName.empty() ? m_sheet->Find( SpriteName ) : nullptr;
    if( frame )
    {
        OutRegion = Vector4( frame->Rect.x, frame->Rect.y, frame->Rect.z, frame->Rect.w );
        OutPivot = Vector2( frame->Pivot.x, frame->Pivot.y );
        OutBorder = Vector4( frame->Border.x, frame->Border.y, frame->Border.z, frame->Border.w );
    }
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
    Vector4 frame;
    Vector2 pivot;
    Vector4 border;
    GetFrame( frame, pivot, border );
    const bool region = frame.z > 0.f && frame.w > 0.f;
    const float ppu = std::max( PixelsPerUnit, 0.01f );
    // Without a texture, a unit square.
    if( !m_texture )
    {
        return Vector2( 1.f, 1.f );
    }
    return Vector2( ( region ? frame.z : texture.x ) / ppu, ( region ? frame.w : texture.y ) / ppu );
}


Moonlight::SpriteCommand SpriteRenderer::BuildCommand( Transform& InTransform ) const
{
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    Moonlight::SpriteCommand command;
    command.World = InTransform.GetLocalToWorldMatrix().GetInternalMatrix();
    const Vector2 texture = GetTextureSize();
    Vector4 frame;
    Vector2 pivot;
    Vector4 border;
    GetFrame( frame, pivot, border );
    const bool region = frame.z > 0.f && frame.w > 0.f;
    if( region && m_texture )
    {
        command.UvRect = glm::vec4( frame.x / texture.x, frame.y / texture.y, ( frame.x + frame.z ) / texture.x, ( frame.y + frame.w ) / texture.y );
    }
    command.Color = glm::vec4( toLinear( Tint.x ), toLinear( Tint.y ), toLinear( Tint.z ), std::clamp( Opacity, 0.f, 1.f ) );
    const Vector2 size = GetWorldSize();
    command.Size = glm::vec2( size.x, size.y );
    command.Pivot = glm::vec2( pivot.x, pivot.y );
    if( m_texture && ( border.x > 0.f || border.y > 0.f || border.z > 0.f || border.w > 0.f ) )
    {
        const float ppu = std::max( PixelsPerUnit, 0.01f );
        command.Border = glm::vec4( border.x, border.y, border.z, border.w ) / ppu;
        command.BorderUv = glm::vec4( border.x / texture.x, border.y / texture.y, border.z / texture.x, border.w / texture.y );
    }
    command.FlipX = FlipX;
    command.FlipY = FlipY;
    command.Lit = Lit;
    command.PointFilter = PointFilter;
    command.Blend = Blend;
    command.Layer = SortingLayer;
    command.Order = OrderInLayer;
    command.Texture = m_texture && !m_texture->IsLoading() && !m_texture->HasLoadFailed() ? m_texture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );
    command.NormalMap = m_normalMap && !m_normalMap->IsLoading() && !m_normalMap->HasLoadFailed() ? m_normalMap->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );

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
