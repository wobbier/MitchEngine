#include "PCH.h"
#include "FontAtlas.h"
#include "CLog.h"
#include "Path.h"
#include "VFS/VFS.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <algorithm>
#include <cstring>
#include <map>

namespace
{
    FT_Library s_library = nullptr;
    std::map<std::string, std::unique_ptr<FontAtlas>>& Atlases()
    {
        static std::map<std::string, std::unique_ptr<FontAtlas>> atlases;
        return atlases;
    }
}


FontAtlas* FontAtlas::Get( const std::string& InFontPath )
{
    auto& atlases = Atlases();
    auto found = atlases.find( InFontPath );
    if( found != atlases.end() )
    {
        return found->second.get();
    }
    std::unique_ptr<FontAtlas> atlas( new FontAtlas() );
    if( !atlas->Load( InFontPath ) )
    {
        atlases[InFontPath] = nullptr;
        return nullptr;
    }
    return ( atlases[InFontPath] = std::move( atlas ) ).get();
}


void FontAtlas::ReleaseAll()
{
    Atlases().clear();
    if( s_library )
    {
        FT_Done_FreeType( s_library );
        s_library = nullptr;
    }
}


FontAtlas::~FontAtlas()
{
    if( m_face )
    {
        FT_Done_Face( static_cast<FT_Face>( m_face ) );
    }
    if( bgfx::isValid( m_texture ) )
    {
        bgfx::destroy( m_texture );
    }
}


bool FontAtlas::Load( const std::string& InFontPath )
{
    if( !s_library )
    {
        if( FT_Init_FreeType( &s_library ) != 0 )
        {
            YIKES( "World text: FreeType didn't start" );
            return false;
        }
        int spread = kSpread;
        FT_Property_Set( s_library, "sdf", "spread", &spread );
        FT_Property_Set( s_library, "bsdf", "spread", &spread );
    }
    if( !VFS::Get().ReadBytes( Path( InFontPath ), m_fontBytes ) || m_fontBytes.empty() )
    {
        YIKES( "World text: can't read the font " + InFontPath );
        return false;
    }
    FT_Face face = nullptr;
    if( FT_New_Memory_Face( s_library, m_fontBytes.data(), static_cast<FT_Long>( m_fontBytes.size() ), 0, &face ) != 0 )
    {
        YIKES( "World text: " + InFontPath + " isn't a font FreeType reads" );
        return false;
    }
    m_face = face;
    FT_Set_Pixel_Sizes( face, 0, kPixelSize );
    const float ascender = static_cast<float>( face->size->metrics.ascender ) / 64.f;
    const float descender = static_cast<float>( face->size->metrics.descender ) / 64.f;
    m_lineHeightPixels = std::max( ascender - descender, 1.f );
    m_ascender = ascender / m_lineHeightPixels;
    m_hasKerning = FT_HAS_KERNING( face );
    m_pixels.assign( static_cast<size_t>( kAtlasSize ) * kAtlasSize * 4, 0 );
    // White everywhere: the SDF lives in alpha.
    for( size_t i = 0; i < m_pixels.size(); i += 4 )
    {
        m_pixels[i] = m_pixels[i + 1] = m_pixels[i + 2] = 255;
    }
    m_dirty = true;
    return true;
}


uint32_t FontAtlas::NextCodepoint( const std::string& InText, size_t& InOutIndex )
{
    const auto byte = [&]( size_t InAt ) { return static_cast<uint8_t>( InText[InAt] ); };
    const uint8_t lead = byte( InOutIndex++ );
    int extra = lead < 0x80 ? 0 : ( lead >> 5 ) == 0x6 ? 1 : ( lead >> 4 ) == 0xE ? 2 : ( lead >> 3 ) == 0x1E ? 3 : -1;
    if( extra < 0 )
    {
        return 0xFFFD;
    }
    uint32_t code = extra == 0 ? lead : lead & ( 0x3F >> extra );
    for( int i = 0; i < extra; ++i )
    {
        if( InOutIndex >= InText.size() || ( byte( InOutIndex ) & 0xC0 ) != 0x80 )
        {
            return 0xFFFD;
        }
        code = ( code << 6 ) | ( byte( InOutIndex++ ) & 0x3F );
    }
    return code;
}


