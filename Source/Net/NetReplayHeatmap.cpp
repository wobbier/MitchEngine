#include "PCH.h"
#include "Net/NetReplayHeatmap.h"
#include "optick.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <set>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

// zlib comes with Assimp's contrib (zlibstatic is linked on every platform): the PNG's deflate and CRC.
#include "../../ThirdParty/Assimp/contrib/zlib/zlib.h"

namespace
{
    const char* const kLayerNames[] = { "positions", "deaths", "kills" };

    // A hero's name: its prefab's file name ("Striker" from Assets/Prefabs/Heroes/Striker.prefab), else
    // the entity's name.
    std::string HeroOf( const NetReplayEntity& InEntity )
    {
        if( !InEntity.Prefab.empty() )
        {
            return std::filesystem::path( InEntity.Prefab ).stem().string();
        }
        return InEntity.Name;
    }

    bool ReadVector( const json& InValue, Vector3& OutVector )
    {
        if( !InValue.is_array() || InValue.size() != 3 || !InValue[0].is_number() || !InValue[1].is_number() || !InValue[2].is_number() )
        {
            return false;
        }
        OutVector = Vector3( InValue[0].get<float>(), InValue[1].get<float>(), InValue[2].get<float>() );
        return std::isfinite( OutVector.x ) && std::isfinite( OutVector.y ) && std::isfinite( OutVector.z );
    }

    std::string ReadName( const json& InPayload, const char* InKey )
    {
        if( !InPayload.is_object() || !InPayload.contains( InKey ) )
        {
            return std::string();
        }
        const json& value = InPayload[InKey];
        if( value.is_string() )
        {
            return value.get<std::string>();
        }
        if( value.is_number_integer() )
        {
            return std::to_string( value.get<int64_t>() );
        }
        return std::string();
    }

    uint32_t ReadNetId( const json& InPayload, const char* InKey )
    {
        if( !InPayload.is_object() || !InPayload.contains( InKey ) || !InPayload[InKey].is_number_integer() )
        {
            return 0;
        }
        const int64_t value = InPayload[InKey].get<int64_t>();
        return value > 0 && value <= 0xFFFFFFFFll ? static_cast<uint32_t>( value ) : 0u;
    }

    std::string Hex( uint64_t InValue )
    {
        char text[32];
        std::snprintf( text, sizeof( text ), "%016llx", static_cast<unsigned long long>( InValue ) );
        return text;
    }

    void PutU32( std::vector<uint8_t>& InOut, uint32_t InValue )
    {
        InOut.push_back( static_cast<uint8_t>( InValue >> 24 ) );
        InOut.push_back( static_cast<uint8_t>( InValue >> 16 ) );
        InOut.push_back( static_cast<uint8_t>( InValue >> 8 ) );
        InOut.push_back( static_cast<uint8_t>( InValue ) );
    }

    void PngChunk( std::vector<uint8_t>& InOut, const char* InType, const std::vector<uint8_t>& InData )
    {
        PutU32( InOut, static_cast<uint32_t>( InData.size() ) );
        const size_t typeAt = InOut.size();
        InOut.insert( InOut.end(), InType, InType + 4 );
        InOut.insert( InOut.end(), InData.begin(), InData.end() );
        const uLong crc = crc32( 0L, InOut.data() + typeAt, static_cast<uInt>( 4 + InData.size() ) );
        PutU32( InOut, static_cast<uint32_t>( crc ) );
    }

    uint8_t ToByte( float InValue )
    {
        return static_cast<uint8_t>( std::clamp( InValue, 0.f, 1.f ) * 255.f + 0.5f );
    }
}


const char* NetReplayHeatmap::LayerName( Layer InLayer )
{
    const size_t index = static_cast<size_t>( InLayer );
    return index < kLayerCount ? kLayerNames[index] : "?";
}


bool NetReplayHeatmap::ParseLayer( const std::string& InName, Layer& OutLayer )
{
    for( size_t i = 0; i < kLayerCount; ++i )
    {
        if( InName == kLayerNames[i] || ( InName.size() > 1 && std::string( kLayerNames[i] ).rfind( InName, 0 ) == 0 ) )
        {
            OutLayer = static_cast<Layer>( i );
            return true;
        }
    }
    return false;
}


int64_t NetReplayHeatmap::Key( int InX, int InZ )
{
    return static_cast<int64_t>( static_cast<uint64_t>( static_cast<uint32_t>( InX ) ) << 32 | static_cast<uint32_t>( InZ ) );
}


void NetReplayHeatmap::Unkey( int64_t InKey, int& OutX, int& OutZ )
{
    OutX = static_cast<int32_t>( static_cast<uint32_t>( static_cast<uint64_t>( InKey ) >> 32 ) );
    OutZ = static_cast<int32_t>( static_cast<uint32_t>( static_cast<uint64_t>( InKey ) ) );
}


