#include <Cores/ExampleCore.h>
#include <Components/Transform.h>

ExampleCore::ExampleCore()
	: Base(ComponentFilter().Requires<Transform>())
{
}

void ExampleCore::Init()
{
}

void ExampleCore::OnEntityAdded(Entity& NewEntity)
{
}

void ExampleCore::OnEntityRemoved(Entity& InEntity)
{
}

void ExampleCore::Update(float dt)
{
	auto& entities = GetEntities();
	for(auto ent : entities)
	{
	}
}

#if USING( ME_EDITOR )

void ExampleCore::OnEditorInspect()
{
}

#endif