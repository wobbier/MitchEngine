#include "SpriteAtlas.h"
#include "File.h"
#include "JSON.h"
#include "Path.h"
#include "VFS/VFS.h"
#include <algorithm>
#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/file.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>


namespace Moonlight
{
    bool SpriteAtlasDesc::Parse( const std::string& InText )
    {
        const json root = json::parse( InText, nullptr, false );
        if( root.is_discarded() || !root.is_object() )
        {
            return false;
        }
        Sources.clear();
        if( auto sources = root.find( "Sources" ); sources != root.end() && sources->is_array() )
        {
            for( const json& source : *sources )
            {
                if( source.is_string() )
                {
                    Sources.push_back( source.get<std::string>() );
                }
            }
        }
        Padding = std::clamp( root.value( "Padding", 2 ), 0, 64 );
        MaxSize = std::clamp( root.value( "MaxSize", 2048 ), 16, 16384 );
        Extrude = std::clamp( root.value( "Extrude", 1 ), 0, 16 );
        return true;
    }


    std::string SpriteAtlasDesc::Write() const
    {
        json root = json::object();
        root["Sources"] = Sources;
        root["Padding"] = Padding;
        root["MaxSize"] = MaxSize;
        root["Extrude"] = Extrude;
        return root.dump( 4 );
    }


    bool PackMaxRects( std::vector<PackRect>& InOutRects, int InMaxSize, int& OutWidth, int& OutHeight )
    {
        struct Free
        {
            int X, Y, W, H;
        };
        std::vector<size_t> order( InOutRects.size() );
        std::iota( order.begin(), order.end(), size_t( 0 ) );
        std::stable_sort( order.begin(), order.end(), [&]( size_t InA, size_t InB ) {
            const PackRect& a = InOutRects[InA];
            const PackRect& b = InOutRects[InB];
            return a.Height != b.Height ? a.Height > b.Height : a.Width > b.Width;
        } );
        std::vector<Free> free{ { 0, 0, InMaxSize, InMaxSize } };
        int usedWidth = 0;
        int usedHeight = 0;
        for( const size_t index : order )
        {
            PackRect& rect = InOutRects[index];
            if( rect.Width <= 0 || rect.Height <= 0 )
            {
                rect.X = rect.Y = 0;
                continue;
            }
            // Best short side fit; ties go to the lowest, then leftmost spot (packs into a corner).
            int best = -1;
            int bestShort = INT32_MAX;
            int bestLong = INT32_MAX;
            for( size_t i = 0; i < free.size(); ++i )
            {
                const Free& node = free[i];
                if( node.W < rect.Width || node.H < rect.Height )
                {
                    continue;
                }
                const int leftoverX = node.W - rect.Width;
                const int leftoverY = node.H - rect.Height;
                const int shortSide = std::min( leftoverX, leftoverY );
                const int longSide = std::max( leftoverX, leftoverY );
                const bool better = shortSide < bestShort || ( shortSide == bestShort && longSide < bestLong )
                    || ( shortSide == bestShort && longSide == bestLong && best >= 0
                        && ( node.Y < free[best].Y || ( node.Y == free[best].Y && node.X < free[best].X ) ) );
                if( better )
                {
                    best = static_cast<int>( i );
                    bestShort = shortSide;
                    bestLong = longSide;
                }
            }
            if( best < 0 )
            {
                return false;
            }
            rect.X = free[best].X;
            rect.Y = free[best].Y;
            usedWidth = std::max( usedWidth, rect.X + rect.Width );
            usedHeight = std::max( usedHeight, rect.Y + rect.Height );

            // Split every free rectangle the placed one overlaps.
            std::vector<Free> next;
            for( const Free& node : free )
            {
                const bool overlaps = rect.X < node.X + node.W && rect.X + rect.Width > node.X && rect.Y < node.Y + node.H && rect.Y + rect.Height > node.Y;
                if( !overlaps )
                {
                    next.push_back( node );
                    continue;
                }
                if( rect.X > node.X ) next.push_back( { node.X, node.Y, rect.X - node.X, node.H } );
                if( rect.X + rect.Width < node.X + node.W ) next.push_back( { rect.X + rect.Width, node.Y, node.X + node.W - rect.X - rect.Width, node.H } );
                if( rect.Y > node.Y ) next.push_back( { node.X, node.Y, node.W, rect.Y - node.Y } );
                if( rect.Y + rect.Height < node.Y + node.H ) next.push_back( { node.X, rect.Y + rect.Height, node.W, node.Y + node.H - rect.Y - rect.Height } );
            }
            // Drop free rectangles contained in another.
            free.clear();
            for( size_t i = 0; i < next.size(); ++i )
            {
                bool contained = false;
                for( size_t j = 0; j < next.size() && !contained; ++j )
                {
                    if( i == j ) continue;
                    const Free& a = next[i];
                    const Free& b = next[j];
                    const bool inside = a.X >= b.X && a.Y >= b.Y && a.X + a.W <= b.X + b.W && a.Y + a.H <= b.Y + b.H;
                    // Of two identical ones, keep the first.
                    contained = inside && !( a.X == b.X && a.Y == b.Y && a.W == b.W && a.H == b.H && i < j );
                }
                if( !contained )
                {
                    free.push_back( next[i] );
                }
            }
        }
        auto pow2 = []( int InValue ) {
            int size = 1;
            while( size < InValue ) size *= 2;
            return size;
        };
        OutWidth = std::max( pow2( usedWidth ), 1 );
        OutHeight = std::max( pow2( usedHeight ), 1 );
        return true;
    }


