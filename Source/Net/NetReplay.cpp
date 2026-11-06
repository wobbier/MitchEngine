#include "PCH.h"
#include "Net/NetReplay.h"
#include "CLog.h"
#include "Jobs/BackgroundLane.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include "Net/NetVersion.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <utility>

// zlib comes with Assimp's contrib (zlibstatic is linked on every platform).
#include "../../ThirdParty/Assimp/contrib/zlib/zlib.h"

namespace
{
    constexpr size_t kMaxChunkBytes = 512u * 1024u * 1024u;
    constexpr uint64_t kMaxListCount = 1u << 20;
    // Writes queued on the lane before the recorder warns (once) that the disk isn't keeping up.
    constexpr uint32_t kBacklogWarning = 8;

    void PutU32( std::vector<uint8_t>& InOut, uint32_t InValue )
    {
        for( int i = 0; i < 4; ++i )
        {
            InOut.push_back( static_cast<uint8_t>( InValue >> ( 8 * i ) ) );
        }
    }

    void PutU64( std::vector<uint8_t>& InOut, uint64_t InValue )
    {
        for( int i = 0; i < 8; ++i )
        {
            InOut.push_back( static_cast<uint8_t>( InValue >> ( 8 * i ) ) );
        }
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

    uint64_t GetU64( const uint8_t* InData )
    {
        uint64_t value = 0;
        for( int i = 0; i < 8; ++i )
        {
            value |= static_cast<uint64_t>( InData[i] ) << ( 8 * i );
        }
        return value;
    }

    uint64_t Checksum( const uint8_t* InData, size_t InSize )
    {
        return NetVersion::Hash( std::string_view( reinterpret_cast<const char*>( InData ), InSize ) );
    }

    void WriteDouble( BitWriter& InWriter, double InValue )
    {
        uint64_t bits = 0;
        std::memcpy( &bits, &InValue, sizeof( bits ) );
        InWriter.WriteBits( static_cast<uint32_t>( bits ), 32 );
        InWriter.WriteBits( static_cast<uint32_t>( bits >> 32 ), 32 );
    }

    double ReadDouble( BitReader& InReader )
    {
        const uint64_t low = InReader.ReadBits( 32 );
        const uint64_t high = InReader.ReadBits( 32 );
        const uint64_t bits = low | ( high << 32 );
        double value = 0.0;
        std::memcpy( &value, &bits, sizeof( value ) );
        return value;
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

    // A command's view tick, as how far behind its own tick in 1/64 ticks when that's exact (what
    // the wire sends), else raw.
    void WriteViewTick( BitWriter& InWriter, uint32_t InTick, double InViewTick )
    {
        InWriter.WriteBool( InViewTick >= 0.0 );
        if( InViewTick < 0.0 )
        {
            WriteDouble( InWriter, InViewTick );
            return;
        }
        const double behind = ( static_cast<double>( InTick ) - InViewTick ) * 64.0;
        const bool exact = behind >= 0.0 && behind < 1e9 && std::floor( behind ) == behind
            && static_cast<double>( InTick ) - static_cast<double>( static_cast<uint64_t>( behind ) ) / 64.0 == InViewTick;
        InWriter.WriteBool( exact );
        if( exact )
        {
            InWriter.WriteVarUInt( static_cast<uint64_t>( behind ) );
        }
        else
        {
            WriteDouble( InWriter, InViewTick );
        }
    }

    double ReadViewTick( BitReader& InReader, uint32_t InTick )
    {
        if( !InReader.ReadBool() )
        {
            return ReadDouble( InReader );
        }
        if( InReader.ReadBool() )
        {
            return static_cast<double>( InTick ) - static_cast<double>( InReader.ReadVarUInt() ) / 64.0;
        }
        return ReadDouble( InReader );
    }

    bool SameBits( float InA, float InB )
    {
        return std::memcmp( &InA, &InB, sizeof( float ) ) == 0;
    }

    // A value the wire quantized (axes, pitch: NetQuantize at InBits) as its InBits, when that's
    // exact; else the raw float.
    void WriteGridFloat( BitWriter& InWriter, float InValue, float InMin, float InMax, int InBits )
    {
        const uint32_t bits = NetQuantize::Quantize( InValue, InMin, InMax, InBits );
        const bool exact = SameBits( NetQuantize::Dequantize( bits, InMin, InMax, InBits ), InValue );
        InWriter.WriteBool( exact );
        if( exact )
        {
            InWriter.WriteBits( bits, InBits );
        }
        else
        {
            InWriter.WriteFloat( InValue );
        }
    }

    float ReadGridFloat( BitReader& InReader, float InMin, float InMax, int InBits )
    {
        return InReader.ReadBool() ? NetQuantize::Dequantize( InReader.ReadBits( InBits ), InMin, InMax, InBits ) : InReader.ReadFloat();
    }

    // Yaw as the wire sends it (NetCommands: 16 bits of a turn), when that's exact.
    void WriteYaw( BitWriter& InWriter, float InDegrees )
    {
        const double wrapped = InDegrees - 360.0 * std::floor( InDegrees / 360.0 );
        const uint32_t bits = static_cast<uint32_t>( std::llround( wrapped / 360.0 * 65536.0 ) ) & 0xFFFFu;
        const bool exact = SameBits( static_cast<float>( bits * 360.0 / 65536.0 ), InDegrees );
        InWriter.WriteBool( exact );
        if( exact )
        {
            InWriter.WriteBits( bits, 16 );
        }
        else
        {
            InWriter.WriteFloat( InDegrees );
        }
    }

    float ReadYaw( BitReader& InReader )
    {
        return InReader.ReadBool() ? static_cast<float>( InReader.ReadBits( 16 ) * 360.0 / 65536.0 ) : InReader.ReadFloat();
    }

    // The link stats are for people: RTT to 0.1 ms, loss to 0.01%, slack to 0.01 tick.
    struct LinkBits
    {
        uint64_t RoundTrip = 0;
        uint64_t Loss = 0;
        int64_t Slack = 0;
        uint32_t Misses = 0;
        uint32_t LateCommands = 0;
        uint32_t Resyncs = 0;

        explicit LinkBits( const NetReplayClient& InClient )
            : RoundTrip( static_cast<uint64_t>( std::llround( std::clamp( InClient.RoundTripMs, 0.f, 1e6f ) * 10.0 ) ) )
            , Loss( static_cast<uint64_t>( std::llround( std::clamp( InClient.PacketLoss, 0.f, 1.f ) * 10000.0 ) ) )
            , Slack( std::llround( std::clamp( InClient.Slack, -1e6f, 1e6f ) * 100.0 ) )
            , Misses( InClient.Misses )
            , LateCommands( InClient.LateCommands )
            , Resyncs( InClient.Resyncs )
        {
        }
        bool operator==( const LinkBits& InOther ) const
        {
            return RoundTrip == InOther.RoundTrip && Loss == InOther.Loss && Slack == InOther.Slack && Misses == InOther.Misses && LateCommands == InOther.LateCommands
                && Resyncs == InOther.Resyncs;
        }
    };

    uint64_t ZigZag( int64_t InValue )
    {
        return ( static_cast<uint64_t>( InValue ) << 1 ) ^ static_cast<uint64_t>( InValue >> 63 );
    }

    int64_t UnZigZag( uint64_t InValue )
    {
        return static_cast<int64_t>( InValue >> 1 ) ^ -static_cast<int64_t>( InValue & 1 );
    }

    bool SameInput( const InputCommand& InA, const InputCommand& InB )
    {
        return InA.Buttons == InB.Buttons && InA.Axes == InB.Axes && InA.HasAim == InB.HasAim && InA.Yaw == InB.Yaw && InA.Pitch == InB.Pitch;
    }

    void WriteClient( BitWriter& InWriter, uint32_t InTick, const NetReplayClient& InClient, const NetReplayClient* InPrevious )
    {
        InWriter.WriteBits( InClient.Id, 8 );
        const InputCommand& command = InClient.Command;
        InWriter.WriteBool( command.Tick == InTick );
        if( command.Tick != InTick )
        {
            InWriter.WriteBits( command.Tick, 32 );
        }
        // Held input costs a bit a tick.
        const bool same = InPrevious && SameInput( InPrevious->Command, command );
        InWriter.WriteBool( same );
        if( !same )
        {
            InWriter.WriteVarUInt( command.Buttons );
            for( const float axis : command.Axes )
            {
                WriteGridFloat( InWriter, axis, -1.f, 1.f, 16 );
            }
            InWriter.WriteBool( command.HasAim );
            WriteYaw( InWriter, command.Yaw );
            WriteGridFloat( InWriter, command.Pitch, -90.f, 90.f, 16 );
        }
        WriteViewTick( InWriter, command.Tick, command.ViewTick );
        InWriter.WriteBool( InClient.Missed );
        // The link, when it changed (at its resolution).
        const LinkBits link( InClient );
        const bool sameLink = InPrevious && LinkBits( *InPrevious ) == link;
        InWriter.WriteBool( sameLink );
        if( !sameLink )
        {
            InWriter.WriteVarUInt( link.RoundTrip );
            InWriter.WriteVarUInt( link.Loss );
            InWriter.WriteVarUInt( ZigZag( link.Slack ) );
            InWriter.WriteVarUInt( link.Misses );
            InWriter.WriteVarUInt( link.LateCommands );
            InWriter.WriteVarUInt( link.Resyncs );
        }
    }

    bool ReadClient( BitReader& InReader, uint32_t InTick, NetReplayClient& OutClient, const NetReplayClient* InPrevious )
    {
        OutClient.Id = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
        InputCommand& command = OutClient.Command;
        command.Tick = InReader.ReadBool() ? InTick : InReader.ReadBits( 32 );
        if( InReader.ReadBool() )
        {
            if( !InPrevious )
            {
                return false;
            }
            command.Buttons = InPrevious->Command.Buttons;
            command.Axes = InPrevious->Command.Axes;
            command.HasAim = InPrevious->Command.HasAim;
            command.Yaw = InPrevious->Command.Yaw;
            command.Pitch = InPrevious->Command.Pitch;
        }
        else
        {
            command.Buttons = static_cast<uint32_t>( InReader.ReadVarUInt() );
            for( float& axis : command.Axes )
            {
                axis = ReadGridFloat( InReader, -1.f, 1.f, 16 );
            }
            command.HasAim = InReader.ReadBool();
            command.Yaw = ReadYaw( InReader );
            command.Pitch = ReadGridFloat( InReader, -90.f, 90.f, 16 );
        }
        command.ViewTick = ReadViewTick( InReader, command.Tick );
        OutClient.Missed = InReader.ReadBool();
        if( InReader.ReadBool() )
        {
            if( !InPrevious )
            {
                return false;
            }
            OutClient.RoundTripMs = InPrevious->RoundTripMs;
            OutClient.PacketLoss = InPrevious->PacketLoss;
            OutClient.Slack = InPrevious->Slack;
            OutClient.Misses = InPrevious->Misses;
            OutClient.LateCommands = InPrevious->LateCommands;
            OutClient.Resyncs = InPrevious->Resyncs;
        }
        else
        {
            OutClient.RoundTripMs = static_cast<float>( static_cast<double>( InReader.ReadVarUInt() ) / 10.0 );
            OutClient.PacketLoss = static_cast<float>( static_cast<double>( InReader.ReadVarUInt() ) / 10000.0 );
            OutClient.Slack = static_cast<float>( static_cast<double>( UnZigZag( InReader.ReadVarUInt() ) ) / 100.0 );
            OutClient.Misses = static_cast<uint32_t>( InReader.ReadVarUInt() );
            OutClient.LateCommands = static_cast<uint32_t>( InReader.ReadVarUInt() );
            OutClient.Resyncs = static_cast<uint32_t>( InReader.ReadVarUInt() );
        }
        return !InReader.IsOverflowed();
    }

    bool SameVectorBits( const Vector3& InA, const Vector3& InB )
    {
        return SameBits( InA.x, InB.x ) && SameBits( InA.y, InB.y ) && SameBits( InA.z, InB.z );
    }

    bool SameHitboxes( const std::vector<NetReplayHitbox>& InA, const std::vector<NetReplayHitbox>& InB )
    {
        if( InA.size() != InB.size() )
        {
            return false;
        }
        for( size_t i = 0; i < InA.size(); ++i )
        {
            const NetReplayHitbox& a = InA[i];
            const NetReplayHitbox& b = InB[i];
            if( a.NetId != b.NetId || a.Name != b.Name || a.Shape != b.Shape || a.Occluder != b.Occluder || !SameVectorBits( a.Position, b.Position )
                || !SameBits( a.Rotation.x, b.Rotation.x ) || !SameBits( a.Rotation.y, b.Rotation.y ) || !SameBits( a.Rotation.z, b.Rotation.z )
                || !SameBits( a.Rotation.w, b.Rotation.w ) || !SameBits( a.Radius, b.Radius ) || !SameBits( a.HalfHeight, b.HalfHeight )
                || !SameVectorBits( a.HalfExtents, b.HalfExtents ) )
            {
                return false;
            }
        }
        return true;
    }

    // InEarlier: the tick's shots before this one. Format 3 points at the newest of them with the
    // same hitboxes rather than repeat them (a pellet, a pierce step, every arrow in flight).
    void WriteShot( BitWriter& InWriter, const NetReplayShot& InShot, const NetReplayShot* InEarlier, size_t InEarlierCount )
    {
        InWriter.WriteBits( InShot.Client, 8 );
        InWriter.WriteVarUInt( InShot.Shooter );
        InWriter.WriteVarUInt( InShot.Instigator );
        WriteVector( InWriter, InShot.Origin );
        WriteVector( InWriter, InShot.Direction );
        InWriter.WriteFloat( InShot.Range );
        WriteDouble( InWriter, InShot.ViewTick );
        WriteDouble( InWriter, InShot.RewoundTicks );
        InWriter.WriteBool( InShot.Hit );
        InWriter.WriteBool( InShot.Occluded );
        InWriter.WriteBool( InShot.Clamped );
        InWriter.WriteBool( InShot.Rejected );
        InWriter.WriteVarUInt( InShot.Target );
        InWriter.WriteString( InShot.TargetPart );
        WriteVector( InWriter, InShot.Point );
        InWriter.WriteFloat( InShot.Distance );
        InWriter.WriteVarUInt( InShot.Tested );
        // 0: the list follows; k: the same as the shot k back.
        uint64_t sameAs = 0;
        for( size_t back = 1; back <= InEarlierCount && !InShot.Hitboxes.empty(); ++back )
        {
            if( SameHitboxes( InEarlier[InEarlierCount - back].Hitboxes, InShot.Hitboxes ) )
            {
                sameAs = back;
                break;
            }
        }
        InWriter.WriteVarUInt( sameAs );
        if( sameAs != 0 )
        {
            return;
        }
        InWriter.WriteVarUInt( InShot.Hitboxes.size() );
        for( const NetReplayHitbox& hitbox : InShot.Hitboxes )
        {
            InWriter.WriteVarUInt( hitbox.NetId );
            InWriter.WriteString( hitbox.Name );
            InWriter.WriteBits( hitbox.Shape, 8 );
            WriteVector( InWriter, hitbox.Position );
            InWriter.WriteFloat( hitbox.Rotation.x );
            InWriter.WriteFloat( hitbox.Rotation.y );
            InWriter.WriteFloat( hitbox.Rotation.z );
            InWriter.WriteFloat( hitbox.Rotation.w );
            InWriter.WriteFloat( hitbox.Radius );
            InWriter.WriteFloat( hitbox.HalfHeight );
            WriteVector( InWriter, hitbox.HalfExtents );
            InWriter.WriteBool( hitbox.Occluder );
        }
    }

    bool ReadShot( BitReader& InReader, NetReplayShot& OutShot, const NetReplayShot* InEarlier, size_t InEarlierCount, uint32_t InVersion )
    {
        OutShot.Client = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
        OutShot.Shooter = static_cast<uint32_t>( InReader.ReadVarUInt() );
        OutShot.Instigator = InVersion >= 3 ? static_cast<uint32_t>( InReader.ReadVarUInt() ) : 0u;
        OutShot.Origin = ReadVector( InReader );
        OutShot.Direction = ReadVector( InReader );
        OutShot.Range = InReader.ReadFloat();
        OutShot.ViewTick = ReadDouble( InReader );
        OutShot.RewoundTicks = ReadDouble( InReader );
        OutShot.Hit = InReader.ReadBool();
        OutShot.Occluded = InReader.ReadBool();
        OutShot.Clamped = InReader.ReadBool();
        OutShot.Rejected = InReader.ReadBool();
        OutShot.Target = static_cast<uint32_t>( InReader.ReadVarUInt() );
        OutShot.TargetPart = InReader.ReadString();
        OutShot.Point = ReadVector( InReader );
        OutShot.Distance = InReader.ReadFloat();
        OutShot.Tested = static_cast<uint32_t>( InReader.ReadVarUInt() );
        if( InVersion >= 3 )
        {
            const uint64_t sameAs = InReader.ReadVarUInt();
            if( sameAs > InEarlierCount || InReader.IsOverflowed() )
            {
                return false;
            }
            if( sameAs != 0 )
            {
                OutShot.Hitboxes = InEarlier[InEarlierCount - sameAs].Hitboxes;
                return true;
            }
        }
        const uint64_t count = InReader.ReadVarUInt();
        if( count > kMaxListCount || InReader.IsOverflowed() )
        {
            return false;
        }
        OutShot.Hitboxes.resize( static_cast<size_t>( count ) );
        for( NetReplayHitbox& hitbox : OutShot.Hitboxes )
        {
            hitbox.NetId = static_cast<uint32_t>( InReader.ReadVarUInt() );
            hitbox.Name = InReader.ReadString();
            hitbox.Shape = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
            hitbox.Position = ReadVector( InReader );
            hitbox.Rotation.x = InReader.ReadFloat();
            hitbox.Rotation.y = InReader.ReadFloat();
            hitbox.Rotation.z = InReader.ReadFloat();
            hitbox.Rotation.w = InReader.ReadFloat();
            hitbox.Radius = InReader.ReadFloat();
            hitbox.HalfHeight = InReader.ReadFloat();
            hitbox.HalfExtents = ReadVector( InReader );
            hitbox.Occluder = InReader.ReadBool();
            if( InReader.IsOverflowed() )
            {
                return false;
            }
        }
        return !InReader.IsOverflowed();
    }

    uint32_t FloatBits( float InValue )
    {
        uint32_t bits = 0;
        std::memcpy( &bits, &InValue, sizeof( bits ) );
        return bits;
    }

    float BitsFloat( uint32_t InBits )
    {
        float value = 0.f;
        std::memcpy( &value, &InBits, sizeof( value ) );
        return value;
    }

    // A vector's float bits XORed with the tick before's (lossless): a mover's sign, exponent and
    // high mantissa rarely change from one tick to the next, so they come out as zeros for deflate.
    void WriteVectorXor( BitWriter& InWriter, const Vector3& InValue, const Vector3& InPrevious )
    {
        InWriter.WriteBits( FloatBits( InValue.x ) ^ FloatBits( InPrevious.x ), 32 );
        InWriter.WriteBits( FloatBits( InValue.y ) ^ FloatBits( InPrevious.y ), 32 );
        InWriter.WriteBits( FloatBits( InValue.z ) ^ FloatBits( InPrevious.z ), 32 );
    }

    Vector3 ReadVectorXor( BitReader& InReader, const Vector3& InPrevious )
    {
        const float x = BitsFloat( InReader.ReadBits( 32 ) ^ FloatBits( InPrevious.x ) );
        const float y = BitsFloat( InReader.ReadBits( 32 ) ^ FloatBits( InPrevious.y ) );
        const float z = BitsFloat( InReader.ReadBits( 32 ) ^ FloatBits( InPrevious.z ) );
        return Vector3( x, y, z );
    }

    // A mover's vectors, in the order of its change mask's bits.
    std::array<const Vector3*, 5> MoverVectors( const CharacterController::NetState& InState )
    {
        return { &InState.Position, &InState.Velocity, &InState.MoveInput, &InState.PendingMove, &InState.GroundNormal };
    }

    // A mover: each of its vectors (exact float bits) and its [Predicted] bytes when they changed
    // since the tick before (all of them at a chunk's start); the flags always.
    void WriteMover( BitWriter& InWriter, const NetReplayMover& InMover, const NetReplayMover* InPrevious )
    {
        InWriter.WriteVarUInt( InMover.NetId );
        const auto vectors = MoverVectors( InMover.State );
        uint32_t mask = 0;
        for( size_t i = 0; i < vectors.size(); ++i )
        {
            if( !InPrevious || !SameVectorBits( *vectors[i], *MoverVectors( InPrevious->State )[i] ) )
            {
                mask |= 1u << i;
            }
        }
        const bool predictedChanged = !InPrevious || InPrevious->Predicted != InMover.Predicted;
        InWriter.WriteBits( mask, static_cast<int>( vectors.size() ) );
        InWriter.WriteBool( predictedChanged );
        for( size_t i = 0; i < vectors.size(); ++i )
        {
            if( mask & ( 1u << i ) )
            {
                if( InPrevious )
                {
                    WriteVectorXor( InWriter, *vectors[i], *MoverVectors( InPrevious->State )[i] );
                }
                else
                {
                    WriteVector( InWriter, *vectors[i] );
                }
            }
        }
        InWriter.WriteBool( InMover.State.WantsJump );
        InWriter.WriteBool( InMover.State.Grounded );
        if( predictedChanged )
        {
            // The same size as the tick before's (a script's fields): XORed with it, so the few bytes
            // that changed stand among zeros for deflate.
            const bool xored = InPrevious && InPrevious->Predicted.size() == InMover.Predicted.size();
            InWriter.WriteBool( xored );
            if( xored )
            {
                std::vector<uint8_t> bytes( InMover.Predicted.size() );
                for( size_t i = 0; i < bytes.size(); ++i )
                {
                    bytes[i] = static_cast<uint8_t>( InMover.Predicted[i] ^ InPrevious->Predicted[i] );
                }
                InWriter.WriteBytes( bytes.data(), bytes.size() );
            }
            else
            {
                InWriter.WriteVarUInt( InMover.Predicted.size() );
                InWriter.WriteBytes( InMover.Predicted.data(), InMover.Predicted.size() );
            }
        }
    }

    const NetReplayMover* FindMover( const NetReplayTick* InTick, uint32_t InNetId )
    {
        if( !InTick )
        {
            return nullptr;
        }
        // Sorted by NetId.
        auto it = std::lower_bound( InTick->Movers.begin(), InTick->Movers.end(), InNetId,
            []( const NetReplayMover& InMover, uint32_t InId ) { return InMover.NetId < InId; } );
        return it != InTick->Movers.end() && it->NetId == InNetId ? &*it : nullptr;
    }

    bool ReadMover( BitReader& InReader, NetReplayMover& OutMover, const NetReplayTick* InPrevious )
    {
        OutMover.NetId = static_cast<uint32_t>( InReader.ReadVarUInt() );
        const uint32_t mask = InReader.ReadBits( 5 );
        const bool predictedChanged = InReader.ReadBool();
        const NetReplayMover* previous = FindMover( InPrevious, OutMover.NetId );
        if( ( mask != 0x1Fu || !predictedChanged ) && !previous )
        {
            return false;
        }
        CharacterController::NetState& state = OutMover.State;
        std::array<Vector3*, 5> vectors = { &state.Position, &state.Velocity, &state.MoveInput, &state.PendingMove, &state.GroundNormal };
        for( size_t i = 0; i < vectors.size(); ++i )
        {
            if( !( mask & ( 1u << i ) ) )
            {
                *vectors[i] = *MoverVectors( previous->State )[i];
            }
            else
            {
                *vectors[i] = previous ? ReadVectorXor( InReader, *MoverVectors( previous->State )[i] ) : ReadVector( InReader );
            }
        }
        state.WantsJump = InReader.ReadBool();
        state.Grounded = InReader.ReadBool();
        if( predictedChanged )
        {
            if( InReader.ReadBool() )
            {
                if( !previous )
                {
                    return false;
                }
                OutMover.Predicted.resize( previous->Predicted.size() );
                InReader.ReadBytes( OutMover.Predicted.data(), OutMover.Predicted.size() );
                for( size_t i = 0; i < OutMover.Predicted.size(); ++i )
                {
                    OutMover.Predicted[i] ^= previous->Predicted[i];
                }
            }
            else
            {
                const uint64_t size = InReader.ReadVarUInt();
                if( size > kMaxChunkBytes || InReader.IsOverflowed() )
                {
                    return false;
                }
                OutMover.Predicted.resize( static_cast<size_t>( size ) );
                InReader.ReadBytes( OutMover.Predicted.data(), OutMover.Predicted.size() );
            }
        }
        else
        {
            OutMover.Predicted = previous->Predicted;
        }
        return !InReader.IsOverflowed();
    }

    void WriteTick( BitWriter& InWriter, const NetReplayTick& InTick, const NetReplayTick* InPrevious )
    {
        if( InPrevious )
        {
            InWriter.WriteVarUInt( InTick.Tick - InPrevious->Tick );
        }
        else
        {
            InWriter.WriteBits( InTick.Tick, 32 );
        }
        InWriter.WriteVarUInt( InTick.TicksOwed );
        InWriter.WriteVarUInt( InTick.TicksDropped );

        // The state: a delta against the tick before (the keyframe: everything), with no budget.
        // The codec names both ticks: a state stamped otherwise (a hand-made record) is copied.
        NetSnapshot stamped;
        const NetSnapshot* state = &InTick.State;
        if( state->Tick != InTick.Tick )
        {
            stamped = InTick.State;
            stamped.Tick = InTick.Tick;
            state = &stamped;
        }
        NetSnapshot stampedBaseline;
        const NetSnapshot* baseline = InPrevious ? &InPrevious->State : nullptr;
        if( baseline && baseline->Tick != InPrevious->Tick )
        {
            stampedBaseline = InPrevious->State;
            stampedBaseline.Tick = InPrevious->Tick;
            baseline = &stampedBaseline;
        }
        NetSnapshot sent;
        NetSnapshotCodec::Write( InWriter, *state, baseline, std::numeric_limits<size_t>::max() / 2, sent );

        InWriter.WriteVarUInt( InTick.Entities.size() );
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            InWriter.WriteVarUInt( entity.NetId );
            InWriter.WriteString( entity.Name );
            InWriter.WriteString( entity.Prefab );
            InWriter.WriteBits( entity.Owner, 8 );
        }

        InWriter.WriteVarUInt( InTick.Clients.size() );
        for( const NetReplayClient& client : InTick.Clients )
        {
            const NetReplayClient* previous = nullptr;
            if( InPrevious )
            {
                for( const NetReplayClient& candidate : InPrevious->Clients )
                {
                    if( candidate.Id == client.Id )
                    {
                        previous = &candidate;
                    }
                }
            }
            WriteClient( InWriter, InTick.Tick, client, previous );
        }

        // Sorted by NetId, so the reader finds the tick before's (the recorder's are).
        std::vector<const NetReplayMover*> movers;
        movers.reserve( InTick.Movers.size() );
        for( const NetReplayMover& mover : InTick.Movers )
        {
            movers.push_back( &mover );
        }
        std::sort( movers.begin(), movers.end(), []( const NetReplayMover* InA, const NetReplayMover* InB ) { return InA->NetId < InB->NetId; } );
        InWriter.WriteVarUInt( movers.size() );
        for( const NetReplayMover* mover : movers )
        {
            const NetReplayMover* previous = nullptr;
            if( InPrevious )
            {
                for( const NetReplayMover& candidate : InPrevious->Movers )
                {
                    previous = candidate.NetId == mover->NetId ? &candidate : previous;
                }
            }
            WriteMover( InWriter, *mover, previous );
        }

        InWriter.WriteVarUInt( InTick.Shots.size() );
        for( size_t i = 0; i < InTick.Shots.size(); ++i )
        {
            WriteShot( InWriter, InTick.Shots[i], InTick.Shots.data(), i );
        }

        InWriter.WriteVarUInt( InTick.Events.size() );
        for( const NetReplayEvent& event : InTick.Events )
        {
            InWriter.WriteString( event.Name );
            InWriter.WriteString( event.Payload );
            InWriter.WriteVarUInt( event.Entity );
        }
    }

    bool ReadTick( BitReader& InReader, NetReplayTick& OutTick, const NetReplayTick* InPrevious, uint32_t InVersion )
    {
        OutTick.Tick = InPrevious ? InPrevious->Tick + static_cast<uint32_t>( InReader.ReadVarUInt() ) : InReader.ReadBits( 32 );
        OutTick.TicksOwed = static_cast<uint32_t>( InReader.ReadVarUInt() );
        OutTick.TicksDropped = static_cast<uint32_t>( InReader.ReadVarUInt() );
        if( !NetSnapshotCodec::Read( InReader, InPrevious ? &InPrevious->State : nullptr, OutTick.State ) || OutTick.State.Tick != OutTick.Tick )
        {
            return false;
        }

        uint64_t count = InReader.ReadVarUInt();
        if( count > kMaxListCount || InReader.IsOverflowed() )
        {
            return false;
        }
        OutTick.Entities.resize( static_cast<size_t>( count ) );
        for( NetReplayEntity& entity : OutTick.Entities )
        {
            entity.NetId = static_cast<uint32_t>( InReader.ReadVarUInt() );
            entity.Name = InReader.ReadString();
            entity.Prefab = InReader.ReadString();
            entity.Owner = static_cast<uint8_t>( InReader.ReadBits( 8 ) );
        }

        count = InReader.ReadVarUInt();
        if( count > 256 || InReader.IsOverflowed() )
        {
            return false;
        }
        OutTick.Clients.resize( static_cast<size_t>( count ) );
        for( NetReplayClient& client : OutTick.Clients )
        {
            // Its previous tick's record: the id comes first, so peek it.
            const NetReplayClient* previous = nullptr;
            BitReader peek = InReader;
            const uint8_t id = static_cast<uint8_t>( peek.ReadBits( 8 ) );
            if( InPrevious )
            {
                for( const NetReplayClient& candidate : InPrevious->Clients )
                {
                    if( candidate.Id == id )
                    {
                        previous = &candidate;
                    }
                }
            }
            if( !ReadClient( InReader, OutTick.Tick, client, previous ) )
            {
                return false;
            }
        }

        if( InVersion >= 3 )
        {
            count = InReader.ReadVarUInt();
            if( count > kMaxListCount || InReader.IsOverflowed() )
            {
                return false;
            }
            OutTick.Movers.resize( static_cast<size_t>( count ) );
            for( NetReplayMover& mover : OutTick.Movers )
            {
                if( !ReadMover( InReader, mover, InPrevious ) )
                {
                    return false;
                }
            }
        }

        count = InReader.ReadVarUInt();
        if( count > kMaxListCount || InReader.IsOverflowed() )
        {
            return false;
        }
        OutTick.Shots.resize( static_cast<size_t>( count ) );
        for( size_t i = 0; i < OutTick.Shots.size(); ++i )
        {
            if( !ReadShot( InReader, OutTick.Shots[i], OutTick.Shots.data(), i, InVersion ) )
            {
                return false;
            }
        }

        count = InReader.ReadVarUInt();
        if( count > kMaxListCount || InReader.IsOverflowed() )
        {
            return false;
        }
        OutTick.Events.resize( static_cast<size_t>( count ) );
        for( NetReplayEvent& event : OutTick.Events )
        {
            event.Name = InReader.ReadString( NetReplayFormat::kMaxEventName );
            event.Payload = InReader.ReadString( NetReplayFormat::kMaxEventPayload );
            event.Entity = static_cast<uint32_t>( InReader.ReadVarUInt() );
        }
        return !InReader.IsOverflowed();
    }

}


std::vector<uint8_t> NetReplayChunk::Encode( const std::vector<const NetReplayTick*>& InTicks, NetReplayCodec InCodec, size_t* OutRawBytes )
{
    BitWriter payload;
    payload.WriteVarUInt( InTicks.size() );
    const NetReplayTick* previous = nullptr;
    for( const NetReplayTick* tick : InTicks )
    {
        WriteTick( payload, *tick, previous );
        previous = tick;
    }
    const std::vector<uint8_t>& raw = payload.GetBytes();
    if( OutRawBytes )
    {
        *OutRawBytes = raw.size();
    }

    std::vector<uint8_t> stored;
    if( InCodec == NetReplayCodec::Deflate )
    {
        uLongf size = compressBound( static_cast<uLong>( raw.size() ) );
        stored.resize( size );
        if( compress2( stored.data(), &size, raw.data(), static_cast<uLong>( raw.size() ), Z_DEFAULT_COMPRESSION ) == Z_OK )
        {
            stored.resize( size );
        }
        else
        {
            InCodec = NetReplayCodec::Raw;
        }
    }
    if( InCodec == NetReplayCodec::Raw )
    {
        stored = raw;
    }

    std::vector<uint8_t> out;
    out.reserve( NetReplayFormat::kChunkHeaderBytes + stored.size() );
    PutU32( out, NetReplayFormat::kChunkMagic );
    PutU32( out, InTicks.empty() ? 0 : InTicks.front()->Tick );
    PutU32( out, InTicks.empty() ? 0 : InTicks.back()->Tick );
    PutU32( out, static_cast<uint32_t>( InTicks.size() ) );
    out.push_back( static_cast<uint8_t>( InCodec ) );
    out.push_back( 0 );
    out.push_back( 0 );
    out.push_back( 0 );
    PutU32( out, static_cast<uint32_t>( raw.size() ) );
    PutU32( out, static_cast<uint32_t>( stored.size() ) );
    PutU64( out, Checksum( stored.data(), stored.size() ) );
    out.insert( out.end(), stored.begin(), stored.end() );
    return out;
}


std::vector<uint8_t> NetReplayChunk::Encode( const std::vector<NetReplayTick>& InTicks, NetReplayCodec InCodec, size_t* OutRawBytes )
{
    std::vector<const NetReplayTick*> ticks;
    ticks.reserve( InTicks.size() );
    for( const NetReplayTick& tick : InTicks )
    {
        ticks.push_back( &tick );
    }
    return Encode( ticks, InCodec, OutRawBytes );
}


bool NetReplayChunk::ReadInfo( const uint8_t* InData, size_t InSize, Info& OutInfo )
{
    if( InSize < NetReplayFormat::kChunkHeaderBytes || GetU32( InData ) != NetReplayFormat::kChunkMagic )
    {
        return false;
    }
    OutInfo.FirstTick = GetU32( InData + 4 );
    OutInfo.LastTick = GetU32( InData + 8 );
    OutInfo.TickCount = GetU32( InData + 12 );
    OutInfo.Codec = static_cast<NetReplayCodec>( InData[16] );
    OutInfo.RawBytes = GetU32( InData + 20 );
    OutInfo.StoredBytes = GetU32( InData + 24 );
    OutInfo.Checksum = GetU64( InData + 28 );
    return ( OutInfo.Codec == NetReplayCodec::Raw || OutInfo.Codec == NetReplayCodec::Deflate ) && OutInfo.RawBytes <= kMaxChunkBytes
        && OutInfo.StoredBytes <= kMaxChunkBytes && OutInfo.LastTick >= OutInfo.FirstTick;
}


bool NetReplayChunk::Decode( const uint8_t* InData, size_t InSize, std::vector<NetReplayTick>& OutTicks, std::string* OutError, uint32_t InVersion )
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    OutTicks.clear();
    if( InVersion < NetReplayFormat::kMinVersion || InVersion > NetReplayFormat::kVersion )
    {
        return fail( "replay format " + std::to_string( InVersion ) + "; this build reads " + std::to_string( NetReplayFormat::kMinVersion ) + " to "
            + std::to_string( NetReplayFormat::kVersion ) );
    }
    Info info;
    if( !ReadInfo( InData, InSize, info ) )
    {
        return fail( "not a replay chunk" );
    }
    if( InSize < NetReplayFormat::kChunkHeaderBytes + info.StoredBytes )
    {
        return fail( "the chunk is cut short" );
    }
    const uint8_t* stored = InData + NetReplayFormat::kChunkHeaderBytes;
    if( Checksum( stored, info.StoredBytes ) != info.Checksum )
    {
        return fail( "the chunk's checksum doesn't match (a torn write?)" );
    }
    std::vector<uint8_t> raw;
    if( info.Codec == NetReplayCodec::Deflate )
    {
        raw.resize( info.RawBytes );
        uLongf size = info.RawBytes;
        if( uncompress( raw.data(), &size, stored, info.StoredBytes ) != Z_OK || size != info.RawBytes )
        {
            return fail( "the chunk won't inflate" );
        }
    }
    else
    {
        raw.assign( stored, stored + info.StoredBytes );
    }

