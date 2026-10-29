#include "RenderTextureAutomation.h"
#if USING( ME_EDITOR )
#include "Components/Camera.h"
#include "Editor/EditorOperations.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Graphics/Texture.h"
#include "Procedural/Tree/TreeTextures.h"
#include "RenderViews.h"
#include "Resource/ResourceCache.h"
#include "Utils/BGFXUtils.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <sstream>
#include <vector>

namespace RenderTextureAutomation
{
    namespace
    {
        // A channel above this (of 255) makes a pixel lit; changes need a bigger step than that.
        constexpr int kLevel = 24;
        constexpr int kDefaultTimeoutFrames = 600;

        struct Image
        {
            uint16_t Width = 0;
            uint16_t Height = 0;
            std::vector<uint8_t> Pixels;    // BGRA8, rows top to bottom
        };

        // One GPU copy at a time: the line that asked for it retries until it lands.
        struct Readback
        {
            std::string Line;
            bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;
            uint16_t Width = 0;
            uint16_t Height = 0;
            std::vector<uint8_t> Pixels;
            uint32_t ReadyFrame = 0;
        };
        Readback s_readback;

        struct Mark
        {
            uint32_t Renders = 0;
            Image Pixels;
        };
        std::map<std::string, Mark> s_marks;

        // A line that keeps running over frames (waits): what it started from. Frames are the
        // renderer's (a retried line runs every other frame).
        std::string s_activeLine;
        uint32_t s_activeFrame = 0;
        uint32_t s_activeStart = 0;


        uint32_t CurrentFrame()
        {
            return GetEngine().m_frameRenderSettings.m_currentFrame;
        }


        std::vector<std::string> Split( const std::string& InArgs )
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while( true )
            {
                const size_t bar = InArgs.find( '|', start );
                std::string part = InArgs.substr( start, bar == std::string::npos ? std::string::npos : bar - start );
                const size_t first = part.find_first_not_of( " \t" ), last = part.find_last_not_of( " \t" );
                parts.push_back( first == std::string::npos ? std::string() : part.substr( first, last - first + 1 ) );
                if( bar == std::string::npos ) break;
                start = bar + 1;
            }
            return parts;
        }


        // "x y w h" (normalized, from the top left).
        bool ParseRect( const std::string& InText, float OutRect[4] )
        {
            std::istringstream stream( InText );
            return static_cast<bool>( stream >> OutRect[0] >> OutRect[1] >> OutRect[2] >> OutRect[3] );
        }


        // <t>: a .rendertexture path, or @Entity for that camera's target. Retries while it loads.
        SharedPtr<Moonlight::Texture> FindTexture( const std::string& InName, std::string& OutKey, std::string& OutError, bool& OutRetry )
        {
            std::string path = InName;
            if( !InName.empty() && InName[0] == '@' )
            {
                EntityHandle entity = EditorOps::GetWorld().FindEntityByName( InName.substr( 1 ) );
                Camera* camera = entity ? entity->TryGetComponent<Camera>() : nullptr;
                if( !camera )
                {
                    OutError = "no camera named " + InName.substr( 1 );
                    return nullptr;
                }
                path = camera->TargetTexture.GetLocalPathString();
                if( path.empty() )
                {
                    OutError = InName.substr( 1 ) + " has no target texture";
                    return nullptr;
                }
            }
            const Path resolved( path );
            if( !resolved.Exists )
            {
                OutError = path + " doesn't exist";
                return nullptr;
            }
            // The cached texture the cameras and materials share (loaded now if nothing has yet).
            SharedPtr<Moonlight::Texture> texture = ResourceCache::GetInstance().Get<Moonlight::Texture>( resolved );
            if( texture && texture->IsLoading() )
            {
                OutRetry = true;
                return nullptr;
            }
            if( !texture || !texture->IsRenderTarget() || !bgfx::isValid( texture->TexHandle ) )
            {
                OutError = path + " is not a render texture";
                return nullptr;
            }
            OutKey = resolved.GetLocalPathString();
            return texture;
        }


