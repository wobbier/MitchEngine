#pragma once
#include <Path.h>
#include <File.h>
#include <JSON.h>

class Config
{
public:
	Config(const Path& ConfigPath);
	~Config();

	std::string GetValue(const std::string& value);
	const json& GetJsonObject(const std::string& value);

	void SetValue(const std::string& key, std::string& newVal);

	void Save();

private:
	json Root;
	File ConfigFile;
};
