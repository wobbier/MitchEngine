#include "SpriteSheet.h"
#include "CLog.h"
#include "File.h"
#include "VFS/VFS.h"
#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <algorithm>
#include <cstring>
#include <map>

#if USING( ME_EDITOR )
#include <Utils/HavanaUtils.h>
#include <imgui.h>
#endif

namespace Moonlight
{
    namespace
    {
        glm::vec4 ReadVec4( const json& InJson, const glm::vec4& InDefault )
        {
            if( !InJson.is_array() || InJson.size() != 4 )
            {
                return InDefault;
            }
            return glm::vec4( InJson[0].get<float>(), InJson[1].get<float>(), InJson[2].get<float>(), InJson[3].get<float>() );
        }


        glm::vec2 ReadVec2( const json& InJson, const glm::vec2& InDefault )
        {
            if( !InJson.is_array() || InJson.size() != 2 )
            {
                return InDefault;
            }
            return glm::vec2( InJson[0].get<float>(), InJson[1].get<float>() );
        }


        bool Opaque( const uint8_t* InRgba, int InWidth, int InX, int InY, int InThreshold )
        {
            return InRgba[( static_cast<size_t>( InY ) * InWidth + InX ) * 4 + 3] >= InThreshold;
        }
    }


    SpriteSheet::SpriteSheet( const Path& InPath )
        : Resource( InPath )
    {
    }


    bool SpriteSheet::Load()
    {
        std::string text;
        if( !VFS::Get().ReadText( FilePath, text ) || !Parse( text ) )
        {
            YIKES( "Sprite sheet " + FilePath.GetLocalPathString() + " isn't a sprite sheet" );
            return false;
        }
        ++m_revision;
        return true;
    }


    void SpriteSheet::Reload()
    {
        Load();
    }


    const SpriteFrame* SpriteSheet::Find( const std::string& InName ) const
    {
        const auto found = std::find_if( Sprites.begin(), Sprites.end(), [&]( const SpriteFrame& InFrame ) { return InFrame.Name == InName; } );
        return found != Sprites.end() ? &*found : nullptr;
    }


    bool SpriteSheet::Parse( const std::string& InText )
    {
        const json root = json::parse( InText, nullptr, false );
        if( root.is_discarded() || !root.is_object() )
        {
            return false;
        }
        Texture = root.value( "Texture", std::string() );
        Slicing = SpriteSlicing();
        if( auto slicing = root.find( "Slicing" ); slicing != root.end() && slicing->is_object() )
        {
            Slicing.Mode = slicing->value( "Mode", std::string( "Grid" ) ) == "Auto" ? SpriteSliceMode::Auto : SpriteSliceMode::Grid;
            Slicing.CellWidth = std::max( slicing->value( "CellWidth", 32 ), 1 );
            Slicing.CellHeight = std::max( slicing->value( "CellHeight", 32 ), 1 );
            Slicing.OffsetX = std::max( slicing->value( "OffsetX", 0 ), 0 );
            Slicing.OffsetY = std::max( slicing->value( "OffsetY", 0 ), 0 );
            Slicing.PaddingX = std::max( slicing->value( "PaddingX", 0 ), 0 );
            Slicing.PaddingY = std::max( slicing->value( "PaddingY", 0 ), 0 );
            Slicing.AlphaThreshold = std::clamp( slicing->value( "AlphaThreshold", 1 ), 1, 255 );
            Slicing.SkipEmpty = slicing->value( "SkipEmpty", true );
            Slicing.Pivot = ReadVec2( slicing->value( "Pivot", json() ), glm::vec2( 0.5f ) );
        }
        Sprites.clear();
        if( auto sprites = root.find( "Sprites" ); sprites != root.end() && sprites->is_array() )
        {
            for( const json& entry : *sprites )
            {
                if( !entry.is_object() )
                {
                    continue;
                }
                SpriteFrame frame;
                frame.Name = entry.value( "Name", std::string() );
                frame.Rect = ReadVec4( entry.value( "Rect", json() ), glm::vec4( 0.f ) );
                frame.Pivot = ReadVec2( entry.value( "Pivot", json() ), glm::vec2( 0.5f ) );
                frame.Border = ReadVec4( entry.value( "Border", json() ), glm::vec4( 0.f ) );
                Sprites.push_back( frame );
            }
        }
        return true;
    }


