#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include "bx/allocator.h"
#include "bx/readerwriter.h"
#include "bx/file.h"
#include "bgfx/bgfx.h"
#include <JSON.h>
#include <Resource/MetaRegistry.h>

namespace Moonlight {
    struct FrameBuffer;
}

namespace Moonlight
{
    enum TextureType
    {
        Diffuse = 0,
        Normal,
        Specular,
        Height,
        Opacity,
        Count
    };

    enum WrapMode
    {
        Clamp,
        Decal,
        Wrap,
        Mirror
    };

    class Texture
        : public Resource
    {
    public:
        TextureType Type;

        int mWidth = 1920;
        int mHeight = 1080;
        int mChannels;
        Texture() = delete;
        Texture( const Path& InFilePath, WrapMode mode = WrapMode::Wrap );
        Texture( FrameBuffer* InFilePath, WrapMode mode = WrapMode::Wrap );
        ~Texture();

        virtual bool Load() override;
        virtual void Reload() override;

        void UpdateBuffer( FrameBuffer* NewBuffer );

        template<typename T> static constexpr T NumMipmapLevels( T width, T height )
        {
            T levels = 1;
            while( ( width | height ) >> levels ) {
                ++levels;
            }
            return levels;
        }

        bx::FileReaderI* s_fileReader = nullptr;
        bx::FileWriterI* s_fileWriter = nullptr/* = BX_NEW(s_allocator, Moonlight::FileWriter)*/;
        bx::AllocatorI* getDefaultAllocator();

        // Textures should not be copied around in memory
        ME_NONCOPYABLE( Texture );

        bgfx::TextureHandle TexHandle;
        static std::string ToString( TextureType type );
        uint64_t m_flags = 0;
        uint8_t m_mips = 0;
    };
}

struct TextureResourceMetadata
    : public MetaBase
{
    enum class OutputTextureType : uint8_t
    {
        Default = 0,
        NormalMap,
        Count
    };

    enum class OutputTextureQuality : uint8_t
    {
        Default = 0,
        Highest,
        Fastest,

        Count
    };

    enum class OutputTextureFormat : uint8_t
    {
        None,
        BC1,          //!< DXT1
        //BC2,          //!< DXT3
        BC3,          //!< DXT5
        //BC4,          //!< LATC1/ATI1
        //BC5,          //!< LATC2/ATI2
        //BC6H,         //!< BC6H
        //BC7,          //!< BC7
        //ETC1,         //!< ETC1 RGB8
        //ETC2,         //!< ETC2 RGB8
        //ETC2A,        //!< ETC2 RGBA8
        //ETC2A1,       //!< ETC2 RGB8A1
        //PTC12,        //!< PVRTC1 RGB 2BPP
        //PTC14,        //!< PVRTC1 RGB 4BPP
        //PTC12A,       //!< PVRTC1 RGBA 2BPP
        //PTC14A,       //!< PVRTC1 RGBA 4BPP
        //PTC22,        //!< PVRTC2 RGBA 2BPP
        //PTC24,        //!< PVRTC2 RGBA 4BPP
        //ATC,          //!< ATC RGB 4BPP
        //ATCE,         //!< ATCE RGBA 8 BPP explicit alpha
        //ATCI,         //!< ATCI RGBA 8 BPP interpolated alpha

        Count
    };

    TextureResourceMetadata( const Path& filePath )
        : MetaBase( filePath )
    {
    }

    virtual std::string GetExtension2() const override;

    void OnSerialize( json& inJson ) override;
    void OnDeserialize( const json& inJson ) override;

    bool GenerateMIPs = true;
    OutputTextureType OutputType = OutputTextureType::Default;
    // I haven't seen much difference in final images with changing this setting, it just takes longer?
    // Might not apply to the formats I'm using based off the source code
    OutputTextureQuality OutputQuality = OutputTextureQuality::Default;
    OutputTextureFormat OutputFormat = OutputTextureFormat::None;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

#if USING( ME_TOOLS )
    void Export() override;
#endif

private:
    std::string FromEnum( OutputTextureType inType );
    OutputTextureType ToEnum( const std::string& inType );

    std::string ReadableOutputQualityFromEnum( OutputTextureQuality inType );
    std::string OutputQualityFromEnum( OutputTextureQuality inType );
    OutputTextureQuality OutputQualityToEnum( const std::string& inType );

    std::string OutputFormatFromEnum( OutputTextureFormat inType );
    OutputTextureFormat OutputFormatToEnum( const std::string& inType );
};

struct TextureResourceMetadataJpg
    : public TextureResourceMetadata
{
    TextureResourceMetadataJpg( const Path& filePath ) : TextureResourceMetadata( filePath ) {}
};


ME_REGISTER_METADATA( "png", TextureResourceMetadata );
ME_REGISTER_METADATA( "jpg", TextureResourceMetadataJpg );
