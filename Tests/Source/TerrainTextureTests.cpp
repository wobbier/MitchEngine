#include <doctest/doctest.h>
#include "Graphics/TerrainTexturePacking.h"
#include "Graphics/TerrainTextureCache.h"
#include <future>
#include "Terrain/TerrainSurface.h"
#include "Utils/GUID.h"
#include "JSON.h"
#include <filesystem>
#include <fstream>
#include <limits>

namespace TerrainTextureTests
{
    using namespace Moonlight;
    struct Files
    {
        std::filesystem::path Root = std::filesystem::path( ".tmp/Tests/TerrainTextures" ) / std::to_string( Guid::Generate() );
        Files() { std::filesystem::create_directories( Root ); }
        ~Files() { std::error_code ec; std::filesystem::remove_all( Root, ec ); }
    };
    void Tga( const std::filesystem::path& path, unsigned width, unsigned height, const std::vector<std::array<uint8_t, 4>>& pixels )
    {
        std::vector<uint8_t> bytes( 18 ); bytes[2] = 2; bytes[12] = width & 255; bytes[13] = width >> 8;
        bytes[14] = height & 255; bytes[15] = height >> 8; bytes[16] = 32; bytes[17] = 0x28;
        for( const auto& pixel : pixels ) bytes.insert( bytes.end(), { pixel[2], pixel[1], pixel[0], pixel[3] } );
        std::ofstream file( path, std::ios::binary ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
    }
    void Put( std::vector<uint8_t>& bytes, size_t offset, uint64_t value, unsigned count )
    {
        for( unsigned i = 0; i < count; ++i ) bytes[offset + i] = static_cast<uint8_t>( value >> ( i * 8 ) );
    }
    uint64_t Get( const std::vector<uint8_t>& bytes, size_t offset, unsigned count )
    {
        uint64_t result = 0; for( unsigned i = 0; i < count; ++i ) result |= uint64_t( bytes[offset + i] ) << ( i * 8 ); return result;
    }
    uint64_t Hash( const std::vector<uint8_t>& bytes, size_t offset )
    {
        uint64_t hash = 14695981039346656037ull; for( size_t i = offset; i < bytes.size(); ++i ) hash = ( hash ^ bytes[i] ) * 1099511628211ull; return hash;
    }
}
using namespace TerrainTextureTests;

TEST_CASE( "Terrain textures: semantic mips preserve linear color energy and linear ORM channels" )
{
    Files files; std::vector<std::array<uint8_t, 4>> pixels;
    for( unsigned z = 0; z < 8; ++z ) for( unsigned x = 0; x < 8; ++x )
    { const auto v = uint8_t( ( x + z ) % 2 ? 255 : 0 ); pixels.push_back( { v, v, v, 255 } ); }
    const auto image = files.Root / "Checker.tga"; Tga( image, 8, 8, pixels );
    TerrainTexturePaths paths{}; paths[0][0] = paths[0][2] = image.generic_string();
    TerrainPackedTextures data; std::string error; REQUIRE_MESSAGE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ), error );
    REQUIRE( data.Maps[0].size() == 16 ); CHECK( data.Maps[0][3].Width == 1 );
    CHECK( data.Maps[0][3].Pixels[0] == 188 ); CHECK( data.Maps[2][3].Pixels[0] == 128 );
    for( const auto& mip : data.Maps[1] ) for( size_t i = 0; i < mip.Pixels.size(); i += 4 )
    { CHECK( mip.Pixels[i] == 128 ); CHECK( mip.Pixels[i + 1] == 128 ); CHECK( mip.Pixels[i + 2] == 255 ); }
}

TEST_CASE( "Terrain textures: rectangular sources filter reductions independently of magnification" )
{
    Files files; std::vector<std::array<uint8_t, 4>> pixels;
    for( unsigned z = 0; z < 2; ++z ) for( unsigned x = 0; x < 16; ++x )
    { const auto v = uint8_t( x % 2 ? 255 : 0 ); pixels.push_back( { v, v, v, 255 } ); }
    const auto path = files.Root / "Rectangular.tga"; Tga( path, 16, 2, pixels );
    TerrainTexturePaths paths{}; paths[0][0] = path.generic_string(); TerrainPackedTextures data; std::string error;
    REQUIRE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ) );
    for( size_t i = 0; i < data.Maps[0][0].Pixels.size(); i += 4 ) CHECK( data.Maps[0][0].Pixels[i] == 188 );
}


