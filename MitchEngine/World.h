#pragma once
#include "Entity.h"

namespace ma {
	class World {
	public:
		World();
		World(std::size_t inEntityPoolSize);
		~World();
		World(const World& world) = delete;
		World(World&& world) = delete;
		World& operator=(const World&) = delete;
		World& operator=(World&&) = delete;

	};
}