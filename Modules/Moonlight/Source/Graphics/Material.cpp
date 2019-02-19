#include "Material.h"

namespace Moonlight
{
	Material::Material()
	{

	}

	Material::~Material()
	{

	}

	void Material::SetTexture(const TextureType& textureType, Moonlight::Texture* loadedTexture)
	{
		Textures[textureType] = loadedTexture;
	}

	const Moonlight::Texture* Material::GetTexture(const TextureType& type) const
	{
		if (Textures.find(type) == Textures.end())
		{
			return nullptr;
		}

		return Textures.at(type);
	}

}