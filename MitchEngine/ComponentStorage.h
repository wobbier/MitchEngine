#pragma once
#include <vector>
#include <memory>
#include <boost/dynamic_bitset.hpp>

#include "Entity.h"
#include "Component.h"
#include "Util.h"
#include "ComponentTypeArray.h"

namespace ma {
	class ComponentStorage {
	public:
		ComponentStorage(std::size_t InEntityAmount);
		~ComponentStorage();

		MA_NONCOPYABLE(ComponentStorage);
		MA_NONMOVABLE(ComponentStorage);

		void AddComponent(Entity& InEntity, BaseComponent* InComponent, TypeId InComponentTypeId);

		BaseComponent& GetComponent(const Entity& InEntity, TypeId InTypeId);

		ComponentTypeArray GetComponentTypes(const Entity& InEntity) const;

		void RemoveComponent(const Entity& InEntity, TypeId InTypeId);

		void RemoveAllComponents(Entity& InEntity);

		void Resize(std::size_t InAmount);
	private:
		typedef std::vector<std::unique_ptr<BaseComponent> > ImplComponentArray;

		struct EntityComponents {
			EntityComponents() = default;

			EntityComponents(EntityComponents&& E) :
				Components(std::move(E.Components)),
				ComponentTypeList(std::move(E.ComponentTypeList)) {
			}
			ImplComponentArray Components;

			ComponentTypeArray ComponentTypeList;
		};

		std::vector<EntityComponents> ComponentEntries;

		ImplComponentArray& GetComponents_Implementation(const Entity& E);

		const ImplComponentArray& GetComponents_Implementation(const Entity& E) const;
	};
}