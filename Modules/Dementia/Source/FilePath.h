#pragma once
#include <windows.h>
#include <string>
#include <algorithm>
#include "Dementia.h"

class FilePath
{
public:
	FilePath(const std::string& InFile)
	{
		char buf[1024];
		GetModuleFileNameA(NULL, buf, 1024);
		std::string ProgramPath(buf);

		std::replace(ProgramPath.begin(), ProgramPath.end(), '\\', '/');
		std::size_t found = ProgramPath.rfind("/../");
		if (found != std::string::npos)
		{
			Directory = ProgramPath.substr(0, found);
			found = Directory.rfind('/', Directory.length());
			if (found != std::string::npos)
			{
				Directory = Directory.substr(0, found + 1);
			}
		}
		else
		{
			size_t pos = ProgramPath.find_last_of("/");
			Directory = ProgramPath.substr(0, pos + 1);
		}

		LocalPath = InFile;
		std::replace(LocalPath.begin(), LocalPath.end(), '\\', '/');

		size_t path = LocalPath.find(Directory);
		if (path != std::string::npos)
		{
			LocalPath.erase(path, Directory.length());
		}

		FullPath = Directory + LocalPath;

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