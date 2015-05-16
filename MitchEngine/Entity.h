// 2015 Mitchell Andrews
#pragma once
#include "Component.h"
#include <vector>

namespace ma {
	class Entity {
	public:
		Entity();
		~Entity();

		Component* GetComponent(int id);
		void AddComponent(Component* component);
		bool HasComponent(int id);
	protected:
	private:
		std::vector<Component*> Components;
	};
}
