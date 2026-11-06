#include "PCH.h"
#include "Net/NetPrediction.h"
#include "Cores/PhysicsCore.h"
#include <chrono>
#include <cmath>
#include <cstdio>


NetPredictor::NetPredictor( PhysicsCore& InPhysics, EntityHandle InPawn, CommandHandler InHandler, float InTickSeconds )
    : m_physics( InPhysics )
    , m_pawn( InPawn )
    , m_handler( std::move( InHandler ) )
    , m_tickSeconds( InTickSeconds )
{
    m_timeline.resize( static_cast<size_t>( std::ceil( kTimelineSeconds / std::max( InTickSeconds, 1e-4f ) ) ) );
}


std::string NetMispredictRecord::Describe() const
{
    char line[512];
    std::string out;
    const Vector3 position = PositionDelta();
    if( position.LengthSquared() > 0.f )
    {
        std::snprintf( line, sizeof( line ), "position off %.1f mm (%.4f, %.4f, %.4f)", position.Length() * 1000.f, position.x, position.y, position.z );
        out += line;
    }
    const Vector3 velocity = VelocityDelta();
    if( velocity.LengthSquared() > 0.f )
    {
        std::snprintf( line, sizeof( line ), "%svelocity off %.3f m/s (%.3f, %.3f, %.3f)", out.empty() ? "" : ", ", velocity.Length(), velocity.x, velocity.y, velocity.z );
        out += line;
    }
    if( Predicted.Grounded != Authoritative.Grounded )
    {
        out += std::string( out.empty() ? "" : ", " ) + ( Authoritative.Grounded ? "grounded on the server, not here" : "grounded here, not on the server" );
    }
    if( ( Predicted.GroundNormal - Authoritative.GroundNormal ).LengthSquared() > 0.f )
    {
        std::snprintf( line, sizeof( line ), "%sground normal (%.3f, %.3f, %.3f) here, (%.3f, %.3f, %.3f) on the server", out.empty() ? "" : ", ", Predicted.GroundNormal.x,
            Predicted.GroundNormal.y, Predicted.GroundNormal.z, Authoritative.GroundNormal.x, Authoritative.GroundNormal.y, Authoritative.GroundNormal.z );
        out += line;
    }
    for( const auto& [begin, end] : ScriptDiffs )
    {
        std::snprintf( line, sizeof( line ), "%sscript bytes %u-%u", out.empty() ? "" : ", ", begin, end - 1 );
        out += line;
    }
    return out.empty() ? std::string( "no difference" ) : out;
}


std::vector<std::pair<uint32_t, uint32_t>> NetPredictor::DiffBytes( const std::vector<uint8_t>& InA, const std::vector<uint8_t>& InB )
{
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    const size_t common = std::min( InA.size(), InB.size() );
    for( size_t i = 0; i < common; ++i )
    {
        if( InA[i] == InB[i] )
        {
            continue;
        }
        size_t end = i + 1;
        while( end < common && InA[end] != InB[end] )
        {
            ++end;
        }
        ranges.emplace_back( static_cast<uint32_t>( i ), static_cast<uint32_t>( end ) );
        i = end;
    }
    if( InA.size() != InB.size() )
    {
        // The longer one's tail (joined to a difference that runs up to it).
        const uint32_t begin = static_cast<uint32_t>( common );
        if( !ranges.empty() && ranges.back().second == begin )
        {
            ranges.back().second = static_cast<uint32_t>( std::max( InA.size(), InB.size() ) );
        }
        else
        {
            ranges.emplace_back( begin, static_cast<uint32_t>( std::max( InA.size(), InB.size() ) ) );
        }
    }
    return ranges;
}


void NetPredictor::NoteOutcome( uint32_t InTick, NetPredictOutcome InOutcome )
{
    if( m_timeline.empty() )
    {
        return;
    }
    TimelineEntry& entry = m_timeline[InTick % m_timeline.size()];
    entry.Tick = InTick;
    entry.Outcome = InOutcome;
}


NetPredictOutcome NetPredictor::GetOutcome( uint32_t InTick ) const
{
    if( m_timeline.empty() || !m_hasReconciled || InTick > m_reconciled || m_reconciled - InTick >= m_timeline.size() )
    {
        return NetPredictOutcome::None;
    }
    const TimelineEntry& entry = m_timeline[InTick % m_timeline.size()];
    return entry.Tick == InTick ? entry.Outcome : NetPredictOutcome::None;
}


const NetMispredictRecord* NetPredictor::FindMispredict( uint32_t InTick ) const
{
    for( const NetMispredictRecord& record : m_mispredicts )
    {
        if( record.Tick == InTick )
        {
            return &record;
        }
    }
    return nullptr;
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
    m_previous = m_pending;
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
    frame.Scripts.clear();
    if( m_scripts.Save )
    {
        m_scripts.Save( frame.Scripts );
    }
    if( !m_hasNewest || InTick > m_newest )
    {
        m_newest = InTick;
        m_hasNewest = true;
    }
}


