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
		void SetTexture(const TextureType& textureType, Moonlight::Texture* loadedTexture);
		const Texture* GetTexture(const TextureType& type) const;
	private:
		std::vector<Texture*> Textures;
	};
}