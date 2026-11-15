#include <doctest/doctest.h>

#include "Graphics/Material.h"
#include "Graphics/MaterialRevision.h"
#include "Graphics/Texture.h"
#include "Materials/FoliageMaterial.h"
#include "Materials/ShaderGraphMaterial.h"
#include "Materials/StandardMaterial.h"
#include <functional>
#include <string>
#include <vector>

// ME-399: a material's revision moves with every change a retained mesh command takes from it
// (RenderCore skips re-reading materials while no revision moved), and only with real changes.

namespace MaterialRevisionTest
{
    struct Edit
    {
        const char* Name;
        std::function<void( StandardMaterial& )> Apply;
    };

    // Every StandardMaterial setter, each to a value that differs from the default.
    std::vector<Edit> StandardEdits()
    {
        return {
            { "SetRenderMode", []( StandardMaterial& m ) { m.SetRenderMode( Moonlight::RenderingMode::Transparent ); } },
            { "SetDiffuseColor", []( StandardMaterial& m ) { m.SetDiffuseColor( Vector3( 0.2f, 0.3f, 0.4f ) ); } },
            { "SetTiling", []( StandardMaterial& m ) { m.SetTiling( Vector2( 2.f, -1.f ) ); } },
            { "SetOffset", []( StandardMaterial& m ) { m.SetOffset( Vector2( 0.f, 1.f ) ); } },
            { "SetFaceMode", []( StandardMaterial& m ) { m.SetFaceMode( Moonlight::RenderFaceMode::Both ); } },
            { "SetBlendMode", []( StandardMaterial& m ) { m.SetRenderMode( Moonlight::RenderingMode::Transparent ); m.SetBlendMode( Moonlight::BlendMode::Additive ); } },
            { "SetSupportsInstancing", []( StandardMaterial& m ) { m.SetSupportsInstancing( false ); } },
            { "SetOpacity", []( StandardMaterial& m ) { m.SetOpacity( 0.5f ); } },
            { "SetMetallic", []( StandardMaterial& m ) { m.SetMetallic( 1.f ); } },
            { "SetRoughness", []( StandardMaterial& m ) { m.SetRoughness( 0.1f ); } },
            { "SetNormalStrength", []( StandardMaterial& m ) { m.SetNormalStrength( 2.f ); } },
            { "SetOcclusionStrength", []( StandardMaterial& m ) { m.SetOcclusionStrength( 0.25f ); } },
            { "SetEmissiveColor", []( StandardMaterial& m ) { m.SetEmissiveColor( Vector3( 1.f, 0.5f, 0.f ) ); } },
            { "SetEmissiveIntensity", []( StandardMaterial& m ) { m.SetEmissiveIntensity( 4.f ); } },
            { "SetAlphaCutoff", []( StandardMaterial& m ) { m.SetAlphaCutoff( 0.5f ); } },
            { "SetInvertRoughness", []( StandardMaterial& m ) { m.SetInvertRoughness( true ); } },
            { "SetTexture", []( StandardMaterial& m ) { m.SetTexture( Moonlight::TextureType::Opacity, MakeShared<Moonlight::Texture>( Path( "" ) ) ); } },
            { "LoadShader", []( StandardMaterial& m ) { m.LoadShader( "" ); } },
        };
    }
}


TEST_CASE( "Material revision: every setter that changes a value bumps it, and the global count" )
{
    for( const MaterialRevisionTest::Edit& edit : MaterialRevisionTest::StandardEdits() )
    {
        CAPTURE( edit.Name );
        StandardMaterial material;
        const uint32_t before = material.GetRevision();
        const uint64_t changes = Moonlight::MaterialRevision::GetChangeCount();
        const uint64_t drawState = material.GetDrawStateHash();
        edit.Apply( material );
        CHECK( material.GetRevision() != before );
        CHECK( Moonlight::MaterialRevision::GetChangeCount() != changes );
        // Each one changes something a mesh command takes from the material (LoadShader("") aside:
        // it only drops the shader, which is not loaded in tests either way).
        if( std::string( edit.Name ) != "LoadShader" )
        {
            CHECK( material.GetDrawStateHash() != drawState );
        }
    }
}


