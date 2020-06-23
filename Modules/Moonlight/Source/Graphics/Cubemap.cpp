#include <iostream>
#include <assert.h>
#include "Cubemap.h"
#include "CLog.h"

Cubemap::Cubemap(const Path& InFilePath)
	: Resource(InFilePath)
{
}

Cubemap::~Cubemap()
{
	// TODO: Unload Cubemaps
}