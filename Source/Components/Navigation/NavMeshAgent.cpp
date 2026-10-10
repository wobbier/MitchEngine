#include "PCH.h"
#include "NavMeshAgent.h"
#include "Components/Navigation/NavMeshModifiers.h"

ME_REFLECT_ENUM( NavAvoidanceQuality, { { "None", NavAvoidanceQuality::None }, { "Low", NavAvoidanceQuality::Low }, { "Medium", NavAvoidanceQuality::Medium }, { "High", NavAvoidanceQuality::High } } )

ME_REFLECT_BEGIN( NavMeshAgent )
    ME_FIELD( Speed ).Category( "Steering" ).Range( 0.f, 50.f );
    ME_FIELD( Acceleration ).Category( "Steering" ).Range( 0.f, 200.f );
    ME_FIELD( AngularSpeed ).Category( "Steering" ).Range( 0.f, 1440.f ).Tooltip( "Degrees per second turning to face the direction of travel" );
    ME_FIELD( StoppingDistance ).Category( "Steering" ).Range( 0.f, 20.f );
    ME_FIELD( Radius ).Category( "Shape" ).Range( 0.05f, 10.f );
    ME_FIELD( Height ).Category( "Shape" ).Range( 0.1f, 20.f );
    ME_FIELD( BaseOffset ).Category( "Shape" ).Tooltip( "Height of the Transform above the navmesh" );
    ME_FIELD( LinkJumpHeight ).Category( "Steering" ).Range( 0.f, 10.f ).Tooltip( "Arc height when crossing a NavMeshLink" );
    ME_FIELD( Avoidance ).Category( "Avoidance" );
    ME_FIELD( AreaMask ).Category( "Pathing" ).MaskChoices( NavAreas::kCount, NavigationUI::AreaName ).Tooltip( "Areas this agent may walk on" );
    ME_FIELD( UpdatePosition ).Category( "Pathing" ).Tooltip( "Move the Transform; off: read the desired velocity and move it yourself" );
    ME_FIELD( UpdateRotation ).Category( "Pathing" );
ME_REFLECT_END()


NavMeshAgent::NavMeshAgent()
    : Component( "NavMeshAgent" )
{
}


void NavMeshAgent::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
}


bool NavMeshAgent::SetDestination( const Vector3& InDestination )
{
    m_destination = InDestination;
    m_requestPending = true;
    m_resetRequested = false;
    m_arrived = false;
    return true;
}


void NavMeshAgent::ResetPath()
{
    m_requestPending = false;
    m_resetRequested = true;
    m_hasPath = false;
    m_arrived = false;
}


void NavMeshAgent::Stop()
{
    m_stopped = true;
}


void NavMeshAgent::Resume()
{
    m_stopped = false;
}


void NavMeshAgent::Warp( const Vector3& InPosition )
{
    m_warpRequested = true;
    m_warpPosition = InPosition;
    m_requestPending = false;
    m_hasPath = false;
    m_arrived = false;
}