TEST_CASE( "Material revision: setting the value a field already has is not a change" )
{
    for( const MaterialRevisionTest::Edit& edit : MaterialRevisionTest::StandardEdits() )
    {
        const std::string name = edit.Name;
        if( name == "LoadShader" || name == "SetTexture" )
        {
            continue;   // a new shader or texture object each time
        }
        CAPTURE( edit.Name );
        StandardMaterial material;
        edit.Apply( material );
        const uint32_t settled = material.GetRevision();
        edit.Apply( material );
        CHECK( material.GetRevision() == settled );
    }

    StandardMaterial material;
    auto texture = MakeShared<Moonlight::Texture>( Path( "" ) );
    material.SetTexture( Moonlight::TextureType::Diffuse, texture );
    const uint32_t settled = material.GetRevision();
    material.SetTexture( Moonlight::TextureType::Diffuse, texture );
    CHECK( material.GetRevision() == settled );
    material.SetTexture( Moonlight::TextureType::Diffuse, nullptr );
    CHECK( material.GetRevision() != settled );
}


TEST_CASE( "Material revision: bulk writes bump it" )
{
    StandardMaterial source;
    source.SetDiffuseColor( Vector3( 0.1f, 0.2f, 0.3f ) );
    source.SetMetallic( 0.7f );
    json saved;
    source.OnSerialize( saved );

    StandardMaterial loaded;
    uint32_t before = loaded.GetRevision();
    loaded.OnDeserialize( saved );
    CHECK( loaded.GetRevision() != before );
    CHECK( loaded.GetInstanceBatchKey() == source.GetInstanceBatchKey() );

    StandardMaterial copied;
    before = copied.GetRevision();
    copied.CopyValues( &source );
    CHECK( copied.GetRevision() != before );

    before = loaded.GetRevision();
    loaded.ClearTextures();
    CHECK( loaded.GetRevision() != before );

    // A copy is a new material (a new pointer), so it keeps the value; assigning over one changes it.
    StandardMaterial copy( source );
    CHECK( copy.GetRevision() == source.GetRevision() );
    Moonlight::MaterialRevision revision;
    Moonlight::MaterialRevision other;
    other.Bump();
    const uint32_t assignedBefore = revision.Get();
    revision = other;
    CHECK( revision.Get() != assignedBefore );

    before = source.GetRevision();
    source.MarkChanged();
    CHECK( source.GetRevision() != before );
}


TEST_CASE( "Material revision: foliage settings and shader graphs" )
{
    FoliageMaterial foliage;
    uint32_t before = foliage.GetRevision();
    FoliageSettings settings = foliage.Foliage;
    settings.BranchSway = 1.25f;
    foliage.SetFoliage( settings );
    CHECK( foliage.GetRevision() != before );

    json saved;
    foliage.OnSerialize( saved );
    FoliageMaterial loaded;
    before = loaded.GetRevision();
    loaded.OnDeserialize( saved );
    CHECK( loaded.GetRevision() != before );
    CHECK( loaded.GetInstanceBatchKey() == foliage.GetInstanceBatchKey() );

    ShaderGraphMaterial graph;
    before = graph.GetRevision();
    graph.SetGraph( "" );
    CHECK( graph.GetRevision() != before );
}


TEST_CASE( "Material revision: external changes move both counts" )
{
    const uint64_t changes = Moonlight::MaterialRevision::GetChangeCount();
    const uint64_t external = Moonlight::MaterialRevision::GetExternalChangeCount();
    Moonlight::MaterialRevision::MarkExternalChange();
    CHECK( Moonlight::MaterialRevision::GetChangeCount() != changes );
    CHECK( Moonlight::MaterialRevision::GetExternalChangeCount() != external );

    // A material's own change is not an external one.
    StandardMaterial material;
    const uint64_t externalAfter = Moonlight::MaterialRevision::GetExternalChangeCount();
    material.SetRoughness( 0.9f );
    CHECK( Moonlight::MaterialRevision::GetExternalChangeCount() == externalAfter );
}
