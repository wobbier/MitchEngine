#include <iostream>
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
#include <Device/FrameBuffer.h>

static void imageReleaseCb( void* _ptr, void* _userData )
{
    BX_UNUSED( _ptr );
    bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)_userData;
    bimg::imageFree( imageContainer );
}

void unload( bx::AllocatorI* _allocator, void* _ptr )
{
	if( _ptr )
	{
		bx::free(_allocator, _ptr);
	}
}

namespace Moonlight
{
    Texture::Texture( const Path& InFilePath, WrapMode mode )
        : Resource( InFilePath )
        , TexHandle( BGFX_INVALID_HANDLE )
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
        // TODO: Unload textures
        if( bgfx::isValid( TexHandle ) )
        {
            bgfx::destroy( TexHandle );
        }
        TexHandle = BGFX_INVALID_HANDLE;
    }

    bool Texture::Load()
    {
        m_flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_W_MIRROR;
        Path compiledTexture( FilePath.FullPath + ".dds" );
        if( !compiledTexture.Exists )
        {
            return false;
        }

        const auto* memory = Moonlight::LoadMemory( compiledTexture );
        if( memory )
        {
            if( bimg::ImageContainer* imageContainer = bimg::imageParse( Moonlight::getDefaultAllocator(), memory->data, memory->size ) )
            {
                const bgfx::Memory* mem = bgfx::makeRef( imageContainer->m_data, imageContainer->m_size, NULL, imageContainer );

                bx::free( Moonlight::getDefaultAllocator(), (void*)memory );
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
                    TexHandle = bgfx::createTexture2D( imageContainer->m_width, imageContainer->m_height, 1 < imageContainer->m_numMips, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ), m_flags, mem );
                }

                if( bgfx::isValid( TexHandle ) )
                {
                    bgfx::setName( TexHandle, FilePath.GetLocalPath().data() );
                }

                bgfx::TextureInfo* info = nullptr;
                if( info )
                {
                    bgfx::calcTextureSize( *info, imageContainer->m_width, imageContainer->m_height, imageContainer->m_depth, imageContainer->m_cubeMap, imageContainer->m_numMips > 0, imageContainer->m_numLayers, bgfx::TextureFormat::Enum( imageContainer->m_format ) );
                }
                mWidth = imageContainer->m_width;
                mHeight = imageContainer->m_height;
                m_mips = imageContainer->m_numMips;

#if USING ( ME_DEBUG )
                //BRUH_FMT( "%i, %s", TexHandle.idx, compiledTexture.FullPath.c_str() );
#endif
                return true;
            }
            bx::free( Moonlight::getDefaultAllocator(), (void*)memory );
        }

        return false;
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

    ImGui::Checkbox( "Generate MIPs", &GenerateMIPs );
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
#else

    Path optickPath = Path( "Engine/Tools/macOS/texturec" );

    // texturec -f $in -o $out -t bc2 -m
    std::string progArgs = "\"" + optickPath.FullPath + "\" -f "+optickPath.GetDirectoryString()+"/../../../";
    progArgs += FilePath.GetLocalPathString();
    progArgs += " -o \""+optickPath.GetDirectoryString()+"/../../../" + FilePath.GetLocalPathString() + ".dds\"" + exportType;
    system( progArgs.c_str() );

#endif
}

#endif
