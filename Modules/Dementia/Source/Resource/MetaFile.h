#pragma once
#include "JSON.h"
#include <filesystem>
#include <time.h>
#include <chrono>
using namespace std::chrono_literals;

#if ME_EDITOR
#include <imgui.h>
#endif
#include "Path.h"

struct MetaBase
{
	MetaBase() = delete;

	MetaBase(const Path& filePath)
	: FilePath(filePath)
	{
		struct stat fileInfo;

		if (stat(filePath.FullPath.c_str(), &fileInfo) != 0) {  // Use stat() to get the info
			//std::cerr << "Error: " << strerror(errno) << '\n';
		}

		//std::cout << "Type:         : ";
		//if ((fileInfo.st_mode & S_IFMT) == S_IFDIR) { // From sys/types.h
		//	std::cout << "Directory\n";
		//}
		//else {
		//	std::cout << "File\n";
		//}

		//std::cout << "Size          : " <<
		//	fileInfo.st_size << '\n';               // Size in bytes
		//std::cout << "Device        : " <<
		//	(char)(fileInfo.st_dev + 'A') << '\n';  // Device number
		//std::cout << "Created       : " <<
		//	std::ctime(&fileInfo.st_ctime);         // Creation time
		//std::cout << "Modified      : " <<
		//	std::ctime(&fileInfo.st_mtime);         // Last mod time

		LastModified = static_cast<long>(fileInfo.st_mtime);
		char str[26];
		ctime_s(str, sizeof str, &fileInfo.st_mtime);
		LastModifiedDebug = std::string(str);// std::ctime(&fileInfo.st_mtime);
	}

	void Serialize(json& outJson)
	{
		outJson["FileType"] = FilePath.Extension;
		outJson["LastModified"] = LastModified;
		outJson["LastModifiedDebug"] = LastModifiedDebug;
		//outJson["LastModified"] = buffer;
		OnSerialize(outJson);
	}

	void Deserialize(const json& inJson)
	{
		if (inJson.contains("FileType"))
		{
			FileType = inJson["FileType"];
		}

		bool wasModified = true;
		if (inJson.contains("LastModified"))
		{
			long CachedLastModified = inJson["LastModified"];
			LastModified = CachedLastModified;
			wasModified = (LastModified != CachedLastModified);
		}
		FlaggedForExport = wasModified;

		if (inJson.contains("LastModifiedDebug"))
		{
			LastModifiedDebug = inJson["LastModifiedDebug"];
		}
		OnDeserialize(inJson);
	}

	virtual void Export()
	{
	}

	virtual void OnSerialize(json& outJson) = 0;
	virtual void OnDeserialize(const json& inJson) = 0;

#if ME_EDITOR
	virtual void OnEditorInspect() {
		ImGui::Text("File Type: ");
		ImGui::SameLine();
		ImGui::Text(FileType.c_str());
		ImGui::Text("Last Modified: ");
		ImGui::SameLine();
		ImGui::Text(LastModifiedDebug.c_str());
	}
#endif

	std::string FileType;
	long LastModified = 0;
	std::string LastModifiedDebug;

	bool FlaggedForExport = false;

	Path FilePath;
};