    std::string SpriteSheet::Write() const
    {
        json root = json::object();
        root["Texture"] = Texture;
        root["Slicing"] = {
            { "Mode", Slicing.Mode == SpriteSliceMode::Auto ? "Auto" : "Grid" },
            { "CellWidth", Slicing.CellWidth }, { "CellHeight", Slicing.CellHeight },
            { "OffsetX", Slicing.OffsetX }, { "OffsetY", Slicing.OffsetY },
            { "PaddingX", Slicing.PaddingX }, { "PaddingY", Slicing.PaddingY },
            { "AlphaThreshold", Slicing.AlphaThreshold }, { "SkipEmpty", Slicing.SkipEmpty },
            { "Pivot", { Slicing.Pivot.x, Slicing.Pivot.y } },
        };
        json sprites = json::array();
        for( const SpriteFrame& frame : Sprites )
        {
            sprites.push_back( {
                { "Name", frame.Name },
                { "Rect", { frame.Rect.x, frame.Rect.y, frame.Rect.z, frame.Rect.w } },
                { "Pivot", { frame.Pivot.x, frame.Pivot.y } },
                { "Border", { frame.Border.x, frame.Border.y, frame.Border.z, frame.Border.w } },
            } );
        }
        root["Sprites"] = sprites;
        return root.dump( 4 );
    }


    std::vector<SpriteFrame> SpriteSheet::SliceGrid( const uint8_t* InRgba, int InWidth, int InHeight, const SpriteSlicing& InSlicing, const std::string& InBaseName )
    {
        std::vector<SpriteFrame> frames;
        const int cellWidth = std::max( InSlicing.CellWidth, 1 );
        const int cellHeight = std::max( InSlicing.CellHeight, 1 );
        for( int y = InSlicing.OffsetY; y + cellHeight <= InHeight; y += cellHeight + InSlicing.PaddingY )
        {
            for( int x = InSlicing.OffsetX; x + cellWidth <= InWidth; x += cellWidth + InSlicing.PaddingX )
            {
                bool any = !InSlicing.SkipEmpty || !InRgba;
                for( int py = y; py < y + cellHeight && !any; ++py )
                {
                    for( int px = x; px < x + cellWidth && !any; ++px )
                    {
                        any = Opaque( InRgba, InWidth, px, py, InSlicing.AlphaThreshold );
                    }
                }
                if( !any )
                {
                    continue;
                }
                SpriteFrame frame;
                frame.Name = InBaseName + "_" + std::to_string( frames.size() );
                frame.Rect = glm::vec4( x, y, cellWidth, cellHeight );
                frame.Pivot = InSlicing.Pivot;
                frames.push_back( frame );
            }
        }
        return frames;
    }


