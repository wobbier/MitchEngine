#pragma once
#include <string>
//#include <GL/glew.h>
#include "SOIL.h"
#include "Utility/Util.h"
#include "Engine/Resource.h"

class Texture : public Resource
{
public:
	//GLuint Id;
	std::string Type;
	std::string Directory;

	int Width;
	int Height;

	Texture();
	~Texture();

	static Texture* Load(const std::string& InFilePath);

	// Textures should not be copied around in memory
	MA_NONCOPYABLE(Texture);
};
