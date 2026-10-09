#include "EditorCameraController.h"

#if USING( ME_EDITOR )

#include "Components/Transform.h"
#include "Components/Camera.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kDegToRad = 3.14159265f / 180.f;
    constexpr float kMinDistance = 0.05f;
    constexpr float kMaxDistance = 100000.f;
}


void EditorCameraController::Init( Transform& InTransform, Camera& InCamera )
{
    m_transform = &InTransform;
    m_camera = &InCamera;
    const Vector3 euler = InTransform.GetRotationEuler();
    m_pitch = std::clamp( euler.x, -89.9f, 89.9f );
    m_yaw = euler.y;
    InTransform.SetRotation( Vector3( m_pitch, m_yaw, 0.f ) );
    m_pivot = InTransform.GetPosition() + InTransform.Front() * m_distance;
    Apply();
}


void EditorCameraController::Apply()
{
    if( !m_transform )
    {
        return;
    }
    m_transform->SetRotation( Vector3( m_pitch, m_yaw, 0.f ) );
    // Use the transform's own basis so the camera position is exactly pivot - Front * distance.
    m_transform->SetPosition( m_pivot - m_transform->Front() * m_distance );
}


void EditorCameraController::SetFlySpeed( float InSpeed )
{
    m_flySpeed = std::clamp( InSpeed, 0.1f, 500.f );
}


void EditorCameraController::Focus( const AABB& InBounds )
{
    if( !InBounds.IsValid() || !m_camera )
    {
        return;
    }
    const float radius = std::max( InBounds.GetExtents().Length(), 0.25f );
    // Fit the bounding sphere in the narrower of the vertical and horizontal fields of view.
    const float halfVertical = std::max( m_camera->GetFOV(), 10.f ) * 0.5f * kDegToRad;
    const float aspect = m_camera->OutputSize.y > 0.f ? m_camera->OutputSize.x / m_camera->OutputSize.y : 1.f;
    const float halfHorizontal = std::atan( std::tan( halfVertical ) * aspect );
    const float halfFov = std::min( halfVertical, halfHorizontal );
    m_focusPivot = InBounds.GetCenter();
    m_focusDistance = std::clamp( radius / std::sin( halfFov ) * 1.15f, kMinDistance, kMaxDistance );
    if( IsOrthographic() && m_camera->OutputSize.y > 0.f )
    {
        // Ortho half-height is OutputSize.y / OrthographicSize.
        m_camera->OrthographicSize = std::max( m_camera->OutputSize.y / ( radius * 1.2f ), 0.01f );
    }
    m_isFocusing = true;
}


void EditorCameraController::SetAngles( float InPitch, float InYaw )
{
    m_pitch = std::clamp( InPitch, -89.9f, 89.9f );
    m_yaw = InYaw;
    Apply();
}


void EditorCameraController::SetOrthographic( bool InOrthographic )
{
    if( m_camera )
    {
        m_camera->Projection = InOrthographic ? Moonlight::ProjectionType::Orthographic : Moonlight::ProjectionType::Perspective;
    }
}


bool EditorCameraController::IsOrthographic() const
{
    return m_camera && m_camera->Projection == Moonlight::ProjectionType::Orthographic;
}


