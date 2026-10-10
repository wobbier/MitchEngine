#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Navigation/NavigationTypes.h"
#include "Math/Vector3.h"
#include <cstdint>

// How hard an agent works to avoid other agents (DetourCrowd obstacle avoidance).
enum class NavAvoidanceQuality : uint8_t
{
    None = 0,
    Low,
    Medium,
    High,
};

// Walks the navmesh of the NavMeshSurface it stands on, steering around other agents (DetourCrowd).
// Give it somewhere to go with SetDestination; while the game runs it moves its Transform (unless
// UpdatePosition is off: then read GetDesiredVelocity and move it yourself, e.g. with a
// CharacterController). Path requests are processed on the next navigation update (PathPending).
class NavMeshAgent
    : public Component<NavMeshAgent>
{
    ME_REFLECTABLE( NavMeshAgent )
    friend class NavigationCore;
public:
    NavMeshAgent();

    float Speed = 3.5f;             // m/s
    float Acceleration = 8.f;       // m/s^2
    float AngularSpeed = 360.f;     // degrees/s turning to face the direction of travel
    float StoppingDistance = 0.f;   // arrive (and stop) this close to the destination
    float Radius = 0.5f;
    float Height = 2.f;
    float BaseOffset = 0.f;         // Transform height above the navmesh surface
    float LinkJumpHeight = 1.f;     // arc height when crossing a NavMeshLink
    NavAvoidanceQuality Avoidance = NavAvoidanceQuality::High;
    uint32_t AreaMask = NavAreas::All;
    // Walk only surfaces baked for this agent type (Project Settings > Navigation); 0 = the surface
    // under the agent baked for the closest radius.
    int AgentType = 0;
    bool UpdatePosition = true;
    bool UpdateRotation = true;

    // Paths
    bool SetDestination( const Vector3& InDestination );
    void ResetPath();
    void Stop();                    // keeps the path; resume with Resume()
    void Resume();
    bool IsStopped() const { return m_stopped; }
    // Moves the agent instantly (drops the path).
    void Warp( const Vector3& InPosition );

    Vector3 GetDestination() const { return m_destination; }
    bool HasPath() const { return m_hasPath; }
    bool IsPathPending() const { return m_requestPending || m_pathPending; }
    NavPathStatus GetPathStatus() const { return m_pathStatus; }
    float GetRemainingDistance() const { return m_remainingDistance; }
    // Reached the destination (within StoppingDistance) and stopped.
    bool HasArrived() const { return m_arrived; }
    Vector3 GetVelocity() const { return m_velocity; }
    Vector3 GetDesiredVelocity() const { return m_desiredVelocity; }
    // Where the agent is on the navmesh (its Transform when UpdatePosition is on, minus BaseOffset).
    Vector3 GetNextPosition() const { return m_position; }
    bool IsOnNavMesh() const { return m_onNavMesh; }
    bool IsOnLink() const { return m_onLink; }

private:
    void OnDeserialize( const json& InJson ) override;

    // Requests, applied by NavigationCore
    bool m_requestPending = false;
    bool m_pathPending = false;     // the crowd is still searching
    bool m_resetRequested = false;
    bool m_warpRequested = false;
    Vector3 m_warpPosition;
    bool m_stopped = false;
    // State, written by NavigationCore
    Vector3 m_destination;
    bool m_hasPath = false;
    bool m_arrived = false;
    NavPathStatus m_pathStatus = NavPathStatus::Invalid;
    float m_remainingDistance = 0.f;
    Vector3 m_velocity;
    Vector3 m_desiredVelocity;
    Vector3 m_position;
    bool m_onNavMesh = false;
    bool m_onLink = false;
    Vector3 m_linkStart;
};
ME_REGISTER_COMPONENT_FOLDER( NavMeshAgent, "Navigation" )
