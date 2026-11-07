#pragma once
#include "Components/Physics/CharacterController.h"
#include "ECS/EntityHandle.h"
#include "Net/NetCommands.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>

class PhysicsCore;

// How the server's word on one predicted tick went (NET-7, the misprediction timeline).
enum class NetPredictOutcome : uint8_t
{
    None,           // not reconciled (no word, or not in the timeline any more)
    Match,          // the prediction was right
    Correction,     // a misprediction: restored and resimulated
    InitialSync,    // a mismatch before the first match: joining, not a miss
};

// What one mismatch found (NET-7, the Overwatch misprediction debugger): the client's prediction
// for Tick against the server's state, the [Predicted] script bytes that differed, and what it
// takes to run the tick again offline (NetMispredict::Repro): the prediction's state after the
// tick before, Tick's command and every later one it resimulated, and where the resim took it.
struct NetMispredictRecord
{
    uint32_t Tick = 0;
    bool InitialSync = false;
    CharacterController::NetState Predicted;        // the client's state after Tick
    CharacterController::NetState Authoritative;    // the server's
    std::vector<uint8_t> PredictedScripts;          // [Predicted] script bytes (empty without hooks)
    std::vector<uint8_t> AuthoritativeScripts;
    std::vector<std::pair<uint32_t, uint32_t>> ScriptDiffs;   // [begin, end) byte ranges that differ
    // The prediction after Tick - 1, which Commands[0] stepped into Predicted. HasStart is false when
    // that tick had left the history (or was never predicted: the first tick).
    bool HasStart = false;
    CharacterController::NetState Start;
    std::vector<uint8_t> StartScripts;
    InputCommand PreviousCommand;                   // Tick - 1's (button edges)
    // Tick's command, then each later one the correction resimulated, up to NewestTick.
    std::vector<InputCommand> Commands;
    // After each resimulated tick (Tick + 1 .. NewestTick): one per Commands[1..].
    std::vector<CharacterController::NetState> Resimulated;
    uint32_t NewestTick = 0;
    float TickSeconds = 1.f / 60.f;
    // How far the correction moved the pawn's present (newest) state: what the visual offset hides.
    Vector3 PresentJump;
    double ResimMilliseconds = 0.0;

    Vector3 PositionDelta() const { return Authoritative.Position - Predicted.Position; }
    Vector3 VelocityDelta() const { return Authoritative.Velocity - Predicted.Velocity; }
    // "position off 512.0 mm (0.5120, 0, 0), velocity off ..., grounded, ground normal, scripts" (what differs).
    std::string Describe() const;
};

// Client-side prediction of the local player's character (NET-4, the Overwatch model).
//
// The owner simulates its pawn with its own commands as soon as it makes them, and keeps the
// last 128 ticks of {command, mover state after the tick}. The server sends the owner its pawn's
// authoritative state for each tick it simulates; Reconcile compares it with the prediction for
// that tick (millimetre tolerances, so float noise across platforms doesn't count). On a
// mismatch it restores the server's state at that tick and re-runs every later tick with the
// recorded commands (PhysicsCore::StepCharacter, the rest of the world left in the present),
// and the jump this makes on screen is smoothed out over SmoothingSeconds (a jump longer than
// MaxSmoothDistance snaps). Mismatches before the first match are the initial sync
// (GetInitialSyncCount), not corrections.
//
// Scripts (NET-6): with ScriptHooks set, the pawn's C# [Predicted] state is part of each tick's
// record and of the comparison (byte for byte), restored with the mover on a mismatch, and the
// pawn's predicted scripts re-run (OnFixedUpdate) in every resimulated tick, before its step.
//
// Debugging (NET-7): every reconciled tick's outcome stays on a timeline for TimelineSeconds, and
// each mismatch's NetMispredictRecord (what differed, and how to run it again offline) for as long
// (at most kMaxMispredicts of them).
class NetPredictor
{
public:
    using CommandHandler = std::function<void( Entity&, const InputCommand& )>;
    struct ScriptHooks
    {
        std::function<void( std::vector<uint8_t>& )> Save;          // the pawn's [Predicted] state now
        std::function<void( const std::vector<uint8_t>& )> Restore;
        std::function<void()> Simulate;                             // one tick of its scripts
    };

    static constexpr size_t kHistory = 128;
    static constexpr double kTimelineSeconds = 10.0;
    static constexpr size_t kMaxMispredicts = 64;
    float PositionTolerance = 0.001f;   // m
    float VelocityTolerance = 0.01f;    // m/s
    float SmoothingSeconds = 0.1f;
    // A correction that would draw the pawn further than this from its corrected pose (a
    // teleport, a respawn) snaps instead of gliding across the level.
    float MaxSmoothDistance = 2.f;      // m

    NetPredictor( PhysicsCore& InPhysics, EntityHandle InPawn, CommandHandler InHandler, float InTickSeconds );

    // Before the tick simulates: the pawn takes the command.
    void Apply( const InputCommand& InCommand );
    // After the tick simulated: what the pawn did with it.
    void Record( uint32_t InTick );
    // The server's state for its pawn after InTick (InScripts: its [Predicted] script state). True
    // when it disagreed (a correction).
    bool Reconcile( uint32_t InTick, const CharacterController::NetState& InServer, const std::vector<uint8_t>& InScripts = {} );
    void SetScriptHooks( ScriptHooks InHooks ) { m_scripts = std::move( InHooks ); }
    // Once a frame: the visual error decays.
    void UpdateSmoothing( double InFrameSeconds );

