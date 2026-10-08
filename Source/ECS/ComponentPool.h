#pragma once
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "ClassTypeId.h"
#include "Core/Assert.h"

class BaseComponent;

// Type-erased view of a ComponentPool<T>.
class IComponentPool
{
public:
    virtual ~IComponentPool() = default;

    virtual BaseComponent* Get( uint32_t entityIndex ) const = 0;
    virtual void Destroy( uint32_t entityIndex ) = 0;
    virtual TypeId GetTypeId() const = 0;

    size_t Count() const { return m_dense.size(); }

    // Dense list of (entity index, component) pairs in no particular order. Stable while no
    // components of this type are added or removed.
    struct Entry
    {
        uint32_t EntityIndex;
        BaseComponent* Component;
    };
    const std::vector<Entry>& GetEntries() const { return m_dense; }

protected:
    std::vector<Entry> m_dense;
};

// Stores all components of one type in fixed-size pages. Addresses are stable for the lifetime of a
// component (pages never move and freed slots are only reused by new components), so systems may
// hold raw pointers between frames. Per-component heap allocations and shared_ptr ref counting are
// gone, and same-type components sit next to each other in memory.
template<typename T>
class ComponentPool final
    : public IComponentPool
{
    static constexpr uint32_t kPageSize = 256;
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    struct Page
    {
        alignas( T ) unsigned char Storage[sizeof( T ) * kPageSize];
    };

public:
    ComponentPool() = default;
    ComponentPool( const ComponentPool& ) = delete;
    ComponentPool& operator=( const ComponentPool& ) = delete;

    ~ComponentPool() override
    {
        // Destroy anything still alive (World normally destroys components explicitly first).
        while( !m_dense.empty() )
        {
            Destroy( m_dense.back().EntityIndex );
        }
    }

    template<typename... Args>
    T& Emplace( uint32_t entityIndex, Args&&... args )
    {
        ME_ASSERT_MSG( Find( entityIndex ) == nullptr, "Entity already has this component." );

        uint32_t slot;
        if( !m_freeSlots.empty() )
        {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
        }
        else
        {
            slot = m_nextSlot++;
            if( slot / kPageSize >= m_pages.size() )
            {
                m_pages.emplace_back( std::make_unique<Page>() );
            }
        }

        T* component = new( SlotAddress( slot ) ) T( std::forward<Args>( args )... );

        if( entityIndex >= m_sparse.size() )
        {
            m_sparse.resize( entityIndex + 1, kNone );
        }
        m_sparse[entityIndex] = static_cast<uint32_t>( m_dense.size() );
        m_dense.push_back( { entityIndex, component } );
        m_slots.push_back( slot );
        return *component;
    }

    T* Find( uint32_t entityIndex ) const
    {
        if( entityIndex >= m_sparse.size() || m_sparse[entityIndex] == kNone )
        {
            return nullptr;
        }
        return static_cast<T*>( m_dense[m_sparse[entityIndex]].Component );
    }

    BaseComponent* Get( uint32_t entityIndex ) const override
    {
        return Find( entityIndex );
    }

    void Destroy( uint32_t entityIndex ) override
    {
        if( entityIndex >= m_sparse.size() || m_sparse[entityIndex] == kNone )
        {
            return;
        }

        const uint32_t denseIndex = m_sparse[entityIndex];
        T* component = static_cast<T*>( m_dense[denseIndex].Component );
        const uint32_t slot = m_slots[denseIndex];

        // Swap-remove from the dense arrays.
        const uint32_t last = static_cast<uint32_t>( m_dense.size() - 1 );
        if( denseIndex != last )
        {
            m_dense[denseIndex] = m_dense[last];
            m_slots[denseIndex] = m_slots[last];
            m_sparse[m_dense[denseIndex].EntityIndex] = denseIndex;
        }
        m_dense.pop_back();
        m_slots.pop_back();
        m_sparse[entityIndex] = kNone;

        component->~T();
        m_freeSlots.push_back( slot );
    }

    TypeId GetTypeId() const override
    {
        return ClassTypeId<BaseComponent>::GetTypeId<T>();
    }

    template<typename Fn>
    void ForEach( Fn&& fn ) const
    {
        for( const Entry& entry : m_dense )
        {
            fn( entry.EntityIndex, *static_cast<T*>( entry.Component ) );
        }
    }

private:
    void* SlotAddress( uint32_t slot )
    {
        return m_pages[slot / kPageSize]->Storage + sizeof( T ) * ( slot % kPageSize );
    }

    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<uint32_t> m_freeSlots;
    std::vector<uint32_t> m_sparse;   // entity index -> dense index
    std::vector<uint32_t> m_slots;    // dense index -> storage slot
    uint32_t m_nextSlot = 0;
};
