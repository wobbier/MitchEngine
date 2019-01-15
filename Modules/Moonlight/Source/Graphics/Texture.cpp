#include <iostream>
#include <assert.h>
#include "Texture.h"
#include "Logger.h"

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
	return LoadedTexture;
}