void NetReplayHeatmap::CellOf( const Vector3& InPosition, int& OutX, int& OutZ ) const
{
    const float cell = m_settings.CellSize > 0.f ? m_settings.CellSize : 1.f;
    OutX = static_cast<int>( std::floor( InPosition.x / cell ) );
    OutZ = static_cast<int>( std::floor( InPosition.z / cell ) );
}


Vector3 NetReplayHeatmap::CellCentre( int InX, int InZ ) const
{
    const float cell = m_settings.CellSize > 0.f ? m_settings.CellSize : 1.f;
    return Vector3( ( static_cast<float>( InX ) + 0.5f ) * cell, FloorAt( InX, InZ ), ( static_cast<float>( InZ ) + 0.5f ) * cell );
}


float NetReplayHeatmap::FloorAt( int InX, int InZ ) const
{
    auto it = m_floor.find( Key( InX, InZ ) );
    return it != m_floor.end() ? it->second : std::numeric_limits<float>::quiet_NaN();
}


void NetReplayHeatmap::AddPoint( Layer InLayer, const Vector3& InPosition, const std::string& InHero, const std::string& InTeam, double InWeight )
{
    const size_t layer = static_cast<size_t>( InLayer );
    if( layer >= kLayerCount || !std::isfinite( InPosition.x ) || !std::isfinite( InPosition.z ) )
    {
        return;
    }
    if( m_settings.MaxDistance > 0.f && ( std::abs( InPosition.x ) > m_settings.MaxDistance || std::abs( InPosition.z ) > m_settings.MaxDistance ) )
    {
        ++m_outOfRange;
        return;
    }
    int x = 0;
    int z = 0;
    CellOf( InPosition, x, z );
    const int64_t key = Key( x, z );
    m_layers[layer]["all"][key] += InWeight;
    if( !InHero.empty() )
    {
        m_layers[layer]["hero:" + InHero][key] += InWeight;
    }
    if( !InTeam.empty() )
    {
        m_layers[layer]["team:" + InTeam][key] += InWeight;
    }
    if( std::isfinite( InPosition.y ) )
    {
        auto [floor, added] = m_floor.try_emplace( key, InPosition.y );
        if( !added )
        {
            floor->second = std::min( floor->second, InPosition.y );
        }
    }
}


