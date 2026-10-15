#include <doctest/doctest.h>
#include "Components/Graphics/Decal.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "Reflection/Reflection.h"
#include <cmath>
#include <memory>

namespace
{
    std::shared_ptr<World> MakeDecalWorld()
    {
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        return world;
    }

    glm::vec3 Apply( const glm::mat4& InMatrix, const glm::vec3& InPoint )
    {
        return glm::vec3( InMatrix * glm::vec4( InPoint, 1.f ) );
    }

    bool Inside( const glm::vec3& InLocal )
    {
        return std::abs( InLocal.x ) <= 0.5f && std::abs( InLocal.y ) <= 0.5f && std::abs( InLocal.z ) <= 0.5f;
    }
}

TEST_CASE( "Decal: the projection box follows the transform and Size" )
{
    auto world = MakeDecalWorld();
    EntityHandle entity = world->CreateEntity( "Decal" );
    Transform& transform = entity->AddComponent<Transform>();
    Decal& decal = entity->AddComponent<Decal>();
    transform.SetPosition( Vector3( 1.f, 2.f, 3.f ) );
    transform.SetRotation( Vector3( 90.f, 0.f, 0.f ) );   // projecting straight down
    decal.Size = Vector3( 2.f, 4.f, 0.5f );

    const Moonlight::DecalCommand command = decal.BuildCommand( transform );
    const glm::vec3 center = Apply( command.World, glm::vec3( 0.f ) );
    CHECK( center.x == doctest::Approx( 1.f ).epsilon( 1e-4 ) );
    CHECK( center.y == doctest::Approx( 2.f ).epsilon( 1e-4 ) );
    CHECK( center.z == doctest::Approx( 3.f ).epsilon( 1e-4 ) );

    // The box projects along the entity's forward (down here); the texture's up is its Y (+Z here).
    const glm::vec3 projector( command.Axes[2] );
    const glm::vec3 up( command.Axes[1] );
    const glm::vec3 right( command.Axes[0] );
    CHECK( projector.y == doctest::Approx( -1.f ).epsilon( 1e-4 ) );
    CHECK( up.z == doctest::Approx( 1.f ).epsilon( 1e-4 ) );
    CHECK( right.x == doctest::Approx( 1.f ).epsilon( 1e-4 ) );

    // Half of Size to each face: 1 m across, 2 m up the texture, 0.25 m deep.
    CHECK( glm::length( Apply( command.World, glm::vec3( 0.5f, 0.f, 0.f ) ) - center ) == doctest::Approx( 1.f ).epsilon( 1e-4 ) );
    CHECK( glm::length( Apply( command.World, glm::vec3( 0.f, 0.5f, 0.f ) ) - center ) == doctest::Approx( 2.f ).epsilon( 1e-4 ) );
    CHECK( glm::length( Apply( command.World, glm::vec3( 0.f, 0.f, 0.5f ) ) - center ) == doctest::Approx( 0.25f ).epsilon( 1e-4 ) );

    // WorldToLocal undoes World, so the shader's inside test sees the same box.
    const glm::vec3 roundTrip = Apply( command.WorldToLocal, Apply( command.World, glm::vec3( 0.3f, -0.2f, 0.4f ) ) );
    CHECK( roundTrip.x == doctest::Approx( 0.3f ).epsilon( 1e-4 ) );
    CHECK( roundTrip.y == doctest::Approx( -0.2f ).epsilon( 1e-4 ) );
    CHECK( roundTrip.z == doctest::Approx( 0.4f ).epsilon( 1e-4 ) );
    CHECK( Inside( Apply( command.WorldToLocal, glm::vec3( 1.5f, 1.9f, 4.f ) ) ) );
    CHECK_FALSE( Inside( Apply( command.WorldToLocal, glm::vec3( 2.5f, 2.f, 3.f ) ) ) );
    CHECK_FALSE( Inside( Apply( command.WorldToLocal, glm::vec3( 1.f, 2.4f, 3.f ) ) ) );

    // Culling bounds enclose the rotated box.
    CHECK( command.WorldBounds.Min.x == doctest::Approx( 0.f ).epsilon( 1e-4 ) );
    CHECK( command.WorldBounds.Max.x == doctest::Approx( 2.f ).epsilon( 1e-4 ) );
    CHECK( command.WorldBounds.Min.y == doctest::Approx( 1.75f ).epsilon( 1e-4 ) );
    CHECK( command.WorldBounds.Max.y == doctest::Approx( 2.25f ).epsilon( 1e-4 ) );
    CHECK( command.WorldBounds.Min.z == doctest::Approx( 1.f ).epsilon( 1e-4 ) );
    CHECK( command.WorldBounds.Max.z == doctest::Approx( 5.f ).epsilon( 1e-4 ) );

    // The entity's own scale multiplies in.
    transform.SetScale( Vector3( 2.f, 1.f, 1.f ) );
    const Moonlight::DecalCommand scaled = decal.BuildCommand( transform );
    CHECK( glm::length( Apply( scaled.World, glm::vec3( 0.5f, 0.f, 0.f ) ) - center ) == doctest::Approx( 2.f ).epsilon( 1e-4 ) );

    // A flat box keeps a millimetre on each axis, so it still inverts.
    decal.Size = Vector3( 0.f, 0.f, 0.f );
    const Moonlight::DecalCommand flat = decal.BuildCommand( transform );
    CHECK( std::abs( glm::determinant( flat.World ) ) > 0.f );
    for( int column = 0; column < 4; ++column )
    {
        for( int row = 0; row < 4; ++row )
        {
            CHECK( std::isfinite( flat.WorldToLocal[column][row] ) );
        }
    }
}

