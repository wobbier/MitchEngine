#include "PCH.h"
#include "Net/NetReplayPlayback.h"
#include "CLog.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Transform.h"
#include "Net/BitStream.h"
#include "Net/NetReplication.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace
{
    // Ticks fed one after another to catch up (a fast-forward, a short jump) before a rebuild from
    // the tick itself is cheaper. The client keeps 64 samples an entity.
    constexpr uint32_t kMaxCatchUp = 48;

    std::string Label( const std::map<uint32_t, NetReplayEntity>& InTable, uint32_t InNetId )
    {
        auto it = InTable.find( InNetId );
        return ( it != InTable.end() && !it->second.Name.empty() ? it->second.Name : std::string( "entity" ) ) + "#" + std::to_string( InNetId );
    }

    std::string Describe( const Vector3& InValue )
    {
        std::ostringstream out;
        out.precision( 6 );
        out << std::fixed << "(" << InValue.x << ", " << InValue.y << ", " << InValue.z << ")";
        return out.str();
    }
}


NetReplayPlayback::NetReplayPlayback( INetReplaySource& InSource, NetReplicationClient& InClient )
    : m_reader( InSource )
    , m_client( InClient )
{
    if( !m_reader.IsEmpty() )
    {
        m_first = m_reader.GetFirstTick();
        m_last = m_reader.GetLastTick();
    }
    m_time = m_first;
    m_loopFrom = m_first;
    m_loopTo = m_last;
}


uint16_t NetReplayPlayback::GetTickRate() const
{
    return std::max<uint16_t>( GetHeader().TickRate, 1 );
}


void NetReplayPlayback::Play()
{
    // At the end: from the start (or the loop's) again.
    if( m_time >= m_last )
    {
        m_time = m_looping ? m_loopFrom : m_first;
    }
    m_paused = false;
}


void NetReplayPlayback::Pause()
{
    m_paused = true;
}


void NetReplayPlayback::SetSpeed( double InSpeed )
{
    m_speed = std::clamp( std::isfinite( InSpeed ) ? InSpeed : 1.0, kMinSpeed, kMaxSpeed );
}


void NetReplayPlayback::Seek( double InTick )
{
    m_time = std::clamp( std::isfinite( InTick ) ? InTick : static_cast<double>( m_first ), static_cast<double>( m_first ), static_cast<double>( m_last ) );
}


void NetReplayPlayback::Step( int64_t InTicks )
{
    m_paused = true;
    const int64_t tick = static_cast<int64_t>( std::floor( m_time ) ) + InTicks;
    m_time = static_cast<double>( std::clamp<int64_t>( tick, m_first, m_last ) );
}


void NetReplayPlayback::SetLoop( uint32_t InFrom, uint32_t InTo )
{
    if( InFrom > InTo )
    {
        std::swap( InFrom, InTo );
    }
    m_loopFrom = std::clamp( InFrom, m_first, m_last );
    m_loopTo = std::clamp( InTo, m_first, m_last );
    m_looping = true;
}


void NetReplayPlayback::SetPauseAt( int64_t InTick )
{
    m_pauseAt = InTick < 0 ? -1 : std::min<int64_t>( InTick, m_last );
}


bool NetReplayPlayback::Update( double InFrameSeconds )
{
    if( m_reader.IsEmpty() )
    {
        m_error = "the replay has no ticks";
        return false;
    }
    if( !m_paused && InFrameSeconds > 0.0 )
    {
        const double before = m_time;
        m_time += InFrameSeconds * GetTickRate() * m_speed;
        if( m_pauseAt >= 0 && before <= static_cast<double>( m_pauseAt ) && m_time >= static_cast<double>( m_pauseAt ) )
        {
            m_time = static_cast<double>( m_pauseAt );
            m_pauseAt = -1;
            m_paused = true;
        }
        else if( m_looping && m_time > m_loopTo )
        {
            m_time = m_loopFrom;
        }
        else if( m_time >= m_last )
        {
            m_time = m_last;
            m_paused = true;
        }
    }
    m_time = std::clamp( m_time, static_cast<double>( m_first ), static_cast<double>( m_last ) );
    return Present();
}


