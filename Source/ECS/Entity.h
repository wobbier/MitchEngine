// 2018 Mitchell Andrews
#pragma once
#include "Component.h"
#include "ClassTypeId.h"
#include <string>
#include <vector>
#include "EntityID.h"

class World;

// Lightweight view of an entity: a World pointer plus a generational id. Copies are cheap and all
// state lives in the World's entity record, so copies held by cores never go stale or diverge.
class Entity
{
    friend class World;
public:
    Entity() = default;
    Entity( World& inWorld, EntityID inId );
    Entity( const Entity& ) = default;
    Entity& operator=( const Entity& ) = default;
    bool operator==( const Entity& entity ) const;
    bool operator!=( const Entity& entity ) const {
        return !operator==( entity );
    }
    explicit operator bool() const
    {
        return IsValid();
    }

    // True while the entity this view refers to is alive.
    bool IsValid() const;

    template <typename T>
    bool HasComponent() const;

    template <typename T, typename... Args>
    T& AddComponent( Args&&... args );

    BaseComponent* AddComponentByName( const std::string& inComponent );

    // Asserts if the component is missing; use TryGetComponent when unsure.
    template <typename T>
    T& GetComponent() const;

    template <typename T>
    T* TryGetComponent() const;

    // Removal is deferred to the next World sync point (end of the current phase).
    template <typename T>
    void RemoveComponent();

    void RemoveComponent( const std::string& Name );

    BaseComponent* GetComponentByName( const std::string& Name ) const;
    std::vector<BaseComponent*> GetAllComponents() const;

    const EntityID& GetId() const;
    World* GetWorld() const { return GameWorld; }
    EntityHandle GetHandle() const;

    // Active state. The effective state also depends on the parents (see IsActiveInHierarchy) and
    // is applied to cores at the next sync point.
    void SetActive( const bool InActive );
    bool IsActiveSelf() const;
    bool IsActiveInHierarchy() const;

    // Destroys the entity and its children at the next sync point. Safe to call repeatedly.
    void MarkForDelete();

    const std::string& GetName() const;
    void SetName( const std::string& InName );

    // Persistent identity used by scenes/prefabs/entity references.
    uint64_t GetGUID() const;

    uint8_t GetLayer() const;
    void SetLayer( uint8_t InLayer );

    // While loading, AddComponent defers Init() until the components are deserialized.
    bool IsLoading() const;
    void SetLoading( bool InLoading );

    // Whether the entity is destroyed when another scene loads.
    bool GetDestroyOnLoad() const;
    void SetDestroyOnLoad( bool InDestroyOnLoad );

#if USING( ME_EDITOR )
    void OnEditorInspect();
#endif

private:
    World* GameWorld = nullptr;
    EntityID Id;

    BaseComponent* GetComponentPtr( TypeId InTypeId ) const;
    const bool HasComponent( TypeId inComponentType ) const;
    void RemoveComponent( TypeId InComponentTypeId );
};

// Template member definitions live at the end of World.h, after World is complete.
#include "Engine/World.h"
