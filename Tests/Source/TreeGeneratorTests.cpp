#include <doctest/doctest.h>

#include "Procedural/Tree/TreeGenerator.h"
#include "Procedural/Tree/TreePresets.h"
#include "Procedural/Tree/TreeAsset.h"
#include "Procedural/Tree/TreeSkeleton.h"
#include "Path.h"
#include <SDL_stdinc.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace TreeGeneratorTest
{
    float Length( const Vector3& InVector )
    {
        return std::sqrt( InVector.x * InVector.x + InVector.y * InVector.y + InVector.z * InVector.z );
    }


    bool Finite( const Vector3& InVector )
    {
        return std::isfinite( InVector.x ) && std::isfinite( InVector.y ) && std::isfinite( InVector.z );
    }


    MitchTree::TreeRecipe Preset( const std::string& InName )
    {
        MitchTree::TreeRecipe recipe;
        REQUIRE( MitchTree::MakePreset( InName, recipe ) );
        return recipe;
    }
}


TEST_CASE( "MitchTree: every preset generates valid, budgeted geometry" )
{
    using namespace TreeGeneratorTest;
    for( const std::string& name : MitchTree::PresetNames() )
    {
        CAPTURE( name );
        const MitchTree::TreeRecipe recipe = Preset( name );
        const MitchTree::TreeMesh mesh = MitchTree::Generate( recipe );
        MESSAGE( name << ": " << mesh.Statistics.Stems << " stems, " << mesh.Statistics.Leaves << " leaves, "
            << mesh.Statistics.Triangles.front() << " triangles, " << mesh.Statistics.Milliseconds << " ms" );
        REQUIRE( mesh.FindPart( "Bark" ) );
        CHECK( ( mesh.FindPart( "Leaves" ) != nullptr ) == ( recipe.Leaves.Kind != MitchTree::LeafKind::None ) );
        CHECK( mesh.Statistics.Stems > 1u );
        CHECK( mesh.LodScreenHeights.size() == static_cast<size_t>( recipe.Lod.Levels ) );
        // Roughly as tall as asked (lean, bend and drooping fronds change it a little).
        CHECK( mesh.Height > recipe.Trunk.Height * 0.6f );
        CHECK( mesh.Height < recipe.Trunk.Height * 1.6f + 1.f );
        CHECK( mesh.Statistics.Triangles.front() < 160000u );
        CHECK( mesh.Statistics.Triangles.back() * 3u < mesh.Statistics.Triangles.front() );
        for( size_t lod = 1; lod < mesh.Statistics.Triangles.size(); ++lod )
        {
            CHECK( mesh.Statistics.Triangles[lod] <= mesh.Statistics.Triangles[lod - 1] );
        }
        for( const MitchTree::TreePart& part : mesh.Parts )
        {
            CAPTURE( part.Name );
            REQUIRE( part.Lods.size() == static_cast<size_t>( recipe.Lod.Levels + 1 ) );
            for( const std::vector<uint32_t>& indices : part.Lods )
            {
                REQUIRE( !indices.empty() );
                CHECK( indices.size() % 3 == 0 );
                bool inRange = true;
                for( uint32_t index : indices ) inRange &= index < part.Vertices.size();
                CHECK( inRange );
            }
            bool valid = true, inBounds = true;
            for( const MitchTree::TreeVertex& vertex : part.Vertices )
            {
                valid &= Finite( vertex.Position ) && Finite( vertex.Normal ) && Finite( vertex.Tangent ) && Finite( vertex.Bitangent );
                valid &= std::fabs( Length( vertex.Normal ) - 1.f ) < 1e-3f && std::fabs( Length( vertex.Tangent ) - 1.f ) < 1e-3f;
                valid &= std::isfinite( vertex.UV.x ) && std::isfinite( vertex.UV.y );
                inBounds &= vertex.Position.x >= mesh.BoundsMin.x && vertex.Position.y >= mesh.BoundsMin.y && vertex.Position.z >= mesh.BoundsMin.z;
                inBounds &= vertex.Position.x <= mesh.BoundsMax.x && vertex.Position.y <= mesh.BoundsMax.y && vertex.Position.z <= mesh.BoundsMax.z;
            }
            CHECK( valid );
            CHECK( inBounds );
        }
        // Sway reaches full scale somewhere; the crown is darker inside than out.
        uint8_t maxSway = 0, minOcclusion = 255;
        for( const MitchTree::TreePart& part : mesh.Parts )
        {
            for( const MitchTree::TreeVertex& vertex : part.Vertices )
            {
                maxSway = std::max( maxSway, vertex.Wind[2] );
                minOcclusion = std::min( minOcclusion, vertex.Wind[3] );
            }
        }
        CHECK( maxSway == 255 );
        if( recipe.Leaves.Kind != MitchTree::LeafKind::None ) CHECK( minOcclusion < 200 );
        // Generous: Debug builds, shared CI machines.
        CHECK( mesh.Statistics.Milliseconds < 5000.0 );
    }
}


