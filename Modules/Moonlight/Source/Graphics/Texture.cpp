#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>
#include "VFS/VFS.h"
#include <vector>
#include <filesystem>
#include <assert.h>
#include "Texture.h"
#include "CLog.h"
#include "Utils/StringUtils.h"
#include <bimg/bimg.h>
#include "Utils/BGFXUtils.h"
#include <Utils/PlatformUtils.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <Utils/HavanaUtils.h>
#include <ImGuiTheme.h>
#include "File.h"
#include "Graphics/DefaultTextures.h"
#include "Procedural/Tree/TreeTextures.h"


namespace
{
#if USING( ME_PLATFORM_LINUX ) && USING( ME_TOOLS )
    std::filesystem::path FindTextureCompilerLauncher()
    {
        namespace fs = std::filesystem;
        std::error_code error;
        const fs::path executable = fs::read_symlink( "/proc/self/exe", error );
        const fs::path workingDirectory = fs::current_path( error );
        // HUB runs from Engine/Tools/HUB; executables normally live in .build/<config>.
        for( fs::path directory : { executable.parent_path(), workingDirectory } )
        {
            while( !directory.empty() )
            {
                for( const char* relative : { "Engine/Tools/RunTextureCompiler.sh", "Tools/RunTextureCompiler.sh" } )
                {
                    const fs::path candidate = directory / relative;
                    if( fs::is_regular_file( candidate, error ) )
                    {
                        return candidate;
                    }
                }
                const fs::path parent = directory.parent_path();
                if( parent == directory )
                {
                    break;
                }
                directory = parent;
            }
        }
        return {};
    }


    std::string ShellQuote( const std::string& text )
    {
        std::string quoted = "'";
        for( char c : text )
        {
            quoted += c == '\'' ? "'\\''" : std::string( 1, c );
        }
        return quoted + "'";
    }
#endif


    void ImageReleaseCb( void*, void* inUserData )
    {
        bimg::imageFree( static_cast<bimg::ImageContainer*>( inUserData ) );
    }


    // Thread safe (loader threads call it), unlike the shared bx reader.
    bool ReadFileBytes( const Path& inPath, std::vector<uint8_t>& outData )
    {
        if( !VFS::Get().ReadBytes( inPath, outData ) )
        {
            YIKES_FMT( "[%s]: can't open the file", inPath.GetLocalPathString().c_str() );
            return false;
        }
        return true;
    }
}


namespace Moonlight
{
    Texture::Texture( const Path& InFilePath, WrapMode mode )
        : Resource( InFilePath )
        , TexHandle( BGFX_INVALID_HANDLE )
        , m_wrapMode( mode )
    {
        //Load();
    }

    Texture::~Texture()
    {
        if( m_pendingImage )
        {
            bimg::imageFree( m_pendingImage );
        }
        DestroyRenderTarget();
        if( bgfx::isValid( TexHandle ) && IsGpuAlive() )
        {
            bgfx::destroy( TexHandle );
        }
        TexHandle = BGFX_INVALID_HANDLE;
    }

    bool Texture::Load()
    {
        return LoadAsync() && FinishAsyncLoad();
    }


    bool Texture::SupportsAsyncLoad() const
    {
        return true;
    }


    bool Texture::LoadAsync()
    {
        // Generated sub-assets ("<tree>.mtree#bark-color") are baked from their recipe, not read.
        const std::string local = FilePath.GetLocalPathString();
        const size_t tree = local.find( ".mtree#" );
        if( tree != std::string::npos )
        {
            std::string error;
            m_pendingImage = MitchTree::BakeChannel( local.substr( 0, tree + 6 ), local.substr( tree + 7 ), error );
            if( !m_pendingImage ) CLog::Log( CLog::LogType::Error, "Tree texture " + local + ": " + error );
            return m_pendingImage != nullptr;
        }
        // Render textures are their settings file, read as is (no compiled twin).
        if( RenderTextureSettings::IsRenderTexturePath( local ) )
        {
            std::string text;
            if( !VFS::Get().ReadText( FilePath, text ) || !m_pendingRenderSettings.Parse( text ) )
            {
                CLog::Log( CLog::LogType::Error, "Render texture " + local + ": can't read its settings" );
                return false;
            }
            m_hasPendingRenderSettings = true;
            return true;
        }
        Path compiledTexture( FilePath.FullPath + ".dds" );
        if( !compiledTexture.Exists )
        {
            return false;
        }

        std::vector<uint8_t> fileData;
        if( !ReadFileBytes( compiledTexture, fileData ) )
        {
            return false;
        }

        // imageParse copies what it needs, so fileData can go out of scope afterwards.
        m_pendingImage = bimg::imageParse( Moonlight::getDefaultAllocator(), fileData.data(), static_cast<uint32_t>( fileData.size() ) );
        return m_pendingImage != nullptr;
    }


