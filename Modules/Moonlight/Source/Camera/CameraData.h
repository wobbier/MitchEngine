#pragma once
#include "Math/Vector3.h"
#include "Math/Vector2.h"
#include "bgfx/bgfx.h"
#include <Math/Matrix4.h>
#include "Math/Frustrum.h"

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

    struct CameraData
    {
        Vector3 Position = Vector3( 0.f, 0.f, 0.f );
        Vector3 Front = Vector3( 0.f, 0.f, 1.f );
        Vector3 Up = Vector3( 0.f, 1.f, 0.f );
        Vector3 ClearColor;
        ClearColorType ClearType = ClearColorType::Color;
        Vector2 OutputSize;
        ProjectionType Projection = ProjectionType::Perspective;
        float FOV = 45.0f;
        float Near = 0.1f;
        float Far = 100.f;
        SkyBox* Skybox = nullptr;
        float OrthographicSize = 1.f;
        bgfx::TextureHandle UITexture = BGFX_INVALID_HANDLE;

        Moonlight::FrameBuffer* Buffer = nullptr;
        glm::vec4 ObliqueData;
        Matrix4 View;
        Matrix4 ProjectionMatrix;
        Frustum ViewFrustum;
        bool IsMain = false;
        bool IsOblique = false;
        bool ShouldRender = true;
        bool ShouldCull = true;

        std::vector<bool> VisibleFlags;
    };
}