#include "PCH.h"
#include "Navigation.bindings.h"

#if USING( ME_SCRIPTING )

#include "Components/Navigation/NavMeshAgent.h"
#include "Cores/NavigationCore.h"
#include "ECS/Entity.h"
#include "Engine/Engine.h"
#include "Scripting/Bindings/BindingContext.h"
#include <algorithm>

using ScriptBindings::MakeHandle;

namespace
{
    NavMeshAgent* AgentOf( EntityID InId )
    {
        EntityHandle handle = MakeHandle( InId );
        return handle ? handle->TryGetComponent<NavMeshAgent>() : nullptr;
    }
}


static bool Eng_NavAgent_SetDestination( EntityID inId, const Vector3* inDestination )
{
    NavMeshAgent* agent = AgentOf( inId );
    return agent && agent->SetDestination( *inDestination );
}


static void Eng_NavAgent_SetStopped( EntityID inId, bool inStopped )
{
    if( NavMeshAgent* agent = AgentOf( inId ) )
    {
        inStopped ? agent->Stop() : agent->Resume();
    }
}


static void Eng_NavAgent_ResetPath( EntityID inId )
{
    if( NavMeshAgent* agent = AgentOf( inId ) )
    {
        agent->ResetPath();
    }
}


static void Eng_NavAgent_Warp( EntityID inId, const Vector3* inPosition )
{
    if( NavMeshAgent* agent = AgentOf( inId ) )
    {
        agent->Warp( *inPosition );
    }
}


static bool Eng_NavAgent_HasPath( EntityID inId )
{
    NavMeshAgent* agent = AgentOf( inId );
    return agent && ( agent->HasPath() || agent->IsPathPending() );
}


static bool Eng_NavAgent_HasArrived( EntityID inId )
{
    NavMeshAgent* agent = AgentOf( inId );
    return agent && agent->HasArrived();
}


static float Eng_NavAgent_GetRemainingDistance( EntityID inId )
{
    NavMeshAgent* agent = AgentOf( inId );
    return agent ? agent->GetRemainingDistance() : 0.f;
}


static void Eng_NavAgent_GetVelocity( EntityID inId, Vector3* outVelocity )
{
    NavMeshAgent* agent = AgentOf( inId );
    *outVelocity = agent ? agent->GetVelocity() : Vector3();
}


static void Eng_NavAgent_GetDesiredVelocity( EntityID inId, Vector3* outVelocity )
{
    NavMeshAgent* agent = AgentOf( inId );
    *outVelocity = agent ? agent->GetDesiredVelocity() : Vector3();
}


static float Eng_NavAgent_GetSpeed( EntityID inId )
{
    NavMeshAgent* agent = AgentOf( inId );
    return agent ? agent->Speed : 0.f;
}


static void Eng_NavAgent_SetSpeed( EntityID inId, float inSpeed )
{
    if( NavMeshAgent* agent = AgentOf( inId ) )
    {
        agent->Speed = std::max( inSpeed, 0.f );
    }
}


static int Eng_Navigation_FindPath( const Vector3* inStart, const Vector3* inEnd, Vector3* outCorners, int inMaxCorners )
{
    NavigationCore* navigation = GetEngine().Navigation;
    if( !navigation || inMaxCorners <= 0 )
    {
        return 0;
    }
    std::vector<Vector3> corners;
    if( navigation->FindPath( *inStart, *inEnd, corners ) == NavPathStatus::Invalid )
    {
        return 0;
    }
    const int count = std::min( static_cast<int>( corners.size() ), inMaxCorners );
    std::copy( corners.begin(), corners.begin() + count, outCorners );
    return count;
}


static bool Eng_Navigation_SamplePosition( const Vector3* inPoint, float inMaxDistance, Vector3* outPosition )
{
    NavigationCore* navigation = GetEngine().Navigation;
    NavMeshHit hit;
    if( !navigation || !navigation->SamplePosition( *inPoint, inMaxDistance, hit ) )
    {
        *outPosition = *inPoint;
        return false;
    }
    *outPosition = hit.Position;
    return true;
}


static bool Eng_Navigation_Raycast( const Vector3* inStart, const Vector3* inEnd, Vector3* outHit )
{
    NavigationCore* navigation = GetEngine().Navigation;
    NavMeshHit hit;
    const bool blocked = navigation && navigation->Raycast( *inStart, *inEnd, hit );
    *outHit = blocked ? hit.Position : *inEnd;
    return blocked;
}


static bool Eng_Navigation_GetRandomPoint( Vector3* outPoint )
{
    NavigationCore* navigation = GetEngine().Navigation;
    return navigation && navigation->GetRandomPoint( *outPoint );
}


void Register_NavigationBindings( ScriptEngineAPI& inAPI )
{
    ScriptBindings::RegisterComponent<NavMeshAgent>( "NavMeshAgent" );
    inAPI.NavAgent_SetDestination = Eng_NavAgent_SetDestination;
    inAPI.NavAgent_SetStopped = Eng_NavAgent_SetStopped;
    inAPI.NavAgent_ResetPath = Eng_NavAgent_ResetPath;
    inAPI.NavAgent_Warp = Eng_NavAgent_Warp;
    inAPI.NavAgent_HasPath = Eng_NavAgent_HasPath;
    inAPI.NavAgent_HasArrived = Eng_NavAgent_HasArrived;
    inAPI.NavAgent_GetRemainingDistance = Eng_NavAgent_GetRemainingDistance;
    inAPI.NavAgent_GetVelocity = Eng_NavAgent_GetVelocity;
    inAPI.NavAgent_GetDesiredVelocity = Eng_NavAgent_GetDesiredVelocity;
    inAPI.NavAgent_GetSpeed = Eng_NavAgent_GetSpeed;
    inAPI.NavAgent_SetSpeed = Eng_NavAgent_SetSpeed;
    inAPI.Navigation_FindPath = Eng_Navigation_FindPath;
    inAPI.Navigation_SamplePosition = Eng_Navigation_SamplePosition;
    inAPI.Navigation_Raycast = Eng_Navigation_Raycast;
    inAPI.Navigation_GetRandomPoint = Eng_Navigation_GetRandomPoint;
}

#endif
