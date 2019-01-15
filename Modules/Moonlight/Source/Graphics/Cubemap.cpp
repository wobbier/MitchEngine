#include <iostream>
#include <assert.h>
#include "Cubemap.h"
#include "Logger.h"

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

	return LoadedCubemap;
}