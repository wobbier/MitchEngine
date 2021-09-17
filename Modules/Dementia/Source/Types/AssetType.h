#pragma once
#include <string>

enum class AssetType : unsigned int
{
	Unknown = 0,
	Audio,
	Level,
	Model,
	Prefab,
	Shader,
	Texture,
	Count
};

namespace {
	std::string AssetTypeToString(AssetType InType)
	{
		switch (InType)
		{
		case AssetType::Unknown:
			return "Unknown";
			break;
		case AssetType::Texture:
			return "Texture";
			break;
		case AssetType::Audio:
			return "Audio";
			break;
		case AssetType::Model:
			return "Model";
			break;
		case AssetType::Level:
			return "Level";
			break;
		case AssetType::Shader:
			return "Shader";
			break;
		case AssetType::Prefab:
			return "Prefab";
			break;
		case AssetType::Count:
		default:
			return "";
			break;
		}
	}
}