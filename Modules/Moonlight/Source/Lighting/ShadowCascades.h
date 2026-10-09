#pragma once
#include "Math/Bounds.h"
#include "Math/Vector3.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace Moonlight
{
    // Cascaded shadow map setup for a directional light. Splits follow the practical (PSSM) scheme;
    // each cascade is fit to the bounding sphere of its frustum slice, so its size never changes as
    // the camera rotates, and its origin is snapped to whole shadow texels so edges don't shimmer
    // while the camera moves.
    struct CascadeInput
    {
        Vector3 CameraPosition;
        Vector3 CameraFront = Vector3( 0.f, 0.f, 1.f );
        Vector3 CameraUp = Vector3( 0.f, 1.f, 0.f );
        float FovDegrees = 45.f;        // vertical
        float Aspect = 16.f / 9.f;
        bool Orthographic = false;
        float OrthographicSize = 1.f;   // half height
        float Near = 0.1f;
        float ShadowDistance = 80.f;    // shadows end here (or at the camera's far plane)
        Vector3 LightDirection = Vector3( 0.f, -1.f, 0.f );  // direction the light travels
        uint32_t Resolution = 2048;     // texels per cascade side
        uint32_t CascadeCount = 4;
        float SplitLambda = 0.75f;      // 0 = uniform splits, 1 = logarithmic
        bool HomogeneousDepth = false;  // clip z in [-1, 1] (OpenGL) instead of [0, 1]
        AABB CasterBounds;              // all shadow casters; extends each cascade towards the light
    };

    struct CascadeSetup
    {
        glm::mat4 View = glm::mat4( 1.f );
        glm::mat4 Projection = glm::mat4( 1.f );
        glm::mat4 ViewProjection = glm::mat4( 1.f );
        float SplitNear = 0.f;
        float SplitFar = 0.f;           // view-space depth where this cascade ends
        float TexelWorldSize = 0.f;     // world size of one shadow texel
        glm::vec3 Center = glm::vec3( 0.f );
        float Radius = 0.f;
    };

    static constexpr uint32_t kMaxCascades = 4;

    // Fills InInput.CascadeCount entries of OutCascades.
    void ComputeCascades( const CascadeInput& InInput, CascadeSetup* OutCascades );

    // Practical split distance i of InCount between near and far.
    float CascadeSplitDistance( uint32_t InIndex, uint32_t InCount, float InNear, float InFar, float InLambda );
}