bool NetReplayHeatmap::AddReplay( NetReplayReader& InReader, const std::string& InPath, std::string* OutError, const std::atomic<bool>* InCancel )
{
    OPTICK_EVENT( "NetReplayHeatmap::AddReplay" );
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    if( InReader.IsEmpty() )
    {
        return fail( "the replay has no ticks" );
    }
    const NetReplayHeader& header = InReader.GetHeader();
    if( !m_sources.empty() && header.SceneHash != 0 && m_sceneHash != 0 && header.SceneHash != m_sceneHash )
    {
        return fail( "recorded on another scene (" + ( header.Map.empty() ? std::string( "unnamed" ) : header.Map ) + ") than " + m_map );
    }
    const uint16_t rate = std::max<uint16_t>( 1, header.TickRate );
    const uint32_t every = std::max<uint32_t>( 1, m_settings.SampleTicks );
    const double sampleSeconds = static_cast<double>( every ) / rate;
    const uint32_t shotWindow = static_cast<uint32_t>( std::max( 0.0, m_settings.KillerShotSeconds ) * rate );

    Source source;
    source.Path = InPath;
    source.Map = header.Map;
    source.RecordedAt = header.RecordedAt;
    source.FirstTick = InReader.GetFirstTick();
    source.LastTick = InReader.GetLastTick();

    // What went in, so a failure part way leaves this heatmap as it was.
    NetReplayHeatmap before = *this;
    std::map<uint32_t, NetReplayEntity> table;
    std::map<uint32_t, std::string> teams;
    struct Hit
    {
        uint32_t Tick = 0;
        uint32_t Target = 0;
        uint32_t Killer = 0;
        Vector3 Origin;
    };
    std::deque<Hit> hits;
    std::set<uint32_t> dead;    // killed, not back yet: no positions
    // Where each character last stood alive (a dead one's killer is placed there: a game parks its dead
    // out of sight, HeroShooter under the arena), the tick it last died, and the "player" its events name.
    std::map<uint32_t, Vector3> lastAlive;
    std::map<uint32_t, uint32_t> lastDeath;
    std::map<uint32_t, std::string> players;
    bool cancelled = false;

    auto heroOf = [&table]( uint32_t InNetId ) {
        auto it = table.find( InNetId );
        return it != table.end() ? HeroOf( it->second ) : std::string();
    };
    auto teamOf = [&teams]( uint32_t InNetId ) {
        auto it = teams.find( InNetId );
        return it != teams.end() ? it->second : std::string();
    };
    // Where an entity is at the tick: its mover (full precision), else its snapshot pose.
    auto positionOf = []( const NetReplayTick& InTick, uint32_t InNetId, Vector3& OutPosition ) {
        for( const NetReplayMover& mover : InTick.Movers )
        {
            if( mover.NetId == InNetId )
            {
                OutPosition = mover.State.Position;
                return true;
            }
        }
        auto state = InTick.State.Entities.find( InNetId );
        if( state == InTick.State.Entities.end() )
        {
            return false;
        }
        OutPosition = state->second.GetPosition();
        return true;
    };

    const bool read = InReader.ForEach( source.FirstTick, source.LastTick, [&]( const NetReplayTick& InTick ) {
        if( InCancel && InCancel->load( std::memory_order_relaxed ) )
        {
            cancelled = true;
            return false;
        }
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            table[entity.NetId] = entity;
        }
        for( const NetReplayMover& mover : InTick.Movers )
        {
            if( !dead.count( mover.NetId ) )
            {
                lastAlive[mover.NetId] = mover.State.Position;
            }
        }
        // Hits, newest last, for kills that don't name their killer.
        for( const NetReplayShot& shot : InTick.Shots )
        {
            if( shot.Hit && shot.Target != 0 )
            {
                hits.push_back( { InTick.Tick, shot.Target, shot.Instigator != 0 ? shot.Instigator : shot.Shooter, shot.Origin } );
            }
        }
        while( !hits.empty() && hits.front().Tick + shotWindow < InTick.Tick )
        {
            hits.pop_front();
        }
        for( const NetReplayEvent& event : InTick.Events )
        {
            const json payload = event.Payload.empty() ? json() : json::parse( event.Payload, nullptr, false );
            const bool isObject = !payload.is_discarded() && payload.is_object();
            if( isObject && event.Entity != 0 )
            {
                const std::string team = ReadName( payload, "team" );
                if( !team.empty() )
                {
                    teams[event.Entity] = team;
                }
                const std::string player = ReadName( payload, "player" );
                if( !player.empty() )
                {
                    players[event.Entity] = player;
                }
            }
            if( event.Entity != 0 && std::find( m_settings.AliveEvents.begin(), m_settings.AliveEvents.end(), event.Name ) != m_settings.AliveEvents.end() )
            {
                dead.erase( event.Entity );
            }
            if( event.Name != m_settings.KillEvent )
            {
                continue;
            }
            ++source.Kills;
            if( event.Entity != 0 )
            {
                dead.insert( event.Entity );
                lastDeath[event.Entity] = InTick.Tick;
            }
            const json none;
            const json& fields = isObject ? payload : none;
            const uint32_t victim = event.Entity;
            Vector3 deathAt;
            const bool hasDeath = ( isObject && fields.contains( "at" ) && ReadVector( fields["at"], deathAt ) ) || ( victim != 0 && positionOf( InTick, victim, deathAt ) );
            std::string victimTeam = ReadName( fields, "victimTeam" );
            if( victimTeam.empty() )
            {
                victimTeam = teamOf( victim );
            }
            if( hasDeath )
            {
                AddPoint( Layer::Deaths, deathAt, heroOf( victim ), victimTeam );
            }

            uint32_t killer = ReadNetId( fields, "killerNetId" );
            if( killer == 0 )
            {
                killer = ReadNetId( fields, "killerId" );
            }
            // Without a named killer, the newest hit on the victim, unless it can't be the kill: its shooter
            // died after landing it (someone else finished the victim off: a projectile still in flight
            // when its shooter died lands later, and counts), or the payload's "killer" names another player.
            const std::string killerName = ReadName( fields, "killer" );
            auto couldBe = [&]( const Hit& InHit ) {
                if( killer != 0 )
                {
                    return InHit.Killer == killer;
                }
                auto died = lastDeath.find( InHit.Killer );
                if( InHit.Killer != victim && died != lastDeath.end() && died->second > InHit.Tick )
                {
                    return false;
                }
                auto name = players.find( InHit.Killer );
                return killerName.empty() || name == players.end() || name->second == killerName;
            };
            const Hit* shot = nullptr;
            for( auto it = hits.rbegin(); it != hits.rend(); ++it )
            {
                if( victim != 0 && it->Target == victim && couldBe( *it ) )
                {
                    shot = &*it;
                    break;
                }
            }
            if( killer == 0 && shot )
            {
                killer = shot->Killer;
            }
            if( killer == 0 )
            {
                ++m_unattributed;
                continue;
            }
            if( killer == victim )
            {
                ++m_suicides;
                continue;
            }
            // Where the killer is: the payload's, the tick's (a dead killer's: where it last stood alive, never
            // where its body was parked), else where its shot came from.
            Vector3 killAt;
            bool hasKill = isObject && fields.contains( "killerAt" ) && ReadVector( fields["killerAt"], killAt );
            if( !hasKill && dead.count( killer ) )
            {
                auto alive = lastAlive.find( killer );
                if( alive != lastAlive.end() )
                {
                    killAt = alive->second;
                    hasKill = true;
                }
            }
            else if( !hasKill )
            {
                hasKill = positionOf( InTick, killer, killAt );
            }
            if( !hasKill && shot )
            {
                killAt = shot->Origin;
                hasKill = true;
            }
            std::string killerTeam = ReadName( fields, "killerTeam" );
            if( killerTeam.empty() )
            {
                killerTeam = teamOf( killer );
            }
            if( hasKill )
            {
                AddPoint( Layer::Kills, killAt, heroOf( killer ), killerTeam );
            }
        }
        if( ( InTick.Tick - source.FirstTick ) % every == 0 )
        {
            for( const NetReplayMover& mover : InTick.Movers )
            {
                if( dead.count( mover.NetId ) )
                {
                    continue;
                }
                AddPoint( Layer::Positions, mover.State.Position, heroOf( mover.NetId ), teamOf( mover.NetId ), sampleSeconds );
                ++source.Samples;
            }
        }
        return true;
    } );
    if( !read || cancelled )
    {
        *this = std::move( before );
        return read ? false : fail( InReader.GetError() );
    }
    source.Seconds = static_cast<double>( source.LastTick - source.FirstTick + 1 ) / rate;
    if( m_sources.empty() )
    {
        m_map = header.Map;
        m_sceneHash = header.SceneHash;
    }
    m_sources.push_back( std::move( source ) );
    return true;
}


