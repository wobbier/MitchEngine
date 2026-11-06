#include "PCH.h"
#include "Net/NetReplayDump.h"
#include "Core/CommandLine.h"
#include "JSON.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayTimeline.h"
#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <map>
#include <ostream>
#include <set>

namespace
{
    std::string Format( const char* InFormat, ... )
    {
        char buffer[512];
        va_list args;
        va_start( args, InFormat );
        std::vsnprintf( buffer, sizeof( buffer ), InFormat, args );
        va_end( args );
        return buffer;
    }

    std::string Hex( uint64_t InValue )
    {
        return Format( "%016" PRIx64, InValue );
    }

    std::string Vec( const Vector3& InValue )
    {
        return Format( "(%.3f, %.3f, %.3f)", InValue.x, InValue.y, InValue.z );
    }

    json VecJson( const Vector3& InValue )
    {
        return json::array( { InValue.x, InValue.y, InValue.z } );
    }

    std::string Bytes( const std::vector<uint8_t>& InBytes )
    {
        std::string text;
        for( const uint8_t byte : InBytes )
        {
            text += Format( "%02x", byte );
        }
        return text;
    }

    bool ParseTick( const std::string& InText, uint32_t& OutTick )
    {
        if( InText.empty() || InText.find_first_not_of( "0123456789" ) != std::string::npos || InText.size() > 10 )
        {
            return false;
        }
        const unsigned long long value = std::stoull( InText );
        if( value > 0xFFFFFFFFull )
        {
            return false;
        }
        OutTick = static_cast<uint32_t>( value );
        return true;
    }

    struct ClientSummary
    {
        uint64_t Ticks = 0;
        uint32_t First = 0;
        uint32_t Last = 0;
        uint32_t Misses = 0;
        uint32_t MissedTicks = 0;
        uint32_t LateCommands = 0;
        uint32_t Resyncs = 0;
        double RoundTripSum = 0.0;
        float RoundTripMax = 0.f;
        double LossSum = 0.0;
    };

    struct Summary
    {
        uint64_t Ticks = 0;
        uint32_t First = 0;
        uint32_t Last = 0;
        uint32_t MaxOwed = 0;
        uint64_t Dropped = 0;
        size_t MaxEntities = 0;
        std::map<std::string, uint64_t> Events;
        uint64_t Shots = 0;         // trigger pulls (and bots'), not projectile sweeps
        uint64_t Hits = 0;
        uint64_t Sweeps = 0;        // a projectile's per-tick sweeps (format 3: the shot has an instigator)
        uint64_t SweepHits = 0;
        uint64_t Occluded = 0;
        uint64_t Clamped = 0;
        uint64_t Rejected = 0;
        std::map<uint8_t, ClientSummary> Clients;
    };

    // Names and owners as the replay goes (Entities records), including the chunk's earlier ticks.
    struct Names
    {
        std::map<uint32_t, NetReplayEntity> Entities;
        // Every owner each NetId had, in order ("1>0": client 1's until it left).
        std::map<uint32_t, std::vector<uint8_t>> Owners;

        void Note( const NetReplayTick& InTick )
        {
            for( const NetReplayEntity& entity : InTick.Entities )
            {
                Entities[entity.NetId] = entity;
                std::vector<uint8_t>& owners = Owners[entity.NetId];
                if( owners.empty() || owners.back() != entity.Owner )
                {
                    owners.push_back( entity.Owner );
                }
            }
        }
        std::string OwnersOf( uint32_t InNetId ) const
        {
            std::string text;
            auto it = Owners.find( InNetId );
            for( const uint8_t owner : it != Owners.end() ? it->second : std::vector<uint8_t>{ 0 } )
            {
                text += ( text.empty() ? "" : ">" ) + std::to_string( owner );
            }
            return text;
        }
        std::string Of( uint32_t InNetId ) const
        {
            if( InNetId == 0 )
            {
                return "-";
            }
            auto it = Entities.find( InNetId );
            return it != Entities.end() && !it->second.Name.empty() ? it->second.Name + "#" + std::to_string( InNetId ) : "#" + std::to_string( InNetId );
        }
        uint8_t OwnerOf( uint32_t InNetId ) const
        {
            auto it = Entities.find( InNetId );
            return it != Entities.end() ? it->second.Owner : 0;
        }
    };

