#pragma once
#include "Path.h"
#include "MeshData.h"
#include "Graphics/ModelResource.h"

namespace Moonlight
{
	class Material;

	class SkyBox
	{
	public:
		SkyBox(const std::string& InPath);

		std::shared_ptr<class Texture> SkyMap = nullptr;
		SharedPtr<Material> SkyMaterial = nullptr;

		std::shared_ptr<class ModelResource> SkyModel;
	};
}