TEST_CASE( "Terrain textures: independent atlas cells keep layer colors isolated through the last padded mip" )
{
    Files files; TerrainTexturePaths paths{};
    const std::array<std::array<uint8_t, 4>, 4> colors{{ { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 255, 255 } }};
    for( unsigned layer = 0; layer < 4; ++layer )
    { const auto path = files.Root / ( std::to_string( layer ) + ".tga" ); Tga( path, 2, 2, std::vector<std::array<uint8_t, 4>>( 4, colors[layer] ) ); paths[layer][0] = path.generic_string(); }
    TerrainPackedTextures data; std::string error; REQUIRE_MESSAGE( PackTerrainTextures( paths, TerrainTextureLayout::Atlas, data, error, 16 ), error );
    CHECK( data.AtlasMaxMip == 2 );
    for( unsigned mip = 0; mip <= data.AtlasMaxMip; ++mip )
    {
        const auto& upload = data.Maps[0][mip]; const unsigned cell = 16 >> mip;
        for( unsigned layer = 0; layer < 4; ++layer ) for( unsigned z = 0; z < cell; ++z ) for( unsigned x = 0; x < cell; ++x )
            for( unsigned c = 0; c < 4; ++c ) CHECK( upload.Pixels[( ( ( layer / 2 ) * cell + z ) * upload.Width + ( layer % 2 ) * cell + x ) * 4 + c] == colors[layer][c] );
    }
}

TEST_CASE( "Terrain textures: every atlas mip wraps its own gutters instead of borrowing neighboring layers" )
{
    Files files; std::vector<std::array<uint8_t, 4>> pixels;
    for( unsigned z = 0; z < 12; ++z ) for( unsigned x = 0; x < 12; ++x ) pixels.push_back( { uint8_t( x * 20 ), uint8_t( z * 20 ), 0, 255 } );
    const auto path = files.Root / "Ramp.tga"; Tga( path, 12, 12, pixels ); TerrainTexturePaths paths{}; paths[0][0] = path.generic_string();
    TerrainPackedTextures data; std::string error; REQUIRE( PackTerrainTextures( paths, TerrainTextureLayout::Atlas, data, error, 16 ) );
    for( unsigned mip = 0; mip <= data.AtlasMaxMip; ++mip )
    {
        const auto& upload = data.Maps[0][mip]; const unsigned cell = 16 >> mip, gutter = cell >= 8 ? 2 : 1, interior = cell - gutter * 2;
        for( unsigned y = 0; y < cell; ++y ) for( unsigned x = 0; x < gutter; ++x ) for( unsigned c = 0; c < 4; ++c )
        {
            CHECK( upload.Pixels[( y * upload.Width + x ) * 4 + c] == upload.Pixels[( y * upload.Width + interior + x ) * 4 + c] );
            CHECK( upload.Pixels[( y * upload.Width + cell - gutter + x ) * 4 + c] == upload.Pixels[( y * upload.Width + gutter + x ) * 4 + c] );
        }
    }
}

TEST_CASE( "Terrain textures: failed decode and oversized sources preserve the previous pack and stamps detect changes" )
{
    Files files; TerrainTexturePaths paths{}; TerrainPackedTextures data; std::string error;
    REQUIRE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ) ); const auto size = data.Maps[0].size();
    paths[0][0] = ( files.Root / "Missing.tga" ).generic_string(); const auto missing = TerrainTextureStamp( paths );
    CHECK_FALSE( PackTerrainTextures( paths, TerrainTextureLayout::Atlas, data, error, 16 ) ); CHECK( data.Layout == TerrainTextureLayout::Array ); CHECK( data.Maps[0].size() == size );
    Tga( paths[0][0], 1, 1, { { 255, 0, 0, 255 } } ); CHECK( TerrainTextureStamp( paths ) != missing );
    Tga( paths[0][0], 4097, 1, {} ); CHECK_FALSE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ) ); CHECK( data.Maps[0].size() == size );
    CHECK_FALSE( PackTerrainTextures( {}, TerrainTextureLayout::Array, data, error, 7 ) );
}

