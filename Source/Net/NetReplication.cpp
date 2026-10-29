#include "PCH.h"
#include "Net/NetReplication.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include "Jobs/JobSystem.h"
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

    void WriteVector( BitWriter& InWriter, const Vector3& InValue )
    {
        InWriter.WriteFloat( InValue.x );
        InWriter.WriteFloat( InValue.y );
        InWriter.WriteFloat( InValue.z );
    }

    Vector3 ReadVector( BitReader& InReader )
    {
        const float x = InReader.ReadFloat();
        const float y = InReader.ReadFloat();
        const float z = InReader.ReadFloat();
        return Vector3( x, y, z );
    }

    void ReadPose( BitReader& InReader, NetEntityState& OutState );
}


namespace
{
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
    m_meta.clear();
    for( auto& [id, handle] : m_entities )
    {
        m_current.Entities.emplace( id, Capture( *handle.Get() ) );
        const NetIdentity& identity = handle->GetComponent<NetIdentity>();
        m_meta[id] = EntityMeta{ identity.m_owner, identity.Priority, identity.AlwaysRelevant };
    }
    PrepareSnapshots();
}


void NetReplicationServer::PrepareSnapshots()
{
    std::vector<std::pair<uint8_t, ClientState*>> clients;
    for( const auto& client : m_server.GetClients() )
    {
        if( client->Accepted )
        {
            clients.emplace_back( client->Id, &m_clients[client->Id] );
        }
    }
    // Each client's snapshot only reads the shared capture: encode them side by side.
    auto prepare = [this, &clients]( uint32_t InBegin, uint32_t InEnd ) {
        for( uint32_t i = InBegin; i < InEnd; ++i )
        {
            Prepare( clients[i].first, *clients[i].second );
        }
    };
    if( Settings.UseJobs && clients.size() > 1 )
    {
        Jobs::JobSystem::Get().ParallelFor( static_cast<uint32_t>( clients.size() ), 1, prepare );
    }
    else
    {
        prepare( 0, static_cast<uint32_t>( clients.size() ) );
    }
}


void NetReplicationServer::Prepare( uint8_t InClient, ClientState& InState ) const
{
    InState.HasPrepared = false;
    const NetSnapshot* baseline = nullptr;
    if( InState.HasAck )
    {
        for( const NetSnapshot& sent : InState.Sent )
        {
            if( sent.Tick == InState.AckedTick )
            {
                baseline = &sent;
            }
        }
    }

    // Where this client is looking from.
    bool hasViewer = false;
    Vector3 viewer;
    if( auto it = m_viewers.find( InClient ); it != m_viewers.end() )
    {
        hasViewer = true;
        viewer = it->second;
    }
    else
    {
        for( const auto& [id, meta] : m_meta )
        {
            if( meta.Owner == InClient )
            {
                hasViewer = true;
                viewer = m_current.Entities.at( id ).GetPosition();
                break;
            }
        }
    }

    // What it gets to see, and what changed there, by priority.
    NetSnapshot view;
    view.Tick = m_current.Tick;
    std::vector<std::pair<float, uint32_t>> candidates;
    for( const auto& [id, state] : m_current.Entities )
    {
        const EntityMeta& meta = m_meta.at( id );
        const float distance = hasViewer ? ( state.GetPosition() - viewer ).Length() : 0.f;
        const bool relevant = Settings.RelevantRadius <= 0.f || !hasViewer || meta.AlwaysRelevant || meta.Owner == InClient || distance <= Settings.RelevantRadius;
        if( !relevant )
        {
            InState.Accumulators.erase( id );
            continue;
        }
        view.Entities.emplace( id, state );
        const NetEntityState* before = nullptr;
        if( baseline )
        {
            auto it = baseline->Entities.find( id );
            before = it != baseline->Entities.end() ? &it->second : nullptr;
        }
        if( before && *before == state )
        {
            InState.Accumulators.erase( id );
            continue;
        }
        const float near = std::max( Settings.NearDistance, 0.001f );
        float& accumulated = InState.Accumulators[id];
        accumulated += meta.Priority * ( distance <= near ? 1.f : near / distance );
        candidates.emplace_back( accumulated, id );
    }
    std::sort( candidates.begin(), candidates.end(), []( const auto& InA, const auto& InB ) {
        return InA.first != InB.first ? InA.first > InB.first : InA.second < InB.second;
    } );

    // The budget: the packet's share, and the client's bandwidth.
    double budget = static_cast<double>( kSnapshotBudget );
    if( Settings.BytesPerSecond > 0 )
    {
        const double perTick = static_cast<double>( Settings.BytesPerSecond ) / std::max<uint16_t>( m_server.GetTickRate(), 1 );
        InState.Tokens = std::min( InState.Tokens + perTick, perTick * 2.0 );
        budget = std::min( budget, InState.Tokens );
    }
    // Highest priority first, while they fit (each entity's size worked out from what changed).
    constexpr double kHeaderBytes = 16.0;
    double used = kHeaderBytes;
    std::set<uint32_t> selected;
    for( const auto& [priority, id] : candidates )
    {
        const NetEntityState& state = view.Entities.at( id );
        const NetEntityState* before = nullptr;
        if( baseline )
        {
            auto it = baseline->Entities.find( id );
            before = it != baseline->Entities.end() ? &it->second : nullptr;
        }
        double bits = 1 + 24 + 3;
        bits += !before || before->Position != state.Position ? NetEntityState::kPositionBits * 3 : 0;
        bits += !before || before->Rotation != state.Rotation ? 38 : 0;
        bits += !before || before->Fields != state.Fields ? 16 + state.Fields.size() * 8.0 : 0;
        if( used + bits / 8.0 > budget )
        {
            continue;
        }
        used += bits / 8.0;
        selected.insert( id );
    }

    BitWriter writer;
    writer.WriteBits( NetMessage::Snapshot, 8 );
    writer.WriteBool( InState.HasSent );
    if( InState.HasSent )
    {
        writer.WriteBits( InState.LastSentTick, 32 );
    }
    NetSnapshot sent;
    NetSnapshotCodec::Write( writer, view, baseline, kSnapshotBudget, sent, &selected );

    // Unchanged since the last one sent: skip it (a keep-alive now and then keeps acks moving).
    if( InState.HasSent && !InState.Sent.empty() && InState.Sent.back().Entities == sent.Entities && m_current.Tick - InState.LastSentTick < kKeepAliveTicks )
    {
        return;
    }
    for( const uint32_t id : selected )
    {
        InState.Accumulators.erase( id );
        InState.LastSentEntity[id] = m_current.Tick;
    }
    if( Settings.BytesPerSecond > 0 )
    {
        InState.Tokens -= static_cast<double>( writer.GetByteCount() );
    }
    InState.Bytes += writer.GetByteCount();
    InState.Prepared = writer.GetBytes();
    InState.PreparedSent = std::move( sent );
    InState.HasPrepared = true;
    InState.HasSent = true;
    InState.LastSentTick = m_current.Tick;
    InState.Sent.push_back( InState.PreparedSent );
    while( InState.Sent.size() > kSnapshotHistory )
    {
        InState.Sent.pop_front();
    }
}


