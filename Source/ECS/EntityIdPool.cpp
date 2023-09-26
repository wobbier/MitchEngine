#include "PCH.h"
#include "EntityIdPool.h"

EntityIdPool::EntityIdPool( std::size_t InPoolSize ) :
    DefaultPoolSize( InPoolSize ),
    NextId( 0 ),
    Entities( InPoolSize )
{
}

EntityID EntityIdPool::Create()
{
    EntityID Id;

    if( !FreeList.empty() )
    {
        Id = FreeList.back();
        FreeList.pop_back();
    }
    else
    {
        Id.Index = NextId++;
        Entities[Id.Index] = Id.Counter = 1;
    }
    return Id;
}

std::size_t EntityIdPool::GetSize() const
{
    return Entities.size();
}

void EntityIdPool::Resize( std::size_t InAmount )
{
    Entities.resize( InAmount );
}

void EntityIdPool::Reset()
{
    Entities.clear();
    NextId = 0;
    FreeList.clear();
}

void EntityIdPool::Remove( EntityID InEntityId )
{
    auto& Counter = Entities[InEntityId.Index];
    ++Counter;
    FreeList.emplace_back( InEntityId.Index, Counter );
}