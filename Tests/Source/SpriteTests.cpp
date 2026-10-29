#include <doctest/doctest.h>
#include "RenderCommands.h"
#include "Components/Graphics/SpriteRenderer.h"
#include "Components/Transform.h"
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    bool Near( float a, float b ) { return std::abs( a - b ) < 1e-4f; }
}

TEST_CASE( "Sprites: quad corners follow size, pivot and the world transform" )
{
    Moonlight::SpriteCommand sprite;
    sprite.Size = glm::vec2( 2.f, 1.f );
    sprite.Pivot = glm::vec2( 0.f, 0.f );   // bottom left at the origin
    sprite.World = glm::translate( glm::mat4( 1.f ), glm::vec3( 10.f, 5.f, 0.f ) );
    Moonlight::SpriteVertex vertices[36];
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 4 );
    // Bottom left, bottom right, top right, top left.
    CHECK( Near( vertices[0].Position[0], 10.f ) );
    CHECK( Near( vertices[0].Position[1], 5.f ) );
    CHECK( Near( vertices[2].Position[0], 12.f ) );
    CHECK( Near( vertices[2].Position[1], 6.f ) );
    // The image's top row (v0) is at the top of the quad; the face looks down -Z.
    CHECK( Near( vertices[0].Uv[1], 1.f ) );
    CHECK( Near( vertices[3].Uv[1], 0.f ) );
    CHECK( Near( vertices[0].Normal[2], -1.f ) );

    // A centred pivot, an atlas region and a horizontal flip.
    sprite.Pivot = glm::vec2( 0.5f, 0.5f );
    sprite.World = glm::mat4( 1.f );
    sprite.UvRect = glm::vec4( 0.25f, 0.f, 0.5f, 0.5f );
    sprite.FlipX = true;
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 4 );
    CHECK( Near( vertices[0].Position[0], -1.f ) );
    CHECK( Near( vertices[0].Position[1], -0.5f ) );
    CHECK( Near( vertices[0].Uv[0], 0.5f ) );    // flipped: the left edge shows the region's right
    CHECK( Near( vertices[1].Uv[0], 0.25f ) );
    CHECK( Near( vertices[0].Uv[1], 0.5f ) );
}


TEST_CASE( "Sprites: 9-slice keeps border sizes while the middle stretches" )
{
    Moonlight::SpriteCommand sprite;
    sprite.Size = glm::vec2( 10.f, 4.f );
    sprite.Pivot = glm::vec2( 0.f, 0.f );
    sprite.Border = glm::vec4( 1.f, 0.5f, 2.f, 0.5f );         // left, top, right, bottom (world)
    sprite.BorderUv = glm::vec4( 0.25f, 0.25f, 0.25f, 0.25f );
    Moonlight::SpriteVertex vertices[36];
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 36 );
    // The first quad is the bottom-left corner: 1 wide, 0.5 tall, a quarter of the UVs.
    CHECK( Near( vertices[1].Position[0], 1.f ) );
    CHECK( Near( vertices[2].Position[1], 0.5f ) );
    CHECK( Near( vertices[1].Uv[0], 0.25f ) );
    CHECK( Near( vertices[2].Uv[1], 0.75f ) );
    // The middle column spans 1..8 (the right border is 2 wide).
    CHECK( Near( vertices[4].Position[0], 1.f ) );
    CHECK( Near( vertices[5].Position[0], 8.f ) );
    // Borders wider than the sprite shrink to fit, in proportion.
    sprite.Size = glm::vec2( 1.5f, 4.f );
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 36 );
    CHECK( Near( vertices[1].Position[0], 0.5f ) );
    CHECK( Near( vertices[5].Position[0], 0.5f ) );
}


TEST_CASE( "Sprites: SpriteRenderer commands (size, tint, sorting) without a texture" )
{
    SpriteRenderer renderer;
    Transform transform;
    renderer.Tint = Vector3( 1.f, 0.5f, 0.f );
    renderer.Opacity = 0.5f;
    renderer.SortingLayer = 2;
    renderer.OrderInLayer = -3;
    const Moonlight::SpriteCommand command = renderer.BuildCommand( transform );
    // No texture: a unit square, white (invalid handle), the tint linearized.
    CHECK( Near( command.Size.x, 1.f ) );
    CHECK( Near( command.Size.y, 1.f ) );
    CHECK_FALSE( bgfx::isValid( command.Texture ) );
    CHECK( Near( command.Color.r, 1.f ) );
    CHECK( command.Color.g < 0.5f );
    CHECK( Near( command.Color.a, 0.5f ) );
    CHECK( command.Layer == 2 );
    CHECK( command.Order == -3 );
    CHECK( Near( command.Bounds.Min.x, -0.5f ) );
    CHECK( Near( command.Bounds.Max.y, 0.5f ) );
    // An explicit size wins.
    renderer.Size = Vector2( 3.f, 2.f );
    CHECK( Near( renderer.BuildCommand( transform ).Size.x, 3.f ) );
}


TEST_CASE( "Sprites: tangents follow the image's axes, flips included (normal maps, LOOK-S9)" )
{
    Moonlight::SpriteCommand sprite;
    Moonlight::SpriteVertex vertices[4];
    // Unflipped: +u along +x; cross(normal, tangent) * w is the image's up (+y).
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 4 );
    const glm::vec3 normal( vertices[0].Normal[0], vertices[0].Normal[1], vertices[0].Normal[2] );
    glm::vec3 tangent( vertices[0].Tangent[0], vertices[0].Tangent[1], vertices[0].Tangent[2] );
    glm::vec3 up = glm::cross( normal, tangent ) * vertices[0].Tangent[3];
    CHECK( Near( tangent.x, 1.f ) );
    CHECK( Near( up.y, 1.f ) );
    // Flipped both ways: +u runs along -x and the image's up along -y.
    sprite.FlipX = sprite.FlipY = true;
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 4 );
    tangent = glm::vec3( vertices[0].Tangent[0], vertices[0].Tangent[1], vertices[0].Tangent[2] );
    up = glm::cross( normal, tangent ) * vertices[0].Tangent[3];
    CHECK( Near( tangent.x, -1.f ) );
    CHECK( Near( up.y, -1.f ) );
    // Rotated a quarter turn about z: the tangent turns with it.
    sprite.FlipX = sprite.FlipY = false;
    sprite.World = glm::rotate( glm::mat4( 1.f ), glm::radians( 90.f ), glm::vec3( 0.f, 0.f, 1.f ) );
    REQUIRE( Moonlight::BuildSpriteVertices( sprite, vertices ) == 4 );
    CHECK( Near( vertices[0].Tangent[1], 1.f ) );
}
