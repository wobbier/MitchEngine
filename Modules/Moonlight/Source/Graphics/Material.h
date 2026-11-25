#pragma once
#include "Texture.h"
#include <string>
#include <unordered_map>
#include <cstring>
#include <type_traits>

#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "ShaderCommand.h"
#include "MaterialDetail.h"
#include "MaterialRevision.h"
#include "Dementia.h"
// move once I move the material??
#include "Core/Assert.h"
#include "Resource/Resource.h"

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

// Empty. Defining ME_MATERIAL_FIELD as const in a syntax-only compile lists every direct write to
// a material's draw-state fields (ME-399 audit); writers must use the setters or MarkChanged().
#ifndef ME_MATERIAL_FIELD
#define ME_MATERIAL_FIELD
#endif

namespace Moonlight
{
    enum class RenderingMode : unsigned int
    {
        Opaque = 0,
        Transparent
    };
    enum class BlendMode : unsigned int
    {
        Alpha = 0,
        Premultiply,
        Additive,
        Multiply,

        Count
    };
    enum class RenderFaceMode : unsigned int
    {
        Front = 0,
        Back,
        Both,

        Count
    };
    class Material
        : public Resource
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

        std::string GetRenderFaceModeString( RenderFaceMode mode )
        {
            switch( mode )
            {
            case RenderFaceMode::Front:
                return "Front";
            case RenderFaceMode::Back:
                return "Back";
            case RenderFaceMode::Both:
                return "Both";
            case RenderFaceMode::Count:
            default:
                break;
            }

            ME_ASSERT_MSG( false, "Unknown Face Mode" );
            return "Front";
        }
        std::string GetBlendModeString( BlendMode mode )
        {
            switch( mode )
            {
            case BlendMode::Alpha:
                return "Alpha";
            case BlendMode::Premultiply:
                return "Premultiply";
            case BlendMode::Additive:
                return "Additive";
            case BlendMode::Multiply:
                return "Multiply";
            case BlendMode::Count:
            default:
                break;
            }

            ME_ASSERT_MSG( false, "Unknown Blend Mode" );
            return "Alpha";
        }
        Material( const Path& InFilePath, WrapMode mode = WrapMode::Wrap );
        Material( const std::string& MaterialTypeName, const std::string& ShaderPath = "" );
        Material() = delete;
        virtual ~Material();

        virtual void LoadShader( const std::string& inShaderName );

        const bool IsTransparent() const;
        // A transparent material that shows only its outermost surface (StandardMaterial::Shell):
        // the renderer lays its depth down first, so nothing of the mesh behind that draws.
        virtual bool IsShell() const { return false; }

        // Draw-state revision (ME-399). Anything a mesh command takes from a material (the fields
        // GetInstanceBatchKey hashes, textures, the shader, transparency, instancing, alpha cutoff,
        // shadow casting) changes through a setter, or the writer calls MarkChanged() after writing
        // the public field. A missed bump leaves a static mesh drawing with its old batch key or
        // flags; --verify-retained-meshes reports it (Rendering-Pipeline.md, "Retained mesh commands").
        uint32_t GetRevision() const
        {
            return m_revision.Get();
        }
        void MarkChanged()
        {
            m_revision.Bump();
        }
        // Everything RenderCore takes from this material into a mesh command, hashed. Inspectors
        // compare it around an edit (ImGui writes the fields through pointers).
        uint64_t GetDrawStateHash();

        void SetRenderMode( RenderingMode newMode );
        void SetDiffuseColor( const Vector3& InColor );
        void SetTiling( const Vector2& InTiling );
        void SetOffset( const Vector2& InOffset );
        void SetFaceMode( RenderFaceMode InMode );
        void SetBlendMode( BlendMode InMode );
        void SetSupportsInstancing( bool InSupports );

        virtual void OnSerialize( json& OutJson );

        virtual void OnDeserialize( const json& InJson );

        virtual void Init() = 0;
        virtual void Use() = 0;

        virtual SharedPtr<Material> CreateInstance() = 0;

        void CopyValues( Material* mat );

#if USING( ME_TOOLS )
        virtual void OnEditorInspect();
        // A texture chosen for a slot in the editor (picker, drag and drop, edscript). A normal slot
        // also marks the texture as a normal map so it cooks as BC5 (ME-239).
        void AssignTexture( TextureType InType, const Path& InPath );
#endif

        //virtual void SetSamplers() = 0;

        void SetTexture( const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture );
        const Texture* GetTexture( const TextureType& type ) const;
        const std::vector<std::shared_ptr<Texture>>& GetTextures() const;
        // Empties every texture slot.
        void ClearTextures();

        // Read freely; write through the setters above (or call MarkChanged() after a write).
        ME_MATERIAL_FIELD RenderingMode RenderMode = RenderingMode::Opaque;
        ME_MATERIAL_FIELD bool SupportsInstancing = false;
        // Runtime only: the camera (cache id) whose output the renderer binds to the diffuse slot,
        // sampled in screen space (PortalMaterial). -1 = none.
        int ScreenViewCamera = -1;
        ME_MATERIAL_FIELD Vector3 DiffuseColor;
        ME_MATERIAL_FIELD Vector2 Tiling;
        // Added after tiling (uv * Tiling + Offset). Tiling (1, -1) with Offset (0, 1) flips an image
        // vertically within the edges: what upright images on the built-in primitives need.
        ME_MATERIAL_FIELD Vector2 Offset;
        std::string ShaderName;
        ME_MATERIAL_FIELD RenderFaceMode FaceMode = RenderFaceMode::Front;
        ME_MATERIAL_FIELD BlendMode AlphaBlendMode = BlendMode::Alpha;
        // Defaults produced by the former importer, used to migrate saved model materials.
        json LegacyImportDefaults;

