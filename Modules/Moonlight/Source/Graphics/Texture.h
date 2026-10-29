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
#include "Graphics/RenderTextureSettings.h"

namespace bimg {
    struct ImageContainer;
}

namespace Moonlight
{
    enum TextureType
    {
        Diffuse = 0,        // base colour (sRGB)
        Normal,
        Specular,
        Height,
        Opacity,
        Emissive,           // sRGB
        MetallicRoughness,  // glTF: G roughness, B metallic
        Occlusion,          // R
        Metallic,           // R, separate FBX/PBR maps
        Roughness,          // R
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
        TextureType Type = TextureType::Diffuse;

        int mWidth = 1920;
        int mHeight = 1080;
        int mChannels;
        Texture() = delete;
        Texture( const Path& InFilePath, WrapMode mode = WrapMode::Wrap );
        ~Texture();

        virtual bool Load() override;
        virtual void Reload() override;
        // Async: the compiled DDS is read and parsed on a loader thread, then uploaded here.
        // A render texture (.rendertexture) reads its settings there and creates its target here.
        bool SupportsAsyncLoad() const override;
        bool LoadAsync() override;
        bool FinishAsyncLoad() override;

        // Render textures: a camera renders into the target (its final, tonemapped image), materials
        // sample TexHandle. They hold display-encoded LDR colour, like an image file, so shaders decode
        // them the same way. The texture owns both handles; a reload with new settings replaces them,
        // so read them when binding, never keep them.
        bool IsRenderTarget() const { return m_isRenderTarget; }
        bgfx::FrameBufferHandle GetRenderTarget() const { return m_renderTarget; }
        const RenderTextureSettings& GetRenderTextureSettings() const { return m_renderSettings; }
        // Changes whenever the target is (re)created.
        uint32_t GetRenderTargetRevision() const { return m_renderRevision; }
        // A camera finished rendering into the target this frame (renderer frame index). The count
        // carries on across reloads.
        void MarkRendered( uint64_t InFrame ) { ++m_renderCount; m_lastRenderedFrame = InFrame; }
        uint32_t GetRenderCount() const { return m_renderCount; }
        uint64_t GetLastRenderedFrame() const { return m_lastRenderedFrame; }

        // The render texture a camera is drawing into right now (the renderer sets it around that
        // camera's views). A camera never samples its own image: binders pass their handles through
        // Sampleable, which swaps that texture for InInstead (black by default).
        static void SetDrawingTarget( bgfx::TextureHandle InHandle ) { s_drawingTarget = InHandle; }
        static bgfx::TextureHandle GetDrawingTarget() { return s_drawingTarget; }
        static bgfx::TextureHandle Sampleable( bgfx::TextureHandle InHandle );
        static bgfx::TextureHandle Sampleable( bgfx::TextureHandle InHandle, bgfx::TextureHandle InInstead );

        // A texture from an image built in memory, uploaded now (main thread). Takes ownership of
        // InImage; InName only labels it (nothing is read from or cached under it).
        static SharedPtr<Texture> CreateFromImage( const std::string& InName, bimg::ImageContainer* InImage, WrapMode InMode = WrapMode::Wrap );

        // Longest side a loaded texture may upload at (0 = no cap; a hardware profile sets it).
        // Bigger mipped textures upload from the first mip that fits and report that size;
        // textures already loaded keep theirs until reloaded. Main thread.
        static void SetMaxUploadSize( uint32_t InTexels ) { s_maxUploadSize = InTexels; }
        static uint32_t GetMaxUploadSize() { return s_maxUploadSize; }

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

    private:
        // Render textures: (re)creates the target for m_renderSettings (main thread).
        bool CreateRenderTarget();
        void DestroyRenderTarget();

        static inline uint32_t s_maxUploadSize = 0;
        static inline bgfx::TextureHandle s_drawingTarget = BGFX_INVALID_HANDLE;
        WrapMode m_wrapMode = WrapMode::Wrap;
        // Parsed by LoadAsync, uploaded (and handed to bgfx) by FinishAsyncLoad.
        bimg::ImageContainer* m_pendingImage = nullptr;

