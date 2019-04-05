#include "PCH.h"
#include "Cores/EditorCore.h"
#include "Components/Transform.h"
#include "Engine/World.h"
#include <stack>
#include "Havana.h"
#include "HavanaEvents.h"

EditorCore::EditorCore(Havana* editor)
	: Base(ComponentFilter().Excludes<Transform>())
	, m_editor(editor)
{
	std::vector<TypeId> events;
	events.push_back(SaveSceneEvent::GetEventId());
	events.push_back(NewSceneEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

EditorCore::~EditorCore()
{
}

void EditorCore::Init()
{
}

void EditorCore::Update(float dt)
{
}

void EditorCore::Update(float dt, Transform* rootTransform)
{
	BROFILER_CATEGORY("SceneGraph::Update", Brofiler::Color::Green);
	RootTransform = rootTransform;
}

bool EditorCore::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == SaveSceneEvent::GetEventId())
	{
		SaveWorld();
		return true;
	}

	return false;
}

void EditorCore::SaveSceneRecursively(json& d, Transform* CurrentTransform)
{
	BROFILER_CATEGORY("SceneGraph::UpdateRecursively", Brofiler::Color::DarkOrange);
	json thing;

	thing["Name"] = CurrentTransform->Name;

	json& componentsJson = thing["Components"];
	Entity* ent = GetWorld().GetEntity(CurrentTransform->Parent);

	auto comps = ent->GetAllComponents();
	for (auto comp : comps)
	{
		json compJson;
		comp->Serialize(compJson);
		componentsJson.push_back(compJson);
	}
	if (CurrentTransform->Children.size() > 0)
	{
		for (Transform* Child : CurrentTransform->Children)
		{
			SaveSceneRecursively(thing["Children"], Child);
		}
	}
	d.push_back(thing);
}

void EditorCore::OnEntityAdded(Entity& NewEntity)
{
	Base::OnEntityAdded(NewEntity);

	//Transform& NewEntityTransform = NewEntity.GetComponent<Transform>();

	//if (NewEntityTransform.ParentTransform == nullptr && NewEntity != *RootEntity.lock().get())
	{
		//NewEntityTransform.SetParent(RootEntity.lock()->GetComponent<Transform>());
	}
}

#if ME_EDITOR

void EditorCore::OnEditorInspect()
{
	BaseCore::OnEditorInspect();
}

void EditorCore::SaveWorld()
{
	json world;

	if (RootTransform->Children.size() > 0)
	{
		for (Transform* Child : RootTransform->Children)
		{
			SaveSceneRecursively(world["Scene"], Child);
		}
	}
	std::cout << world.dump(4) << std::endl;
}

#endif