bool NetPredictor::Reconcile( uint32_t InTick, const CharacterController::NetState& InServer, const std::vector<uint8_t>& InScripts )
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
    // Script state is exact (the same code ran on the same inputs); without hooks it isn't compared.
    const bool scriptsMatch = !m_scripts.Save || frame.Scripts == InScripts;
    if( scriptsMatch && Matches( frame.State, InServer, PositionTolerance, VelocityTolerance ) )
    {
        ++m_matches;
        NoteOutcome( InTick, NetPredictOutcome::Match );
        return false;
    }

    // Wrong: take the server's word for that tick and re-run everything since. What differed, and
    // what it takes to run it again, go on the misprediction timeline first.
    const auto start = std::chrono::steady_clock::now();
    NetMispredictRecord record;
    record.Tick = InTick;
    record.Predicted = frame.State;
    record.Authoritative = InServer;
    if( m_scripts.Save )
    {
        record.PredictedScripts = frame.Scripts;
        record.AuthoritativeScripts = InScripts;
        record.ScriptDiffs = DiffBytes( frame.Scripts, InScripts );
    }
    if( const Frame& before = m_history[( InTick - 1 ) % kHistory]; InTick > 0 && before.Valid && before.Tick == InTick - 1 )
    {
        record.HasStart = true;
        record.Start = before.State;
        record.StartScripts = before.Scripts;
        record.PreviousCommand = before.Command;
    }
    record.Commands.push_back( frame.Command );
    const Vector3 presentBefore = controller->GetNetState().Position;
    const Vector3 shownBefore = presentBefore + m_visualOffset;
    controller->SetNetState( InServer );
    frame.State = InServer;
    if( m_scripts.Restore )
    {
        m_scripts.Restore( InScripts );
        frame.Scripts = InScripts;
    }
    int resimmed = 0;
    m_resimulating = true;
    for( uint32_t tick = InTick + 1; tick <= m_newest; ++tick )
    {
        Frame& replay = m_history[tick % kHistory];
        if( !replay.Valid || replay.Tick != tick )
        {
            break;
        }
        const Frame& before = m_history[( tick - 1 ) % kHistory];
        m_resimCommand = replay.Command;
        m_resimPrevious = before.Valid && before.Tick == tick - 1 ? before.Command : InputCommand();
        if( m_handler )
        {
            m_handler( *m_pawn.Get(), replay.Command );
        }
        // The pawn's scripts, then its step: the order of a live tick (scripts, then physics).
        if( m_scripts.Simulate )
        {
            m_scripts.Simulate();
        }
        m_physics.StepCharacter( *m_pawn.Get(), *controller, m_tickSeconds );
        replay.State = controller->GetNetState();
        if( m_scripts.Save )
        {
            replay.Scripts.clear();
            m_scripts.Save( replay.Scripts );
        }
        record.Commands.push_back( replay.Command );
        record.Resimulated.push_back( replay.State );
        ++resimmed;
    }
    m_resimulating = false;
    // The command the coming tick was given stays in force.
    if( m_handler )
    {
        m_handler( *m_pawn.Get(), m_pending );
    }
    m_lastResimMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
    m_lastResimTicks = resimmed;
    record.NewestTick = InTick + static_cast<uint32_t>( resimmed );
    record.PresentJump = controller->GetNetState().Position - presentBefore;
    record.ResimMilliseconds = m_lastResimMs;
    record.InitialSync = m_matches == 0 && m_corrections == 0;
    NoteOutcome( InTick, record.InitialSync ? NetPredictOutcome::InitialSync : NetPredictOutcome::Correction );
    m_mispredicts.push_back( std::move( record ) );
    while( m_mispredicts.size() > kMaxMispredicts || ( !m_mispredicts.empty() && InTick - m_mispredicts.front().Tick >= m_timeline.size() ) )
    {
        m_mispredicts.pop_front();
    }
    if( m_matches == 0 && m_corrections == 0 )
    {
        ++m_initialSyncs;
        m_visualOffset = Vector3();
        return true;
    }
    ++m_corrections;
    m_lastCorrectionTick = InTick;
    // Draw it where it was, then glide to the corrected pose; a jump too far to glide (the server
    // teleported it) snaps.
    m_visualOffset = shownBefore - controller->GetNetState().Position;
    if( m_visualOffset.LengthSquared() > MaxSmoothDistance * MaxSmoothDistance )
    {
        m_visualOffset = Vector3();
    }
    m_offsetAtCorrection = m_visualOffset;
    m_smoothingLeft = m_visualOffset.IsZero() ? 0.0 : SmoothingSeconds;
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
