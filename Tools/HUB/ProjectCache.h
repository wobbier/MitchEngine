#pragma once
#include <string>
#include <Path.h>
#include <Pointers.h>
#include <vector>

namespace Moonlight { class Texture; }

struct ProjectEntry
{
	std::string Name;
	Path ProjectPath;

	SharedPtr<Moonlight::Texture> TitleImage;
	SharedPtr<Moonlight::Texture> BackgroundImage;
};

struct ProjectCache
{
	static constexpr const char* kCachePath = ".tmp/ProjectCache.json";
	ProjectCache() = default;

	void Load();
	void Save();

	std::vector<ProjectEntry> Projects;
};

