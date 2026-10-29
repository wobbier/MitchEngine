#pragma once
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "bgfx/bgfx.h"
#include <Math/Matrix4.h>
#include "Math/Frustrum.h"
#include "Camera/PostProcessSettings.h"
#include "Camera/ProceduralSkySettings.h"
#include "RenderCommands.h"
#include "Pointers.h"

#include <vector>

namespace Moonlight {
    class SkyBox;
    class Texture;
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

    // How a camera's internal-resolution image is stretched to its output (render scaling, LOOK-S1).
    enum class UpscaleFilter : uint8_t
    {
        Bilinear = 0,
        Nearest,    // blocky at any ratio
        Pixel,      // integer nearest: the scale snaps to 1/N, every texel an exact N x N block
        Sharp,      // Catmull-Rom + contrast-adaptive sharpening (FSR1-style spatial upscale)
    };

    // The internal size a camera renders at for an output size (LOOK-S1): OutputSize * InScale,
    // under InMaxScale (0 = none) and InMaxPixels (0 = none: an enforced hardware profile's
    // resolution). Pixel snaps the scale to 1/N and rounds up, so N x N blocks cover the output.
    // Never below 1 px or above the output.
    void ComputeRenderSize( uint32_t InOutputWidth, uint32_t InOutputHeight, float InScale, UpscaleFilter InFilter, float InMaxScale, uint64_t InMaxPixels, uint32_t& OutWidth, uint32_t& OutHeight, uint32_t& OutPixelFactor );
    // Pixel-perfect cameras (LOOK-S6): the largest whole factor N at which InReference still fits the
    // output (at least 1), and the internal size covering the output in N x N blocks (ceil(out / N):
    // a window that isn't a multiple shows slightly more of the world, never a stretched texel).
    void ComputePixelPerfect( uint32_t InOutputWidth, uint32_t InOutputHeight, uint32_t InReferenceWidth, uint32_t InReferenceHeight, uint32_t& OutFactor, uint32_t& OutWidth, uint32_t& OutHeight );
    // Moves InEye along InRight and InUp so its view-space x and y land on a multiple of InTexel
    // (world units per texel): everything then moves across the screen in whole texels.
    Vector3 SnapToTexelGrid( const Vector3& InEye, const Vector3& InRight, const Vector3& InUp, float InTexel );
    // Dynamic resolution: the next scale for a measured GPU frame time against a target (ms),
    // within [InMinScale, InMaxScale] in steps of 0.05. Drops quickly when over budget, climbs
    // slowly when well under it, holds in between (so targets aren't recreated every frame).
    float StepDynamicScale( float InScale, float InGpuMs, float InTargetMs, float InMinScale, float InMaxScale );

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
        // RmlUi documents, premultiplied, composited over the main camera's output.
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

        // A render texture camera's target (Camera::TargetTexture): its final image goes into the
        // texture, which materials sample, instead of the camera's own output. These cameras render
        // first each frame, and never as the main camera.
        SharedPtr<Texture> Target;
        // Render texture cameras render in this order (the entity's GUID: stable across play,
        // reloads and builds, unlike cache slots).
        uint64_t TargetOrder = 0;
        // Per-camera pass switches (masks over the post settings, like the renderer's feature caps).
        bool SunShadows = true;
        bool AmbientOcclusion = true;
        bool VolumetricFog = true;
        // Render scaling (Camera component, LOOK-S1): the scene and post passes run at
        // OutputSize * RenderScale, and the result is upscaled to OutputSize with Upscale.
        float RenderScale = 1.f;
        UpscaleFilter Upscale = UpscaleFilter::Bilinear;
        float UpscaleSharpness = 0.5f;    // Sharp: 0 = soft, 1 = strongest
        // Dynamic resolution: RenderScale is the ceiling, lowered toward DynamicMinScale while the
        // GPU frame takes longer than DynamicTargetMs.
        bool DynamicResolution = false;
        float DynamicMinScale = 0.5f;
        float DynamicTargetMs = 16.6f;
        // Pixel-perfect (LOOK-S6): RenderScale is 1/N with the Pixel filter, set by CameraCore; caps
        // don't apply (they would break the whole-texel grid) and temporal AA's jitter is off.
        bool PixelPerfect = false;
        // Seconds since this camera last rendered, for eye adaptation (0: one frame).
        float RenderDeltaSeconds = 0.f;

        PostProcessSettings Post;

        // The meshes this camera draws (RenderCommands.h). Cameras of other groups than the world's
        // light with their sky and GroupLights only (directional lights: no local lights or shadows).
        uint8_t RenderGroup = 0;
        std::vector<LightCommand> GroupLights;

        // One byte per renderable (not vector<bool>): mesh jobs write these concurrently.
        std::vector<uint8_t> VisibleFlags;
    };
}
