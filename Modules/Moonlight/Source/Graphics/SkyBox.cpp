#include "SkyBox.h"

#include "Graphics/ModelResource.h"
#include "Resource/ResourceCache.h"
#include "Path.h"
#include "Shaders/UnlitMaterial.h"
#include "Texture.h"


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

	SkyBox::~SkyBox()
	{
		//bgfx::destroy(sky)
	}

}