    EntityHandle GetPawn() const { return m_pawn; }
    // The command the tick being simulated runs with, and the one before it (button edges): the
    // live tick's, or a resimulated tick's from the history.
    const InputCommand& GetCommand() const { return m_resimulating ? m_resimCommand : m_pending; }
    const InputCommand& GetPreviousCommand() const { return m_resimulating ? m_resimPrevious : m_previous; }
    bool IsResimulating() const { return m_resimulating; }
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
    // The [begin, end) byte ranges where two script states differ (a length difference is one).
    static std::vector<std::pair<uint32_t, uint32_t>> DiffBytes( const std::vector<uint8_t>& InA, const std::vector<uint8_t>& InB );

    // The misprediction timeline (NET-7): the last kTimelineSeconds of ticks reconciled.
    uint32_t GetTimelineTicks() const { return static_cast<uint32_t>( m_timeline.size() ); }
    // The newest tick the server has had its say on (false before the first).
    bool GetNewestReconciled( uint32_t& OutTick ) const { OutTick = m_reconciled; return m_hasReconciled; }
    // InTick's outcome, None once it's older than the timeline.
    NetPredictOutcome GetOutcome( uint32_t InTick ) const;
    // Every mismatch on the timeline (oldest first), initial syncs included; one ages off with its
    // tick once a tick kTimelineSeconds newer is reconciled, match or not. Under a steady desync
    // only the newest kMaxMispredicts keep a record: GetOutcome still names the older ones.
    const std::deque<NetMispredictRecord>& GetMispredicts() const { return m_mispredicts; }
    const NetMispredictRecord* FindMispredict( uint32_t InTick ) const;
    // The newest tick the server has had its say on, its state then and what this client had
    // predicted for it (NET-7's server ghost): false before the first.
    bool GetLastReconciled( uint32_t& OutTick, CharacterController::NetState& OutServer, CharacterController::NetState& OutPredicted ) const
    {
        OutTick = m_reconciled;
        OutServer = m_lastServer;
        OutPredicted = m_lastPredicted;
        return m_hasReconciled;
    }

private:
    struct Frame
    {
        uint32_t Tick = 0;
        bool Valid = false;
        InputCommand Command;
        CharacterController::NetState State;
        std::vector<uint8_t> Scripts;
    };

    struct TimelineEntry
    {
        uint32_t Tick = 0;
        NetPredictOutcome Outcome = NetPredictOutcome::None;
    };
    void NoteOutcome( uint32_t InTick, NetPredictOutcome InOutcome );
    // Drops the records older than the timeline (from the newest reconciled tick) and past kMaxMispredicts.
    void PruneMispredicts();

    PhysicsCore& m_physics;
    EntityHandle m_pawn;
    CommandHandler m_handler;
    ScriptHooks m_scripts;
    float m_tickSeconds = 1.f / 60.f;
    std::array<Frame, kHistory> m_history;
    InputCommand m_pending;
    InputCommand m_previous;
    InputCommand m_resimCommand;
    InputCommand m_resimPrevious;
    bool m_resimulating = false;
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
    CharacterController::NetState m_lastServer;     // the newest reconciled tick's
    CharacterController::NetState m_lastPredicted;
    std::vector<TimelineEntry> m_timeline;          // by tick % size
    std::deque<NetMispredictRecord> m_mispredicts;
};


// A misprediction outside the game (NET-7): written as JSON (.tmp/Net/Mispredict_<tick>.json from
// the Net Debugger or the console), read back, and run again through PhysicsCore::StepCharacter so
// a desync reproduces offline (in a doctest, against the same level geometry).
namespace NetMispredict
{
    // Every field at full precision (floats round-trip exactly), script bytes in hex.
    std::string ToJson( const NetMispredictRecord& InRecord );
    bool FromJson( const std::string& InText, NetMispredictRecord& OutRecord, std::string* OutError = nullptr );
    // Writes InRecord to InPath (folders made), now; Export does it on the background lane.
    bool Save( const NetMispredictRecord& InRecord, const std::string& InPath, std::string* OutError = nullptr );
    bool Load( const std::string& InPath, NetMispredictRecord& OutRecord, std::string* OutError = nullptr );
    std::future<bool> Export( NetMispredictRecord InRecord, std::string InPath );
    // "<folder>/Mispredict_<tick>.json"
    std::string DefaultPath( const std::string& InFolder, uint32_t InTick );

    struct Repro
    {
        std::string Error;          // couldn't run (no start state, no controller)
        // From Start, Commands[0] stepped: the client's prediction again, and whether it's the
        // recorded one bit for bit (the client's side of the desync, reproduced).
        CharacterController::NetState Predicted;
        bool PredictedMatches = false;
        // From Authoritative, Commands[1..] stepped: the correction's resim again, and how many
        // ticks of it are the recorded ones bit for bit (all of them: deterministic).
        std::vector<CharacterController::NetState> Resimulated;
        size_t ResimMatches = 0;
        bool Ran() const { return Error.empty(); }
        bool Reproduced( const NetMispredictRecord& InRecord ) const { return Ran() && PredictedMatches && ResimMatches == InRecord.Resimulated.size(); }
    };
    // InPawn (a CharacterController in a world with the recording's level) is put in the record's
    // states and stepped with its commands: InHandler applies each, as the predictor does, then
    // PhysicsCore::StepCharacter. Only the mover: [Predicted] script state isn't re-run here. The
    // pawn is left in the resim's last state.
    Repro Run( PhysicsCore& InPhysics, Entity& InPawn, const NetMispredictRecord& InRecord, const std::function<void( Entity&, const InputCommand& )>& InHandler );
}
