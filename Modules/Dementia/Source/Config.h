#pragma once
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

// for convenience
using json = nlohmann::json;

class Config
{
public:
	Config(const char* file);
	~Config();
	std::string GetValue(std::string value);
	json Root;
};
