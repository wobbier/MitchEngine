#include "PCH.h"
#include "FlyingCamera.h"

ME_REFLECT_BEGIN( FlyingCamera )
    ME_FIELD( FlyingSpeed ).Speed( 0.1f );
    ME_FIELD( SpeedModifier ).Speed( 0.5f ).Tooltip( "Speed multiplier while Shift is held" );
    ME_FIELD( LookSensitivity ).Speed( 0.01f );
ME_REFLECT_END()


FlyingCamera::FlyingCamera()
    : Component( "FlyingCamera" )
{
}


FlyingCamera::~FlyingCamera()
{
}


void FlyingCamera::Init()
{
}