    BitReader reader( raw );
    const uint64_t count = reader.ReadVarUInt();
    if( count != info.TickCount || count > kMaxListCount )
    {
        return fail( "the chunk's tick count doesn't match its header" );
    }
    OutTicks.resize( static_cast<size_t>( count ) );
    for( size_t i = 0; i < OutTicks.size(); ++i )
    {
        if( !ReadTick( reader, OutTicks[i], i > 0 ? &OutTicks[i - 1] : nullptr, InVersion ) )
        {
            OutTicks.clear();
            return fail( "tick " + std::to_string( i ) + " of the chunk from " + std::to_string( info.FirstTick ) + " won't decode" );
        }
    }
    if( !OutTicks.empty() && ( OutTicks.front().Tick != info.FirstTick || OutTicks.back().Tick != info.LastTick ) )
    {
        OutTicks.clear();
        return fail( "the chunk's ticks don't match its header" );
    }
    return true;
}


std::vector<uint8_t> NetReplayFile::EncodeHeader( const NetReplayHeader& InHeader )
{
    BitWriter header;
    header.WriteString( InHeader.Product );
    header.WriteString( InHeader.EngineCommit );
    header.WriteString( InHeader.GameCommit );
    header.WriteString( InHeader.Map );
    header.WriteString( InHeader.CommandLine );
    header.WriteString( InHeader.RecordedAt );
    header.WriteBits( InHeader.TickRate, 16 );
    header.WriteVarUInt( InHeader.KeyframeInterval );
    header.WriteVarUInt( InHeader.NetProtocol );
    for( const uint64_t hash : { InHeader.BuildHash, InHeader.ScriptSchemaHash, InHeader.SceneHash, InHeader.NetSchemaHash } )
    {
        header.WriteBits( static_cast<uint32_t>( hash ), 32 );
        header.WriteBits( static_cast<uint32_t>( hash >> 32 ), 32 );
    }

    std::vector<uint8_t> out( std::begin( NetReplayFormat::kMagic ), std::end( NetReplayFormat::kMagic ) );
    PutU32( out, NetReplayFormat::kVersion );
    PutU32( out, static_cast<uint32_t>( header.GetByteCount() ) );
    out.insert( out.end(), header.GetBytes().begin(), header.GetBytes().end() );
    return out;
}


