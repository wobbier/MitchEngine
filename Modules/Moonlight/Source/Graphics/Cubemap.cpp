#include <iostream>
#include <assert.h>
#include "Cubemap.h"
#include "Logger.h"

Cubemap::Cubemap(const FilePath& InFilePath)
	: Resource(InFilePath)
{
}

Cubemap::~Cubemap()
{
	// TODO: Unload Cubemaps
}