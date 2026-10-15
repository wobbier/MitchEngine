#include "PCH.h"
#include "Light.h"
#include "Components/Transform.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( Moonlight::LightType, { { "Directional", Moonlight::LightType::Directional }, { "Point", Moonlight::LightType::Point }, { "Spot", Moonlight::LightType::Spot }, { "Bar", Moonlight::LightType::Bar }, { "Box", Moonlight::LightType::Box }, { "Rectangle", Moonlight::LightType::Rectangle } } )

ME_REFLECT_BEGIN( Light )
    ME_FIELD( LightType ).Display( "Type" );
    ME_FIELD( Color ).Color();
    ME_FIELD( Intensity ).Range( 0.f, 100.f ).Speed( 0.05f );
    ME_FIELD( Range ).Display( "Range (m)" ).Range( 0.01f, 1000.f ).Tooltip( "Local lights: fade distance from the source; bar/box/rectangle: measured from the emitter surface" );
    ME_FIELD( InnerConeAngle ).Range( 0.f, 89.f ).Tooltip( "Spot: full intensity inside this half-angle (degrees)" );
    ME_FIELD( OuterConeAngle ).Range( 0.f, 89.f ).Tooltip( "Spot: no light outside this half-angle (degrees)" );
    ME_FIELD( Length ).Display( "Bar Length (m)" ).Range( 0.f, 1000.f ).Speed( 0.01f ).Tooltip( "Bar: segment length along local X, before transform scale; rounded ends extend by Radius" );
    ME_FIELD( Radius ).Display( "Bar Radius (m)" ).Range( 0.001f, 100.f ).Speed( 0.001f ).Tooltip( "Bar: fluorescent tube radius before transform scale" );
    ME_FIELD( Size ).Display( "Box Size (m)" ).Range( 0.001f, 1000.f ).Speed( 0.01f ).Tooltip( "Box: full emitter dimensions on local X/Y/Z, before transform scale; emits in all directions" );
    ME_FIELD( RectangleWidth ).Display( "Rectangle Width (m)" ).Range( 0.001f, 1000.f ).Speed( 0.01f ).Tooltip( "Rectangle: face width along local X before transform scale; emits only forwards (+Z)" );
    ME_FIELD( RectangleHeight ).Display( "Rectangle Height (m)" ).Range( 0.001f, 1000.f ).Speed( 0.01f ).Tooltip( "Rectangle: face height along local Y before transform scale; cosine falloff towards the edges of the forward hemisphere" );
    ME_FIELD( CastShadows ).Category( "Shadows" ).Tooltip( "Directional, point and spot lights support shadows; bar, box and rectangle lights are unshadowed" );
    ME_FIELD( ShadowBias ).Category( "Shadows" ).Range( 0.f, 5.f ).Speed( 0.01f ).Tooltip( "Shadow texels the receiver is pushed towards the light (fixes acne)" );
    ME_FIELD( ShadowNormalBias ).Category( "Shadows" ).Range( 0.f, 5.f ).Speed( 0.01f ).Tooltip( "Shadow texels the receiver is pushed along its normal (fixes acne on slopes)" );
    ME_FIELD( ShadowDistance ).Display( "Shadow Distance (m)" ).Category( "Shadows" ).Range( 1.f, 1000.f ).Tooltip( "Directional: how far from the camera shadows reach" );
ME_REFLECT_END()


Light::Light()
    : Component( "Light" )
{
}


void Light::Init()
{
}


void Light::OnDeserialize( const json& inJson )
{
    if( inJson.contains( "LightType" ) && inJson["LightType"] == "Square" )
    {
        // Preserve scenes saved with the original square-face controls.
        json migrated = inJson;
        migrated["LightType"] = "Rectangle";
        const float side = inJson.contains( "SquareSize" ) && inJson["SquareSize"].is_number() ? inJson["SquareSize"].get<float>() : 1.f;
        if( !migrated.contains( "RectangleWidth" ) ) migrated["RectangleWidth"] = side;
        if( !migrated.contains( "RectangleHeight" ) ) migrated["RectangleHeight"] = side;
        Reflection::FromJson( StaticType(), this, migrated );
    }
    else
    {
        Reflection::FromJson( StaticType(), this, inJson );
    }
    // Light components saved before light types existed were plain colours.
    if( !inJson.contains( "LightType" ) && inJson.contains( "Colour" ) )
    {
        Color = Vector3( inJson["Colour"][0].get<float>(), inJson["Colour"][1].get<float>(), inJson["Colour"][2].get<float>() );
    }
}


Moonlight::LightCommand Light::BuildCommand( const Transform& InTransform ) const
{
    auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
    Transform& transform = const_cast<Transform&>( InTransform );

    Moonlight::LightCommand command;
    command.Type = LightType;
    command.Position = transform.GetWorldPosition();
    command.Direction = transform.Front().Normalized();
    command.Color = Vector3( toLinear( Color.x ), toLinear( Color.y ), toLinear( Color.z ) ) * Intensity;
    command.Range = std::max( Range, 0.01f );
    const float outer = std::clamp( OuterConeAngle, 0.1f, 89.f );
    const float inner = std::clamp( InnerConeAngle, 0.f, outer - 0.05f );
    command.CosOuter = std::cos( outer * 3.14159265f / 180.f );
    command.CosInner = std::cos( inner * 3.14159265f / 180.f );
    const bool extended = LightType == Moonlight::LightType::Bar || LightType == Moonlight::LightType::Box || LightType == Moonlight::LightType::Rectangle;
    if( extended )
    {
        command.AxisX = transform.Right().Normalized();
        command.AxisY = transform.Up().Normalized();
        command.AxisZ = command.Direction;
        const Vector3 scale = transform.GetWorldScale();
        if( LightType == Moonlight::LightType::Bar )
        {
            const float radius = std::max( Radius, 0.001f ) * std::max( scale.y, scale.z );
            command.HalfSize = Vector3( std::max( Length, 0.f ) * scale.x * 0.5f, radius, radius );
        }
        else if( LightType == Moonlight::LightType::Rectangle )
        {
            command.HalfSize = Vector3( std::max( RectangleWidth, 0.001f ) * scale.x * 0.5f,
                std::max( RectangleHeight, 0.001f ) * scale.y * 0.5f, 0.f );
        }
        else
        {
            command.HalfSize = Vector3( std::max( Size.x, 0.001f ) * scale.x, std::max( Size.y, 0.001f ) * scale.y, std::max( Size.z, 0.001f ) * scale.z ) * 0.5f;
        }
    }
    command.CastShadows = CastShadows && !extended;
    command.ShadowBias = ShadowBias;
    command.ShadowNormalBias = ShadowNormalBias;
    command.ShadowDistance = std::max( ShadowDistance, 1.f );
    command.OuterConeAngle = outer;
    return command;
}