bool NetReplayHeatmap::IsEmpty() const
{
    for( const auto& layer : m_layers )
    {
        auto all = layer.find( "all" );
        if( all != layer.end() && !all->second.empty() )
        {
            return false;
        }
    }
    return true;
}


std::vector<std::string> NetReplayHeatmap::GetGroups() const
{
    std::vector<std::string> groups{ "all" };
    std::vector<std::string> others;
    for( const auto& layer : m_layers )
    {
        for( const auto& [group, cells] : layer )
        {
            if( group != "all" && std::find( others.begin(), others.end(), group ) == others.end() )
            {
                others.push_back( group );
            }
        }
    }
    std::sort( others.begin(), others.end() );
    groups.insert( groups.end(), others.begin(), others.end() );
    return groups;
}


double NetReplayHeatmap::GetTotal( Layer InLayer, const std::string& InGroup ) const
{
    const size_t layer = static_cast<size_t>( InLayer );
    if( layer >= kLayerCount )
    {
        return 0.0;
    }
    auto group = m_layers[layer].find( InGroup );
    double total = 0.0;
    if( group != m_layers[layer].end() )
    {
        for( const auto& [key, value] : group->second )
        {
            total += value;
        }
    }
    return total;
}


double NetReplayHeatmap::ValueAt( Layer InLayer, const Vector3& InPosition, const std::string& InGroup ) const
{
    const size_t layer = static_cast<size_t>( InLayer );
    if( layer >= kLayerCount )
    {
        return 0.0;
    }
    auto group = m_layers[layer].find( InGroup );
    if( group == m_layers[layer].end() )
    {
        return 0.0;
    }
    int x = 0;
    int z = 0;
    CellOf( InPosition, x, z );
    auto cell = group->second.find( Key( x, z ) );
    return cell != group->second.end() ? cell->second : 0.0;
}


void NetReplayHeatmap::Bounds( int& OutMinX, int& OutMinZ, int& OutMaxX, int& OutMaxZ ) const
{
    OutMinX = OutMinZ = std::numeric_limits<int>::max();
    OutMaxX = OutMaxZ = std::numeric_limits<int>::min();
    // Every group's cells, not only "all"'s: a file edited by hand may hold a hero's cell "all" lacks.
    for( const auto& layer : m_layers )
    {
        for( const auto& [group, cells] : layer )
        {
            for( const auto& [key, value] : cells )
            {
                int x = 0;
                int z = 0;
                Unkey( key, x, z );
                OutMinX = std::min( OutMinX, x );
                OutMinZ = std::min( OutMinZ, z );
                OutMaxX = std::max( OutMaxX, x );
                OutMaxZ = std::max( OutMaxZ, z );
            }
        }
    }
}


void NetReplayHeatmap::Extent( int64_t& OutWidth, int64_t& OutHeight ) const
{
    int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    Bounds( minX, minZ, maxX, maxZ );
    OutWidth = maxX >= minX ? static_cast<int64_t>( maxX ) - minX + 1 : 0;
    OutHeight = maxZ >= minZ ? static_cast<int64_t>( maxZ ) - minZ + 1 : 0;
}


NetReplayHeatmap::Grid NetReplayHeatmap::GetGrid( Layer InLayer, const std::string& InGroup ) const
{
    Grid grid;
    grid.CellSize = m_settings.CellSize;
    if( IsEmpty() )
    {
        return grid;
    }
    int64_t width = 0;
    int64_t height = 0;
    Extent( width, height );
    if( width <= 0 || height <= 0 || width * height > kMaxGridCells )
    {
        return grid;
    }
    int maxX = 0;
    int maxZ = 0;
    Bounds( grid.MinX, grid.MinZ, maxX, maxZ );
    grid.Width = static_cast<int>( width );
    grid.Height = static_cast<int>( height );
    grid.Values.assign( static_cast<size_t>( grid.Width ) * grid.Height, 0.f );
    const size_t layer = static_cast<size_t>( InLayer );
    if( layer >= kLayerCount )
    {
        return grid;
    }
    auto group = m_layers[layer].find( InGroup );
    if( group == m_layers[layer].end() )
    {
        return grid;
    }
    for( const auto& [key, value] : group->second )
    {
        int x = 0;
        int z = 0;
        Unkey( key, x, z );
        const int64_t column = static_cast<int64_t>( x ) - grid.MinX;
        const int64_t row = static_cast<int64_t>( z ) - grid.MinZ;
        if( column < 0 || column >= grid.Width || row < 0 || row >= grid.Height )
        {
            continue;
        }
        float& cell = grid.Values[static_cast<size_t>( row ) * grid.Width + static_cast<size_t>( column )];
        cell = static_cast<float>( value );
        grid.Max = std::max( grid.Max, cell );
        grid.Total += value;
    }
    return grid;
}


