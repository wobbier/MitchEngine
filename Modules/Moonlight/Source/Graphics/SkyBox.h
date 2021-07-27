#pragma once
#include <string>
#include <Pointers.h>

class ModelResource;

namespace Moonlight
{
	class Material;
	class Texture;

	class SkyBox
	{
	public:
		SkyBox(const std::string& InPath);
		~SkyBox();

		std::shared_ptr<Texture> SkyMap = nullptr;
		SharedPtr<Material> SkyMaterial = nullptr;

		std::shared_ptr<ModelResource> SkyModel;
	};
}