TEST_CASE( "Terrain textures: surface version two persists map references parameters and reads version one defaults" )
{
    using namespace TerrainData; Surface source; Settings settings;
    auto& layer = source.Layers[1]; layer.BaseColorMap = "Assets/Textures/Soil.png"; layer.NormalMap = "Assets/Textures/Normal.png";
    layer.OrmMap = "Assets/Textures/ORM.tga"; layer.TileSize = 7.5f; layer.NormalStrength = 1.5f; layer.Metallic = 0.7f; layer.FlipNormalY = true;
    std::vector<uint8_t> bytes; std::string error; REQUIRE( SerializeSurface( source, settings, bytes, error ) ); Surface decoded;
    REQUIRE( DeserializeSurface( bytes, settings, decoded, error ) ); CHECK( decoded == source );
    const auto valid = bytes; source.Layers[1].BaseColorMap = "../Outside.png"; CHECK_FALSE( SerializeSurface( source, settings, bytes, error ) ); CHECK( bytes == valid );
    source.Layers[1].BaseColorMap.clear(); source.Layers[1].TileSize = std::numeric_limits<float>::quiet_NaN(); CHECK_FALSE( SerializeSurface( source, settings, bytes, error ) );
    const size_t length = static_cast<size_t>( Get( valid, 8, 4 ) ); auto value = json::parse( valid.begin() + 24, valid.begin() + 24 + length );
    for( auto& item : value["Layers"] ) for( const char* key : { "BaseColorMap", "NormalMap", "OrmMap", "TileSize", "NormalStrength", "Metallic", "FlipNormalY" } ) item.erase( key );
    const auto text = value.dump(); std::vector<uint8_t> legacy( 24 ); legacy[0] = 'M'; legacy[1] = 'T'; legacy[2] = 'S'; legacy[3] = 'F';
    Put( legacy, 4, 1, 4 ); Put( legacy, 8, text.size(), 4 ); legacy.insert( legacy.end(), text.begin(), text.end() ); Put( legacy, 16, Hash( legacy, 24 ), 8 );
    REQUIRE( DeserializeSurface( legacy, settings, decoded, error ) ); CHECK( decoded.Layers[1].TileSize == 4 ); CHECK( decoded.Layers[1].BaseColorMap.empty() ); CHECK_FALSE( decoded.Layers[1].FlipNormalY );
}


TEST_CASE( "Terrain textures: compiled DDS twins load without source images and replace source fallback" )
{
    Files files; TerrainTexturePaths paths{}; paths[0][0] = ( files.Root / "Source.tga" ).generic_string();
    Tga( paths[0][0], 1, 1, { { 0, 255, 0, 255 } } ); TerrainPackedTextures data; std::string error;
    REQUIRE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ) ); CHECK( data.Maps[0][0].Pixels[1] == 255 );
    const auto stamp = TerrainTextureStamp( paths );
    std::vector<uint8_t> dds( 128 ); dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
    Put( dds, 4, 124, 4 ); Put( dds, 8, 0x100f, 4 ); Put( dds, 12, 1, 4 ); Put( dds, 16, 1, 4 ); Put( dds, 20, 4, 4 );
    Put( dds, 76, 32, 4 ); Put( dds, 80, 0x41, 4 ); Put( dds, 88, 32, 4 ); Put( dds, 92, 0xff, 4 );
    Put( dds, 96, 0xff00, 4 ); Put( dds, 100, 0xff0000, 4 ); Put( dds, 104, 0xff000000, 4 ); Put( dds, 108, 0x1000, 4 );
    dds.insert( dds.end(), { 255, 0, 0, 255 } );
    { std::ofstream file( paths[0][0] + ".dds", std::ios::binary ); file.write( reinterpret_cast<const char*>( dds.data() ), dds.size() ); }
    CHECK( TerrainTextureStamp( paths ) != stamp );
    REQUIRE_MESSAGE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ), error );
    CHECK( data.Maps[0][0].Pixels[0] == 255 ); CHECK( data.Maps[0][0].Pixels[1] == 0 );
    std::filesystem::remove( paths[0][0] );
    REQUIRE( PackTerrainTextures( paths, TerrainTextureLayout::Array, data, error, 8 ) ); CHECK( data.Maps[0][0].Pixels[0] == 255 );
}