    bool ComposeSpriteAtlas( const std::vector<AtlasInput>& InInputs, const SpriteAtlasDesc& InDesc, SpriteAtlasImage& OutImage, std::string& OutError )
    {
        // Each sprite's cell: its pixels, the extruded edge around them, and the padding.
        const int margin = InDesc.Extrude + InDesc.Padding;
        std::vector<PackRect> rects( InInputs.size() );
        std::vector<glm::ivec4> sources( InInputs.size() );
        for( size_t i = 0; i < InInputs.size(); ++i )
        {
            const AtlasInput& input = InInputs[i];
            glm::ivec4 source = input.Rect.z > 0.f && input.Rect.w > 0.f ? glm::ivec4( input.Rect ) : glm::ivec4( 0, 0, input.ImageWidth, input.ImageHeight );
            source.x = std::clamp( source.x, 0, std::max( input.ImageWidth - 1, 0 ) );
            source.y = std::clamp( source.y, 0, std::max( input.ImageHeight - 1, 0 ) );
            source.z = std::clamp( source.z, 0, input.ImageWidth - source.x );
            source.w = std::clamp( source.w, 0, input.ImageHeight - source.y );
            sources[i] = source;
            rects[i].Width = source.z + margin * 2;
            rects[i].Height = source.w + margin * 2;
        }
        int width = 0;
        int height = 0;
        if( !PackMaxRects( rects, InDesc.MaxSize, width, height ) )
        {
            OutError = "the sprites don't fit a " + std::to_string( InDesc.MaxSize ) + " x " + std::to_string( InDesc.MaxSize ) + " atlas";
            return false;
        }
        OutImage.Width = width;
        OutImage.Height = height;
        OutImage.Rgba.assign( size_t( width ) * height * 4, 0 );
        OutImage.Frames.clear();
        for( size_t i = 0; i < InInputs.size(); ++i )
        {
            const AtlasInput& input = InInputs[i];
            const glm::ivec4 source = sources[i];
            const int x0 = rects[i].X + margin;
            const int y0 = rects[i].Y + margin;
            // The sprite, then its edge texels repeated outwards by Extrude.
            for( int y = -InDesc.Extrude; y < source.w + InDesc.Extrude; ++y )
            {
                for( int x = -InDesc.Extrude; x < source.z + InDesc.Extrude; ++x )
                {
                    if( source.z <= 0 || source.w <= 0 || !input.Rgba )
                    {
                        continue;
                    }
                    const int sx = source.x + std::clamp( x, 0, source.z - 1 );
                    const int sy = source.y + std::clamp( y, 0, source.w - 1 );
                    const uint8_t* from = input.Rgba + ( size_t( sy ) * input.ImageWidth + sx ) * 4;
                    uint8_t* to = OutImage.Rgba.data() + ( size_t( y0 + y ) * width + ( x0 + x ) ) * 4;
                    std::copy( from, from + 4, to );
                }
            }
            SpriteFrame frame;
            frame.Name = input.Name;
            frame.Rect = glm::vec4( float( x0 ), float( y0 ), float( source.z ), float( source.w ) );
            frame.Pivot = input.Pivot;
            frame.Border = input.Border;
            OutImage.Frames.push_back( frame );
        }
        return true;
    }


