#include <doctest/doctest.h>
#include "Graphics/ModelResource.h"
#include "Path.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <filesystem>
#include <limits>

#if USING( ME_TOOLS )

namespace ModelImportTest
{
    struct Extents
    {
        aiVector3D Min{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        aiVector3D Max{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

        float Height() const
        {
            return Max.y - Min.y;
        }
    };

    void Accumulate( const aiScene& InScene, const aiNode& InNode, const aiMatrix4x4& InParent, Extents& OutExtents )
    {
        const aiMatrix4x4 world = InParent * InNode.mTransformation;
        for( unsigned int m = 0; m < InNode.mNumMeshes; ++m )
        {
            const aiMesh& mesh = *InScene.mMeshes[InNode.mMeshes[m]];
            for( unsigned int v = 0; v < mesh.mNumVertices; ++v )
            {
                const aiVector3D p = world * mesh.mVertices[v];
                OutExtents.Min = aiVector3D( std::min( OutExtents.Min.x, p.x ), std::min( OutExtents.Min.y, p.y ), std::min( OutExtents.Min.z, p.z ) );
                OutExtents.Max = aiVector3D( std::max( OutExtents.Max.x, p.x ), std::max( OutExtents.Max.y, p.y ), std::max( OutExtents.Max.z, p.z ) );
            }
        }
        for( unsigned int c = 0; c < InNode.mNumChildren; ++c )
        {
            Accumulate( InScene, *InNode.mChildren[c], world, OutExtents );
        }
    }

    // The world-space extents of the cooked model at InPath.
    Extents CookedExtents( const std::string& InPath )
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile( InPath + ".assbin", 0 );
        Extents extents;
        if( scene && scene->mRootNode )
        {
            Accumulate( *scene, *scene->mRootNode, aiMatrix4x4(), extents );
        }
        return extents;
    }
}

using namespace ModelImportTest;

TEST_CASE( "Models: import scale and unit conversion are baked into the cooked model" )
{
    namespace fs = std::filesystem;
    const std::string source = "Assets/Models/Synty/ExplorerKit/FBX/SM_Prop_Torch_01.fbx";
    if( !fs::exists( source ) )
    {
        MESSAGE( "skipped: the Synty sample isn't in this checkout" );
        return;
    }
    fs::create_directories( ".tmp/Tests/Models" );
    const std::string path = ".tmp/Tests/Models/Torch.fbx";
    fs::copy_file( source, path, fs::copy_options::overwrite_existing );

    ModelResourceMetadata meta{ Path( path ) };
    meta.Export();
    const Extents raw = CookedExtents( path );
    REQUIRE( raw.Height() > 10.f );     // authored in centimetres: about a hundred units tall

    meta.ConvertUnits = true;
    meta.Export();
    const Extents metres = CookedExtents( path );
    CHECK( metres.Height() == doctest::Approx( raw.Height() * 0.01f ).epsilon( 0.001 ) );
    CHECK( metres.Min.x == doctest::Approx( raw.Min.x * 0.01f ).epsilon( 0.001 ) );

    meta.ImportScale = 2.f;
    meta.Export();
    CHECK( CookedExtents( path ).Height() == doctest::Approx( raw.Height() * 0.02f ).epsilon( 0.001 ) );

    // The settings round-trip through the .meta JSON (defaults aren't written).
    json saved;
    meta.OnSerialize( saved );
    ModelResourceMetadata loaded{ Path( path ) };
    loaded.OnDeserialize( saved );
    CHECK( loaded.ConvertUnits );
    CHECK( loaded.ImportScale == doctest::Approx( 2.f ) );
    json defaults;
    ModelResourceMetadata{ Path( path ) }.OnSerialize( defaults );
    CHECK_FALSE( defaults.contains( "ImportScale" ) );
    CHECK( meta.GetBakedScale( 0.0 ) == doctest::Approx( 2.f ) );   // no units in the file: metres
    fs::remove_all( ".tmp/Tests/Models" );
}

#endif