    std::vector<SpriteFrame> SpriteSheet::SliceAuto( const uint8_t* InRgba, int InWidth, int InHeight, const SpriteSlicing& InSlicing, const std::string& InBaseName )
    {
        std::vector<SpriteFrame> frames;
        if( !InRgba || InWidth <= 0 || InHeight <= 0 )
        {
            return frames;
        }
        // Islands of opaque texels (8-connected), flood-filled into bounding boxes.
        std::vector<uint8_t> visited( static_cast<size_t>( InWidth ) * InHeight, 0 );
        std::vector<glm::ivec4> boxes;   // min x, min y, max x, max y
        std::vector<int> stack;
        for( int y = 0; y < InHeight; ++y )
        {
            for( int x = 0; x < InWidth; ++x )
            {
                const size_t index = static_cast<size_t>( y ) * InWidth + x;
                if( visited[index] || !Opaque( InRgba, InWidth, x, y, InSlicing.AlphaThreshold ) )
                {
                    continue;
                }
                glm::ivec4 box( x, y, x, y );
                visited[index] = 1;
                stack.assign( 1, static_cast<int>( index ) );
                while( !stack.empty() )
                {
                    const int at = stack.back();
                    stack.pop_back();
                    const int cx = at % InWidth;
                    const int cy = at / InWidth;
                    box = glm::ivec4( std::min( box.x, cx ), std::min( box.y, cy ), std::max( box.z, cx ), std::max( box.w, cy ) );
                    for( int dy = -1; dy <= 1; ++dy )
                    {
                        for( int dx = -1; dx <= 1; ++dx )
                        {
                            const int nx = cx + dx;
                            const int ny = cy + dy;
                            if( nx < 0 || ny < 0 || nx >= InWidth || ny >= InHeight )
                            {
                                continue;
                            }
                            const size_t neighbour = static_cast<size_t>( ny ) * InWidth + nx;
                            if( !visited[neighbour] && Opaque( InRgba, InWidth, nx, ny, InSlicing.AlphaThreshold ) )
                            {
                                visited[neighbour] = 1;
                                stack.push_back( static_cast<int>( neighbour ) );
                            }
                        }
                    }
                }
                boxes.push_back( box );
            }
        }
        // Reading order: by row band (boxes overlapping vertically share a row), then left to right.
        std::sort( boxes.begin(), boxes.end(), []( const glm::ivec4& a, const glm::ivec4& b ) {
            const bool sameRow = a.y <= b.w && b.y <= a.w;
            return sameRow ? a.x < b.x : a.y < b.y;
        } );
        for( const glm::ivec4& box : boxes )
        {
            SpriteFrame frame;
            frame.Name = InBaseName + "_" + std::to_string( frames.size() );
            frame.Rect = glm::vec4( box.x, box.y, box.z - box.x + 1, box.w - box.y + 1 );
            frame.Pivot = InSlicing.Pivot;
            frames.push_back( frame );
        }
        return frames;
    }


    bool SpriteSheet::DecodeImage( const std::string& InPath, std::vector<uint8_t>& OutRgba, int& OutWidth, int& OutHeight )
    {
        std::vector<uint8_t> bytes;
        if( !VFS::Get().ReadBytes( Path( InPath ), bytes ) )
        {
            return false;
        }
        bx::DefaultAllocator allocator;
        bimg::ImageContainer* image = bimg::imageParse( &allocator, bytes.data(), static_cast<uint32_t>( bytes.size() ), bimg::TextureFormat::RGBA8 );
        if( !image )
        {
            return false;
        }
        OutWidth = static_cast<int>( image->m_width );
        OutHeight = static_cast<int>( image->m_height );
        const uint8_t* data = static_cast<const uint8_t*>( image->m_data );
        OutRgba.assign( data, data + static_cast<size_t>( OutWidth ) * OutHeight * 4 );
        bimg::imageFree( image );
        return true;
    }


    bool SpriteSheet::Slice()
    {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
        if( Texture.empty() || !DecodeImage( Texture, rgba, width, height ) )
        {
            return false;
        }
        std::string base = Texture.substr( Texture.find_last_of( "/\\" ) + 1 );
        base = base.substr( 0, base.find_last_of( '.' ) );
        std::vector<SpriteFrame> frames = Slicing.Mode == SpriteSliceMode::Auto ? SliceAuto( rgba.data(), width, height, Slicing, base ) : SliceGrid( rgba.data(), width, height, Slicing, base );
        // Hand-set pivots and borders survive a re-slice under the same name.
        for( SpriteFrame& frame : frames )
        {
            if( const SpriteFrame* old = Find( frame.Name ) )
            {
                frame.Pivot = old->Pivot;
                frame.Border = old->Border;
            }
        }
        Sprites = std::move( frames );
        return true;
    }
}


