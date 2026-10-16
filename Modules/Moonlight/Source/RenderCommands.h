#pragma once
#include <vector>
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "Math/Matrix4.h"
#include "Math/Bounds.h"
#include <string>
#include <cstdint>
#include "Pointers.h"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

namespace Moonlight {
    class ShaderCommand;
}

namespace Moonlight {
    class MeshData;
}
namespace Moonlight {
    class Material;
}
namespace Moonlight {
    class SkyBox;
}

namespace Moonlight
{
    enum MeshType
    {
        Model = 0,
        Plane,
        Cube,
        Sphere,
        Cylinder,
        Capsule,
        MeshCount
    };

    struct ModelCommand
    {
        std::vector<MeshData*> Meshes;
        ShaderCommand* ModelShader;
        Matrix4 Transform;
    };

    struct DebugColliderCommand
    {
        MeshType Type = MeshType::MeshCount;
        glm::mat4 Transform;
    };

    struct MeshCommand
    {
        MeshCommand() = default;
        void Reset()
        {
            SingleMesh = nullptr;
            MeshMaterial = nullptr;
            Transform = glm::mat4( 1.f );
            ID = 0u;
            PosedVertices = BGFX_INVALID_HANDLE;
        }
        MeshData* SingleMesh = nullptr;
        SharedPtr<Material> MeshMaterial = nullptr;
        glm::mat4 Transform;
        MeshType Type = MeshType::Model;
        uint64_t ID = 0u;
        int VisibilityIndex = 0;
        bool Visible = false;

        uint64_t BatchKey = 0u;
        uint16_t VertexBufferIdx = UINT16_MAX;
        // The index buffer of the level of detail drawn this frame (Lod; 0 = full mesh).
        uint16_t IndexBufferIdx = UINT16_MAX;
        uint8_t Lod = 0;
        bool IsTransparent = false;
        bool SupportsInstancing = false;
        bool CastShadows = true;
        // Shadow passes: casters are culled per cascade / spot light by their world bounds, and
        // alpha-tested materials discard in the depth pass too.
        float AlphaCutoff = 0.f;
        // The material draws its own shadow caster program (shader graphs with vertex offset or clip).
        bool CustomShadow = false;
        AABB WorldBounds;
        // Skinned meshes: this frame's mesh-space bone palette (owned by the Mesh component, valid
        // until the next RenderCore update). Skinned meshes are drawn one at a time.
        const glm::mat4* SkinPalette = nullptr;
        uint16_t SkinBoneCount = 0;
        bgfx::DynamicVertexBufferHandle PosedVertices = BGFX_INVALID_HANDLE;
    };

    enum class LightType : uint8_t
    {
        Directional = 0,
        Point,
        Spot,
        Bar,
        Box,
        Rectangle,
    };

    enum class DirectionalSource : uint8_t
    {
        Automatic = 0, // primary directional follows a procedural sky; other lights use their transform
        Transform,
        Sun,
        Moon,
    };

    // One decal for this frame, gathered from Decal components by RenderCore: a unit box (-0.5..0.5)
    // projecting along its +Z onto whatever the opaque pass drew inside it.
    struct DecalCommand
    {
        glm::mat4 World = glm::mat4( 1.f );
        glm::mat4 WorldToLocal = glm::mat4( 1.f );
        // Columns: the box's X (texture right), Y (texture up) and Z (projection) axes in world space.
        glm::mat4 Axes = glm::mat4( 1.f );
        bgfx::TextureHandle Albedo = BGFX_INVALID_HANDLE;   // invalid: a solid tint
        bgfx::TextureHandle Normal = BGFX_INVALID_HANDLE;   // invalid: no normal detail
        float Color[4] = { 1.f, 1.f, 1.f, 1.f };            // linear tint, a opacity
        float Emissive[4] = { 0.f, 0.f, 0.f, 1.f };         // rgb linear emitted radiance, w cos(fade start angle)
        float Surface[4] = { 0.f, 0.6f, 1.f, 0.f };         // x metallic, y roughness, z normal strength, w cos(fade end angle)
        float TextureSize[4] = { 1.f, 1.f, 1.f, 1.f };      // texels: albedo width, height, normal map width, height (for mip selection)
        AABB WorldBounds;
        int SortOrder = 0;
    };

    // One light for this frame, gathered from Light components by RenderCore.
    struct LightCommand
    {
        LightType Type = LightType::Point;
        DirectionalSource Source = DirectionalSource::Automatic;
        bool SkySun = false; // resolved per camera; receives the moon's eclipse shadow
        Vector3 Position;
        Vector3 Direction = Vector3( 0.f, -1.f, 0.f );   // world space, where the light points
        Vector3 Color = Vector3( 1.f, 1.f, 1.f );         // linear RGB * intensity
        float Range = 10.f;
        float CosInner = 0.9f;
        float CosOuter = 0.8f;
        // Extended emitters: orthonormal world axes and scaled half dimensions. Bars run along X;
        // HalfSize.x is half the segment length, HalfSize.y/z the capsule radius. Rectangles lie in
        // local XY with zero Z thickness, emitting only along Direction (local +Z).
        Vector3 AxisX = Vector3( 1.f, 0.f, 0.f );
        Vector3 AxisY = Vector3( 0.f, 1.f, 0.f );
        Vector3 AxisZ = Vector3( 0.f, 0.f, 1.f );
        Vector3 HalfSize;

        float GetBoundingRadius() const
        {
            if( Type == LightType::Bar ) return Range + HalfSize.x + HalfSize.y;
            if( Type == LightType::Box || Type == LightType::Rectangle ) return Range + HalfSize.Length();
            return Range;
        }

        bool CastShadows = false;
        // Biases are in shadow texels, so they hold across cascade sizes and spot distances.
        float ShadowBias = 0.5f;          // receiver pushed towards the light
        float ShadowNormalBias = 2.5f;    // receiver pushed along its normal (scaled by the slope)
        float ShadowDistance = 80.f;      // directional: cascades cover the view out to here
        float OuterConeAngle = 30.f;      // spot: degrees, for the shadow projection
    };

    enum class ParticleBlend : uint8_t
    {
        Additive = 0,   // light: fire, sparks, magic
        Alpha,          // matter: smoke, dust, debris
    };

    enum class ParticleAlignment : uint8_t
    {
        Billboard = 0,  // faces the camera
        Stretched,      // camera-facing, stretched along the velocity (sparks, rain)
        Horizontal,     // flat on the XZ plane (ripples, decals)
    };

    // One particle as the GPU sees it (one 64-byte instance).
    struct ParticleInstance
    {
        glm::vec4 PositionSize;     // world position, size
        glm::vec4 Color;            // linear RGB * intensity, alpha
        glm::vec4 Params;           // rotation (radians), flipbook frame
        glm::vec4 Velocity;         // world velocity (stretched particles)
    };

    // Everything one particle system draws this frame (filled by ParticleCore).
    struct ParticleBatch
    {
        bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;   // invalid: the built-in soft dot
        ParticleBlend Blend = ParticleBlend::Additive;
        ParticleAlignment Alignment = ParticleAlignment::Billboard;
        float StretchFactor = 0.1f;
        float Softness = 0.5f;           // soft-particle fade distance (0 = hard)
        bool Lit = false;                // shaded by the scene's lights like a volume (smoke)
        uint16_t FlipbookColumns = 1;
        uint16_t FlipbookRows = 1;
        AABB Bounds;
        std::vector<ParticleInstance> Instances;
    };
}
