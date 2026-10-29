#include "PCH.h"
#include "Net/NetReplication.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "Net/BitStream.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr size_t kSnapshotHistory = 32;
    constexpr size_t kSampleHistory = 64;

    // Placed networked entities, by GUID: the same order on both sides.
    std::vector<EntityHandle> PlacedNetworked( World& InWorld )
    {
        std::vector<std::pair<uint64_t, EntityHandle>> placed;
        InWorld.ForEachEntity( [&]( Entity& InEntity ) {
            if( InEntity.HasComponent<NetIdentity>() )
            {
                placed.emplace_back( InEntity.GetGUID(), InWorld.GetEntity( InEntity.GetId() ) );
            }
        } );
        std::sort( placed.begin(), placed.end(), []( const auto& InA, const auto& InB ) { return InA.first < InB.first; } );
        std::vector<EntityHandle> handles;
        for( auto& [guid, handle] : placed )
        {
            handles.push_back( handle );
        }
        return handles;
    }

    // Remote bodies follow the replicated pose; only the server simulates.
    void MakeKinematic( Entity& InEntity )
    {
        if( Rigidbody* body = InEntity.TryGetComponent<Rigidbody>() )
        {
            body->Type = BodyType::Kinematic;
        }
    }

    void WritePose( BitWriter& InWriter, const NetEntityState& InState )
    {
        for( const uint32_t axis : InState.Position )
        {
            InWriter.WriteBits( axis, NetEntityState::kPositionBits );
        }
        InWriter.WriteBits( static_cast<uint32_t>( InState.Rotation & 0xFFFFFFFFu ), 32 );
        InWriter.WriteBits( static_cast<uint32_t>( InState.Rotation >> 32 ), 6 );
    }

    void ReadPose( BitReader& InReader, NetEntityState& OutState )
    {
        for( uint32_t& axis : OutState.Position )
        {
            axis = InReader.ReadBits( NetEntityState::kPositionBits );
        }
        const uint64_t low = InReader.ReadBits( 32 );
        const uint64_t high = InReader.ReadBits( 6 );
        OutState.Rotation = low | ( high << 32 );
    }

    NetEntityState Capture( Entity& InEntity )
    {
        NetEntityState state;
        Transform& transform = InEntity.GetComponent<Transform>();
        state.SetPose( transform.GetWorldPosition(), transform.GetWorldRotation() );
        state.Fields = NetSchema::Write( InEntity );
        return state;
    }

    void Apply( Entity& InEntity, const Vector3& InPosition, const Quaternion& InRotation )
    {
        if( Transform* transform = InEntity.TryGetComponent<Transform>() )
        {
            transform->SetWorldPosition( InPosition );
            transform->SetWorldRotation( InRotation );
        }
    }
}


NetReplicationServer::NetReplicationServer( World& InWorld, NetServer& InServer )
    : m_world( InWorld )
    , m_server( InServer )
{
    m_server.OnPacket = [this]( NetServer::Client& InClient ) { OnPacket( InClient ); };
    m_server.OnClientAccepted = [this]( NetServer::Client& InClient ) { OnClientAccepted( InClient ); };
    m_server.OnMessage = [this]( NetServer::Client& InClient, uint8_t InType, BitReader& InReader, bool ) {
        if( InType == NetMessage::SnapshotAck )
        {
            ClientState& state = m_clients[InClient.Id];
            const uint32_t tick = InReader.ReadBits( 32 );
            if( !InReader.IsOverflowed() && ( !state.HasAck || tick > state.AckedTick ) )
            {
                state.HasAck = true;
                state.AckedTick = tick;
            }
        }
    };
}


void NetReplicationServer::AssignStaticIds()
{
    uint32_t id = 1;
    for( EntityHandle& handle : PlacedNetworked( m_world ) )
    {
        handle->GetComponent<NetIdentity>().m_netId = id;
        m_entities[id] = handle;
        ++id;
    }
    m_staticCount = id - 1;
}


uint32_t NetReplicationServer::GetNetId( Entity& InEntity ) const
{
    const NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
    return identity ? identity->m_netId : 0;
}