TEST_CASE( "Decal: surface, colour and fade parameters" )
{
    auto world = MakeDecalWorld();
    EntityHandle entity = world->CreateEntity( "Decal" );
    Transform& transform = entity->AddComponent<Transform>();
    Decal& decal = entity->AddComponent<Decal>();
    decal.Color = Vector3( 1.f, 0.5f, 0.f );
    decal.Opacity = 0.5f;
    decal.EmissiveColor = Vector3( 1.f, 1.f, 1.f );
    decal.EmissiveIntensity = 3.f;
    decal.Metallic = -1.f;
    decal.Roughness = 2.f;
    decal.NormalStrength = 0.75f;
    decal.AngleFade = 60.f;
    decal.AngleFadeWidth = 20.f;
    decal.SortOrder = 7;

    const Moonlight::DecalCommand command = decal.BuildCommand( transform );
    // sRGB colours go linear; opacity rides in alpha.
    CHECK( command.Color[0] == doctest::Approx( 1.f ) );
    CHECK( command.Color[1] == doctest::Approx( 0.214f ).epsilon( 0.01 ) );
    CHECK( command.Color[2] == doctest::Approx( 0.f ) );
    CHECK( command.Color[3] == doctest::Approx( 0.5f ) );
    CHECK( command.Emissive[0] == doctest::Approx( 3.f ) );
    CHECK( command.Surface[0] == doctest::Approx( 0.f ) );
    CHECK( command.Surface[1] == doctest::Approx( 1.f ) );
    CHECK( command.Surface[2] == doctest::Approx( 0.75f ) );
    CHECK( command.SortOrder == 7 );

    // Full coverage up to 40 degrees from the projection, none past 60.
    CHECK( command.Emissive[3] == doctest::Approx( std::cos( 40.f * 3.14159265f / 180.f ) ).epsilon( 1e-4 ) );
    CHECK( command.Surface[3] == doctest::Approx( 0.5f ).epsilon( 1e-4 ) );
    decal.AngleFade = 10.f;
    const Moonlight::DecalCommand narrow = decal.BuildCommand( transform );
    CHECK( narrow.Emissive[3] == doctest::Approx( 1.f ) );

    // Without textures there's nothing to wait for: a solid tint, flat normals.
    CHECK( decal.Resolve() );
    CHECK_FALSE( bgfx::isValid( command.Albedo ) );
    CHECK_FALSE( bgfx::isValid( command.Normal ) );
}

TEST_CASE( "Decal: fields round-trip through reflection" )
{
    Decal source;
    source.Size = Vector3( 3.f, 2.f, 0.5f );
    source.Color = Vector3( 0.2f, 0.4f, 0.6f );
    source.Roughness = 0.1f;
    source.AngleFade = 45.f;
    source.SortOrder = -2;
    json data;
    Reflection::ToJson( Decal::StaticType(), &source, data );

    Decal loaded;
    Reflection::FromJson( Decal::StaticType(), &loaded, data );
    CHECK( loaded.Size.y == doctest::Approx( 2.f ) );
    CHECK( loaded.Color.z == doctest::Approx( 0.6f ) );
    CHECK( loaded.Roughness == doctest::Approx( 0.1f ) );
    CHECK( loaded.AngleFade == doctest::Approx( 45.f ) );
    CHECK( loaded.SortOrder == -2 );
}