#if USING( ME_EDITOR )
void SpriteSheetMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();
    // The sheet as edited here; it's written back on every change (the watcher reloads users).
    static std::map<std::string, Moonlight::SpriteSheet*> s_sheets;
    Moonlight::SpriteSheet*& sheet = s_sheets[FilePath.GetLocalPathString()];
    if( !sheet )
    {
        sheet = new Moonlight::SpriteSheet( FilePath );
        sheet->Load();
    }
    bool changed = false;
    char texture[256] = {};
    std::strncpy( texture, sheet->Texture.c_str(), sizeof( texture ) - 1 );
    HavanaUtils::Label( "Texture" );
    if( ImGui::InputText( "##SheetTexture", texture, sizeof( texture ), ImGuiInputTextFlags_EnterReturnsTrue ) )
    {
        sheet->Texture = texture;
        changed = true;
    }
    ImGui::SetItemTooltip( "The image to cut, as a project path (Assets/...); Enter applies" );

    Moonlight::SpriteSlicing& slicing = sheet->Slicing;
    HavanaUtils::Label( "Slice Mode" );
    int mode = static_cast<int>( slicing.Mode );
    const char* modes[] = { "Grid", "Auto" };
    if( ImGui::Combo( "##SliceMode", &mode, modes, 2 ) )
    {
        slicing.Mode = static_cast<Moonlight::SpriteSliceMode>( mode );
        changed = true;
    }
    if( slicing.Mode == Moonlight::SpriteSliceMode::Grid )
    {
        HavanaUtils::Label( "Cell Size" );
        changed |= ImGui::InputInt2( "##CellSize", &slicing.CellWidth );
        HavanaUtils::Label( "Offset" );
        changed |= ImGui::InputInt2( "##Offset", &slicing.OffsetX );
        HavanaUtils::Label( "Padding" );
        changed |= ImGui::InputInt2( "##Padding", &slicing.PaddingX );
        HavanaUtils::Label( "Skip Empty" );
        changed |= ImGui::Checkbox( "##SkipEmpty", &slicing.SkipEmpty );
    }
    HavanaUtils::Label( "Alpha Threshold" );
    changed |= ImGui::SliderInt( "##AlphaThreshold", &slicing.AlphaThreshold, 1, 255 );
    HavanaUtils::Label( "Default Pivot" );
    changed |= ImGui::DragFloat2( "##DefaultPivot", &slicing.Pivot.x, 0.01f, 0.f, 1.f );
    if( ImGui::Button( "Slice" ) )
    {
        if( sheet->Slice() )
        {
            changed = true;
            CLog::Log( CLog::LogType::Info, "Sprite sheet: " + std::to_string( sheet->Sprites.size() ) + " sprite(s) from " + sheet->Texture );
        }
        else
        {
            CLog::Log( CLog::LogType::Warning, "Sprite sheet: can't read " + sheet->Texture );
        }
    }
    ImGui::SeparatorText( ( std::to_string( sheet->Sprites.size() ) + " sprite(s)" ).c_str() );
    for( size_t i = 0; i < sheet->Sprites.size(); ++i )
    {
        Moonlight::SpriteFrame& frame = sheet->Sprites[i];
        ImGui::PushID( static_cast<int>( i ) );
        if( ImGui::TreeNode( frame.Name.c_str() ) )
        {
            char name[128] = {};
            std::strncpy( name, frame.Name.c_str(), sizeof( name ) - 1 );
            if( ImGui::InputText( "Name", name, sizeof( name ), ImGuiInputTextFlags_EnterReturnsTrue ) )
            {
                frame.Name = name;
                changed = true;
            }
            changed |= ImGui::DragFloat4( "Rect", &frame.Rect.x, 1.f, 0.f, 16384.f, "%.0f" );
            changed |= ImGui::DragFloat2( "Pivot", &frame.Pivot.x, 0.01f, 0.f, 1.f );
            changed |= ImGui::DragFloat4( "Border", &frame.Border.x, 1.f, 0.f, 4096.f, "%.0f" );
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if( changed )
    {
        File file{ FilePath };
        file.Write( sheet->Write() );
    }
}
#endif
