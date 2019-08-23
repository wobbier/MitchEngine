#include "Config.h"
#include <iostream>

Config::Config(const Path& ConfigPath)
	: ConfigFile(ConfigPath)
{
	Root = json::parse(ConfigFile.Read());
	if (Root.is_null())
	{
		YIKES("Failed to parse configuration.");
		return;
	}
}

Config::~Config()
{
}

std::string Config::GetValue(const std::string& value)
{
	return Root[value];
}

const json& Config::GetObject(const std::string& value)
{
	if (Root.contains(value))
	{
		return Root[value];
	}
	return Root;
}