json NetReplayHeatmap::ToJson() const
{
    json out;
    out["format"] = "mheatmap";
    out["version"] = kFormatVersion;
    out["cellSize"] = m_settings.CellSize;
    out["sampleTicks"] = m_settings.SampleTicks;
    out["killEvent"] = m_settings.KillEvent;
    out["map"] = m_map;
    out["sceneHash"] = Hex( m_sceneHash );
    out["unattributedKills"] = m_unattributed;
    out["suicides"] = m_suicides;
    out["outOfRange"] = m_outOfRange;
    out["maxDistance"] = m_settings.MaxDistance;
    json sources = json::array();
    for( const Source& source : m_sources )
    {
        sources.push_back( { { "path", source.Path }, { "map", source.Map }, { "recordedAt", source.RecordedAt }, { "firstTick", source.FirstTick },
            { "lastTick", source.LastTick }, { "seconds", source.Seconds }, { "kills", source.Kills }, { "samples", source.Samples } } );
    }
    out["replays"] = sources;
    if( !IsEmpty() )
    {
        int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
        int64_t width = 0, height = 0;
        Bounds( minX, minZ, maxX, maxZ );
        Extent( width, height );
        out["bounds"] = { { "minX", minX }, { "minZ", minZ }, { "width", width }, { "height", height } };
    }
    json floor = json::array();
    for( const auto& [key, y] : m_floor )
    {
        int x = 0;
        int z = 0;
        Unkey( key, x, z );
        floor.push_back( { x, z, y } );
    }
    out["floor"] = floor;
    json layers = json::object();
    for( size_t i = 0; i < kLayerCount; ++i )
    {
        json groups = json::object();
        for( const auto& [group, cells] : m_layers[i] )
        {
            json list = json::array();
            double total = 0.0;
            double max = 0.0;
            for( const auto& [key, value] : cells )
            {
                int x = 0;
                int z = 0;
                Unkey( key, x, z );
                list.push_back( { x, z, value } );
                total += value;
                max = std::max( max, value );
            }
            groups[group] = { { "total", total }, { "max", max }, { "cells", list } };
        }
        layers[kLayerNames[i]] = { { "unit", i == 0 ? "seconds" : "count" }, { "groups", groups } };
    }
    out["layers"] = layers;
    return out;
}


bool NetReplayHeatmap::FromJson( const json& InJson, std::string* OutError )
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    if( !InJson.is_object() || InJson.value( "format", std::string() ) != "mheatmap" )
    {
        return fail( "not a heatmap (\"format\": \"mheatmap\")" );
    }
    if( InJson.value( "version", 0 ) > kFormatVersion )
    {
        return fail( "heatmap version " + std::to_string( InJson.value( "version", 0 ) ) + " is newer than this build reads (" + std::to_string( kFormatVersion ) + ")" );
    }
    NetReplayHeatmap loaded;
    try
    {
        loaded.m_settings.CellSize = InJson.value( "cellSize", 1.f );
        loaded.m_settings.SampleTicks = InJson.value( "sampleTicks", 6u );
        loaded.m_settings.KillEvent = InJson.value( "killEvent", std::string( "kill" ) );
        if( !( loaded.m_settings.CellSize > 0.f ) )
        {
            return fail( "the heatmap's cellSize isn't positive" );
        }
        loaded.m_map = InJson.value( "map", std::string() );
        loaded.m_sceneHash = std::strtoull( InJson.value( "sceneHash", std::string( "0" ) ).c_str(), nullptr, 16 );
        loaded.m_unattributed = InJson.value( "unattributedKills", 0u );
        loaded.m_suicides = InJson.value( "suicides", 0u );
        loaded.m_outOfRange = InJson.value( "outOfRange", 0u );
        loaded.m_settings.MaxDistance = InJson.value( "maxDistance", loaded.m_settings.MaxDistance );
        if( InJson.contains( "replays" ) && InJson["replays"].is_array() )
        {
            for( const json& entry : InJson["replays"] )
            {
                Source source;
                source.Path = entry.value( "path", std::string() );
                source.Map = entry.value( "map", std::string() );
                source.RecordedAt = entry.value( "recordedAt", std::string() );
                source.FirstTick = entry.value( "firstTick", 0u );
                source.LastTick = entry.value( "lastTick", 0u );
                source.Seconds = entry.value( "seconds", 0.0 );
                source.Kills = entry.value( "kills", 0u );
                source.Samples = entry.value( "samples", 0u );
                loaded.m_sources.push_back( std::move( source ) );
            }
        }
        if( InJson.contains( "floor" ) && InJson["floor"].is_array() )
        {
            for( const json& cell : InJson["floor"] )
            {
                if( cell.is_array() && cell.size() == 3 )
                {
                    loaded.m_floor[Key( cell[0].get<int>(), cell[1].get<int>() )] = cell[2].get<float>();
                }
            }
        }
        const json& layers = InJson.contains( "layers" ) ? InJson["layers"] : json::object();
        for( size_t i = 0; i < kLayerCount; ++i )
        {
            if( !layers.contains( kLayerNames[i] ) || !layers[kLayerNames[i]].contains( "groups" ) )
            {
                continue;
            }
            for( const auto& [group, entry] : layers[kLayerNames[i]]["groups"].items() )
            {
                CellMap& cells = loaded.m_layers[i][group];
                if( !entry.contains( "cells" ) || !entry["cells"].is_array() )
                {
                    continue;
                }
                for( const json& cell : entry["cells"] )
                {
                    if( !cell.is_array() || cell.size() != 3 )
                    {
                        return fail( "a " + std::string( kLayerNames[i] ) + " cell of " + group + " isn't [x, z, value]" );
                    }
                    cells[Key( cell[0].get<int>(), cell[1].get<int>() )] += cell[2].get<double>();
                }
            }
        }
    }
    catch( const std::exception& InError )
    {
        return fail( std::string( "the heatmap doesn't read: " ) + InError.what() );
    }
    *this = std::move( loaded );
    return true;
}


