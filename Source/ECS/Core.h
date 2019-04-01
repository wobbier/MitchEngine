#pragma once
#include "Entity.h"
#include "ClassTypeId.h"
#include "ComponentFilter.h"
#include <vector>

class World;

class BaseCore
{
public:
	BaseCore() = default;
	BaseCore(const char* CompName, const ComponentFilter& Filter);
	virtual ~BaseCore() = 0;

	// Each core must update each loop
	virtual void Update(float dt) = 0;
	virtual void OnEntityAdded(Entity& NewEntity);

	// Get The World attached to the Core
	World& GetWorld() const;

	// Get All the entities that are within the Core
	const std::vector<Entity>& GetEntities() const;

	// Get the component filter associated with the core.
	const ComponentFilter& GetComponentFilter() const;

	const std::string& GetName() const;

#if ME_EDITOR
	virtual void OnEditorInspect();
#endif

protected:
	class Engine* GameEngine;
private:
	// Separate init from construction code.
	virtual void Init() = 0;

	// Add an entity to the core
	void Add(Entity& InEntity);

	void Remove(Entity& InEntity);

	// The Entities that are attached to this system
	std::vector<Entity> Entities;

	// The World attached to the system
	World* GameWorld;

	ComponentFilter CompFilter;

	std::string Name;

	friend class World;
};

// Use the CRTP patten to define custom systems
template<typename T>
class Core
	: public BaseCore
{
public:
	typedef Core<T> Base;

	Core() = default;

	Core(ComponentFilter& InComponentFilter) : BaseCore(typeid(T).name(), InComponentFilter)
	{
	}

	static TypeId GetTypeId()
	{
		return ClassTypeId<BaseCore>::GetTypeId<T>();
	}

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif
};

#if ME_EDITOR

template<typename T>
void Core<T>::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

#endif