    SharedPtr<Texture> Texture::CreateFromImage( const std::string& InName, bimg::ImageContainer* InImage, WrapMode InMode )
    {
        auto texture = MakeShared<Texture>( Path( InName ), InMode );
        texture->m_pendingImage = InImage;
        if( !texture->FinishAsyncLoad() )
        {
            return nullptr;
        }
        return texture;
    }


    bool Texture::FinishAsyncLoad()
    {
        if( m_hasPendingRenderSettings )
        {
            m_hasPendingRenderSettings = false;
            m_isRenderTarget = true;
            m_renderSettings = m_pendingRenderSettings;
            return CreateRenderTarget();
        }
        bimg::ImageContainer* imageContainer = m_pendingImage;
        m_pendingImage = nullptr;
        if( !imageContainer )
        {
            return false;
        }
        m_flags = BGFX_TEXTURE_NONE;
        if( m_wrapMode == WrapMode::Clamp || m_wrapMode == WrapMode::Decal )
            m_flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        else if( m_wrapMode == WrapMode::Mirror )
            m_flags |= BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;
        // A size cap starts the upload at the first mip that fits (single 2D images with mips).
        uint8_t skip = 0;
        if( s_maxUploadSize > 0 && imageContainer->m_numLayers == 1 && !imageContainer->m_cubeMap && imageContainer->m_depth <= 1 )
        {
            while( skip + 1 < imageContainer->m_numMips && std::max( imageContainer->m_width >> skip, imageContainer->m_height >> skip ) > s_maxUploadSize )
            {
                ++skip;
            }
        }
        bimg::ImageMip firstMip{};
        if( skip > 0 && !bimg::imageGetRawData( *imageContainer, 0, skip, imageContainer->m_data, imageContainer->m_size, firstMip ) )
        {
            skip = 0;
        }
        // The mip's own size (ImageMip reports block-aligned sizes).
        mWidth = static_cast<int>( std::max( imageContainer->m_width >> skip, 1u ) );
        mHeight = static_cast<int>( std::max( imageContainer->m_height >> skip, 1u ) );
        m_mips = static_cast<uint8_t>( imageContainer->m_numMips - skip );
        if( !IsGpuAlive() )
        {
            bimg::imageFree( imageContainer );
            return true;
        }

        if( imageContainer->m_cubeMap )
        {
            YIKES( "You gotta implement cubemap textures" );
        }
        else if( 1 < imageContainer->m_depth )
        {
            YIKES( "You gotta implement 3d textures" );
        }
        else if( bgfx::isTextureValid( 0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ), m_flags ) )
        {
            // bgfx owns the image from here and frees it via ImageReleaseCb once uploaded. Skipped
            // mips come first in the data, so the upload is the tail from the first kept mip.
            const uint8_t* data = skip > 0 ? firstMip.m_data : static_cast<const uint8_t*>( imageContainer->m_data );
            const uint32_t size = imageContainer->m_size - static_cast<uint32_t>( data - static_cast<const uint8_t*>( imageContainer->m_data ) );
            const bgfx::Memory* mem = bgfx::makeRef( data, size, ImageReleaseCb, imageContainer );
            TexHandle = bgfx::createTexture2D( static_cast<uint16_t>( mWidth ), static_cast<uint16_t>( mHeight ), 1 < m_mips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ), m_flags, mem );
            if( bgfx::isValid( TexHandle ) )
            {
                bgfx::setName( TexHandle, FilePath.GetLocalPath().data() );
            }
            return true;
        }

        bimg::imageFree( imageContainer );
        return true;
    }

    void Texture::Reload()
    {
        if( m_isRenderTarget || RenderTextureSettings::IsRenderTexturePath( FilePath.GetLocalPathString() ) )
        {
            // Keep the image (and the handles materials bind) unless the settings changed.
            if( !LoadAsync() )
            {
                return;
            }
            m_hasPendingRenderSettings = false;
            if( !m_isRenderTarget || m_pendingRenderSettings != m_renderSettings || ( !bgfx::isValid( TexHandle ) && IsGpuAlive() ) )
            {
                m_isRenderTarget = true;
                m_renderSettings = m_pendingRenderSettings;
                CreateRenderTarget();
            }
            return;
        }
        if( bgfx::isValid( TexHandle ) )
        {
            bgfx::destroy( TexHandle );
            TexHandle = BGFX_INVALID_HANDLE;
        }
        Load();
    }


    bool Texture::CreateRenderTarget()
    {
        DestroyRenderTarget();
        if( bgfx::isValid( TexHandle ) && IsGpuAlive() )
        {
            bgfx::destroy( TexHandle );
        }
        TexHandle = BGFX_INVALID_HANDLE;

        const RenderTextureSettings& settings = m_renderSettings;
        mWidth = settings.Width;
        mHeight = settings.Height;
        mChannels = 4;
        m_flags = BGFX_TEXTURE_RT;
        if( settings.Wrap == RenderTextureWrap::Clamp )
            m_flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        else if( settings.Wrap == RenderTextureWrap::Mirror )
            m_flags |= BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR;
        ++m_renderRevision;
        if( !IsGpuAlive() )
        {
            // Cooks, tests and headless runs: the settings are enough.
            m_mips = 1;
            return true;
        }

        // Display-encoded 8-bit colour, like the camera's own output and like images (no sRGB
        // flag: shaders decode with toLinear). Mips are regenerated after each render, where the
        // format supports that.
        constexpr bgfx::TextureFormat::Enum kFormat = bgfx::TextureFormat::BGRA8;
        const bool mips = settings.Mips && ( bgfx::getCaps()->formats[kFormat] & BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN ) != 0;
        const uint16_t width = static_cast<uint16_t>( settings.Width );
        const uint16_t height = static_cast<uint16_t>( settings.Height );
        if( !bgfx::isTextureValid( 0, false, 1, kFormat, m_flags ) )
        {
            CLog::Log( CLog::LogType::Error, "Render texture " + FilePath.GetLocalPathString() + ": the GPU can't render to BGRA8 textures" );
            return false;
        }
        // Opaque black until a camera renders into it (render targets start out undefined).
        bgfx::TextureInfo info;
        bgfx::calcTextureSize( info, width, height, 1, false, mips, 1, kFormat );
        const bgfx::Memory* black = bgfx::alloc( info.storageSize );
        std::memset( black->data, 0, black->size );
        for( uint32_t alpha = 3; alpha < black->size; alpha += 4 )
        {
            black->data[alpha] = 0xff;
        }
        TexHandle = bgfx::createTexture2D( width, height, mips, 1, kFormat, m_flags, black );
        if( !bgfx::isValid( TexHandle ) )
        {
            CLog::Log( CLog::LogType::Error, "Render texture " + FilePath.GetLocalPathString() + ": couldn't create the texture" );
            return false;
        }
        m_mips = info.numMips;
        bgfx::setName( TexHandle, FilePath.GetLocalPath().data() );
        // Mipped attachments regenerate their mips whenever a view finishes rendering into them.
        m_renderTarget = bgfx::createFrameBuffer( 1, &TexHandle, false );
        if( !bgfx::isValid( m_renderTarget ) )
        {
            CLog::Log( CLog::LogType::Error, "Render texture " + FilePath.GetLocalPathString() + ": couldn't create the render target" );
            return false;
        }
        bgfx::setName( m_renderTarget, FilePath.GetLocalPath().data() );
        return true;
    }


    bgfx::TextureHandle Texture::Sampleable( bgfx::TextureHandle InHandle )
    {
        return Sampleable( InHandle, GetDefaultTexture( DefaultTexture::Black ) );
    }


    bgfx::TextureHandle Texture::Sampleable( bgfx::TextureHandle InHandle, bgfx::TextureHandle InInstead )
    {
        return ( bgfx::isValid( InHandle ) && InHandle.idx == s_drawingTarget.idx ) ? InInstead : InHandle;
    }


    void Texture::DestroyRenderTarget()
    {
        if( bgfx::isValid( m_renderTarget ) && IsGpuAlive() )
        {
            bgfx::destroy( m_renderTarget );
        }
        m_renderTarget = BGFX_INVALID_HANDLE;
    }


    std::string Texture::ToString( TextureType type )
    {
        switch( type )
        {
        case TextureType::Diffuse:
            return "Diffuse";
        case TextureType::Normal:
            return "Normal";
        case TextureType::Specular:
            return "Specular";
        case TextureType::Height:
            return "Height";
        case TextureType::Opacity:
            return "Opacity";
        case TextureType::Emissive:
            return "Emissive";
        case TextureType::MetallicRoughness:
            return "MetallicRoughness";
        case TextureType::Occlusion:
            return "Occlusion";
        case TextureType::Metallic:
            return "Metallic";
        case TextureType::Roughness:
            return "Roughness";
        case TextureType::Count:
        default:
            CLog::GetInstance().Log( CLog::LogType::Error, "Couldn't find texture type: " + std::to_string( type ) );
            return "";
        }
    }
}

