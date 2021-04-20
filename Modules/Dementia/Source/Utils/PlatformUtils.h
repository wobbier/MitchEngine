#pragma once

#include "StringUtils.h"
#include <Path.h>

namespace PlatformUtils
{
	void RunProcess(const Path& inFilePath, const std::string& inArgs = "");

	void SystemCall(const Path& inFilePath, const std::string& inArgs = "", bool inRunFromDirectory = true);

	void CreateDirectory(const Path& inFilePath);
}