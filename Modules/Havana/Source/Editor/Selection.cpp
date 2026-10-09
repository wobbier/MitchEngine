#include "Selection.h"

#if USING( ME_EDITOR )

#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/EditorEvents.h"
#include "Events/HavanaEvents.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_set>


Selection& Selection::Get()
{
    static Selection instance;
    return instance;
}


Selection::Selection()
{
    EventManager::GetInstance().RegisterReceiver( this, { InspectEvent::GetEventId(), ClearInspectEvent::GetEventId(), PickingEvent::GetEventId() } );
}


void Selection::Prune() const
{
    m_entities.erase( std::remove_if( m_entities.begin(), m_entities.end(), []( const EntityHandle& handle ) { return !handle.IsValid(); } ), m_entities.end() );
}


void Selection::Changed()
{
    ++m_version;
    if( m_isNotifying )
    {
        return;
    }
    m_isNotifying = true;
    SelectionChangedEvent evt;
    evt.Fire();
    m_isNotifying = false;
}


void Selection::Set( const EntityHandle& InEntity )
{
    m_entities.clear();
    m_core = nullptr;
    if( InEntity )
    {
        m_entities.push_back( InEntity );
    }
    Changed();
}


void Selection::Set( const std::vector<EntityHandle>& InEntities )
{
    m_entities.clear();
    m_core = nullptr;
    for( const EntityHandle& entity : InEntities )
    {
        if( entity && std::find( m_entities.begin(), m_entities.end(), entity ) == m_entities.end() )
        {
            m_entities.push_back( entity );
        }
    }
    Changed();
}


void Selection::Add( const EntityHandle& InEntity )
{
    if( !InEntity )
    {
        return;
    }
    m_core = nullptr;
    // Re-adding moves it to the end so it becomes the active entity.
    m_entities.erase( std::remove( m_entities.begin(), m_entities.end(), InEntity ), m_entities.end() );
    m_entities.push_back( InEntity );
    Changed();
}


void Selection::Remove( const EntityHandle& InEntity )
{
    const size_t before = m_entities.size();
    m_entities.erase( std::remove( m_entities.begin(), m_entities.end(), InEntity ), m_entities.end() );
    if( before != m_entities.size() )
    {
        Changed();
    }
}


void Selection::Toggle( const EntityHandle& InEntity )
{
    if( Contains( InEntity ) )
    {
        Remove( InEntity );
    }
    else
    {
        Add( InEntity );
    }
}


void Selection::SetCore( BaseCore* InCore )
{
    m_entities.clear();
    m_core = InCore;
    Changed();
}


void Selection::Clear()
{
    if( m_entities.empty() && !m_core )
    {
        return;
    }
    m_entities.clear();
    m_core = nullptr;
    Changed();
}


bool Selection::Contains( const EntityHandle& InEntity ) const
{
    return std::find( m_entities.begin(), m_entities.end(), InEntity ) != m_entities.end();
}


bool Selection::IsEmpty() const
{
    Prune();
    return m_entities.empty() && !m_core;
}


size_t Selection::Count() const
{
    Prune();
    return m_entities.size();
}


EntityHandle Selection::GetActive() const
{
    Prune();
    return m_entities.empty() ? EntityHandle() : m_entities.back();
}


Transform* Selection::GetActiveTransform() const
{
    EntityHandle active = GetActive();
    return active ? active->TryGetComponent<Transform>() : nullptr;
}


std::vector<EntityHandle> Selection::GetEntities() const
{
    Prune();
    return m_entities;
}


std::vector<Entity*> Selection::GetRootEntities() const
{
    Prune();
    std::unordered_set<const Transform*> selectedTransforms;
    for( const EntityHandle& handle : m_entities )
    {
        if( Transform* transform = handle->TryGetComponent<Transform>() )
        {
            selectedTransforms.insert( transform );
        }
    }

    std::vector<Entity*> roots;
    for( const EntityHandle& handle : m_entities )
    {
        Transform* transform = handle->TryGetComponent<Transform>();
        bool hasSelectedAncestor = false;
        for( Transform* parent = transform ? transform->GetParentTransform() : nullptr; parent; parent = parent->GetParentTransform() )
        {
            if( selectedTransforms.count( parent ) )
            {
                hasSelectedAncestor = true;
                break;
            }
        }
        if( !hasSelectedAncestor )
        {
            roots.push_back( handle.Get() );
        }
    }
    return roots;
}


std::vector<uint64_t> Selection::SaveGUIDs() const
{
    Prune();
    std::vector<uint64_t> guids;
    for( const EntityHandle& handle : m_entities )
    {
        guids.push_back( handle->GetGUID() );
    }
    return guids;
}


void Selection::RestoreGUIDs( World& InWorld, const std::vector<uint64_t>& InGUIDs )
{
    std::vector<EntityHandle> entities;
    for( uint64_t guid : InGUIDs )
    {
        if( EntityHandle handle = InWorld.FindEntityByGUID( guid ) )
        {
            entities.push_back( handle );
        }
    }
    Set( entities );
}


bool Selection::OnEvent( const BaseEvent& evt )
{
    if( m_isNotifying )
    {
        return false;
    }
    if( evt.GetEventId() == InspectEvent::GetEventId() )
    {
        const InspectEvent& inspect = static_cast<const InspectEvent&>( evt );
        if( inspect.SelectedCore )
        {
            SetCore( inspect.SelectedCore );
        }
        else if( inspect.SelectedEntity )
        {
            Set( inspect.SelectedEntity );
        }
        else if( Transform* transform = inspect.SelectedTransform.Get() )
        {
            Set( transform->Parent );
        }
    }
    else if( evt.GetEventId() == ClearInspectEvent::GetEventId() )
    {
        Clear();
    }
    else if( evt.GetEventId() == PickingEvent::GetEventId() )
    {
        // Scene view click: Ctrl toggles, otherwise replaces.
        const PickingEvent& picking = static_cast<const PickingEvent&>( evt );
        EntityHandle picked = GetEngine().GetWorld().lock()->FindEntityByIDValue( picking.RawEntityID );
        if( ImGui::GetIO().KeyCtrl )
        {
            Toggle( picked );
        }
        else
        {
            Set( picked );
        }
    }
    return false;
}

#endif