bool NetReplayHeatmap::Save( const std::string& InPath, std::string* OutError ) const
{
    std::error_code error;
    const std::filesystem::path path( InPath );
    if( path.has_parent_path() )
    {
        std::filesystem::create_directories( path.parent_path(), error );
    }
    std::ofstream file( InPath, std::ios::binary | std::ios::trunc );
    if( !file )
    {
        if( OutError )
        {
            *OutError = "can't write " + InPath;
        }
        return false;
    }
    file << ToJson().dump( 1 ) << "\n";
    return static_cast<bool>( file );
}


bool NetReplayHeatmap::Load( const std::string& InPath, std::string* OutError )
{
    std::ifstream file( InPath, std::ios::binary );
    if( !file )
    {
        if( OutError )
        {
            *OutError = "can't read " + InPath;
        }
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    const json parsed = json::parse( text.str(), nullptr, false );
    if( parsed.is_discarded() )
    {
        if( OutError )
        {
            *OutError = InPath + " isn't JSON";
        }
        return false;
    }
    return FromJson( parsed, OutError );
}


Vector3 NetReplayHeatmap::Ramp( float InT )
{
    // Blue, cyan, green, yellow, red, white.
    static const Vector3 kStops[] = { Vector3( 0.10f, 0.15f, 0.65f ), Vector3( 0.05f, 0.65f, 0.95f ), Vector3( 0.20f, 0.85f, 0.30f ),
        Vector3( 1.00f, 0.88f, 0.10f ), Vector3( 0.95f, 0.20f, 0.10f ), Vector3( 1.00f, 1.00f, 1.00f ) };
    constexpr int kSegments = 5;
    const float t = std::clamp( InT, 0.f, 1.f ) * kSegments;
    const int index = std::min( static_cast<int>( t ), kSegments - 1 );
    const float f = t - static_cast<float>( index );
    const Vector3& a = kStops[index];
    const Vector3& b = kStops[index + 1];
    return Vector3( a.x + ( b.x - a.x ) * f, a.y + ( b.y - a.y ) * f, a.z + ( b.z - a.z ) * f );
}


std::string NetReplayHeatmap::GridError() const
{
    int64_t width = 0;
    int64_t height = 0;
    Extent( width, height );
    if( IsEmpty() || width <= 0 || height <= 0 )
    {
        return "the heatmap is empty";
    }
    return "the heatmap spans " + std::to_string( width ) + " x " + std::to_string( height ) + " cells, more than " + std::to_string( kMaxGridCells )
        + " (a sample far off the map? Settings::MaxDistance leaves those out)";
}


int NetReplayHeatmap::PixelsPerCell( int InWidth, int InHeight, int InRequested ) const
{
    if( InRequested > 0 )
    {
        // No more than 4096 px on a side, however many were asked for.
        const int longest = std::max( 1, std::max( InWidth, InHeight ) );
        return std::clamp( std::min( InRequested, 4096 / longest ), 1, 64 );
    }
    // A big map gets 1 px a cell (at most kMaxGridCells pixels), a small one 2 to 16.
    const int longest = std::max( 1, std::max( InWidth, InHeight ) );
    return std::clamp( 512 / longest, longest > 256 ? 1 : 2, 16 );
}


bool NetReplayHeatmap::WriteImage( const std::string& InPath, int InWidth, int InHeight, const std::vector<uint8_t>& InRgba, std::string* OutError ) const
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    // Each row behind a filter byte (0: none), deflated: an RGBA8 PNG.
    std::vector<uint8_t> raw;
    raw.reserve( static_cast<size_t>( InWidth * 4 + 1 ) * InHeight );
    for( int row = 0; row < InHeight; ++row )
    {
        raw.push_back( 0 );
        const uint8_t* line = InRgba.data() + static_cast<size_t>( row ) * InWidth * 4;
        raw.insert( raw.end(), line, line + static_cast<size_t>( InWidth ) * 4 );
    }
    uLongf size = compressBound( static_cast<uLong>( raw.size() ) );
    std::vector<uint8_t> deflated( size );
    if( compress2( deflated.data(), &size, raw.data(), static_cast<uLong>( raw.size() ), Z_BEST_COMPRESSION ) != Z_OK )
    {
        return fail( "deflate failed" );
    }
    deflated.resize( size );
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<uint8_t> ihdr;
    PutU32( ihdr, static_cast<uint32_t>( InWidth ) );
    PutU32( ihdr, static_cast<uint32_t>( InHeight ) );
    ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );   // 8 bits, RGBA, deflate, adaptive filters, no interlace
    PngChunk( png, "IHDR", ihdr );
    PngChunk( png, "IDAT", deflated );
    PngChunk( png, "IEND", {} );
    std::error_code error;
    const std::filesystem::path path( InPath );
    if( path.has_parent_path() )
    {
        std::filesystem::create_directories( path.parent_path(), error );
    }
    std::ofstream file( InPath, std::ios::binary | std::ios::trunc );
    if( !file )
    {
        return fail( "can't write " + InPath );
    }
    file.write( reinterpret_cast<const char*>( png.data() ), static_cast<std::streamsize>( png.size() ) );
    return file ? true : fail( "can't write " + InPath );
}


