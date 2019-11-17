#include "Config.h"
#include <iostream>

Config::Config(const Path& ConfigPath)
	: ConfigFile(ConfigPath)
{
	Root = nlohmann::json::parse(ConfigFile.Read());
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
	if (Root.contains(value))
	{
		return Root[value];
	}
	return "";
}

const nlohmann::json& Config::GetObject(const std::string& value)
{
	if (Root.contains(value))
	{
		return Root[value];
	}
	return Root;
}

void Config::SetValue(std::string& key, std::string& newVal)
{
	Root[key] = newVal;
}

void Config::Save()
{
	ConfigFile.Write(Root.dump(4));
}