#include "AssetBrowser.h"
#include <filesystem>
#include <thread>

AssetBrowser::AssetBrowser()
{

}

AssetBrowser::AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay)
	: PathToWatch(pathToWatch)
	, Delay(delay)
{
	for (auto& file : std::filesystem::recursive_directory_iterator(PathToWatch))
	{
		Paths[file.path().string()] = std::filesystem::last_write_time(file);
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

bool AssetBrowser::Contains(const std::string& key)
{
	return Paths.find(key) != Paths.end();
}