namespace TerrainTextureTests
{
    void SamePack( const TerrainPackedTextures& a, const TerrainPackedTextures& b )
    {
        CHECK( a.Layout == b.Layout ); CHECK( a.CellSize == b.CellSize ); CHECK( a.AtlasMaxMip == b.AtlasMaxMip );
        for( unsigned kind = 0; kind < 3; ++kind )
        {
            REQUIRE( a.Maps[kind].size() == b.Maps[kind].size() );
            for( size_t i = 0; i < a.Maps[kind].size(); ++i )
            {
                const auto& x = a.Maps[kind][i]; const auto& y = b.Maps[kind][i];
                CHECK( x.Layer == y.Layer ); CHECK( x.Mip == y.Mip ); CHECK( x.Width == y.Width ); CHECK( x.Height == y.Height ); CHECK( x.Pixels == y.Pixels );
            }
        }
    }


    std::vector<uint8_t> CacheBytes( const std::filesystem::path& path )
    {
        std::ifstream file( path, std::ios::binary ); return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
    }


    void CacheWrite( const std::filesystem::path& path, const std::vector<uint8_t>& bytes )
    {
        std::ofstream file( path, std::ios::binary | std::ios::trunc ); file.write( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
    }
}

TEST_CASE( "Terrain texture cache: cold and warm array/atlas packs exactly match ordinary packing" )
{
    Files files; TerrainTexturePaths paths{}; const auto path = files.Root / "Source.tga";
    Tga( path, 2, 2, { { 30, 80, 120, 255 }, { 200, 90, 40, 255 }, { 255, 0, 0, 255 }, { 0, 255, 0, 255 } } );
    paths[0][0] = paths[1][2] = path.generic_string(); TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache";
    TerrainTextureCacheInfo info; std::string error; std::filesystem::path previous;
    for( auto layout : { TerrainTextureLayout::Array, TerrainTextureLayout::Atlas } ) for( uint16_t cell : { 8, 16 } )
    {
        TerrainPackedTextures direct, cold, warm;
        REQUIRE( PackTerrainTextures( paths, layout, direct, error, cell ) );
        REQUIRE( LoadOrPackTerrainTextures( paths, layout, options, cold, error, info, cell ) );
        CHECK_FALSE( info.Hit ); CHECK( info.Stored ); CHECK( info.Warning.empty() ); CHECK( info.File != previous ); previous = info.File;
        REQUIRE( LoadOrPackTerrainTextures( paths, layout, options, warm, error, info, cell ) );
        CHECK( info.Hit ); CHECK_FALSE( info.Stored ); SamePack( direct, cold ); SamePack( direct, warm );
    }
}

TEST_CASE( "Terrain texture cache: contents invalidate preserved timestamps and identical relocated files reuse packs" )
{
    Files files; const auto path = files.Root / "Source.tga"; Tga( path, 1, 1, { { 255, 0, 0, 255 } } );
    TerrainTexturePaths paths{}; paths[0][0] = path.generic_string(); TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache";
    TerrainPackedTextures data; TerrainTextureCacheInfo info; std::string error;
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); const auto first = info.File;
    const auto time = std::filesystem::last_write_time( path ); Tga( path, 1, 1, { { 0, 255, 0, 255 } } ); std::filesystem::last_write_time( path, time );
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK_FALSE( info.Hit ); CHECK( info.File != first ); CHECK( data.Maps[0][0].Pixels[1] == 255 ); const auto second = info.File;
    const auto moved = files.Root / "Moved.tga"; std::filesystem::rename( path, moved ); paths[0][0] = moved.generic_string();
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK( info.Hit ); CHECK( info.File == second );
    paths[0][0].clear(); paths[1][0] = moved.generic_string();
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK_FALSE( info.Hit ); CHECK( info.File != second );
}

