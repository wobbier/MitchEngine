#pragma once
#include "nlohmann/json.hpp"
#include "Resource/Resource.h"

class JsonResource
	: public Resource
{
public:
	JsonResource() = default;
	JsonResource(const Path& InFilePath);

	const nlohmann::json& GetJson() const;

private:
	nlohmann::json Data;
};