#pragma once
#include "Graphics/Material.h"
#include "ShaderGraphMaterial.h"
#include <cmath>

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
        // Vec4 uniforms read 16 bytes: widen the Vector3/Vector2 members instead of over-reading them.
        // Colours are authored in sRGB; shading is linear.
        auto toLinear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
        const float diffuse[4] = { toLinear( DiffuseColor.x ), toLinear( DiffuseColor.y ), toLinear( DiffuseColor.z ), 1.f };
        const float tiling[4] = { Tiling.x, Tiling.y, 0.f, 0.f };
        bgfx::setUniform( s_diffuse, diffuse );
        bgfx::setUniform( s_tiling, tiling );
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
