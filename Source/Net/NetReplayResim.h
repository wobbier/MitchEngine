#pragma once
#include "Components/Physics/CharacterController.h"
#include "Net/NetCommands.h"
#include "Net/NetReplay.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Resim check (ME-316 phase 3): does a character's recorded movement follow from its owner's
// recorded commands? From a tick's recorded mover (CharacterController::NetState at full precision
// and its [Predicted] script bytes, format 3), each later tick is simulated again with the command
// the server ran for its owner, the way the owner's prediction resimulates (NetPredictor: the
// command, the predicted scripts' fixed step, then PhysicsCore::StepCharacter), and the result is
// compared with that tick's recorded mover: position and velocity within the predictor's
// tolerances, grounded exactly, the [Predicted] bytes exactly.
//
// A divergent tick is one the owner's prediction would have been corrected at: the server did
// something the commands don't explain (damage, a knockback, a respawn), or the simulation isn't
// deterministic. After one, the check carries on from that tick's recorded mover, so every
// divergence in the range is found, not only the first.
//
// The engine side is the caller's (Hooks): NetCore::ResimulateReplay runs the world's own physics
// and scripts; tests drive a PhysicsCore of their own.
namespace NetReplayResim
{
    struct Settings
    {
        float PositionTolerance = 0.001f;   // m (NetPredictor's)
        float VelocityTolerance = 0.01f;    // m/s
        bool ComparePredicted = true;       // the [Predicted] bytes, when the recording has them
        size_t MaxDivergences = 64;         // kept (all are counted)
    };

    struct Hooks
    {
        // Put the character in the recorded state (its mover and [Predicted] bytes).
        std::function<void( const CharacterController::NetState&, const std::vector<uint8_t>& )> Restore;
        // One tick: InCommand is what the server ran for its owner, InPrevious the tick before's
        // (button edges).
        std::function<void( uint32_t InTick, const InputCommand& InCommand, const InputCommand& InPrevious )> Step;
        // The character's state after the step.
        std::function<void( CharacterController::NetState&, std::vector<uint8_t>& )> Save;
    };

    struct Divergence
    {
        uint32_t Tick = 0;
        std::string What;       // "position off by 0.512 m: resimulated (...), recorded (...)"
        CharacterController::NetState Recorded;
        CharacterController::NetState Resimulated;
    };

    struct Result
    {
        uint32_t NetId = 0;
        std::string Name;
        uint8_t Client = 0;         // its owner (the last one seen)
        uint32_t From = 0;          // the range asked for
        uint32_t To = 0;
        uint32_t FirstSeed = 0;     // the tick it started from (the first with a mover and an owner)
        uint32_t Ticks = 0;         // simulated again and compared
        uint32_t Seeds = 0;         // starts: the first, after each divergence, after a gap
        uint32_t Gaps = 0;          // ticks it couldn't simulate: no mover (not there), no owner or no command
        uint32_t DivergenceCount = 0;
        std::vector<Divergence> Divergences;
        bool ComparedPredicted = false;
        std::string Error;          // couldn't run (the replay won't read, no such character)
        bool Diverged() const { return DivergenceCount > 0; }
        bool Ran() const { return Error.empty() && Ticks > 0; }
    };

    // InNetId's ticks InFrom..InTo: seeded at InFrom (or the first tick after it with a mover and an
    // owner's command), each later tick simulated and compared.
    Result Run( NetReplayReader& InReader, uint32_t InNetId, uint32_t InFrom, uint32_t InTo, const Hooks& InHooks, const Settings& InSettings = Settings() );

    // The NetIds with a recorded mover and an owning client (a player's character) in InFrom..InTo:
    // the ones a resim check can run (a bot's commands aren't recorded). OutSpans (optional): each
    // one's first and last tick with a mover.
    std::vector<uint32_t> FindCharacters( NetReplayReader& InReader, uint32_t InFrom, uint32_t InTo, std::string* OutError = nullptr,
        std::map<uint32_t, std::pair<uint32_t, uint32_t>>* OutSpans = nullptr );

    // "<name>#<id> client N ticks a-b: ..." with the divergences, one line each.
    std::string Describe( const Result& InResult, size_t InMaxLines = 8 );
}