TEST_CASE( "MitchTree: generation is deterministic and seeded" )
{
    const MitchTree::TreeRecipe recipe = TreeGeneratorTest::Preset( "Maple" );
    const MitchTree::TreeMesh a = MitchTree::Generate( recipe );
    const MitchTree::TreeMesh b = MitchTree::Generate( recipe );
    REQUIRE( a.Parts.size() == b.Parts.size() );
    for( size_t p = 0; p < a.Parts.size(); ++p )
    {
        REQUIRE( a.Parts[p].Vertices.size() == b.Parts[p].Vertices.size() );
        CHECK( std::memcmp( a.Parts[p].Vertices.data(), b.Parts[p].Vertices.data(), a.Parts[p].Vertices.size() * sizeof( MitchTree::TreeVertex ) ) == 0 );
        CHECK( a.Parts[p].Lods == b.Parts[p].Lods );
    }
    MitchTree::TreeRecipe reseeded = recipe;
    reseeded.Seed += 1;
    const MitchTree::TreeMesh c = MitchTree::Generate( reseeded );
    CHECK( ( c.Parts[0].Vertices.size() != a.Parts[0].Vertices.size() || std::memcmp( c.Parts[0].Vertices.data(), a.Parts[0].Vertices.data(), a.Parts[0].Vertices.size() * sizeof( MitchTree::TreeVertex ) ) != 0 ) );
}


TEST_CASE( "MitchTree: editing a deeper level keeps the trunk and limbs" )
{
    MitchTree::TreeRecipe recipe = TreeGeneratorTest::Preset( "Oak" );
    const MitchTree::Detail::Skeleton before = MitchTree::Detail::BuildSkeleton( recipe );
    recipe.Branches[2].Count += 5;
    recipe.Branches[1].Gnarl += 0.2f;
    const MitchTree::Detail::Skeleton after = MitchTree::Detail::BuildSkeleton( recipe );
    size_t compared = 0;
    for( size_t i = 0; i < before.Stems.size() && before.Stems[i].Level <= 1; ++i )
    {
        REQUIRE( i < after.Stems.size() );
        const auto& x = before.Stems[i];
        const auto& y = after.Stems[i];
        REQUIRE( x.Id == y.Id );
        REQUIRE( x.Nodes.size() == y.Nodes.size() );
        for( size_t n = 0; n < x.Nodes.size(); ++n )
        {
            CHECK( x.Nodes[n].Position.x == y.Nodes[n].Position.x );
            CHECK( x.Nodes[n].Position.y == y.Nodes[n].Position.y );
            CHECK( x.Nodes[n].Radius == y.Nodes[n].Radius );
        }
        ++compared;
    }
    CHECK( compared > 10 );
}


