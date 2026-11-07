#include "PCH.h"
#include "Net/NetDebug.h"
#include "Net/BitStream.h"
#include "Net/NetSession.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    void WriteHitbox( BitWriter& InWriter, const NetReplayHitbox& InHitbox )
    {
        InWriter.WriteVarUInt( InHitbox.NetId );
        InWriter.WriteString( InHitbox.Name );
        InWriter.WriteBits( InHitbox.Shape, 8 );
        for( const float value : { InHitbox.Position.x, InHitbox.Position.y, InHitbox.Position.z, InHitbox.Rotation.x, InHitbox.Rotation.y, InHitbox.Rotation.z,
                 InHitbox.Rotation.w, InHitbox.Radius, InHitbox.HalfHeight, InHitbox.HalfExtents.x, InHitbox.HalfExtents.y, InHitbox.HalfExtents.z } )
        {
            InWriter.WriteFloat( value );
        }
        InWriter.WriteBool( InHitbox.Occluder );
    }

    bool ReadHitbox( BitReader& InReader, NetReplayHitbox& OutHitbox )
    {
        OutHitbox.NetId = static_cast<uint32_t>( InReader.ReadVarUInt() );
        OutHitbox.Name = InReader.ReadText( 256 );
        OutHitbox.Shape = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
        float* fields[] = { &OutHitbox.Position.x, &OutHitbox.Position.y, &OutHitbox.Position.z, &OutHitbox.Rotation.x, &OutHitbox.Rotation.y, &OutHitbox.Rotation.z,
            &OutHitbox.Rotation.w, &OutHitbox.Radius, &OutHitbox.HalfHeight, &OutHitbox.HalfExtents.x, &OutHitbox.HalfExtents.y, &OutHitbox.HalfExtents.z };
        for( float* field : fields )
        {
            *field = InReader.ReadFloat();
        }
        OutHitbox.Occluder = InReader.ReadBool();
        return !InReader.IsOverflowed();
    }

    void WriteDouble( BitWriter& InWriter, double InValue )
    {
        InWriter.WriteBytes( &InValue, sizeof( InValue ) );
    }

    double ReadDouble( BitReader& InReader )
    {
        double value = 0.0;
        InReader.ReadBytes( &value, sizeof( value ) );
        return value;
    }
}


void NetDebugMessages::WriteSubscribe( BitWriter& InWriter, const NetDebugSubscription& InSubscription )
{
    InWriter.WriteBits( NetMessage::NetDebugSubscribe, 8 );
    InWriter.WriteBool( InSubscription.Shots );
    InWriter.WriteBool( InSubscription.Bandwidth );
}


bool NetDebugMessages::ReadSubscribe( BitReader& InReader, NetDebugSubscription& OutSubscription )
{
    OutSubscription.Shots = InReader.ReadBool();
    OutSubscription.Bandwidth = InReader.ReadBool();
    return !InReader.IsOverflowed();
}


void NetDebugMessages::WriteShot( BitWriter& InWriter, const NetShotResult& InResult )
{
    InWriter.WriteBits( InResult.CommandTick, 32 );
    NetReplayShots::Write( InWriter, InResult.Shot );
    InWriter.WriteVarUInt( InResult.Live.size() );
    for( const NetReplayHitbox& hitbox : InResult.Live )
    {
        WriteHitbox( InWriter, hitbox );
    }
}


bool NetDebugMessages::ReadShot( BitReader& InReader, NetShotResult& OutResult, uint32_t InReplayVersion )
{
    OutResult = NetShotResult();
    OutResult.CommandTick = InReader.ReadBits( 32 );
    if( !NetReplayShots::Read( InReader, OutResult.Shot, InReplayVersion ) || OutResult.Shot.Hitboxes.size() > NetDebug::kMaxHitboxes )
    {
        return false;
    }
    const uint64_t live = InReader.ReadVarUInt();
    if( live > NetDebug::kMaxHitboxes || InReader.IsOverflowed() )
    {
        return false;
    }
    OutResult.Live.resize( static_cast<size_t>( live ) );
    for( NetReplayHitbox& hitbox : OutResult.Live )
    {
        if( !ReadHitbox( InReader, hitbox ) )
        {
            return false;
        }
    }
    return !InReader.IsOverflowed();
}


std::vector<uint8_t> NetDebugMessages::EncodeShots( const std::vector<NetShotResult>& InResults )
{
    BitWriter writer;
    writer.WriteBytes( NetDebug::kShotBundleMagic, sizeof( NetDebug::kShotBundleMagic ) );
    writer.WriteBits( NetDebug::kShotBundleVersion, 8 );
    writer.WriteBits( NetReplayFormat::kVersion, 8 );
    writer.WriteVarUInt( InResults.size() );
    for( const NetShotResult& result : InResults )
    {
        WriteShot( writer, result );
    }
    return writer.GetBytes();
}