TEST_CASE( "Terrain texture cache: compiled selection and compiled-only inputs invalidate source fallback correctly" )
{
    Files files; const auto path = files.Root / "Source.tga"; Tga( path, 1, 1, { { 0, 255, 0, 255 } } );
    TerrainTexturePaths paths{}; paths[0][0] = path.generic_string(); TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache";
    TerrainPackedTextures data; TerrainTextureCacheInfo info; std::string error;
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); const auto source = info.File;
    std::vector<uint8_t> dds( 128 ); dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
    Put( dds, 4, 124, 4 ); Put( dds, 8, 0x100f, 4 ); Put( dds, 12, 1, 4 ); Put( dds, 16, 1, 4 ); Put( dds, 20, 4, 4 );
    Put( dds, 76, 32, 4 ); Put( dds, 80, 0x41, 4 ); Put( dds, 88, 32, 4 ); Put( dds, 92, 0xff, 4 ); Put( dds, 96, 0xff00, 4 );
    Put( dds, 100, 0xff0000, 4 ); Put( dds, 104, 0xff000000, 4 ); Put( dds, 108, 0x1000, 4 ); dds.insert( dds.end(), { 255, 0, 0, 255 } ); CacheWrite( path.string() + ".dds", dds );
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK_FALSE( info.Hit ); CHECK( info.File != source ); const auto compiled = info.File; CHECK( data.Maps[0][0].Pixels[0] == 255 );
    std::filesystem::remove( path );
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK( info.Hit ); CHECK( info.File == compiled );
    Tga( path, 1, 1, { { 0, 0, 255, 255 } } );
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK( info.Hit );
    std::filesystem::remove( path.string() + ".dds" );
    REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK_FALSE( info.Hit ); CHECK( data.Maps[0][0].Pixels[2] == 255 );
    const auto before = data; CacheWrite( path.string() + ".dds", { 'b', 'a', 'd' } );
    CHECK_FALSE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); SamePack( data, before );
    std::filesystem::remove( path.string() + ".dds" ); std::filesystem::remove( path );
    CHECK_FALSE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); SamePack( data, before );
}

TEST_CASE( "Terrain texture cache: corrupt oversized truncated and incompatible entries rebuild within fixed bounds" )
{
    Files files; TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache";
    TerrainPackedTextures data; TerrainTextureCacheInfo info; std::string error;
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Atlas, options, data, error, info, 16 ) );
    const auto expected = data; const auto path = info.File; const auto valid = CacheBytes( path );
    const auto repair = [&]() {
        REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Atlas, options, data, error, info, 16 ) );
        CHECK_FALSE( info.Hit ); CHECK( info.Stored ); CHECK_FALSE( info.Warning.empty() ); SamePack( data, expected );
        CHECK( CacheBytes( path ) == valid );
    };
    for( size_t offset : { 0u, 4u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 40u, 48u, 56u, 64u } )
    { auto broken = valid; broken[offset] ^= 0x20; CacheWrite( path, broken ); repair(); }
    auto broken = valid; broken.back() ^= 0x01; CacheWrite( path, broken ); repair();
    broken = valid; broken.resize( 63 ); CacheWrite( path, broken ); repair();
    broken = valid; broken.push_back( 0 ); CacheWrite( path, broken ); repair();
    std::filesystem::resize_file( path, 1024ull * 1024 * 1024 ); repair();
}

