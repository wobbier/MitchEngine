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
		void SetTexture(std::string textureType, Moonlight::Texture* loadedTexture);
		const Texture* GetTexture(const std::string& type) const;
	private:
		std::unordered_map<std::string, Texture*> Textures;
	};
}