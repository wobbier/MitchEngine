#pragma once

enum class AssetType : unsigned int
{
	Unknown = 0,
	Folder,
	Texture,
	Audio,
	Model,
	Level,
	Shader,
	Template,
	Text,
	Prefab,
	Count
};