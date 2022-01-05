#include "UnlitMaterial.h"

namespace Moonlight
{
	UnlitMaterial::UnlitMaterial()
		: Moonlight::Material("UnlitMaterial", "Assets/Shaders/Unlit")
	{

	}

	void UnlitMaterial::Init()
	{

	}

	void UnlitMaterial::Use()
	{

	}

	SharedPtr<Moonlight::Material> UnlitMaterial::CreateInstance()
	{
		return MakeShared<UnlitMaterial>(*this);
	}

}