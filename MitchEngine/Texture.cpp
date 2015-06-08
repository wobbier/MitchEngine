#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "Logger.h"
#include <SOIL.h>

using namespace ma;

Texture::~Texture() {
	// TODO: Unload textures
}

Texture* Texture::Load(const std::string& InFilePath) {
	Texture* LoadedTexture = new Texture();
	LoadedTexture->Directory = InFilePath.substr(0, InFilePath.find_last_of('/'));

	// Generate texture ID and load texture data 
	glGenTextures(1, &LoadedTexture->Id);
	unsigned char* image = SOIL_load_image(InFilePath.c_str(), &LoadedTexture->Width, &LoadedTexture->Height, 0, SOIL_LOAD_RGBA);
	if (image == 0) {
		Logger::Get().Log(Logger::ERR, "Failed to load texture: " + InFilePath);
		assert(0);
	}

	Logger::Get().Log(Logger::INFO, "Loaded Texture: " + InFilePath);

	// Assign texture to ID
	glBindTexture(GL_TEXTURE_2D, LoadedTexture->Id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LoadedTexture->Width, LoadedTexture->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
	glGenerateMipmap(GL_TEXTURE_2D);

	// Parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	SOIL_free_image_data(image);
}
