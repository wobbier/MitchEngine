#include "SkyBox.h"
#include "Resource/ResourceCache.h"
#include "Texture.h"
#include "MeshData.h"
#include "ShaderCommand.h"
#include "Game.h"
#include "Material.h"
#include "Engine/Engine.h"
#include "Shaders/UnlitMaterial.h"

namespace Moonlight
{
	SkyBox::SkyBox(const std::string& InPath)
	{
		Path SystemPath(InPath);

		SkyMap = ResourceCache::GetInstance().Get<Texture>(SystemPath);
		SkyModel = ResourceCache::GetInstance().Get<ModelResource>(Path("Assets/Skybox/Skybox.fbx"));
		SkyMaterial = std::make_shared<UnlitMaterial>();
		SkyMaterial->SetTexture(TextureType::Diffuse, SkyMap);
	}
}