        // Set through LoadShader (or MarkChanged() after assigning it).
        ME_MATERIAL_FIELD Moonlight::ShaderCommand MeshShader;
        const std::string& GetTypeName() const;

        bool Load() override
        {
            return true;
        }


        void Reload() override
        {
        }

    protected:
        // Assigns and bumps the revision only when the value differs.
        template<typename T, typename U>
        void SetDrawValue( T& InOutField, const U& InValue )
        {
            auto& field = Writable( InOutField );
            if( !SameDrawValue( field, InValue ) )
            {
                field = InValue;
                MarkChanged();
            }
        }
        // The field itself, for the material's own bulk writes (deserialising, syncing): call
        // MarkChanged() too. Fields are const in the ME_MATERIAL_FIELD audit compile only.
        template<typename T>
        static std::remove_const_t<T>& Writable( T& InField )
        {
            return const_cast<std::remove_const_t<T>&>( InField );
        }

    private:
        template<typename T, typename U>
        static bool SameDrawValue( const T& InA, const U& InB )
        {
            return InA == InB;
        }
        static bool SameDrawValue( const Vector3& InA, const Vector3& InB )
        {
            return InA.InternalVector == InB.InternalVector;
        }

    private:
        std::vector<std::shared_ptr<Texture>> Textures;
        std::string TypeName;
        MaterialRevision m_revision;
    public:
        virtual uint64_t GetRenderState( uint64_t state ) const;

        // > 0 for alpha-tested materials (fragments below it are discarded, also in shadow maps).
        virtual float GetAlphaCutoff() const
        {
            return 0.f;
        }

        // The program skinned meshes draw with; invalid = draw the bind pose with MeshShader.
        virtual bgfx::ProgramHandle GetSkinnedProgram()
        {
            return BGFX_INVALID_HANDLE;
        }
        // The depth-only program shadow casters draw with (UseShadowCaster() binds its parameters);
        // invalid = the renderer's stock caster, which only knows about AlphaCutoff and the
        // diffuse/opacity maps.
        virtual bgfx::ProgramHandle GetShadowProgram( bool InSkinned )
        {
            return BGFX_INVALID_HANDLE;
        }
        // Binds GetShadowProgram's uniforms and textures (the renderer binds none for it).
        virtual void UseShadowCaster()
        {
            Use();
        }
        // False for materials that never cast (the mesh's own CastShadows still applies).
        virtual bool CastsShadows() const
        {
            return true;
        }
        // Once per frame per drawn mesh, before its shader is checked (on a render job thread: only
        // touch this material). Materials whose program comes from a reloadable asset sync here.
        virtual void PrepareForDraw()
        {
        }

        virtual uint64_t GetInstanceBatchKey() const
        {
            auto mix = []( uint64_t h, uint64_t v ) {
                return ( h ^ ( v + 0x9e3779b97f4a7c15ULL + ( h << 6 ) + ( h >> 2 ) ) );
                };
            auto texId = [this]( const TextureType& type ) -> uint64_t {
                const Texture* tex = GetTexture( type );
                return ( tex && bgfx::isValid( tex->TexHandle ) ) ? tex->TexHandle.idx : 0xFFFFu;
                };
            auto floatBits = []( float f ) -> uint64_t {
                uint32_t bits = 0u;
                std::memcpy( &bits, &f, sizeof( bits ) );
                return bits;
                };

            uint64_t h = 1469598103934665603ULL;
            h = mix( h, MeshShader.GetProgram().idx );
            h = mix( h, texId( TextureType::Diffuse ) );
            h = mix( h, texId( TextureType::Normal ) );
            h = mix( h, texId( TextureType::Opacity ) );
            h = mix( h, GetRenderState( 0 ) );
            h = mix( h, floatBits( DiffuseColor.x ) );
            h = mix( h, floatBits( DiffuseColor.y ) );
            h = mix( h, floatBits( DiffuseColor.z ) );
            h = mix( h, floatBits( Tiling.x ) );
            h = mix( h, floatBits( Tiling.y ) );
            h = mix( h, floatBits( Offset.x ) );
            h = mix( h, floatBits( Offset.y ) );
            return h;
        }
    };



    struct MaterialResourceMetadata
        : public MetaBase
    {
        MaterialResourceMetadata( const Path& filePath ) : MetaBase( filePath )
        {
        }

        void OnSerialize( json& inJson ) override
        {
        };
        void OnDeserialize( const json& inJson ) override
        {
        };

        virtual std::string GetExtension2() const override
        {
            return "mat";
        };

#if USING( ME_EDITOR )
        virtual void OnEditorInspect() final
        {
        };
#endif
#if USING( ME_TOOLS )
        void Export() override
        {
        };
#endif
    };

    ME_REGISTER_METADATA( "mat", MaterialResourceMetadata );
}
