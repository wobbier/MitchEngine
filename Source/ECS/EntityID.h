#pragma once
#include <cstdint>
#include <functional>

// 64-bit generational entity id: a slot index plus the generation the slot had when the entity was
// created. Destroying an entity bumps its slot's generation, so stale ids/handles never resolve to a
// newer entity reusing the slot. Generation 0 is reserved for "null".
//
// Layout is mirrored by ScriptCore/Source/Core/Entity.cs - keep them in sync.
struct EntityID
{
    typedef std::uint64_t IntType;

    std::uint32_t Index = 0;
    std::uint32_t Generation = 0;

    EntityID() = default;
    EntityID( std::uint32_t inIndex, std::uint32_t inGeneration ) : Index( inIndex ), Generation( inGeneration ) {}

    static EntityID FromValue( IntType value )
    {
        return EntityID( static_cast<std::uint32_t>( value & 0xFFFFFFFFull ), static_cast<std::uint32_t>( value >> 32 ) );
    }

    inline operator IntType() const
    {
        return Value();
    }

    bool operator==( const EntityID& other ) const
    {
        return Index == other.Index && Generation == other.Generation;
    }

    bool operator!=( const EntityID& other ) const
    {
        return !( *this == other );
    }

    inline IntType Value() const
    {
        return ( static_cast<IntType>( Generation ) << 32 ) | static_cast<IntType>( Index );
    }

    void Clear()
    {
        Index = 0;
        Generation = 0;
    }

    bool IsNull() const
    {
        return Generation == 0;
    }
};

static_assert( sizeof( EntityID ) == 8, "EntityID is passed by value to managed code; keep it 8 bytes." );

namespace std
{
    template<>
    struct hash<EntityID>
    {
        std::size_t operator()( const EntityID& eid ) const noexcept
        {
            return std::hash<uint64_t>()( eid.Value() );
        }
    };
}
