#include "PCH.h"
#include "Components/Graphics/OccluderFade.h"

ME_REFLECT_BEGIN( OccluderFade )
    ME_FIELD( Target ).Tooltip( "The entity, by name, the camera keeps in view" );
    ME_FIELD( Radius ).Range( 0.f, 5.f ).Tooltip( "Metres around the camera-to-target line that count as in the way" );
    ME_FIELD( Amount ).Range( 0.f, 1.f ).Tooltip( "How much of an occluder dithers away" );
    ME_FIELD( FadeSeconds ).Range( 0.f, 2.f );
ME_REFLECT_END()


OccluderFade::OccluderFade()
    : Component( "OccluderFade" )
{
}