    namespace
    {
        bool IsImage( const std::string& InPath )
        {
            std::string extension = std::filesystem::path( InPath ).extension().string();
            std::transform( extension.begin(), extension.end(), extension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return extension == ".png" || extension == ".tga" || extension == ".jpg" || extension == ".jpeg";
        }
    }


    bool BuildSpriteAtlas( const std::string& InAtlasPath, SpriteAtlasImage& OutImage, std::vector<AtlasInput>& OutInputs, std::string& OutError )
    {
        std::string text;
        SpriteAtlasDesc desc;
        if( !VFS::Get().ReadText( Path( InAtlasPath ), text ) || !desc.Parse( text ) )
        {
            OutError = "can't read " + InAtlasPath;
            return false;
        }
        // Decoded images, shared by the sprites cut from them.
        std::map<std::string, std::vector<uint8_t>> images;
        std::map<std::string, glm::ivec2> sizes;
        auto image = [&]( const std::string& InPath ) -> bool {
            if( images.count( InPath ) )
            {
                return true;
            }
            std::vector<uint8_t> rgba;
            int width = 0;
            int height = 0;
            if( !SpriteSheet::DecodeImage( InPath, rgba, width, height ) )
            {
                return false;
            }
            images[InPath] = std::move( rgba );
            sizes[InPath] = glm::ivec2( width, height );
            return true;
        };
        std::vector<std::string> names;
        auto addImage = [&]( const std::string& InPath ) {
            if( !image( InPath ) )
            {
                OutError = "can't decode " + InPath;
                return false;
            }
            AtlasInput input;
            input.Name = std::filesystem::path( InPath ).stem().string();
            input.SourceTexture = InPath;
            OutInputs.push_back( input );
            return true;
        };
        OutInputs.clear();
        for( const std::string& source : desc.Sources )
        {
            const std::string extension = std::filesystem::path( source ).extension().string();
            if( extension == std::string( "." ) + SpriteSheet::kExtension )
            {
                std::string sheetText;
                SpriteSheet sheet{ Path( source ) };
                if( !VFS::Get().ReadText( Path( source ), sheetText ) || !sheet.Parse( sheetText ) || !image( sheet.Texture ) )
                {
                    OutError = "can't read the sprite sheet " + source;
                    return false;
                }
                const std::string sheetName = std::filesystem::path( source ).stem().string();
                for( const SpriteFrame& frame : sheet.Sprites )
                {
                    AtlasInput input;
                    input.Name = sheetName + "/" + frame.Name;
                    input.SourceTexture = sheet.Texture;
                    input.SourceSheet = source;
                    input.SourceSprite = frame.Name;
                    input.Rect = frame.Rect;
                    input.Pivot = frame.Pivot;
                    input.Border = frame.Border;
                    OutInputs.push_back( input );
                }
            }
            else if( IsImage( source ) )
            {
                if( !addImage( source ) )
                {
                    return false;
                }
            }
            else
            {
                // A folder: its images, sorted so the atlas doesn't depend on listing order.
                std::vector<std::string> files;
                std::string folder = source;
                while( !folder.empty() && folder.back() == '/' )
                {
                    folder.pop_back();
                }
                VFS::Get().Enumerate( folder, false, [&]( const std::string& InPath, bool InIsDirectory ) {
                    if( !InIsDirectory && IsImage( InPath ) )
                    {
                        files.push_back( InPath );
                    }
                } );
                std::sort( files.begin(), files.end() );
                files.erase( std::unique( files.begin(), files.end() ), files.end() );
                for( const std::string& file : files )
                {
                    if( !addImage( file ) )
                    {
                        return false;
                    }
                }
            }
        }
        for( AtlasInput& input : OutInputs )
        {
            input.Rgba = images[input.SourceTexture].data();
            input.ImageWidth = sizes[input.SourceTexture].x;
            input.ImageHeight = sizes[input.SourceTexture].y;
        }
        const bool composed = ComposeSpriteAtlas( OutInputs, desc, OutImage, OutError );
        for( AtlasInput& input : OutInputs )
        {
            input.Rgba = nullptr;
        }
        return composed;
    }