bool NetReplayHeatmap::WritePng( const std::string& InPath, Layer InLayer, const std::string& InGroup, std::string* OutError, int InPixelsPerCell ) const
{
    const Grid grid = GetGrid( InLayer, InGroup );
    if( grid.Width <= 0 || grid.Height <= 0 )
    {
        if( OutError )
        {
            *OutError = GridError();
        }
        return false;
    }
    const int scale = PixelsPerCell( grid.Width, grid.Height, InPixelsPerCell );
    const int width = grid.Width * scale;
    const int height = grid.Height * scale;
    std::vector<uint8_t> rgba( static_cast<size_t>( width ) * height * 4, 0 );
    for( int row = 0; row < grid.Height; ++row )
    {
        for( int column = 0; column < grid.Width; ++column )
        {
            const float value = grid.At( column, row );
            if( value <= 0.f || grid.Max <= 0.f )
            {
                continue;
            }
            // Square root: a few hot cells shouldn't wash the rest out.
            const float t = std::sqrt( value / grid.Max );
            const Vector3 colour = Ramp( t );
            const uint8_t pixel[4] = { ToByte( colour.x ), ToByte( colour.y ), ToByte( colour.z ), ToByte( 0.45f + 0.55f * t ) };
            for( int y = row * scale; y < ( row + 1 ) * scale; ++y )
            {
                for( int x = column * scale; x < ( column + 1 ) * scale; ++x )
                {
                    std::copy( pixel, pixel + 4, rgba.begin() + ( static_cast<size_t>( y ) * width + x ) * 4 );
                }
            }
        }
    }
    return WriteImage( InPath, width, height, rgba, OutError );
}


