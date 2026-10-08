#include <Utils/CommonUtils.h>
#include <optick.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Events/HavanaEvents.h>
#include "World/SceneSerializer.h"

#if USING( ME_EDITOR )

void CommonUtils::RecusiveDelete(EntityHandle ent, Transform* trans)
{
	if (!trans)
	{
		return;
	}
	// MarkForDelete destroys the whole subtree at the next sync point.
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
	OPTICK_EVENT("CommonUtils::SerializeEntity");
	World& world = *GetEngine().GetWorld().lock();
	outEntity = SceneSerializer::SerializeEntities(world, { CurrentTransform->Parent.Get() });
}

EntityHandle CommonUtils::DeserializeEntity(const json& obj, Transform* parent)
{
	World& world = *GetEngine().GetWorld().lock();
	SceneSerializer::LoadOptions options;
	options.RemapGUIDs = true;
	options.LoadCores = false;
	options.Parent = parent;
	std::vector<EntityHandle> roots = SceneSerializer::Deserialize(world, obj, options);
	return roots.empty() ? EntityHandle() : roots.front();
}

void CommonUtils::DuplicateEntity(const EntityHandle& entity)
{
	if (!entity || !entity->HasComponent<Transform>())
	{
		return;
	}

	Transform& source = entity->GetComponent<Transform>();
	json j;
	SerializeEntity(j, &source);

	// Same parent, placed right after the original.
	EntityHandle handle = DeserializeEntity(j, source.GetParentTransform());
	if (handle && source.GetParentTransform())
	{
		handle->GetComponent<Transform>().SetSiblingIndex(source.GetSiblingIndex() + 1);
	}

	InspectEvent evt;
	evt.SelectedEntity = handle;
	evt.Fire();
}

#endif