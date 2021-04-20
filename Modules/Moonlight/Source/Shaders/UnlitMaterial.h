#pragma once
#include <Graphics/Material.h>

namespace Moonlight
{
	class UnlitMaterial
		: public Material
	{
	public:
		UnlitMaterial();

		virtual void Init() final;
		virtual void Use() final;

		virtual SharedPtr<Material> CreateInstance() final;
	};

	ME_REGISTER_MATERIAL_NAME(UnlitMaterial, "Unlit")
}