    bool ShotInvolves( const NetReplayShot& InShot, uint32_t InEntity )
    {
        return InEntity == 0 || InShot.Shooter == InEntity || InShot.Target == InEntity || ( InShot.Instigator != 0 && InShot.Instigator == InEntity );
    }

    std::string CommandText( const InputCommand& InCommand )
    {
        std::string text = Format( "buttons %08x axes %.3f %.3f %.3f %.3f", InCommand.Buttons, InCommand.Axes[0], InCommand.Axes[1], InCommand.Axes[2], InCommand.Axes[3] );
        if( InCommand.HasAim )
        {
            text += Format( " aim %.2f %.2f", InCommand.Yaw, InCommand.Pitch );
        }
        text += InCommand.ViewTick >= 0.0 ? Format( " view %.3f", InCommand.ViewTick ) : std::string( " view -" );
        return text;
    }

    json CommandJson( const InputCommand& InCommand )
    {
        json command;
        command["tick"] = InCommand.Tick;
        command["buttons"] = InCommand.Buttons;
        command["axes"] = json::array( { InCommand.Axes[0], InCommand.Axes[1], InCommand.Axes[2], InCommand.Axes[3] } );
        if( InCommand.HasAim )
        {
            command["yaw"] = InCommand.Yaw;
            command["pitch"] = InCommand.Pitch;
        }
        command["viewTick"] = InCommand.ViewTick;
        return command;
    }

    std::string ShotText( const NetReplayShot& InShot, const Names& InNames )
    {
        std::string text = Format( "shot client %u %s ", InShot.Client, InNames.Of( InShot.Shooter ).c_str() );
        if( InShot.Instigator != 0 )
        {
            text += "(sweep, fired by " + InNames.Of( InShot.Instigator ) + ") ";
        }
        if( InShot.Rejected )
        {
            text += "REJECTED (view tick in the future)";
        }
        else if( InShot.Hit )
        {
            text += "HIT " + InNames.Of( InShot.Target ) + ( InShot.TargetPart.empty() ? "" : " (" + InShot.TargetPart + ")" ) + " at " + Vec( InShot.Point )
                + Format( " d=%.2f", InShot.Distance );
        }
        else if( InShot.Occluded )
        {
            text += "MISS (occluded; behind it: " + InNames.Of( InShot.Target ) + ( InShot.TargetPart.empty() ? "" : " (" + InShot.TargetPart + ")" ) + ")";
        }
        else
        {
            text += "MISS";
        }
        text += " from " + Vec( InShot.Origin ) + " dir " + Vec( InShot.Direction ) + Format( " range %.1f", InShot.Range );
        text += InShot.ViewTick >= 0.0 ? Format( " view %.3f", InShot.ViewTick ) : std::string( " view -" );
        text += Format( " rewound %.3f ticks", InShot.RewoundTicks );
        if( InShot.Clamped )
        {
            text += " (clamped at the cap)";
        }
        text += Format( ", %u tested", InShot.Tested );
        return text;
    }