TEST_CASE( "MitchTree: a lone trunk is a closed tube apart from its buried base" )
{
    MitchTree::TreeRecipe recipe;
    recipe.Trunk.Height = 6.f;
    recipe.Trunk.Gnarl = 0.3f;
    recipe.Leaves.Kind = MitchTree::LeafKind::None;
    recipe.Lod.Levels = 2;
    const MitchTree::TreeMesh mesh = MitchTree::Generate( recipe );
    const MitchTree::TreePart* bark = mesh.FindPart( "Bark" );
    REQUIRE( bark );
    for( const std::vector<uint32_t>& indices : bark->Lods )
    {
        // Count triangle edges by quantized position (the seam duplicates vertices).
        using Key = std::tuple<int64_t, int64_t, int64_t>;
        auto key = [&]( uint32_t index ) {
            const Vector3& p = bark->Vertices[index].Position;
            return Key( std::llround( p.x * 1e4 ), std::llround( p.y * 1e4 ), std::llround( p.z * 1e4 ) );
        };
        std::map<std::pair<Key, Key>, int> edges;
        for( size_t t = 0; t < indices.size(); t += 3 )
        {
            for( int e = 0; e < 3; ++e )
            {
                Key a = key( indices[t + e] ), b = key( indices[t + ( e + 1 ) % 3] );
                if( b < a ) std::swap( a, b );
                ++edges[{ a, b }];
            }
        }
        const float baseY = -recipe.Trunk.SinkDepth;
        for( const auto& [edge, uses] : edges )
        {
            if( uses == 2 ) continue;
            // Open edges may only lie on the buried base ring.
            const bool onBase = std::fabs( static_cast<float>( std::get<1>( edge.first ) ) * 1e-4f - baseY ) < 0.05f && std::fabs( static_cast<float>( std::get<1>( edge.second ) ) * 1e-4f - baseY ) < 0.05f;
            CHECK( onBase );
            CHECK( uses == 1 );
        }
    }
}


TEST_CASE( "MitchTree: recipes round-trip through .mtree JSON" )
{
    for( const std::string& name : MitchTree::PresetNames() )
    {
        CAPTURE( name );
        const MitchTree::TreeRecipe recipe = TreeGeneratorTest::Preset( name );
        const json saved = recipe.ToJson();
        CHECK( saved["Format"] == "MitchTree" );
        MitchTree::TreeRecipe loaded;
        std::string error;
        REQUIRE( loaded.FromJson( json::parse( saved.dump() ), error ) );
        CHECK( loaded.ToJson() == saved );
    }
    MitchTree::TreeRecipe recipe;
    std::string error;
    CHECK_FALSE( recipe.FromJson( json::object(), error ) );
    CHECK_FALSE( recipe.FromJson( json{ { "Format", "MitchTree" }, { "Version", 99 } }, error ) );
    // Out-of-range values are clamped rather than trusted.
    json hostile = MitchTree::TreeRecipe().ToJson();
    hostile["Trunk"]["Height"] = 1e9;
    hostile["Lod"]["Levels"] = 40;
    REQUIRE( recipe.FromJson( hostile, error ) );
    CHECK( recipe.Trunk.Height <= 80.f );
    CHECK( recipe.Lod.Levels <= 4 );
}


// Engine/Assets/Trees/Presets mirrors the code presets (the editor creates new trees from the code;
// the files are there to open, place and build scenes with). Run with ME_TREE_PRESETS_UPDATE=1 to
// rewrite them.
TEST_CASE( "MitchTree: preset assets match the code presets" )
{
    const bool update = SDL_getenv( "ME_TREE_PRESETS_UPDATE" ) != nullptr;
    const std::filesystem::path folder = std::filesystem::path( Path( "Engine/Assets" ).FullPath ) / "Trees" / "Presets";
    if( !std::filesystem::exists( folder.parent_path().parent_path() ) )
    {
        return;
    }
    if( update ) std::filesystem::create_directories( folder );
    for( const std::string& name : MitchTree::PresetNames() )
    {
        CAPTURE( name );
        const MitchTree::TreeRecipe recipe = TreeGeneratorTest::Preset( name );
        const std::filesystem::path file = folder / ( name + ".mtree" );
        std::string error;
        if( update )
        {
            CHECK_MESSAGE( MitchTree::SaveRecipe( file.string(), recipe, error ), error );
            continue;
        }
        MitchTree::TreeRecipe saved;
        REQUIRE_MESSAGE( MitchTree::LoadRecipe( file.string(), saved, error ), error );
        CHECK_MESSAGE( saved.ToJson() == recipe.ToJson(), "out of date: run the tests with ME_TREE_PRESETS_UPDATE=1" );
    }
}
