#include "PCH.h"
#include "Tilemap.h"
#include "Components/Transform.h"
#include "Graphics/SpriteSheet.h"
#include "Graphics/Texture.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#if USING( ME_EDITOR )
#include <imgui.h>
#include <Utils/HavanaUtils.h>
#endif

// The collider fields shared by the 2D colliders (Colliders2D.cpp).
#define ME_TILEMAP_COLLIDER_FIELDS()                                                                                        \
    ME_FIELD( Offset ).Tooltip( "Shifts the shape in the entity's local XY plane" );                                       \
    ME_FIELD( IsTrigger ).Display( "Is Trigger" ).Tooltip( "Report overlaps without colliding" );                         \
    ME_FIELD( Friction ).Range( 0.f, 2.f );                                                                                \
    ME_FIELD( Restitution ).Range( 0.f, 1.f );                                                                             \
    ME_FIELD( Density ).Range( 0.f, 100.f );

ME_REFLECT_BEGIN( Tilemap )
    ME_FIELD( Sheet ).Asset( "SpriteSheet" ).Tooltip( "The .spritesheet the tiles come from" );
    ME_FIELD( Palette ).Tooltip( "Sheet sprite names the cells use; \"@Base\" autotiles from Base_0..Base_15 by the four neighbours (1 up, 2 right, 4 down, 8 left)" );
    ME_FIELD( CellSize ).Range( 0.01f, 100.f ).Tooltip( "World units per cell" );
    ME_FIELD( Tint ).Color();
    ME_FIELD( Lit ).Category( "Rendering" );
    ME_FIELD( PointFilter ).Category( "Rendering" ).Display( "Point Filter" );
    ME_FIELD( SortingLayer ).Category( "Rendering" ).Display( "Sorting Layer" );
    ME_FIELD( OrderInLayer ).Category( "Rendering" ).Display( "Order In Layer" );
ME_REFLECT_END()

ME_REFLECT_BEGIN( TilemapCollider2D )
    ME_TILEMAP_COLLIDER_FIELDS()
ME_REFLECT_END()

#if USING( ME_EDITOR )
bool Tilemap::s_paintEnabled = false;
int Tilemap::s_paintIndex = 0;
#endif


Tilemap::Tilemap()
    : Component( "Tilemap" )
{
}


TilemapCollider2D::TilemapCollider2D()
    : Component( "TilemapCollider2D" )
{
}


int Tilemap::GetTile( int InX, int InY ) const
{
    const auto found = m_cells.find( Key( InX, InY ) );
    return found != m_cells.end() ? found->second : -1;
}


void Tilemap::SetTile( int InX, int InY, int InPaletteIndex )
{
    const int64_t key = Key( InX, InY );
    if( InPaletteIndex < 0 || InPaletteIndex >= static_cast<int>( Palette.size() ) )
    {
        if( m_cells.erase( key ) )
        {
            ++m_revision;
        }
        return;
    }
    auto found = m_cells.find( key );
    if( found == m_cells.end() || found->second != InPaletteIndex )
    {
        m_cells[key] = static_cast<int16_t>( InPaletteIndex );
        ++m_revision;
    }
}


void Tilemap::FillRect( int InX0, int InY0, int InX1, int InY1, int InPaletteIndex )
{
    for( int y = std::min( InY0, InY1 ); y <= std::max( InY0, InY1 ); ++y )
    {
        for( int x = std::min( InX0, InX1 ); x <= std::max( InX0, InX1 ); ++x )
        {
            SetTile( x, y, InPaletteIndex );
        }
    }
}


void Tilemap::Clear()
{
    if( !m_cells.empty() )
    {
        m_cells.clear();
        ++m_revision;
    }
}


void Tilemap::LocalToCell( const Vector2& InLocal, int& OutX, int& OutY ) const
{
    const float size = std::max( CellSize, 0.0001f );
    OutX = static_cast<int>( std::floor( InLocal.x / size ) );
    OutY = static_cast<int>( std::floor( InLocal.y / size ) );
}


