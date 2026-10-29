#pragma once
#include "Components/Physics/CharacterController.h"
#include "ECS/EntityHandle.h"
#include "Net/NetCommands.h"
#include <array>
#include <cstdint>
#include <functional>

class PhysicsCore;

// Client-side prediction of the local player's character (NET-4, the Overwatch model).
//
// The owner simulates its pawn with its own commands as soon as it makes them, and keeps the
// last 128 ticks of {command, mover state after the tick}. The server sends the owner its pawn's
// authoritative state for each tick it simulates; Reconcile compares it with the prediction for
// that tick (millimetre tolerances, so float noise across platforms doesn't count). On a
// mismatch it restores the server's state at that tick and re-runs every later tick with the
// recorded commands (PhysicsCore::StepCharacter, the rest of the world left in the present),
// and the jump this makes on screen is smoothed out over SmoothingSeconds. Mismatches before the
// first match are the initial sync (GetInitialSyncCount), not corrections.
class NetPredictor
{
public:
    using CommandHandler = std::function<void( Entity&, const InputCommand& )>;

    static constexpr size_t kHistory = 128;
    float PositionTolerance = 0.001f;   // m
    float VelocityTolerance = 0.01f;    // m/s
    float SmoothingSeconds = 0.1f;

    NetPredictor( PhysicsCore& InPhysics, EntityHandle InPawn, CommandHandler InHandler, float InTickSeconds );

    // Before the tick simulates: the pawn takes the command.
    void Apply( const InputCommand& InCommand );
    // After the tick simulated: what the pawn did with it.
    void Record( uint32_t InTick );
    // The server's state for its pawn after InTick. True when it disagreed (a correction).
    bool Reconcile( uint32_t InTick, const CharacterController::NetState& InServer );
    // Once a frame: the visual error decays.
    void UpdateSmoothing( double InFrameSeconds );

    EntityHandle GetPawn() const { return m_pawn; }
    // Where the pawn is drawn relative to its simulated pose (the remains of corrections).
    Vector3 GetVisualOffset() const { return m_visualOffset; }
    // Mispredictions after the first agreement with the server.
    int GetCorrectionCount() const { return m_corrections; }
    // Before it: the pawn started somewhere the server's had already moved on from (it simulated
    // idle ticks before this client's first command); taking its state is joining, not a miss.
    int GetInitialSyncCount() const { return m_initialSyncs; }
    int GetMatchCount() const { return m_matches; }
    uint32_t GetLastCorrectionTick() const { return m_lastCorrectionTick; }
    int GetLastResimTicks() const { return m_lastResimTicks; }
    double GetLastResimMilliseconds() const { return m_lastResimMs; }

    static bool Matches( const CharacterController::NetState& InA, const CharacterController::NetState& InB, float InPositionTolerance, float InVelocityTolerance );

private:
    struct Frame
    {
        uint32_t Tick = 0;
        bool Valid = false;
        InputCommand Command;
        CharacterController::NetState State;
    };

    PhysicsCore& m_physics;
    EntityHandle m_pawn;
    CommandHandler m_handler;
    float m_tickSeconds = 1.f / 60.f;
    std::array<Frame, kHistory> m_history;
    InputCommand m_pending;
    bool m_hasNewest = false;
    uint32_t m_newest = 0;
    bool m_hasReconciled = false;
    uint32_t m_reconciled = 0;
    int m_corrections = 0;
    int m_initialSyncs = 0;
    int m_matches = 0;
    uint32_t m_lastCorrectionTick = 0;
    int m_lastResimTicks = 0;
    double m_lastResimMs = 0.0;
    Vector3 m_visualOffset;
    Vector3 m_offsetAtCorrection;
    double m_smoothingLeft = 0.0;
};
