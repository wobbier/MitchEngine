#include "PCH.h"

#include "Entity.h"
#include "Engine/World.h"

namespace
{
    // World used to resolve entity references while (de)serializing; see SerializationWorldScope.
    thread_local World* s_serializationWorld = nullptr;
}


EntityHandle::EntityHandle( EntityID InID, World* InWorld )
    : ID( InID )
    , GameWorld( InWorld )
{
}


EntityHandle::EntityHandle( EntityID InID, const WeakPtr<World>& InWorld )
    : ID( InID )
    , GameWorld( InWorld.lock().get() )
{
}


EntityHandle::EntityHandle( EntityID InID, const SharedPtr<World>& InWorld )
    : ID( InID )
    , GameWorld( InWorld.get() )
{
}


EntityHandle::operator bool() const
{
    return IsValid();
}


bool EntityHandle::IsValid() const
{
    return GameWorld && GameWorld->EntityExists( ID );
}


bool EntityHandle::operator==( const EntityHandle& other ) const
{
    return ID == other.ID && GameWorld == other.GameWorld;
}


Entity* EntityHandle::operator->() const
{
    return Get();
}


Entity* EntityHandle::Get() const
{
    return GameWorld ? GameWorld->GetEntityRaw( ID ) : nullptr;
}


EntityID EntityHandle::GetID() const
{
    return ID;
}


void EntityHandle::Reset()
{
    ID.Clear();
    GameWorld = nullptr;
}


namespace Reflection
{
    void CustomTypeTraits<EntityHandle>::ToJson( const EntityHandle& value, json& out )
    {
        Entity* entity = value.Get();
        out = entity ? json( entity->GetGUID() ) : json( 0 );
    }


    bool CustomTypeTraits<EntityHandle>::FromJson( EntityHandle& value, const json& in )
    {
        if( !in.is_number_unsigned() && !in.is_number_integer() )
        {
            return false;
        }
        const uint64_t guid = in.get<uint64_t>();
        World* world = SerializationWorldScope::GetCurrent();
        value = ( guid != 0 && world ) ? world->FindEntityByGUID( guid ) : EntityHandle();
        return true;
    }
}


SerializationWorldScope::SerializationWorldScope( World* InWorld )
    : m_previous( s_serializationWorld )
{
    s_serializationWorld = InWorld;
}


SerializationWorldScope::~SerializationWorldScope()
{
    s_serializationWorld = m_previous;
}


World* SerializationWorldScope::GetCurrent()
{
    return s_serializationWorld;
}