void TextureResourceMetadata::OnSerialize( json& outJson )
{
    outJson["Type"] = FromEnum( OutputType );
    outJson["MIPs"] = GenerateMIPs;
    outJson["Quality"] = OutputQualityFromEnum( OutputQuality );
    outJson["Format"] = OutputFormatFromEnum( OutputFormat );
}

void TextureResourceMetadata::OnDeserialize( const json& inJson )
{
    // Through the VFS: a pak-served .dds counts (read-only runs must not re-export).
    if( !Path( FilePath.FullPath + ".dds" ).Exists )
    {
        FlaggedForExport = true;
    }

    if( inJson.contains( "Type" ) )
    {
        OutputType = ToEnum( inJson["Type"] );
    }

    if( inJson.contains( "MIPs" ) )
    {
        GenerateMIPs = inJson["MIPs"];
    }

    if( inJson.contains( "Quality" ) )
    {
        OutputQuality = OutputQualityToEnum( inJson["Quality"] );
    }

    if( inJson.contains( "Format" ) )
    {
        OutputFormat = OutputFormatToEnum( inJson["Format"] );
    }
}

std::string TextureResourceMetadata::FromEnum( OutputTextureType inType )
{
    switch( inType )
    {
    case OutputTextureType::NormalMap:
        return "Normal Map";
    case OutputTextureType::Default:
    default:
        return "Default";
        break;
    }
}

