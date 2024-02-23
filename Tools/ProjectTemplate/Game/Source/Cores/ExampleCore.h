#pragma once
#include <ECS/Core.h>
#include <Components/Transform.h>

class ExampleCore final
	: public Core<ExampleCore>
{
public:
	ExampleCore();
	~ExampleCore() = default;

	virtual void Init() final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	virtual void OnEntityRemoved(Entity& InEntity) final;

	virtual void Update(float dt) final;

private:
#if USING( ME_EDITOR )
	virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_CORE(ExampleCore)