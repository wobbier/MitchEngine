#pragma once
#include <wrl/client.h>

#include <string>
#include <algorithm>
#include "Dementia.h"
#include <fstream>
#include "Logger.h"

#if ME_EDITOR
#include <filesystem>
#endif

class Path
{
public:
	Path() = default;

	explicit Path(const std::string& InFile, bool Raw = false)
	{
		char buf[1024];
		GetModuleFileNameA(NULL, buf, 1024);
		std::string ProgramPath(buf);

		std::replace(ProgramPath.begin(), ProgramPath.end(), '\\', '/');
		size_t pos = ProgramPath.find_last_of("/");
		ProgramPath = ProgramPath.substr(0, pos + 1);

		std::string assetPrefix;
#if ME_EDITOR
		assetPrefix = "../../";
#endif
		LocalPath = InFile;

		std::replace(LocalPath.begin(), LocalPath.end(), '\\', '/');

		size_t path = LocalPath.find(':');
		if (path != std::string::npos)
		{
			FullPath = LocalPath;
		}
		else
		{
			FullPath = ProgramPath + assetPrefix + LocalPath;
		}

		path = LocalPath.rfind("Assets");
		if (path != std::string::npos)
		{
			LocalPath = LocalPath.substr(path, LocalPath.size());
		}

#if ME_EDITOR
		if (!std::filesystem::exists(FullPath))
		{
			if (!Raw)
			{
				std::string tempPath = ProgramPath + assetPrefix + "Engine/" + LocalPath;
				if (std::filesystem::exists(tempPath))
				{
					FullPath = std::move(tempPath);
					assetPrefix = assetPrefix.append("Engine/");
					Exists = true;
				}
			}
		}
		else
		{
			Exists = true;
		}

		if (std::filesystem::is_regular_file(FullPath))
		{
			IsFile = true;
		}
		else
		{
			IsFolder = true;
		}
#else

		// Rough till I look up how UWP validates files
		Exists = true;
#endif
		pos = FullPath.find_last_of("/");
		Directory = FullPath.substr(0, pos + 1);

#if ME_PLATFORM_UWP
		std::replace(LocalPath.begin(), LocalPath.end(), '/', '\\');
		FullPath = LocalPath;
#endif
	}

	~Path()
	{

	}
	bool IsFile = false;
	bool IsFolder = false;
	bool Exists = false;
	std::string FullPath;
	std::string LocalPath;
	std::string Directory;
};