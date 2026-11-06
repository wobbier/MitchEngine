#include "PCH.h"
#include "Net/NetReplayTimeline.h"
#include "JSON.h"
#include "optick.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
    constexpr size_t kMaxLabelPayload = 160;
    // Hitches and misses this close to the run before (in ticks) join it: a server a tick behind
    // every other tick for a second is one hitch on the scrubber, not thirty.
    constexpr uint32_t kHitchMergeTicks = 30;
    constexpr uint32_t kMissMergeTicks = 10;

    std::string Named( const std::map<uint32_t, NetReplayEntity>& InTable, uint32_t InNetId )
    {
        if( InNetId == 0 )
        {
            return "-";
        }
        auto it = InTable.find( InNetId );
        return ( it != InTable.end() && !it->second.Name.empty() ? it->second.Name : std::string() ) + "#" + std::to_string( InNetId );
    }

    // An event's line: a kill's "killer > victim" when its payload says so (HeroShooter's), else
    // the payload, cut short.
    std::string EventLabel( const NetReplayEvent& InEvent, const std::map<uint32_t, NetReplayEntity>& InTable )
    {
        std::string text = InEvent.Name;
        const json payload = json::parse( InEvent.Payload, nullptr, false );
        if( !payload.is_discarded() && payload.is_object() && payload.contains( "killer" ) && payload.contains( "victim" ) && payload["killer"].is_string()
            && payload["victim"].is_string() )
        {
            text += " " + payload["killer"].get<std::string>() + " > " + payload["victim"].get<std::string>();
        }
        else if( !InEvent.Payload.empty() )
        {
            text += " " + ( InEvent.Payload.size() > kMaxLabelPayload ? InEvent.Payload.substr( 0, kMaxLabelPayload ) + "..." : InEvent.Payload );
        }
        if( InEvent.Entity != 0 )
        {
            text += " [" + Named( InTable, InEvent.Entity ) + "]";
        }
        return text;
    }
}


const char* NetReplayTimeline::KindName( NetReplayMarker::Kind InKind )
{
    switch( InKind )
    {
    case NetReplayMarker::Kind::Kill: return "kill";
    case NetReplayMarker::Kind::Join: return "join";
    case NetReplayMarker::Kind::Leave: return "leave";
    case NetReplayMarker::Kind::Event: return "event";
    case NetReplayMarker::Kind::Hit: return "hit";
    case NetReplayMarker::Kind::Miss: return "miss";
    case NetReplayMarker::Kind::Resync: return "resync";
    case NetReplayMarker::Kind::Hitch: return "hitch";
    default: return "?";
    }
}


bool NetReplayTimeline::ParseKind( const std::string& InName, NetReplayMarker::Kind& OutKind )
{
    for( uint8_t i = 0; i < static_cast<uint8_t>( NetReplayMarker::Kind::Count ); ++i )
    {
        if( InName == KindName( static_cast<NetReplayMarker::Kind>( i ) ) )
        {
            OutKind = static_cast<NetReplayMarker::Kind>( i );
            return true;
        }
    }
    return false;
}


