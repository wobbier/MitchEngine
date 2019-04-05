// 2018 Mitchell Andrews
#pragma once
#include <string>
#include <vector>
#include "ClassTypeId.h"
#include <memory>
#include "EntityID.h"
#include "Dementia.h"

#include <nlohmann/json.hpp>

// for convenience
using json = nlohmann::json;

class BaseComponent
{
	friend class ComponentStorage;
public:
	BaseComponent() = delete;
	BaseComponent(const char* CompName)
		: Name(CompName)
	{
		Name = Name.substr(Name.find(' ') + 1);
	}

	virtual ~BaseComponent() = default;

	virtual void Init() = 0;

	const std::string& GetName() const
	{
		return Name;
	}

	EntityID Parent;

#if ME_EDITOR
	virtual void OnEditorInspect() = 0;
	virtual void Serialize(json& outJson) = 0;
#endif

private:
	std::string Name;
};

template<typename T>
class Component
	: public BaseComponent
{
public:
	Component()
		: BaseComponent(typeid(T).name())
	{
	}

	static TypeId GetTypeId()
	{
		return ClassTypeId<BaseComponent>::GetTypeId<T>();
	}

	// Each core must update each loop
	virtual void Update(float dt)
	{
	}

#if ME_EDITOR
	virtual void OnEditorInspect() override
	{
	}

	virtual void Serialize(json& outJson) override
	{
		outJson["Type"] = GetName();
	}

#endif
};

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
