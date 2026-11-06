#include "PCH.h"
#include "Net/NetReplayResim.h"
#include "optick.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace
{
    std::string Vec( const Vector3& InValue )
    {
        char text[96];
        std::snprintf( text, sizeof( text ), "(%.4f, %.4f, %.4f)", InValue.x, InValue.y, InValue.z );
        return text;
    }

    float Largest( const Vector3& InA, const Vector3& InB )
    {
        return std::max( { std::abs( InA.x - InB.x ), std::abs( InA.y - InB.y ), std::abs( InA.z - InB.z ) } );
    }

    // What differs, in the order a reader wants it: "" when nothing does.
    std::string Compare( const CharacterController::NetState& InResim, const std::vector<uint8_t>& InResimBytes, const NetReplayMover& InRecorded,
        const NetReplayResim::Settings& InSettings, bool InComparePredicted )
    {
        std::string what;
        auto add = [&what]( const std::string& InPart ) { what += ( what.empty() ? "" : "; " ) + InPart; };
        const float position = Largest( InResim.Position, InRecorded.State.Position );
        if( position > InSettings.PositionTolerance )
        {
            char text[64];
            std::snprintf( text, sizeof( text ), "position off by %.4f m: ", position );
            add( text + std::string( "resimulated " ) + Vec( InResim.Position ) + ", recorded " + Vec( InRecorded.State.Position ) );
        }
        const float velocity = Largest( InResim.Velocity, InRecorded.State.Velocity );
        if( velocity > InSettings.VelocityTolerance )
        {
            char text[64];
            std::snprintf( text, sizeof( text ), "velocity off by %.4f m/s: ", velocity );
            add( text + std::string( "resimulated " ) + Vec( InResim.Velocity ) + ", recorded " + Vec( InRecorded.State.Velocity ) );
        }
        if( InResim.Grounded != InRecorded.State.Grounded )
        {
            add( std::string( "grounded: resimulated " ) + ( InResim.Grounded ? "yes" : "no" ) + ", recorded " + ( InRecorded.State.Grounded ? "yes" : "no" ) );
        }
        if( InComparePredicted && InResimBytes != InRecorded.Predicted )
        {
            size_t first = 0;
            while( first < InResimBytes.size() && first < InRecorded.Predicted.size() && InResimBytes[first] == InRecorded.Predicted[first] )
            {
                ++first;
            }
            size_t last = std::max( InResimBytes.size(), InRecorded.Predicted.size() );
            while( last > first && last <= InResimBytes.size() && last <= InRecorded.Predicted.size() && InResimBytes[last - 1] == InRecorded.Predicted[last - 1] )
            {
                --last;
            }
            add( "[Predicted] bytes " + std::to_string( first ) + ".." + std::to_string( last ) + " differ (" + std::to_string( InResimBytes.size() ) + " resimulated, "
                + std::to_string( InRecorded.Predicted.size() ) + " recorded)" );
        }
        return what;
    }

    const NetReplayMover* FindMover( const NetReplayTick& InTick, uint32_t InNetId )
    {
        for( const NetReplayMover& mover : InTick.Movers )
        {
            if( mover.NetId == InNetId )
            {
                return &mover;
            }
        }
        return nullptr;
    }

    const NetReplayClient* FindClient( const NetReplayTick& InTick, uint8_t InClient )
    {
        for( const NetReplayClient& client : InTick.Clients )
        {
            if( client.Id == InClient )
            {
                return &client;
            }
        }
        return nullptr;
    }
}


