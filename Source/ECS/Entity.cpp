#include "PCH.h"
#include "Entity.h"
#include "Engine/World.h"
#include "ComponentDetail.h"
#include "CLog.h"

#if USING( ME_EDITOR )
#include "imgui.h"
#endif
#include "Utils/HavanaUtils.h"


Entity::Entity( World& inWorld, EntityID inId )
    : GameWorld( &inWorld )
    , Id( inId )
{
}


bool Entity::IsValid() const
{
    return GameWorld && GameWorld->EntityExists( Id );
}


const bool Entity::HasComponent( TypeId inComponentType ) const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record && inComponentType < kMaxComponentTypes && record->Mask.test( inComponentType );
}


BaseComponent* Entity::AddComponentByName( const std::string& inComponent )
{
    ComponentRegistry& reg = GetComponentRegistry();
    ComponentRegistry::iterator it = reg.find( inComponent );

    if( it == reg.end() ) {
        CLog::GetInstance().Log( CLog::LogType::Warning, "Factory not found for component " + inComponent );
        return nullptr;
    }

    return it->second.CreateFunc( *this );
}


const EntityID& Entity::GetId() const
{
    return Id;
}


EntityHandle Entity::GetHandle() const
{
    return EntityHandle( Id, GameWorld );
}


BaseComponent* Entity::GetComponentPtr( TypeId InTypeId ) const
{
    return GameWorld ? GameWorld->GetComponent( Id, InTypeId ) : nullptr;
}


BaseComponent* Entity::GetComponentByName( const std::string& Name ) const
{
    ComponentRegistry& reg = GetComponentRegistry();
    ComponentRegistry::iterator it = reg.find( Name );
    if( it == reg.end() )
    {
        return nullptr;
    }
    return GetComponentPtr( it->second.GetTypeFunc() );
}


void Entity::SetActive( const bool InActive )
{
    World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    if( !record )
    {
        return;
    }
    record->ActiveSelf = InActive;
    GameWorld->MarkEntityDirty( Id );
}


bool Entity::IsActiveSelf() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record && record->ActiveSelf;
}


bool Entity::IsActiveInHierarchy() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record && record->ActiveInHierarchy;
}


void Entity::MarkForDelete()
{
    if( GameWorld )
    {
        GameWorld->MarkEntityForDelete( *this );
    }
}


const std::string& Entity::GetName() const
{
    static const std::string kEmpty;
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record ? record->Name : kEmpty;
}


void Entity::SetName( const std::string& InName )
{
    if( World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr )
    {
        record->Name = InName;
    }
}


uint64_t Entity::GetGUID() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record ? record->GUID : 0;
}


uint8_t Entity::GetLayer() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record ? record->Layer : 0;
}


void Entity::SetLayer( uint8_t InLayer )
{
    if( World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr )
    {
        record->Layer = InLayer < 32 ? InLayer : 31;
    }
}


bool Entity::IsLoading() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return record && record->IsLoading;
}


void Entity::SetLoading( bool InLoading )
{
    if( World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr )
    {
        record->IsLoading = InLoading;
        if( !InLoading )
        {
            GameWorld->MarkEntityDirty( Id );
        }
    }
}


bool Entity::GetDestroyOnLoad() const
{
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    return !record || record->DestroyOnLoad;
}


void Entity::SetDestroyOnLoad( bool InDestroyOnLoad )
{
    if( World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr )
    {
        record->DestroyOnLoad = InDestroyOnLoad;
    }
}

#if USING( ME_EDITOR )

void Entity::OnEditorInspect()
{
    World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    if( !record )
    {
        return;
    }
    HavanaUtils::Label( "Destroy On Load" );
    ImGui::Checkbox( "##DOL", &record->DestroyOnLoad );
    HavanaUtils::Label( "GUID" );
    ImGui::Text( "%016llx", static_cast<unsigned long long>( record->GUID ) );
}

#endif

bool Entity::operator==( const Entity& entity ) const
{
    return Id == entity.Id && entity.GameWorld == GameWorld;
}


void Entity::RemoveComponent( TypeId InComponentTypeId )
{
    if( GameWorld )
    {
        GameWorld->RemoveComponent( Id, InComponentTypeId );
    }
}


void Entity::RemoveComponent( const std::string& Name )
{
    ComponentRegistry& reg = GetComponentRegistry();
    ComponentRegistry::iterator it = reg.find( Name );

    if( it == reg.end() )
    {
        BRUH( "Factory not found for component " + Name );
        return;
    }

    RemoveComponent( it->second.GetTypeFunc() );
}


std::vector<BaseComponent*> Entity::GetAllComponents() const
{
    std::vector<BaseComponent*> components;
    const World::EntityRecord* record = GameWorld ? GameWorld->GetRecord( Id ) : nullptr;
    if( !record )
    {
        return components;
    }
    for( std::size_t typeId = 0; typeId < kMaxComponentTypes; ++typeId )
    {
        if( record->Mask.test( typeId ) )
        {
            if( BaseComponent* component = GameWorld->GetComponent( Id, typeId ) )
            {
                components.push_back( component );
            }
        }
    }
    return components;
}
