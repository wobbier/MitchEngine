#pragma once
#include "Graphics/Material.h"

class DiffuseMaterial
	: public Moonlight::Material
{
public:
	DiffuseMaterial()
		: Moonlight::Material("DiffuseMaterial", "Assets/Shaders/DiffuseShader.hlsl")
	{

	}
};

class WhiteMaterial
	: public Moonlight::Material
{
public:
	WhiteMaterial()
		: Moonlight::Material("WhiteMaterial", "Assets/Shaders/WhiteShader.hlsl")
	{

	}
};

ME_REGISTER_MATERIAL_NAME(DiffuseMaterial, "Diffuse")
ME_REGISTER_MATERIAL_NAME(WhiteMaterial, "White")