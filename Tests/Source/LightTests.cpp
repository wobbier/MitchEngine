#include <doctest/doctest.h>
#include "Components/Lighting/Light.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <cmath>

TEST_CASE( "Light: extended emitter properties round-trip through component serialization" )
{
    for( Moonlight::LightType type : { Moonlight::LightType::Bar, Moonlight::LightType::Box, Moonlight::LightType::Rectangle } )
    {
        Light source;
        source.LightType = type;
        source.Length = 2.4f;
        source.Radius = 0.018f;
        source.Size = Vector3( 3.f, 2.f, 1.f );
        source.RectangleWidth = 2.5f;
        source.RectangleHeight = 0.75f;
        json saved;
        source.Serialize( saved );
        const char* name = type == Moonlight::LightType::Bar ? "Bar" : type == Moonlight::LightType::Box ? "Box" : "Rectangle";
        CHECK( saved["LightType"] == name );

        Light restored;
        restored.Deserialize( saved );
        CHECK( restored.LightType == type );
        CHECK( restored.Length == doctest::Approx( 2.4f ) );
        CHECK( restored.Radius == doctest::Approx( 0.018f ) );
        CHECK( ( restored.Size == source.Size ) );
        CHECK( restored.RectangleWidth == doctest::Approx( 2.5f ) );
        CHECK( restored.RectangleHeight == doctest::Approx( 0.75f ) );
    }
}

TEST_CASE( "Light: legacy scenes keep their type and default emitter dimensions" )
{
    Light light;
    light.Deserialize( json { { "LightType", "Spot" }, { "Range", 5.f }, { "Intensity", 8.f } } );
    CHECK( light.LightType == Moonlight::LightType::Spot );
    CHECK( light.Range == 5.f );
    CHECK( light.Length == doctest::Approx( 1.2f ) );
    CHECK( light.Radius == doctest::Approx( 0.025f ) );
    CHECK( light.RectangleWidth == doctest::Approx( 2.f ) );
    CHECK( light.RectangleHeight == doctest::Approx( 1.f ) );

    light.Deserialize( json { { "Colour", { 0.2f, 0.4f, 0.6f } } } );
    CHECK( ( light.Color == Vector3( 0.2f, 0.4f, 0.6f ) ) );
}

TEST_CASE( "Light: saved square emitters migrate to rectangle dimensions" )
{
    Light light;
    light.Deserialize( json { { "LightType", "Square" }, { "SquareSize", 2.5f } } );
    CHECK( light.LightType == Moonlight::LightType::Rectangle );
    CHECK( light.RectangleWidth == doctest::Approx( 2.5f ) );
    CHECK( light.RectangleHeight == doctest::Approx( 2.5f ) );
    json saved;
    light.Serialize( saved );
    CHECK( saved["LightType"] == "Rectangle" );
    CHECK_FALSE( saved.contains( "SquareSize" ) );

    light.Deserialize( json { { "LightType", "Square" } } );
    CHECK( light.RectangleWidth == doctest::Approx( 1.f ) );
    CHECK( light.RectangleHeight == doctest::Approx( 1.f ) );

    light.Deserialize( json { { "LightType", "Square" }, { "SquareSize", 3.f }, { "RectangleWidth", 4.f }, { "RectangleHeight", 2.f } } );
    CHECK( light.RectangleWidth == doctest::Approx( 4.f ) );
    CHECK( light.RectangleHeight == doctest::Approx( 2.f ) );
}

