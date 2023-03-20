#pragma once
#include "Entity.h"
#include "ClassTypeId.h"
#include "ComponentFilter.h"
#include <vector>
#include "CoreDetail.h"
#include "Core/UpdateContext.h"

class World;
class DebugDrawer;

#define ME_REGISTER_CORE(TYPE)                      \
	namespace details {                                  \
    namespace                                            \
    {                                                    \
        template<class T>                                \
        class CoreRegistration;                     \
                                                         \
        template<>                                       \
        class CoreRegistration<TYPE>                \
        {                                                \
            static const CoreRegistryEntry<TYPE>& reg;       \
        };                                               \
                                                         \
        const CoreRegistryEntry<TYPE>&                       \
            CoreRegistration<TYPE>::reg =           \
                CoreRegistryEntry<TYPE>::Instance(#TYPE);    \
    }}

class BaseCore
{
	friend class World;
public:
	BaseCore() = default;
	BaseCore(const char* CompName, const ComponentFilter& Filter);
	virtual ~BaseCore() {};

	// Each core must update each loop
	virtual void Update(const UpdateContext& inUpdateContext) {};
	virtual void LateUpdate(const UpdateContext& inUpdateContext) {};
	virtual void OnEntityAdded(Entity& NewEntity) {};
	virtual void OnEntityRemoved(Entity& InEntity) {};
	virtual void OnEntityDestroyed(Entity& InEntity) {};
	virtual void OnDrawGuizmo(DebugDrawer*) {};

	// Get The World attached to the Core
	World& GetWorld() const;

	// Get All the entities that are within the Core
	const std::vector<Entity>& GetEntities() const;

	// Get All the entities that are within the Core
	std::vector<Entity>& GetEntities();

	// Get the component filter associated with the core.
	const ComponentFilter& GetComponentFilter() const;

	const std::string& GetName() const;

#if USING( ME_EDITOR )
	virtual void OnEditorInspect();
	virtual void Serialize(json& outJson) = 0;
#endif
	virtual void Deserialize(const json& inJson) = 0;
	const bool GetIsSerializable() const;

protected:
	void SetIsSerializable(bool value);

	class Engine* GameEngine;
	World* GameWorld;

private:
	// Separate init from construction code.
	virtual void Init() {};

	// Separate init from construction code.
	virtual void OnStart() {};
	virtual void OnStop() {};

	// Add an entity to the core
	void Add(Entity& InEntity);

	void Remove(Entity& InEntity);

	void Clear();

	// The Entities that are attached to this system
	std::vector<Entity> Entities;

	// The World attached to the system

	ComponentFilter CompFilter;

	std::string Name;

	bool IsRunning = false;
	bool DestroyOnLoad = true;
	bool IsSerializable = true;
	bool _padding[5];
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
	virtual void OnEntityAdded(Entity& NewEntity) override
	{
	}

	virtual void OnEntityRemoved(Entity& InEntity) override
	{
	}

	virtual void OnEntityDestroyed(Entity& InEntity) override
	{
	}

	virtual void OnDeserialize(const json& inJson)
	{
	}

#if USING( ME_EDITOR )
	virtual void OnSerialize(json& outJson)
	{
	}
	virtual void OnEditorInspect() override;
	virtual void Serialize(json& outJson) final {
		outJson["Type"] = GetName();
		OnSerialize(outJson);
	}
#endif
	virtual void Deserialize(const json& inJson) final {
		OnDeserialize(inJson);
	}
};

#if USING( ME_EDITOR )

template<typename T>
void Core<T>::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

#endif