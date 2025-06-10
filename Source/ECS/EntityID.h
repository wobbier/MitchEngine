#pragma once
#include <cstdint>
#include <functional>

// 64 Bit IDs
#define MITCH_ENTITY_ID_INDEX_BIT_COUNT 48
#define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 16

struct EntityID
{
    typedef std::uint64_t IntType;

    IntType Index { MITCH_ENTITY_ID_INDEX_BIT_COUNT };
    IntType Counter { MITCH_ENTITY_ID_COUNTER_BIT_COUNT };

    EntityID() : Index( 0 ), Counter( 0 ) {};
    EntityID( IntType inIndex, IntType inCounter ) : Index( inIndex ), Counter( inCounter ) {};
    EntityID& operator=( const EntityID& ) = default;

    inline operator IntType() const
    {
        return Value();
    }

    bool operator==( const EntityID& other ) const
    {
        return ( Value() == other.Value() );
    }

    inline IntType Value() const
    {
        return ( Counter << MITCH_ENTITY_ID_COUNTER_BIT_COUNT ) | Index;
    }

    void Clear()
    {
        Index = Counter = 0;
    }

    bool IsNull() const
    {
        return Value() == 0;
    }
};

namespace std
{
    template<>
    struct hash<EntityID>
    {
        std::size_t operator()( const EntityID& eid ) const noexcept
        {
            return std::hash<uint32_t>()( eid.Value() );
        }
    };
}
