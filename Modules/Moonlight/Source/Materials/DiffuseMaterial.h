#pragma once
#include "Graphics/Material.h"
#include "ShaderGraphMaterial.h"

class DiffuseMaterial
    : public Moonlight::Material
{
public:
    DiffuseMaterial()
        : Moonlight::Material( "DiffuseMaterial", "Assets/Shaders/Diffuse" )
        , s_diffuse( BGFX_INVALID_HANDLE )
        , s_tiling( BGFX_INVALID_HANDLE )
    {

    }

    void Init() override
    {
        s_diffuse = bgfx::createUniform( "s_diffuse", bgfx::UniformType::Vec4 );
        s_tiling = bgfx::createUniform( "s_tiling", bgfx::UniformType::Vec4 );
    }

    virtual void Use() final
    {
        bgfx::setUniform( s_diffuse, &DiffuseColor.x );
        bgfx::setUniform( s_tiling, &Tiling.x );
    }

    SharedPtr<Material> CreateInstance() final
    {
        SharedPtr<DiffuseMaterial> ptr = MakeShared<DiffuseMaterial>( *this );

        return ptr;
    }
private:
    bgfx::UniformHandle s_diffuse;
    bgfx::UniformHandle s_tiling;
};

class WhiteMaterial
    : public Moonlight::Material
{
public:
    WhiteMaterial()
        : Moonlight::Material( "WhiteMaterial", "Assets/Shaders/Diffuse" )
    {

    }
    WhiteMaterial( WhiteMaterial* ref )
        : Moonlight::Material( "WhiteMaterial" )
    {
        CopyValues( ref );
    }


    void Init() override
    {
    }

    virtual void Use() final
    {

    }

    SharedPtr<Material> CreateInstance() final
    {
        SharedPtr<WhiteMaterial> ptr = MakeShared<WhiteMaterial>( this );

        return ptr;
    }

};

ME_REGISTER_MATERIAL_NAME( DiffuseMaterial, "Diffuse" )
ME_REGISTER_MATERIAL_NAME( WhiteMaterial, "White" )
