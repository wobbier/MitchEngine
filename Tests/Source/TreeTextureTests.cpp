#include <doctest/doctest.h>

#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreePresets.h"
#include "Procedural/Tree/TreeTextures.h"
#include "Path.h"
#include <bimg/bimg.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace TreeTextureTest
{
    // Mean absolute difference between two columns (or rows) of an RGBA8 image.
    double EdgeDifference( const MitchTree::Image& InImage, bool InColumns, int InA, int InB )
    {
        double sum = 0.0;
        const int count = InColumns ? InImage.Height : InImage.Width;
        for( int i = 0; i < count; ++i )
        {
            const int ax = InColumns ? InA : i, ay = InColumns ? i : InA, bx = InColumns ? InB : i, by = InColumns ? i : InB;
            for( int c = 0; c < 3; ++c )
            {
                sum += std::abs( InImage.Pixels[( static_cast<size_t>( ay ) * InImage.Width + ax ) * 4 + c] - InImage.Pixels[( static_cast<size_t>( by ) * InImage.Width + bx ) * 4 + c] );
            }
        }
        return sum / ( count * 3.0 );
    }


    double AlphaCoverage( const uint8_t* InPixels, uint32_t InWidth, uint32_t InHeight, float InCutoff )
    {
        size_t covered = 0;
        for( size_t i = 0; i < static_cast<size_t>( InWidth ) * InHeight; ++i ) covered += InPixels[i * 4 + 3] / 255.f > InCutoff;
        return static_cast<double>( covered ) / ( static_cast<double>( InWidth ) * InHeight );
    }
}


TEST_CASE( "MitchTree textures: every bark style tiles seamlessly" )
{
    using namespace TreeTextureTest;
    for( MitchTree::BarkStyle style : { MitchTree::BarkStyle::Furrowed, MitchTree::BarkStyle::Plated, MitchTree::BarkStyle::Smooth,
             MitchTree::BarkStyle::Birch, MitchTree::BarkStyle::Ringed, MitchTree::BarkStyle::Weathered } )
    {
        CAPTURE( static_cast<int>( style ) );
        MitchTree::BarkTexture settings;
        settings.Style = style;
        settings.Resolution = 128;
        const MitchTree::BarkImages bark = MitchTree::GenerateBark( settings );
        REQUIRE( bark.Color.Width == 128 );
        REQUIRE( bark.Normal.Pixels.size() == bark.Color.Pixels.size() );
        // The wrap seam differs no more than two neighbouring interior columns/rows do.
        const double seamX = EdgeDifference( bark.Color, true, 0, 127 ), interiorX = EdgeDifference( bark.Color, true, 63, 64 );
        const double seamY = EdgeDifference( bark.Color, false, 0, 127 ), interiorY = EdgeDifference( bark.Color, false, 63, 64 );
        CHECK( seamX < interiorX * 2.5 + 2.0 );
        CHECK( seamY < interiorY * 2.5 + 2.0 );
        // Not a flat fill.
        CHECK( EdgeDifference( bark.Color, true, 10, 70 ) > 1.0 );
    }
}


TEST_CASE( "MitchTree textures: leaf atlases are deterministic and fill every cell" )
{
    using namespace TreeTextureTest;
    for( MitchTree::LeafShape shape : { MitchTree::LeafShape::Oval, MitchTree::LeafShape::Lobed, MitchTree::LeafShape::Palmate,
             MitchTree::LeafShape::Serrated, MitchTree::LeafShape::Round, MitchTree::LeafShape::Needles, MitchTree::LeafShape::Frond } )
    {
        CAPTURE( static_cast<int>( shape ) );
        MitchTree::LeafTexture settings;
        settings.Shape = shape;
        settings.Resolution = 256;
        const MitchTree::LeafImages a = MitchTree::GenerateLeaves( settings );
        const MitchTree::LeafImages b = MitchTree::GenerateLeaves( settings );
        CHECK( a.Color.Pixels == b.Color.Pixels );
        const std::vector<MitchTree::LeafRegion> regions = MitchTree::GeneratedLeafRegions( settings );
        if( shape == MitchTree::LeafShape::Frond )
        {
            CHECK( regions.empty() );
            CHECK( AlphaCoverage( a.Color.Pixels.data(), 256, 256, 0.5f ) > 0.15 );
            continue;
        }
        REQUIRE( regions.size() == static_cast<size_t>( settings.Columns * settings.Rows ) );
        for( const MitchTree::LeafRegion& region : regions )
        {
            // Some leaf in each cell, and the stalk at its pivot.
            const int x0 = static_cast<int>( region.Rect.x * 256 ), y0 = static_cast<int>( region.Rect.y * 256 );
            const int x1 = static_cast<int>( region.Rect.z * 256 ), y1 = static_cast<int>( region.Rect.w * 256 );
            size_t covered = 0;
            for( int y = y0; y < y1; ++y ) for( int x = x0; x < x1; ++x ) covered += a.Color.Pixels[( static_cast<size_t>( y ) * 256 + x ) * 4 + 3] > 128;
            CHECK( static_cast<double>( covered ) / ( ( x1 - x0 ) * ( y1 - y0 ) ) > 0.04 );
            const int px = static_cast<int>( region.Pivot.x * 256 ), py = static_cast<int>( region.Pivot.y * 256 ) - 2;
            CHECK( a.Color.Pixels[( static_cast<size_t>( py ) * 256 + px ) * 4 + 3] > 64 );
        }
    }
}


