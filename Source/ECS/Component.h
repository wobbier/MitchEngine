// 2018 Mitchell Andrews
#pragma once
#include "ClassTypeId.h"
#include "EntityID.h"
#include "Dementia.h"

#include "EntityHandle.h"
#include "JSON.h"

#define ME_REGISTER_COMPONENT_FOLDER(TYPE, FOLDER)            \
	namespace details {                                       \
    namespace                                                 \
    {                                                         \
        template<class T>                                     \
        class ComponentRegistration;                          \
                                                              \
        template<>                                            \
        class ComponentRegistration<TYPE>                     \
        {                                                     \
            static const RegistryEntry<TYPE>& reg;            \
        };                                                    \
                                                              \
        const RegistryEntry<TYPE>&                            \
            ComponentRegistration<TYPE>::reg =                \
                RegistryEntry<TYPE>::Instance(#TYPE, FOLDER); \
    }}
#define ME_REGISTER_COMPONENT(TYPE) ME_REGISTER_COMPONENT_FOLDER(TYPE, "")

class BaseComponent
{
	friend class ComponentStorage;
public:
	BaseComponent() = delete;
	BaseComponent(const char* CompName)
		: TypeName(CompName)
	{
		TypeName = TypeName.substr(TypeName.find(' ') + 1);
	}

	virtual ~BaseComponent() = default;

	virtual void Init() = 0;

	const std::string& GetName() const
	{
		return TypeName;
	}

	EntityHandle Parent;

	virtual void Serialize(json& outJson) = 0;
	virtual void Deserialize(const json& inJson) = 0;

#if ME_EDITOR
	virtual void OnEditorInspect() = 0;
#endif

private:
	std::string TypeName;
};

template<typename T>
class Component
	: public BaseComponent
{
public:
	Component(const char* Name)
		: BaseComponent(Name)
	{
	}

	static TypeId GetTypeId()
	{
		return ClassTypeId<BaseComponent>::GetTypeId<T>();
	}

	virtual void Serialize(json& outJson) final
	{
		outJson["Type"] = GetName();
		OnSerialize(outJson);
	}

	virtual void Deserialize(const json& inJson) final
	{
		OnDeserialize(inJson);
	}

#if ME_EDITOR

	virtual void OnEditorInspect() override
	{
	}

#endif
private:
	virtual void OnSerialize(json& outJson) = 0;
	virtual void OnDeserialize(const json& inJson) = 0;
};

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
