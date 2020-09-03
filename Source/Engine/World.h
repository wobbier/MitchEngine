#pragma once

#include "Dementia.h"

#include "ECS/Entity.h"
#include "ECS/EntityHandle.h"
#include "ECS/Core.h"
#include "ECS/ComponentStorage.h"
#include "ECS/EntityIdPool.h"
#include "Resource/ResourceCache.h"
#include "Pointers.h"

class Transform;

class World
	: public std::enable_shared_from_this<World>
{
private:
	typedef std::vector<Entity> EntityArray;
	typedef std::unordered_map<EntityID, Entity, EntityIDHash> MasterEntityArray;

	struct CoreDeleter
	{
		void operator() (BaseCore* InCore) const
		{
			InCore->GameWorld = nullptr;
			InCore->Entities.clear();
		}
	};

	typedef std::unordered_map<TypeId, std::unique_ptr<BaseCore, CoreDeleter>> CoreArray;

	// Access to components
	friend class Entity;


public:
	SharedPtr<World> GetSharedPtr();
	template <typename TCore>
	void AddCore(TCore& inCore);

	template <typename T, typename... Args>
	T& AddCore(Args&& ... args);

	template <typename TCore>
	bool HasCore();

	BaseCore* GetCore(TypeId InType);

	bool HasCore(TypeId InType);

	std::vector<BaseCore*> GetAllCores();

	const CoreArray& GetAllCoresArray();

	EntityHandle CreateEntity();

	void Simulate();
	void Start();
	void Stop();

	void Destroy();

	void Unload();

	void UpdateLoadedCores(float DeltaTime);

	void MarkEntityForDelete(Entity& EntityToDestroy);

	EntityHandle CreateFromPrefab(std::string& FilePath, Transform* Parent = nullptr);

	std::size_t GetEntityCount() const;
	EntityHandle GetEntity(const EntityID& id);
	Entity* GetEntityRaw(const EntityID& id);
	const bool EntityExists(const EntityID& InEntity) const;
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

	void DestroyEntity(Entity& InEntity, bool RemoveFromWorld = true);

	void AddCore(BaseCore& InCore, TypeId InCoreTypeId, bool HandleUpdate = false);

	void CheckForResize(std::size_t InNumEntitiesToBeAllocated);

	void Resize(std::size_t InAmount);

	void ActivateEntity(Entity& InEntity, const bool InActive);

	EntityHandle LoadPrefab(const nlohmann::json& obj, Transform* parent, Transform* root);
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
		return GetCore(T::GetTypeId());
	}
	return AddCore(new T{ std::forward<Args>(args)... }, T::GetTypeId());
}

template <typename TCore>
bool World::HasCore()
{
	return Cores.find(TCore::GetTypeId()) != Cores.end();
}
