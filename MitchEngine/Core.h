// 2015 Mitchell Andrews
#pragma once
#include "Entity.h"
#include <vector>

namespace ma {
	class World;

	class BaseCore {
	public:
		BaseCore() = default;
		virtual ~BaseCore() = 0;

		// Each core must update each loop
		virtual void Update(float dt) = 0;

		// Can receive messages from the engine.
		virtual void SendMessage(class Message* message) = 0;

		// Get The World attached to the System
		World& GetWorld() const;

		// Get All the entities that are within the System
		std::vector<Entity> GetEntities() const;
	protected:
		class Engine* GameEngine;
	private:
		// Separate init from construction code.
		virtual void Init() = 0;

		// The Entities that are attached to this system
		std::vector<Entity> Entities;

		// The World attached to the system
		World* GameWorld;

		friend class World;
	};

	// Use the CRTP patten to define custom systems
	template<typename T>
	class Core
		: BaseCore {
	public:
		typedef Core<T> BaseCore;

		Core() = default;
	};
}

