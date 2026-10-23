#include <doctest/doctest.h>

#include "Components/Graphics/WindZone.h"
#include "Components/Transform.h"
#include "Materials/FoliageMaterial.h"
#include "Procedural/Tree/TreeGenerator.h"
#include "Procedural/Tree/TreePresets.h"
#include <cmath>

TEST_CASE( "FoliageMaterial: foliage settings round trip through JSON" )
{
    FoliageSettings source;
    source.Mode = FoliageSettings::Part::Leaves;
    source.Translucency = 0.7f;
    source.Variation = 0.25f;
    source.TreeHeight = 14.5f;
    source.TrunkLean = 0.02f;
    source.BranchSway = 0.45f;
    source.LeafFlutter = 0.05f;
    source.WindFrequency = 1.5f;
    json saved;
    source.Write( saved );
    CHECK( saved["Part"] == "Leaves" );

    FoliageSettings restored;
    restored.Read( saved );
    CHECK( restored.Mode == FoliageSettings::Part::Leaves );
    CHECK( restored.Translucency == 0.7f );
    CHECK( restored.Variation == 0.25f );
    CHECK( restored.TreeHeight == 14.5f );
    CHECK( restored.TrunkLean == 0.02f );
    CHECK( restored.BranchSway == 0.45f );
    CHECK( restored.LeafFlutter == 0.05f );
    CHECK( restored.WindFrequency == 1.5f );
    json again;
    restored.Write( again );
    CHECK( again == saved );

    // Garbage keeps sane values rather than poisoning the shader.
    json broken = saved;
    broken["TreeHeight"] = -3.f;
    broken["BranchSway"] = "far";
    FoliageSettings clamped;
    clamped.Read( broken );
    CHECK( clamped.TreeHeight == doctest::Approx( 0.1f ) );
    CHECK( clamped.BranchSway == FoliageSettings().BranchSway );
}


TEST_CASE( "FoliageMaterial: equal settings batch together, different wind or parts do not" )
{
    FoliageSettings leaves;
    leaves.Mode = FoliageSettings::Part::Leaves;
    leaves.Translucency = 0.6f;
    leaves.BranchSway = 0.3f;
    const uint64_t seed = 0x1234u;
    const FoliageSettings copy = leaves;
    CHECK( copy.Hash( seed ) == leaves.Hash( seed ) );
    CHECK( leaves.Hash( seed ) != leaves.Hash( seed + 1 ) );

    FoliageSettings bark = leaves;
    bark.Mode = FoliageSettings::Part::Bark;
    CHECK( bark.Hash( seed ) != leaves.Hash( seed ) );
    FoliageSettings stiffer = leaves;
    stiffer.BranchSway = 0.1f;
    CHECK( stiffer.Hash( seed ) != leaves.Hash( seed ) );
    FoliageSettings taller = leaves;
    taller.TreeHeight = 20.f;
    CHECK( taller.Hash( seed ) != leaves.Hash( seed ) );
}


TEST_CASE( "WindZone: blows horizontally along the entity's forward axis" )
{
    WindZone zone;
    zone.Strength = 1.5f;
    zone.Gusts = 0.8f;
    zone.GustFrequency = 0.3f;
    zone.Turbulence = 0.4f;
    Transform transform;
    // Pitched down and turned: the wind stays level.
    transform.SetRotation( Vector3( 35.f, 90.f, 0.f ) );
    const Moonlight::WindCommand wind = zone.BuildCommand( transform );
    CHECK( wind.Direction.y == 0.f );
    CHECK( std::sqrt( wind.Direction.x * wind.Direction.x + wind.Direction.z * wind.Direction.z ) == doctest::Approx( 1.f ) );
    Vector3 forward = transform.Front();
    forward.y = 0.f;
    forward = forward.Normalized();
    CHECK( wind.Direction.x == doctest::Approx( forward.x ) );
    CHECK( wind.Direction.z == doctest::Approx( forward.z ) );
    CHECK( wind.Strength == 1.5f );
    CHECK( wind.Gusts == 0.8f );
    CHECK( wind.GustFrequency == 0.3f );
    CHECK( wind.Turbulence == 0.4f );

    // Straight down keeps the default heading; negative or NaN values are calm.
    transform.SetRotation( Vector3( 90.f, 0.f, 0.f ) );
    zone.Strength = -2.f;
    zone.Gusts = std::nanf( "" );
    const Moonlight::WindCommand down = zone.BuildCommand( transform );
    CHECK( down.Direction.x == Moonlight::WindCommand().Direction.x );
    CHECK( down.Direction.z == Moonlight::WindCommand().Direction.z );
    CHECK( down.Strength == 0.f );
    CHECK( down.Gusts == 0.f );
}


TEST_CASE( "MitchTree: the wind stream weights leaves, branches and the trunk" )
{
    MitchTree::TreeRecipe recipe;
    REQUIRE( MitchTree::MakePreset( "Oak", recipe ) );
    const MitchTree::TreeMesh mesh = MitchTree::Generate( recipe );
    CHECK( mesh.MaxSway > 0.f );
    const MitchTree::TreePart* bark = mesh.FindPart( "Bark" );
    const MitchTree::TreePart* leaves = mesh.FindPart( "Leaves" );
    REQUIRE( bark );
    REQUIRE( leaves );
    // Bark never flutters; the trunk base is rigid while some twig sways fully.
    int barkSwayMax = 0, barkFlutterMax = 0;
    for( const MitchTree::TreeVertex& vertex : bark->Vertices )
    {
        barkSwayMax = std::max<int>( barkSwayMax, vertex.Wind[2] );
        barkFlutterMax = std::max<int>( barkFlutterMax, vertex.Wind[0] );
    }
    CHECK( barkFlutterMax == 0 );
    CHECK( barkSwayMax >= 250 );
    CHECK( bark->Vertices.front().Wind[2] == 0 );
    // Leaf tips flutter most, the corners beside the stalk least.
    int flutterMin = 255, flutterMax = 0;
    for( const MitchTree::TreeVertex& vertex : leaves->Vertices )
    {
        flutterMin = std::min<int>( flutterMin, vertex.Wind[0] );
        flutterMax = std::max<int>( flutterMax, vertex.Wind[0] );
    }
    CHECK( flutterMin < flutterMax / 2 );
    CHECK( flutterMax >= 200 );
}
