#include "PCH.h"
#include "Net/NetKillcam.h"
#include "Net/NetReplication.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace
{
    void PutU8( std::vector<uint8_t>& InOut, uint8_t InValue )
    {
        InOut.push_back( InValue );
    }

    void PutU16( std::vector<uint8_t>& InOut, uint16_t InValue )
    {
        InOut.push_back( static_cast<uint8_t>( InValue ) );
        InOut.push_back( static_cast<uint8_t>( InValue >> 8 ) );
    }

    void PutU32( std::vector<uint8_t>& InOut, uint32_t InValue )
    {
        for( int i = 0; i < 4; ++i )
        {
            InOut.push_back( static_cast<uint8_t>( InValue >> ( 8 * i ) ) );
        }
    }

    uint16_t GetU16( const uint8_t* InData )
    {
        return static_cast<uint16_t>( InData[0] | ( InData[1] << 8 ) );
    }

    uint32_t GetU32( const uint8_t* InData )
    {
        uint32_t value = 0;
        for( int i = 0; i < 4; ++i )
        {
            value |= static_cast<uint32_t>( InData[i] ) << ( 8 * i );
        }
        return value;
    }

    // A tick's changes to the NetId table: its rows in, the NetIds it no longer has out.
    void Apply( std::map<uint32_t, NetReplayEntity>& InOut, const NetReplayTick& InTick )
    {
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            InOut[entity.NetId] = entity;
        }
        for( auto it = InOut.begin(); it != InOut.end(); )
        {
            it = InTick.State.Entities.count( it->first ) ? std::next( it ) : InOut.erase( it );
        }
    }
}


