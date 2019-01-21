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

Cubemap* Cubemap::Load(const FilePath& InFilePath)
{
	Cubemap* LoadedCubemap = new Cubemap();


	return LoadedCubemap;
}