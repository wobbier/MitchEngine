#include "AssetBrowser.h"
#include <filesystem>
#include <thread>
#include "imgui.h"
#include <stack>
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "File.h"
//#include <stringapiset.h>
#include "Utils/StringUtils.h"
#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "Events/SceneEvents.h"
#include "HavanaEvents.h"
#include "optick.h"
#include "Utils/ImGuiUtils.h"
#include <Utils/CommonUtils.h>

AssetBrowser::AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay)
	: PathToWatch(pathToWatch)
	, Delay(delay)
{
	Icons["Image"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Image.png"));
	Icons["File"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/File.png"));
	Icons["Terrain"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Terrain.png"));
	Icons["World"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/World.png"));
	Icons["Audio"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Audio.png"));
	Icons["Prefab"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Prefab.png"));

	AssetDirectory.FullPath = Path(PathToWatch);
	for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
	{
		Paths[file.path().string()] = std::filesystem::last_write_time(file);
		ProccessDirectory(file);
	}
}

AssetBrowser::~AssetBrowser()
{

}

void AssetBrowser::Start(const std::function<void(std::string, FileStatus)>& action)
{
	fileBrowser = std::thread([this, action]() {ThreadStart(action); });
}

void AssetBrowser::ThreadStart(const std::function<void(std::string, FileStatus)>& action)
{
	while (IsRunning)
	{
		std::this_thread::sleep_for(Delay);

		bool WasModified = false;
		auto it = Paths.begin();
		while (it != Paths.end())
		{
			if (!std::filesystem::exists(it->first))
			{
				action(it->first, FileStatus::Deleted);
				it = Paths.erase(it);
				WasModified = true;
			}
			else
			{
				it++;
			}
		}

		for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
		{
			auto currentFileLastWriteTime = std::filesystem::last_write_time(file);

			if (!Contains(file.path().string()))
			{
				Paths[file.path().string()] = currentFileLastWriteTime;
				action(file.path().string(), FileStatus::Created);
				WasModified = true;
			}
			else
			{
				if (Paths[file.path().string()] != currentFileLastWriteTime)
				{
					Paths[file.path().string()] = currentFileLastWriteTime;
					action(file.path().string(), FileStatus::Modified);
					WasModified = true;
				}
			}
		}

		if (WasModified)
		{
			Paths.clear();
			AssetDirectory = Directory();
			for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
			{
				Paths[file.path().string()] = std::filesystem::last_write_time(file);
				ProccessDirectory(file);
			}
		}
	}
}

void AssetBrowser::Draw()
{
	OPTICK_CATEGORY("Asset Browser", Optick::Category::Debug);
	ImGui::Begin("Assets");

	std::stack<std::string> directories;
	directories.push("Assets");

	//if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
	{
		Recursive(AssetDirectory);
	}

	ImGui::End();
}

void AssetBrowser::Recursive(Directory& dir)
{
	return;
	for (auto& directory : dir.Directories)
	{
		bool node_open = ImGui::TreeNode(directory.first.c_str());
		if (ImGui::IsItemClicked())
		{
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
			{
				IM_ASSERT(payload->DataSize == sizeof(ParentDescriptor));
				ParentDescriptor* payload_n = (ParentDescriptor*)payload->Data;

				json prefab;
				SavePrefab(prefab, payload_n->Parent, true);

				File(Path(directory.second.FullPath.Directory + "/" + payload_n->Parent->Name + std::string(".prefab"))).Write(prefab[0].dump(4));
			}
			ImGui::EndDragDropTarget();
		}
		if (node_open)
		{
			// we have a subdir
			Recursive(directory.second);
			ImGui::TreePop();
		}
	}
	int i = 0;
	for (AssetDescriptor& files : dir.Files)
	{
		switch (files.Type)
		{
		case AssetType::Level:
			ImGui::Image(Icons["World"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Texture:
			ImGui::Image(Icons["Image"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Model:
			ImGui::Image(Icons["Terrain"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Audio:
			ImGui::Image(Icons["Audio"]->TexHandle, ImVec2(16, 16));
			break;
		case AssetType::Prefab:
			ImGui::Image(Icons["Prefab"]->TexHandle, ImVec2(16, 16));
			break;
		default:
			ImGui::Image(Icons["File"]->TexHandle, ImVec2(16, 16));
			break;
		}

		ImGui::SameLine();
		//ImGui::Text(files.Name.c_str());
		int node_clicked = -1;
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow;
		node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-5, 3));
		ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, files.Name.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedAsset = &files;
			node_clicked = i;
			if (ImGui::IsMouseDoubleClicked(0))
			{
				if (files.FullPath.LocalPath.rfind(".lvl") != std::string::npos)
				{
					LoadSceneEvent evt;
					evt.Level = files.FullPath.LocalPath;
					evt.Fire();
				}
				else
				{
#if ME_PLATFORM_WIN64
				ShellExecute(NULL, L"open", StringUtils::ToWString(SelectedAsset->FullPath.FullPath).c_str(), NULL, NULL, SW_SHOWDEFAULT);
#endif
				}
			}
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			//files.FullPath = dir.FullPath;
			ImGui::SetDragDropPayload("DND_ASSET_BROWSER", &files, sizeof(AssetDescriptor));
			ImGui::Text(files.Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CHILD_TRANSFORM"))
			{
				IM_ASSERT(payload->DataSize == sizeof(ParentDescriptor));
				ParentDescriptor* payload_n = (ParentDescriptor*)payload->Data;

				json prefab;
				SavePrefab(prefab, payload_n->Parent, true);
				
				File(Path(files.FullPath.Directory + payload_n->Parent->Name + std::string(".prefab"))).Write(prefab[0].dump(4));
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopStyleVar(2);
		i++;
	}
}

void AssetBrowser::ProccessDirectory(const std::filesystem::directory_entry& file)
{
	std::string& parentDir = AssetDirectory.FullPath.FullPath;
	std::size_t t = file.path().string().find(parentDir);
	if (t != std::string::npos)
	{
		std::string dir2 = file.path().string().substr(t, file.path().string().length());

		ProccessDirectoryRecursive(dir2, AssetDirectory, file);

	}
}

bool AssetBrowser::ProccessDirectoryRecursive(std::string& dir, Directory& dirRef, const std::filesystem::directory_entry& file)
{
	std::size_t d = dir.find_first_of('\\');
	if (d != std::string::npos)
	{
		std::string newdir = dir.substr(d + 1, dir.length());
		std::size_t d2 = newdir.find_first_of('\\');
		if (d2 != std::string::npos)
		{
			std::string foldername = newdir.substr(0, d2);
			if (dirRef.Directories.find(foldername) != dirRef.Directories.end())
			{
				return ProccessDirectoryRecursive(newdir, dirRef.Directories[foldername], file);
			}
			else
			{
				Directory newDirectory;
				newDirectory.FullPath = Path(newdir);
				dirRef.Directories[foldername] = newDirectory;
				return true;
			}
		}
		else
		{
			if (file.is_regular_file())
			{
				if (newdir.find(".meta") != std::string::npos)
				{
					return false;
				}
				// we have a file

				AssetType type = AssetType::Unknown;
				if (newdir.rfind(".png") != std::string::npos || newdir.rfind(".jpg") != std::string::npos || newdir.rfind(".tif") != std::string::npos)
				{
					type = AssetType::Texture;
				}
				else if (newdir.rfind(".lvl") != std::string::npos)
				{
					type = AssetType::Level;
				}
				else if (newdir.rfind(".prefab") != std::string::npos)
				{
					type = AssetType::Prefab;
				}
				else if (newdir.rfind(".wav") != std::string::npos || newdir.rfind(".mp3") != std::string::npos)
				{
					type = AssetType::Audio;
				}
				else if (newdir.rfind(".obj") != std::string::npos || newdir.rfind(".fbx") != std::string::npos || newdir.rfind(".FBX") != std::string::npos)
				{
					type = AssetType::Model;
				}

				AssetDescriptor desc;
				desc.Name = newdir;
				//desc.MetaFile = File(Path(file.path().string() + ".meta"));
				desc.FullPath = Path(file.path().string());
				desc.Type = type;
				dirRef.Files.push_back(desc);
				//const std::string & data = dirRef.Files.back().MetaFile.Read();
				//if (data.empty())
				//{
				//	//dirRef.Files.back().MetaFile.Write("{}");
				//}
				//else
				//{

				//}
				return true;
			}
			Directory newDirectory;
			newDirectory.FullPath = Path(dir);
			dirRef.Directories[newdir] = newDirectory;
			return true;
		}
	}

	Directory newDirectory;
	newDirectory.FullPath = Path(dir);
	dirRef.Directories[dir] = newDirectory;
	return false;
}

void AssetBrowser::SavePrefab(json& d, Transform* CurrentTransform, bool IsRoot)
{
	json newJson;
	/*json& refJson = d;

	if (!IsRoot)
	{
		refJson = newJson;
	}*/

	newJson["Name"] = CurrentTransform->Name;

	json& componentsJson = newJson["Components"];
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
			SavePrefab(newJson["Children"], Child.get(), false);
		}
	}

	d.push_back(newJson);
}

bool AssetBrowser::Contains(const std::string& key)
{
	return Paths.find(key) != Paths.end();
}

