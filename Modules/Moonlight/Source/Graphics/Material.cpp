#include "Material.h"
#include "optick.h"
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

	const bool Material::IsTransparent() const
	{
		if (RenderMode == RenderingMode::Transparent)
		{
			return true;
		}
		else if (Textures[TextureType::Opacity])
		{
			return true;
		}
		return false;
	}

	void Material::SetRenderMode(RenderingMode newMode)
	{
		RenderMode = newMode;
	}
	void Material::SetTexture(const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture)
	{
		Textures[textureType] = loadedTexture;
	}

	const Moonlight::Texture* Material::GetTexture(const TextureType& type) const
	{
		if (Textures[type])
		{
			return Textures[type].get();
		}
		return nullptr;
	}

	std::vector<std::shared_ptr<Moonlight::Texture>>& Material::GetTextures()
	{
		return Textures;
	}
}