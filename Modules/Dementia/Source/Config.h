#pragma once
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "Path.h"
#include "File.h"

// for convenience
using json = nlohmann::json;

class Config
{
public:
	Config(const Path& ConfigPath);
	~Config();

	std::string GetValue(const std::string& value);
	const json& GetObject(const std::string& value);

private:
	json Root;
	File ConfigFile;
};
