#pragma once
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "bgfx/bgfx.h"
#include <Math/Matrix4.h>
#include "Math/Frustrum.h"
#include "Camera/PostProcessSettings.h"
#include "Camera/ProceduralSkySettings.h"
#include "RenderCommands.h"

#include <vector>

namespace Moonlight {
    class SkyBox;
}
namespace Moonlight {
    struct FrameBuffer;
}

namespace Moonlight
{
    enum class ProjectionType : uint8_t
    {
        Perspective = 0,
        Orthographic
    };

    enum class ClearColorType : uint8_t
    {
        Color = 0,
        Skybox,
        Procedural
    };

    // Per-viewport inspection override; never serialized into a scene's camera or materials.
    // Values match u_viewShading.x in Standard.frag.
    enum class ViewShadingMode : uint8_t
    {
        Lit = 0,
        Unlit,
        Solid,
        Normals,
        Wireframe,
        VertexNormals,
        Metallic,
        Roughness,
        Occlusion,
        Emissive,
        UVChecker,
        Depth,
        LightingOnly,
        Overdraw,
        Count
    };

    // Editor viewport overrides. Defaults preserve the camera's authored rendering.
    struct ViewRenderOptions
    {
        bool PostProcessing = true;
        bool Fog = true;
        bool Background = true;
        bool Transparencies = true;
        bool Particles = true;
        bool Decals = true;
        bool DebugDraw = true;
        bool BackfaceCulling = true;
        bool GameUI = true;
        float Exposure = 0.f;       // additional EV for lit views
        float DepthRange = 50.f;    // metres, near = white, range and beyond = black
    };

    struct CameraData
    {
        Vector3 Position = Vector3( 0.f, 0.f, 0.f );
        Vector3 Front = Vector3( 0.f, 0.f, 1.f );
        Vector3 Up = Vector3( 0.f, 1.f, 0.f );
        Vector3 ClearColor;
        ClearColorType ClearType = ClearColorType::Color;
        ProceduralSkySettings ProceduralSky;
        Vector2 OutputSize;
        ProjectionType Projection = ProjectionType::Perspective;
        float FOV = 45.0f;
        float Near = 0.1f;
        float Far = 100.f;
        SkyBox* Skybox = nullptr;
        float OrthographicSize = 1.f;
        bgfx::TextureHandle UITexture = BGFX_INVALID_HANDLE;
        // RmlUi documents, premultiplied, composited over the output like UITexture.
        bgfx::TextureHandle RmlTexture = BGFX_INVALID_HANDLE;

        Moonlight::FrameBuffer* Buffer = nullptr;
        glm::vec4 ObliqueData;
        Matrix4 View;
        Matrix4 ProjectionMatrix;
        Frustum ViewFrustum;
        bool IsMain = false;
        // Normalized rect (x, y from the top left, width, height) of the main output this camera
        // draws into; a partial rect is split screen and OutputSize is already the rect's size.
        glm::vec4 Viewport = glm::vec4( 0.f, 0.f, 1.f, 1.f );
        bool IsOblique = false;
        bool ShouldRender = true;
        bool ShouldCull = true;
        // The editor scene view (gets editor-only debug drawing: grid, selection, gizmos).
        bool IsEditorView = false;
        ViewShadingMode Shading = ViewShadingMode::Lit;
        ViewRenderOptions ViewOptions;

        PostProcessSettings Post;

        // The meshes this camera draws (RenderCommands.h). Cameras of other groups than the world's
        // light with their sky and GroupLights only (directional lights: no local lights or shadows).
        uint8_t RenderGroup = 0;
        std::vector<LightCommand> GroupLights;

        // One byte per renderable (not vector<bool>): mesh jobs write these concurrently.
        std::vector<uint8_t> VisibleFlags;
    };
}
