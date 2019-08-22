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
		if (comp.is_null())
		{
			continue;
		}
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

bool Scene::Load(SharedPtr<World> InWorld)
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
		json& cores = level["Cores"];
		for (json& core : cores)
		{
			LoadCore(core);
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
	GameWorld->AddCoreByName(core["Type"]);
}

bool Scene::IsNewScene()
{
	return FilePath.LocalPath.empty();
}

void Scene::SaveSceneRecursively(json& d, Transform* CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");
	json thing;

	thing["Name"] = CurrentTransform->Name;

	json& componentsJson = thing["Components"];
	Entity* ent = GetEngine().GetWorld().lock()->GetEntity(CurrentTransform->Parent).lock().get();

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

void Scene::Save(std::string fileName, Transform* root)
{
	FilePath = Path(fileName);

	File worldFile(FilePath);
	json world;

	if (root->Children.size() > 0)
	{
		for (Transform* Child : root->Children)
		{
			SaveSceneRecursively(world["Scene"], Child);
		}
	}

	json& cores = world["Cores"];
	for (auto& core : GetEngine().GetWorld().lock()->GetAllCores())
	{
		if ((*core).CanSerialize())
		{
			json coreDef;
			coreDef["Type"] = (*core).GetName();
			cores.push_back(coreDef);
		}
	}

	worldFile.Write(world.dump(4));
	std::cout << world.dump(4) << std::endl;
}

