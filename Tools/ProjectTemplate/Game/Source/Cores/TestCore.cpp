#include "TestCore.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Engine/Input.h"
#include "Events/EventManager.h"
#include "optick.h"

TestCore::TestCore() : Base(ComponentFilter().Requires<Transform>())
{

}

void TestCore::OnEntityAdded(Entity& NewEntity)
{
}

void TestCore::OnEntityRemoved(Entity& InEntity)
{
}

#if ME_EDITOR
void TestCore::OnEditorInspect()
{
}
#endif

void TestCore::Update(float dt)
{
}

void TestCore::Init()
{
}
