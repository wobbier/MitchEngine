#pragma once
#include <string>
#include <processenv.h>
#include "Dementia.h"

class FilePath
{
public:
	FilePath(std::string InFile)
	{
#if ME_PLATFORM_WIN64
		char buf[256];
		GetCurrentDirectoryA(256, buf);
		Path = std::string(buf) + '\\';
#endif
	}

	~FilePath()
	{

	}

	std::string Path;
};