bool NetReplayPlayback::Present()
{
    const uint32_t shown = GetTick();
    // Between two ticks, the one after too (the client interpolates towards it). On a whole tick only
    // that tick: the next one's spawns and despawns would change the world a tick early.
    const uint32_t need = m_time > static_cast<double>( shown ) ? std::min( shown + 1, m_last ) : shown;
    const bool forward = m_fed && shown >= m_lastShown && shown >= m_historyStart && need >= m_fedTick && need - m_fedTick <= kMaxCatchUp;
    if( !forward )
    {
        if( !Rebuild( shown ) )
        {
            return false;
        }
    }
    for( uint32_t tick = m_fedTick + 1; tick <= need; ++tick )
    {
        NetReplayTick record;
        // A tick the replay doesn't have (a gap) is skipped: the next one says what changed.
        if( m_reader.ReadTick( tick, record ) )
        {
            Feed( record );
        }
        else if( !m_reader.GetError().empty() )
        {
            m_error = m_reader.GetError();
            return false;
        }
    }
    m_lastShown = shown;
    if( !m_hasRecord || m_record.Tick != shown )
    {
        m_hasRecord = m_reader.ReadTick( shown, m_record );
    }
    m_client.DriveRenderTick( m_time );
    return true;
}


bool NetReplayPlayback::Rebuild( uint32_t InTick )
{
    ++m_rebuilds;
    NetReplayTick record;
    if( !m_reader.ReadTick( InTick, record ) )
    {
        m_error = m_reader.GetError().empty() ? "the replay has no tick " + std::to_string( InTick ) : m_reader.GetError();
        return false;
    }
    // What every NetId is, from the chunk's keyframe (which names every live one) to this tick.
    m_table.clear();
    const uint32_t chunkFirst = m_reader.GetKeyframeTick( InTick );
    if( !m_reader.ForEach( chunkFirst, InTick, [this]( const NetReplayTick& InRecord ) {
            for( const NetReplayEntity& entity : InRecord.Entities )
            {
                m_table[entity.NetId] = entity;
            }
            return true;
        } ) )
    {
        m_error = m_reader.GetError();
        return false;
    }

    m_client.ResetHistory();
    SyncEntities( record, nullptr );
    BitWriter snapshot;
    NetSnapshot sent;
    NetReplicationMessages::WriteSnapshot( snapshot, -1, record.State, nullptr, std::numeric_limits<size_t>::max(), sent );
    Send( snapshot, false );
    m_fed = true;
    m_fedTick = InTick;
    m_historyStart = InTick;
    m_fedState = record.State;
    m_lastShown = InTick;
    ++m_ticksFed;
    return true;
}


void NetReplayPlayback::Feed( const NetReplayTick& InTick )
{
    for( const NetReplayEntity& entity : InTick.Entities )
    {
        m_table[entity.NetId] = entity;
    }
    SyncEntities( InTick, &m_fedState );
    BitWriter snapshot;
    NetSnapshot sent;
    NetReplicationMessages::WriteSnapshot( snapshot, m_fedTick, InTick.State, &m_fedState, std::numeric_limits<size_t>::max(), sent );
    Send( snapshot, false );
    m_fedTick = InTick.Tick;
    m_fedState = InTick.State;
    ++m_ticksFed;
}


void NetReplayPlayback::SyncEntities( const NetReplayTick& InTick, const NetSnapshot* InBefore )
{
    // Gone: everything the client has that the tick doesn't (a rebuild compares with the client).
    std::vector<uint32_t> gone;
    if( InBefore )
    {
        for( const auto& [id, state] : InBefore->Entities )
        {
            if( !InTick.State.Entities.count( id ) )
            {
                gone.push_back( id );
            }
        }
    }
    else
    {
        for( const auto& [id, handle] : m_client.GetEntities() )
        {
            if( !InTick.State.Entities.count( id ) )
            {
                gone.push_back( id );
            }
        }
    }
    for( const uint32_t id : gone )
    {
        if( !m_client.GetEntities().count( id ) )
        {
            continue;
        }
        BitWriter despawn;
        NetReplicationMessages::WriteDespawn( despawn, id );
        Send( despawn, true );
    }
    // New: spawned from their prefab (placed ones are the scene's, bound by AssignStaticIds).
    for( const auto& [id, state] : InTick.State.Entities )
    {
        if( m_client.GetEntities().count( id ) )
        {
            continue;
        }
        if( id < NetReplicationServer::kFirstDynamicId )
        {
            if( !m_warnedStatic )
            {
                m_warnedStatic = true;
                CLog::Log( CLog::LogType::Warning,
                    "Replay: " + Label( m_table, id ) + " is placed in the scene but was destroyed earlier in the playback; seeking back can't bring it back (reload the replay)" );
            }
            continue;
        }
        auto it = m_table.find( id );
        BitWriter spawn;
        NetReplicationMessages::WriteSpawn( spawn, id, it != m_table.end() ? it->second.Owner : 0, it != m_table.end() ? it->second.Prefab : std::string(), state );
        Send( spawn, true );
    }
    // Owners: what the client's copy says against the table.
    for( const auto& [id, handle] : m_client.GetEntities() )
    {
        auto it = m_table.find( id );
        const NetIdentity* identity = handle ? handle->TryGetComponent<NetIdentity>() : nullptr;
        if( it == m_table.end() || !identity || identity->GetOwner() == it->second.Owner )
        {
            continue;
        }
        BitWriter owner;
        NetReplicationMessages::WriteOwner( owner, id, it->second.Owner );
        Send( owner, true );
    }
}


