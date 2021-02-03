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

	void Init() override
	{
		throw std::logic_error("The method or operation is not implemented.");
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

	void Init() override
	{
		throw std::logic_error("The method or operation is not implemented.");
	}

};

ME_REGISTER_MATERIAL_NAME(DiffuseMaterial, "Diffuse")
ME_REGISTER_MATERIAL_NAME(WhiteMaterial, "White")