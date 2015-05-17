// 2015 Mitchell Andrews
#pragma once
#include "Component.h"
#include <vector>

namespace ma {
	class Entity {
	public:
		Entity();
		~Entity();

		// Get the current component by id
		Component* GetComponent(int id);

		// Add a new component to the entity
		void AddComponent(Component* component);

		// Check if the entity already has this type of object.
		bool HasComponent(int id);

		// Handle used to identify the entity.
		unsigned int Handle;
	protected:
	private:
		std::vector<Component*> Components;
	};
}