const FontAtlas::Glyph& FontAtlas::GetGlyph( uint32_t InCodepoint )
{
    auto found = m_glyphs.find( InCodepoint );
    if( found != m_glyphs.end() )
    {
        return found->second;
    }
    Glyph& glyph = m_glyphs[InCodepoint];
    FT_Face face = static_cast<FT_Face>( m_face );
    if( FT_Load_Char( face, InCodepoint, FT_LOAD_DEFAULT ) != 0 )
    {
        return glyph;
    }
    glyph.Advance = static_cast<float>( face->glyph->advance.x ) / 64.f / m_lineHeightPixels;
    if( face->glyph->outline.n_points == 0 || FT_Render_Glyph( face->glyph, FT_RENDER_MODE_SDF ) != 0 )
    {
        return glyph;
    }
    const FT_Bitmap& bitmap = face->glyph->bitmap;
    const int width = static_cast<int>( bitmap.width );
    const int height = static_cast<int>( bitmap.rows );
    if( width <= 0 || height <= 0 )
    {
        return glyph;
    }
    // Shelf packing, a pixel apart.
    if( m_shelfX + width + 1 > kAtlasSize )
    {
        m_shelfX = 1;
        m_shelfY += m_shelfHeight + 1;
        m_shelfHeight = 0;
    }
    if( m_shelfY + height + 1 > kAtlasSize )
    {
        static bool s_warned = false;
        if( !s_warned )
        {
            s_warned = true;
            YIKES( "World text: the glyph atlas is full; further characters are skipped" );
        }
        return glyph;
    }
    for( int y = 0; y < height; ++y )
    {
        const uint8_t* row = bitmap.buffer + static_cast<ptrdiff_t>( y ) * bitmap.pitch;
        for( int x = 0; x < width; ++x )
        {
            m_pixels[( static_cast<size_t>( m_shelfY + y ) * kAtlasSize + m_shelfX + x ) * 4 + 3] = row[x];
        }
    }
    const float atlas = static_cast<float>( kAtlasSize );
    glyph.Uv = glm::vec4( m_shelfX / atlas, m_shelfY / atlas, ( m_shelfX + width ) / atlas, ( m_shelfY + height ) / atlas );
    glyph.Size = glm::vec2( width, height ) / m_lineHeightPixels;
    // bitmap_left / top already include the SDF spread around the outline.
    glyph.Offset = glm::vec2( face->glyph->bitmap_left, face->glyph->bitmap_top - height ) / m_lineHeightPixels;
    glyph.Visible = true;
    m_shelfX += width + 1;
    m_shelfHeight = std::max( m_shelfHeight, height );
    m_dirty = true;
    return glyph;
}


float FontAtlas::GetKerning( uint32_t InLeft, uint32_t InRight ) const
{
    if( !m_hasKerning )
    {
        return 0.f;
    }
    FT_Face face = static_cast<FT_Face>( m_face );
    FT_Vector kerning{};
    FT_Get_Kerning( face, FT_Get_Char_Index( face, InLeft ), FT_Get_Char_Index( face, InRight ), FT_KERNING_DEFAULT, &kerning );
    return static_cast<float>( kerning.x ) / 64.f / m_lineHeightPixels;
}


bgfx::TextureHandle FontAtlas::GetTexture()
{
    if( !bgfx::isValid( m_texture ) )
    {
        m_texture = bgfx::createTexture2D( kAtlasSize, kAtlasSize, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP );
        bgfx::setName( m_texture, "World Text Glyphs" );
    }
    if( m_dirty )
    {
        // The packed rows so far (glyphs arrive in bursts: the first frames of new text).
        const int rows = std::min( m_shelfY + m_shelfHeight + 1, kAtlasSize );
        bgfx::updateTexture2D( m_texture, 0, 0, 0, 0, kAtlasSize, static_cast<uint16_t>( rows ), bgfx::copy( m_pixels.data(), static_cast<uint32_t>( rows ) * kAtlasSize * 4 ) );
        m_dirty = false;
    }
    return m_texture;
}