TextureResourceMetadata::OutputTextureType TextureResourceMetadata::ToEnum( const std::string& inType )
{
    for( int n = 0; n < (int)OutputTextureType::Count; n++ )
    {
        if( FromEnum( (OutputTextureType)n ) == inType )
        {
            return (OutputTextureType)n;
        }
    }

    return OutputTextureType::Default;
}

std::string TextureResourceMetadata::ReadableOutputQualityFromEnum( OutputTextureQuality inType )
{
    switch( inType )
    {
    case OutputTextureQuality::Fastest:
        return "Fastest";
    case OutputTextureQuality::Highest:
        return "Highest";
    case OutputTextureQuality::Default:
    case OutputTextureQuality::Count:
    default:
        return "Default";
    }
}

std::string TextureResourceMetadata::OutputQualityFromEnum( OutputTextureQuality inType )
{
    switch( inType )
    {
    case OutputTextureQuality::Fastest:
        return "f";
    case OutputTextureQuality::Highest:
        return "h";
    case OutputTextureQuality::Default:
    case OutputTextureQuality::Count:
    default:
        return "d";
    }
}

TextureResourceMetadata::OutputTextureQuality TextureResourceMetadata::OutputQualityToEnum( const std::string& inType )
{
    for( int n = 0; n < (int)OutputTextureQuality::Count; n++ )
    {
        if( OutputQualityFromEnum( (OutputTextureQuality)n ) == inType )
        {
            return (OutputTextureQuality)n;
        }
    }

    return OutputTextureQuality::Default;
}