std::vector<NetReplayTick> NetKillcam::Filter( const NetKillcamRequest& InRequest, const std::vector<std::shared_ptr<const NetReplayTick>>& InRing,
    const std::map<uint32_t, NetReplayEntity>& InRingEntities, std::string* OutError )
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return std::vector<NetReplayTick>();
    };
    if( InRing.empty() )
    {
        return fail( "the replay ring is empty" );
    }
    const uint32_t first = std::max( InRequest.FirstTick, InRing.front()->Tick );
    const uint32_t last = std::min( InRequest.DeathTick, InRing.back()->Tick );
    if( first > last )
    {
        return fail( "ticks " + std::to_string( InRequest.FirstTick ) + "-" + std::to_string( InRequest.DeathTick ) + " aren't in the ring ("
            + std::to_string( InRing.front()->Tick ) + "-" + std::to_string( InRing.back()->Tick ) + ")" );
    }
    // Where the killer stands before it first appears in the range: where it first does.
    bool found = false;
    Vector3 viewer;
    for( const auto& tick : InRing )
    {
        if( tick->Tick < first || tick->Tick > last )
        {
            continue;
        }
        if( auto it = tick->State.Entities.find( InRequest.KillerNetId ); it != tick->State.Entities.end() )
        {
            viewer = it->second.GetPosition();
            found = true;
            break;
        }
    }
    if( !found )
    {
        return fail( "the killer (#" + std::to_string( InRequest.KillerNetId ) + ") isn't in ticks " + std::to_string( first ) + "-" + std::to_string( last ) );
    }

    const uint32_t chunkTicks = std::max<uint32_t>( 1, InRequest.ChunkTicks );
    std::map<uint32_t, NetReplayEntity> table = InRingEntities;
    std::vector<NetReplayTick> out;
    out.reserve( last - first + 1 );
    std::set<uint32_t> previous;
    for( size_t i = 0; i < InRing.size(); ++i )
    {
        const NetReplayTick& tick = *InRing[i];
        if( i > 0 )
        {
            Apply( table, tick );
        }
        if( tick.Tick < first )
        {
            continue;
        }
        if( tick.Tick > last )
        {
            break;
        }
        auto killer = tick.State.Entities.find( InRequest.KillerNetId );
        if( killer != tick.State.Entities.end() )
        {
            viewer = killer->second.GetPosition();
        }
        auto killerRow = table.find( InRequest.KillerNetId );
        const uint8_t killerClient = killerRow != table.end() ? killerRow->second.Owner : 0;

        // NET-13's view from the killer (NetReplicationServer::Prepare's rule), and the victim.
        std::set<uint32_t> relevant;
        NetReplayTick& filtered = out.emplace_back();
        filtered.Tick = tick.Tick;
        filtered.TicksOwed = tick.TicksOwed;
        filtered.TicksDropped = tick.TicksDropped;
        filtered.State.Tick = tick.Tick;
        for( const auto& [id, state] : tick.State.Entities )
        {
            auto row = table.find( id );
            const bool owned = killerClient != 0 && row != table.end() && row->second.Owner == killerClient;
            const float distance = ( state.GetPosition() - viewer ).Length();
            if( id == InRequest.KillerNetId || id == InRequest.VictimNetId
                || NetReplicationSettings::IsRelevant( InRequest.RelevantRadius, true, distance, InRequest.AlwaysRelevant.count( id ) > 0, owned ) )
            {
                relevant.insert( id );
                filtered.State.Entities.emplace( id, state );
            }
        }
        auto isRelevant = [&relevant]( uint32_t InId ) { return InId != 0 && relevant.count( InId ) > 0; };

        if( killerClient != 0 )
        {
            for( const NetReplayClient& client : tick.Clients )
            {
                if( client.Id == killerClient )
                {
                    filtered.Clients.push_back( client );
                }
            }
        }
        for( const NetReplayMover& mover : tick.Movers )
        {
            if( mover.NetId == InRequest.KillerNetId )
            {
                filtered.Movers.push_back( mover );
            }
        }
        for( const NetReplayShot& shot : tick.Shots )
        {
            if( !isRelevant( shot.Shooter ) && !isRelevant( shot.Instigator ) && !isRelevant( shot.Target ) )
            {
                continue;
            }
            NetReplayShot& kept = filtered.Shots.emplace_back( shot );
            kept.Hitboxes.clear();
            for( const NetReplayHitbox& hitbox : shot.Hitboxes )
            {
                if( hitbox.NetId == 0 || isRelevant( hitbox.NetId ) )
                {
                    kept.Hitboxes.push_back( hitbox );
                }
            }
        }
        for( const NetReplayEvent& event : tick.Events )
        {
            if( event.Entity == 0 || isRelevant( event.Entity ) )
            {
                filtered.Events.push_back( event );
            }
        }
        // A chunk's first tick names everything in it; later ones what came into view, and owner changes.
        const bool chunkStart = ( out.size() - 1 ) % chunkTicks == 0;
        std::set<uint32_t> named;
        for( const uint32_t id : relevant )
        {
            if( chunkStart || !previous.count( id ) )
            {
                if( auto row = table.find( id ); row != table.end() )
                {
                    filtered.Entities.push_back( row->second );
                    named.insert( id );
                }
            }
        }
        for( const NetReplayEntity& entity : tick.Entities )
        {
            if( relevant.count( entity.NetId ) && !named.count( entity.NetId ) )
            {
                filtered.Entities.push_back( entity );
            }
        }
        previous = std::move( relevant );
    }
    return out;
}