void EditorCameraController::Update( float InDeltaSeconds, const Input& InInput )
{
    if( !m_transform || !m_camera )
    {
        return;
    }

    // Start a navigation mode only from inside the viewport; keep it while the button is held.
    if( m_mode == Mode::None && InInput.ViewportHovered )
    {
        if( InInput.RightPressed )
        {
            m_mode = InInput.Alt ? Mode::Dolly : Mode::Fly;
        }
        else if( InInput.LeftPressed && InInput.Alt )
        {
            m_mode = Mode::Orbit;
        }
        else if( InInput.MiddlePressed )
        {
            m_mode = Mode::Pan;
        }
        if( m_mode != Mode::None )
        {
            m_isFocusing = false;
        }
    }
    if( ( m_mode == Mode::Fly || m_mode == Mode::Dolly ) && !InInput.Right ) m_mode = Mode::None;
    if( m_mode == Mode::Orbit && !InInput.Left ) m_mode = Mode::None;
    if( m_mode == Mode::Pan && !InInput.Middle ) m_mode = Mode::None;

    const Vector2& delta = InInput.MouseDelta;
    const float viewportHeight = std::max( InInput.ViewportSize.y, 1.f );

    switch( m_mode )
    {
    case Mode::Fly:
    {
        // Look around the camera position (the pivot moves with it).
        const Vector3 position = m_transform->GetPosition();
        m_yaw += delta.x * LookSensitivity;
        m_pitch = std::clamp( m_pitch + delta.y * LookSensitivity, -89.9f, 89.9f );
        m_transform->SetRotation( Vector3( m_pitch, m_yaw, 0.f ) );

        if( InInput.Wheel != 0.f )
        {
            SetFlySpeed( m_flySpeed * std::pow( 1.2f, InInput.Wheel ) );
        }
        const float speed = m_flySpeed * ( InInput.Shift ? 3.f : 1.f ) * InDeltaSeconds;
        Vector3 move;
        if( InInput.Forward ) move += m_transform->Front();
        if( InInput.Back ) move -= m_transform->Front();
        if( InInput.StrafeRight ) move += m_transform->Right();
        if( InInput.StrafeLeft ) move -= m_transform->Right();
        if( InInput.Up ) move += Vector3::Up;
        if( InInput.Down ) move -= Vector3::Up;
        const Vector3 newPosition = position + move * speed;
        m_pivot = newPosition + m_transform->Front() * m_distance;
        m_transform->SetPosition( newPosition );
        return;
    }
    case Mode::Orbit:
        m_yaw += delta.x * LookSensitivity;
        m_pitch = std::clamp( m_pitch + delta.y * LookSensitivity, -89.9f, 89.9f );
        break;
    case Mode::Pan:
    {
        // World units per pixel at the pivot distance.
        float unitsPerPixel = 2.f * m_distance * std::tan( std::max( m_camera->GetFOV(), 1.f ) * 0.5f * kDegToRad ) / viewportHeight;
        if( IsOrthographic() )
        {
            unitsPerPixel = 2.f / std::max( m_camera->OrthographicSize, 0.001f );
        }
        m_pivot -= m_transform->Right() * ( delta.x * unitsPerPixel );
        m_pivot += m_transform->Up() * ( delta.y * unitsPerPixel );
        break;
    }
    case Mode::Dolly:
        m_distance = std::clamp( m_distance * std::exp( delta.y * 0.01f ), kMinDistance, kMaxDistance );
        break;
    case Mode::None:
        if( InInput.ViewportHovered && InInput.Wheel != 0.f )
        {
            if( IsOrthographic() )
            {
                m_camera->OrthographicSize = std::clamp( m_camera->OrthographicSize * std::pow( 1.15f, InInput.Wheel ), 0.01f, 100000.f );
            }
            else
            {
                const float target = m_distance * std::pow( 0.85f, InInput.Wheel );
                if( target < kMinDistance * 4.f )
                {
                    // Too close to the pivot: push the pivot forward instead of stalling.
                    m_pivot += m_transform->Front() * ( m_distance * 0.5f );
                }
                else
                {
                    m_distance = std::min( target, kMaxDistance );
                }
            }
            m_isFocusing = false;
        }
        break;
    }

    if( m_isFocusing )
    {
        const float t = 1.f - std::exp( -FocusSmoothing * InDeltaSeconds );
        m_pivot = m_pivot + ( m_focusPivot - m_pivot ) * t;
        m_distance = m_distance + ( m_focusDistance - m_distance ) * t;
        if( ( m_focusPivot - m_pivot ).Length() < 0.001f && std::fabs( m_focusDistance - m_distance ) < 0.001f )
        {
            m_pivot = m_focusPivot;
            m_distance = m_focusDistance;
            m_isFocusing = false;
        }
    }
    Apply();
}

#endif