bool NetReplayHeatmap::WriteOverviewPng( const std::string& InPath, const std::string& InGroup, std::string* OutError, int InPixelsPerCell ) const
{
    const Grid positions = GetGrid( Layer::Positions, InGroup );
    const Grid deaths = GetGrid( Layer::Deaths, InGroup );
    const Grid kills = GetGrid( Layer::Kills, InGroup );
    if( positions.Width <= 0 || positions.Height <= 0 )
    {
        if( OutError )
        {
            *OutError = GridError();
        }
        return false;
    }
    // 3 px a cell at least (a kill's dot inside a death's), unless that's past 4096 px across.
    int scale = PixelsPerCell( positions.Width, positions.Height, InPixelsPerCell );
    if( std::max( positions.Width, positions.Height ) * 3 <= 4096 )
    {
        scale = std::max( 3, scale );
    }
    const int width = positions.Width * scale;
    const int height = positions.Height * scale;
    std::vector<uint8_t> rgba( static_cast<size_t>( width ) * height * 4 );
    for( size_t i = 0; i < rgba.size(); i += 4 )
    {
        rgba[i] = 18;
        rgba[i + 1] = 20;
        rgba[i + 2] = 26;
        rgba[i + 3] = 255;
    }
    auto fill = [&]( int InColumn, int InRow, int InInset, const Vector3& InColour ) {
        for( int y = InRow * scale + InInset; y < ( InRow + 1 ) * scale - InInset; ++y )
        {
            for( int x = InColumn * scale + InInset; x < ( InColumn + 1 ) * scale - InInset; ++x )
            {
                uint8_t* pixel = rgba.data() + ( static_cast<size_t>( y ) * width + x ) * 4;
                pixel[0] = ToByte( InColour.x );
                pixel[1] = ToByte( InColour.y );
                pixel[2] = ToByte( InColour.z );
            }
        }
    };
    for( int row = 0; row < positions.Height; ++row )
    {
        for( int column = 0; column < positions.Width; ++column )
        {
            if( const float time = positions.At( column, row ); time > 0.f && positions.Max > 0.f )
            {
                const float t = std::sqrt( time / positions.Max );
                fill( column, row, 0, Vector3( 0.12f + 0.45f * t, 0.16f + 0.55f * t, 0.24f + 0.70f * t ) );
            }
            if( const float died = deaths.At( column, row ); died > 0.f && deaths.Max > 0.f )
            {
                const float t = std::sqrt( died / deaths.Max );
                fill( column, row, 0, Vector3( 0.55f + 0.45f * t, 0.10f + 0.15f * t, 0.10f ) );
            }
            if( const float killed = kills.At( column, row ); killed > 0.f && kills.Max > 0.f )
            {
                const float t = std::sqrt( killed / kills.Max );
                fill( column, row, std::max( 1, scale / 4 ), Vector3( 0.15f, 0.55f + 0.45f * t, 0.20f ) );
            }
        }
    }
    return WriteImage( InPath, width, height, rgba, OutError );
}


std::string NetReplayHeatmap::Describe( size_t InHottest ) const
{
    std::ostringstream out;
    double seconds = 0.0;
    for( const Source& source : m_sources )
    {
        seconds += source.Seconds;
    }
    char line[256];
    std::snprintf( line, sizeof( line ), "heatmap: %zu replay(s) of %s, %.1f s, %g m cells", m_sources.size(), m_map.empty() ? "an unnamed scene" : m_map.c_str(), seconds,
        static_cast<double>( m_settings.CellSize ) );
    out << line;
    if( !IsEmpty() )
    {
        int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
        int64_t width = 0, height = 0;
        Bounds( minX, minZ, maxX, maxZ );
        Extent( width, height );
        std::snprintf( line, sizeof( line ), ", %lldx%lld cells from (%d, %d)", static_cast<long long>( width ), static_cast<long long>( height ), minX, minZ );
        out << line;
        if( width * height > kMaxGridCells )
        {
            out << " (too many to draw: " << GridError() << ")";
        }
    }
    out << "\n";
    for( const Source& source : m_sources )
    {
        std::snprintf( line, sizeof( line ), "  replay %s: ticks %u..%u (%.1f s), %u kill event(s), %u position sample(s)\n", source.Path.c_str(), source.FirstTick,
            source.LastTick, source.Seconds, source.Kills, source.Samples );
        out << line;
    }
    for( const std::string& group : GetGroups() )
    {
        std::snprintf( line, sizeof( line ), "  %-18s positions %.1f s, deaths %.0f, kills %.0f\n", group.c_str(), GetTotal( Layer::Positions, group ),
            GetTotal( Layer::Deaths, group ), GetTotal( Layer::Kills, group ) );
        out << line;
    }
    if( m_unattributed > 0 || m_suicides > 0 )
    {
        out << "  " << m_unattributed << " kill(s) without a known killer and " << m_suicides << " by the victim itself: deaths only\n";
    }
    if( m_outOfRange > 0 )
    {
        out << "  " << m_outOfRange << " sample(s) more than " << m_settings.MaxDistance << " m from the origin left out\n";
    }
    for( size_t i = 0; i < kLayerCount; ++i )
    {
        auto all = m_layers[i].find( "all" );
        if( all == m_layers[i].end() || all->second.empty() || InHottest == 0 )
        {
            continue;
        }
        std::vector<std::pair<double, int64_t>> cells;
        for( const auto& [key, value] : all->second )
        {
            cells.push_back( { value, key } );
        }
        std::sort( cells.begin(), cells.end(), []( const auto& InA, const auto& InB ) { return InA.first > InB.first || ( InA.first == InB.first && InA.second < InB.second ); } );
        out << "  hottest " << kLayerNames[i] << ":";
        for( size_t n = 0; n < std::min( InHottest, cells.size() ); ++n )
        {
            int x = 0;
            int z = 0;
            Unkey( cells[n].second, x, z );
            const Vector3 centre = CellCentre( x, z );
            std::snprintf( line, sizeof( line ), " (%.1f, %.1f) %s%g", centre.x, centre.z, i == 0 ? "" : "x", i == 0 ? std::round( cells[n].first * 10.0 ) / 10.0 : cells[n].first );
            out << line << ( i == 0 ? " s" : "" );
        }
        out << "\n";
    }
    return out.str();
}