bool NetReplayTimeline::Build( NetReplayReader& InReader, std::string* OutError, const std::atomic<bool>* InCancel )
{
    OPTICK_EVENT( "NetReplayTimeline::Build" );
    m_markers.clear();
    m_clients.clear();
    m_owed.clear();
    m_shots.clear();
    if( InReader.IsEmpty() )
    {
        m_first = m_last = 0;
        return true;
    }
    m_first = InReader.GetFirstTick();
    m_last = InReader.GetLastTick();
    const size_t length = static_cast<size_t>( m_last - m_first ) + 1;
    m_owed.assign( length, 0 );
    m_shots.assign( length, 0 );

    std::map<uint32_t, NetReplayEntity> table;
    std::map<uint8_t, size_t> clientIndex;
    // Open runs: a hitch (the marker's index), and each client's miss run.
    struct HitchStats
    {
        uint32_t MaxOwed = 0;
        uint32_t Behind = 0;        // ticks with some owed or dropped
        uint64_t Dropped = 0;
    };
    std::map<int, HitchStats> hitches;
    int hitch = -1;
    uint32_t hitchEnd = 0;
    std::map<uint8_t, std::pair<int, uint32_t>> missRuns;
    std::map<int, uint32_t> missCounts;    // a miss run's repeated ticks
    std::map<uint8_t, uint32_t> resyncs;
    bool cancelled = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    const bool read = InReader.ForEach( m_first, m_last, [&]( const NetReplayTick& InTick ) {
        if( InCancel && InCancel->load( std::memory_order_relaxed ) )
        {
            cancelled = true;
            return false;
        }
        const size_t at = InTick.Tick - m_first;
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            table[entity.NetId] = entity;
        }
        m_owed[at] = InTick.TicksOwed;

        // The server behind: owed ticks or dropped backlog, a run as one marker.
        if( InTick.TicksOwed > 0 || InTick.TicksDropped > 0 )
        {
            if( hitch >= 0 && InTick.Tick - hitchEnd <= kHitchMergeTicks )
            {
                m_markers[hitch].Ticks = InTick.Tick - m_markers[hitch].Tick + 1;
                ++hitches[hitch].Behind;
            }
            else
            {
                NetReplayMarker marker;
                marker.Tick = InTick.Tick;
                marker.Type = NetReplayMarker::Kind::Hitch;
                marker.Name = "hitch";
                hitch = static_cast<int>( m_markers.size() );
                m_markers.push_back( marker );
            }
            HitchStats& stats = hitches[hitch];
            stats.Behind = std::max<uint32_t>( stats.Behind, 1 );
            stats.MaxOwed = std::max( stats.MaxOwed, InTick.TicksOwed );
            stats.Dropped += InTick.TicksDropped;
            hitchEnd = InTick.Tick;
        }

        for( const NetReplayEvent& event : InTick.Events )
        {
            NetReplayMarker marker;
            marker.Tick = InTick.Tick;
            marker.Name = event.Name;
            marker.Entity = event.Entity;
            marker.Label = EventLabel( event, table );
            if( event.Name == "kill" )
            {
                marker.Type = NetReplayMarker::Kind::Kill;
            }
            else if( event.Name == "net.joined" || event.Name == "net.left" )
            {
                marker.Type = event.Name == "net.joined" ? NetReplayMarker::Kind::Join : NetReplayMarker::Kind::Leave;
                const json payload = json::parse( event.Payload, nullptr, false );
                if( !payload.is_discarded() && payload.is_object() && payload.contains( "client" ) && payload["client"].is_number_integer() )
                {
                    marker.Client = static_cast<uint8_t>( payload["client"].get<int>() );
                }
            }
            m_markers.push_back( std::move( marker ) );
        }

        for( const NetReplayShot& shot : InTick.Shots )
        {
            if( shot.Instigator != 0 )
            {
                continue;   // a projectile's sweep, not a trigger pull
            }
            m_shots[at] = static_cast<uint16_t>( std::min<uint32_t>( m_shots[at] + 1u, 0xFFFFu ) );
            if( shot.Hit )
            {
                NetReplayMarker marker;
                marker.Tick = InTick.Tick;
                marker.Type = NetReplayMarker::Kind::Hit;
                marker.Name = "hit";
                marker.Entity = shot.Shooter;
                marker.Client = shot.Client;
                marker.Label = "client " + std::to_string( shot.Client ) + " " + Named( table, shot.Shooter ) + " hit " + Named( table, shot.Target )
                    + ( shot.TargetPart.empty() ? "" : " (" + shot.TargetPart + ")" );
                m_markers.push_back( std::move( marker ) );
            }
        }

        for( const NetReplayClient& client : InTick.Clients )
        {
            auto found = clientIndex.find( client.Id );
            if( found == clientIndex.end() )
            {
                NetReplayClientSeries series;
                series.Client = client.Id;
                series.FirstTick = InTick.Tick;
                const size_t count = static_cast<size_t>( m_last - InTick.Tick ) + 1;
                series.RoundTripMs.assign( count, nan );
                series.PacketLoss.assign( count, nan );
                series.Slack.assign( count, nan );
                series.Missed.assign( count, 0 );
                found = clientIndex.emplace( client.Id, m_clients.size() ).first;
                m_clients.push_back( std::move( series ) );
            }
            NetReplayClientSeries& series = m_clients[found->second];
            const size_t index = InTick.Tick - series.FirstTick;
            series.LastTick = InTick.Tick;
            series.RoundTripMs[index] = client.RoundTripMs;
            series.PacketLoss[index] = client.PacketLoss;
            series.Slack[index] = client.Slack;
            series.Missed[index] = client.Missed ? 1 : 0;
            series.Misses = client.Misses;
            series.LateCommands = client.LateCommands;
            series.Resyncs = client.Resyncs;
            series.MaxRoundTripMs = std::max( series.MaxRoundTripMs, client.RoundTripMs );
            series.MaxSlack = std::max( series.MaxSlack, client.Slack );

            if( client.Missed )
            {
                auto run = missRuns.find( client.Id );
                if( run != missRuns.end() && InTick.Tick - run->second.second <= kMissMergeTicks )
                {
                    NetReplayMarker& marker = m_markers[run->second.first];
                    marker.Ticks = InTick.Tick - marker.Tick + 1;
                    ++missCounts[run->second.first];
                    run->second.second = InTick.Tick;
                }
                else
                {
                    NetReplayMarker marker;
                    marker.Tick = InTick.Tick;
                    marker.Type = NetReplayMarker::Kind::Miss;
                    marker.Name = "miss";
                    marker.Client = client.Id;
                    missRuns[client.Id] = { static_cast<int>( m_markers.size() ), InTick.Tick };
                    missCounts[static_cast<int>( m_markers.size() )] = 1;
                    m_markers.push_back( std::move( marker ) );
                }
            }
            auto before = resyncs.find( client.Id );
            if( before != resyncs.end() && client.Resyncs > before->second )
            {
                NetReplayMarker marker;
                marker.Tick = InTick.Tick;
                marker.Type = NetReplayMarker::Kind::Resync;
                marker.Name = "resync";
                marker.Client = client.Id;
                marker.Label = "resync: client " + std::to_string( client.Id ) + "'s clock went back (" + std::to_string( client.Resyncs ) + " so far)";
                m_markers.push_back( std::move( marker ) );
            }
            resyncs[client.Id] = client.Resyncs;
        }
        return true;
    } );
    if( !read )
    {
        if( OutError )
        {
            *OutError = InReader.GetError();
        }
        return false;
    }
    if( cancelled )
    {
        return false;
    }

    // Runs know their length now: their labels.
    for( size_t i = 0; i < m_markers.size(); ++i )
    {
        NetReplayMarker& marker = m_markers[i];
        const std::string span = marker.Ticks > 1 ? " over " + std::to_string( marker.Ticks ) + " ticks" : "";
        if( marker.Type == NetReplayMarker::Kind::Hitch )
        {
            const HitchStats& stats = hitches[static_cast<int>( i )];
            marker.Label = "hitch: the server was behind " + std::to_string( stats.Behind ) + " tick(s)" + span + " (up to " + std::to_string( stats.MaxOwed ) + " owed"
                + ( stats.Dropped ? ", " + std::to_string( stats.Dropped ) + " dropped" : "" ) + ")";
        }
        else if( marker.Type == NetReplayMarker::Kind::Miss )
        {
            const uint32_t missed = missCounts[static_cast<int>( i )];
            marker.Label = "miss: client " + std::to_string( marker.Client ) + "'s command hadn't arrived for " + std::to_string( missed ) + " tick(s)" + span
                + " (the server repeated the last)";
        }
    }
    std::stable_sort( m_markers.begin(), m_markers.end(), []( const NetReplayMarker& InA, const NetReplayMarker& InB ) { return InA.Tick < InB.Tick; } );
    return true;
}


