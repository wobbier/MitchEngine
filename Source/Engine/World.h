#pragma once
#include <memory>
#include <unordered_map>
#include "ECS/Entity.h"
#include "ECS/Core.h"
#include "ECS/ComponentStorage.h"
#include "ECS/EntityIdPool.h"
#include "Dementia.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"

class World
{
private:
	typedef std::vector<Entity> EntityArray;
	typedef std::vector<SharedPtr<Entity>> MasterEntityArray;

	struct CoreDeleter
	{
		void operator() (BaseCore* InCore) const
		{
			InCore->GameWorld = nullptr;
			InCore->Entities.clear();
		}
	};

	typedef std::unordered_map<TypeId, std::unique_ptr<BaseCore, CoreDeleter>> CoreArray;
public:
	template <typename TCore>
	void AddCore(TCore& inCore);

	std::vector<BaseCore*> GetAllCores();

	WeakPtr<Entity> CreateEntity();

	void Simulate();

	void Destroy();
	void Cleanup();

	void DestroyEntity(Entity &InEntity);

	std::size_t GetEntityCount() const;
	Entity* GetEntity(EntityID id);
	World();
	World(std::size_t InEntityPoolSize);
	~World();

	ME_NONCOPYABLE(World);
	ME_NONMOVABLE(World);
private:
	CoreArray Cores;

	EntityIdPool EntIdPool;

	struct TEntityAttributes
	{
		struct Attribute
		{
			bool IsActive;

			std::bitset<64> Cores;
		};

		TEntityAttributes(std::size_t InEntityAmount) :
			Storage(InEntityAmount),
			Attributes(InEntityAmount)
		{
		}

		void Resize(std::size_t InAmount)
		{
			Storage.Resize(InAmount);
			Attributes.resize(InAmount);
		}

		ComponentStorage Storage;

		std::vector<Attribute> Attributes;
	}

	EntityAttributes;

	struct TEntityCache
	{
		MasterEntityArray Alive;
		EntityArray Killed;
		EntityArray Activated;
		EntityArray Deactivated;

		void ClearTemp()
		{
			Killed.clear();
			Activated.clear();
			Deactivated.clear();
		}
	}

	EntityCache;

	void AddCore(BaseCore& InCore, TypeId InCoreTypeId);

	void CheckForResize(std::size_t InNumEntitiesToBeAllocated);

	void Resize(std::size_t InAmount);

	void ActivateEntity(Entity& InEntity, const bool InActive);

	// Access to components
	friend class Entity;
};

template<typename TCore>
void World::AddCore(TCore& InCore)
{
	AddCore(InCore, TCore::GetTypeId());
}
