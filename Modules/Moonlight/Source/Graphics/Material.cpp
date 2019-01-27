#include "Material.h"

namespace Moonlight
{
	Material::Material()
	{

	}

	Material::~Material()
	{

	}

	void Material::SetTexture(std::string textureType, Moonlight::Texture* loadedTexture)
	{
		Textures[textureType] = loadedTexture;
	}

	const Moonlight::Texture* Material::GetTexture(const std::string& type) const
	{
		if (Textures.find(type) == Textures.end())
		{
			return nullptr;
		}

		return Textures.at(type);
	}

}