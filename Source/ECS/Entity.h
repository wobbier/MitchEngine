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

	template <typename T>
	bool HasComponent();

	template <typename T>
	T& AddComponent(T* inComponent);

	BaseComponent* AddComponentByName(const std::string& inComponent);

	template <typename T, typename... Args>
	T& AddComponent(Args&&... args);

	template <typename T>
	T& GetComponent() const;

	template <typename T>
	void RemoveComponent();

	std::vector<BaseComponent*> GetAllComponents() const;

	const EntityID& GetId() const;

	void SetActive(const bool InActive);
	bool IsLoading = false;

protected:
private:
	World * GameWorld = nullptr;
	EntityID Id;

	void AddComponent(BaseComponent* inComponent, TypeId inComponentTypeId);
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
T& Entity::AddComponent(T* inComponent)
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
	return AddComponent(new T{ std::forward<Args>(args)... });
}

template <typename T>
void Entity::RemoveComponent()
{
	RemoveComponent(T::GetTypeId());
}