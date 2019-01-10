// 2018 Mitchell Andrews
#pragma once
#include "Component.h"
#include "ClassTypeId.h"
#include <unordered_map>
#include <string>
#include <cstdint>

#ifdef _WIN32
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 20
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 12
#else
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 48
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 16
#endif

class World;

class Entity
{
public:
	struct ID
	{
		typedef
#ifdef _WIN64
			std::uint64_t
#else
			std::uint32_t
#endif
			IntType;

		IntType Index{ MITCH_ENTITY_ID_INDEX_BIT_COUNT };
		IntType Counter{ MITCH_ENTITY_ID_COUNTER_BIT_COUNT };

		ID() : Index(0), Counter(0) {};
		ID(IntType inIndex, IntType inCounter) : Index(inIndex), Counter(inCounter) {};

		inline operator IntType() const
		{
			return Value();
		}

		inline IntType Value() const
		{
			return (Counter << MITCH_ENTITY_ID_COUNTER_BIT_COUNT) | Index;
		}

		void Clear()
		{
			Index = Counter = 0;
		}

		bool IsNull() const
		{
			return Value() == 0;
		}
	};

	Entity();
	Entity(World& inWorld, Entity::ID inId);
	~Entity();
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	bool operator==(const Entity& entity) const;
	bool operator!=(const Entity& entity) const { return !operator==(entity); }

	template <typename T>
	T& AddComponent(T* inComponent);

	template <typename T, typename... Args>
	T& AddComponent(Args&&... args);

	template <typename T>
	T& GetComponent() const;

	template <typename T>
	void RemoveComponent();

	const ID& GetId() const;

	void SetActive(const bool InActive);

protected:
private:
	World * GameWorld = nullptr;
	ID Id;

	void AddComponent(BaseComponent* inComponent, TypeId inComponentTypeId);
	BaseComponent& GetComponent(TypeId InTypeId) const;
	void RemoveComponent(TypeId InComponentTypeId);
};

template <typename T>
T& Entity::AddComponent(T* inComponent)
{
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
	return AddComponent(new T{ std::forward<Args>(args)... });
}

template <typename T>
void Entity::RemoveComponent()
{
	RemoveComponent(T::GetTypeId());
}