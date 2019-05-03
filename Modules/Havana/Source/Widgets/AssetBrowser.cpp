#include "AssetBrowser.h"
#include <filesystem>
#include <thread>
#include "imgui.h"
#include <stack>
#include "FilePath.h"
#include "Resource/ResourceCache.h"
#include "Graphics/Texture.h"

AssetBrowser::AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay)
	: PathToWatch(pathToWatch)
	, Delay(delay)
{
	Icons["Image"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(FilePath("Assets/Havana/UI/Image.png"));
	Icons["File"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(FilePath("Assets/Havana/UI/File.png"));
	Icons["Terrain"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(FilePath("Assets/Havana/UI/Terrain.png"));
	Icons["World"] = ResourceCache::GetInstance().Get<Moonlight::Texture>(FilePath("Assets/Havana/UI/World.png"));

	AssetDirectory.FullPath = FilePath(PathToWatch);
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
	for (auto& files : dir.Files)
	{
		// we have a file
		if (files.find(".png") != std::string::npos || files.find(".jpg") != std::string::npos)
		{
			ImGui::Image(Icons["Image"]->CubesTexture, ImVec2(16, 16));
		}
		else if(files.find(".lvl") != std::string::npos)
		{
			ImGui::Image(Icons["World"]->CubesTexture, ImVec2(16, 16));
		}
		else if (files.find(".obj") != std::string::npos || files.find(".fbx") != std::string::npos)
		{
			ImGui::Image(Icons["Terrain"]->CubesTexture, ImVec2(16, 16));
		}
		else
		{
			ImGui::Image(Icons["File"]->CubesTexture, ImVec2(16, 16));
		}
		ImGui::SameLine();
		ImGui::Text(files.c_str());
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
				newDirectory.FullPath = FilePath(newdir);
				dirRef.Directories[foldername] = newDirectory;
				return true;
			}
		}
		else
		{
			if (file.is_regular_file())
			{
				dirRef.Files.push_back(newdir);
				return true;
			}
			Directory newDirectory;
			newDirectory.FullPath = FilePath(dir);
			dirRef.Directories[newdir] = newDirectory;
			return true;
		}
	}

	Directory newDirectory;
	newDirectory.FullPath = FilePath(dir);
	dirRef.Directories[dir] = newDirectory;
	return false;
}

bool AssetBrowser::Contains(const std::string& key)
{
	return Paths.find(key) != Paths.end();
}

