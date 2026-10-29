#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <cstdint>
#include <string>

// Marks an entity as networked (NET-3). The server sends its pose and its components'
// Replicated fields to every client in snapshots; clients show it interpolated, with any
// Rigidbody made kinematic (only the server simulates bodies).
//
// - Placed in the scene: a static NetId, the same on both sides (the placed NetIdentity
//   entities' GUIDs, sorted; the handshake checks the scene matches).
// - Created while a session runs (server): a dynamic NetId, and a reliable Spawn to every client,
//   which creates it from Prefab (or as a bare entity with a Transform when there's none).
class NetIdentity
    : public Component<NetIdentity>
{
    ME_REFLECTABLE( NetIdentity )
    friend class NetReplicationServer;
    friend class NetReplicationClient;
public:
    NetIdentity();

    std::string Prefab;     // what clients create for a spawned copy

    uint32_t GetNetId() const { return m_netId; }
    // The client that owns it (0: the server).
    uint8_t GetOwner() const { return m_owner; }
    void SetOwner( uint8_t InOwner ) { m_owner = InOwner; }

private:
    uint32_t m_netId = 0;
    uint8_t m_owner = 0;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif
};
ME_REGISTER_COMPONENT_FOLDER( NetIdentity, "Networking" )
