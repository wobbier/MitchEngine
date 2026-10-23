#include "PCH.h"
#include "WindZone.h"
#include "Components/Transform.h"
#include "Debug/DebugDraw.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_BEGIN( WindZone )
    ME_FIELD( Strength ).Range( 0.f, 4.f ).Tooltip( "0 is calm, 1 a strong breeze, 2 and above a storm" );
    ME_FIELD( Gusts ).Range( 0.f, 2.f ).Tooltip( "How much gusts add on top of the strength" );
    ME_FIELD( GustFrequency ).Range( 0.f, 2.f ).Tooltip( "Gust fronts per second (they roll downwind across a forest)" );
    ME_FIELD( Turbulence ).Range( 0.f, 2.f ).Tooltip( "Crosswise, irregular motion" );
ME_REFLECT_END()


WindZone::WindZone()
    : Component( "WindZone" )
{
}


Moonlight::WindCommand WindZone::BuildCommand( const Transform& InTransform ) const
{
    Moonlight::WindCommand command;
    Vector3 forward = const_cast<Transform&>( InTransform ).Front();
    forward.y = 0.f;
    const float length = forward.Length();
    // Straight up or down blows nowhere in particular: keep the default heading.
    if( length > 1e-4f && std::isfinite( length ) ) command.Direction = forward * ( 1.f / length );
    command.Strength = std::isfinite( Strength ) ? std::max( Strength, 0.f ) : 0.f;
    command.Gusts = std::isfinite( Gusts ) ? std::max( Gusts, 0.f ) : 0.f;
    command.GustFrequency = std::isfinite( GustFrequency ) ? std::max( GustFrequency, 0.f ) : 0.f;
    command.Turbulence = std::isfinite( Turbulence ) ? std::max( Turbulence, 0.f ) : 0.f;
    return command;
}


#if USING( ME_EDITOR )
void WindZone::OnEditorInspect()
{
    // Show which way it blows while it's inspected.
    if( Transform* transform = Parent ? Parent->TryGetComponent<Transform>() : nullptr )
    {
        const Moonlight::WindCommand wind = BuildCommand( *transform );
        const Vector3 from = transform->GetWorldPosition();
        const Vector3 to = from + wind.Direction * ( 2.f + Strength );
        const Vector3 side( -wind.Direction.z, 0.f, wind.Direction.x );
        const Vector4 color( 0.55f, 0.85f, 1.f, 1.f );
        DebugDraw::Line( from, to, color );
        DebugDraw::Line( to, to - wind.Direction * 0.5f + side * 0.3f, color );
        DebugDraw::Line( to, to - wind.Direction * 0.5f - side * 0.3f, color );
    }
}
#endif