bool NetDebugMessages::IsShotBundle( const uint8_t* InData, size_t InSize )
{
    return InData && InSize >= sizeof( NetDebug::kShotBundleMagic ) && std::memcmp( InData, NetDebug::kShotBundleMagic, sizeof( NetDebug::kShotBundleMagic ) ) == 0;
}


bool NetDebugMessages::DecodeShots( const uint8_t* InData, size_t InSize, std::vector<NetShotResult>& OutResults )
{
    OutResults.clear();
    if( !IsShotBundle( InData, InSize ) )
    {
        return false;
    }
    BitReader reader( InData + sizeof( NetDebug::kShotBundleMagic ), InSize - sizeof( NetDebug::kShotBundleMagic ) );
    const uint32_t version = reader.ReadBits( 8 );
    const uint32_t replayVersion = reader.ReadBits( 8 );
    const uint64_t count = reader.ReadVarUInt();
    if( reader.IsOverflowed() || version != NetDebug::kShotBundleVersion || replayVersion < NetReplayFormat::kMinVersion || replayVersion > NetReplayFormat::kVersion
        || count > NetDebug::kMaxBundleShots )
    {
        return false;
    }
    OutResults.resize( static_cast<size_t>( count ) );
    for( NetShotResult& result : OutResults )
    {
        if( !ReadShot( reader, result, replayVersion ) )
        {
            OutResults.clear();
            return false;
        }
    }
    return true;
}


void NetDebugMessages::WriteBandwidth( BitWriter& InWriter, const NetBandwidthReport& InReport )
{
    InWriter.WriteBits( NetMessage::NetDebug, 8 );
    InWriter.WriteBits( NetDebug::Bandwidth, 8 );
    InWriter.WriteBits( InReport.Tick, 32 );
    WriteDouble( InWriter, InReport.Seconds );
    InWriter.WriteVarUInt( InReport.SnapshotBytes );
    InWriter.WriteVarUInt( InReport.OverheadBits );
    InWriter.WriteVarUInt( InReport.Top.size() );
    for( const NetEntityBytes& entity : InReport.Top )
    {
        InWriter.WriteVarUInt( entity.NetId );
        InWriter.WriteVarUInt( entity.Bits );
    }
}


bool NetDebugMessages::ReadDebug( BitReader& InReader, uint8_t& OutKind, NetBandwidthReport& OutBandwidth )
{
    OutKind = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
    if( OutKind == NetDebug::Bandwidth )
    {
        OutBandwidth = NetBandwidthReport();
        OutBandwidth.Tick = InReader.ReadBits( 32 );
        OutBandwidth.Seconds = ReadDouble( InReader );
        OutBandwidth.SnapshotBytes = InReader.ReadVarUInt();
        OutBandwidth.OverheadBits = InReader.ReadVarUInt();
        const uint64_t count = InReader.ReadVarUInt();
        if( count > NetDebug::kTopEntities || InReader.IsOverflowed() || !std::isfinite( OutBandwidth.Seconds ) || OutBandwidth.Seconds < 0.0 )
        {
            return false;
        }
        OutBandwidth.Top.resize( static_cast<size_t>( count ) );
        for( NetEntityBytes& entity : OutBandwidth.Top )
        {
            entity.NetId = static_cast<uint32_t>( InReader.ReadVarUInt() );
            entity.Bits = InReader.ReadVarUInt();
            entity.BytesPerSecond = OutBandwidth.Seconds > 0.0 ? static_cast<double>( entity.Bits ) / 8.0 / OutBandwidth.Seconds : 0.0;
        }
        return !InReader.IsOverflowed();
    }
    return false;
}


bool NetShotComparison::Agrees() const
{
    if( !HasClient || !HasServer )
    {
        return false;
    }
    if( Client.Hit != Server.Shot.Hit )
    {
        return false;
    }
    return !Client.Hit || Client.Target == Server.Shot.Target;
}


bool NetShotComparison::SamePart() const
{
    return Agrees() && ( !Client.Hit || Client.TargetPart == Server.Shot.TargetPart );
}


float NetShotComparison::PointDelta() const
{
    return HasClient && HasServer && Client.Hit && Server.Shot.Hit ? ( Client.Point - Server.Shot.Point ).Length() : -1.f;
}


