#pragma once

#include "ECS/Entity.h"
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "imgui.h"

class SelfDestruct final
	: public Component<SelfDestruct>
{
public:
	SelfDestruct()
		: Component("SelfDestruct")
	{
	}

	SelfDestruct(float InLifetime)
		: Component("SelfDestruct")
		, Lifetime(InLifetime)
	{
	}

	virtual void OnSerialize(json& outJson) final
	{
	}

	virtual void OnDeserialize(const json& inJson) final
	{
	}

#if ME_EDITOR
	virtual void OnEditorInspect() final
	{
		ImGui::DragFloat("Movement Speed", &Lifetime);
	}
#endif
	virtual void Init() final
	{
	}

	float Lifetime = 5.0f;
};
ME_REGISTER_COMPONENT(SelfDestruct)

class SelfDestructor
	: public Core<SelfDestructor>
{
public:
	SelfDestructor()
		: Base(ComponentFilter().Requires<SelfDestruct>())
	{
	}
//
//	virtual void OnEntityAdded(Entity& NewEntity) final;
//	virtual void OnEntityRemoved(Entity& InEntity) final;
//#if ME_EDITOR
//	virtual void OnEditorInspect() final;
//#endif

	virtual void Update(float dt) final;

//private:
//	virtual void Init() final;
//	virtual void OnStart() override;
//	virtual void OnStop() override;
};
ME_REGISTER_CORE(SelfDestructor)