std::string Tilemap::GetSpriteName( int InX, int InY ) const
{
    const int index = GetTile( InX, InY );
    if( index < 0 || index >= static_cast<int>( Palette.size() ) )
    {
        return {};
    }
    const std::string& entry = Palette[index];
    if( entry.empty() || entry[0] != '@' )
    {
        return entry;
    }
    // Autotiling: which neighbours hold the same entry.
    const std::string base = entry.substr( 1 );
    const int mask = ( GetTile( InX, InY + 1 ) == index ? 1 : 0 ) | ( GetTile( InX + 1, InY ) == index ? 2 : 0 )
        | ( GetTile( InX, InY - 1 ) == index ? 4 : 0 ) | ( GetTile( InX - 1, InY ) == index ? 8 : 0 );
    const std::string variant = base + "_" + std::to_string( mask );
    return m_sheet && !m_sheet->Find( variant ) ? base : variant;
}


std::vector<std::pair<Vector2, Vector2>> Tilemap::BuildOutline() const
{
    // Boundary edges per grid line, then runs of adjacent edges merged into one segment.
    // Horizontal: key (line y, side) -> x cells; vertical: key (line x, side) -> y cells.
    std::map<std::pair<int, int>, std::vector<int>> horizontal;
    std::map<std::pair<int, int>, std::vector<int>> vertical;
    for( const auto& [key, tile] : m_cells )
    {
        const int x = static_cast<int>( key >> 32 );
        const int y = static_cast<int>( static_cast<int32_t>( key & 0xffffffff ) );
        if( GetTile( x, y + 1 ) < 0 ) horizontal[{ y + 1, 0 }].push_back( x );
        if( GetTile( x, y - 1 ) < 0 ) horizontal[{ y, 1 }].push_back( x );
        if( GetTile( x + 1, y ) < 0 ) vertical[{ x + 1, 0 }].push_back( y );
        if( GetTile( x - 1, y ) < 0 ) vertical[{ x, 1 }].push_back( y );
    }
    const float size = CellSize;
    std::vector<std::pair<Vector2, Vector2>> segments;
    auto merge = [&]( std::map<std::pair<int, int>, std::vector<int>>& InLines, bool InHorizontal ) {
        for( auto& [line, cells] : InLines )
        {
            std::sort( cells.begin(), cells.end() );
            size_t start = 0;
            for( size_t i = 1; i <= cells.size(); ++i )
            {
                if( i < cells.size() && cells[i] == cells[i - 1] + 1 )
                {
                    continue;
                }
                const float from = static_cast<float>( cells[start] ) * size;
                const float to = static_cast<float>( cells[i - 1] + 1 ) * size;
                const float at = static_cast<float>( line.first ) * size;
                segments.emplace_back( InHorizontal ? Vector2( from, at ) : Vector2( at, from ), InHorizontal ? Vector2( to, at ) : Vector2( at, to ) );
                start = i;
            }
        }
    };
    merge( horizontal, true );
    merge( vertical, false );
    return segments;
}


bool Tilemap::Resolve()
{
    const std::string& sheet = Sheet.GetLocalPathString();
    if( sheet != m_loadedSheet )
    {
        m_loadedSheet = sheet;
        m_sheet = ( !sheet.empty() && Sheet.Exists ) ? ResourceCache::GetInstance().Get<Moonlight::SpriteSheet>( Sheet ) : nullptr;
    }
    const std::string texture = m_sheet ? m_sheet->Texture : std::string();
    if( texture != m_loadedTexture )
    {
        m_loadedTexture = texture;
        const Path path( texture );
        m_texture = ( !texture.empty() && path.Exists ) ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( path ) : nullptr;
    }
    return m_sheet && m_texture && !m_texture->IsLoading() && !m_texture->HasLoadFailed();
}


