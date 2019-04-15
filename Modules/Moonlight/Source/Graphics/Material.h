#pragma once
#include "Texture.h"
#include <string>
#include <unordered_map>

namespace Moonlight
{
	class Material
	{
	public:
		Material();
		~Material();
		void SetTexture(const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture);
		const Texture* GetTexture(const TextureType& type) const;
		std::vector<std::shared_ptr<Texture>>& GetTextures();
	private:
		std::vector<std::shared_ptr<Texture>> Textures;
	};
}