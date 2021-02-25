#pragma once
#include <nlohmann/json.hpp>

#include <Path.h>
#include <File.h>

class Config
{
public:
	Config(const Path& ConfigPath);
	~Config();

	std::string GetValue(const std::string& value);
	const nlohmann::json& GetJsonObject(const std::string& value);

	void SetValue(std::string& key, std::string& newVal);

	void Save();

private:
	nlohmann::json Root;
	File ConfigFile;
};
