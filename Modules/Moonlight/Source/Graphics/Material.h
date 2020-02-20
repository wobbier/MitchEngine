#pragma once
#include "Texture.h"
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

using json = nlohmann::json;

namespace Moonlight
{
	enum class RenderingMode : unsigned int
	{
		Opaque = 0,
		Transparent
	};
	class Material
	{
	public:
		std::string GetRenderingModeString(RenderingMode mode)
		{
			switch (mode)
			{
			case RenderingMode::Transparent:
				return "Transparent";
			case RenderingMode::Opaque:
			default:
				return "Opaque";
			}
			return "Opaque";
		}
		Material();
		~Material();

		const bool IsTransparent() const;
		void SetRenderMode(RenderingMode newMode);

		void OnSerialize(json& InJson);

		void OnDeserialize(const json& InJson);

		void SetTexture(const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture);
		const Texture* GetTexture(const TextureType& type) const;
		std::vector<std::shared_ptr<Texture>>& GetTextures();
		RenderingMode RenderMode = RenderingMode::Opaque;
		Vector3 DiffuseColor;
		Vector2 Tiling;
	private:
		std::vector<std::shared_ptr<Texture>> Textures;
	};
}