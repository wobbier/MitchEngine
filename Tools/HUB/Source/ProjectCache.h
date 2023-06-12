#pragma once
#include <string>
#include <Path.h>
#include <Pointers.h>
#include <vector>
#include "Graphics\Texture.h"

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

	SharedPtr<Moonlight::Texture> GetActiveBackgroundTexture( size_t SelectedProjectIndex );
	SharedPtr<Moonlight::Texture> GetActiveTitleTexture( size_t SelectedProjectIndex );

	std::vector<ProjectEntry> Projects;
};