    bool WriteSpriteAtlas( const SpriteAtlasImage& InImage, const std::string& InBase, std::string& OutError )
    {
        const std::string pngPath = Path( InBase + ".png" ).FullPath;
        std::error_code ec;
        std::filesystem::create_directories( std::filesystem::path( pngPath ).parent_path(), ec );
        bx::FileWriter writer;
        bx::Error error;
        if( !bx::open( &writer, pngPath.c_str(), false, &error ) )
        {
            OutError = "can't write " + pngPath;
            return false;
        }
        bimg::imageWritePng( &writer, static_cast<uint32_t>( InImage.Width ), static_cast<uint32_t>( InImage.Height ), static_cast<uint32_t>( InImage.Width * 4 ),
            InImage.Rgba.data(), bimg::TextureFormat::RGBA8, false, &error );
        bx::close( &writer );
        if( !error.isOk() )
        {
            OutError = "PNG encoding failed for " + pngPath;
            return false;
        }
        SpriteSheet sheet{ Path( InBase + ".spritesheet" ) };
        sheet.Texture = InBase + ".png";
        sheet.Sprites = InImage.Frames;
        const std::string sheetPath = Path( InBase + ".spritesheet" ).FullPath;
        std::ofstream out( sheetPath, std::ios::binary | std::ios::trunc );
        if( !out )
        {
            OutError = "can't write " + sheetPath;
            return false;
        }
        out << sheet.Write();
        return true;
    }
}


int Moonlight::PackSpriteAtlasFile( const std::string& InAtlasPath, std::string& OutError, SpriteAtlasImage* OutImage )
{
    SpriteAtlasImage image;
    std::vector<AtlasInput> inputs;
    if( !BuildSpriteAtlas( InAtlasPath, image, inputs, OutError ) || !WriteSpriteAtlas( image, InAtlasPath, OutError ) )
    {
        return -1;
    }
    const int count = static_cast<int>( image.Frames.size() );
    if( OutImage )
    {
        *OutImage = std::move( image );
    }
    return count;
}


#if USING( ME_EDITOR )
#include <Utils/HavanaUtils.h>
#include <Utils/ImGuiUtils.h>
#include <bgfx/bgfx.h>
#include <imgui.h>
#include <cstring>

void SpriteAtlasMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();
    struct Editing
    {
        Moonlight::SpriteAtlasDesc Desc;
        bool Loaded = false;
        // The last pack: its image on the GPU for the preview, and its sprites.
        bgfx::TextureHandle Preview = BGFX_INVALID_HANDLE;
        Moonlight::SpriteAtlasImage Image;
        std::string Status;
        char NewSource[256] = {};
    };
    static std::map<std::string, Editing> s_atlases;
    Editing& editing = s_atlases[FilePath.GetLocalPathString()];
    if( !editing.Loaded )
    {
        std::string text;
        editing.Loaded = true;
        if( VFS::Get().ReadText( FilePath, text ) )
        {
            editing.Desc.Parse( text );
        }
    }
    Moonlight::SpriteAtlasDesc& desc = editing.Desc;
    bool changed = false;