    json ShotJson( const NetReplayShot& InShot )
    {
        json shot;
        shot["client"] = InShot.Client;
        shot["shooter"] = InShot.Shooter;
        shot["instigator"] = InShot.Instigator;
        shot["origin"] = VecJson( InShot.Origin );
        shot["direction"] = VecJson( InShot.Direction );
        shot["range"] = InShot.Range;
        shot["viewTick"] = InShot.ViewTick;
        shot["rewoundTicks"] = InShot.RewoundTicks;
        shot["hit"] = InShot.Hit;
        shot["occluded"] = InShot.Occluded;
        shot["clamped"] = InShot.Clamped;
        shot["rejected"] = InShot.Rejected;
        shot["target"] = InShot.Target;
        shot["targetPart"] = InShot.TargetPart;
        shot["point"] = VecJson( InShot.Point );
        shot["distance"] = InShot.Distance;
        shot["tested"] = InShot.Tested;
        json hitboxes = json::array();
        for( const NetReplayHitbox& hitbox : InShot.Hitboxes )
        {
            json entry;
            entry["netId"] = hitbox.NetId;
            entry["name"] = hitbox.Name;
            entry["shape"] = hitbox.Shape == 0 ? "capsule" : "box";
            entry["position"] = VecJson( hitbox.Position );
            entry["rotation"] = json::array( { hitbox.Rotation.x, hitbox.Rotation.y, hitbox.Rotation.z, hitbox.Rotation.w } );
            entry["radius"] = hitbox.Radius;
            entry["halfHeight"] = hitbox.HalfHeight;
            entry["halfExtents"] = VecJson( hitbox.HalfExtents );
            entry["occluder"] = hitbox.Occluder;
            hitboxes.push_back( entry );
        }
        shot["hitboxes"] = hitboxes;
        return shot;
    }

    json HeaderJson( const NetReplayHeader& InHeader )
    {
        json header;
        header["formatVersion"] = InHeader.FormatVersion;
        header["product"] = InHeader.Product;
        header["engineCommit"] = InHeader.EngineCommit;
        header["gameCommit"] = InHeader.GameCommit;
        header["map"] = InHeader.Map;
        header["commandLine"] = InHeader.CommandLine;
        header["recordedAt"] = InHeader.RecordedAt;
        header["tickRate"] = InHeader.TickRate;
        header["keyframeInterval"] = InHeader.KeyframeInterval;
        header["netProtocol"] = InHeader.NetProtocol;
        header["buildHash"] = Hex( InHeader.BuildHash );
        header["scriptSchemaHash"] = Hex( InHeader.ScriptSchemaHash );
        header["sceneHash"] = Hex( InHeader.SceneHash );
        header["netSchemaHash"] = Hex( InHeader.NetSchemaHash );
        return header;
    }
}


bool NetReplayDump::ParseArgs( const std::vector<std::string>& InArgs, Options& OutOptions, std::string& OutError )
{
    OutOptions = Options();
    bool any = false;
    for( size_t i = 1; i < InArgs.size(); ++i )
    {
        const std::string& arg = InArgs[i];
        const bool hasValue = i + 1 < InArgs.size();
        if( arg == "--replay-dump" )
        {
            if( !hasValue || InArgs[i + 1].rfind( "--", 0 ) == 0 )
            {
                OutError = "--replay-dump: which file?";
                return false;
            }
            OutOptions.Path = InArgs[++i];
        }
        else if( arg == "--ticks" )
        {
            const std::string range = hasValue ? InArgs[++i] : std::string();
            const size_t colon = range.find( ':' );
            const std::string from = colon == std::string::npos ? range : range.substr( 0, colon );
            const std::string to = colon == std::string::npos ? range : range.substr( colon + 1 );
            if( range.empty() || ( !from.empty() && !ParseTick( from, OutOptions.From ) ) || ( !to.empty() && !ParseTick( to, OutOptions.To ) ) || OutOptions.From > OutOptions.To )
            {
                OutError = "--ticks " + range + ": expected a:b, a: or :b (server ticks, a <= b)";
                return false;
            }
        }
        else if( arg == "--entity" )
        {
            if( !hasValue || !ParseTick( InArgs[i + 1], OutOptions.Entity ) || OutOptions.Entity == 0 )
            {
                OutError = "--entity: expected a NetId (1 or more)";
                return false;
            }
            ++i;
        }
        else if( arg == "--summary" )
        {
            OutOptions.Summary = any = true;
        }
        else if( arg == "--events" )
        {
            OutOptions.Events = any = true;
        }
        else if( arg == "--shots" )
        {
            OutOptions.Shots = any = true;
        }
        else if( arg == "--commands" )
        {
            OutOptions.Commands = any = true;
        }
        else if( arg == "--net" )
        {
            OutOptions.Net = any = true;
        }
        else if( arg == "--states" )
        {
            OutOptions.States = any = true;
        }
        else if( arg == "--markers" )
        {
            OutOptions.Markers = any = true;
        }
        else if( arg == "--json" )
        {
            OutOptions.Json = true;
        }
    }
    if( OutOptions.Path.empty() )
    {
        OutError = "--replay-dump: which file?";
        return false;
    }
    if( !any )
    {
        OutOptions.Summary = OutOptions.Events = OutOptions.Shots = OutOptions.CommandChanges = true;
    }
    return true;
}