bool NetReplayFile::DecodeHeader( const uint8_t* InData, size_t InSize, NetReplayHeader& OutHeader, size_t& OutSize, std::string* OutError )
{
    auto fail = [OutError]( const std::string& InWhy ) {
        if( OutError )
        {
            *OutError = InWhy;
        }
        return false;
    };
    if( InSize < 16 || std::memcmp( InData, NetReplayFormat::kMagic, 8 ) != 0 )
    {
        return fail( "not a match replay (.mreplay)" );
    }
    const uint32_t version = GetU32( InData + 8 );
    if( version < NetReplayFormat::kMinVersion || version > NetReplayFormat::kVersion )
    {
        return fail( "replay format " + std::to_string( version ) + "; this build reads " + std::to_string( NetReplayFormat::kMinVersion ) + " to "
            + std::to_string( NetReplayFormat::kVersion ) );
    }
    const uint32_t size = GetU32( InData + 12 );
    if( InSize < 16 + static_cast<size_t>( size ) )
    {
        return fail( "the replay's header is cut short" );
    }
    BitReader reader( InData + 16, size );
    OutHeader.FormatVersion = version;
    OutHeader.Product = reader.ReadText();
    OutHeader.EngineCommit = reader.ReadText();
    OutHeader.GameCommit = reader.ReadText();
    OutHeader.Map = reader.ReadText();
    OutHeader.CommandLine = reader.ReadText( 65536 );
    OutHeader.RecordedAt = reader.ReadText();
    OutHeader.TickRate = static_cast<uint16_t>( reader.ReadBits( 16 ) );
    OutHeader.KeyframeInterval = static_cast<uint32_t>( reader.ReadVarUInt() );
    OutHeader.NetProtocol = static_cast<uint32_t>( reader.ReadVarUInt() );
    uint64_t* hashes[] = { &OutHeader.BuildHash, &OutHeader.ScriptSchemaHash, &OutHeader.SceneHash, &OutHeader.NetSchemaHash };
    for( uint64_t* hash : hashes )
    {
        const uint64_t low = reader.ReadBits( 32 );
        const uint64_t high = reader.ReadBits( 32 );
        *hash = low | ( high << 32 );
    }
    if( reader.IsOverflowed() )
    {
        return fail( "the replay's header won't decode" );
    }
    OutSize = 16 + size;
    return true;
}


