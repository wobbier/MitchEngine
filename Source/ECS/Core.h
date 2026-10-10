#pragma once
#include "Entity.h"
#include "ClassTypeId.h"
#include "ComponentFilter.h"
#include <vector>
#include "CoreDetail.h"
#include "Core/UpdateContext.h"

class World;
class DebugDrawer;

#define ME_REGISTER_CORE(TYPE)                      \
	namespace details {                                  \
    namespace                                            \
    {                                                    \
        template<class T>                                \
        class CoreRegistration;                     \
                                                         \
        template<>                                       \
        class CoreRegistration<TYPE>                \
        {                                                \
            static const CoreRegistryEntry<TYPE>& reg;       \
        };                                               \
                                                         \
        const CoreRegistryEntry<TYPE>&                       \
            CoreRegistration<TYPE>::reg =           \
                CoreRegistryEntry<TYPE>::Instance(#TYPE);    \
    }}

class BaseCore
{
    friend class World;
public:
    BaseCore() = default;
    BaseCore( const char* CompName, const ComponentFilter& Filter );
    virtual ~BaseCore() {};

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) {};
    // Runs zero or more times per frame at the fixed simulation rate (physics, deterministic logic).
    virtual void FixedUpdate( const UpdateContext& inUpdateContext ) {};
    virtual void LateUpdate( const UpdateContext& inUpdateContext ) {};
    virtual void PostRender( const UpdateContext& inUpdateContext ) {};
    virtual void OnEntityAdded( Entity& NewEntity ) {};
    virtual void OnEntityRemoved( Entity& InEntity ) {};
    virtual void OnEntityDestroyed( Entity& InEntity ) {};
    virtual void OnDrawGuizmo( DebugDrawer* ) {};
    virtual void OnAddedToWorld() {};
    virtual void OnRemovedFromWorld() {};

    // Get The World attached to the Core
    World& GetWorld() const;

    // Get All the entities that are within the Core
    const std::vector<Entity>& GetEntities() const;

    // Get All the entities that are within the Core
    std::vector<Entity>& GetEntities();

    // Get the component filter associated with the core.
    const ComponentFilter& GetComponentFilter() const;

    // O(1): is the entity with this slot index currently matched by this core?
    bool Contains( uint32_t InEntityIndex ) const;

    // Cores update in ascending priority order (ties broken by name), so ordering is deterministic;
    // RunsAfter / RunsBefore constraints override it.
    int GetPriority() const { return Priority; }
    const std::vector<std::string>& GetRunsAfter() const { return m_runsAfter; }
    const std::vector<std::string>& GetRunsBefore() const { return m_runsBefore; }

    // The name a core type is known by (GetName, scene "Cores" lists): the class name without its
    // namespace.
    static std::string CleanCoreName( const char* InRawTypeName );

    // TypeId the World registered this core under.
    TypeId GetTypeIdInternal() const { return m_coreTypeId; }

    const std::string& GetName() const;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect();
    virtual void Serialize( json& outJson ) = 0;
#endif
    virtual void Deserialize( const json& inJson ) = 0;
    const bool GetIsSerializable() const;

protected:
    void SetIsSerializable( bool value );

    class Engine* GameEngine = nullptr;
    World* GameWorld = nullptr;
    bool DestroyOnLoad = true;
    int Priority = 0;

    // Update order constraints between the world's cores, in every phase (FixedUpdate, Update,
    // LateUpdate): this core runs after / before the other one whatever their priorities. Cores
    // that aren't in the world are ignored; constraints that form a cycle fall back to priority
    // order there (with a warning). Engine-owned cores (physics, animation, render) run at fixed
    // points of the frame instead: use LateUpdate to run after animation.
    //   CombatCore() : Base( ... ) { RunsAfter<MovementCore>(); RunsBefore( "HudCore" ); }
    template<typename T>
    void RunsAfter()
    {
        m_runsAfter.push_back( CleanCoreName( typeid( T ).name() ) );
    }
    template<typename T>
    void RunsBefore()
    {
        m_runsBefore.push_back( CleanCoreName( typeid( T ).name() ) );
    }
    void RunsAfter( const std::string& InCoreName )
    {
        m_runsAfter.push_back( InCoreName );
    }
    void RunsBefore( const std::string& InCoreName )
    {
        m_runsBefore.push_back( InCoreName );
    }

private:
    // Separate init from construction code.
    virtual void Init() {};

    // Separate init from construction code.
    virtual void OnStart() {};
    virtual void OnStop() {};

    // Add an entity to the core
    void Add( Entity& InEntity );

    void Remove( Entity& InEntity );

    void Clear();

    // The Entities that are attached to this system
    std::vector<Entity> Entities;
    // Entity slot index -> position in Entities + 1 (0 = not a member).
    std::vector<uint32_t> m_memberSlots;
    std::vector<std::string> m_runsAfter;
    std::vector<std::string> m_runsBefore;

    // The World attached to the system

    ComponentFilter CompFilter;

    std::string Name;

    bool IsRunning = false;
    bool IsSerializable = true;
    // Created by the World from a scene/registry (as opposed to owned by the engine/editor).
    bool IsOwnedByWorld = false;
    TypeId m_coreTypeId = 0;
};

// Use the CRTP patten to define custom systems
template<typename T>
class Core
    : public BaseCore
{
public:
    typedef Core<T> Base;

    Core() = default;

    Core( const ComponentFilter& InComponentFilter ) : BaseCore( typeid( T ).name(), InComponentFilter )
    {
    }

    static TypeId GetTypeId()
    {
        return ClassTypeId<BaseCore>::GetTypeId<T>();
    }
    virtual void OnEntityAdded( Entity& NewEntity ) override
    {
    }

    virtual void OnEntityRemoved( Entity& InEntity ) override
    {
    }

    virtual void OnEntityDestroyed( Entity& InEntity ) override
    {
    }

    virtual void OnDeserialize( const json& inJson )
    {
    }

#if USING( ME_EDITOR )
    virtual void OnSerialize( json& outJson )
    {
    }
    virtual void OnEditorInspect() override;
    virtual void Serialize( json& outJson ) final {
        outJson["Type"] = GetName();
        OnSerialize( outJson );
    }
#endif
    virtual void Deserialize( const json& inJson ) final {
        OnDeserialize( inJson );
    }
};

#if USING( ME_EDITOR )

template<typename T>
void Core<T>::OnEditorInspect()
{
    BaseCore::OnEditorInspect();
}

#endif

#ifndef ME_ALIAS_CONCAT
#define ME_ALIAS_CONCAT_INNER( A, B ) A##B
#define ME_ALIAS_CONCAT( A, B ) ME_ALIAS_CONCAT_INNER( A, B )
#endif

// A core type was renamed: scenes that still list FORMER_NAME get TYPE.
//   ME_REGISTER_CORE_ALIAS( CombatCore, "DamageCore" )
#define ME_REGISTER_CORE_ALIAS( TYPE, FORMER_NAME ) \
    namespace details { namespace { \
        const CoreAlias ME_ALIAS_CONCAT( s_coreAlias, __LINE__ )( FORMER_NAME, #TYPE ); \
    }}
