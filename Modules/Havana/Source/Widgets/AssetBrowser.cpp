#include "AssetBrowser.h"
#include <filesystem>
#include <thread>
#include "imgui.h"
#include <stack>
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"
#include "File.h"

AssetBrowser::AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay)
	: PathToWatch(pathToWatch)
	, Delay(delay)
{
	Icons["Image"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Image.png"));
	Icons["File"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/File.png"));
	Icons["Terrain"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/Terrain.png"));
	Icons["World"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("Assets/Havana/UI/World.png"));

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

		auto it = Paths.begin();
		while (it != Paths.end())
		{
			if (!std::filesystem::exists(it->first))
			{
				action(it->first, FileStatus::Deleted);
				it = Paths.erase(it);
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
			}
			else
			{
				if (Paths[file.path().string()] != currentFileLastWriteTime)
				{
					Paths[file.path().string()] = currentFileLastWriteTime;
					action(file.path().string(), FileStatus::Modified);
				}
			}
		}
	}
}

void AssetBrowser::Draw()
{
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
	for (auto& directory : dir.Directories)
	{
		bool node_open = ImGui::TreeNode(directory.first.c_str());
		if (ImGui::IsItemClicked())
		{
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
			ImGui::Image(Icons["World"]->ShaderResourceView, ImVec2(16, 16));
			break;
		case AssetType::Texture:
			ImGui::Image(Icons["Image"]->ShaderResourceView, ImVec2(16, 16));
			break;
		case AssetType::Model:
			ImGui::Image(Icons["Terrain"]->ShaderResourceView, ImVec2(16, 16));
			break;
		default:
			ImGui::Image(Icons["File"]->ShaderResourceView, ImVec2(16, 16));
			break;
		}

		ImGui::SameLine();
		//ImGui::Text(files.Name.c_str());
		int node_clicked = -1;
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
		node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(-5, 3));
		ImGui::TreeNodeEx((void*)(intptr_t)i, node_flags, files.Name.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedAsset = &files;
			node_clicked = i;
		}

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			//files.FullPath = dir.FullPath;
			ImGui::SetDragDropPayload("DND_DEMO_CELL", &files, sizeof(AssetDescriptor));
			ImGui::Text(files.Name.c_str());
			ImGui::EndDragDropSource();
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
				if (newdir.find(".png") != std::string::npos || newdir.find(".jpg") != std::string::npos)
				{
					type = AssetType::Texture;
				}
				else if (newdir.find(".lvl") != std::string::npos)
				{
					type = AssetType::Level;
				}
				else if (newdir.find(".obj") != std::string::npos || newdir.find(".fbx") != std::string::npos)
				{
					type = AssetType::Model;
				}

				AssetDescriptor desc;
				desc.Name = newdir;
				desc.MetaFile = File(Path(file.path().string() + ".meta"));
				desc.FullPath = Path(file.path().string());
				desc.Type = type;
				dirRef.Files.push_back(desc);
				const std::string & data = dirRef.Files.back().MetaFile.Read();
				if (data.empty())
				{
					dirRef.Files.back().MetaFile.Write("{}");
				}
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

bool AssetBrowser::Contains(const std::string& key)
{
	return Paths.find(key) != Paths.end();
}