uint32_t NetReplicationServer::GetLastSentTick( uint8_t InClient, uint32_t InNetId ) const
{
    auto client = m_clients.find( InClient );
    if( client == m_clients.end() )
    {
        return 0;
    }
    auto it = client->second.LastSentEntity.find( InNetId );
    return it != client->second.LastSentEntity.end() ? it->second : 0;
}


uint64_t NetReplicationServer::GetClientSnapshotBytes( uint8_t InClient ) const
{
    auto client = m_clients.find( InClient );
    return client != m_clients.end() ? client->second.Bytes : 0;
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
    // Its own characters' exact state, for its prediction (full precision: it restores from it).
    for( auto& [id, handle] : m_entities )
    {
        const NetIdentity& identity = handle->GetComponent<NetIdentity>();
        const CharacterController* controller = handle->TryGetComponent<CharacterController>();
        if( identity.m_owner != InClient.Id || controller == nullptr )
        {
            continue;
        }
        const CharacterController::NetState mover = controller->GetNetState();
        BitWriter owner;
        owner.WriteBits( NetMessage::OwnerState, 8 );
        owner.WriteBits( m_current.Tick, 32 );
        owner.WriteVarUInt( id );
        WriteVector( owner, mover.Position );
        WriteVector( owner, mover.Velocity );
        WriteVector( owner, mover.MoveInput );
        WriteVector( owner, mover.PendingMove );
        WriteVector( owner, mover.GroundNormal );
        owner.WriteBool( mover.WantsJump );
        owner.WriteBool( mover.Grounded );
        m_server.Send( InClient, owner, false );
    }

    // This tick's snapshot was encoded already (PrepareSnapshots).
    ClientState& state = m_clients[InClient.Id];
    if( state.HasPrepared )
    {
        BitWriter message;
        message.WriteBytes( state.Prepared.data(), state.Prepared.size() );
        m_server.Send( InClient, message, false );
        m_snapshotBytes += state.Prepared.size();
        ++m_snapshotsSent;
        state.HasPrepared = false;
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
    else if( InType == NetMessage::OwnerState && !InReliable )
    {
        const uint32_t tick = InReader.ReadBits( 32 );
        const uint32_t id = static_cast<uint32_t>( InReader.ReadVarUInt() );
        CharacterController::NetState state;
        state.Position = ReadVector( InReader );
        state.Velocity = ReadVector( InReader );
        state.MoveInput = ReadVector( InReader );
        state.PendingMove = ReadVector( InReader );
        state.GroundNormal = ReadVector( InReader );
        state.WantsJump = InReader.ReadBool();
        state.Grounded = InReader.ReadBool();
        if( !InReader.IsOverflowed() && OnOwnerState )
        {
            OnOwnerState( id, tick, state );
        }
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
        if( samples.empty() || entity == m_entities.end() || !entity->second || m_predicted.count( id ) )
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
