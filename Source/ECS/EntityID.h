#pragma once
#include <cstdint>

#ifdef _WIN32
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 20
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 12
#else
#	define MITCH_ENTITY_ID_INDEX_BIT_COUNT 48
#	define MITCH_ENTITY_ID_COUNTER_BIT_COUNT 16
#endif

struct EntityID
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

	EntityID() : Index(0), Counter(0) {};
	EntityID(IntType inIndex, IntType inCounter) : Index(inIndex), Counter(inCounter) {};

	inline operator IntType() const
	{
		return Value();
	}
	
	bool operator==(const EntityID& other) const
	{
		return (Value() == other.Value());
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