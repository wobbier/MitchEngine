#pragma once
#include <string>
#include "Utility/Util.h"
#include "Engine/Resource.h"

class Cubemap : public Resource
{
public:
	unsigned int Id;
	std::string Type;
	std::string Directory;

	int Width;
	int Height;

	Cubemap();
	~Cubemap();

	static Cubemap* Load(const std::string& InFilePath);

	// Textures should not be copied around in memory
	MA_NONCOPYABLE(Cubemap);
	int nrChannels;
};
