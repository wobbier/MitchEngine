#include "Renderer.h"
#include "RenderCommands.h"
#include "Camera/CameraData.h"
#include "Device/FrameBuffer.h"
#include "Graphics/Texture.h"
#include "CLog.h"
#include "optick.h"
#include <algorithm>
#include <utility>

// Sprites (LOOK-S2): SpriteCore's commands drawn after transparent meshes into HDR colour, in
// sorting layer, order in layer, then back-to-front order; consecutive sprites sharing a texture
// and state draw as one batch from a transient vertex buffer.

namespace
{
    const bgfx::VertexLayout& SpriteLayout()
    {
        static bgfx::VertexLayout layout = []
        {
            bgfx::VertexLayout out;
            out.begin()
                .add( bgfx::Attrib::Position, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Normal, 3, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float )
                .add( bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float )
                .add( bgfx::Attrib::Color0, 4, bgfx::AttribType::Float )
                .end();
            return out;
        }();
        return layout;
    }

    // 16-bit indices: a transient vertex buffer holds at most this many.
    constexpr uint32_t kMaxSpriteVertices = 65532;
}


const bgfx::VertexLayout& Moonlight::GetSpriteVertexLayout()
{
    return SpriteLayout();
}


uint32_t Moonlight::BuildSpriteVertices( const SpriteCommand& InSprite, SpriteVertex* OutVertices )
{
    float left = InSprite.Border.x;
    float top = InSprite.Border.y;
    float right = InSprite.Border.z;
    float bottom = InSprite.Border.w;
    float leftUv = InSprite.BorderUv.x;
    float topUv = InSprite.BorderUv.y;
    float rightUv = InSprite.BorderUv.z;
    float bottomUv = InSprite.BorderUv.w;
    // A flipped 9-slice mirrors its borders with the image.
    if( InSprite.FlipX )
    {
        std::swap( left, right );
        std::swap( leftUv, rightUv );
    }
    if( InSprite.FlipY )
    {
        std::swap( top, bottom );
        std::swap( topUv, bottomUv );
    }
    const float width = InSprite.Size.x;
    const float height = InSprite.Size.y;
    // Borders wider than the sprite shrink to fit, keeping their proportions.
    if( left + right > width && left + right > 0.f )
    {
        const float fit = width / ( left + right );
        left *= fit;
        right *= fit;
    }
    if( top + bottom > height && top + bottom > 0.f )
    {
        const float fit = height / ( top + bottom );
        top *= fit;
        bottom *= fit;
    }
    const bool nine = left > 0.f || top > 0.f || right > 0.f || bottom > 0.f;
    const float x0 = -InSprite.Pivot.x * width;
    const float y0 = -InSprite.Pivot.y * height;
    const float x3 = x0 + width;
    const float y3 = y0 + height;
    const glm::vec4& uv = InSprite.UvRect;
    // Columns and rows (bottom to top; v runs top to bottom in the image).
    const float xs[4] = { x0, x0 + left, x3 - right, x3 };
    const float ys[4] = { y0, y0 + bottom, y3 - top, y3 };
    const float us[4] = { uv.x, uv.x + leftUv, uv.z - rightUv, uv.z };
    const float vs[4] = { uv.w, uv.w - bottomUv, uv.y + topUv, uv.y };
    const int columns[3] = { 0, 1, 2 };
    const int count = nine ? 3 : 1;
    const glm::vec3 normal = glm::normalize( glm::mat3( InSprite.World ) * glm::vec3( 0.f, 0.f, -1.f ) );
    // +u runs along local +x (or -x when flipped); +v (down the image) along local -y (or +y flipped).
    const glm::vec3 tangent = glm::normalize( glm::mat3( InSprite.World ) * glm::vec3( InSprite.FlipX ? -1.f : 1.f, 0.f, 0.f ) );
    const glm::vec3 bitangentUp = glm::normalize( glm::mat3( InSprite.World ) * glm::vec3( 0.f, InSprite.FlipY ? -1.f : 1.f, 0.f ) );
    // The image's "up" (its green channel) is bitangentUp; w makes cross(normal, tangent) * w match it.
    const float handedness = glm::dot( glm::cross( normal, tangent ), bitangentUp ) < 0.f ? -1.f : 1.f;

    uint32_t written = 0;
    auto emit = [&]( int InColumn, int InRow, int InCornerX, int InCornerY ) {
        const int ix = nine ? InColumn + InCornerX : InCornerX * 3;
        const int iy = nine ? InRow + InCornerY : InCornerY * 3;
        const glm::vec4 position = InSprite.World * glm::vec4( xs[ix], ys[iy], 0.f, 1.f );
        float u = us[ix];
        float v = vs[iy];
        if( InSprite.FlipX ) u = uv.x + uv.z - u;
        if( InSprite.FlipY ) v = uv.y + uv.w - v;
        SpriteVertex& vertex = OutVertices[written++];
        vertex.Position[0] = position.x;
        vertex.Position[1] = position.y;
        vertex.Position[2] = position.z;
        vertex.Normal[0] = normal.x;
        vertex.Normal[1] = normal.y;
        vertex.Normal[2] = normal.z;
        vertex.Tangent[0] = tangent.x;
        vertex.Tangent[1] = tangent.y;
        vertex.Tangent[2] = tangent.z;
        vertex.Tangent[3] = handedness;
        vertex.Uv[0] = u;
        vertex.Uv[1] = v;
        vertex.Color[0] = InSprite.Color.r;
        vertex.Color[1] = InSprite.Color.g;
        vertex.Color[2] = InSprite.Color.b;
        vertex.Color[3] = InSprite.Color.a;
    };
    for( int row = 0; row < count; ++row )
    {
        for( int column = 0; column < count; ++column )
        {
            emit( columns[column], row, 0, 0 );
            emit( columns[column], row, 1, 0 );
            emit( columns[column], row, 1, 1 );
            emit( columns[column], row, 0, 1 );
        }
    }
    return written;
}