NetReplayResim::Result NetReplayResim::Run( NetReplayReader& InReader, uint32_t InNetId, uint32_t InFrom, uint32_t InTo, const Hooks& InHooks, const Settings& InSettings )
{
    OPTICK_EVENT( "NetReplayResim::Run" );
    Result result;
    result.NetId = InNetId;
    result.From = InFrom;
    result.To = InTo;
    if( InReader.IsEmpty() || InFrom > InTo )
    {
        result.Error = "no ticks to check";
        return result;
    }
    if( !InHooks.Restore || !InHooks.Step || !InHooks.Save )
    {
        result.Error = "nothing to simulate with";
        return result;
    }
    std::map<uint32_t, NetReplayEntity> table;
    bool seeded = false;
    bool haveCommand = false;
    InputCommand previous;
    CharacterController::NetState state;
    std::vector<uint8_t> bytes;
    std::vector<NetReplayMover> lastMovers;     // the tick before's (PoseOthers)
    // Names come from the chunk's first tick on.
    const uint32_t start = InReader.GetKeyframeTick( std::max( InFrom, InReader.GetFirstTick() ) );
    const bool read = InReader.ForEach( std::min( start, InFrom ), InTo, [&]( const NetReplayTick& InTick ) {
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            table[entity.NetId] = entity;
        }
        std::vector<NetReplayMover> movers;
        if( InHooks.PoseOthers )
        {
            movers.swap( lastMovers );
            lastMovers = InTick.Movers;
        }
        if( InTick.Tick < InFrom )
        {
            return true;
        }
        auto named = table.find( InNetId );
        const uint8_t owner = named != table.end() ? named->second.Owner : 0;
        if( named != table.end() )
        {
            result.Name = named->second.Name;
            if( owner != 0 )
            {
                result.Client = owner;
            }
        }
        const NetReplayMover* mover = FindMover( InTick, InNetId );
        const NetReplayClient* client = owner != 0 ? FindClient( InTick, owner ) : nullptr;
        if( !mover || !client )
        {
            // Not there, or nobody's: the next tick that has both starts again.
            if( seeded )
            {
                ++result.Gaps;
            }
            seeded = false;
            haveCommand = client != nullptr;
            previous = client ? client->Command : InputCommand();
            return true;
        }
        if( !seeded )
        {
            InHooks.Restore( mover->State, mover->Predicted );
            if( result.Seeds == 0 )
            {
                result.FirstSeed = InTick.Tick;
            }
            ++result.Seeds;
            seeded = true;
            haveCommand = true;
            previous = client->Command;
            return true;
        }
        if( InHooks.PoseOthers )
        {
            InHooks.PoseOthers( movers );
        }
        InHooks.Step( InTick.Tick, client->Command, haveCommand ? previous : InputCommand() );
        state = CharacterController::NetState();
        bytes.clear();
        InHooks.Save( state, bytes );
        ++result.Ticks;
        const bool comparePredicted = InSettings.ComparePredicted && !mover->Predicted.empty();
        result.ComparedPredicted = result.ComparedPredicted || comparePredicted;
        const std::string what = Compare( state, bytes, *mover, InSettings, comparePredicted );
        if( !what.empty() )
        {
            ++result.DivergenceCount;
            if( result.Divergences.size() < InSettings.MaxDivergences )
            {
                result.Divergences.push_back( { InTick.Tick, what, mover->State, state } );
            }
            // Carry on from what the server had.
            InHooks.Restore( mover->State, mover->Predicted );
            ++result.Seeds;
        }
        previous = client->Command;
        haveCommand = true;
        return true;
    } );
    if( !read )
    {
        result.Error = InReader.GetError();
    }
    else if( result.Seeds == 0 )
    {
        result.Error = "no tick in " + std::to_string( InFrom ) + "-" + std::to_string( InTo ) + " has #" + std::to_string( InNetId ) + "'s mover and its owner's command";
    }
    return result;
}


std::vector<uint32_t> NetReplayResim::FindCharacters( NetReplayReader& InReader, uint32_t InFrom, uint32_t InTo, std::string* OutError,
    std::map<uint32_t, std::pair<uint32_t, uint32_t>>* OutSpans )
{
    std::map<uint32_t, NetReplayEntity> table;
    std::set<uint32_t> found;
    if( InReader.IsEmpty() )
    {
        return {};
    }
    const uint32_t start = InReader.GetKeyframeTick( std::max( InFrom, InReader.GetFirstTick() ) );
    const bool read = InReader.ForEach( std::min( start, InFrom ), InTo, [&]( const NetReplayTick& InTick ) {
        for( const NetReplayEntity& entity : InTick.Entities )
        {
            table[entity.NetId] = entity;
        }
        if( InTick.Tick < InFrom )
        {
            return true;
        }
        for( const NetReplayMover& mover : InTick.Movers )
        {
            auto named = table.find( mover.NetId );
            if( named != table.end() && named->second.Owner != 0 )
            {
                found.insert( mover.NetId );
            }
            if( OutSpans )
            {
                auto [span, added] = OutSpans->try_emplace( mover.NetId, InTick.Tick, InTick.Tick );
                span->second.second = InTick.Tick;
            }
        }
        return true;
    } );
    if( !read && OutError )
    {
        *OutError = InReader.GetError();
    }
    return std::vector<uint32_t>( found.begin(), found.end() );
}


std::string NetReplayResim::Describe( const Result& InResult, size_t InMaxLines )
{
    std::string text = ( InResult.Name.empty() ? std::string() : InResult.Name ) + "#" + std::to_string( InResult.NetId ) + " client " + std::to_string( InResult.Client )
        + " ticks " + std::to_string( InResult.From ) + "-" + std::to_string( InResult.To ) + ": ";
    if( !InResult.Error.empty() )
    {
        return text + "can't check: " + InResult.Error;
    }
    if( InResult.Ticks == 0 )
    {
        // Seeded, then nothing after it in the range: no verdict.
        return text + "nothing to compare: only tick " + std::to_string( InResult.FirstSeed ) + " (the seed) has its mover and its owner's command in the range";
    }
    text += std::to_string( InResult.Ticks ) + " resimulated from tick " + std::to_string( InResult.FirstSeed ) + ( InResult.ComparedPredicted ? " (mover and [Predicted] state)" : " (mover)" );
    if( InResult.Gaps > 0 )
    {
        text += ", " + std::to_string( InResult.Gaps ) + " gap(s)";
    }
    if( !InResult.Diverged() )
    {
        return text + ", every one matches the recording";
    }
    text += ", " + std::to_string( InResult.DivergenceCount ) + " diverge; the first at tick " + std::to_string( InResult.Divergences.front().Tick );
    for( size_t i = 0; i < InResult.Divergences.size() && i < InMaxLines; ++i )
    {
        text += "\n  tick " + std::to_string( InResult.Divergences[i].Tick ) + ": " + InResult.Divergences[i].What;
    }
    if( InResult.DivergenceCount > InMaxLines )
    {
        text += "\n  ... " + std::to_string( InResult.DivergenceCount - std::min<size_t>( InMaxLines, InResult.Divergences.size() ) ) + " more";
    }
    return text;
}
