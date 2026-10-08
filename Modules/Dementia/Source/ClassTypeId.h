#pragma once
#include <atomic>
#include <cstddef>

typedef std::size_t TypeId;

// Dense, per-family type ids (0, 1, 2...) assigned on first request. Thread safe.
// Ids are only stable within one run; serialize types by name, never by TypeId.
template<typename TBase>
class ClassTypeId
{
public:
    template<typename T>
    static TypeId GetTypeId()
    {
        static const TypeId Id = NextTypeId.fetch_add( 1, std::memory_order_relaxed );
        return Id;
    }

    static TypeId GetTypeCount()
    {
        return NextTypeId.load( std::memory_order_relaxed );
    }

private:
    static std::atomic<TypeId> NextTypeId;
};

template<typename TBase>
std::atomic<TypeId> ClassTypeId<TBase>::NextTypeId{ 0 };