bool NetReplayFileSource::Open( const std::string& InPath )
{
    m_path = InPath;
    m_error.clear();
    m_chunks.clear();
    m_offsets.clear();
    m_hasIndex = false;
    std::ifstream file( InPath, std::ios::binary );
    if( !file )
    {
        m_error = "can't open " + InPath;
        return false;
    }
    file.seekg( 0, std::ios::end );
    m_fileSize = static_cast<uint64_t>( file.tellg() );
    file.seekg( 0 );

    // The header (it's small: read a generous prefix, more if its size says so).
    std::vector<uint8_t> prefix( static_cast<size_t>( std::min<uint64_t>( m_fileSize, 16 ) ) );
    file.read( reinterpret_cast<char*>( prefix.data() ), static_cast<std::streamsize>( prefix.size() ) );
    if( prefix.size() == 16 && std::memcmp( prefix.data(), NetReplayFormat::kMagic, 8 ) == 0 )
    {
        const uint64_t headerSize = 16ull + GetU32( prefix.data() + 12 );
        if( headerSize <= m_fileSize && headerSize < 16ull * 1024 * 1024 )
        {
            prefix.resize( static_cast<size_t>( headerSize ) );
            file.seekg( 0 );
            file.read( reinterpret_cast<char*>( prefix.data() ), static_cast<std::streamsize>( prefix.size() ) );
        }
    }
    size_t headerBytes = 0;
    if( !NetReplayFile::DecodeHeader( prefix.data(), prefix.size(), m_header, headerBytes, &m_error ) )
    {
        m_error = InPath + ": " + m_error;
        return false;
    }

    // The index, when the recorder closed the file.
    if( m_fileSize >= headerBytes + 24 )
    {
        uint8_t trailer[16] = {};
        file.clear();
        file.seekg( static_cast<std::streamoff>( m_fileSize - 16 ) );
        file.read( reinterpret_cast<char*>( trailer ), 16 );
        const uint64_t indexAt = GetU64( trailer );
        if( file && std::memcmp( trailer + 8, NetReplayFormat::kTrailer, 8 ) == 0 && indexAt >= headerBytes && indexAt + 8 <= m_fileSize - 16 )
        {
            std::vector<uint8_t> index( static_cast<size_t>( m_fileSize - 16 - indexAt ) );
            file.seekg( static_cast<std::streamoff>( indexAt ) );
            file.read( reinterpret_cast<char*>( index.data() ), static_cast<std::streamsize>( index.size() ) );
            const uint32_t count = index.size() >= 8 ? GetU32( index.data() + 4 ) : 0;
            if( file && index.size() >= 8 && GetU32( index.data() ) == NetReplayFormat::kIndexMagic && index.size() == 8 + static_cast<size_t>( count ) * 20 )
            {
                for( uint32_t i = 0; i < count; ++i )
                {
                    const uint8_t* entry = index.data() + 8 + static_cast<size_t>( i ) * 20;
                    NetReplayChunk::Info info;
                    info.FirstTick = GetU32( entry + 8 );
                    info.LastTick = GetU32( entry + 12 );
                    info.TickCount = GetU32( entry + 16 );
                    m_offsets.push_back( GetU64( entry ) );
                    m_chunks.push_back( info );
                }
                m_hasIndex = true;
            }
        }
    }
    // No index (the server died): walk the chunks, stopping at the first torn one.
    if( !m_hasIndex )
    {
        uint64_t at = headerBytes;
        uint8_t header[NetReplayFormat::kChunkHeaderBytes] = {};
        while( at + NetReplayFormat::kChunkHeaderBytes <= m_fileSize )
        {
            file.clear();
            file.seekg( static_cast<std::streamoff>( at ) );
            file.read( reinterpret_cast<char*>( header ), sizeof( header ) );
            NetReplayChunk::Info info;
            if( !file || !NetReplayChunk::ReadInfo( header, sizeof( header ), info ) || at + sizeof( header ) + info.StoredBytes > m_fileSize )
            {
                break;
            }
            m_offsets.push_back( at );
            m_chunks.push_back( info );
            at += sizeof( header ) + info.StoredBytes;
        }
    }
    return true;
}