NetKillcam::Encoded NetKillcam::Encode( const NetKillcamRequest& InRequest, const std::vector<std::shared_ptr<const NetReplayTick>>& InRing,
    const std::map<uint32_t, NetReplayEntity>& InRingEntities )
{
    const auto start = std::chrono::steady_clock::now();
    Encoded encoded;
    const std::vector<NetReplayTick> ticks = Filter( InRequest, InRing, InRingEntities, &encoded.Error );
    if( ticks.empty() )
    {
        return encoded;
    }
    const uint32_t chunkTicks = std::max<uint32_t>( 1, InRequest.ChunkTicks );
    std::vector<std::vector<uint8_t>> chunks;
    for( size_t begin = 0; begin < ticks.size(); begin += chunkTicks )
    {
        std::vector<const NetReplayTick*> pointers;
        for( size_t i = begin; i < std::min( ticks.size(), begin + chunkTicks ); ++i )
        {
            pointers.push_back( &ticks[i] );
        }
        size_t raw = 0;
        chunks.push_back( NetReplayChunk::Encode( pointers, InRequest.Codec, &raw ) );
        encoded.RawBytes += raw;
    }
    if( chunks.size() > NetKillcamFormat::kMaxChunks )
    {
        encoded.Error = std::to_string( chunks.size() ) + " chunks, past " + std::to_string( NetKillcamFormat::kMaxChunks );
        return encoded;
    }

    NetReplayHeader header = InRequest.Header;
    header.FormatVersion = NetReplayFormat::kVersion;
    header.KeyframeInterval = chunkTicks;
    header.CommandLine.clear();     // the server's own business
    const std::vector<uint8_t> headerBytes = NetReplayFile::EncodeHeader( header );

    const uint16_t count = static_cast<uint16_t>( chunks.size() );
    size_t total = NetKillcamFormat::kInfoBytes + headerBytes.size();
    for( const auto& chunk : chunks )
    {
        total += NetKillcamFormat::kPrefixBytes + chunk.size();
    }
    const uint8_t killerClient = [&]() -> uint8_t {
        for( const NetReplayTick& tick : ticks )
        {
            for( const NetReplayEntity& entity : tick.Entities )
            {
                if( entity.NetId == InRequest.KillerNetId )
                {
                    return entity.Owner;
                }
            }
        }
        return 0;
    }();
    for( uint16_t index = 0; index < count; ++index )
    {
        std::vector<uint8_t> blob;
        blob.reserve( NetKillcamFormat::kPrefixBytes + NetKillcamFormat::kInfoBytes + headerBytes.size() + chunks[index].size() );
        PutU32( blob, NetKillcamFormat::kMagic );
        PutU8( blob, NetKillcamFormat::kVersion );
        PutU32( blob, InRequest.Id );
        PutU16( blob, index );
        PutU16( blob, count );
        PutU32( blob, static_cast<uint32_t>( total ) );
        if( index == 0 )
        {
            PutU32( blob, InRequest.VictimNetId );
            PutU32( blob, InRequest.KillerNetId );
            PutU8( blob, killerClient );
            PutU32( blob, InRequest.DeathTick );
            PutU32( blob, ticks.front().Tick );
            PutU32( blob, static_cast<uint32_t>( headerBytes.size() ) );
            blob.insert( blob.end(), headerBytes.begin(), headerBytes.end() );
        }
        blob.insert( blob.end(), chunks[index].begin(), chunks[index].end() );
        encoded.Bytes += blob.size();
        encoded.Blobs.push_back( std::move( blob ) );
    }
    encoded.FirstTick = ticks.front().Tick;
    encoded.LastTick = ticks.back().Tick;
    encoded.Ticks = static_cast<uint32_t>( ticks.size() );
    encoded.Milliseconds = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
    return encoded;
}


bool NetKillcam::ReadPrefix( const uint8_t* InData, size_t InSize, Prefix& OutPrefix )
{
    if( InSize < NetKillcamFormat::kPrefixBytes || GetU32( InData ) != NetKillcamFormat::kMagic )
    {
        return false;
    }
    OutPrefix.Id = GetU32( InData + 5 );
    OutPrefix.Index = GetU16( InData + 9 );
    OutPrefix.Count = GetU16( InData + 11 );
    OutPrefix.TotalBytes = GetU32( InData + 13 );
    return true;
}


