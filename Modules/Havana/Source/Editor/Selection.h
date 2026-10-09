#pragma once
#include "ECS/EntityHandle.h"
#include "Events/EventReceiver.h"
#include <cstdint>
#include <vector>

#if USING( ME_EDITOR )

class BaseCore;
class Transform;
class Entity;
class World;

// Fired after the editor selection changes (any widget may react).
class SelectionChangedEvent
    : public Event<SelectionChangedEvent>
{
public:
    SelectionChangedEvent() : Event() {}
};

// The editor's single source of truth for what is selected: any number of entities (one of them
// "active", the one the inspector shows) or one core. Widgets query it every frame.
// Legacy InspectEvent / ClearInspectEvent / PickingEvent are accepted as selection requests.
class Selection
    : public EventReceiver
{
public:
    static Selection& Get();

    void Set( const EntityHandle& InEntity );
    void Set( const std::vector<EntityHandle>& InEntities );
    void Add( const EntityHandle& InEntity );
    void Remove( const EntityHandle& InEntity );
    void Toggle( const EntityHandle& InEntity );
    void SetCore( BaseCore* InCore );
    void Clear();

    bool Contains( const EntityHandle& InEntity ) const;
    bool IsEmpty() const;
    size_t Count() const;

    // The most recently selected live entity (null handle when none).
    EntityHandle GetActive() const;
    Transform* GetActiveTransform() const;
    BaseCore* GetCore() const { return m_core; }

    // Live selected entities, in selection order.
    std::vector<EntityHandle> GetEntities() const;
    // Selected entities without a selected ancestor: what delete/duplicate/move operate on.
    std::vector<Entity*> GetRootEntities() const;

    // Increments on every change; cheap change detection for widgets.
    uint64_t GetVersion() const { return m_version; }

    // Play-mode / undo friendly persistence.
    std::vector<uint64_t> SaveGUIDs() const;
    void RestoreGUIDs( World& InWorld, const std::vector<uint64_t>& InGUIDs );

    bool OnEvent( const BaseEvent& evt ) override;

    // What a scene-view click on InPicked should select: the outermost model/prefab instance root
    // first, then one level deeper per click while InCurrent is already on that branch.
    static EntityHandle ResolvePickTarget( const EntityHandle& InPicked, const EntityHandle& InCurrent );

private:
    Selection();
    void Prune() const;
    void Changed();

    mutable std::vector<EntityHandle> m_entities;
    BaseCore* m_core = nullptr;
    uint64_t m_version = 0;
    bool m_isNotifying = false;
};

#endif