    ImGui::SeparatorText( ( std::to_string( desc.Sources.size() ) + " source(s)" ).c_str() );
    for( size_t i = 0; i < desc.Sources.size(); ++i )
    {
        ImGui::PushID( static_cast<int>( i ) );
        if( ImGui::SmallButton( "x" ) )
        {
            desc.Sources.erase( desc.Sources.begin() + static_cast<std::ptrdiff_t>( i ) );
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted( desc.Sources[i].c_str() );
        ImGui::PopID();
    }
    if( ImGui::InputTextWithHint( "##NewSource", "Add an image, folder or .spritesheet (Enter)", editing.NewSource, sizeof( editing.NewSource ), ImGuiInputTextFlags_EnterReturnsTrue ) && editing.NewSource[0] )
    {
        desc.Sources.push_back( editing.NewSource );
        editing.NewSource[0] = '\0';
        changed = true;
    }
    HavanaUtils::Label( "Padding" );
    changed |= ImGui::SliderInt( "##AtlasPadding", &desc.Padding, 0, 16 );
    HavanaUtils::Label( "Max Size" );
    changed |= ImGui::InputInt( "##AtlasMaxSize", &desc.MaxSize, 256, 1024 );
    HavanaUtils::Label( "Extrude" );
    changed |= ImGui::SliderInt( "##AtlasExtrude", &desc.Extrude, 0, 8 );
    ImGui::SetItemTooltip( "Edge texels repeated outwards, so filtering doesn't bleed neighbours in" );
    if( changed )
    {
        File file( FilePath );
        file.Write( desc.Write() );
    }

    // An earlier pack (here, by a cook or by atlas-pack) shows until the next one.
    const std::string base = FilePath.GetLocalPathString();
    bool packNow = ImGui::Button( "Pack" );
    ImGui::SetItemTooltip( "Writes <atlas>.png and <atlas>.spritesheet beside it, as a cook does" );
    bool loadExisting = false;
    if( !packNow && !bgfx::isValid( editing.Preview ) && editing.Status.empty() && Path( base + ".spritesheet" ).Exists && Path( base + ".png" ).Exists )
    {
        loadExisting = true;
    }
    if( packNow || loadExisting )
    {
        std::string error;
        Moonlight::SpriteAtlasImage image;
        int count = -1;
        if( packNow )
        {
            count = Moonlight::PackSpriteAtlasFile( base, error, &image );
        }
        else
        {
            std::string sheetText;
            Moonlight::SpriteSheet sheet{ Path( base + ".spritesheet" ) };
            if( Moonlight::SpriteSheet::DecodeImage( base + ".png", image.Rgba, image.Width, image.Height ) && VFS::Get().ReadText( Path( base + ".spritesheet" ), sheetText ) && sheet.Parse( sheetText ) )
            {
                image.Frames = sheet.Sprites;
                count = static_cast<int>( image.Frames.size() );
            }
            else
            {
                error = "can't read the last pack";
            }
        }
        if( count < 0 )
        {
            editing.Status = "Pack failed: " + error;
        }
        else
        {
            if( bgfx::isValid( editing.Preview ) )
            {
                bgfx::destroy( editing.Preview );
            }
            editing.Preview = bgfx::createTexture2D( static_cast<uint16_t>( image.Width ), static_cast<uint16_t>( image.Height ), false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy( image.Rgba.data(), static_cast<uint32_t>( image.Rgba.size() ) ) );
            editing.Image = std::move( image );
            double used = 0.0;
            for( const Moonlight::SpriteFrame& frame : editing.Image.Frames )
            {
                used += double( frame.Rect.z ) * frame.Rect.w;
            }
            const double fill = used / std::max( 1.0, double( editing.Image.Width ) * editing.Image.Height );
            char status[160];
            std::snprintf( status, sizeof( status ), "%d sprite(s) in %d x %d (%.0f%% filled)", count, editing.Image.Width, editing.Image.Height, fill * 100.0 );
            editing.Status = status;
        }
    }
    if( !editing.Status.empty() )
    {
        ImGui::SameLine();
        ImGui::TextUnformatted( editing.Status.c_str() );
    }
    if( bgfx::isValid( editing.Preview ) && editing.Image.Width > 0 )
    {
        const float width = std::max( ImGui::GetContentRegionAvail().x, 64.f );
        const float scale = width / static_cast<float>( editing.Image.Width );
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image( editing.Preview, ImVec2( width, editing.Image.Height * scale ) );
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for( const Moonlight::SpriteFrame& frame : editing.Image.Frames )
        {
            const ImVec2 min( origin.x + frame.Rect.x * scale, origin.y + frame.Rect.y * scale );
            const ImVec2 max( min.x + frame.Rect.z * scale, min.y + frame.Rect.w * scale );
            draw->AddRect( min, max, IM_COL32( 255, 200, 0, 200 ) );
            if( ImGui::IsMouseHoveringRect( min, max ) )
            {
                ImGui::SetTooltip( "%s (%.0f x %.0f)", frame.Name.c_str(), frame.Rect.z, frame.Rect.w );
            }
        }
    }
}
#endif
