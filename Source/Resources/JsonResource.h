#pragma once
#include "Resource/Resource.h"
#include "JSON.h"

class JsonResource
	: public Resource
{
public:
	JsonResource() = default;
	JsonResource(const Path& InFilePath);

	const json& GetJson() const;

private:
	json Data;
};