void Tilemap::BuildCommands( Transform& InTransform, std::vector<Moonlight::SpriteCommand>& OutCommands, uint64_t InEntityId ) const
{
    if( !m_sheet || !m_texture || m_texture->IsLoading() || m_texture->HasLoadFailed() )
    {
        return;
    }
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    const glm::mat4& world = InTransform.GetLocalToWorldMatrix().GetInternalMatrix();
    const glm::vec2 textureSize( static_cast<float>( std::max( m_texture->mWidth, 1 ) ), static_cast<float>( std::max( m_texture->mHeight, 1 ) ) );
    const glm::vec4 color( toLinear( Tint.x ), toLinear( Tint.y ), toLinear( Tint.z ), 1.f );
    const float size = CellSize;
    for( const auto& [key, tile] : m_cells )
    {
        const int x = static_cast<int>( key >> 32 );
        const int y = static_cast<int>( static_cast<int32_t>( key & 0xffffffff ) );
        const Moonlight::SpriteFrame* frame = m_sheet->Find( GetSpriteName( x, y ) );
        if( !frame )
        {
            continue;
        }
        Moonlight::SpriteCommand command;
        command.World = world;
        command.World[3] = world * glm::vec4( ( x + 0.5f ) * size, ( y + 0.5f ) * size, 0.f, 1.f );
        command.UvRect = glm::vec4( frame->Rect.x, frame->Rect.y, frame->Rect.x + frame->Rect.z, frame->Rect.y + frame->Rect.w ) / glm::vec4( textureSize, textureSize );
        command.Color = color;
        command.Size = glm::vec2( size );
        command.Pivot = glm::vec2( 0.5f );
        command.Lit = Lit;
        command.PointFilter = PointFilter;
        command.Layer = SortingLayer;
        command.Order = OrderInLayer;
        command.Texture = m_texture->TexHandle;
        command.ID = InEntityId;
        const glm::vec3 lo = glm::vec3( world * glm::vec4( x * size, y * size, 0.f, 1.f ) );
        const glm::vec3 hi = glm::vec3( world * glm::vec4( ( x + 1 ) * size, ( y + 1 ) * size, 0.f, 1.f ) );
        command.Bounds = AABB( Vector3( glm::min( lo, hi ) ), Vector3( glm::max( lo, hi ) ) );
        command.Sequence = static_cast<uint32_t>( OutCommands.size() );
        OutCommands.push_back( command );
    }
}


void Tilemap::OnSerialize( json& OutJson )
{
    Reflection::ToJson( StaticType(), this, OutJson );
    // [x, y, palette index], sorted so saves are stable.
    std::vector<std::array<int, 3>> tiles;
    tiles.reserve( m_cells.size() );
    for( const auto& [key, tile] : m_cells )
    {
        tiles.push_back( { static_cast<int>( key >> 32 ), static_cast<int>( static_cast<int32_t>( key & 0xffffffff ) ), tile } );
    }
    std::sort( tiles.begin(), tiles.end(), []( const std::array<int, 3>& a, const std::array<int, 3>& b ) { return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0]; } );
    json array = json::array();
    for( const auto& tile : tiles )
    {
        array.push_back( { tile[0], tile[1], tile[2] } );
    }
    OutJson["Tiles"] = array;
}


void Tilemap::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_cells.clear();
    if( auto tiles = InJson.find( "Tiles" ); tiles != InJson.end() && tiles->is_array() )
    {
        for( const json& tile : *tiles )
        {
            if( tile.is_array() && tile.size() == 3 )
            {
                m_cells[Key( tile[0].get<int>(), tile[1].get<int>() )] = static_cast<int16_t>( tile[2].get<int>() );
            }
        }
    }
    ++m_revision;
}


#if USING( ME_EDITOR )
void Tilemap::OnEditorInspect()
{
    Resolve();
    HavanaUtils::Label( "Tiles" );
    ImGui::Text( "%zu", m_cells.size() );
    HavanaUtils::Label( "Paint" );
    ImGui::Checkbox( "##PaintTiles", &s_paintEnabled );
    ImGui::SetItemTooltip( "Paint in the World view: left button paints the brush, right button erases" );
    ImGui::SeparatorText( "Brush" );
    if( ImGui::Selectable( "Eraser", s_paintIndex < 0 ) )
    {
        s_paintIndex = -1;
    }
    for( size_t i = 0; i < Palette.size(); ++i )
    {
        ImGui::PushID( static_cast<int>( i ) );
        if( ImGui::Selectable( ( std::to_string( i ) + ": " + Palette[i] ).c_str(), s_paintIndex == static_cast<int>( i ) ) )
        {
            s_paintIndex = static_cast<int>( i );
        }
        ImGui::PopID();
    }
    if( ImGui::Button( "Clear All" ) )
    {
        Clear();
    }
}
#endif
