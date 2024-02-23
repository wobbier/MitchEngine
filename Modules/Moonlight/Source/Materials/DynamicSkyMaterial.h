#pragma once
#include "Graphics/Material.h"

class DynamicSkyMaterial
    : public Moonlight::Material
{
public:
    DynamicSkyMaterial();

    void Init() override;

    virtual void Use() final;

    SharedPtr<Material> CreateInstance() final;

    bgfx::UniformHandle u_sunLuminance;
    bgfx::UniformHandle u_skyLuminanceXYZ;
    bgfx::UniformHandle u_skyLuminance;
    bgfx::UniformHandle u_sunDirection;
    bgfx::UniformHandle u_parameters;
    bgfx::UniformHandle u_perezCoeff;
};

ME_REGISTER_MATERIAL_NAME( DynamicSkyMaterial, "DynamicSky" )
