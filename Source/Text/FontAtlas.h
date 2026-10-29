#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Signed-distance glyphs for world-space text (LOOK-S12): FreeType renders each glyph as an SDF
// (FT_RENDER_MODE_SDF) into a shared atlas texture, packed in shelves on first use. The sprite
// pass draws them with its SDF mode, so text stays crisp at any size and distance. Metrics are in
// units of the font's line height (1 = one line). Main thread only.
class FontAtlas
{
public:
    struct Glyph
    {
        glm::vec4 Uv = glm::vec4( 0.f );    // u0, v0 (top), u1, v1
        glm::vec2 Size = glm::vec2( 0.f );  // quad size, line-height units (the SDF spread included)
        glm::vec2 Offset = glm::vec2( 0.f );// quad's bottom left from the pen on the baseline
        float Advance = 0.f;
        bool Visible = false;               // spaces and controls have no quad
    };

    // The atlas for a .ttf / .otf (one per path; null when the font can't load).
    static FontAtlas* Get( const std::string& InFontPath );
    // Frees every atlas (shutdown).
    static void ReleaseAll();
    ~FontAtlas();

    const Glyph& GetGlyph( uint32_t InCodepoint );
    float GetKerning( uint32_t InLeft, uint32_t InRight ) const;
    // Ascender above the baseline, line-height units.
    float GetAscender() const { return m_ascender; }
    // Uploads newly packed glyphs; the texture to bind.
    bgfx::TextureHandle GetTexture();

    static constexpr int kAtlasSize = 1024;
    static constexpr int kPixelSize = 48;   // the line height glyphs are rasterized at
    static constexpr int kSpread = 6;       // SDF falloff in pixels each side

    // Decodes one UTF-8 code point at InOutIndex (advancing it); U+FFFD on bad input.
    static uint32_t NextCodepoint( const std::string& InText, size_t& InOutIndex );

private:
    FontAtlas() = default;
    bool Load( const std::string& InFontPath );

    std::vector<uint8_t> m_fontBytes;
    void* m_face = nullptr;                 // FT_Face
    std::vector<uint8_t> m_pixels;          // RGBA8, kAtlasSize squared
    std::unordered_map<uint32_t, Glyph> m_glyphs;
    bgfx::TextureHandle m_texture = BGFX_INVALID_HANDLE;
    bool m_dirty = false;
    int m_shelfX = 1;
    int m_shelfY = 1;
    int m_shelfHeight = 0;
    float m_lineHeightPixels = 1.f;
    float m_ascender = 0.8f;
    bool m_hasKerning = false;
};