bool NetReplayFileSource::ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes )
{
    if( InIndex >= m_offsets.size() )
    {
        return false;
    }
    std::ifstream file( m_path, std::ios::binary );
    uint8_t header[NetReplayFormat::kChunkHeaderBytes] = {};
    file.seekg( static_cast<std::streamoff>( m_offsets[InIndex] ) );
    file.read( reinterpret_cast<char*>( header ), sizeof( header ) );
    NetReplayChunk::Info info;
    if( !file || !NetReplayChunk::ReadInfo( header, sizeof( header ), info ) )
    {
        m_error = "chunk " + std::to_string( InIndex ) + " isn't where the index says";
        return false;
    }
    OutBytes.assign( header, header + sizeof( header ) );
    OutBytes.resize( sizeof( header ) + info.StoredBytes );
    file.read( reinterpret_cast<char*>( OutBytes.data() + sizeof( header ) ), info.StoredBytes );
    if( !file )
    {
        m_error = "chunk " + std::to_string( InIndex ) + " is cut short";
        return false;
    }
    m_chunks[InIndex] = info;
    return true;
}


bool NetReplayMemorySource::AddChunk( std::vector<uint8_t> InBytes )
{
    NetReplayChunk::Info info;
    if( !NetReplayChunk::ReadInfo( InBytes.data(), InBytes.size(), info ) )
    {
        return false;
    }
    m_infos.push_back( info );
    m_chunks.push_back( std::move( InBytes ) );
    return true;
}