size_t NetReplayTimeline::Count( NetReplayMarker::Kind InKind ) const
{
    return static_cast<size_t>( std::count_if( m_markers.begin(), m_markers.end(), [InKind]( const NetReplayMarker& InMarker ) { return InMarker.Type == InKind; } ) );
}


int NetReplayTimeline::Find( NetReplayMarker::Kind InKind, int InNth, const std::string& InName ) const
{
    std::vector<int> matches;
    for( size_t i = 0; i < m_markers.size(); ++i )
    {
        if( m_markers[i].Type == InKind && ( InName.empty() || m_markers[i].Name == InName ) )
        {
            matches.push_back( static_cast<int>( i ) );
        }
    }
    const int count = static_cast<int>( matches.size() );
    const int at = InNth < 0 ? count + InNth : InNth;
    return at >= 0 && at < count ? matches[at] : -1;
}


int NetReplayTimeline::Neighbour( uint32_t InTick, bool InForward, uint32_t InMask ) const
{
    const int count = static_cast<int>( m_markers.size() );
    if( InForward )
    {
        for( int i = 0; i < count; ++i )
        {
            if( m_markers[i].Tick > InTick && ( InMask & ( 1u << static_cast<uint32_t>( m_markers[i].Type ) ) ) )
            {
                return i;
            }
        }
        return -1;
    }
    for( int i = count - 1; i >= 0; --i )
    {
        if( m_markers[i].Tick < InTick && ( InMask & ( 1u << static_cast<uint32_t>( m_markers[i].Type ) ) ) )
        {
            return i;
        }
    }
    return -1;
}