bool NetReplayDump::Write( NetReplayReader& InReader, const Options& InOptions, std::ostream& InOut, std::string& OutError )
{
    const NetReplayHeader& header = InReader.GetHeader();
    if( InReader.IsEmpty() )
    {
        OutError = "the replay has no ticks";
        return false;
    }
    // Start at the chunk holding the first tick asked for: its earlier ticks name the entities.
    const uint32_t from = std::max( InOptions.From, InReader.GetFirstTick() );
    const uint32_t to = std::min( InOptions.To, InReader.GetLastTick() );

    // Pass 1: the summary.
    Summary summary;
    Names names;
    std::set<uint32_t> seenEntities;
    if( !InReader.ForEach( 0, to, [&]( const NetReplayTick& InTick ) {
            names.Note( InTick );
            if( InTick.Tick < from )
            {
                return true;
            }
            summary.First = summary.Ticks == 0 ? InTick.Tick : summary.First;
            summary.Last = InTick.Tick;
            ++summary.Ticks;
            summary.MaxOwed = std::max( summary.MaxOwed, InTick.TicksOwed );
            summary.Dropped += InTick.TicksDropped;
            summary.MaxEntities = std::max( summary.MaxEntities, InTick.State.Entities.size() );
            for( const auto& [id, state] : InTick.State.Entities )
            {
                seenEntities.insert( id );
            }
            for( const NetReplayEvent& event : InTick.Events )
            {
                if( InOptions.Entity == 0 || event.Entity == InOptions.Entity )
                {
                    ++summary.Events[event.Name];
                }
            }
            for( const NetReplayShot& shot : InTick.Shots )
            {
                if( !ShotInvolves( shot, InOptions.Entity ) )
                {
                    continue;
                }
                if( shot.Instigator != 0 )
                {
                    ++summary.Sweeps;
                    summary.SweepHits += shot.Hit ? 1 : 0;
                    continue;
                }
                ++summary.Shots;
                summary.Hits += shot.Hit ? 1 : 0;
                summary.Occluded += shot.Occluded ? 1 : 0;
                summary.Clamped += shot.Clamped ? 1 : 0;
                summary.Rejected += shot.Rejected ? 1 : 0;
            }
            for( const NetReplayClient& client : InTick.Clients )
            {
                ClientSummary& entry = summary.Clients[client.Id];
                entry.First = entry.Ticks == 0 ? InTick.Tick : entry.First;
                entry.Last = InTick.Tick;
                ++entry.Ticks;
                entry.Misses = client.Misses;
                entry.MissedTicks += client.Missed ? 1 : 0;
                entry.LateCommands = client.LateCommands;
                entry.Resyncs = client.Resyncs;
                entry.RoundTripSum += client.RoundTripMs;
                entry.RoundTripMax = std::max( entry.RoundTripMax, client.RoundTripMs );
                entry.LossSum += client.PacketLoss;
            }
            return true;
        } ) )
    {
        OutError = InReader.GetError();
        return false;
    }

    json document;
    if( InOptions.Summary )
    {
        if( InOptions.Json )
        {
            document["header"] = HeaderJson( header );
            json counts;
            counts["ticks"] = summary.Ticks;
            counts["firstTick"] = summary.First;
            counts["lastTick"] = summary.Last;
            counts["seconds"] = header.TickRate ? static_cast<double>( summary.Ticks ) / header.TickRate : 0.0;
            counts["maxTicksOwed"] = summary.MaxOwed;
            counts["ticksDropped"] = summary.Dropped;
            counts["maxEntities"] = summary.MaxEntities;
            counts["events"] = summary.Events;
            counts["shots"] = { { "total", summary.Shots }, { "hits", summary.Hits }, { "misses", summary.Shots - summary.Hits }, { "occluded", summary.Occluded },
                { "clamped", summary.Clamped }, { "rejected", summary.Rejected } };
            counts["sweeps"] = { { "total", summary.Sweeps }, { "hits", summary.SweepHits } };
            json clients = json::array();
            for( const auto& [id, entry] : summary.Clients )
            {
                clients.push_back( { { "id", id }, { "ticks", entry.Ticks }, { "firstTick", entry.First }, { "lastTick", entry.Last }, { "misses", entry.Misses },
                    { "missedTicks", entry.MissedTicks }, { "lateCommands", entry.LateCommands }, { "resyncs", entry.Resyncs },
                    { "roundTripMsAverage", entry.Ticks ? entry.RoundTripSum / entry.Ticks : 0.0 }, { "roundTripMsMax", entry.RoundTripMax },
                    { "packetLossAverage", entry.Ticks ? entry.LossSum / entry.Ticks : 0.0 } } );
            }
            counts["clients"] = clients;
            json entities = json::array();
            for( const auto& [id, entity] : names.Entities )
            {
                if( seenEntities.count( id ) && ( InOptions.Entity == 0 || id == InOptions.Entity ) )
                {
                    entities.push_back( { { "netId", id }, { "name", entity.Name }, { "prefab", entity.Prefab }, { "owner", entity.Owner },
                        { "owners", names.Owners[id] } } );
                }
            }
            counts["entities"] = entities;
            document["summary"] = counts;
        }
        else
        {
            InOut << "replay " << InOptions.Path << " (format " << header.FormatVersion << ")\n";
            InOut << "  product      " << header.Product << "\n";
            InOut << "  recorded     " << header.RecordedAt << "\n";
            InOut << "  map          " << header.Map << "\n";
            InOut << "  engine       " << ( header.EngineCommit.empty() ? "?" : header.EngineCommit ) << "\n";
            InOut << "  game         " << ( header.GameCommit.empty() ? "?" : header.GameCommit ) << "\n";
            InOut << "  command line " << header.CommandLine << "\n";
            InOut << Format( "  %u Hz, a keyframe every %u ticks, protocol %u; build %s scripts %s scene %s netschema %s\n", header.TickRate, header.KeyframeInterval,
                header.NetProtocol, Hex( header.BuildHash ).c_str(), Hex( header.ScriptSchemaHash ).c_str(), Hex( header.SceneHash ).c_str(), Hex( header.NetSchemaHash ).c_str() );
            InOut << Format( "ticks %u..%u: %" PRIu64 " ticks (%.1f s); up to %zu entities; most ticks owed %u, %" PRIu64 " dropped\n", summary.First, summary.Last, summary.Ticks,
                header.TickRate ? static_cast<double>( summary.Ticks ) / header.TickRate : 0.0, summary.MaxEntities, summary.MaxOwed, summary.Dropped );
            InOut << "entities\n";
            for( const auto& [id, entity] : names.Entities )
            {
                if( seenEntities.count( id ) && ( InOptions.Entity == 0 || id == InOptions.Entity ) )
                {
                    InOut << Format( "  #%-6u %-24s owner %-5s %s\n", id, entity.Name.c_str(), names.OwnersOf( id ).c_str(), entity.Prefab.c_str() );
                }
            }
            InOut << "clients\n";
            for( const auto& [id, entry] : summary.Clients )
            {
                InOut << Format( "  client %u: ticks %u..%u, rtt %.1f ms avg / %.1f max, loss %.1f%%, %u misses (%u ticks repeated here), %u late, %u resyncs\n", id, entry.First,
                    entry.Last, entry.Ticks ? entry.RoundTripSum / entry.Ticks : 0.0, entry.RoundTripMax, entry.Ticks ? 100.0 * entry.LossSum / entry.Ticks : 0.0, entry.Misses,
                    entry.MissedTicks, entry.LateCommands, entry.Resyncs );
            }
            InOut << "events";
            for( const auto& [name, count] : summary.Events )
            {
                InOut << " " << name << " x" << count;
            }
            InOut << "\n";
            InOut << Format( "shots %" PRIu64 ": %" PRIu64 " hit, %" PRIu64 " missed (%" PRIu64 " occluded), %" PRIu64 " clamped, %" PRIu64 " rejected\n", summary.Shots,
                summary.Hits, summary.Shots - summary.Hits, summary.Occluded, summary.Clamped, summary.Rejected );
            if( summary.Sweeps > 0 )
            {
                InOut << Format( "projectile sweeps %" PRIu64 ": %" PRIu64 " hit\n", summary.Sweeps, summary.SweepHits );
            }
        }
    }

    // The timeline's markers (NetReplayTimeline: what the viewer's scrubber jumps to).
    if( InOptions.Markers )
    {
        NetReplayTimeline timeline;
        std::string error;
        if( !timeline.Build( InReader, &error ) )
        {
            OutError = error;
            return false;
        }
        json markers = json::array();
        for( const NetReplayMarker& marker : timeline.GetMarkers() )
        {
            if( marker.Tick < from || marker.Tick > to || ( InOptions.Entity && marker.Entity != InOptions.Entity ) )
            {
                continue;
            }
            if( InOptions.Json )
            {
                markers.push_back( { { "tick", marker.Tick }, { "kind", NetReplayTimeline::KindName( marker.Type ) }, { "name", marker.Name }, { "label", marker.Label },
                    { "entity", marker.Entity }, { "client", marker.Client }, { "ticks", marker.Ticks } } );
            }
            else
            {
                InOut << marker.Tick << " marker " << NetReplayTimeline::KindName( marker.Type ) << " " << marker.Label << "\n";
            }
        }
        if( InOptions.Json )
        {
            document["markers"] = markers;
        }
        else
        {
            InOut << "markers:";
            for( uint8_t i = 0; i < static_cast<uint8_t>( NetReplayMarker::Kind::Count ); ++i )
            {
                InOut << " " << NetReplayTimeline::KindName( static_cast<NetReplayMarker::Kind>( i ) ) << " " << timeline.Count( static_cast<NetReplayMarker::Kind>( i ) );
            }
            InOut << " (whole replay)\n";
        }
    }

    // Pass 2: the ticks.
    if( !( InOptions.Events || InOptions.Shots || InOptions.Commands || InOptions.CommandChanges || InOptions.Net || InOptions.States ) )
    {
        if( InOptions.Json )
        {
            InOut << document.dump( 2 ) << "\n";
        }
        return true;
    }
    names = Names();
    json ticks = json::array();
    std::map<uint8_t, InputCommand> lastCommand;
    if( !InReader.ForEach( 0, to, [&]( const NetReplayTick& InTick ) {
            names.Note( InTick );
            const uint8_t owner = InOptions.Entity ? names.OwnerOf( InOptions.Entity ) : 0;
            auto commandWanted = [&]( const NetReplayClient& InClient ) {
                if( InOptions.Entity && InClient.Id != owner )
                {
                    return false;
                }
                if( InOptions.Commands )
                {
                    return true;
                }
                auto last = lastCommand.find( InClient.Id );
                const bool changed = last == lastCommand.end() || last->second.Buttons != InClient.Command.Buttons || last->second.Axes != InClient.Command.Axes
                    || last->second.HasAim != InClient.Command.HasAim || last->second.Yaw != InClient.Command.Yaw || last->second.Pitch != InClient.Command.Pitch;
                return InOptions.CommandChanges && ( changed || InClient.Missed );
            };
            const bool inRange = InTick.Tick >= from;
            json tick;
            bool any = false;
            if( inRange && InOptions.Net )
            {
                if( InOptions.Json )
                {
                    tick["ticksOwed"] = InTick.TicksOwed;
                    tick["ticksDropped"] = InTick.TicksDropped;
                    json net = json::array();
                    for( const NetReplayClient& client : InTick.Clients )
                    {
                        net.push_back( { { "client", client.Id }, { "roundTripMs", client.RoundTripMs }, { "packetLoss", client.PacketLoss }, { "slack", client.Slack },
                            { "missed", client.Missed }, { "misses", client.Misses }, { "lateCommands", client.LateCommands }, { "resyncs", client.Resyncs } } );
                    }
                    tick["net"] = net;
                }
                else
                {
                    InOut << Format( "%u net owed %u dropped %u", InTick.Tick, InTick.TicksOwed, InTick.TicksDropped );
                    for( const NetReplayClient& client : InTick.Clients )
                    {
                        InOut << Format( " | client %u rtt %.1f loss %.1f%% slack %.2f misses %u%s late %u resyncs %u", client.Id, client.RoundTripMs, client.PacketLoss * 100.f,
                            client.Slack, client.Misses, client.Missed ? " (missed)" : "", client.LateCommands, client.Resyncs );
                    }
                    InOut << "\n";
                }
                any = true;
            }
            if( inRange && InOptions.Events )
            {
                json events = json::array();
                for( const NetReplayEvent& event : InTick.Events )
                {
                    if( InOptions.Entity && event.Entity != InOptions.Entity )
                    {
                        continue;
                    }
                    if( InOptions.Json )
                    {
                        events.push_back( { { "name", event.Name }, { "payload", event.Payload }, { "entity", event.Entity } } );
                    }
                    else
                    {
                        InOut << InTick.Tick << " event " << event.Name << " " << event.Payload << ( event.Entity ? " [" + names.Of( event.Entity ) + "]" : std::string() ) << "\n";
                    }
                }
                if( !events.empty() )
                {
                    tick["events"] = events;
                    any = true;
                }
            }
            if( inRange && InOptions.Shots )
            {
                json shots = json::array();
                for( const NetReplayShot& shot : InTick.Shots )
                {
                    if( !ShotInvolves( shot, InOptions.Entity ) )
                    {
                        continue;
                    }
                    if( InOptions.Json )
                    {
                        shots.push_back( ShotJson( shot ) );
                    }
                    else
                    {
                        InOut << InTick.Tick << " " << ShotText( shot, names ) << "\n";
                        for( const NetReplayHitbox& hitbox : shot.Hitboxes )
                        {
                            InOut << Format( "    hitbox %s %s %s at %s r %.2f h %.2f%s\n", names.Of( hitbox.NetId ).c_str(), hitbox.Name.c_str(), hitbox.Shape == 0 ? "capsule" : "box",
                                Vec( hitbox.Position ).c_str(), hitbox.Radius, hitbox.HalfHeight, hitbox.Occluder ? " occluder" : "" );
                        }
                    }
                }
                if( !shots.empty() )
                {
                    tick["shots"] = shots;
                    any = true;
                }
            }
            if( InOptions.Commands || InOptions.CommandChanges )
            {
                json commands = json::array();
                for( const NetReplayClient& client : InTick.Clients )
                {
                    if( inRange && commandWanted( client ) )
                    {
                        if( InOptions.Json )
                        {
                            json entry = CommandJson( client.Command );
                            entry["client"] = client.Id;
                            entry["missed"] = client.Missed;
                            commands.push_back( entry );
                        }
                        else
                        {
                            InOut << InTick.Tick << " command client " << static_cast<int>( client.Id ) << " " << CommandText( client.Command ) << ( client.Missed ? " (missed: repeated)" : "" ) << "\n";
                        }
                    }
                    lastCommand[client.Id] = client.Command;
                }
                if( !commands.empty() )
                {
                    tick["commands"] = commands;
                    any = true;
                }
            }
            if( inRange && InOptions.States )
            {
                json states = json::object();
                for( const auto& [id, state] : InTick.State.Entities )
                {
                    if( InOptions.Entity && id != InOptions.Entity )
                    {
                        continue;
                    }
                    const Vector3 position = state.GetPosition();
                    const Quaternion rotation = state.GetRotation();
                    if( InOptions.Json )
                    {
                        states[std::to_string( id )] = { { "position", VecJson( position ) }, { "rotation", json::array( { rotation.x, rotation.y, rotation.z, rotation.w } ) },
                            { "fields", Bytes( state.Fields ) } };
                    }
                    else
                    {
                        InOut << InTick.Tick << " state " << names.Of( id ) << " at " << Vec( position ) << Format( " rot (%.3f, %.3f, %.3f, %.3f)", rotation.x, rotation.y, rotation.z, rotation.w )
                              << " fields " << Bytes( state.Fields ) << "\n";
                    }
                }
                if( !states.empty() )
                {
                    tick["states"] = states;
                    any = true;
                }
                // Characters' movers at full precision (format 3).
                json movers = json::object();
                for( const NetReplayMover& mover : InTick.Movers )
                {
                    if( InOptions.Entity && mover.NetId != InOptions.Entity )
                    {
                        continue;
                    }
                    const CharacterController::NetState& state = mover.State;
                    if( InOptions.Json )
                    {
                        movers[std::to_string( mover.NetId )] = { { "position", VecJson( state.Position ) }, { "velocity", VecJson( state.Velocity ) },
                            { "moveInput", VecJson( state.MoveInput ) }, { "pendingMove", VecJson( state.PendingMove ) }, { "groundNormal", VecJson( state.GroundNormal ) },
                            { "wantsJump", state.WantsJump }, { "grounded", state.Grounded }, { "predicted", Bytes( mover.Predicted ) } };
                    }
                    else
                    {
                        InOut << InTick.Tick << " mover " << names.Of( mover.NetId ) << Format( " at (%.6f, %.6f, %.6f)", state.Position.x, state.Position.y, state.Position.z )
                              << " vel " << Vec( state.Velocity ) << " input " << Vec( state.MoveInput ) << ( state.Grounded ? " grounded" : " airborne" )
                              << ( state.WantsJump ? " jump" : "" ) << " predicted " << Bytes( mover.Predicted ) << "\n";
                    }
                }
                if( !movers.empty() )
                {
                    tick["movers"] = movers;
                    any = true;
                }
            }
            if( any && InOptions.Json )
            {
                tick["tick"] = InTick.Tick;
                ticks.push_back( tick );
            }
            return true;
        } ) )
    {
        OutError = InReader.GetError();
        return false;
    }
    if( InOptions.Json )
    {
        document["ticks"] = ticks;
        InOut << document.dump( 2 ) << "\n";
    }
    return true;
}


bool NetReplayDump::IsRequested()
{
    return CommandLine::Has( "--replay-dump" );
}


int NetReplayDump::Run()
{
    Options options;
    std::string error;
    if( !ParseArgs( CommandLine::GetArgs(), options, error ) )
    {
        std::cerr << "ReplayDump: " << error << "\n";
        return 2;
    }
    NetReplayFileSource source;
    if( !source.Open( options.Path ) )
    {
        std::cerr << "ReplayDump: " << source.GetError() << "\n";
        return 1;
    }
    if( !source.HasIndex() && !options.Json )
    {
        std::cout << "(no index: the recording wasn't closed; read up to its last whole chunk)\n";
    }
    NetReplayReader reader( source );
    if( !Write( reader, options, std::cout, error ) )
    {
        std::cerr << "ReplayDump: " << options.Path << ": " << error << "\n";
        return 1;
    }
    std::cout.flush();
    return 0;
}
