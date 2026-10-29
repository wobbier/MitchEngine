#include "PCH.h"
#include "Net/NetSnapshot.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"
#include "Net/BitStream.h"
#include "Net/NetQuantize.h"
#include "Reflection/Reflection.h"
#include <cmath>
#include <cstring>

namespace
{
    constexpr float kSmallestThree = 0.70710678f;

    bool ForEachReplicated( BaseComponent& InComponent, const std::function<void( const Reflection::FieldInfo&, void* )>& InVisit )
    {
        const Reflection::TypeInfo* type = InComponent.GetTypeInfo();
        void* object = InComponent.GetReflectedObject();
        if( type == nullptr || object == nullptr )
        {
            return false;
        }
        bool any = false;
        type->ForEachField( object, [&]( const Reflection::FieldInfo& InField, void* InOwner ) {
            if( InField.Has( Reflection::FieldFlags::Replicated ) )
            {
                InVisit( InField, InField.Get( InOwner ) );
                any = true;
            }
        } );
        return any;
    }

    void WriteValue( BitWriter& InWriter, const Reflection::FieldInfo& InField, const void* InValue )
    {
        using Reflection::PropertyType;
        const float* floats = static_cast<const float*>( InValue );
        switch( InField.GetType() )
        {
        case PropertyType::Bool:
            InWriter.WriteBool( *static_cast<const bool*>( InValue ) );
            break;
        case PropertyType::Int:
        case PropertyType::UInt:
        {
            uint64_t bits = 0;
            std::memcpy( &bits, InValue, InField.Ops->IntBytes );
            InWriter.WriteVarUInt( bits );
            break;
        }
        case PropertyType::Enum:
            InWriter.WriteVarUInt( static_cast<uint64_t>( InField.Ops->GetEnum( InValue ) ) );
            break;
        case PropertyType::Float:
            InWriter.WriteFloat( floats[0] );
            break;
        case PropertyType::Double:
            InWriter.WriteBytes( InValue, sizeof( double ) );
            break;
        case PropertyType::Vector2:
        case PropertyType::Vector3:
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        {
            const int count = InField.GetType() == PropertyType::Vector2 ? 2 : InField.GetType() == PropertyType::Vector3 ? 3 : 4;
            for( int i = 0; i < count; ++i )
            {
                InWriter.WriteFloat( floats[i] );
            }
            break;
        }
        case PropertyType::String:
            InWriter.WriteString( *static_cast<const std::string*>( InValue ) );
            break;
        default:
            break;
        }
    }

    void ReadValue( BitReader& InReader, const Reflection::FieldInfo& InField, void* OutValue )
    {
        using Reflection::PropertyType;
        float* floats = static_cast<float*>( OutValue );
        switch( InField.GetType() )
        {
        case PropertyType::Bool:
            *static_cast<bool*>( OutValue ) = InReader.ReadBool();
            break;
        case PropertyType::Int:
        case PropertyType::UInt:
        {
            const uint64_t bits = InReader.ReadVarUInt();
            std::memcpy( OutValue, &bits, InField.Ops->IntBytes );
            break;
        }
        case PropertyType::Enum:
            InField.Ops->SetEnum( OutValue, static_cast<int64_t>( InReader.ReadVarUInt() ) );
            break;
        case PropertyType::Float:
            floats[0] = InReader.ReadFloat();
            break;
        case PropertyType::Double:
            InReader.ReadBytes( OutValue, sizeof( double ) );
            break;
        case PropertyType::Vector2:
        case PropertyType::Vector3:
        case PropertyType::Vector4:
        case PropertyType::Quaternion:
        {
            const int count = InField.GetType() == PropertyType::Vector2 ? 2 : InField.GetType() == PropertyType::Vector3 ? 3 : 4;
            for( int i = 0; i < count; ++i )
            {
                floats[i] = InReader.ReadFloat();
            }
            break;
        }
        case PropertyType::String:
            *static_cast<std::string*>( OutValue ) = InReader.ReadString();
            break;
        default:
            break;
        }
    }
}