bool NetReplayMemorySource::ReadChunk( size_t InIndex, std::vector<uint8_t>& OutBytes )
{
    if( InIndex >= m_chunks.size() )
    {
        return false;
    }
    OutBytes = m_chunks[InIndex];
    return true;
}


uint32_t NetReplayReader::GetFirstTick() const
{
    return IsEmpty() ? 0 : m_source.GetChunkInfo( 0 ).FirstTick;
}


uint32_t NetReplayReader::GetLastTick() const
{
    return IsEmpty() ? 0 : m_source.GetChunkInfo( m_source.GetChunkCount() - 1 ).LastTick;
}


uint64_t NetReplayReader::GetTickCount() const
{
    uint64_t count = 0;
    for( size_t i = 0; i < m_source.GetChunkCount(); ++i )
    {
        count += m_source.GetChunkInfo( i ).TickCount;
    }
    return count;
}


bool NetReplayReader::LoadChunk( size_t InIndex )
{
    if( m_loaded == InIndex )
    {
        return true;
    }
    m_loaded = static_cast<size_t>( -1 );
    if( !m_source.ReadChunk( InIndex, m_bytes ) )
    {
        m_error = "chunk " + std::to_string( InIndex ) + " can't be read";
        return false;
    }
    if( !NetReplayChunk::Decode( m_bytes.data(), m_bytes.size(), m_ticks, &m_error, m_source.GetHeader().FormatVersion ) )
    {
        return false;
    }
    m_loaded = InIndex;
    return true;
}


