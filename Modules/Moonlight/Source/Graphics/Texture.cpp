#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "Logger.h"

#if ME_OPENGL
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

Texture::Texture() : Resource()
{
}

Texture::~Texture()
{
	// TODO: Unload textures
}

Texture* Texture::Load(const std::string& InFilePath)
{
	Texture* LoadedTexture = new Texture();
	LoadedTexture->Directory = InFilePath.substr(0, InFilePath.find_last_of('/'));

#if ME_OPENGL
	// Generate texture ID and load texture data
	glGenTextures(1, &LoadedTexture->Id);
	glBindTexture(GL_TEXTURE_2D, LoadedTexture->Id);

	// Parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_set_flip_vertically_on_load(true); // tell stb_image.h to flip loaded texture's on the y-axis.
	unsigned char* image = stbi_load(InFilePath.c_str(), &LoadedTexture->Width, &LoadedTexture->Height, &LoadedTexture->nrChannels, 0);
	if (image)
	{
		GLenum format;
		if (LoadedTexture->nrChannels == 1)
			format = GL_RED;
		else if (LoadedTexture->nrChannels == 3)
			format = GL_RGB;
		else if (LoadedTexture->nrChannels == 4)
			format = GL_RGBA;

		Logger::GetInstance().Log(Logger::LogType::Info, "Loaded Texture: " + InFilePath);

		// Assign texture to ID
		glTexImage2D(GL_TEXTURE_2D, 0, format, LoadedTexture->Width, LoadedTexture->Height, 0, format, GL_UNSIGNED_BYTE, image);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		Logger::GetInstance().Log(Logger::LogType::Error, "Failed to load texture: " + InFilePath);
		assert(0);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(image);
#endif
	return LoadedTexture;
}