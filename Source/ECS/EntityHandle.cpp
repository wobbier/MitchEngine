#include "PCH.h"

#include "Entity.h"
#include "Engine/World.h"

#include "World/SceneSerializer.h"

namespace
{
    // World used to resolve entity references while (de)serializing; see SerializationWorldScope.
    thread_local World* s_serializationWorld = nullptr;
    thread_local const std::unordered_map<uint64_t, uint64_t>* s_serializationRemap = nullptr;
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
        out = entity ? json( SceneSerializer::GUIDToString( entity->GetGUID() ) ) : json( nullptr );
    }


    bool CustomTypeTraits<EntityHandle>::FromJson( EntityHandle& value, const json& in )
    {
        if( in.is_null() )
        {
            value = EntityHandle();
            return true;
        }
        if( !in.is_string() && !in.is_number_unsigned() && !in.is_number_integer() )
        {
            return false;
        }
        value = SerializationWorldScope::Resolve( SceneSerializer::GUIDFromJson( in ) );
        return true;
    }
}


SerializationWorldScope::SerializationWorldScope( World* InWorld, const std::unordered_map<uint64_t, uint64_t>* InRemap )
    : m_previousWorld( s_serializationWorld )
    , m_previousRemap( s_serializationRemap )
{
    s_serializationWorld = InWorld;
    s_serializationRemap = InRemap;
}


SerializationWorldScope::~SerializationWorldScope()
{
    s_serializationWorld = m_previousWorld;
    s_serializationRemap = m_previousRemap;
}


EntityHandle SerializationWorldScope::Resolve( uint64_t InSavedGUID )
{
    if( InSavedGUID == 0 || !s_serializationWorld )
    {
        return {};
    }
    if( s_serializationRemap )
    {
        auto it = s_serializationRemap->find( InSavedGUID );
        if( it != s_serializationRemap->end() )
        {
            InSavedGUID = it->second;
        }
    }
    return s_serializationWorld->FindEntityByGUID( InSavedGUID );
}


World* SerializationWorldScope::GetCurrent()
{
    return s_serializationWorld;
}