void NetReplayPlayback::Send( const BitWriter& InMessage, bool InReliable )
{
    m_client.Deliver( InMessage.GetBytes(), InReliable );
}


NetReplayPlayback::Check NetReplayPlayback::CheckWorld( const NetScriptState* InScripts ) const
{
    Check check;
    check.Tick = GetTick();
    if( !m_hasRecord || m_record.Tick != check.Tick )
    {
        check.Problems.push_back( "the replay has no tick " + std::to_string( check.Tick ) );
        return check;
    }
    if( m_time != std::floor( m_time ) )
    {
        check.Problems.push_back( "between ticks (" + std::to_string( m_time ) + "): pause on a whole tick to check it" );
        return check;
    }
    for( const auto& [id, recorded] : m_record.State.Entities )
    {
        ++check.Entities;
        const std::string label = Label( m_table, id );
        auto bound = m_client.GetEntities().find( id );
        if( bound == m_client.GetEntities().end() || !bound->second )
        {
            check.Problems.push_back( label + ": not in the world" );
            continue;
        }
        const size_t problems = check.Problems.size();
        // What the client showed: the recorded state, bit for bit.
        const NetEntityState* shown = m_client.GetShownState( id );
        if( !shown || m_client.GetShownTick( id ) != check.Tick )
        {
            check.Problems.push_back( label + ": the client showed tick " + std::to_string( m_client.GetShownTick( id ) ) + ", not " + std::to_string( check.Tick ) );
        }
        else if( *shown != recorded )
        {
            check.Problems.push_back( label + ": the sample shown isn't the recorded state" );
        }
        // The world, captured as the server captures it.
        const NetEntityState captured = NetReplicationState::Capture( *bound->second.Get(), InScripts );
        if( captured.Position != recorded.Position )
        {
            check.Problems.push_back( label + ": position " + Describe( captured.GetPosition() ) + ", recorded " + Describe( recorded.GetPosition() ) );
        }
        if( captured.Rotation != recorded.Rotation && Quaternion::Angle( captured.GetRotation(), recorded.GetRotation() ) > 2e-3f )
        {
            check.Problems.push_back( label + ": rotation off by " + std::to_string( Quaternion::Angle( captured.GetRotation(), recorded.GetRotation() ) ) + " rad" );
        }
        if( captured.Fields != recorded.Fields )
        {
            size_t at = 0;
            while( at < captured.Fields.size() && at < recorded.Fields.size() && captured.Fields[at] == recorded.Fields[at] )
            {
                ++at;
            }
            check.Problems.push_back( label + ": fields differ from byte " + std::to_string( at ) + " (" + std::to_string( captured.Fields.size() ) + " bytes, recorded "
                + std::to_string( recorded.Fields.size() ) + ")" );
        }
        // Who owns it: the recording's owner as of this tick (the spawns and owner changes fed).
        auto entry = m_table.find( id );
        const NetIdentity* identity = bound->second->TryGetComponent<NetIdentity>();
        if( entry != m_table.end() && identity && identity->GetOwner() != entry->second.Owner )
        {
            check.Problems.push_back( label + ": owner " + std::to_string( identity->GetOwner() ) + ", recorded " + std::to_string( entry->second.Owner ) );
        }
        check.Matched += check.Problems.size() == problems ? 1 : 0;
    }
    // Nothing more in the world than the recording has.
    for( const auto& [id, handle] : m_client.GetEntities() )
    {
        if( !m_record.State.Entities.count( id ) )
        {
            check.Problems.push_back( Label( m_table, id ) + ": in the world, not in the recording" );
        }
    }
    return check;
}
