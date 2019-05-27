#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <thread>
#include <stack>
#include <map>
#include "FilePath.h"
#include "Pointers.h"
#include "Graphics/Texture.h"
#include "File.h"

enum class FileStatus : unsigned int
{
	Created = 0,
	Modified,
	Deleted
};

class AssetBrowser
{
	struct AssetDescriptor
	{
		std::string Name;
		File MetaFile;
	};
	struct Directory 
	{
		std::map<std::string, Directory> Directories;
		std::vector<AssetBrowser::AssetDescriptor> Files;
		FilePath FullPath;
	};
public:
	AssetBrowser() = delete;
	AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay);
	~AssetBrowser();
	void Start(const std::function<void(std::string, FileStatus)>& action);
	std::chrono::duration<int, std::milli> Delay;
	std::string PathToWatch;

	void ThreadStart(const std::function<void(std::string, FileStatus)>& action);

	void Draw();

	void Recursive(Directory& dir);
	void ProccessDirectory(const std::filesystem::directory_entry& file);
private:
	bool ProccessDirectoryRecursive(std::string& dir, Directory& dirRef, const std::filesystem::directory_entry& file);
	std::unordered_map<std::string, std::filesystem::file_time_type> Paths;
	bool IsRunning = true;
	bool Contains(const std::string& key);
	std::thread fileBrowser;
	Directory AssetDirectory;
	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
};