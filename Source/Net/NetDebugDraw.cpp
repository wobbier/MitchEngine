#include "PCH.h"
#include "Net/NetDebugDraw.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "Debug/DebugDraw.h"
#include "Net/NetDebug.h"
#include "Net/NetReplay.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

void NetDebugDraw::Hitbox( uint8_t InShape, const Vector3& InPosition, const Quaternion& InRotation, float InRadius, float InHalfHeight, const Vector3& InHalfExtents,
    const Vector4& InColor, float InDuration )
{
#if !USING( ME_HEADLESS )
    if( InShape == 0 )
    {
        const Vector3 up = InRotation * Vector3( 0.f, InHalfHeight, 0.f );
        DebugDraw::Capsule( InPosition - up, InPosition + up, InRadius, InColor, InDuration, DebugDraw::NoDepthTest );
    }
    else
    {
        const Matrix4 transform( glm::translate( glm::mat4( 1.f ), InPosition.InternalVector ) * glm::mat4_cast( InRotation.InternalQuat ) );
        DebugDraw::Box( transform, AABB( Vector3() - InHalfExtents, InHalfExtents ), InColor, InDuration, DebugDraw::NoDepthTest );
    }
#else
    (void)InShape; (void)InPosition; (void)InRotation; (void)InRadius; (void)InHalfHeight; (void)InHalfExtents; (void)InColor; (void)InDuration;
#endif
}


void NetDebugDraw::Hitbox( const NetReplayHitbox& InHitbox, const Vector4& InColor, float InDuration )
{
    Hitbox( InHitbox.Shape, InHitbox.Position, InHitbox.Rotation, InHitbox.Radius, InHitbox.HalfHeight, InHitbox.HalfExtents, InColor, InDuration );
}


void NetDebugDraw::Shots( const NetShotLedger& InLedger, double InNow )
{
#if !USING( ME_HEADLESS )
    for( const NetShotComparison& shot : InLedger.GetRecent() )
    {
        if( InNow - shot.At > NetDebug::kShowSeconds )
        {
            continue;
        }
        const bool disagree = shot.HasClient && shot.HasServer && !shot.Agrees();
        if( shot.HasClient )
        {
            const NetPredictedShot& client = shot.Client;
            const Vector3 end = client.Hit || client.Occluded ? client.Point : client.Origin + client.Direction * client.Range;
            DebugDraw::Line( client.Origin, end, disagree ? DebugDraw::Red : kClient, 0.f, DebugDraw::NoDepthTest );
            if( client.Hit )
            {
                DebugDraw::Sphere( client.Point, 0.05f, kClient, 0.f, DebugDraw::NoDepthTest, 12 );
            }
            for( const NetReplayHitbox& hitbox : client.Hitboxes )
            {
                Hitbox( hitbox, kClient );
            }
        }
        if( shot.HasServer )
        {
            const NetReplayShot& server = shot.Server.Shot;
            if( !shot.HasClient )
            {
                const Vector3 end = server.Hit || server.Occluded ? server.Point : server.Origin + server.Direction * server.Range;
                DebugDraw::Line( server.Origin, end, kServerHit, 0.f, DebugDraw::NoDepthTest );
            }
            if( server.Hit || server.Occluded )
            {
                DebugDraw::Sphere( server.Point, 0.08f, server.Hit ? kServerHit : DebugDraw::Orange, 0.f, DebugDraw::NoDepthTest, 12 );
            }
            for( const NetReplayHitbox& hitbox : server.Hitboxes )
            {
                Hitbox( hitbox, kRewound );
                for( const NetReplayHitbox& live : shot.Server.Live )
                {
                    if( live.NetId == hitbox.NetId && live.Name == hitbox.Name )
                    {
                        Hitbox( live, kLive );
                        DebugDraw::Line( hitbox.Position, live.Position, DebugDraw::Cyan, 0.f, DebugDraw::NoDepthTest );
                    }
                }
            }
        }
    }
#else
    (void)InLedger; (void)InNow;
#endif
}


void NetDebugDraw::Ghost( NetCore& InNet )
{
#if !USING( ME_HEADLESS )
    NetPredictor* predictor = InNet.GetPredictor();
    EntityHandle pawn = predictor ? predictor->GetPawn() : EntityHandle();
    const CharacterController* controller = pawn ? pawn->TryGetComponent<CharacterController>() : nullptr;
    if( !controller )
    {
        return;
    }
    // The controller's capsule about its NetState position (the body's centre).
    const float half = std::max( 0.f, controller->Height * 0.5f - controller->Radius );
    auto capsule = [&]( const Vector3& InCentre, const Vector4& InColor ) {
        DebugDraw::Capsule( InCentre - Vector3( 0.f, half, 0.f ), InCentre + Vector3( 0.f, half, 0.f ), controller->Radius, InColor, 0.f, DebugDraw::NoDepthTest );
    };
    uint32_t tick = 0;
    CharacterController::NetState server;
    CharacterController::NetState predicted;
    if( predictor->GetLastReconciled( tick, server, predicted ) )
    {
        const bool agree = NetPredictor::Matches( predicted, server, predictor->PositionTolerance, predictor->VelocityTolerance );
        capsule( server.Position, kServerHit );
        if( !agree )
        {
            capsule( predicted.Position, DebugDraw::Red );
            DebugDraw::Line( predicted.Position, server.Position, DebugDraw::Red, 0.f, DebugDraw::NoDepthTest );
        }
    }
    // A correction being smoothed out: the uncorrected pose (the simulated one plus the whole offset
    // the correction left, i.e. where the pawn would be drawn had it not been corrected), and a line
    // from it to where the pawn is drawn now (the simulated pose plus what's left of the offset).
    const Vector3 correction = predictor->GetCorrectionOffset();
    if( correction.Length() > 0.001f )
    {
        const Vector3 simulated = controller->GetNetState().Position;
        const Vector3 drawn = simulated + predictor->GetVisualOffset();
        capsule( simulated + correction, DebugDraw::Orange );
        DebugDraw::Line( simulated + correction, drawn, DebugDraw::Orange, 0.f, DebugDraw::NoDepthTest );
    }
#else
    (void)InNet;
#endif
}
