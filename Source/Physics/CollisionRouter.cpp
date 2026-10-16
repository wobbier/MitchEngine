#include "Physics/CollisionRouter.h"

#include "Components/Physics/Rigidbody.h"
#include "Components/Physics/Rigidbody2D.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

struct CollisionSubscription::Registry
{
    struct Listener
    {
        uint64_t Id = 0;
        CollisionHandler Callback;
        bool Removed = false;
    };

    struct Entry
    {
        EntityHandle Owner;
        std::vector<Listener> Listeners;
    };

    // Keyed by EntityID::Value(): ids carry a generation, so a reused slot never inherits callbacks.
    std::unordered_map<uint64_t, Entry> Entries;
    uint64_t NextId = 1;
    int DispatchDepth = 0;
    bool NeedsCompaction = false;
    // Entries of destroyed entities are dropped once the map grows past this.
    size_t PruneAt = 64;

    void Remove( uint64_t InEntityKey, uint64_t InId )
    {
        auto found = Entries.find( InEntityKey );
        if( found == Entries.end() )
        {
            return;
        }
        std::vector<Listener>& listeners = found->second.Listeners;
        for( size_t i = 0; i < listeners.size(); ++i )
        {
            if( listeners[i].Id != InId )
            {
                continue;
            }
            if( DispatchDepth > 0 )
            {
                // Mid-dispatch: indices must stay put, so mark it and compact afterwards.
                listeners[i].Removed = true;
                listeners[i].Callback = nullptr;
                NeedsCompaction = true;
            }
            else
            {
                listeners.erase( listeners.begin() + i );
                if( listeners.empty() )
                {
                    Entries.erase( found );
                }
            }
            return;
        }
    }

    void Compact()
    {
        for( auto it = Entries.begin(); it != Entries.end(); )
        {
            std::vector<Listener>& listeners = it->second.Listeners;
            listeners.erase( std::remove_if( listeners.begin(), listeners.end(), []( const Listener& listener ) { return listener.Removed; } ), listeners.end() );
            it = listeners.empty() ? Entries.erase( it ) : std::next( it );
        }
        NeedsCompaction = false;
    }

    void PruneDeadEntities()
    {
        for( auto it = Entries.begin(); it != Entries.end(); )
        {
            it = it->second.Owner ? std::next( it ) : Entries.erase( it );
        }
        PruneAt = std::max<size_t>( 64, Entries.size() * 2 );
    }
};


CollisionSubscription::CollisionSubscription( const std::shared_ptr<Registry>& InRegistry, uint64_t InEntityKey, uint64_t InId )
    : m_registry( InRegistry )
    , m_entityKey( InEntityKey )
    , m_id( InId )
{
}


CollisionSubscription::~CollisionSubscription()
{
    if( std::shared_ptr<Registry> registry = m_registry.lock() )
    {
        registry->Remove( m_entityKey, m_id );
    }
}


CollisionRouter::CollisionRouter()
    : m_registry( std::make_shared<CollisionSubscription::Registry>() )
{
    Listen<CollisionEvent>( [this]( const CollisionEvent& InEvent ) { OnCollision( InEvent ); } );
}


std::unique_ptr<CollisionSubscription> CollisionRouter::Subscribe( const EntityHandle& InEntity, CollisionHandler InHandler )
{
    if( !InEntity || !InHandler )
    {
        return nullptr;
    }
    CollisionSubscription::Registry& registry = *m_registry;
    if( registry.DispatchDepth == 0 && registry.Entries.size() >= registry.PruneAt )
    {
        registry.PruneDeadEntities();
    }
    const uint64_t key = InEntity.GetID().Value();
    CollisionSubscription::Registry::Entry& entry = registry.Entries[key];
    entry.Owner = InEntity;
    const uint64_t id = registry.NextId++;
    entry.Listeners.push_back( { id, std::move( InHandler ), false } );
    return std::unique_ptr<CollisionSubscription>( new CollisionSubscription( m_registry, key, id ) );
}


int CollisionRouter::ResolveTargets( Entity& InCollider, Entity* OutTargets[2] )
{
    OutTargets[0] = &InCollider;
    OutTargets[1] = nullptr;
    if( InCollider.TryGetComponent<Rigidbody>() || InCollider.TryGetComponent<Rigidbody2D>() )
    {
        return 1;
    }
    for( Transform* parent = InCollider.GetComponent<Transform>().GetParentTransform(); parent; parent = parent->GetParentTransform() )
    {
        Entity* candidate = parent->Parent.Get();
        if( candidate && ( candidate->TryGetComponent<Rigidbody>() || candidate->TryGetComponent<Rigidbody2D>() ) )
        {
            OutTargets[1] = candidate;
            return 2;
        }
    }
    return 1;
}


void CollisionRouter::OnCollision( const CollisionEvent& InEvent )
{
    if( m_registry->Entries.empty() )
    {
        return;
    }
    // Each side hears about the other; the normal points from the receiver towards the other.
    Deliver( InEvent.A, InEvent.B, InEvent, InEvent.Normal );
    Deliver( InEvent.B, InEvent.A, InEvent, InEvent.Normal * -1.f );
}


void CollisionRouter::Deliver( const EntityHandle& InSelf, const EntityHandle& InOther, const CollisionEvent& InEvent, const Vector3& InNormal )
{
    Entity* collider = InSelf.Get();
    if( !collider )
    {
        return;
    }
    Entity* targets[2] = {};
    const int targetCount = ResolveTargets( *collider, targets );

    CollisionSubscription::Registry& registry = *m_registry;
    ++registry.DispatchDepth;
    for( int t = 0; t < targetCount; ++t )
    {
        const EntityHandle self = targets[t]->GetHandle();
        const uint64_t key = self.GetID().Value();
        auto found = registry.Entries.find( key );
        if( found == registry.Entries.end() )
        {
            continue;
        }
        Collision hit;
        hit.Self = self;
        hit.Collider = InSelf;
        hit.Other = InOther;
        hit.State = InEvent.State;
        hit.IsTrigger = InEvent.IsTrigger;
        hit.Is2D = InEvent.Is2D;
        hit.Point = InEvent.Point;
        hit.Normal = InNormal;

        // Listeners added by a callback wait for the next contact change.
        const size_t count = found->second.Listeners.size();
        for( size_t i = 0; i < count; ++i )
        {
            // Look the entry up again: a callback may have subscribed other entities (rehash).
            found = registry.Entries.find( key );
            CollisionSubscription::Registry::Listener& listener = found->second.Listeners[i];
            if( listener.Removed )
            {
                continue;
            }
            // Called through a copy, so a callback may unsubscribe itself.
            const CollisionHandler callback = listener.Callback;
            callback( hit );
        }
    }
    if( --registry.DispatchDepth == 0 && registry.NeedsCompaction )
    {
        registry.Compact();
    }
}
