#include "Material.h"
#include "optick.h"
#include "Texture.h"
#include "Resource/ResourceCache.h"

namespace Moonlight
{
	Material::Material(const std::string& MaterialTypeName, const std::string& ShaderPath)
		: Textures(TextureType::Count, nullptr)
		, DiffuseColor(1.f, 1.f, 1.f)
		, Tiling(1.f, 1.f)
		, TypeName(MaterialTypeName)
	{
        if(ShaderPath.length() > 0)
        {
            MeshShader = ShaderCommand(ShaderPath);
        }
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

	void Material::OnSerialize(json& OutJson)
	{
		OutJson["Type"] = TypeName;
		OutJson["DiffuseColor"] = { DiffuseColor.x, DiffuseColor.y, DiffuseColor.z };
		OutJson["Tiling"] = { Tiling.x, Tiling.y };
		for (unsigned int type = 0; type < TextureType::Count; ++type)
		{
			auto texture = Textures[type];
			if (texture)
			{
				json& savedTexture = OutJson["Textures"][Texture::ToString(static_cast<TextureType>(type))];
				savedTexture["Path"] = texture->GetPath().LocalPath;
				savedTexture["RenderMode"] = GetRenderingModeString(RenderMode);
			}
		}
	}

	void Material::OnDeserialize(const json& InJson)
	{
		if (InJson.contains("Type"))
		{
			TypeName = InJson["Type"];
		}
		if (InJson.contains("DiffuseColor"))
		{
			DiffuseColor = Vector3((float)InJson["DiffuseColor"][0], (float)InJson["DiffuseColor"][1], (float)InJson["DiffuseColor"][2]);
		}
		if (InJson.contains("Tiling"))
		{
			Tiling = Vector2((float)InJson["Tiling"][0], (float)InJson["Tiling"][1]);
		}
		if (InJson.contains("Textures"))
		{
			for (unsigned int type = 0; type < TextureType::Count; ++type)
			{
				if (InJson["Textures"].contains(Texture::ToString(static_cast<TextureType>(type))))
				{
					Path texturePath = Path(InJson["Textures"][Texture::ToString(static_cast<TextureType>(type))]["Path"]);
					SetTexture(static_cast<TextureType>(type), ResourceCache::GetInstance().Get<Moonlight::Texture>(texturePath));
				}
			}
		}
	}

    void Material::CopyValues(Material* mat)
    {
        DiffuseColor = mat->DiffuseColor;
        Textures = mat->Textures;
        RenderMode = mat->RenderMode;
        Tiling = mat->Tiling;
        MeshShader = mat->MeshShader;
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

	const std::string& Material::GetTypeName() const
	{
		return TypeName;
	}
}
