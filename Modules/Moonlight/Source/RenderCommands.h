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
#include "Materials/ShaderGraphParams.h"

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
    class ShaderGraphAsset;
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

    // Render groups keep separate scenes in one renderer: a camera draws only the meshes of its own
    // group. Group 0 is the world (its lights, local shadows, decals, particles and debug drawing);
    // other groups (editor previews) are lit by their camera's sky and CameraData::GroupLights.
    constexpr uint8_t kMaxRenderGroups = 8;

    // The scene's wind this frame (a WindZone, else a light breeze), read by the foliage shaders.
    struct WindCommand
    {
        Vector3 Direction = Vector3( 0.94f, 0.f, 0.33f );   // horizontal unit vector, blowing towards
        float Strength = 0.3f;          // 0 calm, 1 a strong breeze, 2+ a storm
        float Gusts = 0.4f;             // how much gusts add on top of the strength
        float GustFrequency = 0.15f;    // gust fronts per second
        float Turbulence = 0.3f;        // crosswise, irregular motion
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
            Outlined = false;
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
        // Screen-door dither (ME-279): 0 = solid. d in (0, 1] drops the texels whose 4x4 Bayer
        // threshold is below d (fading out by d); -d keeps only those, the complement, so a d and a
        // -d draw of the same surface cover it exactly once. Shadows dither the same way.
        float DitherFade = 0.f;
        glm::vec4 PortalClipPlane = glm::vec4( 0.f );
        // A LOD crossfade: the level fading out (0xFF = none). Drawn as a second command with the
        // complementary mask (DitherFade negated), so the switch doesn't pop.
        uint8_t CrossfadeLod = 0xFF;
        bool IsTransparent = false;
        bool SupportsInstancing = false;
        bool CastShadows = true;
        // Per-object outline (Outline component): an inverted-hull draw after the opaque scene.
        bool Outlined = false;
        Vector3 OutlineColor;
        float OutlineWidth = 0.f;
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
        uint8_t RenderGroup = 0;
    };

    // Fills the geometry and material fields of a static mesh's command (what RenderCore does for
    // Mesh components; editor previews fill their own). InLod is clamped to the mesh's levels.
    void FillMeshCommand( MeshCommand& OutCommand, MeshData* InMesh, const SharedPtr<Material>& InMaterial, uint8_t InLod = 0 );

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
        Automatic = 0, // follows the procedural sun and supplies moonlight when no explicit Moon exists
        Transform,     // legacy serialized value; resolves as Automatic with a procedural sky
        Sun,
        Moon,
        Manual,        // keeps the entity rotation, colour and intensity even under a procedural sky
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
        // A Decal-target shader graph replacing the built-in decal surface (null: the built-in one).
        SharedPtr<ShaderGraphAsset> Graph;
        // The decal's values for the graph's properties.
        ShaderGraphBinding GraphBinding;
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

        // Extended emitters: projected source area over pi (m^2), added to the squared surface
        // distance in the falloff. An area source's irradiance stays finite on its surface instead
        // of growing as 1/d^2 across the whole face, and still falls off as 1/d^2 far away (exact
        // on-axis for a disc: I / (d^2 + r^2)). Zero for point and spot lights.
        float GetNearFieldArea() const
        {
            constexpr float kInvPi = 0.318309886f;
            if( Type == LightType::Rectangle ) return 4.f * HalfSize.x * HalfSize.y * kInvPi;
            // A convex body's mean projected area is a quarter of its surface area.
            if( Type == LightType::Box ) return 2.f * ( HalfSize.x * HalfSize.y + HalfSize.y * HalfSize.z + HalfSize.z * HalfSize.x ) * kInvPi;
            // The tube side-on (length times diameter) plus its round ends.
            if( Type == LightType::Bar ) return 4.f * HalfSize.x * HalfSize.y * kInvPi + HalfSize.y * HalfSize.y;
            return 0.f;
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

    // Sprites (LOOK-S2): textured quads from SpriteRenderer, drawn after transparents.
    enum class SpriteBlend : uint8_t
    {
        Alpha = 0,
        Additive,
    };

    // One sprite this frame (filled by SpriteCore). The quad lies in the entity's local XY plane,
    // facing local -Z; Size, Pivot and borders are in world units, UVs in 0..1.
    struct SpriteCommand
    {
        glm::mat4 World = glm::mat4( 1.f );
        glm::vec4 UvRect = glm::vec4( 0.f, 0.f, 1.f, 1.f );   // u0, v0 (top), u1, v1 (bottom)
        glm::vec4 Color = glm::vec4( 1.f );                     // linear tint, alpha
        glm::vec2 Size = glm::vec2( 1.f );
        glm::vec2 Pivot = glm::vec2( 0.5f );
        // 9-slice: borders left, top, right, bottom in world units and in UV units (0 = plain quad).
        glm::vec4 Border = glm::vec4( 0.f );
        glm::vec4 BorderUv = glm::vec4( 0.f );
        bool FlipX = false;
        bool FlipY = false;
        bool Lit = false;
        bool PointFilter = false;
        // The texture's alpha is a signed distance field (0.5 = the edge): world-space text glyphs.
        bool Sdf = false;
        SpriteBlend Blend = SpriteBlend::Alpha;
        int32_t Layer = 0;
        int32_t Order = 0;
        uint32_t Sequence = 0;     // gathering order: the last tie-break
        uint64_t ID = 0u;          // the entity (editor picking)
        bgfx::TextureHandle Texture = BGFX_INVALID_HANDLE;     // invalid: white
        // Lit sprites (LOOK-S9): a tangent-space normal map over the same region (invalid: flat).
        bgfx::TextureHandle NormalMap = BGFX_INVALID_HANDLE;
        AABB Bounds;
    };

    // The vertices of a sprite's quad (4) or 9-slice (9 quads, 36) in world space, in quad order
    // (each quad: bottom left, bottom right, top right, top left). Returns the vertex count.
    struct SpriteVertex
    {
        float Position[3];
        float Normal[3];
        float Tangent[4];   // the image's +u direction in world space, w = handedness (flips)
        float Uv[2];
        float Color[4];
    };
    uint32_t BuildSpriteVertices( const SpriteCommand& InSprite, SpriteVertex* OutVertices );
    // The layout SpriteVertex matches (position, normal, tangent, uv, colour).
    const bgfx::VertexLayout& GetSpriteVertexLayout();

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
        // A Particle-target shader graph colouring the quads (null: the built-in particle shader).
        SharedPtr<ShaderGraphAsset> Graph;
        // The particle system's values for the graph's properties.
        ShaderGraphBinding GraphBinding;
    };
}
