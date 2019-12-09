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
#include <vector>

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

	template <typename T>
	T& GetCore();

	template <typename T, typename... Args>
	T& AddCore(Args&& ... args);

	template <typename TCore>
	bool HasCore();

	BaseCore* GetCore(TypeId InType);

	bool HasCore(TypeId InType);

	std::vector<BaseCore*> GetAllCores();

	WeakPtr<Entity> CreateEntity();

	void Simulate();
	void Start();
	void Stop();

	void Destroy();
	void Cleanup();

	void UpdateLoadedCores(float DeltaTime);

	void DestroyEntity(Entity &InEntity);

	std::size_t GetEntityCount() const;
	WeakPtr<Entity> GetEntity(EntityID id);
	World();
	World(std::size_t InEntityPoolSize);
	~World();

	ME_NONCOPYABLE(World);
	ME_NONMOVABLE(World);
	bool IsLoading = true;
	void AddCoreByName(const std::string& core);
	std::unordered_map<TypeId, BaseCore*> m_loadedCores;
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

		~TEntityAttributes() {
			//Attributes.clear();
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

	void AddCore(BaseCore& InCore, TypeId InCoreTypeId, bool HandleUpdate = false);

	void CheckForResize(std::size_t InNumEntitiesToBeAllocated);

	void Resize(std::size_t InAmount);

	void ActivateEntity(Entity& InEntity, const bool InActive);

	// Access to components
	friend class Entity;
public:
	void MarkEntityForDelete(Entity& EntityToDestroy);
};

template<typename TCore>
void World::AddCore(TCore& InCore)
{
	AddCore(InCore, TCore::GetTypeId());
}

template <typename T, typename... Args>
T& World::AddCore(Args&& ... args)
{
	if (HasCore(T::GetTypeId()))
	{
		return GetCore<T>();
	}
	return AddCore(new T{ std::forward<Args>(args)... }, T::GetTypeId());
}

template <typename T>
T& World::GetCore()
{
	*Cores.find(T::GetTypeId())->second.get();
}

template <typename TCore>
bool World::HasCore()
{
	return Cores.find(TCore::GetTypeId()) != Cores.end();
}