        // Copies the texture's image (mip 0) back from the GPU for InLine: false until it has landed.
        bool ReadImage( const std::string& InLine, const Moonlight::Texture& InTexture, Image& OutImage, std::string& OutError )
        {
            const uint32_t frame = GetEngine().m_frameRenderSettings.m_currentFrame;
            if( s_readback.Line == InLine && s_readback.ReadyFrame != 0 )
            {
                if( frame < s_readback.ReadyFrame )
                {
                    return false;
                }
                OutImage.Width = s_readback.Width;
                OutImage.Height = s_readback.Height;
                OutImage.Pixels = s_readback.Pixels;
                s_readback.Line.clear();
                s_readback.ReadyFrame = 0;
                return true;
            }
            const uint16_t width = static_cast<uint16_t>( InTexture.mWidth );
            const uint16_t height = static_cast<uint16_t>( InTexture.mHeight );
            if( !bgfx::isValid( s_readback.Texture ) || s_readback.Width != width || s_readback.Height != height )
            {
                if( bgfx::isValid( s_readback.Texture ) && Moonlight::IsGpuAlive() )
                {
                    bgfx::destroy( s_readback.Texture );
                }
                s_readback.Texture = bgfx::createTexture2D( width, height, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK );
                s_readback.Width = width;
                s_readback.Height = height;
            }
            s_readback.Pixels.assign( static_cast<size_t>( width ) * height * 4, 0 );
            bgfx::TextureRegion destination, source;
            destination.init( s_readback.Texture );
            source.init( InTexture.TexHandle );
            // The capture view runs after every camera's views: this frame's image.
            bgfx::blit( Moonlight::RenderView::EditorCapture, destination, source );
            s_readback.ReadyFrame = bgfx::read( destination, s_readback.Pixels.data() );
            s_readback.Line = InLine;
            return false;
        }


        bool IsLit( const uint8_t* InPixel )
        {
            return InPixel[0] > kLevel || InPixel[1] > kLevel || InPixel[2] > kLevel;
        }


        // Lit pixels inside the normalized rect, as a fraction of its pixels.
        float LitFraction( const Image& InImage, const float InRect[4] )
        {
            const int width = InImage.Width, height = InImage.Height;
            const int x0 = std::clamp( static_cast<int>( std::floor( InRect[0] * width ) ), 0, width - 1 );
            const int y0 = std::clamp( static_cast<int>( std::floor( InRect[1] * height ) ), 0, height - 1 );
            const int x1 = std::clamp( static_cast<int>( std::ceil( ( InRect[0] + InRect[2] ) * width ) ), x0 + 1, width );
            const int y1 = std::clamp( static_cast<int>( std::ceil( ( InRect[1] + InRect[3] ) * height ) ), y0 + 1, height );
            size_t lit = 0;
            for( int y = y0; y < y1; ++y )
            {
                for( int x = x0; x < x1; ++x )
                {
                    lit += IsLit( &InImage.Pixels[( static_cast<size_t>( y ) * width + x ) * 4] ) ? 1 : 0;
                }
            }
            return static_cast<float>( lit ) / static_cast<float>( ( x1 - x0 ) * ( y1 - y0 ) );
        }


        // Pixels with any channel more than kLevel apart, as a fraction (1 when the sizes differ).
        float ChangedFraction( const Image& InA, const Image& InB )
        {
            if( InA.Width != InB.Width || InA.Height != InB.Height || InA.Pixels.empty() )
            {
                return 1.f;
            }
            size_t changed = 0;
            for( size_t i = 0; i + 3 < InA.Pixels.size(); i += 4 )
            {
                for( size_t c = 0; c < 3; ++c )
                {
                    if( std::abs( static_cast<int>( InA.Pixels[i + c] ) - static_cast<int>( InB.Pixels[i + c] ) ) > kLevel )
                    {
                        ++changed;
                        break;
                    }
                }
            }
            return static_cast<float>( changed ) / static_cast<float>( InA.Pixels.size() / 4 );
        }