void NetReplicationServer::WriteSpawn( BitWriter& InWriter, uint32_t InId, Entity& InEntity ) const
{
    const NetIdentity& identity = InEntity.GetComponent<NetIdentity>();
    InWriter.WriteBits( NetMessage::Spawn, 8 );
    InWriter.WriteVarUInt( InId );
    InWriter.WriteBits( identity.m_owner, 8 );
    InWriter.WriteString( identity.Prefab );
    WritePose( InWriter, Capture( InEntity ) );
}


void NetReplicationServer::CaptureTick( uint32_t InTick )
{
    // New networked entities: dynamic ids, announced to everyone.
    m_world.ForEachEntity( [this]( Entity& InEntity ) {
        NetIdentity* identity = InEntity.TryGetComponent<NetIdentity>();
        if( identity && identity->m_netId == 0 )
        {
            identity->m_netId = m_nextDynamicId++;
            m_entities[identity->m_netId] = m_world.GetEntity( InEntity.GetId() );
            BitWriter spawn;
            WriteSpawn( spawn, identity->m_netId, InEntity );
            for( const auto& client : m_server.GetClients() )
            {
                if( client->Accepted )
                {
                    m_server.Send( *client, spawn, true );
                }
            }
        }
    } );
    // Gone ones.
    for( auto it = m_entities.begin(); it != m_entities.end(); )
    {
        if( !it->second || !it->second->HasComponent<NetIdentity>() )
        {
            BitWriter despawn;
            despawn.WriteBits( NetMessage::Despawn, 8 );
            despawn.WriteVarUInt( it->first );
            for( const auto& client : m_server.GetClients() )
            {
                if( client->Accepted )
                {
                    m_server.Send( *client, despawn, true );
                }
            }
            it = m_entities.erase( it );
        }
        else
        {
            ++it;
        }
    }

    m_current.Tick = InTick;
    m_current.Entities.clear();
    for( auto& [id, handle] : m_entities )
    {
        m_current.Entities.emplace( id, Capture( *handle.Get() ) );
    }
}


void NetReplicationServer::OnClientAccepted( NetServer::Client& InClient )
{
    m_clients[InClient.Id] = ClientState();
    // A late joiner placed every static entity: drop the ones destroyed since.
    for( uint32_t id = 1; id <= m_staticCount; ++id )
    {
        if( m_entities.find( id ) == m_entities.end() )
        {
            BitWriter despawn;
            despawn.WriteBits( NetMessage::Despawn, 8 );
            despawn.WriteVarUInt( id );
            m_server.Send( InClient, despawn, true );
        }
    }
    // ... and gets everything spawned so far.
    for( auto& [id, handle] : m_entities )
    {
        if( id >= kFirstDynamicId )
        {
            BitWriter spawn;
            WriteSpawn( spawn, id, *handle.Get() );
            m_server.Send( InClient, spawn, true );
        }
    }
}


void NetReplicationServer::OnPacket( NetServer::Client& InClient )
{
    ClientState& state = m_clients[InClient.Id];
    const NetSnapshot* baseline = nullptr;
    if( state.HasAck )
    {
        for( const NetSnapshot& sent : state.Sent )
        {
            if( sent.Tick == state.AckedTick )
            {
                baseline = &sent;
            }
        }
    }
    // Unchanged since the last one sent: skip it (a keep-alive now and then keeps acks moving).
    if( state.HasSent && !state.Sent.empty() && state.Sent.back().Entities == m_current.Entities && m_current.Tick - state.LastSentTick < kKeepAliveTicks )
    {
        return;
    }
    BitWriter writer;
    writer.WriteBits( NetMessage::Snapshot, 8 );
    writer.WriteBool( state.HasSent );
    if( state.HasSent )
    {
        writer.WriteBits( state.LastSentTick, 32 );
    }
    NetSnapshot sent;
    NetSnapshotCodec::Write( writer, m_current, baseline, kSnapshotBudget, sent );
    m_server.Send( InClient, writer, false );
    m_snapshotBytes += writer.GetByteCount();
    ++m_snapshotsSent;
    state.HasSent = true;
    state.LastSentTick = m_current.Tick;
    state.Sent.push_back( std::move( sent ) );
    while( state.Sent.size() > kSnapshotHistory )
    {
        state.Sent.pop_front();
    }
}


