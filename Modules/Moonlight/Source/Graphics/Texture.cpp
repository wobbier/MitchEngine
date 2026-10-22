#include <algorithm>
#include <iostream>
#include <fstream>
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
#include <Device/FrameBuffer.h>


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
        std::ifstream file( inPath.FullPath, std::ios::binary | std::ios::ate );
        if( !file )
        {
            YIKES_FMT( "[%s]: can't open the file", inPath.GetLocalPathString().c_str() );
            return false;
        }
        outData.resize( static_cast<size_t>( file.tellg() ) );
        file.seekg( 0 );
        return static_cast<bool>( file.read( reinterpret_cast<char*>( outData.data() ), static_cast<std::streamsize>( outData.size() ) ) );
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

    Texture::Texture( Moonlight::FrameBuffer* InFilePath, WrapMode mode /*= WrapMode::Wrap*/ )
        : Resource( Path( "" ) )
        , TexHandle( BGFX_INVALID_HANDLE )
    {
    }

    Texture::~Texture()
    {
        if( m_pendingImage )
        {
            bimg::imageFree( m_pendingImage );
        }
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


    bool Texture::FinishAsyncLoad()
    {
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
        if( bgfx::isValid( TexHandle ) )
        {
            bgfx::destroy( TexHandle );
            TexHandle = BGFX_INVALID_HANDLE;
        }
        Load();
    }

    void Texture::UpdateBuffer( FrameBuffer* NewBuffer )
    {
        TexHandle = NewBuffer->Texture;
        mWidth = NewBuffer->Width;
        mHeight = NewBuffer->Height;
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
    if( !std::filesystem::exists( FilePath.FullPath + ".dds" ) )
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
    case OutputTextureFormat::BC1:
        return "BC1";
    //case OutputTextureFormat::BC2:
    //    return "BC2";
    case OutputTextureFormat::BC3:
        return "BC3";
    //case OutputTextureFormat::BC4:
    //    return "BC4";
    //case OutputTextureFormat::BC5:
    //    return "BC5";
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
    if( OutputType == OutputTextureType::NormalMap )
    {
        exportType += " -n";
    }
    if( OutputFormat != OutputTextureFormat::None )
    {
        exportType += " -t " + OutputFormatFromEnum( OutputFormat );
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
