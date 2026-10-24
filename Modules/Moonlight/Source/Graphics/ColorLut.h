#pragma once
#include "Resource/Resource.h"
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Moonlight
{
    // A 3D colour-grading lookup table parsed from an Adobe/Resolve .cube file (display-referred,
    // sRGB in and out). The loader thread parses the text into RGBA16F texels; the main thread
    // uploads a volume texture. Saving the file reparses it, so grades tune live.
    class ColorLut final
        : public Resource
    {
    public:
        explicit ColorLut( const Path& InPath );
        ~ColorLut() override;

        bool Load() override;
        void Reload() override;
        bool SupportsAsyncLoad() const override;
        bool LoadAsync() override;
        bool FinishAsyncLoad() override;

        bgfx::TextureHandle GetHandle() const { return m_handle; }
        int GetSize() const { return m_size; }
        bool IsReady() const { return bgfx::isValid( m_handle ); }

        // Parses .cube text: LUT_3D_SIZE N and N^3 "r g b" rows (red fastest), '#' comments,
        // TITLE and DOMAIN_MIN/MAX lines. Returns false with OutError set on anything else.
        // Non-identity domains aren't supported; they parse with a warning and sample as 0..1.
        static bool Parse( const std::string& InText, int& OutSize, std::vector<float>& OutTexels, std::string& OutError, bool& OutDomainIgnored );

    private:
        void DestroyHandle();

        bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
        int m_size = 0;
        // RGBA16F texels staged by LoadAsync for the main-thread upload.
        std::vector<uint16_t> m_pendingTexels;
        int m_pendingSize = 0;
    };

    struct ColorLutMetadata final
        : public MetaBase
    {
        explicit ColorLutMetadata( const Path& InPath ) : MetaBase( InPath ) {}
        std::string GetExtension2() const override { return "cube"; }
        void OnSerialize( json& ) override {}
        void OnDeserialize( const json& ) override {}
    };
}

ME_REGISTER_METADATA( "cube", Moonlight::ColorLutMetadata )
