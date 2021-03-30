#include "Config.h"

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
	if (Root.contains(value))
	{
		return Root[value];
	}
	return "";
}

const json& Config::GetJsonObject(const std::string& value)
{
	if (Root.contains(value))
	{
		return Root[value];
	}
	return Root;
}

void Config::SetValue(const std::string& key, std::string& newVal)
{
	Root[key] = newVal;
}

void Config::Save()
{
	ConfigFile.Write(Root.dump(4));
}
