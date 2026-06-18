#pragma once
#include "Graphics/Material.h"
#include "ShaderGraphMaterial.h"

class DiffuseMaterial
    : public Moonlight::Material
{
public:
    DiffuseMaterial()
        : Moonlight::Material( "DiffuseMaterial", "Assets/Shaders/Diffuse" )
    {
        SupportsInstancing = true;
    }

    void Init() override
    {
        if ( !bgfx::isValid( s_diffuse ) )
        {
            s_diffuse = bgfx::createUniform( "s_diffuse", bgfx::UniformType::Vec4 );
        }
        if ( !bgfx::isValid( s_tiling ) )
        {
            s_tiling = bgfx::createUniform( "s_tiling", bgfx::UniformType::Vec4 );
        }
    }

    virtual void Use() final
    {
        bgfx::setUniform( s_diffuse, &DiffuseColor.x );

        bgfx::setUniform( s_tiling, &Tiling.x );
    }

    SharedPtr<Material> CreateInstance() final
    {
        return MakeShared<DiffuseMaterial>( *this );
    }

private:
    inline static bgfx::UniformHandle s_diffuse = BGFX_INVALID_HANDLE;
    inline static bgfx::UniformHandle s_tiling = BGFX_INVALID_HANDLE;
};

class WhiteMaterial
    : public Moonlight::Material
{
public:
    WhiteMaterial()
        : Moonlight::Material( "WhiteMaterial", "Assets/Shaders/Diffuse" )
    {
        SupportsInstancing = true;
    }
    WhiteMaterial( WhiteMaterial* ref )
        : Moonlight::Material( "WhiteMaterial" )
    {
        CopyValues( ref );
        SupportsInstancing = true;
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