void BGFXRenderer::RenderSprites( Moonlight::CameraData& camera )
{
    OPTICK_EVENT( "Renderer::RenderSprites" );
    if( m_sprites.empty() || !m_spriteProgram.IsLoaded() || !camera.Buffer || !bgfx::isValid( camera.Buffer->ParticleBuffer ) )
    {
        return;
    }
    const glm::mat4& viewMatrix = camera.View.GetInternalMatrix();
    m_spriteOrder.clear();
    for( uint32_t i = 0; i < m_sprites.size(); ++i )
    {
        const Moonlight::SpriteCommand& sprite = m_sprites[i];
        if( camera.ShouldCull && !camera.ViewFrustum.Intersects( sprite.Bounds ) )
        {
            continue;
        }
        const glm::vec4 center = viewMatrix * glm::vec4( sprite.Bounds.GetCenter().InternalVector, 1.f );
        m_spriteOrder.emplace_back( center.z, i );
    }
    if( m_spriteOrder.empty() )
    {
        return;
    }
    // Layer, then order in layer, then back to front, then gathering order (stable across frames).
    std::sort( m_spriteOrder.begin(), m_spriteOrder.end(), [this]( const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b ) {
        const Moonlight::SpriteCommand& sa = m_sprites[a.second];
        const Moonlight::SpriteCommand& sb = m_sprites[b.second];
        if( sa.Layer != sb.Layer ) return sa.Layer < sb.Layer;
        if( sa.Order != sb.Order ) return sa.Order < sb.Order;
        if( a.first != b.first ) return a.first > b.first;
        return sa.Sequence < sb.Sequence;
    } );

    const bgfx::ViewId view = m_views.Allocate( camera.IsEditorView ? "Editor Sprites" : "Sprites" );
    if( view == UINT16_MAX )
    {
        return;
    }
    Moonlight::FrameBuffer& buffer = *camera.Buffer;
    bgfx::setViewFrameBuffer( view, buffer.ParticleBuffer );
    bgfx::setViewRect( view, 0, 0, static_cast<uint16_t>( buffer.Width ), static_cast<uint16_t>( buffer.Height ) );
    bgfx::setViewTransform( view, &camera.View.GetInternalMatrix()[0][0], &camera.ProjectionMatrix.GetInternalMatrix()[0][0] );
    bgfx::setViewMode( view, bgfx::ViewMode::Sequential );
    bgfx::touch( view );

    const glm::mat4& projection = camera.ProjectionMatrix.GetInternalMatrix();
    const float depthParams[4] = { projection[2][2], projection[3][2], camera.Projection == Moonlight::ProjectionType::Orthographic ? 1.f : 0.f, bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f };
    const bgfx::VertexLayout& layout = SpriteLayout();

    size_t next = 0;
    while( next < m_spriteOrder.size() )
    {
        // A chunk of sprites whose vertices fit one transient buffer.
        size_t end = next;
        uint32_t vertices = 0;
        while( end < m_spriteOrder.size() )
        {
            const Moonlight::SpriteCommand& sprite = m_sprites[m_spriteOrder[end].second];
            const bool nine = sprite.Border != glm::vec4( 0.f );
            const uint32_t count = nine ? 36u : 4u;
            if( vertices + count > kMaxSpriteVertices )
            {
                break;
            }
            vertices += count;
            ++end;
        }
        const uint32_t indices = vertices / 4 * 6;
        if( bgfx::getAvailTransientVertexBuffer( vertices, layout ) < vertices || bgfx::getAvailTransientIndexBuffer( indices ) < indices )
        {
            static bool s_warned = false;
            if( !s_warned )
            {
                s_warned = true;
                YIKES( "Sprites: out of transient buffer space this frame; the rest are skipped" );
            }
            return;
        }
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer( &tvb, vertices, layout );
        bgfx::allocTransientIndexBuffer( &tib, indices );
        auto* vertexData = reinterpret_cast<Moonlight::SpriteVertex*>( tvb.data );
        auto* indexData = reinterpret_cast<uint16_t*>( tib.data );

        uint32_t written = 0;
        uint32_t batchStart = 0;
        for( size_t i = next; i < end; ++i )
        {
            const Moonlight::SpriteCommand& sprite = m_sprites[m_spriteOrder[i].second];
            const uint32_t first = written;
            written += Moonlight::BuildSpriteVertices( sprite, vertexData + written );
            for( uint32_t quad = first; quad < written; quad += 4 )
            {
                uint16_t* out = indexData + quad / 4 * 6;
                out[0] = static_cast<uint16_t>( quad );
                out[1] = static_cast<uint16_t>( quad + 1 );
                out[2] = static_cast<uint16_t>( quad + 2 );
                out[3] = static_cast<uint16_t>( quad );
                out[4] = static_cast<uint16_t>( quad + 2 );
                out[5] = static_cast<uint16_t>( quad + 3 );
            }
            // A batch ends where the next sprite's texture or state differs.
            const bool last = i + 1 == end;
            const Moonlight::SpriteCommand* following = last ? nullptr : &m_sprites[m_spriteOrder[i + 1].second];
            const bool sameBatch = following && following->Texture.idx == sprite.Texture.idx && following->NormalMap.idx == sprite.NormalMap.idx && following->Lit == sprite.Lit
                && following->Blend == sprite.Blend && following->PointFilter == sprite.PointFilter && following->Sdf == sprite.Sdf;
            if( sameBatch )
            {
                continue;
            }
            const bgfx::TextureHandle texture = bgfx::isValid( sprite.Texture ) ? sprite.Texture : m_spriteWhite;
            const bool normalMapped = sprite.Lit && bgfx::isValid( sprite.NormalMap );
            const float params[4] = { sprite.Lit ? 1.f : 0.f, sprite.Blend == Moonlight::SpriteBlend::Additive ? 1.f : 0.f, normalMapped ? 1.f : 0.f, sprite.Sdf ? 1.f : 0.f };
            const uint64_t blend = sprite.Blend == Moonlight::SpriteBlend::Additive ? BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE ) : BGFX_STATE_BLEND_ALPHA;
            const uint32_t sampler = sprite.PointFilter ? ( BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP ) : UINT32_MAX;
            bgfx::setVertexBuffer( 0, &tvb );
            bgfx::setIndexBuffer( &tib, batchStart / 4 * 6, ( written - batchStart ) / 4 * 6 );
            bgfx::setTexture( 0, s_texSprite, Moonlight::Texture::Sampleable( texture, m_spriteWhite ), sampler );
            bgfx::setTexture( 1, s_sceneDepth, buffer.DepthTexture, BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
            bgfx::setTexture( 2, s_texSpriteNormal, normalMapped ? Moonlight::Texture::Sampleable( sprite.NormalMap, m_spriteWhite ) : m_spriteWhite, sampler );
            bgfx::setUniform( u_spriteParams, params );
            bgfx::setUniform( u_particleDepth, depthParams );
            // The sprite shader includes the lighting code, so its samplers are always bound.
            BindLighting();
            bgfx::setState( BGFX_STATE_WRITE_RGB | blend );
            bgfx::submit( view, m_spriteProgram.GetProgram() );
            ++m_spriteBatches;
            batchStart = written;
        }
        next = end;
    }
}
