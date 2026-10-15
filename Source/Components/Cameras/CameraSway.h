#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Math/Quaternion.h"

// Camera-local motion applied to the rendered view, leaving the authored/controller pose intact.
// CameraCore advances it during Play; no scene core is required.
class CameraSway
    : public Component<CameraSway>
{
    ME_REFLECTABLE( CameraSway )
public:
    CameraSway();

    Vector3 PositionAmplitude = Vector3( 0.015f, 0.025f, 0.01f );
    Vector3 RotationAmplitude = Vector3( 0.25f, 0.35f, 0.15f );
    float Frequency = 0.18f;
    float Handheld = 0.15f;

    void Advance( float InDeltaTime );
    void Reset();
    // Apply once to a fresh world pose each frame. Position is in metres, rotation in degrees.
    void Apply( Vector3& InOutPosition, Quaternion& InOutRotation ) const;
    void OnDisable() override;

private:
    double m_phase = 0.0;
};
ME_REGISTER_COMPONENT_FOLDER( CameraSway, "Rendering" )
