#include <Utils/CommonUtils.h>

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

#endif