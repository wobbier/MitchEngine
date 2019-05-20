#pragma once
#include <string>
#include "FilePath.h"
#include "Components/Transform.h"
#include "File.h"

class Scene
{
public:
	Scene(const std::string& SceneFilePath)
		: Path(SceneFilePath)
	{
		CurrentLevel = File(Path);
	}

	void UnLoad()
	{
		GameWorld = nullptr;
	}

	void LoadSceneObject(const json& obj, Transform* parent)
	{
		WeakPtr<Entity> ent;
		if (parent)
		{
			auto t = parent->GetChildByName(obj["Name"]);
			if (t)
			{
				ent = GameWorld->GetEntity(t->Parent);
			}
		}
		if (!ent.lock())
		{
			ent = GameWorld->CreateEntity();
		}
		ent.lock()->IsLoading = true;
		Transform* transComp = nullptr;
		for (const json& comp : obj["Components"])
		{
			BaseComponent* addedComp = ent.lock()->AddComponentByName(comp["Type"]);
			if (comp["Type"] == "Transform")
			{
				transComp = static_cast<Transform*>(addedComp);
				if (parent)
				{
					transComp->SetParent(*parent);
				}
				transComp->SetName(obj["Name"]);
			}
			if (addedComp)
			{
				addedComp->Deserialize(comp);
			}
			addedComp->Init();
		}
		ent.lock()->SetActive(true);
		ent.lock()->IsLoading = false;

		if (obj.contains("Children"))
		{
			for (const json& child : obj["Children"])
			{
				LoadSceneObject(child, transComp);
			}
		}
	}

	void Load(SharedPtr<World> InWorld)
	{
		GameWorld = InWorld;
		GameWorld->IsLoading = true;

		CurrentLevel.Read();

		if (CurrentLevel.Data.length() > 0)
		{
			json level;

			level = json::parse(CurrentLevel.Data);
			json& scene = level["Scene"];
			for (json& ent : scene)
			{
				LoadSceneObject(ent, nullptr);
			}
		}

		GameWorld->IsLoading = false;
	}

	SharedPtr<World> GameWorld;

	File CurrentLevel;

	FilePath Path;
};