TEST_CASE( "MitchTree textures: baked channels are mipmapped and keep leaf coverage" )
{
    using namespace TreeTextureTest;
    const std::filesystem::path folder = std::filesystem::path( Path( ".tmp" ).FullPath ) / "Tests" / "TreeTextures";
    std::filesystem::create_directories( folder );
    MitchTree::TreeRecipe recipe;
    REQUIRE( MitchTree::MakePreset( "Oak", recipe ) );
    recipe.LeafTextures.Resolution = 256;
    recipe.BarkTextures.Resolution = 128;
    const std::string file = ( folder / "Coverage.mtree" ).string();
    std::string error;
    REQUIRE( MitchTree::SaveRecipe( file, recipe, error ) );
    const std::string local = Path( file ).GetLocalPathString();
    for( const std::string& channel : MitchTree::TextureChannels() )
    {
        CAPTURE( channel );
        bimg::ImageContainer* image = MitchTree::BakeChannel( local, channel, error );
        REQUIRE_MESSAGE( image, error );
        CHECK( image->m_numMips > 4 );
        if( channel == "leaf-color" )
        {
            bimg::ImageMip full, small;
            REQUIRE( bimg::imageGetRawData( *image, 0, 0, image->m_data, image->m_size, full ) );
            REQUIRE( bimg::imageGetRawData( *image, 0, 4, image->m_data, image->m_size, small ) );
            const double fullCoverage = AlphaCoverage( full.m_data, full.m_width, full.m_height, recipe.Foliage.AlphaCutoff );
            const double smallCoverage = AlphaCoverage( small.m_data, small.m_width, small.m_height, recipe.Foliage.AlphaCutoff );
            MESSAGE( "coverage " << fullCoverage << " full, " << smallCoverage << " at mip 4" );
            CHECK( std::abs( smallCoverage - fullCoverage ) < 0.08 );
        }
        bimg::imageFree( image );
    }
    CHECK_FALSE( MitchTree::BakeChannel( local, "nonsense", error ) );
    // Exporting writes ordinary PNGs.
    const MitchTree::BarkImages bark = MitchTree::GenerateBark( recipe.BarkTextures );
    CHECK( MitchTree::WritePng( bark.Color, ( folder / "Bark.png" ).string(), error ) );
    CHECK( std::filesystem::file_size( folder / "Bark.png" ) > 1000 );
}


// Writes every preset's generated maps to .tmp/TreeTextures for a look (ME_TREE_TEXTURE_EXPORT=1).
TEST_CASE( "MitchTree textures: export preset maps" )
{
    if( !std::getenv( "ME_TREE_TEXTURE_EXPORT" ) ) return;
    const std::filesystem::path folder = std::filesystem::path( Path( ".tmp" ).FullPath ) / "TreeTextures";
    std::filesystem::create_directories( folder );
    for( const std::string& name : MitchTree::PresetNames() )
    {
        MitchTree::TreeRecipe recipe;
        REQUIRE( MitchTree::MakePreset( name, recipe ) );
        std::string error;
        const MitchTree::BarkImages bark = MitchTree::GenerateBark( recipe.BarkTextures );
        CHECK( MitchTree::WritePng( bark.Color, ( folder / ( name + "_BarkColor.png" ) ).string(), error ) );
        CHECK( MitchTree::WritePng( bark.Normal, ( folder / ( name + "_BarkNormal.png" ) ).string(), error ) );
        if( recipe.Leaves.Kind == MitchTree::LeafKind::None ) continue;
        const MitchTree::LeafImages leaves = MitchTree::GenerateLeaves( recipe.LeafTextures );
        CHECK( MitchTree::WritePng( leaves.Color, ( folder / ( name + "_LeafColor.png" ) ).string(), error ) );
        CHECK( MitchTree::WritePng( leaves.Normal, ( folder / ( name + "_LeafNormal.png" ) ).string(), error ) );
    }
}