TEST_CASE( "Light: extended emitters follow parent position rotation and scale" )
{
    World world;
    world.IsLoading = false;
    EntityHandle parent = world.CreateEntity();
    Transform& parentTransform = parent->AddComponent<Transform>();
    parentTransform.SetPosition( Vector3( 4.f, 5.f, 6.f ) );
    parentTransform.SetRotation( Vector3( 0.f, 0.f, 90.f ) );
    parentTransform.SetScale( Vector3( 2.f, 3.f, 4.f ) );

    EntityHandle child = world.CreateEntity();
    Transform& transform = child->AddComponent<Transform>();
    transform.SetParent( parentTransform );
    transform.SetScale( Vector3( -2.f, 1.f, 0.5f ) );

    Light light;
    light.LightType = Moonlight::LightType::Bar;
    light.Length = 2.f;
    light.Radius = 0.1f;
    light.Range = 7.f;
    light.CastShadows = true;
    Moonlight::LightCommand bar = light.BuildCommand( transform );
    CHECK( ( bar.Position == Vector3( 4.f, 5.f, 6.f ) ) );
    CHECK( bar.AxisX.x == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( bar.AxisX.y == doctest::Approx( 1.f ) );
    CHECK( bar.HalfSize.x == doctest::Approx( 4.f ) );
    CHECK( bar.HalfSize.y == doctest::Approx( 0.3f ) );
    CHECK( bar.Range == 7.f );
    CHECK_FALSE( bar.CastShadows );

    light.LightType = Moonlight::LightType::Box;
    light.Size = Vector3( 2.f, 4.f, 6.f );
    Moonlight::LightCommand box = light.BuildCommand( transform );
    CHECK( box.HalfSize.x == doctest::Approx( 4.f ) );
    CHECK( box.HalfSize.y == doctest::Approx( 6.f ) );
    CHECK( box.HalfSize.z == doctest::Approx( 6.f ) );
    CHECK( box.AxisX.Dot( box.AxisY ) == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( box.AxisZ.Length() == doctest::Approx( 1.f ) );
    CHECK_FALSE( box.CastShadows );

    light.LightType = Moonlight::LightType::Rectangle;
    light.RectangleWidth = 2.f;
    light.RectangleHeight = 4.f;
    Moonlight::LightCommand rectangle = light.BuildCommand( transform );
    CHECK( ( rectangle.Position == bar.Position ) );
    CHECK( rectangle.HalfSize.x == doctest::Approx( 4.f ) );
    CHECK( rectangle.HalfSize.y == doctest::Approx( 6.f ) );
    CHECK( rectangle.HalfSize.z == 0.f );
    CHECK( rectangle.AxisX.y == doctest::Approx( 1.f ) );
    CHECK( rectangle.AxisY.x == doctest::Approx( -1.f ) );
    CHECK( rectangle.Direction.z == doctest::Approx( 1.f ) );
    CHECK( rectangle.Direction.Dot( rectangle.AxisX ) == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( rectangle.Direction.Dot( rectangle.AxisY ) == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( rectangle.GetBoundingRadius() == doctest::Approx( 7.f + std::sqrt( 52.f ) ) );
    CHECK_FALSE( rectangle.CastShadows );
}

TEST_CASE( "Light: rectangle emission direction follows world rotation and ignores depth scale" )
{
    Transform transform;
    transform.SetRotation( Vector3( 90.f, 0.f, 0.f ) );
    transform.SetScale( Vector3( 2.f, 2.f, -10.f ) );
    Light light;
    light.LightType = Moonlight::LightType::Rectangle;
    light.RectangleWidth = 3.f;
    light.RectangleHeight = 1.5f;
    const Moonlight::LightCommand rectangle = light.BuildCommand( transform );
    CHECK( rectangle.Direction.x == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( rectangle.Direction.y == doctest::Approx( -1.f ) );
    CHECK( rectangle.Direction.z == doctest::Approx( 0.f ).epsilon( 1e-5 ) );
    CHECK( rectangle.HalfSize.x == doctest::Approx( 3.f ) );
    CHECK( rectangle.HalfSize.y == doctest::Approx( 1.5f ) );
    CHECK( rectangle.HalfSize.z == 0.f );
}

TEST_CASE( "Light: invalid dimensions stay finite and point spot shadows remain available" )
{
    Transform transform;
    Light light;
    light.LightType = Moonlight::LightType::Bar;
    light.Length = -2.f;
    light.Radius = -1.f;
    light.Range = 0.f;
    light.CastShadows = true;
    Moonlight::LightCommand command = light.BuildCommand( transform );
    CHECK( command.HalfSize.x == 0.f );
    CHECK( command.HalfSize.y > 0.f );
    CHECK( command.Range > 0.f );
    CHECK( std::isfinite( command.GetBoundingRadius() ) );

    light.LightType = Moonlight::LightType::Box;
    light.Size = Vector3( -1.f, 0.f, 2.f );
    command = light.BuildCommand( transform );
    CHECK( command.HalfSize.x > 0.f );
    CHECK( command.HalfSize.y > 0.f );
    CHECK( command.HalfSize.z == 1.f );

    light.LightType = Moonlight::LightType::Rectangle;
    light.RectangleWidth = -1.f;
    light.RectangleHeight = 0.f;
    command = light.BuildCommand( transform );
    CHECK( command.HalfSize.x > 0.f );
    CHECK( command.HalfSize.y > 0.f );
    CHECK( command.HalfSize.z == 0.f );
    CHECK( std::isfinite( command.GetBoundingRadius() ) );
    CHECK_FALSE( command.CastShadows );

    for( Moonlight::LightType type : { Moonlight::LightType::Point, Moonlight::LightType::Spot, Moonlight::LightType::Directional } )
    {
        light.LightType = type;
        CHECK( light.BuildCommand( transform ).CastShadows );
    }
}