std::string TextureResourceMetadata::OutputFormatFromEnum( OutputTextureFormat inType )
{
    switch( inType )
    {
    case OutputTextureFormat::Auto:
        return "Auto";
    case OutputTextureFormat::BC1:
        return "BC1";
    //case OutputTextureFormat::BC2:
    //    return "BC2";
    case OutputTextureFormat::BC3:
        return "BC3";
    case OutputTextureFormat::BC4:
        return "BC4";
    case OutputTextureFormat::BC5:
        return "BC5";
    //case OutputTextureFormat::BC6H:
    //    return "BC6H";
    //case OutputTextureFormat::BC7:
    //    return "BC7";
    //case OutputTextureFormat::ETC1:
    //    return "ETC1";
    //case OutputTextureFormat::ETC2:
    //    return "ETC2";
    //case OutputTextureFormat::ETC2A:
    //    return "ETC2A";
    //case OutputTextureFormat::ETC2A1:
    //    return "ETC2A1";
    //case OutputTextureFormat::PTC12:
    //    return "PTC12";
    //case OutputTextureFormat::PTC14:
    //    return "PTC14";
    //case OutputTextureFormat::PTC12A:
    //    return "PTC12A";
    //case OutputTextureFormat::PTC14A:
    //    return "PTC14A";
    //case OutputTextureFormat::PTC22:
    //    return "PTC22";
    //case OutputTextureFormat::PTC24:
    //    return "PTC24";
    //case OutputTextureFormat::ATC:
    //    return "ATC";
    //case OutputTextureFormat::ATCE:
    //    return "ATCE";
    //case OutputTextureFormat::ATCI:
    //    return "ATCI";
    case OutputTextureFormat::Count:
    case OutputTextureFormat::None:
    default:
        return "None";
    }
}

TextureResourceMetadata::OutputTextureFormat TextureResourceMetadata::OutputFormatToEnum( const std::string& inType )
{
    for( int n = 0; n < (int)OutputTextureFormat::Count; n++ )
    {
        if( OutputFormatFromEnum( (OutputTextureFormat)n ) == inType )
        {
            return (OutputTextureFormat)n;
        }
    }

    return OutputTextureFormat::None;
}

namespace
{
    // True if any pixel of the image isn't fully opaque. Images that can't be read count as
    // transparent, so they get a format that keeps alpha.
    bool ImageHasTransparency( const std::string& InPath )
    {
        std::vector<uint8_t> data;
        if( !VFS::Get().ReadBytes( Path( InPath, true ), data ) )
        {
            return true;
        }
        bx::DefaultAllocator allocator;
        bimg::ImageContainer* image = bimg::imageParse( &allocator, data.data(), static_cast<uint32_t>( data.size() ) );
        if( !image )
        {
            return true;
        }
        const uint64_t pixels = uint64_t( image->m_width ) * image->m_height;
        bool transparent = true;
        switch( image->m_format )
        {
        case bimg::TextureFormat::R8:
        case bimg::TextureFormat::RG8:
        case bimg::TextureFormat::RGB8:
        case bimg::TextureFormat::R16:
        case bimg::TextureFormat::RG16:
            transparent = false;
            break;
        case bimg::TextureFormat::RGBA8:
        case bimg::TextureFormat::BGRA8:
        {
            const uint8_t* rgba = static_cast<const uint8_t*>( image->m_data );
            transparent = false;
            for( uint64_t i = 0; i < pixels && !transparent; ++i )
            {
                transparent = rgba[i * 4 + 3] != 255;
            }
            break;
        }
        case bimg::TextureFormat::RGBA16:
        {
            const uint16_t* rgba = static_cast<const uint16_t*>( image->m_data );
            transparent = false;
            for( uint64_t i = 0; i < pixels && !transparent; ++i )
            {
                transparent = rgba[i * 4 + 3] != 0xFFFF;
            }
            break;
        }
        default:
            break;
        }
        bimg::imageFree( image );
        return transparent;
    }
}