        bool m_isRenderTarget = false;
        // Parsed by LoadAsync (loader thread) for FinishAsyncLoad.
        bool m_hasPendingRenderSettings = false;
        RenderTextureSettings m_pendingRenderSettings;
        RenderTextureSettings m_renderSettings;
        bgfx::FrameBufferHandle m_renderTarget = BGFX_INVALID_HANDLE;
        uint32_t m_renderRevision = 0;
        uint32_t m_renderCount = 0;
        uint64_t m_lastRenderedFrame = 0;
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
        // Picked per texture when it's cooked (ChooseAutoFormat): BC5 for normal maps, BC3 when
        // the image has transparency, BC1 otherwise; HDR images stay uncompressed.
        Auto,
        None,
        BC1,          //!< DXT1: RGB, 4 bits per pixel
        //BC2,          //!< DXT3
        BC3,          //!< DXT5: RGBA, 8 bits per pixel
        BC4,          //!< LATC1/ATI1: one channel (masks, heights)
        BC5,          //!< LATC2/ATI2: two channels (normal maps; shaders rebuild z)
        //BC6H,         //!< BC6H (bimg's texturec can't encode BC6H or BC7)
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
        // New images take the project look's import defaults (LookPresets); saved metas override.
        if( s_defaultUncompressed )
        {
            OutputFormat = OutputTextureFormat::None;
        }
        GenerateMIPs = s_defaultMips;
    }

    // Import defaults for images without a .meta yet (the project look sets them: pixel art).
    static inline bool s_defaultUncompressed = false;
    static inline bool s_defaultMips = true;

    virtual std::string GetExtension2() const override;

    void OnSerialize( json& inJson ) override;
    void OnDeserialize( const json& inJson ) override;

    bool GenerateMIPs = true;
    OutputTextureType OutputType = OutputTextureType::Default;
    // I haven't seen much difference in final images with changing this setting, it just takes longer?
    // Might not apply to the formats I'm using based off the source code
    OutputTextureQuality OutputQuality = OutputTextureQuality::Default;
    OutputTextureFormat OutputFormat = OutputTextureFormat::Auto;

    // The format a cook writes: OutputFormat, with Auto resolved for this image.
    OutputTextureFormat ResolveOutputFormat() const;
    // Type NormalMap, or a file named like a normal map (Brick_N.png, rock_normal.png, wall_nrm.tga).
    bool IsNormalMap() const;
    static bool LooksLikeNormalMap( const std::string& InFileName );
    static OutputTextureFormat ChooseAutoFormat( bool InNormalMap, bool InHasAlpha, bool InHighDynamicRange );

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

#if USING( ME_TOOLS )
    void Export() override;
    // texturec runs off the main thread on hot reload.
    bool ExportsInBackground() const override
    {
        return true;
    }
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


struct TextureResourceMetadataTga
    : public TextureResourceMetadata
{
    TextureResourceMetadataTga( const Path& filePath ) : TextureResourceMetadata( filePath ) {}
};


// A .rendertexture is its own runtime form: nothing is imported or cooked, the meta only carries the
// asset's GUID. The asset browser edits the file's settings, and saving them hot reloads the texture.
struct RenderTextureMetadata
    : public MetaBase
{
    explicit RenderTextureMetadata( const Path& InPath )
        : MetaBase( InPath )
    {
    }

    std::string GetExtension2() const override
    {
        return Moonlight::RenderTextureSettings::kExtension;
    }
    void OnSerialize( json& OutJson ) override
    {
    }
    void OnDeserialize( const json& InJson ) override
    {
    }

    const Reflection::TypeInfo* GetEditableType() override;
    void* GetEditableData() override;
    void SaveEditableData() override;

private:
    Moonlight::RenderTextureSettings m_settings;
    bool m_loaded = false;
};


ME_REGISTER_METADATA( "png", TextureResourceMetadata );
ME_REGISTER_METADATA( "jpg", TextureResourceMetadataJpg );
ME_REGISTER_METADATA( "tga", TextureResourceMetadataTga );
ME_REGISTER_METADATA( "rendertexture", RenderTextureMetadata );