bool NetReplayReader::ReadTick( uint32_t InTick, NetReplayTick& OutTick )
{
    for( size_t i = 0; i < m_source.GetChunkCount(); ++i )
    {
        const NetReplayChunk::Info& info = m_source.GetChunkInfo( i );
        if( InTick < info.FirstTick || InTick > info.LastTick )
        {
            continue;
        }
        if( !LoadChunk( i ) )
        {
            return false;
        }
        for( const NetReplayTick& tick : m_ticks )
        {
            if( tick.Tick == InTick )
            {
                OutTick = tick;
                return true;
            }
        }
        return false;
    }
    return false;
}


bool NetReplayReader::ForEach( uint32_t InFrom, uint32_t InTo, const std::function<bool( const NetReplayTick& )>& InFn )
{
    for( size_t i = 0; i < m_source.GetChunkCount(); ++i )
    {
        const NetReplayChunk::Info& info = m_source.GetChunkInfo( i );
        if( info.LastTick < InFrom || info.FirstTick > InTo )
        {
            continue;
        }
        if( !LoadChunk( i ) )
        {
            return false;
        }
        for( const NetReplayTick& tick : m_ticks )
        {
            if( tick.Tick >= InFrom && tick.Tick <= InTo && !InFn( tick ) )
            {
                return true;
            }
        }
    }
    return true;
}


// The file, its chunk index and what has been written: touched only by the queue's tasks (one at a
// time, in order), except the counters and the error.
struct NetReplayRecorder::Writer
{
    std::ofstream File;
    std::string Path;
    uint64_t Offset = 0;        // the end of the whole chunks (where the next one, or a tail, goes)
    uint64_t FileEnd = 0;       // the end of what's in the file (a tail may reach past Offset)
    std::vector<std::pair<uint64_t, NetReplayChunk::Info>> Index;
    std::atomic<uint64_t> Chunks{ 0 };
    std::atomic<uint64_t> Bytes{ 0 };
    std::atomic<uint64_t> RawBytes{ 0 };
    std::atomic<bool> Failed{ false };
    mutable std::mutex ErrorLock;
    std::string Error;

    void Fail( const std::string& InWhy )
    {
        {
            std::lock_guard<std::mutex> lock( ErrorLock );
            if( !Error.empty() )
            {
                return;
            }
            Error = InWhy;
        }
        Failed.store( true );
        CLog::Log( CLog::LogType::Error, "Replay: " + InWhy + "; the rest of the match isn't written" );
    }

    // InBytes at Offset (over the tail, if one is there); a chunk moves Offset on, a tail doesn't.
    // Bytes a longer tail left past the end are cut off.
    bool WriteAt( const std::vector<uint8_t>& InBytes, bool InCommit )
    {
        File.seekp( static_cast<std::streamoff>( Offset ) );
        File.write( reinterpret_cast<const char*>( InBytes.data() ), static_cast<std::streamsize>( InBytes.size() ) );
        File.flush();
        if( !File )
        {
            return false;
        }
        const uint64_t end = Offset + InBytes.size();
        if( end < FileEnd )
        {
            std::error_code error;
            std::filesystem::resize_file( Path, end, error );
            if( error )
            {
                return false;
            }
        }
        FileEnd = end;
        if( InCommit )
        {
            Offset = end;
        }
        return true;
    }
};


NetReplayRecorder::NetReplayRecorder( NetReplayHeader InHeader, const NetReplaySettings& InSettings )
    : m_header( std::move( InHeader ) )
    , m_settings( InSettings )
{
    m_settings.KeyframeInterval = std::max<uint32_t>( 1, m_settings.KeyframeInterval );
    m_header.KeyframeInterval = m_settings.KeyframeInterval;
    m_header.FormatVersion = NetReplayFormat::kVersion;
    m_flushTicks = m_settings.FlushSeconds > 0.0 ? static_cast<uint32_t>( std::max( 1.0, std::round( m_settings.FlushSeconds * m_header.TickRate ) ) ) : 0u;
}


NetReplayRecorder::~NetReplayRecorder()
{
    Close();
}


bool NetReplayRecorder::OpenFile( const std::string& InPath )
{
    std::error_code error;
    const std::filesystem::path path( InPath );
    if( path.has_parent_path() )
    {
        std::filesystem::create_directories( path.parent_path(), error );
    }
    auto writer = std::make_shared<Writer>();
    writer->File.open( InPath, std::ios::binary | std::ios::trunc );
    const std::vector<uint8_t> header = NetReplayFile::EncodeHeader( m_header );
    writer->File.write( reinterpret_cast<const char*>( header.data() ), static_cast<std::streamsize>( header.size() ) );
    writer->File.flush();
    if( !writer->File )
    {
        CLog::Log( CLog::LogType::Error, "Replay: can't write " + InPath );
        return false;
    }
    writer->Path = InPath;
    writer->Offset = header.size();
    writer->FileEnd = header.size();
    writer->Bytes = header.size();
    m_path = InPath;
    m_writer = std::move( writer );
    m_queue = std::make_unique<Jobs::SerialQueue>( "Replay Writer" );
    return true;
}


