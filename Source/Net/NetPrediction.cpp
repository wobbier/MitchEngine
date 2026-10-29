#include "PCH.h"
#include "Net/NetPrediction.h"
#include "Cores/PhysicsCore.h"
#include <chrono>
#include <cmath>


NetPredictor::NetPredictor( PhysicsCore& InPhysics, EntityHandle InPawn, CommandHandler InHandler, float InTickSeconds )
    : m_physics( InPhysics )
    , m_pawn( InPawn )
    , m_handler( std::move( InHandler ) )
    , m_tickSeconds( InTickSeconds )
{
}


bool NetPredictor::Matches( const CharacterController::NetState& InA, const CharacterController::NetState& InB, float InPositionTolerance, float InVelocityTolerance )
{
    auto near = []( const Vector3& InX, const Vector3& InY, float InTolerance ) {
        return std::abs( InX.x - InY.x ) <= InTolerance && std::abs( InX.y - InY.y ) <= InTolerance && std::abs( InX.z - InY.z ) <= InTolerance;
    };
    return near( InA.Position, InB.Position, InPositionTolerance ) && near( InA.Velocity, InB.Velocity, InVelocityTolerance ) && InA.Grounded == InB.Grounded;
}


void NetPredictor::Apply( const InputCommand& InCommand )
{
    m_pending = InCommand;
    if( m_pawn && m_handler )
    {
        m_handler( *m_pawn.Get(), InCommand );
    }
}


void NetPredictor::Record( uint32_t InTick )
{
    CharacterController* controller = m_pawn ? m_pawn->TryGetComponent<CharacterController>() : nullptr;
    if( !controller )
    {
        return;
    }
    Frame& frame = m_history[InTick % kHistory];
    frame.Tick = InTick;
    frame.Valid = true;
    frame.Command = m_pending;
    frame.Command.Tick = InTick;
    frame.State = controller->GetNetState();
    if( !m_hasNewest || InTick > m_newest )
    {
        m_newest = InTick;
        m_hasNewest = true;
    }
}


bool NetPredictor::Reconcile( uint32_t InTick, const CharacterController::NetState& InServer )
{
    CharacterController* controller = m_pawn ? m_pawn->TryGetComponent<CharacterController>() : nullptr;
    if( !controller || ( m_hasReconciled && InTick <= m_reconciled ) )
    {
        return false;
    }
    Frame& frame = m_history[InTick % kHistory];
    if( !frame.Valid || frame.Tick != InTick || !m_hasNewest || InTick > m_newest )
    {
        return false;   // not predicted (yet, or any more)
    }
    m_hasReconciled = true;
    m_reconciled = InTick;
    if( Matches( frame.State, InServer, PositionTolerance, VelocityTolerance ) )
    {
        ++m_matches;
        return false;
    }

    // Wrong: take the server's word for that tick and re-run everything since.
    const auto start = std::chrono::steady_clock::now();
    const Vector3 shownBefore = controller->GetNetState().Position + m_visualOffset;
    controller->SetNetState( InServer );
    frame.State = InServer;
    int resimmed = 0;
    for( uint32_t tick = InTick + 1; tick <= m_newest; ++tick )
    {
        Frame& replay = m_history[tick % kHistory];
        if( !replay.Valid || replay.Tick != tick )
        {
            break;
        }
        if( m_handler )
        {
            m_handler( *m_pawn.Get(), replay.Command );
        }
        m_physics.StepCharacter( *m_pawn.Get(), *controller, m_tickSeconds );
        replay.State = controller->GetNetState();
        ++resimmed;
    }
    // The command the coming tick was given stays in force.
    if( m_handler )
    {
        m_handler( *m_pawn.Get(), m_pending );
    }
    m_lastResimMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
    m_lastResimTicks = resimmed;
    if( m_matches == 0 && m_corrections == 0 )
    {
        ++m_initialSyncs;
        m_visualOffset = Vector3();
        return true;
    }
    ++m_corrections;
    m_lastCorrectionTick = InTick;
    // Draw it where it was, then glide to the corrected pose.
    m_visualOffset = shownBefore - controller->GetNetState().Position;
    m_offsetAtCorrection = m_visualOffset;
    m_smoothingLeft = SmoothingSeconds;
    return true;
}


void NetPredictor::UpdateSmoothing( double InFrameSeconds )
{
    if( m_smoothingLeft <= 0.0 )
    {
        m_visualOffset = Vector3();
        return;
    }
    m_smoothingLeft = std::max( 0.0, m_smoothingLeft - InFrameSeconds );
    m_visualOffset = m_offsetAtCorrection * static_cast<float>( m_smoothingLeft / SmoothingSeconds );
}