NetKillcamReceiver::Result NetKillcamReceiver::Accept( const uint8_t* InData, size_t InSize )
{
    NetKillcam::Prefix prefix;
    if( !NetKillcam::ReadPrefix( InData, InSize, prefix ) )
    {
        return Result::NotKillcam;
    }
    auto bad = [this]( const std::string& InWhy ) {
        m_error = InWhy;
        return Result::Bad;
    };
    if( InData[4] != NetKillcamFormat::kVersion )
    {
        return bad( "killcam stream version " + std::to_string( InData[4] ) + "; this build reads " + std::to_string( NetKillcamFormat::kVersion ) );
    }
    if( prefix.Id == 0 || prefix.Count == 0 || prefix.Count > NetKillcamFormat::kMaxChunks || prefix.Index >= prefix.Count )
    {
        return bad( "a killcam blob with chunk " + std::to_string( prefix.Index ) + " of " + std::to_string( prefix.Count ) );
    }
    if( prefix.Id < m_id )
    {
        return Result::Stale;
    }
    if( prefix.Id == m_id && ( prefix.Count != m_count || m_have.count( prefix.Index ) ) )
    {
        return prefix.Count != m_count ? bad( "killcam " + std::to_string( m_id ) + "'s chunk count changed" ) : Result::Stale;
    }

    // What follows the prefix: chunk 0's info and header, then the chunk.
    const uint8_t* payload = InData + NetKillcamFormat::kPrefixBytes;
    size_t size = InSize - NetKillcamFormat::kPrefixBytes;
    NetReplayHeader header;
    uint32_t victim = 0;
    uint32_t killer = 0;
    uint8_t killerClient = 0;
    uint32_t deathTick = 0;
    uint32_t firstTick = 0;
    if( prefix.Index == 0 )
    {
        if( size < NetKillcamFormat::kInfoBytes )
        {
            return bad( "killcam " + std::to_string( prefix.Id ) + "'s first blob is cut short" );
        }
        victim = GetU32( payload );
        killer = GetU32( payload + 4 );
        killerClient = payload[8];
        deathTick = GetU32( payload + 9 );
        firstTick = GetU32( payload + 13 );
        const uint32_t headerSize = GetU32( payload + 17 );
        payload += NetKillcamFormat::kInfoBytes;
        size -= NetKillcamFormat::kInfoBytes;
        size_t taken = 0;
        std::string error;
        if( headerSize > size || !NetReplayFile::DecodeHeader( payload, headerSize, header, taken, &error ) || taken != headerSize )
        {
            return bad( "killcam " + std::to_string( prefix.Id ) + "'s header: " + ( error.empty() ? std::string( "cut short" ) : error ) );
        }
        payload += headerSize;
        size -= headerSize;
    }
    NetReplayChunk::Info info;
    if( !NetReplayChunk::ReadInfo( payload, size, info ) || size < NetReplayFormat::kChunkHeaderBytes + info.StoredBytes )
    {
        return bad( "killcam " + std::to_string( prefix.Id ) + "'s chunk " + std::to_string( prefix.Index ) + " isn't a replay chunk" );
    }

    if( prefix.Id > m_id )
    {
        Reset();
        m_id = prefix.Id;
        m_count = prefix.Count;
        m_totalBytes = prefix.TotalBytes;
    }
    if( prefix.Index == 0 )
    {
        m_victim = victim;
        m_killer = killer;
        m_killerClient = killerClient;
        m_deathTick = deathTick;
        m_firstTick = firstTick;
        m_source = std::make_shared<NetReplayMemorySource>( std::move( header ) );
    }
    m_waiting.emplace( prefix.Index, std::vector<uint8_t>( payload, payload + size ) );
    m_have.insert( prefix.Index );
    ++m_received;
    m_bytes += InSize;
    Drain();
    return Result::Accepted;
}


void NetKillcamReceiver::Drain()
{
    if( !m_source )
    {
        return;
    }
    for( auto it = m_waiting.find( static_cast<uint16_t>( m_ready ) ); it != m_waiting.end(); it = m_waiting.find( static_cast<uint16_t>( m_ready ) ) )
    {
        m_source->AddChunk( std::move( it->second ) );
        m_waiting.erase( it );
        ++m_ready;
    }
}


void NetKillcamReceiver::Reset()
{
    *this = NetKillcamReceiver();
}
