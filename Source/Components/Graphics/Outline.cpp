#include "PCH.h"
#include "Outline.h"

ME_REFLECT_BEGIN( Outline )
    ME_FIELD( Color ).Color().Tooltip( "Outline colour (sRGB)" );
    ME_FIELD( Width ).Display( "Width (m)" ).Range( 0.f, 0.5f ).Speed( 0.001f ).Tooltip( "Metres the hull inflates along the mesh normals; the ring thins with distance" );
ME_REFLECT_END()


Outline::Outline()
    : Component( "Outline" )
{
}
