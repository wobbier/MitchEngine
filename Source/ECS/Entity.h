// 2018 Mitchell Andrews
#pragma once
#include "Component.h"
#include "ClassTypeId.h"
#include <unordered_map>
#include <string>
#include "EntityID.h"

class World;

class Entity
{
	friend class World;
public:
	Entity();
	Entity(World& inWorld, EntityID inId);
	~Entity();
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	bool operator==(const Entity& entity) const;
	bool operator!=(const Entity& entity) const { return !operator==(entity); }
	explicit operator bool() const
	{
		return GameWorld != nullptr;
	}

	template <typename T>
	bool HasComponent();

	template <typename T>
	T& AddComponent(SharedPtr<T> inComponent);

	BaseComponent* AddComponentByName(const std::string& inComponent);

	template <typename T, typename... Args>
	T& AddComponent(Args&&... args);

	template <typename T>
	T& GetComponent() const;

	template <typename T>
	void RemoveComponent();

	void RemoveComponent(const std::string& Name);

	std::vector<BaseComponent*> GetAllComponents() const;

	const EntityID& GetId() const;

	void SetActive(const bool InActive);
	void MarkForDelete();
	bool IsLoading = false;
	bool DestroyOnLoad = true;

#if USING( ME_EDITOR )
	void OnEditorInspect();
#endif

private:
	World* GameWorld = nullptr;
	EntityID Id;

	void AddComponent(SharedPtr<BaseComponent> inComponent, TypeId inComponentTypeId);
	const bool HasComponent(TypeId inComponentType) const;
	BaseComponent& GetComponent(TypeId InTypeId) const;
	void RemoveComponent(TypeId InComponentTypeId);
};

template <typename T>
bool Entity::HasComponent()
{
	//static_assert(std::is_base_of<BaseComponent, T>(), "T is not a component, cannot add T to entity");
	return HasComponent(T::GetTypeId());
}

template <typename T>
T& Entity::AddComponent(SharedPtr<T> inComponent)
{
	if (HasComponent(T::GetTypeId()))
	{
		return GetComponent<T>();
	}

	//static_assert(std::is_base_of<BaseComponent, T>(), "T is not a component, cannot add T to entity");
	AddComponent(inComponent, T::GetTypeId());
	return *inComponent;
}

template <typename T>
T& Entity::GetComponent() const
{
	//static_assert(std::is_base_of<BaseComponent, T>(), "T is not a component, cannot get T from Entity");
	return static_cast<T&>(GetComponent(T::GetTypeId()));
}

template <typename T, typename... Args>
T& Entity::AddComponent(Args&&... args)
{
	if (HasComponent(T::GetTypeId()))
	{
		return GetComponent<T>();
	}
	SharedPtr<T> t = MakeShared<T>(std::forward<Args>(args)...);
	return AddComponent(t);
}

template <typename T>
void Entity::RemoveComponent()
{
	RemoveComponent(T::GetTypeId());
}