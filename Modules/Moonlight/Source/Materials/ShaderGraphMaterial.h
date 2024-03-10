#pragma once
#include "Graphics/Material.h"

class ShaderGraphMaterial
    : public Moonlight::Material
{
public:
    ShaderGraphMaterial()
        : Moonlight::Material( "ShaderGraphMaterial" )
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
        //for (auto uniform : s_uniforms)
        //{
        //    bgfx::setUniform(uniform, &s_uniformTextures[i])
        //}
    }

    SharedPtr<Material> CreateInstance() final
    {
        SharedPtr<ShaderGraphMaterial> ptr = MakeShared<ShaderGraphMaterial>( *this );

        return ptr;
    }

    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;

#if USING( ME_TOOLS )
    void OnEditorInspect() override;
#endif


    void LoadShader( const std::string& inShaderName ) override;

private:
    bgfx::UniformHandle s_diffuse;
    bgfx::UniformHandle s_tiling;
    std::vector<bgfx::UniformHandle> s_uniforms;
    std::vector<bgfx::TextureHandle> s_uniformTextures;
};

ME_REGISTER_MATERIAL_NAME( ShaderGraphMaterial, "ShaderGraphMaterial" )
