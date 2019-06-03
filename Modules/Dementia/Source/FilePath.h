#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include "Dementia.h"

class FilePath
{
public:
	FilePath() = default;

	explicit FilePath(const std::string& InFile)
	{
		char buf[1024];
		GetModuleFileNameA(NULL, buf, 1024);
		std::string ProgramPath(buf);

		std::replace(ProgramPath.begin(), ProgramPath.end(), '\\', '/');
		size_t pos = ProgramPath.find_last_of("/");
		Directory = ProgramPath.substr(0, pos + 1);

		LocalPath = InFile;

		std::replace(LocalPath.begin(), LocalPath.end(), '\\', '/');

		size_t path = LocalPath.find(':');
		if (path != std::string::npos)
		{
			FullPath = LocalPath;
		}
		else
		{
			FullPath = Directory + LocalPath;
		}
		path = LocalPath.rfind("Assets");
		if (path != std::string::npos)
		{
			LocalPath = LocalPath.substr(path, LocalPath.size());
		}

		pos = FullPath.find_last_of("/");
		Directory = FullPath.substr(0, pos + 1);

#if ME_PLATFORM_UWP
		std::replace(LocalPath.begin(), LocalPath.end(), '/', '\\');
#endif
	}

	~FilePath()
	{

	}

	std::string FullPath;
	std::string LocalPath;
	std::string Directory;
};