bool TextureResourceMetadata::LooksLikeNormalMap( const std::string& InFileName )
{
    std::string stem = std::filesystem::path( InFileName ).stem().string();
    std::transform( stem.begin(), stem.end(), stem.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if( stem.find( "normal" ) != std::string::npos )
    {
        return true;
    }
    // Not a bare "_n": that also names letters and numbers (Decal_Letter_N.png).
    for( const char* suffix : { "_nrm", "_nor", "_ddn", "_nm" } )
    {
        const std::string ending( suffix );
        if( stem.size() > ending.size() && stem.compare( stem.size() - ending.size(), ending.size(), ending ) == 0 )
        {
            return true;
        }
    }
    return false;
}


TextureResourceMetadata::OutputTextureFormat TextureResourceMetadata::ChooseAutoFormat( bool InNormalMap, bool InHasAlpha, bool InHighDynamicRange )
{
    if( InHighDynamicRange )
    {
        return OutputTextureFormat::None;   // BC6H would be the format, but texturec can't encode it
    }
    if( InNormalMap )
    {
        return OutputTextureFormat::BC5;
    }
    return InHasAlpha ? OutputTextureFormat::BC3 : OutputTextureFormat::BC1;
}


bool TextureResourceMetadata::IsNormalMap() const
{
    return OutputType == OutputTextureType::NormalMap || LooksLikeNormalMap( FilePath.FullPath );
}


TextureResourceMetadata::OutputTextureFormat TextureResourceMetadata::ResolveOutputFormat() const
{
    if( OutputFormat != OutputTextureFormat::Auto )
    {
        return OutputFormat;
    }
    std::string extension = std::filesystem::path( FilePath.FullPath ).extension().string();
    std::transform( extension.begin(), extension.end(), extension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    const bool highDynamicRange = extension == ".hdr" || extension == ".exr";
    const bool normalMap = IsNormalMap();
    // Normal maps and HDR images don't need the (full-image) transparency check.
    const bool hasAlpha = !normalMap && !highDynamicRange && ImageHasTransparency( FilePath.FullPath );
    return ChooseAutoFormat( normalMap, hasAlpha, highDynamicRange );
}


std::string TextureResourceMetadata::GetExtension2() const
{
    return std::string( "dds" );
}

#if USING( ME_EDITOR )

void TextureResourceMetadata::OnEditorInspect()
{
    MetaBase::OnEditorInspect();

    HavanaUtils::Label( "Import Settings" );
    if( ImGui::BeginCombo( "##ImportSettings", FromEnum( OutputType ).c_str() ) )
    {
        for( int n = 0; n < (int)OutputTextureType::Count; n++ )
        {
            if( ImGui::Selectable( FromEnum( (OutputTextureType)n ).c_str(), false ) )
            {
                OutputType = (OutputTextureType)n;

                //static_cast<RenderCore*>(GetEngine().GetWorld().lock()->GetCore(RenderCore::GetTypeId()))->UpdateMesh(this);

                break;
            }
        }
        ImGui::EndCombo();
    }


    HavanaUtils::Label( "Format" );
    if( ImGui::BeginCombo( "##FormatSetting", OutputFormatFromEnum( OutputFormat ).c_str() ) )
    {
        for( int n = 0; n < (int)OutputTextureFormat::Count; n++ )
        {
            if( ImGui::Selectable( OutputFormatFromEnum( (OutputTextureFormat)n ).c_str(), false ) )
            {
                OutputFormat = (OutputTextureFormat)n;

                break;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip( "Auto: BC5 for normal maps, BC3 for images with transparency, BC1 otherwise (HDR stays uncompressed).\nNone keeps every pixel exact, for pixel art, UI and data textures." );

    if( OutputFormat != OutputTextureFormat::None )
    {
        HavanaUtils::Label( "Quality" );
        if( ImGui::BeginCombo( "##QualitySetting", ReadableOutputQualityFromEnum( OutputQuality ).c_str() ) )
        {
            for( int n = 0; n < (int)OutputTextureQuality::Count; n++ )
            {
                if( ImGui::Selectable( ReadableOutputQualityFromEnum( (OutputTextureQuality)n ).c_str(), false ) )
                {
                    OutputQuality = (OutputTextureQuality)n;

                    break;
                }
            }
            ImGui::EndCombo();
        }
    }

    HavanaUtils::Label( "Generate MIPs" );
    ImGui::Checkbox( "##GenerateMIPs", &GenerateMIPs );
}

#endif

#if USING( ME_TOOLS )

void TextureResourceMetadata::Export()
{
    std::string exportType = " --as dds";
    if( GenerateMIPs )
    {
        exportType += " -m";
    }
    const OutputTextureFormat format = ResolveOutputFormat();
    // -n renormalizes normal maps (each mip too) and keeps x, y in the two channels BC5 stores.
    if( OutputType == OutputTextureType::NormalMap || ( OutputFormat == OutputTextureFormat::Auto && format == OutputTextureFormat::BC5 ) )
    {
        exportType += " -n";
    }
    if( format != OutputTextureFormat::None )
    {
        exportType += " -t " + OutputFormatFromEnum( format );
        exportType += " -q " + OutputQualityFromEnum( OutputQuality );
    }
    /*
        rule texturec_bc1
            command = texturec -f $in -o $out -t bc1 -m
        rule texturec_bc2
            command = texturec -f $in -o $out -t bc2 -m
        rule texturec_bc3
            command = texturec -f $in -o $out -t bc3 -m
        rule texturec_bc4
            command = texturec -f $in -o $out -t bc4 -m
        rule texturec_bc5
            command = texturec -f $in -o $out -t bc5 -m
        rule texturec_bc7
            command = texturec -f $in -o $out -t bc7 -m
        rule texturec_etc1
            command = texturec -f $in -o $out -t etc1 -m
        rule texturec_etc2
            command = texturec -f $in -o $out -t etc2 -m
        rule texturec_diffuse
            command = texturec -f $in -o $out -t bc2 -m
        rule texturec_normal
            command = texturec -f $in -o $out -t bc5 -m -n
        rule texturec_height
            command = texturec -f $in -o $out -t r8
        rule texturec_equirect
            command = texturec -f $in -o $out --max 512 -t rgba16f --equirect
    */

#if USING( ME_PLATFORM_WIN64 )
    Path texturecPath = Path( "Engine/Tools/Win64/texturec.exe" );
    if( !texturecPath.Exists )
    {
        texturecPath = Path( "texturec.exe" );
    }

    std::string progArgs = "-f \"";
    progArgs += FilePath.FullPath;
    progArgs += "\" -o \"" + FilePath.FullPath + ".dds\"" + exportType;
    // ./shaderc -f ../../../Assets/Shaders/vs_cubes.shader -o ../../../Assets/Shaders/dummy.bin --varyingdef ./varying.def.sc --platform windows -p vs_5_0 --type vertex
    CLog::GetInstance().Log( CLog::LogType::Info, progArgs );
    PlatformUtils::SystemCall( texturecPath, progArgs );
#elif USING( ME_PLATFORM_MACOS )

    Path texturecPath = Path( "Engine/Tools/macOS/texturec" );

    std::string progArgs = "-f \"";
    progArgs += FilePath.FullPath;
    progArgs += "\" -o \"" + FilePath.FullPath + ".dds\"" + exportType;
    CLog::GetInstance().Log( CLog::LogType::Info, progArgs );
    PlatformUtils::SystemCall( texturecPath, progArgs );

#elif USING( ME_PLATFORM_LINUX )

    const std::filesystem::path launcher = FindTextureCompilerLauncher();
    if( launcher.empty() )
    {
        YIKES( "Could not locate Engine/Tools/RunTextureCompiler.sh for texture export." );
        return;
    }
    const std::string command = ShellQuote( launcher.string() )
        + " -f " + ShellQuote( FilePath.FullPath )
        + " -o " + ShellQuote( FilePath.FullPath + ".dds" ) + exportType;
    std::string output;
    if( PlatformUtils::RunCommand( command, output ) != 0 )
    {
        YIKES( "Texture export failed for " + FilePath.FullPath + ": " + output );
    }

#endif
}

#endif


const Reflection::TypeInfo* RenderTextureMetadata::GetEditableType()
{
    return &Moonlight::RenderTextureSettings::StaticType();
}


void* RenderTextureMetadata::GetEditableData()
{
    if( !m_loaded )
    {
        File file{ FilePath };
        m_settings.Parse( file.Read() );
        m_loaded = true;
    }
    return &m_settings;
}


void RenderTextureMetadata::SaveEditableData()
{
    if( m_loaded )
    {
        m_settings.Clamp();
        File file{ FilePath };
        file.Write( m_settings.Write() );
    }
}