std::string NetShotComparison::Describe() const
{
    auto side = []( bool InHit, bool InOccluded, uint32_t InTarget, const std::string& InPart ) {
        char text[128];
        if( InHit )
        {
            std::snprintf( text, sizeof( text ), "hit #%u %s", InTarget, InPart.c_str() );
        }
        else
        {
            std::snprintf( text, sizeof( text ), "missed%s", InOccluded ? " (occluded)" : "" );
        }
        return std::string( text );
    };
    const std::string client = HasClient ? side( Client.Hit, Client.Occluded, Client.Target, Client.TargetPart ) : std::string();
    const std::string server = HasServer ? side( Server.Shot.Hit, Server.Shot.Occluded, Server.Shot.Target, Server.Shot.TargetPart ) : std::string();
    char text[256];
    if( !HasServer )
    {
        std::snprintf( text, sizeof( text ), "client only (no result): %s", client.c_str() );
    }
    else if( !HasClient )
    {
        std::snprintf( text, sizeof( text ), "server only: %s", server.c_str() );
    }
    else if( Agrees() )
    {
        const float delta = PointDelta();
        if( delta >= 0.f )
        {
            std::snprintf( text, sizeof( text ), "agree: %s%s, %.1f mm apart", server.c_str(), SamePart() ? "" : ( " (client: " + Client.TargetPart + ")" ).c_str(), delta * 1000.f );
        }
        else
        {
            std::snprintf( text, sizeof( text ), "agree: both %s", server.c_str() );
        }
    }
    else
    {
        std::snprintf( text, sizeof( text ), "DISAGREE: client %s, server %s", client.c_str(), server.c_str() );
    }
    return text;
}


void NetShotLedger::AddPredicted( NetPredictedShot InShot, double InNow )
{
    ++m_totals.Predicted;
    m_pending.push_back( { std::move( InShot ), InNow } );
}


const NetShotComparison& NetShotLedger::AddResult( NetShotResult InResult, double InNow )
{
    ++m_totals.Results;
    // The closest predicted ray of the same tick and shooter (pellets, pierce steps through a corpse).
    auto best = m_pending.end();
    float bestCost = 0.f;
    const float length = InResult.Shot.Direction.Length();
    const Vector3 direction = length > 1e-9f ? InResult.Shot.Direction * ( 1.f / length ) : InResult.Shot.Direction;
    for( auto it = m_pending.begin(); it != m_pending.end(); ++it )
    {
        if( it->Shot.Tick != InResult.CommandTick || it->Shot.Shooter != InResult.Shot.Shooter )
        {
            continue;
        }
        const float origin = ( it->Shot.Origin - InResult.Shot.Origin ).Length();
        const float dot = it->Shot.Direction.Dot( direction );
        if( origin > kMaxOriginDelta || dot < kMinDirectionDot )
        {
            continue;
        }
        const float cost = origin + ( 1.f - dot ) * 1000.f;
        if( best == m_pending.end() || cost < bestCost )
        {
            best = it;
            bestCost = cost;
        }
    }
    NetShotComparison comparison;
    comparison.HasServer = true;
    comparison.Server = std::move( InResult );
    comparison.At = InNow;
    if( best != m_pending.end() )
    {
        comparison.HasClient = true;
        comparison.Client = std::move( best->Shot );
        m_pending.erase( best );
    }
    Settle( std::move( comparison ) );
    return m_recent.back();
}


void NetShotLedger::Update( double InNow )
{
    while( !m_pending.empty() && InNow - m_pending.front().At > kPendingSeconds )
    {
        NetShotComparison comparison;
        comparison.HasClient = true;
        comparison.Client = std::move( m_pending.front().Shot );
        comparison.At = InNow;
        m_pending.pop_front();
        Settle( std::move( comparison ) );
    }
}


void NetShotLedger::Reset()
{
    m_pending.clear();
    m_recent.clear();
    m_totals = Totals();
}


void NetShotLedger::Settle( NetShotComparison InComparison )
{
    if( InComparison.HasClient && InComparison.HasServer )
    {
        ++m_totals.Paired;
        if( InComparison.Agrees() )
        {
            ++m_totals.Agreed;
            m_totals.SamePart += InComparison.SamePart() ? 1 : 0;
        }
        else
        {
            ++m_totals.Disagreed;
        }
        const float delta = InComparison.PointDelta();
        if( delta >= 0.f )
        {
            ++m_totals.PointPairs;
            m_totals.SumPointDelta += delta;
            m_totals.MaxPointDelta = std::max( m_totals.MaxPointDelta, static_cast<double>( delta ) );
        }
    }
    else if( InComparison.HasClient )
    {
        ++m_totals.ClientOnly;
    }
    else
    {
        ++m_totals.ServerOnly;
    }
    m_recent.push_back( std::move( InComparison ) );
    while( m_recent.size() > kMaxRecent )
    {
        m_recent.pop_front();
    }
}
