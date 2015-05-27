#pragma once
#include <memory>
#include <boost/dynamic_bitset.hpp>
#include "Entity.h"
#include "Core.h"
#include "ComponentStorage.h"
#include "EntityIdPool.h"

namespace ma {
	typedef std::vector<Entity> EntityArray;
	class World {
	public:

		template <typename TCore>
		void AddCore(TCore& inCore);

		Entity CreateEntity();

		void Simulate();

		std::size_t GetEntityCount() const;

		World();
		World(std::size_t InEntityPoolSize);
		~World();
		World(const World& world) = delete;
		World(World&& world) = delete;
		World& operator=(const World&) = delete;
		World& operator=(World&&) = delete;
	private:
		struct CoreDeleter {
			void operator() (BaseCore* InCore) const {
				InCore->GameWorld = nullptr;
				InCore->Entities.clear();
			}
		};

		typedef std::unordered_map<Type, std::unique_ptr<BaseCore, CoreDeleter>> CoreArray;

		CoreArray Cores;

		EntityIdPool EntIdPool;

		struct TEntityAttributes {
			struct Attribute {
				bool IsActive;

				boost::dynamic_bitset<> Cores;
			};

			TEntityAttributes(std::size_t InEntityAmount) :
				Storage(InEntityAmount),
				Attributes(InEntityAmount) {

			}

			void Resize(std::size_t InAmount) {
				Storage.Resize(InAmount);
				Attributes.resize(InAmount);
			}

			ComponentStorage Storage;

			std::vector<Attribute> Attributes;
		};

		TEntityAttributes EntityAttributes;

		struct TEntityCache {
			EntityArray Alive;
			EntityArray Killed;
			EntityArray Activated;
			EntityArray Deactivated;
		}

		EntityCache;

		void AddCore(BaseCore& InCore, Type InCoreTypeId);

		void CheckForResize(std::size_t InNumEntitiesToBeAllocated);

		void Resize(std::size_t InAmount);

		// Access to components
		friend class Entity;
	};

	template<typename TCore>
	void World::AddCore(TCore& InCore) {
		AddCore(InCore, TCore::GetTypeId());
	}
}