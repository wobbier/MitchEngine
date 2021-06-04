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

	MetaBase(const Path& filePath);

	void Serialize(json& outJson);
	void Deserialize(const json& inJson);

	virtual void Export() {	}

	virtual std::string GetExtension2() const = 0;

	void Save();

	virtual void OnSerialize(json& outJson) = 0;
	virtual void OnDeserialize(const json& inJson) = 0;

#if ME_EDITOR
	virtual void OnEditorInspect() {
		ImGui::Text("File Type: ");
		ImGui::SameLine();
        ImGui::Text("%s", FileType.c_str());
		ImGui::Text("Last Modified: ");
		ImGui::SameLine();
        ImGui::Text("%s", LastModifiedDebug.c_str());
	}
#endif

	std::string FileType;
	long LastModified = 0;
	std::string LastModifiedDebug;

	bool FlaggedForExport = false;

	Path FilePath;
};