void NetEntityState::SetPose( const Vector3& InPosition, const Quaternion& InRotation )
{
    for( int i = 0; i < 3; ++i )
    {
        Position[i] = NetQuantize::Quantize( InPosition[i], -kPositionRange, kPositionRange, kPositionBits );
    }
    // Smallest three, packed (the same scheme as NetQuantize::WriteQuaternion).
    float q[4] = { InRotation.x, InRotation.y, InRotation.z, InRotation.w };
    const float length = std::sqrt( q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] );
    if( !( length > 1e-12f ) )
    {
        q[0] = q[1] = q[2] = 0.f;
        q[3] = 1.f;
    }
    int largest = 0;
    for( int i = 0; i < 4; ++i )
    {
        q[i] = length > 1e-12f ? q[i] / length : q[i];
        if( std::abs( q[i] ) > std::abs( q[largest] ) )
        {
            largest = i;
        }
    }
    const float sign = q[largest] < 0.f ? -1.f : 1.f;
    uint64_t packed = static_cast<uint64_t>( largest );
    int shift = 2;
    for( int i = 0; i < 4; ++i )
    {
        if( i != largest )
        {
            packed |= static_cast<uint64_t>( NetQuantize::Quantize( q[i] * sign, -kSmallestThree, kSmallestThree, kRotationBits ) ) << shift;
            shift += kRotationBits;
        }
    }
    Rotation = packed;
}


Vector3 NetEntityState::GetPosition() const
{
    return Vector3( NetQuantize::Dequantize( Position[0], -kPositionRange, kPositionRange, kPositionBits ),
        NetQuantize::Dequantize( Position[1], -kPositionRange, kPositionRange, kPositionBits ),
        NetQuantize::Dequantize( Position[2], -kPositionRange, kPositionRange, kPositionBits ) );
}


Quaternion NetEntityState::GetRotation() const
{
    const int largest = static_cast<int>( Rotation & 3 );
    float q[4] = {};
    float sum = 0.f;
    int shift = 2;
    for( int i = 0; i < 4; ++i )
    {
        if( i != largest )
        {
            const uint32_t value = static_cast<uint32_t>( ( Rotation >> shift ) & ( ( 1u << kRotationBits ) - 1u ) );
            q[i] = NetQuantize::Dequantize( value, -kSmallestThree, kSmallestThree, kRotationBits );
            sum += q[i] * q[i];
            shift += kRotationBits;
        }
    }
    q[largest] = std::sqrt( std::max( 0.f, 1.f - sum ) );
    const float length = std::sqrt( sum + q[largest] * q[largest] );
    return Quaternion( q[0] / length, q[1] / length, q[2] / length, q[3] / length );
}


std::vector<uint8_t> NetSchema::Write( Entity& InEntity )
{
    BitWriter writer;
    for( BaseComponent* component : InEntity.GetAllComponents() )
    {
        ForEachReplicated( *component, [&writer]( const Reflection::FieldInfo& InField, void* InValue ) { WriteValue( writer, InField, InValue ); } );
    }
    return writer.GetBytes();
}


void NetSchema::Read( Entity& InEntity, const std::vector<uint8_t>& InFields )
{
    if( InFields.empty() )
    {
        return;
    }
    BitReader reader( InFields );
    for( BaseComponent* component : InEntity.GetAllComponents() )
    {
        ForEachReplicated( *component, [&reader]( const Reflection::FieldInfo& InField, void* InValue ) {
            if( !reader.IsOverflowed() )
            {
                ReadValue( reader, InField, InValue );
            }
        } );
    }
}


size_t NetSchema::CountReplicatedFields( Entity& InEntity )
{
    size_t count = 0;
    for( BaseComponent* component : InEntity.GetAllComponents() )
    {
        ForEachReplicated( *component, [&count]( const Reflection::FieldInfo&, void* ) { ++count; } );
    }
    return count;
}


