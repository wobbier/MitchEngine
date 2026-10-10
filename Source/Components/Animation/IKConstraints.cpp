#include "PCH.h"
#include "IKConstraints.h"

ME_REFLECT_BEGIN( TwoBoneIK )
    ME_FIELD( Tip ).Tooltip( "The end bone (foot, hand); its parent and grandparent bend" );
    ME_FIELD( Target ).Tooltip( "Where the tip goes" );
    ME_FIELD( Pole ).Tooltip( "Optional: the middle joint bends toward it" );
    ME_FIELD( Weight ).Range( 0.f, 1.f );
    ME_FIELD( MatchTargetRotation ).Tooltip( "Also turn the tip to the target's rotation" );
ME_REFLECT_END()


ME_REFLECT_BEGIN( LookAtIK )
    ME_FIELD( Bone ).Tooltip( "The bone that aims; empty aims this entity" );
    ME_FIELD( Target );
    ME_FIELD( AimAxis ).Tooltip( "The bone's forward direction, in its own space" );
    ME_FIELD( Weight ).Range( 0.f, 1.f );
    ME_FIELD( MaxAngle ).Range( 0.f, 180.f ).Tooltip( "Furthest the aim turns from the animated pose (degrees)" );
    ME_FIELD( ChainLength ).Range( 1, 8 ).Tooltip( "Bones sharing the turn: the aiming bone and its parents" );
ME_REFLECT_END()


TwoBoneIK::TwoBoneIK()
    : Component( "TwoBoneIK" )
{
}


LookAtIK::LookAtIK()
    : Component( "LookAtIK" )
{
}
