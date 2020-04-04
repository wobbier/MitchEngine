#include "PCH.h"

#include "JsonResource.h"
#include "File.h"

JsonResource::JsonResource(const Path& InFilePath)
	: Resource(InFilePath)
{
	Data = nlohmann::json::parse(File(InFilePath).Read());
}

const nlohmann::json& JsonResource::GetJson() const
{
	return Data;
}

