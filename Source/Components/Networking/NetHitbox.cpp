#include "PCH.h"
#include "Components/Networking/NetHitbox.h"

ME_REFLECT_ENUM( NetHitboxShape, { { "Capsule", NetHitboxShape::Capsule }, { "Box", NetHitboxShape::Box } } )

ME_REFLECT_BEGIN( NetHitbox )
    ME_FIELD( Shape );
    ME_FIELD( Center );
    ME_FIELD( Radius ).Range( 0.01f, 10.f );
    ME_FIELD( Height ).Range( 0.f, 20.f ).Tooltip( "Capsule: end to end, including the caps" );
    ME_FIELD( HalfExtents ).Tooltip( "Box" );
ME_REFLECT_END()


NetHitbox::NetHitbox()
    : Component( "NetHitbox" )
{
}
