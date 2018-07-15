#include "PCH.h"
#include "Config.h"
#include <iostream>

Config::Config(const char* file)
{
	std::ifstream ConfigFile = std::ifstream(file);
	bool parsingSuccessful = false; // Reader.parse(ConfigFile, Root);
	if (!parsingSuccessful)
	{
		//std::cout << "Failed to parse configuration\n" << Reader.getFormattedErrorMessages();
		return;
	}
}

Config::~Config()
{
}

std::string Config::GetValue(std::string value)
{
	return "";// Root[value].asString();
}