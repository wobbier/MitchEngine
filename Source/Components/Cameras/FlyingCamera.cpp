#include "PCH.h"
#include "FlyingCamera.h"

ME_REFLECT_BEGIN( FlyingCamera )
    ME_FIELD( FlyingSpeed ).Speed( 0.1f ).Unit( Reflection::Unit::MetersPerSecond ).Log().SoftRange( 0.5f, 50.f );
    ME_FIELD( SpeedModifier ).Speed( 0.5f ).Unit( Reflection::Unit::Multiplier ).Tooltip( "Speed multiplier while Shift is held" );
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
