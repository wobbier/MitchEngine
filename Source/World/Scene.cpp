#include "PCH.h"

#include "Scene.h"
#include "Engine/Engine.h"

Scene::Scene(const std::string& SceneFilePath)
	: FilePath(std::move(SceneFilePath))
{
	CurrentLevel = File(FilePath);
}

void Scene::UnLoad()
{
	GameWorld = nullptr;
}

void Scene::LoadSceneObject(const json& obj, Transform* parent)
{
	EntityHandle ent;
	if (parent)
	{
		auto t = parent->GetChildByName(obj["Name"]);
		if (t)
		{
			ent = t->Parent;
		}
	}
	if (!ent)
	{
		ent = GameWorld->CreateEntity();
	}
	ent->IsLoading = true;
	Transform* transComp = nullptr;
	for (const json& comp : obj["Components"])
	{
		if (comp.is_null())
		{
			continue;
		}
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
			addedComp->Init();
		}
	}
	ent->SetActive(true);

	if (obj.contains("DestroyOnLoad"))
	{
		ent->DestroyOnLoad = obj["DestroyOnLoad"];
	}

	ent->IsLoading = false;

	if (obj.contains("Children"))
	{
		for (const json& child : obj["Children"])
		{
			LoadSceneObject(child, transComp);
		}
	}
}

bool Scene::Load(SharedPtr<World> InWorld)
{
	GameWorld = InWorld;
	GameWorld->IsLoading = true;

	if (CurrentLevel.FilePath.LocalPath.size() > 0)
	{
		CurrentLevel.Read();
	}

	if (CurrentLevel.Data.length() > 0)
	{
		json level;

		level = json::parse(CurrentLevel.Data);

		json& cores = level["Cores"];
		for (json& core : cores)
		{
			LoadCore(core);
		}

		json& scene = level["Scene"];
		for (json& ent : scene)
		{
			LoadSceneObject(ent, nullptr);
		}
	}
	else
	{
		GameWorld->IsLoading = false;
		return false;
	}

	GameWorld->IsLoading = false;
	return true;
}

void Scene::LoadCore(json& core)
{
	auto addedCore = GameWorld->AddCoreByName(core["Type"]);
	addedCore->Deserialize(core);
}

bool Scene::IsNewScene()
{
	return FilePath.LocalPath.empty();
}

void Scene::SaveSceneRecursively(json& d, Transform* CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	json outEntity;

	outEntity["Name"] = CurrentTransform->GetName();
	outEntity["DestroyOnLoad"] = CurrentTransform->Parent->DestroyOnLoad;

	json& componentsJson = outEntity["Components"];
	EntityHandle ent = CurrentTransform->Parent;

	auto comps = ent->GetAllComponents();
	for (auto comp : comps)
	{
		json compJson;
		comp->Serialize(compJson);
		componentsJson.push_back(compJson);
	}
	if (CurrentTransform->GetChildren().size() > 0)
	{
		for (SharedPtr<Transform> Child : CurrentTransform->GetChildren())
		{
			SaveSceneRecursively(outEntity["Children"], Child.get());
		}
	}
	d.push_back(outEntity);
}

void Scene::Save(std::string fileName, Transform* root)
{
#if ME_EDITOR
	FilePath = Path(fileName);

	File worldFile(FilePath);
	json world;

	if (root->GetChildren().size() > 0)
	{
		for (SharedPtr<Transform> Child : root->GetChildren())
		{
			SaveSceneRecursively(world["Scene"], Child.get());
		}
	}

	json& cores = world["Cores"];
	for (auto& core : GetEngine().GetWorld().lock()->GetAllCores())
	{
		if ((*core).GetIsSerializable())
		{
			json coreDef;
			(*core).Serialize(coreDef);
			cores.push_back(coreDef);
		}
	}

	worldFile.Write(world.dump(4));
	std::cout << world.dump(4) << std::endl;
#endif
}

