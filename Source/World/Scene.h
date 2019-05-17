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

	}

	void LoadSceneObject(const json& obj, Transform* parent)
	{
		bool exists = false;
		Entity* ent = nullptr;
		if (parent)
		{
			auto t = parent->GetChildByName(obj["Name"]);
			if (t)
			{
				exists = true;
				ent = GameWorld->GetEntity(parent->Parent);
			}
		}
		if (!ent)
		{
			ent = GameWorld->CreateEntity().lock().get();
		}
		if (obj["Name"] == "Sponza")
		{
			int i = 0;
			i++;
		}
		Transform* transComp = nullptr;
		for (const json& comp : obj["Components"])
		{
			ent->IsLoading = true;
			BaseComponent* addedComp = ent->AddComponentByName(comp["Type"]);
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
			ent->IsLoading = false;
		}

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