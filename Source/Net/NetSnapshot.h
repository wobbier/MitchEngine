#pragma once
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

class BaseComponent;
class BitReader;
class BitWriter;
class Entity;

// One networked entity's state in a snapshot (NET-3), kept in its quantized wire form so a delta
// is an exact comparison: world position at 1/1024 m (within +/-4096 m), rotation as smallest
// three at 12 bits, and the Replicated fields of its components (NetSchema) as bytes.
struct NetEntityState
{
    std::array<uint32_t, 3> Position{};
    uint64_t Rotation = 0;              // 2-bit index + 3 x 12 bits
    std::vector<uint8_t> Fields;        // NetSchema::Write of every replicated component

    bool operator==( const NetEntityState& InOther ) const
    {
        return Position == InOther.Position && Rotation == InOther.Rotation && Fields == InOther.Fields;
    }
    bool operator!=( const NetEntityState& InOther ) const { return !( *this == InOther ); }

    void SetPose( const Vector3& InPosition, const Quaternion& InRotation );
    Vector3 GetPosition() const;
    Quaternion GetRotation() const;

    static constexpr float kPositionRange = 4096.f;
    static constexpr int kPositionBits = 23;    // 8192 m / 2^23 = 1/1024 m
    static constexpr int kRotationBits = 12;
};


// Every networked entity's state at a server tick, by NetId.
struct NetSnapshot
{
    uint32_t Tick = 0;
    std::map<uint32_t, NetEntityState> Entities;
};


// The components' Replicated fields (FieldFlags::Replicated, from C++ reflection), in component
// order. Supported field types: bool, ints, enums, float, double, Vector2/3/4, Quaternion and
// strings; others are skipped.
namespace NetSchema
{
    // Each replicated component's fields as bits.
    std::vector<uint8_t> Write( Entity& InEntity );
    // Applies what Write produced (to the same components, in the same order).
    void Read( Entity& InEntity, const std::vector<uint8_t>& InFields );
    // How many fields of the entity's components replicate.
    size_t CountReplicatedFields( Entity& InEntity );
}


namespace NetSnapshotCodec
{
    // Quake 3 style: InCurrent against InBaseline (null: everything). Entities absent from the
    // baseline or changed are written whole-part (pose and fields each flagged), and entities in
    // the baseline but not in InCurrent are listed as gone. Stops writing changed entities once
    // InByteBudget is spent; OutSent then holds what the client will actually have (unsent
    // entities keep their baseline state), which is what to delta against later.
    void Write( BitWriter& InWriter, const NetSnapshot& InCurrent, const NetSnapshot* InBaseline, size_t InByteBudget, NetSnapshot& OutSent );
    // The baseline must be the one the writer used (its tick is in the message).
    bool Read( BitReader& InReader, const NetSnapshot* InBaseline, NetSnapshot& OutSnapshot );
}
