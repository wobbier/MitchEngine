#include "DynamicSkyMaterial.h"

DynamicSkyMaterial::DynamicSkyMaterial()
	: Moonlight::Material("DynamicSkyMaterial", "Assets/Shaders/Sky/Sky")
	, u_sunLuminance(BGFX_INVALID_HANDLE)
	, u_skyLuminanceXYZ(BGFX_INVALID_HANDLE)
	, u_skyLuminance(BGFX_INVALID_HANDLE)
	, u_sunDirection(BGFX_INVALID_HANDLE)
	, u_parameters(BGFX_INVALID_HANDLE)
	, u_perezCoeff(BGFX_INVALID_HANDLE)
{

}

void DynamicSkyMaterial::Init()
{
	u_sunLuminance = bgfx::createUniform("u_sunLuminance", bgfx::UniformType::Vec4);
	u_skyLuminanceXYZ = bgfx::createUniform("u_skyLuminanceXYZ", bgfx::UniformType::Vec4);
	u_skyLuminance = bgfx::createUniform("u_skyLuminance", bgfx::UniformType::Vec4);
	u_sunDirection = bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
	u_parameters = bgfx::createUniform("u_parameters", bgfx::UniformType::Vec4);
	u_perezCoeff = bgfx::createUniform("u_perezCoeff", bgfx::UniformType::Vec4, 5);
}

void DynamicSkyMaterial::Use()
{
}

SharedPtr<Moonlight::Material> DynamicSkyMaterial::CreateInstance()
{
	SharedPtr<DynamicSkyMaterial> ptr = std::make_shared<DynamicSkyMaterial>(*this);

	return ptr;
}