void NetReplayRecorder::Record( NetReplayTick&& InTick )
{
    if( m_closed )
    {
        return;
    }
    InTick.State.Tick = InTick.Tick;
    // What every NetId is: the tick's changes go in the table; gone ones leave it.
    for( const NetReplayEntity& entity : InTick.Entities )
    {
        m_entities[entity.NetId] = entity;
    }
    for( auto it = m_entities.begin(); it != m_entities.end(); )
    {
        it = InTick.State.Entities.count( it->first ) ? std::next( it ) : m_entities.erase( it );
    }
    // A chunk's first tick names every live one, so the chunk reads alone (counted apart from
    // m_chunk: a ring-only recorder has none, and its ticks carry only what changed too).
    const bool chunkStart = m_chunkTicks == 0;
    m_chunkTicks = ( m_chunkTicks + 1 ) % m_settings.KeyframeInterval;
    if( chunkStart )
    {
        InTick.Entities.clear();
        for( const auto& [id, entity] : m_entities )
        {
            InTick.Entities.push_back( entity );
        }
    }

    auto tick = std::make_shared<const NetReplayTick>( std::move( InTick ) );
    ++m_ticks;
    const size_t ringTicks = static_cast<size_t>( std::max( 0.0, m_settings.RingSeconds * m_header.TickRate ) );
    if( ringTicks > 0 )
    {
        m_ring.push_back( tick );
        while( m_ring.size() > ringTicks )
        {
            m_ring.pop_front();
        }
    }
    if( m_writer )
    {
        m_chunk.push_back( std::move( tick ) );
        if( m_chunk.size() >= m_settings.KeyframeInterval )
        {
            SubmitChunk();
        }
        else if( m_flushTicks > 0 && m_chunk.size() % m_flushTicks == 0 )
        {
            SubmitTail();
        }
    }
}


void NetReplayRecorder::SubmitTail()
{
    // Only when the lane has caught up: a tail never adds to a backlog (the next one will do).
    if( !m_queue || m_queue->GetPending() > 0 || m_writer->Failed.load() )
    {
        return;
    }
    // The open chunk so far, as a chunk of its own after the whole ones; the next tail or the
    // chunk itself overwrites it. A server that dies loses only the ticks since.
    m_queue->Submit( [writer = m_writer, ticks = m_chunk, codec = m_settings.Codec]() {
        if( writer->Failed.load() )
        {
            return;
        }
        std::vector<const NetReplayTick*> pointers;
        pointers.reserve( ticks.size() );
        for( const auto& tick : ticks )
        {
            pointers.push_back( tick.get() );
        }
        if( !writer->WriteAt( NetReplayChunk::Encode( pointers, codec ), false ) )
        {
            writer->Fail( "the open chunk didn't write" );
        }
    } );
}


void NetReplayRecorder::SubmitChunk()
{
    if( m_chunk.empty() || !m_writer || !m_queue )
    {
        m_chunk.clear();
        return;
    }
    if( m_writer->Failed.load() )
    {
        m_chunk.clear();
        return;
    }
    const uint32_t pending = m_queue->GetPending();
    if( !m_warnedBacklog && pending >= kBacklogWarning )
    {
        m_warnedBacklog = true;
        CLog::Log( CLog::LogType::Warning, "Replay: " + std::to_string( pending ) + " chunks wait to be written (the disk is slow); recording goes on in memory" );
    }
    // Past MaxBacklogSeconds of the match waiting (a hung or failing disk), the file stops: the
    // queued chunks are let go rather than held without end. The ring goes on.
    const double backlogSeconds = static_cast<double>( pending ) * m_settings.KeyframeInterval / std::max<uint16_t>( 1, m_header.TickRate );
    if( m_settings.MaxBacklogSeconds > 0.0 && backlogSeconds >= m_settings.MaxBacklogSeconds )
    {
        m_writer->Fail( "the disk can't keep up (" + std::to_string( static_cast<int>( backlogSeconds ) ) + " s of the match wait to be written)" );
        m_queue->Clear();
        m_chunk.clear();
        return;
    }
    // Encode, compress and write on the lane: the ticks are immutable from here.
    m_queue->Submit( [writer = m_writer, ticks = std::exchange( m_chunk, {} ), codec = m_settings.Codec]() {
        if( writer->Failed.load() )
        {
            return;
        }
        std::vector<const NetReplayTick*> pointers;
        pointers.reserve( ticks.size() );
        for( const auto& tick : ticks )
        {
            pointers.push_back( tick.get() );
        }
        size_t raw = 0;
        const std::vector<uint8_t> bytes = NetReplayChunk::Encode( pointers, codec, &raw );
        const uint64_t offset = writer->Offset;
        if( !writer->WriteAt( bytes, true ) )
        {
            writer->Fail( "a chunk didn't write" );
            return;
        }
        NetReplayChunk::Info info;
        NetReplayChunk::ReadInfo( bytes.data(), bytes.size(), info );
        writer->Index.emplace_back( offset, info );
        writer->RawBytes += raw;
        writer->Bytes += bytes.size();
        ++writer->Chunks;
    } );
}


void NetReplayRecorder::Close()
{
    if( m_closed )
    {
        return;
    }
    m_closed = true;
    if( !m_writer || !m_queue )
    {
        return;
    }
    SubmitChunk();
    std::future<void> done = m_queue->Submit( [writer = m_writer]() {
        if( writer->Failed.load() )
        {
            writer->File.close();
            return;
        }
        std::vector<uint8_t> index;
        PutU32( index, NetReplayFormat::kIndexMagic );
        PutU32( index, static_cast<uint32_t>( writer->Index.size() ) );
        for( const auto& [offset, info] : writer->Index )
        {
            PutU64( index, offset );
            PutU32( index, info.FirstTick );
            PutU32( index, info.LastTick );
            PutU32( index, info.TickCount );
        }
        PutU64( index, writer->Offset );
        index.insert( index.end(), std::begin( NetReplayFormat::kTrailer ), std::end( NetReplayFormat::kTrailer ) );
        const bool written = writer->WriteAt( index, true );
        writer->File.close();
        if( !written || !writer->File )
        {
            writer->Fail( "the index didn't write" );
            return;
        }
        writer->Bytes += index.size();
    } );
    // Bounded: a hung disk mustn't hold the server's shutdown (its report and goodbyes) forever.
    // (The lane always runs Normal work, and runs it inline once shut down, so this only times out
    // on a write that doesn't return.)
    const auto timeout = std::chrono::duration<double>( std::max( 0.0, m_settings.CloseTimeoutSeconds ) );
    if( m_settings.CloseTimeoutSeconds > 0.0 && done.wait_for( timeout ) != std::future_status::ready )
    {
        m_writer->Fail( "the last writes didn't finish in " + std::to_string( static_cast<int>( m_settings.CloseTimeoutSeconds ) ) + " s (the disk hung); the file has no index" );
        m_queue->Clear();
        // Its destructor would wait for the hung write: let the lane finish with it whenever it can
        // (the tasks hold the writer).
        (void)m_queue.release();
        return;
    }
    m_queue->Flush();
    m_queue.reset();
}


void NetReplayRecorder::WaitForWrites()
{
    if( m_queue )
    {
        m_queue->Flush();
    }
}


uint64_t NetReplayRecorder::GetChunksWritten() const
{
    return m_writer ? m_writer->Chunks.load() : 0;
}


uint64_t NetReplayRecorder::GetBytesWritten() const
{
    return m_writer ? m_writer->Bytes.load() : 0;
}


uint64_t NetReplayRecorder::GetRawBytes() const
{
    return m_writer ? m_writer->RawBytes.load() : 0;
}


uint32_t NetReplayRecorder::GetPendingWrites() const
{
    return m_queue ? m_queue->GetPending() : 0;
}


bool NetReplayRecorder::HasFailed() const
{
    return m_writer && m_writer->Failed.load();
}


std::string NetReplayRecorder::GetError() const
{
    if( !m_writer )
    {
        return {};
    }
    std::lock_guard<std::mutex> lock( m_writer->ErrorLock );
    return m_writer->Error;
}
