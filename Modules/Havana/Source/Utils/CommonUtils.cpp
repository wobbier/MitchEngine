#include <Utils/CommonUtils.h>
#include <optick.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <HavanaEvents.h>

#if ME_EDITOR

void CommonUtils::RecusiveDelete(EntityHandle ent, Transform* trans)
{
	if (!trans)
	{
		return;
	}
	for (auto child : trans->GetChildren())
	{
		RecusiveDelete(child->Parent, child.get());
	}
	ent->MarkForDelete();
}

void CommonUtils::DoComponentRecursive(const FolderTest& currentFolder, const EntityHandle& entity)
{
	for (auto& entry : currentFolder.Folders)
	{
		if (ImGui::BeginMenu(entry.first.c_str()))
		{
			DoComponentRecursive(entry.second, entity);
			ImGui::EndMenu();
		}
	}
	for (auto& ptr : currentFolder.Reg)
	{
		if (ImGui::Selectable(ptr.first.c_str()))
		{
			if (entity)
			{
				entity->AddComponentByName(ptr.first);
				//AddComponentCommand* compCmd = new AddComponentCommand(ptr.first, entity);
				//EditorCommands.Push(compCmd);
			}
			/*if (SelectedTransform)
			{
				m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
			}*/
		}
	}
}

void CommonUtils::DrawAddComponentList(const EntityHandle& entity)
{
	ImGui::Text("Components");
	ImGui::Separator();
	std::map<std::string, FolderTest> folders;
	ComponentRegistry& reg = GetComponentRegistry();

	for (auto& thing : reg)
	{
		if (thing.second.Folder == "")
		{
			folders[""].Reg[thing.first] = &thing.second;
		}
		else
		{
			/*auto it = folders.at(thing.second.Folder);
			if (it == folders.end())
			{

			}*/
			std::string folderPath = thing.second.Folder;
			std::size_t pos = folderPath.rfind("/");
			if (pos == std::string::npos)
			{
				folders[thing.second.Folder].Reg[thing.first] = &thing.second;
			}
			else
			{
				FolderTest& test = folders[thing.second.Folder.substr(0, pos)];
				while (pos != std::string::npos)
				{
					pos = folderPath.rfind("/");
					if (pos == std::string::npos)
					{
						test.Folders[folderPath].Reg[thing.first] = &thing.second;
					}
					else
					{
						test = folders[folderPath.substr(0, pos)];
						folderPath = folderPath.substr(pos + 1, folderPath.size());
					}
				}
			}
		}
	}

	for (auto& thing : folders)
	{
		if (thing.first != "")
		{
			if (ImGui::BeginMenu(thing.first.c_str()))
			{
				DoComponentRecursive(thing.second, entity);
				ImGui::EndMenu();
			}
		}
		else
		{
			for (auto& ptr : thing.second.Reg)
			{
				if (ImGui::Selectable(ptr.first.c_str()))
				{
					if (entity)
					{
						entity->AddComponentByName(ptr.first);
						//AddComponentCommand* compCmd = new AddComponentCommand(ptr.first, entity);
						//EditorCommands.Push(compCmd);
					}
					/*if (SelectedTransform)
					{
						m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
					}*/
				}
			}
		}
	}

	//for (auto& thing : reg)
	//{
	//	if (ImGui::Selectable(thing.first.c_str()))
	//	{
	//		if (entity)
	//		{
	//			AddComponentCommand* compCmd = new AddComponentCommand(thing.first, entity);
	//			EditorCommands.Push(compCmd);
	//		}
	//		/*if (SelectedTransform)
	//		{
	//			m_engine->GetWorld().lock()->GetEntity(SelectedTransform->Parent).lock()->AddComponentByName(thing.first);
	//		}*/
	//	}
	//}
}
void CommonUtils::SerializeEntity(json& outEntity, Transform* CurrentTransform)
{
	OPTICK_EVENT("SceneGraph::UpdateRecursively");

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
			SerializeEntity(outEntity["Children"], Child.get());
		}
	}
}

EntityHandle CommonUtils::DeserializeEntity(const json& obj, Transform* parent)
{
	std::string thingg = obj.dump(4);
	EntityHandle ent;
	if (parent)
	{
		auto t = parent->GetChildByName(obj["Name"]);
		if (t)
		{
			// change name cause already exists
			//ent = t->Parent;
		}
	}
	if (!ent)
	{
		ent = GetEngine().GetWorld().lock()->CreateEntity();
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
			DeserializeEntity(child, transComp);
		}
	}
	return ent;
}

void CommonUtils::DuplicateEntity(const EntityHandle& entity)
{
	if (entity->HasComponent<Transform>())
	{
		json j;
		SerializeEntity(j, &entity->GetComponent<Transform>());

		EntityHandle handle = DeserializeEntity(j, nullptr);

		InspectEvent evt;
		evt.SelectedEntity = handle;
		evt.Fire();
	}
}

#endif