#include "Material.h"
#include "Brofiler.h"
#include "Texture.h"

namespace Moonlight
{
	Material::Material()
		: Textures(TextureType::Count, nullptr)
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
		return Textures[type];
	}

}