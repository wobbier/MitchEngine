#include <Cores/ExampleCore.h>
#include <Components/Transform.h>
#include <Components/Camera.h>

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
}

#if ME_EDITOR

void ExampleCore::OnEditorInspect()
{
}

#endif