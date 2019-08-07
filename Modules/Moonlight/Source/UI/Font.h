#pragma once
#include "Resource/Resource.h"

class Font
	: public Resource
{
	Font() = default;
	Font(const Path& InFilePath);


};