#include <iostream>
#include <assert.h>
#include "Graphics/Texture.h"
#include "Utility/Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

	// Generate texture ID and load texture data
	glGenTextures(1, &LoadedTexture->Id);
	glBindTexture(GL_TEXTURE_2D, LoadedTexture->Id);

	// Parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_set_flip_vertically_on_load(true); // tell stb_image.h to flip loaded texture's on the y-axis.
	unsigned char* image = stbi_load(InFilePath.c_str(), &LoadedTexture->Width, &LoadedTexture->Height, &LoadedTexture->nrChannels, 0);
	if (image)
	{
		Logger::Get().Log(Logger::LogType::Info, "Loaded Texture: " + InFilePath);

		// Assign texture to ID
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, LoadedTexture->Width, LoadedTexture->Height, 0, GL_RGB, GL_UNSIGNED_BYTE, image);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		Logger::Get().Log(Logger::LogType::Error, "Failed to load texture: " + InFilePath);
		assert(0);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(image);
	return LoadedTexture;
}