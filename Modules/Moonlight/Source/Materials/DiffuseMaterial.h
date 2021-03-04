#pragma once
#include "Graphics/Material.h"

class DiffuseMaterial
	: public Moonlight::Material
{
public:
	DiffuseMaterial()
		: Moonlight::Material("DiffuseMaterial", "Assets/Shaders/Samples/Cubes")
	{

	}

	void Init() override
	{
	}

};

class WhiteMaterial
	: public Moonlight::Material
{
public:
	WhiteMaterial()
		: Moonlight::Material("WhiteMaterial", "Assets/Shaders/Samples/Cubes")
	{

	}

	void Init() override
	{
	}

};

ME_REGISTER_MATERIAL_NAME(DiffuseMaterial, "Diffuse")
ME_REGISTER_MATERIAL_NAME(WhiteMaterial, "White")