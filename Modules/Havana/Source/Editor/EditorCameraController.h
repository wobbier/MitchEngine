#pragma once
#include "Dementia.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Bounds.h"

#if USING( ME_EDITOR )

class Transform;
class Camera;

// Scene view navigation, Unity/Unreal style:
//   RMB drag            fly look; WASD / QE / Space move while held, wheel changes fly speed, Shift = fast
//   Alt + LMB drag      orbit around the pivot
//   MMB drag            pan
//   Alt + RMB drag      dolly
//   Wheel               zoom toward the pivot (orthographic size in ortho views)
//   Focus()             frame bounds (F), animated
class EditorCameraController
{
public:
    struct Input
    {
        bool ViewportHovered = false;
        bool ViewportFocused = false;
        Vector2 ViewportSize;
        Vector2 MouseDelta;
        float Wheel = 0.f;
        bool Left = false;
        bool Middle = false;
        bool Right = false;
        bool LeftPressed = false;
        bool MiddlePressed = false;
        bool RightPressed = false;
        bool Alt = false;
        bool Shift = false;
        bool Forward = false, Back = false, StrafeLeft = false, StrafeRight = false, Up = false, Down = false;
    };

    void Init( Transform& InTransform, Camera& InCamera );
    void Update( float InDeltaSeconds, const Input& InInput );

    // Frames the bounds (animated). Invalid bounds are ignored.
    void Focus( const AABB& InBounds );
    // Snaps to a view direction (degrees). Keeps the pivot.
    void SetAngles( float InPitch, float InYaw );
    void SetOrthographic( bool InOrthographic );
    bool IsOrthographic() const;

    // True while a drag/fly owns the mouse (other scene tools should ignore it).
    bool IsNavigating() const { return m_mode != Mode::None; }
    bool IsFlying() const { return m_mode == Mode::Fly; }

    const Vector3& GetPivot() const { return m_pivot; }
    float GetFlySpeed() const { return m_flySpeed; }
    void SetFlySpeed( float InSpeed );
    float GetPitch() const { return m_pitch; }
    float GetYaw() const { return m_yaw; }

    float LookSensitivity = 0.15f;   // degrees per pixel
    float FocusSmoothing = 14.f;

private:
    enum class Mode { None, Fly, Orbit, Pan, Dolly };

    void Apply();

    Transform* m_transform = nullptr;
    Camera* m_camera = nullptr;
    Mode m_mode = Mode::None;
    Vector3 m_pivot;
    float m_distance = 10.f;
    float m_pitch = 0.f;
    float m_yaw = 0.f;
    float m_flySpeed = 10.f;

    bool m_isFocusing = false;
    Vector3 m_focusPivot;
    float m_focusDistance = 10.f;
};

#endif
