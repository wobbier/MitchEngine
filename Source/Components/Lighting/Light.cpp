#include "PCH.h"
#include "Light.h"
#include "Components/Transform.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( Moonlight::LightType, { { "Directional", Moonlight::LightType::Directional }, { "Point", Moonlight::LightType::Point }, { "Spot", Moonlight::LightType::Spot } } )

ME_REFLECT_BEGIN( Light )
    ME_FIELD( LightType ).Display( "Type" );
    ME_FIELD( Color ).Color();
    ME_FIELD( Intensity ).Range( 0.f, 100.f ).Speed( 0.05f );
    ME_FIELD( Range ).Range( 0.01f, 1000.f ).Tooltip( "Point/spot: distance where the light fades out" );
    ME_FIELD( InnerConeAngle ).Range( 0.f, 89.f ).Tooltip( "Spot: full intensity inside this half-angle (degrees)" );
    ME_FIELD( OuterConeAngle ).Range( 0.f, 89.f ).Tooltip( "Spot: no light outside this half-angle (degrees)" );
    ME_FIELD( CastShadows ).Category( "Shadows" );
    ME_FIELD( ShadowBias ).Category( "Shadows" ).Range( 0.f, 5.f ).Speed( 0.01f ).Tooltip( "Shadow texels the receiver is pushed towards the light (fixes acne)" );
    ME_FIELD( ShadowNormalBias ).Category( "Shadows" ).Range( 0.f, 5.f ).Speed( 0.01f ).Tooltip( "Shadow texels the receiver is pushed along its normal (fixes acne on slopes)" );
    ME_FIELD( ShadowDistance ).Category( "Shadows" ).Range( 1.f, 1000.f ).Tooltip( "Directional: how far from the camera shadows reach" );
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
    Reflection::FromJson( StaticType(), this, inJson );
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
    command.CastShadows = CastShadows;
    command.ShadowBias = ShadowBias;
    command.ShadowNormalBias = ShadowNormalBias;
    command.ShadowDistance = std::max( ShadowDistance, 1.f );
    command.OuterConeAngle = outer;
    return command;
}
