#include "PCH.h"
#include <iostream>
#include <assert.h>
#include "Graphics/Cubemap.h"
#include "Utility/Logger.h"
/*
#include <glad/glad.h>
#include <GLFW/glfw3.h>
*/
#include <stb_image.h>

Cubemap::Cubemap() : Resource()
{
}

Cubemap::~Cubemap()
{
	// TODO: Unload Cubemaps
}

Cubemap* Cubemap::Load(const std::string& InFilePath)
{
	Cubemap* LoadedCubemap = new Cubemap();
	LoadedCubemap->Directory = InFilePath.substr(0, InFilePath.find_last_of('/'));
	/*
	// Generate Cubemap ID and load Cubemap data
	glGenTextures(1, &LoadedCubemap->Id);
	glBindTexture(GL_TEXTURE_CUBE_MAP, LoadedCubemap->Id);
	*/

	std::vector<std::string> faces
	{
		"_right.jpg",
			"_left.jpg",
			"_top.jpg",
			"_bottom.jpg",
			"_front.jpg",
			"_back.jpg"
	};

	//for (unsigned int i = 0; i < faces.size(); i++)
	//{
	//	unsigned char* image = stbi_load((InFilePath + faces[i]).c_str(), &LoadedCubemap->Width, &LoadedCubemap->Height, &LoadedCubemap->nrChannels, 0);
	//	if (image)
	//	{
	//		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, LoadedCubemap->Width, LoadedCubemap->Height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
	//	}
	//	else
	//	{
	//		Logger::GetInstance().Log(Logger::LogType::Error, "Failed to load Cubemap: " + InFilePath);
	//		assert(0);
	//	}
	//	stbi_image_free(image);
	//}
	//// Parameters
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	return LoadedCubemap;
}