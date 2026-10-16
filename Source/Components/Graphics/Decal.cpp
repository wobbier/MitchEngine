#include "PCH.h"
#include "Decal.h"
#include "Materials/ShaderGraphAsset.h"
#include "Components/Transform.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

ME_REFLECT_BEGIN( Decal )
    ME_FIELD( Albedo ).Asset( "Texture" ).Tooltip( "sRGB colour, alpha is coverage; empty projects a solid colour" );
    ME_FIELD( NormalMap ).Asset( "Texture" ).Tooltip( "Tangent-space normals; empty uses the surface normal reconstructed from depth" );
    ME_FIELD( Color ).Color();
    ME_FIELD( Opacity ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( Size ).Display( "Size (m)" ).Tooltip( "The projection box: width, height and depth" );
    ME_FIELD( SortOrder ).Tooltip( "Overlapping decals draw in ascending order (higher lands on top)" );
    ME_FIELD( Graph ).Asset( "ShaderGraph" ).Tooltip( "A Decal shader graph for the projected surface (replaces the textures and surface values)" );
    ME_FIELD( Metallic ).Category( "Surface" ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( Roughness ).Category( "Surface" ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( NormalStrength ).Category( "Surface" ).Range( 0.f, 4.f ).Speed( 0.01f );
    ME_FIELD( EmissiveColor ).Category( "Surface" ).Color();
    ME_FIELD( EmissiveIntensity ).Category( "Surface" ).Range( 0.f, 100.f ).Speed( 0.05f );
    ME_FIELD( AngleFade ).Category( "Fade" ).Range( 0.f, 180.f ).Tooltip( "Surfaces turned further than this from the projection get none of the decal (degrees)" );
    ME_FIELD( AngleFadeWidth ).Category( "Fade" ).Range( 0.f, 90.f ).Tooltip( "Degrees over which the decal fades out before AngleFade" );
ME_REFLECT_END()


Decal::Decal()
    : Component( "Decal" )
{
}


bool Decal::Resolve()
{
    auto resolve = []( const Path& InPath, std::string& InOutLoaded, SharedPtr<Moonlight::Texture>& OutTexture ) {
        const std::string& path = InPath.GetLocalPathString();
        if( path != InOutLoaded )
        {
            InOutLoaded = path;
            OutTexture = ( !path.empty() && InPath.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( InPath ) : nullptr;
        }
    };
    resolve( Albedo, m_loadedAlbedo, m_albedo );
    resolve( NormalMap, m_loadedNormal, m_normal );
    const std::string& graph = Graph.GetLocalPathString();
    if( graph != m_loadedGraph )
    {
        m_loadedGraph = graph;
        m_graph = graph.empty() ? nullptr : ResourceCache::GetInstance().Get<Moonlight::ShaderGraphAsset>( Moonlight::ShaderGraphAsset::ManifestPathFor( graph ) );
    }
    return !m_albedo || !m_albedo->IsLoading();
}


Matrix4 Decal::BoxMatrix( Transform& InTransform, const Vector3& InSize )
{
    // A flat box would make the projection singular: keep a millimetre of every axis.
    const glm::vec3 size = glm::max( glm::abs( InSize.InternalVector ), glm::vec3( 1e-3f ) );
    return Matrix4( InTransform.GetLocalToWorldMatrix().GetInternalMatrix() * glm::scale( glm::mat4( 1.f ), size ) );
}


Moonlight::DecalCommand Decal::BuildCommand( Transform& InTransform ) const
{
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    auto usable = []( const SharedPtr<Moonlight::Texture>& InTexture ) {
        return InTexture && !InTexture->IsLoading() && !InTexture->HasLoadFailed() ? InTexture->TexHandle : bgfx::TextureHandle( BGFX_INVALID_HANDLE );
    };
    constexpr float kDegrees = 3.14159265f / 180.f;

    Moonlight::DecalCommand command;
    const Matrix4 box = BoxMatrix( InTransform, Size );
    command.World = box.GetInternalMatrix();
    command.WorldToLocal = glm::inverse( command.World );
    for( int i = 0; i < 3; ++i )
    {
        const glm::vec3 axis( command.World[i] );
        const float length = glm::length( axis );
        glm::vec3 fallback( 0.f );
        fallback[i] = 1.f;
        command.Axes[i] = glm::vec4( length > 1e-8f ? axis / length : fallback, 0.f );
    }
    command.Albedo = usable( m_albedo );
    command.Normal = usable( m_normal );
    if( bgfx::isValid( command.Albedo ) )
    {
        command.TextureSize[0] = static_cast<float>( std::max( m_albedo->mWidth, 1 ) );
        command.TextureSize[1] = static_cast<float>( std::max( m_albedo->mHeight, 1 ) );
    }
    if( bgfx::isValid( command.Normal ) )
    {
        command.TextureSize[2] = static_cast<float>( std::max( m_normal->mWidth, 1 ) );
        command.TextureSize[3] = static_cast<float>( std::max( m_normal->mHeight, 1 ) );
    }

    const float opacity = std::clamp( Opacity, 0.f, 1.f );
    command.Color[0] = toLinear( Color.x );
    command.Color[1] = toLinear( Color.y );
    command.Color[2] = toLinear( Color.z );
    command.Color[3] = opacity;

    // Full coverage up to (AngleFade - AngleFadeWidth) from the projection, none past AngleFade.
    const float fadeEnd = std::clamp( AngleFade, 0.f, 180.f );
    const float fadeStart = std::clamp( fadeEnd - std::max( AngleFadeWidth, 0.f ), 0.f, fadeEnd );
    const float intensity = std::max( EmissiveIntensity, 0.f );
    command.Emissive[0] = toLinear( EmissiveColor.x ) * intensity;
    command.Emissive[1] = toLinear( EmissiveColor.y ) * intensity;
    command.Emissive[2] = toLinear( EmissiveColor.z ) * intensity;
    command.Emissive[3] = std::cos( fadeStart * kDegrees );

    command.Surface[0] = std::clamp( Metallic, 0.f, 1.f );
    command.Surface[1] = std::clamp( Roughness, 0.f, 1.f );
    command.Surface[2] = std::max( NormalStrength, 0.f );
    command.Surface[3] = std::cos( fadeEnd * kDegrees );

    command.WorldBounds = AABB::FromCenterExtents( Vector3(), Vector3( 0.5f, 0.5f, 0.5f ) ).Transformed( box );
    command.SortOrder = SortOrder;
    command.Graph = m_graph && m_graph->IsValid() && m_graph->GetTarget() == "Decal" ? m_graph : nullptr;
    return command;
}
