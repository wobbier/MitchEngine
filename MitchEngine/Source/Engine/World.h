#pragma once
#include <memory>
#include <unordered_map>
#include <boost/dynamic_bitset.hpp>
#include "Engine/Entity.h"
#include "Engine/Core.h"
#include "Engine/ComponentStorage.h"
#include "Engine/EntityIdPool.h"
#include "Utility/Util.h"
#include "Engine/ResourceCache.h"

class World
{
private:
	typedef std::vector<Entity> EntityArray;

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

	Entity CreateEntity();

	void Simulate();

	std::size_t GetEntityCount() const;

	World();
	World(std::size_t InEntityPoolSize);
	~World();

	MA_NONCOPYABLE(World);
	MA_NONMOVABLE(World);
private:
	CoreArray Cores;

	EntityIdPool EntIdPool;

	struct TEntityAttributes
	{
		struct Attribute
		{
			bool IsActive;

			boost::dynamic_bitset<> Cores;
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
		EntityArray Alive;
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