TEST_CASE( "Terrain texture cache: disabled undersized and unavailable storage preserve usable rendering data" )
{
    Files files; TerrainPackedTextures expected, data; std::string error; TerrainTextureCacheInfo info;
    REQUIRE( PackTerrainTextures( {}, TerrainTextureLayout::Array, expected, error, 8 ) );
    const auto blocker = files.Root / "Blocker"; CacheWrite( blocker, { 1 } );
    TerrainTextureCacheOptions options; options.Directory = blocker / "Cache";
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK_FALSE( info.Hit ); CHECK_FALSE( info.Stored ); CHECK_FALSE( info.Warning.empty() ); SamePack( data, expected );
    options.Directory = files.Root / "Cache"; options.Enabled = false;
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK_FALSE( info.Hit ); CHECK_FALSE( info.Stored ); CHECK( info.File.empty() ); CHECK_FALSE( std::filesystem::exists( options.Directory ) ); SamePack( data, expected );
    options.Enabled = true; options.MaxBytes = 1;
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK_FALSE( info.Stored ); CHECK_FALSE( std::filesystem::exists( options.Directory ) ); SamePack( data, expected );
    options.MaxBytes = 512ull << 20;
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); const auto file = info.File;
    std::filesystem::permissions( file, std::filesystem::perms::owner_read );
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK( info.Hit ); SamePack( data, expected );
    std::filesystem::permissions( file, std::filesystem::perms::owner_all );
}

TEST_CASE( "Terrain texture cache: least recently used packs retire within the disk allowance" )
{
    Files files; const auto path = files.Root / "Source.tga"; TerrainTexturePaths paths{}; paths[0][0] = path.generic_string();
    TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache"; TerrainTextureCacheInfo info; TerrainPackedTextures data; std::string error;
    Tga( path, 1, 1, { { 255, 0, 0, 255 } } ); REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    const auto first = info.File; options.MaxBytes = std::filesystem::file_size( first ) * 2;
    std::filesystem::last_write_time( first, std::filesystem::file_time_type::clock::now() - std::chrono::hours( 2 ) );
    Tga( path, 1, 1, { { 0, 255, 0, 255 } } ); REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); const auto second = info.File;
    std::filesystem::last_write_time( second, std::filesystem::file_time_type::clock::now() - std::chrono::hours( 1 ) );
    Tga( path, 1, 1, { { 0, 0, 255, 255 } } ); REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); const auto third = info.File;
    CHECK_FALSE( std::filesystem::exists( first ) ); CHECK( std::filesystem::exists( second ) );
    std::filesystem::last_write_time( third, std::filesystem::file_time_type::clock::now() - std::chrono::hours( 1 ) );
    Tga( path, 1, 1, { { 0, 255, 0, 255 } } ); REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) ); CHECK( info.Hit );
    Tga( path, 1, 1, { { 255, 255, 255, 255 } } ); REQUIRE( LoadOrPackTerrainTextures( paths, TerrainTextureLayout::Array, options, data, error, info, 8 ) );
    CHECK( std::filesystem::exists( second ) ); CHECK_FALSE( std::filesystem::exists( third ) );
    uint64_t bytes = 0; for( const auto& entry : std::filesystem::directory_iterator( options.Directory ) ) bytes += entry.file_size(); CHECK( bytes <= options.MaxBytes );
}

TEST_CASE( "Terrain texture cache: concurrent writers publish complete packs and leave no temporary files" )
{
    Files files; TerrainTextureCacheOptions options; options.Directory = files.Root / "Cache";
    struct Result { bool Success; TerrainPackedTextures Data; TerrainTextureCacheInfo Info; std::string Error; };
    std::vector<std::future<Result>> jobs;
    for( int n = 0; n < 8; ++n ) jobs.push_back( std::async( std::launch::async, [options] {
        Result result; result.Success = LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, result.Data, result.Error, result.Info, 16 ); return result;
    } ) );
    TerrainPackedTextures expected; std::string error; REQUIRE( PackTerrainTextures( {}, TerrainTextureLayout::Array, expected, error, 16 ) );
    for( auto& job : jobs ) { const auto result = job.get(); CHECK_MESSAGE( result.Success, result.Error ); SamePack( result.Data, expected ); }
    TerrainPackedTextures data; TerrainTextureCacheInfo info;
    REQUIRE( LoadOrPackTerrainTextures( {}, TerrainTextureLayout::Array, options, data, error, info, 16 ) ); CHECK( info.Hit ); SamePack( data, expected );
    for( const auto& entry : std::filesystem::directory_iterator( options.Directory ) ) CHECK( entry.path().extension() == ".tpack" );
}