        // Keeps a multi-frame line's state while it retries; a new line starts over.
        void Begin( const std::string& InLine, uint32_t InStart )
        {
            if( s_activeLine != InLine )
            {
                s_activeLine = InLine;
                s_activeFrame = CurrentFrame();
                s_activeStart = InStart;
            }
        }


        // Frames since the line began.
        int ActiveFrames()
        {
            return static_cast<int>( CurrentFrame() - s_activeFrame );
        }


        void End()
        {
            s_activeLine.clear();
        }


        std::string Percent( float InFraction )
        {
            char text[32];
            std::snprintf( text, sizeof( text ), "%.1f%%", InFraction * 100.f );
            return text;
        }
    }


    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry )
    {
        OutRetry = false;
        const std::vector<std::string> args = Split( InArgs );
        const std::string line = InCommand + " " + InArgs;
        std::string key;
        bool loading = false;
        SharedPtr<Moonlight::Texture> texture = FindTexture( args[0], key, OutError, loading );
        if( loading )
        {
            Begin( line, 0 );
            if( ActiveFrames() > kDefaultTimeoutFrames )
            {
                End();
                OutError = args[0] + " is still loading";
                return false;
            }
            OutRetry = true;
            return true;
        }
        if( !texture )
        {
            End();
            return false;
        }

        if( InCommand == "rtt-assert-size" )
        {
            if( args.size() < 3 )
            {
                OutError = "usage: rtt-assert-size <t> | <width> | <height>";
                return false;
            }
            const int width = std::atoi( args[1].c_str() ), height = std::atoi( args[2].c_str() );
            const int timeout = args.size() > 3 ? std::atoi( args[3].c_str() ) : 0;
            Begin( line, 0 );
            if( texture->mWidth != width || texture->mHeight != height )
            {
                if( ActiveFrames() < timeout )
                {
                    OutRetry = true;
                    return true;
                }
                End();
                OutError = key + " is " + std::to_string( texture->mWidth ) + " x " + std::to_string( texture->mHeight ) + ", not " + args[1] + " x " + args[2];
                return false;
            }
            End();
            return true;
        }
        if( InCommand == "rtt-mark" )
        {
            Mark& mark = s_marks[key];
            Image image;
            if( !ReadImage( line, *texture, image, OutError ) )
            {
                OutRetry = OutError.empty();
                return OutError.empty();
            }
            mark.Renders = texture->GetRenderCount();
            mark.Pixels = std::move( image );
            return true;
        }
        if( InCommand == "rtt-wait" )
        {
            const auto found = s_marks.find( key );
            const uint32_t base = found != s_marks.end() ? found->second.Renders : 0;
            const uint32_t renders = args.size() > 1 && !args[1].empty() ? static_cast<uint32_t>( std::max( 1, std::atoi( args[1].c_str() ) ) ) : 1;
            const int timeout = args.size() > 2 ? std::atoi( args[2].c_str() ) : kDefaultTimeoutFrames;
            Begin( line, 0 );
            if( texture->GetRenderCount() >= base + renders )
            {
                End();
                return true;
            }
            if( ActiveFrames() > timeout )
            {
                End();
                OutError = key + ": " + std::to_string( texture->GetRenderCount() - std::min( base, texture->GetRenderCount() ) ) + " render(s) in " + std::to_string( timeout ) + " frames, waited for " + std::to_string( renders );
                return false;
            }
            OutRetry = true;
            return true;
        }
        if( InCommand == "rtt-assert-renders" )
        {
            if( args.size() < 4 )
            {
                OutError = "usage: rtt-assert-renders <t> | <frames> | <min> | <max>";
                return false;
            }
            const int frames = std::max( 1, std::atoi( args[1].c_str() ) );
            const uint32_t minimum = static_cast<uint32_t>( std::atoi( args[2].c_str() ) );
            const uint32_t maximum = static_cast<uint32_t>( std::atoi( args[3].c_str() ) );
            Begin( line, texture->GetRenderCount() );
            if( ActiveFrames() < frames )
            {
                OutRetry = true;
                return true;
            }
            const uint32_t renders = texture->GetRenderCount() - s_activeStart;
            End();
            if( renders < minimum || renders > maximum )
            {
                OutError = key + " rendered " + std::to_string( renders ) + " times in " + std::to_string( frames ) + " frames, expected " + args[2] + " to " + args[3];
                return false;
            }
            return true;
        }
        if( InCommand == "rtt-assert-changed" )
        {
            const auto found = s_marks.find( key );
            if( found == s_marks.end() )
            {
                OutError = "rtt-mark " + key + " first";
                return false;
            }
            const float minimum = args.size() > 1 && !args[1].empty() ? static_cast<float>( std::atof( args[1].c_str() ) ) : 0.01f;
            // A render since the mark first, then the new image.
            Begin( line, 0 );
            if( texture->GetRenderCount() <= found->second.Renders && s_readback.Line != line )
            {
                if( ActiveFrames() > kDefaultTimeoutFrames )
                {
                    End();
                    OutError = key + " didn't render again since the mark";
                    return false;
                }
                OutRetry = true;
                return true;
            }
            Image image;
            if( !ReadImage( line, *texture, image, OutError ) )
            {
                OutRetry = OutError.empty();
                return OutError.empty();
            }
            End();
            const float changed = ChangedFraction( image, found->second.Pixels );
            if( changed < minimum )
            {
                OutError = key + ": " + Percent( changed ) + " of pixels changed since the mark, expected at least " + Percent( minimum );
                return false;
            }
            return true;
        }
        if( InCommand == "rtt-assert-lit" || InCommand == "rtt-assert-dark" )
        {
            const bool lit = InCommand == "rtt-assert-lit";
            float rect[4] = { 0.f, 0.f, 1.f, 1.f };
            if( args.size() < 2 || ( !lit && args.size() < 3 ) || ( args.size() > 2 && !ParseRect( args[2], rect ) ) )
            {
                OutError = lit ? "usage: rtt-assert-lit <t> | <fraction> [| x y w h]" : "usage: rtt-assert-dark <t> | <fraction> | x y w h";
                return false;
            }
            Image image;
            if( !ReadImage( line, *texture, image, OutError ) )
            {
                OutRetry = OutError.empty();
                return OutError.empty();
            }
            const float fraction = LitFraction( image, rect );
            const float limit = static_cast<float>( std::atof( args[1].c_str() ) );
            if( lit ? fraction < limit : fraction > limit )
            {
                OutError = key + ": " + Percent( fraction ) + " of the pixels are lit, expected " + ( lit ? "at least " : "at most " ) + Percent( limit );
                return false;
            }
            return true;
        }
        if( InCommand == "rtt-capture" )
        {
            if( args.size() < 2 || args[1].empty() )
            {
                OutError = "usage: rtt-capture <t> | <path.png>";
                return false;
            }
            Image image;
            if( !ReadImage( line, *texture, image, OutError ) )
            {
                OutRetry = OutError.empty();
                return OutError.empty();
            }
            MitchTree::Image png;
            png.Width = image.Width;
            png.Height = image.Height;
            png.Pixels = std::move( image.Pixels );
            for( size_t i = 0; i + 3 < png.Pixels.size(); i += 4 )
            {
                std::swap( png.Pixels[i], png.Pixels[i + 2] );   // BGRA to RGBA
                png.Pixels[i + 3] = 255;
            }
            std::error_code error;
            const std::filesystem::path target( args[1] );
            if( target.has_parent_path() )
            {
                std::filesystem::create_directories( target.parent_path(), error );
            }
            return MitchTree::WritePng( png, args[1], OutError );
        }
        OutError = "unknown command";
        return false;
    }
}
#endif
