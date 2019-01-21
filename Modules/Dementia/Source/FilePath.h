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
		std::size_t found = ProgramPath.rfind("../");
		if (found != std::string::npos)
		{
			Directory = ProgramPath.substr(0, found + 3);
		}
		else
		{
			size_t pos = ProgramPath.find_last_of("/");
			Directory = ProgramPath.substr(0, pos + 1);
		}

		FullPath = Directory + InFile;
		LocalPath = InFile;

#if ME_PLATFORM_UWP
		std::replace(LocalPath.begin(), LocalPath.end(), '/', '\\');
#endif
		/*std::string file = InFile;
		std::replace(file.begin(), file.end(), '/', '\\');
		FullPath = file;*/
	}

	~FilePath()
	{

	}

	std::string FullPath;
	std::string LocalPath;
	std::string Directory;
};