NetReplicationClient::NetReplicationClient( World& InWorld, NetClient& InClient )
    : m_world( InWorld )
    , m_client( InClient )
{
    m_client.OnMessage = [this]( uint8_t InType, BitReader& InReader, bool InReliable ) { OnMessage( InType, InReader, InReliable ); };
}


void NetReplicationClient::AssignStaticIds()
{
    uint32_t id = 1;
    for( EntityHandle& handle : PlacedNetworked( m_world ) )
    {
        Bind( id++, handle );
    }
}


void NetReplicationClient::Bind( uint32_t InId, EntityHandle InEntity )
{
    NetIdentity& identity = InEntity->HasComponent<NetIdentity>() ? InEntity->GetComponent<NetIdentity>() : InEntity->AddComponent<NetIdentity>();
    identity.m_netId = InId;
    MakeKinematic( *InEntity.Get() );
    m_entities[InId] = InEntity;
}


EntityHandle NetReplicationClient::FindEntity( uint32_t InNetId ) const
{
    auto it = m_entities.find( InNetId );
    return it != m_entities.end() ? it->second : EntityHandle();
}


void NetReplicationClient::OnMessage( uint8_t InType, BitReader& InReader, bool InReliable )
{
    if( InType == NetMessage::Spawn && InReliable )
    {
        OnSpawn( InReader );
    }
    else if( InType == NetMessage::Despawn && InReliable )
    {
        OnDespawn( InReader );
    }
    else if( InType == NetMessage::Snapshot && !InReliable )
    {
        OnSnapshot( InReader );
    }
}


void NetReplicationClient::OnSpawn( BitReader& InReader )
{
    const uint32_t id = static_cast<uint32_t>( InReader.ReadVarUInt() );
    const uint8_t owner = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
    const std::string prefab = InReader.ReadString( 512 );
    NetEntityState pose;
    ReadPose( InReader, pose );
    if( InReader.IsOverflowed() || m_entities.count( id ) )
    {
        return;
    }
    EntityHandle entity = prefab.empty() ? EntityHandle() : m_world.CreateFromPrefab( prefab );
    if( !entity )
    {
        entity = m_world.CreateEntity();
    }
    if( !entity->HasComponent<Transform>() )
    {
        entity->AddComponent<Transform>();
    }
    Bind( id, entity );
    NetIdentity& identity = entity->GetComponent<NetIdentity>();
    identity.Prefab = prefab;
    identity.m_owner = owner;
    Apply( *entity.Get(), pose.GetPosition(), pose.GetRotation() );
}


void NetReplicationClient::OnDespawn( BitReader& InReader )
{
    const uint32_t id = static_cast<uint32_t>( InReader.ReadVarUInt() );
    auto it = m_entities.find( id );
    if( InReader.IsOverflowed() || it == m_entities.end() )
    {
        return;
    }
    if( it->second )
    {
        m_world.DestroyEntity( it->second );
    }
    m_entities.erase( it );
    m_samples.erase( id );
}


