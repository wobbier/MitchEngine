#include "PCH.h"
#include "TextRenderer.h"
#include "Components/Transform.h"
#include "Text/FontAtlas.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( TextAlignment, { { "Left", TextAlignment::Left }, { "Center", TextAlignment::Center }, { "Right", TextAlignment::Right } } )
ME_REFLECT_ENUM( TextAnchor, { { "Top", TextAnchor::Top }, { "Middle", TextAnchor::Middle }, { "Bottom", TextAnchor::Bottom } } )

ME_REFLECT_BEGIN( TextRenderer )
    ME_FIELD( Text ).Tooltip( "UTF-8; \\n breaks lines" );
    ME_FIELD( Font ).Tooltip( "A .ttf or .otf; empty = the engine's Inter" );
    ME_FIELD( Size ).Range( 0.001f, 1000.f ).Speed( 0.01f ).Tooltip( "Line height in world units" );
    ME_FIELD( LineSpacing ).Range( 0.1f, 10.f ).Speed( 0.01f );
    ME_FIELD( Alignment );
    ME_FIELD( Anchor ).Tooltip( "Which part of the block sits on the entity: its top, middle or bottom" );
    ME_FIELD( Color ).Color();
    ME_FIELD( Opacity ).Range( 0.f, 1.f ).Speed( 0.01f );
    ME_FIELD( Billboard ).Tooltip( "Turn to face the main camera (damage numbers, name tags)" );
    ME_FIELD( Lit ).Tooltip( "Shaded by the scene's lights like a lit sprite" );
    ME_FIELD( SortingLayer ).Category( "Sorting" ).Display( "Sorting Layer" );
    ME_FIELD( OrderInLayer ).Category( "Sorting" ).Display( "Order In Layer" );
ME_REFLECT_END()

namespace
{
    FontAtlas* AtlasFor( const Path& InFont )
    {
        const std::string path = InFont.GetLocalPathString();
        return FontAtlas::Get( path.empty() ? std::string( "Engine/Assets/Fonts/Inter-Regular.ttf" ) : path );
    }


    // Each line's width (line-height units) and the codepoints, split at \n.
    std::vector<std::pair<float, std::vector<uint32_t>>> Layout( FontAtlas& InAtlas, const std::string& InText )
    {
        std::vector<std::pair<float, std::vector<uint32_t>>> lines( 1 );
        size_t at = 0;
        while( at < InText.size() )
        {
            const uint32_t code = FontAtlas::NextCodepoint( InText, at );
            if( code == '\n' )
            {
                lines.emplace_back();
                continue;
            }
            if( code == '\r' )
            {
                continue;
            }
            lines.back().second.push_back( code );
        }
        for( auto& [width, codes] : lines )
        {
            for( size_t i = 0; i < codes.size(); ++i )
            {
                width += InAtlas.GetGlyph( codes[i] ).Advance + ( i + 1 < codes.size() ? InAtlas.GetKerning( codes[i], codes[i + 1] ) : 0.f );
            }
        }
        return lines;
    }
}


TextRenderer::TextRenderer()
    : Component( "TextRenderer" )
{
}


glm::vec2 TextRenderer::Measure() const
{
    FontAtlas* atlas = AtlasFor( Font );
    if( !atlas )
    {
        return glm::vec2( 0.f );
    }
    const auto lines = Layout( *atlas, Text );
    float width = 0.f;
    for( const auto& line : lines )
    {
        width = std::max( width, line.first );
    }
    return glm::vec2( width, 1.f + ( lines.size() - 1 ) * LineSpacing ) * Size;
}


void TextRenderer::BuildCommands( Transform& InTransform, const glm::mat3& InCameraRotation, std::vector<Moonlight::SpriteCommand>& OutCommands, uint64_t InEntityId ) const
{
    FontAtlas* atlas = AtlasFor( Font );
    if( !atlas || Text.empty() )
    {
        return;
    }
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    glm::mat4 world = InTransform.GetLocalToWorldMatrix().GetInternalMatrix();
    if( Billboard )
    {
        // The camera's orientation (its forward is +Z, so the text's -Z face looks back at it),
        // the entity's position and scale.
        const glm::vec3 scale( glm::length( glm::vec3( world[0] ) ), glm::length( glm::vec3( world[1] ) ), glm::length( glm::vec3( world[2] ) ) );
        const glm::vec3 position( world[3] );
        world = glm::mat4( glm::vec4( InCameraRotation[0] * scale.x, 0.f ), glm::vec4( InCameraRotation[1] * scale.y, 0.f ), glm::vec4( InCameraRotation[2] * scale.z, 0.f ), glm::vec4( position, 1.f ) );
    }
    const auto lines = Layout( *atlas, Text );
    const bgfx::TextureHandle texture = atlas->GetTexture();
    const float lineStep = LineSpacing;
    const float blockHeight = 1.f + ( lines.size() - 1 ) * lineStep;
    // The first baseline relative to the anchor (line-height units, y up).
    const float top = Anchor == TextAnchor::Top ? 0.f : Anchor == TextAnchor::Middle ? blockHeight * 0.5f : blockHeight;
    const glm::vec4 color( toLinear( Color.x ), toLinear( Color.y ), toLinear( Color.z ), std::clamp( Opacity, 0.f, 1.f ) );
    for( size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex )
    {
        const auto& [width, codes] = lines[lineIndex];
        float pen = Alignment == TextAlignment::Left ? 0.f : Alignment == TextAlignment::Center ? -width * 0.5f : -width;
        const float baseline = top - atlas->GetAscender() - static_cast<float>( lineIndex ) * lineStep;
        for( size_t i = 0; i < codes.size(); ++i )
        {
            const FontAtlas::Glyph& glyph = atlas->GetGlyph( codes[i] );
            if( glyph.Visible )
            {
                Moonlight::SpriteCommand command;
                const glm::vec2 center = ( glm::vec2( pen, baseline ) + glyph.Offset + glyph.Size * 0.5f ) * Size;
                command.World = world * glm::translate( glm::mat4( 1.f ), glm::vec3( center, 0.f ) );
                command.UvRect = glyph.Uv;
                command.Size = glyph.Size * Size;
                command.Color = color;
                command.Sdf = true;
                command.Lit = Lit;
                command.Layer = SortingLayer;
                command.Order = OrderInLayer;
                command.Texture = texture;
                command.ID = InEntityId;
                const glm::vec3 middle( command.World[3] );
                const float radius = glm::length( glm::vec3( world[0] ) ) * std::max( command.Size.x, command.Size.y );
                command.Bounds = AABB( Vector3( middle - glm::vec3( radius ) ), Vector3( middle + glm::vec3( radius ) ) );
                command.Sequence = static_cast<uint32_t>( OutCommands.size() );
                OutCommands.push_back( command );
            }
            pen += glyph.Advance + ( i + 1 < codes.size() ? atlas->GetKerning( codes[i], codes[i + 1] ) : 0.f );
        }
    }
}