void NetSnapshotCodec::Write( BitWriter& InWriter, const NetSnapshot& InCurrent, const NetSnapshot* InBaseline, size_t InByteBudget, NetSnapshot& OutSent,
    const std::set<uint32_t>* InOnly )
{
    OutSent.Tick = InCurrent.Tick;
    OutSent.Entities.clear();
    InWriter.WriteBits( InCurrent.Tick, 32 );
    InWriter.WriteBool( InBaseline != nullptr );
    if( InBaseline )
    {
        InWriter.WriteBits( InBaseline->Tick, 32 );
    }

    // Gone since the baseline (NetIds ascending, delta-coded).
    std::vector<uint32_t> gone;
    if( InBaseline )
    {
        for( const auto& [id, state] : InBaseline->Entities )
        {
            if( InCurrent.Entities.find( id ) == InCurrent.Entities.end() )
            {
                gone.push_back( id );
            }
        }
    }
    InWriter.WriteVarUInt( gone.size() );
    uint32_t previous = 0;
    for( const uint32_t id : gone )
    {
        InWriter.WriteVarUInt( id - previous );
        previous = id;
    }

    // New or changed, within the budget.
    const size_t start = InWriter.GetByteCount();
    previous = 0;
    for( const auto& [id, state] : InCurrent.Entities )
    {
        const NetEntityState* before = nullptr;
        if( InBaseline )
        {
            auto it = InBaseline->Entities.find( id );
            before = it != InBaseline->Entities.end() ? &it->second : nullptr;
        }
        if( before && *before == state )
        {
            OutSent.Entities.emplace( id, state );
            continue;
        }
        if( ( InOnly && !InOnly->count( id ) ) || InWriter.GetByteCount() - start + 16 + state.Fields.size() > InByteBudget )
        {
            // Next time: the client keeps what it had (nothing, for a new one).
            if( before )
            {
                OutSent.Entities.emplace( id, *before );
            }
            continue;
        }
        InWriter.WriteBool( true );
        InWriter.WriteVarUInt( id - previous );
        previous = id;
        const bool position = !before || before->Position != state.Position;
        const bool rotation = !before || before->Rotation != state.Rotation;
        const bool fields = !before || before->Fields != state.Fields;
        InWriter.WriteBool( position );
        if( position )
        {
            for( const uint32_t axis : state.Position )
            {
                InWriter.WriteBits( axis, NetEntityState::kPositionBits );
            }
        }
        InWriter.WriteBool( rotation );
        if( rotation )
        {
            InWriter.WriteBits( static_cast<uint32_t>( state.Rotation & 0xFFFFFFFFu ), 32 );
            InWriter.WriteBits( static_cast<uint32_t>( state.Rotation >> 32 ), 6 );
        }
        InWriter.WriteBool( fields );
        if( fields )
        {
            InWriter.WriteVarUInt( state.Fields.size() );
            InWriter.WriteBytes( state.Fields.data(), state.Fields.size() );
        }
        OutSent.Entities.emplace( id, state );
    }
    InWriter.WriteBool( false );
}


bool NetSnapshotCodec::Read( BitReader& InReader, const NetSnapshot* InBaseline, NetSnapshot& OutSnapshot )
{
    OutSnapshot.Tick = InReader.ReadBits( 32 );
    const bool hasBaseline = InReader.ReadBool();
    if( hasBaseline )
    {
        const uint32_t baselineTick = InReader.ReadBits( 32 );
        if( !InBaseline || InBaseline->Tick != baselineTick )
        {
            return false;
        }
        OutSnapshot.Entities = InBaseline->Entities;
    }
    else
    {
        OutSnapshot.Entities.clear();
    }

    const uint64_t goneCount = InReader.ReadVarUInt();
    if( goneCount > 65536 )
    {
        return false;
    }
    uint32_t previous = 0;
    for( uint64_t i = 0; i < goneCount && !InReader.IsOverflowed(); ++i )
    {
        previous += static_cast<uint32_t>( InReader.ReadVarUInt() );
        OutSnapshot.Entities.erase( previous );
    }

    previous = 0;
    while( InReader.ReadBool() )
    {
        previous += static_cast<uint32_t>( InReader.ReadVarUInt() );
        NetEntityState& state = OutSnapshot.Entities[previous];
        if( InReader.ReadBool() )
        {
            for( uint32_t& axis : state.Position )
            {
                axis = InReader.ReadBits( NetEntityState::kPositionBits );
            }
        }
        if( InReader.ReadBool() )
        {
            const uint64_t low = InReader.ReadBits( 32 );
            const uint64_t high = InReader.ReadBits( 6 );
            state.Rotation = low | ( high << 32 );
        }
        if( InReader.ReadBool() )
        {
            const uint64_t size = InReader.ReadVarUInt();
            if( size > 1024 )
            {
                return false;
            }
            state.Fields.resize( static_cast<size_t>( size ) );
            InReader.ReadBytes( state.Fields.data(), state.Fields.size() );
        }
        if( InReader.IsOverflowed() )
        {
            return false;
        }
    }
    return !InReader.IsOverflowed();
}