void NetReplicationClient::OnSnapshot( BitReader& InReader )
{
    const bool hasPrevious = InReader.ReadBool();
    const uint32_t previousSent = hasPrevious ? InReader.ReadBits( 32 ) : 0;
    // The baseline the server used is one of ours: find it by the tick in the message.
    BitReader peek = InReader;
    peek.ReadBits( 32 );
    const NetSnapshot* baseline = nullptr;
    if( peek.ReadBool() )
    {
        const uint32_t baselineTick = peek.ReadBits( 32 );
        for( const NetSnapshot& snapshot : m_snapshots )
        {
            if( snapshot.Tick == baselineTick )
            {
                baseline = &snapshot;
            }
        }
    }
    NetSnapshot decoded;
    if( !NetSnapshotCodec::Read( InReader, baseline, decoded ) || ( m_hasSnapshot && decoded.Tick <= m_newest ) )
    {
        return;
    }
    // Did we have the one sent before it? Then nothing changed in between: entities held still
    // until this tick, rather than moving all the way from that older sample.
    const bool contiguous = hasPrevious && m_hasSnapshot && previousSent == m_newest;
    for( const auto& [id, state] : decoded.Entities )
    {
        std::deque<Sample>& samples = m_samples[id];
        if( contiguous && !samples.empty() && samples.back().Tick + 1 < decoded.Tick && samples.back().State != state )
        {
            samples.push_back( Sample{ decoded.Tick - 1, samples.back().State } );
        }
        samples.push_back( Sample{ decoded.Tick, state } );
        while( samples.size() > kSampleHistory )
        {
            samples.pop_front();
        }
    }
    m_hasSnapshot = true;
    m_newest = decoded.Tick;
    ++m_received;
    m_snapshots.push_back( std::move( decoded ) );
    while( m_snapshots.size() > kSnapshotHistory )
    {
        m_snapshots.pop_front();
    }
}


void NetReplicationClient::BeforeFlush()
{
    if( m_hasSnapshot )
    {
        BitWriter ack;
        ack.WriteBits( NetMessage::SnapshotAck, 8 );
        ack.WriteBits( m_newest, 32 );
        m_client.Send( ack, false );
    }
}


void NetReplicationClient::Update( double InFrameSeconds )
{
    if( !m_hasSnapshot )
    {
        return;
    }
    // The newest tick the server has told us about (every packet says), minus the delay.
    const double latest = std::max<double>( m_client.GetServerTick(), m_newest );
    const double target = latest - m_delay;
    const double ticks = InFrameSeconds * m_client.GetTickRate();
    if( !m_clockStarted || std::abs( target - m_renderTick ) > 6.0 )
    {
        m_renderTick = target;
        m_clockStarted = true;
    }
    else
    {
        // Steer gently towards the target so motion stays smooth.
        m_renderTick += ticks * ( 1.0 + std::clamp( ( target - m_renderTick ) * 0.1, -0.1, 0.1 ) );
    }

    // Adaptive delay: running past the samples of something that moves means it's too short.
    bool starved = false;
    for( auto& [id, samples] : m_samples )
    {
        auto entity = m_entities.find( id );
        if( samples.empty() || entity == m_entities.end() || !entity->second )
        {
            continue;
        }
        // The samples either side of the render tick.
        const Sample* before = &samples.front();
        const Sample* after = nullptr;
        for( const Sample& sample : samples )
        {
            if( sample.Tick <= m_renderTick )
            {
                before = &sample;
            }
            else
            {
                after = &sample;
                break;
            }
        }
        Vector3 position = before->State.GetPosition();
        Quaternion rotation = before->State.GetRotation();
        if( after && after->Tick > before->Tick && before->Tick <= m_renderTick )
        {
            const float t = static_cast<float>( ( m_renderTick - before->Tick ) / ( after->Tick - before->Tick ) );
            position = position + ( after->State.GetPosition() - position ) * t;
            rotation = Quaternion::Slerp( rotation, after->State.GetRotation(), t );
        }
        else if( !after && before->Tick < m_renderTick && samples.size() > 1 && samples[samples.size() - 2].State.Position != before->State.Position )
        {
            starved = true;
        }
        Apply( *entity->second.Get(), position, rotation );
        NetSchema::Read( *entity->second.Get(), before->State.Fields );
    }
    if( starved )
    {
        m_delay = std::min( m_delay + 0.25, 8.0 );
        m_settleSeconds = 0.0;
    }
    else
    {
        m_settleSeconds += InFrameSeconds;
        if( m_settleSeconds > 2.0 && m_delay > 1.5 )
        {
            m_delay = std::max( 1.5, m_delay - 0.05 );
            m_settleSeconds = 1.5;
        }
    }
}
