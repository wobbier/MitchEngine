// 2015 Mitchell Andrews
#pragma once
#include "Component.h"
#include "TypeId.h"
#include <unordered_map>
#include <string>

#define MITCH_32_BIT_IDS 1

#ifdef MITCH_32_BIT_IDS
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 20
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 12
#else
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 48
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 16
#endif


namespace ma {
	class World;

	class Entity {
	public:
		struct ID {
			// Possible room for 64-bit
			typedef __int32 IntType;

			IntType Index{ MITCH_ENTITY_ID_INDEX_BIT_COUNT };
			IntType Counter{ MITCH_ENTITY_ID_COUNTER_BIT_COUNT };

			ID() : Index(0), Counter(0) {};
			ID(IntType inIndex, IntType inCounter) : Index(inIndex), Counter(inCounter) {};

			inline operator IntType() const {
				return Value();
			}

			inline IntType Value() const {
				return (Counter << MITCH_ENTITY_ID_COUNTER_BIT_COUNT) | Index;
			}

			void Clear() {
				Index = Counter = 0;
			}

			bool IsNull() const {
				return Value() == 0;
			}
		};

		Entity();
		Entity(World& inWorld, Entity::ID inId);
		~Entity();
		Entity(const Entity&) = default;
		Entity& operator=(const Entity&) = default;


		template <typename T>
		T& AddComponent(T* inComponent);

		const ID& GetId() const;
	protected:
	private:
		World* GameWorld = nullptr;
		ID Id;

		void AddComponent(BaseComponent* inComponent, Type inComponentTypeId);
	};


}
