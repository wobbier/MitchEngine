#pragma once
#include "Texture.h"
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include "Math/Vector3.h"

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

		void OnSerialize(json& InJson)
		{
			InJson["DiffuseColor"] = { DiffuseColor.X(),DiffuseColor.Y(),DiffuseColor.Z() };
			for (auto texture : Textures)
			{
				if (texture)
				{
					json& savedTexture = InJson["Textures"][Texture::ToString(texture->Type)];
					savedTexture["Path"] = texture->GetPath().LocalPath;
					savedTexture["RenderMode"] = GetRenderingModeString(RenderMode);
				}
			}
		}

		void OnDeserialize(const json& InJson)
		{
			if (InJson.contains("DiffuseColor"))
			{
				DiffuseColor = Vector3((float)InJson["DiffuseColor"][0], (float)InJson["DiffuseColor"][1], (float)InJson["DiffuseColor"][2]);
			}
			if (InJson.contains("Textures"))
			{
				for (unsigned int type = 0; type < TextureType::Count; ++type)
				{
					if (InJson["Textures"].contains(Texture::ToString(static_cast<TextureType>(type))))
					{
						SetTexture(static_cast<TextureType>(type), ResourceCache::GetInstance().Get<Texture>(Path(InJson["Textures"][Texture::ToString(static_cast<TextureType>(type))]["Path"])));
					}
				}
			}
		}

		void SetTexture(const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture);
		const Texture* GetTexture(const TextureType& type) const;
		std::vector<std::shared_ptr<Texture>>& GetTextures();
		RenderingMode RenderMode = RenderingMode::Opaque;
		Vector3 DiffuseColor;
	private:
		std::vector<std::shared_ptr<Texture>> Textures;
	};
}