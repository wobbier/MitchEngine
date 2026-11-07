#pragma once
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <cstdint>

class NetCore;
class NetShotLedger;
struct NetReplayHitbox;

// World overlays for net debugging (NET-7), drawn with DebugDraw on top of the scene (no depth
// test), shared by the client's Net Debugger, the server's lag-compensation overlays and the replay
// viewer (ME-316) so a hitbox looks the same in all three. Headless builds draw nothing.
namespace NetDebugDraw
{
    // A hitbox: a capsule along its rotation's up (HalfHeight from the centre to each cap's centre)
    // or an oriented box (HalfExtents).
    void Hitbox( uint8_t InShape, const Vector3& InPosition, const Quaternion& InRotation, float InRadius, float InHalfHeight, const Vector3& InHalfExtents,
        const Vector4& InColor, float InDuration = 0.f );
    void Hitbox( const NetReplayHitbox& InHitbox, const Vector4& InColor, float InDuration = 0.f );

    // Every shot the ledger settled in the last NetDebug::kShowSeconds: the client's ray, its hit
    // point and its view of the target's hitboxes in green; the server's hit point in blue, its rewound
    // hitboxes in yellow and their live poses in grey (a cyan line from rewound to live). A shot the
    // two disagree on draws its rays red.
    void Shots( const NetShotLedger& InLedger, double InNow );

    // The predicted pawn: the server's state for the newest tick it judged (a blue capsule), what this
    // client had predicted for that tick (green when they agree, red when not), and while a correction's
    // visual offset lasts, the uncorrected position it's drawn from (an orange capsule, a line to the
    // simulated one).
    void Ghost( NetCore& InNet );

    inline const Vector4 kClient( 0.35f, 0.9f, 0.35f, 1.f );     // green
    inline const Vector4 kServerHit( 0.3f, 0.5f, 1.f, 1.f );     // blue
    inline const Vector4 kRewound( 1.f, 0.86f, 0.1f, 1.f );      // yellow
    inline const Vector4 kLive( 0.5f, 0.5f, 0.5f, 1.f );         // grey
}
