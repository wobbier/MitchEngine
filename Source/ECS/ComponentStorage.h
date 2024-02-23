#pragma once
#include <vector>
#include <memory>

#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "ECS/ComponentTypeArray.h"

#include "Dementia.h"

class ComponentStorage
{
public:
    ComponentStorage( std::size_t InEntityAmount );
    ~ComponentStorage();

    ME_HARDSTUCK( ComponentStorage )

        void AddComponent( Entity& InEntity, SharedPtr<BaseComponent> InComponent, TypeId InComponentTypeId );

    BaseComponent& GetComponent( const Entity& InEntity, TypeId InTypeId );

    const ComponentTypeArray& GetComponentTypes( const Entity& InEntity ) const;

    void RemoveComponent( const Entity& InEntity, TypeId InTypeId );

    void RemoveAllComponents( Entity& InEntity );

    std::vector<BaseComponent*> GetAllComponents( const Entity& InEntity );

    void Resize( std::size_t InAmount );

    void Reset();
private:
    typedef std::vector<std::shared_ptr<BaseComponent>> ImplComponentArray;

    struct EntityComponents
    {
        EntityComponents() = default;

        EntityComponents( EntityComponents&& E ) :
            Components( std::move( E.Components ) ),
            ComponentTypeList( std::move( E.ComponentTypeList ) )
        {
        }
        ImplComponentArray Components;

        ComponentTypeArray ComponentTypeList;
    };

    std::vector<EntityComponents> ComponentEntries;

    inline ImplComponentArray& GetComponents_Implementation( const Entity& E );

    inline const ImplComponentArray& GetComponents_Implementation( const Entity& E ) const;
};
