#pragma once
#include "Texture.h"
#include <string>
#include <unordered_map>

#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "ShaderCommand.h"
#include "MaterialDetail.h"
#include "Dementia.h"

#define ME_REGISTER_MATERIAL_NAME_FOLDER(TYPE, NAME, FOLDER)            \
	namespace details {                                       \
    namespace                                                 \
    {                                                         \
        template<class T>                                     \
        class MaterialRegistration;                          \
                                                              \
        template<>                                            \
        class MaterialRegistration<TYPE>                     \
        {                                                     \
            static const MaterialRegistryEntry<TYPE>& reg;            \
        };                                                    \
                                                              \
        const MaterialRegistryEntry<TYPE>&                            \
            MaterialRegistration<TYPE>::reg =                \
                MaterialRegistryEntry<TYPE>::Instance(#TYPE, NAME, FOLDER); \
    }}
#define ME_REGISTER_MATERIAL_NAME(TYPE, NAME) ME_REGISTER_MATERIAL_NAME_FOLDER(TYPE, NAME, "")
#define ME_REGISTER_MATERIAL(TYPE) ME_REGISTER_MATERIAL_NAME(TYPE, #TYPE)

namespace Moonlight
{
    enum class RenderingMode : unsigned int
    {
        Opaque = 0,
        Transparent
    };
    class Material
    {
    public:
        std::string GetRenderingModeString( RenderingMode mode )
        {
            switch( mode )
            {
            case RenderingMode::Transparent:
                return "Transparent";
            case RenderingMode::Opaque:
            default:
                return "Opaque";
            }
            return "Opaque";
        }
        Material( const std::string& MaterialTypeName, const std::string& ShaderPath = "" );
        Material() = delete;
        virtual ~Material();

        virtual void LoadShader( const std::string& inShaderName );

        const bool IsTransparent() const;
        void SetRenderMode( RenderingMode newMode );

        virtual void OnSerialize( json& OutJson );

        virtual void OnDeserialize( const json& InJson );

        virtual void Init() = 0;
        virtual void Use() = 0;

        virtual SharedPtr<Material> CreateInstance() = 0;

        void CopyValues( Material* mat );

#if USING( ME_TOOLS )
        virtual void OnEditorInspect() {}
#endif

        //virtual void SetSamplers() = 0;

        void SetTexture( const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture );
        const Texture* GetTexture( const TextureType& type ) const;
        std::vector<std::shared_ptr<Texture>>& GetTextures();
        RenderingMode RenderMode = RenderingMode::Opaque;
        Vector3 DiffuseColor;
        Vector2 Tiling;
        std::string ShaderName;

        Moonlight::ShaderCommand MeshShader;
        const std::string& GetTypeName() const;
    private:
        std::vector<std::shared_ptr<Texture>> Textures;
        std::string TypeName;
    public:
        virtual uint64_t GetRenderState( uint64_t state ) const;
    };
}
