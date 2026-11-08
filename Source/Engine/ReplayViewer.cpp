#include "PCH.h"
#include "Engine/ReplayViewer.h"
#include "Net/NetDebugDraw.h"
#include "CLog.h"
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/Model.h"
#include "Components/Graphics/TextRenderer.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Networking/SingletonKillcam.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Transform.h"
#include "Core/CommandLine.h"
#include "Cores/NetCore.h"
#include "Engine/HeatmapOverlay.h"
#include "Engine/World.h"
#include "Jobs/BackgroundLane.h"
#include "Net/NetLagCompensation.h"
#include "Net/NetReplayPlayback.h"
#include "Net/NetReplication.h"
#include "Net/NetSnapshot.h"
#include "optick.h"
#if !USING( ME_HEADLESS )
#include "Debug/DebugDraw.h"
#endif
#if USING( ME_SCRIPTING )
#include "Components/Scripting/ScriptComponent.h"
#include "Scripting/ScriptEngine.h"
#endif
#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
#include <imgui.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
    // Trails: the characters' last 2 s.
    constexpr uint32_t kTrailTicks = 120;
    // Frames a marker jump waits on the tick before and on the tick (spawns settle, scripts start).
    constexpr int kSettleFrames = 3;
    // Frames the resim check waits after seeking to a character (its entity spawns, its script starts).
    constexpr int kResimSettleFrames = 4;
    // World-side values are read again for this many frames after the tick changes.
    constexpr int kRefreshFrames = 5;

    void Say( CLog::LogType InType, const std::string& InMessage )
    {
        CLog::Log( InType, InMessage );
        std::cout << InMessage << std::endl;
    }

    std::string Format( const char* InFormat, ... )
    {
        char buffer[512];
        va_list args;
        va_start( args, InFormat );
        std::vsnprintf( buffer, sizeof( buffer ), InFormat, args );
        va_end( args );
        return buffer;
    }

    std::string Vec( const Vector3& InValue )
    {
        return Format( "(%.3f, %.3f, %.3f)", InValue.x, InValue.y, InValue.z );
    }

    bool Same( const Vector3& InA, const Vector3& InB )
    {
        return InA.x == InB.x && InA.y == InB.y && InA.z == InB.z;
    }

    json VecJson( const Vector3& InValue )
    {
        return json::array( { InValue.x, InValue.y, InValue.z } );
    }

    std::string Hex( const std::vector<uint8_t>& InBytes )
    {
        std::string text;
        for( const uint8_t byte : InBytes )
        {
            text += Format( "%02x", byte );
        }
        return text;
    }

    // The byte indices of InNow that differ from InBefore (or are past its end).
    std::vector<size_t> ChangedBytes( const std::vector<uint8_t>& InNow, const std::vector<uint8_t>& InBefore )
    {
        std::vector<size_t> changed;
        for( size_t i = 0; i < InNow.size(); ++i )
        {
            if( i >= InBefore.size() || InNow[i] != InBefore[i] )
            {
                changed.push_back( i );
            }
        }
        return changed;
    }

    bool ParseUInt( const std::string& InText, uint32_t& OutValue )
    {
        if( InText.empty() || InText.size() > 10 || InText.find_first_not_of( "0123456789" ) != std::string::npos )
        {
            return false;
        }
        const unsigned long long value = std::stoull( InText );
        if( value > 0xFFFFFFFFull )
        {
            return false;
        }
        OutValue = static_cast<uint32_t>( value );
        return true;
    }

    bool ParseInt( const std::string& InText, int& OutValue )
    {
        const bool negative = !InText.empty() && InText[0] == '-';
        uint32_t magnitude = 0;
        if( !ParseUInt( negative ? InText.substr( 1 ) : InText, magnitude ) || magnitude > 1000000u )
        {
            return false;
        }
        OutValue = negative ? -static_cast<int>( magnitude ) : static_cast<int>( magnitude );
        return true;
    }

    // "a:b", "a:", ":b" -> the range (unbounded ends stay as given).
    bool ParseSpan( const std::string& InText, uint32_t& OutFrom, uint32_t& OutTo )
    {
        const size_t colon = InText.find( ':' );
        if( colon == std::string::npos )
        {
            return false;
        }
        const std::string from = InText.substr( 0, colon );
        const std::string to = InText.substr( colon + 1 );
        if( ( from.empty() && to.empty() ) || ( !from.empty() && !ParseUInt( from, OutFrom ) ) || ( !to.empty() && !ParseUInt( to, OutTo ) ) )
        {
            return false;
        }
        return OutFrom <= OutTo;
    }

    Vector3 AimDirection( float InYawDegrees, float InPitchDegrees )
    {
        const float yaw = InYawDegrees * 3.14159265f / 180.f;
        const float pitch = InPitchDegrees * 3.14159265f / 180.f;
        return Vector3( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
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

    // The C# script's public fields as the world has them: name -> the value's JSON.
    std::map<std::string, std::string> ScriptFields( const EntityHandle& InEntity )
    {
        std::map<std::string, std::string> fields;
#if USING( ME_SCRIPTING )
        const ScriptComponent* script = InEntity ? InEntity->TryGetComponent<ScriptComponent>() : nullptr;
        const int handle = script ? script->GetHandle() : -1;
        if( handle >= 0 )
        {
            const json parsed = json::parse( ScriptEngine::GetFieldsJson( handle ), nullptr, false );
            if( !parsed.is_discarded() && parsed.is_object() )
            {
                for( auto it = parsed.begin(); it != parsed.end(); ++it )
                {
                    fields[it.key()] = it.value().dump();
                }
            }
        }
#else
        (void)InEntity;
#endif
        return fields;
    }

#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
    ImU32 KindColor( NetReplayMarker::Kind InKind )
    {
        switch( InKind )
        {
        case NetReplayMarker::Kind::Kill: return IM_COL32( 255, 70, 70, 255 );
        case NetReplayMarker::Kind::Join: return IM_COL32( 90, 230, 90, 255 );
        case NetReplayMarker::Kind::Leave: return IM_COL32( 170, 170, 170, 255 );
        case NetReplayMarker::Kind::Event: return IM_COL32( 90, 140, 255, 160 );
        case NetReplayMarker::Kind::Hit: return IM_COL32( 255, 160, 40, 170 );
        case NetReplayMarker::Kind::Miss: return IM_COL32( 255, 225, 40, 255 );
        case NetReplayMarker::Kind::Resync: return IM_COL32( 230, 80, 230, 255 );
        case NetReplayMarker::Kind::Hitch: return IM_COL32( 160, 100, 255, 255 );
        default: return IM_COL32_WHITE;
        }
    }

    // How tall a kind's mark is on the scrubber (0..1): the moments people look for stand out.
    float KindHeight( NetReplayMarker::Kind InKind )
    {
        switch( InKind )
        {
        case NetReplayMarker::Kind::Kill:
        case NetReplayMarker::Kind::Join:
        case NetReplayMarker::Kind::Leave: return 1.f;
        case NetReplayMarker::Kind::Miss:
        case NetReplayMarker::Kind::Resync:
        case NetReplayMarker::Kind::Hitch: return 0.7f;
        default: return 0.4f;
        }
    }

    const ImVec4 kChanged( 1.f, 0.85f, 0.25f, 1.f );
#endif
}


// The tick shown (or the one before): its record, names as of it, and the world's view of it.
struct ReplayViewer::Shown
{
    uint32_t Tick = 0;
    bool Valid = false;
    NetReplayTick Record;
    std::map<uint32_t, NetReplayEntity> Table;
    // Per NetId: the recorded C++ fields by name (NetSchema::Describe of the state's NetSchema part),
    // where its script's bytes start in the state's Fields (after the u16 size and the NetSchema
    // bytes: NetReplicationState::Unpack), and the script's public fields as the world had them while
    // this tick was shown (none: never read).
    std::map<uint32_t, std::vector<NetSchema::FieldValue>> Fields;
    std::map<uint32_t, size_t> ScriptStart;
    std::map<uint32_t, std::map<std::string, std::string>> Script;
    int Refreshes = 0;
};


ReplayViewer::ReplayViewer( std::string InPath, std::vector<Action> InActions )
    : m_path( std::move( InPath ) )
    , m_actions( std::move( InActions ) )
    , m_timeline( std::make_unique<NetReplayTimeline>() )
{
}


ReplayViewer::~ReplayViewer()
{
    m_cancel = true;
    if( m_timelineBuild.valid() )
    {
        m_timelineBuild.wait();
    }
}


bool ReplayViewer::Configure( NetCore& InNet, std::string& OutError )
{
    m_source = std::make_unique<NetReplayFileSource>();
    if( !m_source->Open( m_path ) )
    {
        OutError = m_source->GetError();
        return false;
    }
    m_reader = std::make_unique<NetReplayReader>( *m_source );

    if( CommandLine::Has( "--replay-marker" ) )
    {
        m_markerRequest = CommandLine::GetString( "--replay-marker" );
        const std::string kind = m_markerRequest.substr( 0, m_markerRequest.find( ':' ) );
        NetReplayMarker::Kind parsed;
        int nth = 0;
        const size_t colon = m_markerRequest.find( ':' );
        if( !NetReplayTimeline::ParseKind( kind, parsed ) || ( colon != std::string::npos && !ParseInt( m_markerRequest.substr( colon + 1 ), nth ) ) )
        {
            OutError = "--replay-marker " + m_markerRequest + ": expected KIND[:N] (kill, join, leave, event, hit, miss, resync, hitch; N from 0, negative from the end)";
            return false;
        }
        InNet.GetPlayback()->Pause();
    }
    m_reportPath = CommandLine::GetString( "--replay-report" );
    if( CommandLine::Has( "--replay-report" ) && m_reportPath.empty() )
    {
        OutError = "--replay-report: which file?";
        return false;
    }
    m_checkPending = CommandLine::Has( "--replay-check" );
    if( CommandLine::Has( "--resim-check" ) )
    {
        m_resimCheck = true;
        const std::string span = CommandLine::GetString( "--resim-check" );
        if( !span.empty() && span.rfind( "--", 0 ) != 0 && !ParseSpan( span, m_resimFrom, m_resimTo ) )
        {
            OutError = "--resim-check " + span + ": expected A:B (ticks; A: or :B work too)";
            return false;
        }
        InNet.GetPlayback()->Pause();
    }
    if( CommandLine::Has( "--replay-camera" ) )
    {
        const std::string camera = CommandLine::GetString( "--replay-camera" );
        const size_t colon = camera.find( ':' );
        const std::string mode = camera.substr( 0, colon );
        uint32_t id = 0;
        if( mode == "game" || mode == "free" )
        {
            SetCamera( mode == "game" ? CameraMode::Game : CameraMode::Free );
        }
        else if( ( mode == "follow" || mode == "pov" ) && colon != std::string::npos && ParseUInt( camera.substr( colon + 1 ), id ) && id != 0 )
        {
            SetCamera( mode == "follow" ? CameraMode::Follow : CameraMode::Pov, id );
        }
        else
        {
            OutError = "--replay-camera " + camera + ": expected game, free, follow:NETID or pov:NETID";
            return false;
        }
    }
    if( CommandLine::Has( "--replay-overlays" ) )
    {
        const std::string list = "," + CommandLine::GetString( "--replay-overlays" ) + ",";
        m_drawShots = list.find( ",shots," ) != std::string::npos;
        m_drawHitboxes = list.find( ",hitboxes," ) != std::string::npos;
        m_drawTrails = list.find( ",trails," ) != std::string::npos;
        if( list.find( ",heatmap," ) != std::string::npos && !CommandLine::Has( "--replay-heatmap" ) )
        {
            HeatmapOverlay::Get().Build( { m_path } );
            m_heatmapPending = true;
        }
    }
    // ME-215: a heatmap over the ground, from a file (ReplayDump --heatmap's, many replays) or built
    // from this replay on the background lane; --replay-heatmap-layer LAYER[:GROUP] picks what it shows.
    if( CommandLine::Has( "--replay-heatmap" ) )
    {
        HeatmapOverlay& overlay = HeatmapOverlay::Get();
        const std::string file = CommandLine::GetString( "--replay-heatmap" );
        if( !file.empty() && file.rfind( "--", 0 ) != 0 )
        {
            std::string error;
            if( !overlay.Load( file, error ) )
            {
                OutError = "--replay-heatmap " + file + ": " + error;
                return false;
            }
            Say( CLog::LogType::Info, "Replay: heatmap from " + file );
        }
        else
        {
            overlay.Build( { m_path } );
        }
        m_heatmapPending = true;
    }
    if( CommandLine::Has( "--replay-heatmap-layer" ) )
    {
        const std::string pick = CommandLine::GetString( "--replay-heatmap-layer" );
        const size_t colon = pick.find( ':' );
        NetReplayHeatmap::Layer layer;
        if( !NetReplayHeatmap::ParseLayer( pick.substr( 0, colon ), layer ) )
        {
            OutError = "--replay-heatmap-layer " + pick + ": expected LAYER[:GROUP] (positions, deaths or kills; all, hero:NAME, team:NAME)";
            return false;
        }
        HeatmapOverlay::Get().SetLayer( layer );
        m_heatmapGroup = colon == std::string::npos ? std::string() : pick.substr( colon + 1 );
    }

    // The timeline, on the background lane from a source of its own (the file is read again: the
    // main thread never waits on it). Low: a decode of every chunk is long CPU work, and scene reads
    // and replay writes mustn't queue behind it.
    std::string path = m_path;
    NetReplayTimeline* timeline = m_timeline.get();
    std::atomic<bool>* cancel = &m_cancel;
    m_timelineBuild = Jobs::BackgroundLane::Get().Submit( "Replay Timeline", [path, timeline, cancel]() -> std::string {
        NetReplayFileSource source;
        if( !source.Open( path ) )
        {
            return source.GetError();
        }
        NetReplayReader reader( source );
        std::string error;
        if( !timeline->Build( reader, &error, cancel ) && error.empty() )
        {
            error = "cancelled";
        }
        return error;
    }, Jobs::BackgroundPriority::Low );
    return true;
}


void ReplayViewer::PollTimeline()
{
    if( m_timelineReady || !m_timelineBuild.valid() || m_timelineBuild.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
    {
        return;
    }
    m_timelineError = m_timelineBuild.get();
    m_timelineReady = m_timelineError.empty();
    if( m_timelineReady )
    {
        std::string counts;
        for( uint8_t i = 0; i < static_cast<uint8_t>( NetReplayMarker::Kind::Count ); ++i )
        {
            const auto kind = static_cast<NetReplayMarker::Kind>( i );
            counts += Format( "%s%s %zu", counts.empty() ? "" : ", ", NetReplayTimeline::KindName( kind ), m_timeline->Count( kind ) );
        }
        Say( CLog::LogType::Info, "Replay: timeline: " + counts );
    }
    else
    {
        Say( CLog::LogType::Error, "Replay: the timeline couldn't be built: " + m_timelineError );
    }
}


bool ReplayViewer::IsSettling() const
{
    return !m_markerRequest.empty() || m_jumpStage != 0 || m_heatmapPending;
}


void ReplayViewer::PollHeatmap()
{
    HeatmapOverlay& overlay = HeatmapOverlay::Get();
    if( !m_heatmapPending || overlay.IsBuilding() )
    {
        return;
    }
    m_heatmapPending = false;
    if( !overlay.HasHeatmap() )
    {
        return;     // the build failed: the overlay logged why
    }
    if( !m_heatmapGroup.empty() && !overlay.SetGroup( m_heatmapGroup ) )
    {
        Say( CLog::LogType::Warning, "Replay: the heatmap has no group " + m_heatmapGroup + "; showing all" );
    }
    m_heatmapGroup.clear();
    overlay.SetShown( true );
    Say( CLog::LogType::Info, "Replay: " + overlay.Describe().substr( 0, overlay.Describe().find( '\n' ) ) );
}


void ReplayViewer::JumpTo( NetCore& InNet, uint32_t InTick )
{
    NetReplayPlayback* playback = InNet.GetPlayback();
    if( !playback )
    {
        return;
    }
    playback->Pause();
    playback->SetPauseAt( -1 );
    InTick = std::clamp( InTick, playback->GetFirstTick(), playback->GetLastTick() );
    m_jumpTick = InTick;
    m_jumpFrames = 0;
    if( InTick > playback->GetFirstTick() && playback->GetTick() != InTick - 1 && playback->GetTick() != InTick )
    {
        // The tick before first: the inspector's diffs need the world as it was then.
        playback->Seek( InTick - 1 );
        m_jumpStage = 1;
    }
    else if( playback->GetTick() == InTick - 1 && InTick > playback->GetFirstTick() )
    {
        m_jumpStage = 1;
    }
    else
    {
        playback->Seek( InTick );
        m_jumpStage = 3;
    }
}


bool ReplayViewer::JumpToMarker( NetCore& InNet, NetReplayMarker::Kind InKind, int InNth, std::string& OutError )
{
    const NetReplayTimeline* timeline = GetTimeline();
    if( !timeline )
    {
        OutError = m_timelineError.empty() ? "the timeline is still being built" : "no timeline: " + m_timelineError;
        return false;
    }
    const int index = timeline->Find( InKind, InNth );
    if( index < 0 )
    {
        OutError = Format( "there's no %s marker %d (%zu in the replay)", NetReplayTimeline::KindName( InKind ), InNth, timeline->Count( InKind ) );
        return false;
    }
    m_jumpMarker = index;
    if( timeline->GetMarkers()[index].Entity != 0 )
    {
        m_selected = timeline->GetMarkers()[index].Entity;     // the inspector shows who it's about
    }
    JumpTo( InNet, timeline->GetMarkers()[index].Tick );
    return true;
}


void ReplayViewer::Refresh( NetCore& InNet )
{
    OPTICK_EVENT( "ReplayViewer::Refresh" );
    NetReplayPlayback* playback = InNet.GetPlayback();
    const NetReplayTick* record = playback ? playback->GetRecord() : nullptr;
    if( !record )
    {
        return;
    }
    const uint32_t tick = record->Tick;
    // The recorded C++ fields by name, against the world's entity (its components' layout).
    auto describe = [&InNet]( Shown& InShown ) {
        if( !InShown.Valid || !InShown.Fields.empty() )
        {
            return;
        }
        for( const auto& [id, state] : InShown.Record.State.Entities )
        {
            EntityHandle entity = InNet.FindEntity( id );
            if( entity )
            {
                std::vector<uint8_t> reflected;
                std::vector<uint8_t> script;
                InShown.ScriptStart[id] = NetReplicationState::Unpack( state.Fields, reflected, script );
                InShown.Fields[id] = NetSchema::Describe( *entity.Get(), reflected );
            }
        }
    };
    if( !m_shown || m_shown->Tick != tick || !m_shown->Valid )
    {
        std::unique_ptr<Shown> next = std::make_unique<Shown>();
        next->Tick = tick;
        next->Valid = true;
        next->Record = *record;
        next->Table = playback->GetEntityTable();
        // Playing at 1x the tick before is the one shown last; else it's read once paused (below).
        if( m_shown && m_shown->Valid && m_shown->Tick + 1 == tick )
        {
            m_before = std::move( m_shown );
        }
        else
        {
            m_before.reset();
        }
        describe( *next );
        m_shown = std::move( next );
        if( m_selected == 0 && !m_shown->Record.Movers.empty() )
        {
            m_selected = m_shown->Record.Movers.front().NetId;
        }

        // Trails: on from the last tick (the ticks since it, from the reader's cached chunk when
        // playing faster than a tick a frame), or read again after a jump or a step back.
        if( m_drawTrails )
        {
            if( m_hasTrails && tick == m_trailTick + 1 )
            {
                for( const NetReplayMover& mover : record->Movers )
                {
                    m_trails[mover.NetId].emplace_back( tick, mover.State.Position );
                }
            }
            else
            {
                const bool onward = m_hasTrails && tick > m_trailTick && tick - m_trailTick <= kTrailTicks;
                if( !onward )
                {
                    m_trails.clear();
                }
                const uint32_t first = tick > playback->GetFirstTick() + kTrailTicks ? tick - kTrailTicks : playback->GetFirstTick();
                m_reader->ForEach( onward ? m_trailTick + 1 : first, tick, [this]( const NetReplayTick& InTick ) {
                    for( const NetReplayMover& mover : InTick.Movers )
                    {
                        m_trails[mover.NetId].emplace_back( InTick.Tick, mover.State.Position );
                    }
                    return true;
                } );
            }
            for( auto it = m_trails.begin(); it != m_trails.end(); )
            {
                while( !it->second.empty() && it->second.front().first + kTrailTicks < tick )
                {
                    it->second.pop_front();
                }
                it = it->second.empty() || it->second.back().first + 1 < tick ? m_trails.erase( it ) : std::next( it );
            }
            m_hasTrails = true;
            m_trailTick = tick;
        }
    }
    // The tick before, for the inspector's changes, from the viewer's own reader (the world never
    // showed it: no script fields): only paused (a jump, a step, a check), once per tick, so playing
    // fast never reads and decodes a tick a frame on the main thread.
    if( playback->IsPaused() && tick > playback->GetFirstTick() && ( !m_before || m_before->Tick + 1 != tick ) )
    {
        m_before = std::make_unique<Shown>();
        m_before->Tick = tick - 1;      // tried, even if it doesn't read
        if( m_reader->ReadTick( tick - 1, m_before->Record ) )
        {
            m_before->Valid = true;
            m_before->Table = m_shown->Table;
            describe( *m_before );
        }
    }
    // The world's script fields, read again for a few frames (a script spawned by a seek starts a frame
    // later); only while paused (stepping, a jump, a check): playing, the tick changes every frame.
    if( m_shown->Refreshes < kRefreshFrames && playback->IsPaused() )
    {
        ++m_shown->Refreshes;
        for( const auto& [id, state] : m_shown->Record.State.Entities )
        {
            m_shown->Script[id] = ScriptFields( InNet.FindEntity( id ) );
        }
    }
}


bool ReplayViewer::Update( NetCore& InNet, std::string& OutError )
{
    OPTICK_EVENT( "ReplayViewer::Update" );
    NetReplayPlayback* playback = InNet.GetPlayback();
    if( !playback )
    {
        return false;
    }
    PollTimeline();
    PollHeatmap();

    // --replay-marker, once the timeline is in.
    if( !m_markerRequest.empty() )
    {
        if( !m_timelineError.empty() )
        {
            OutError = "--replay-marker " + m_markerRequest + ": no timeline (" + m_timelineError + ")";
            m_markerRequest.clear();
            return true;
        }
        if( m_timelineReady )
        {
            NetReplayMarker::Kind kind = NetReplayMarker::Kind::Kill;
            int nth = 0;
            const size_t colon = m_markerRequest.find( ':' );
            NetReplayTimeline::ParseKind( m_markerRequest.substr( 0, colon ), kind );
            if( colon != std::string::npos )
            {
                ParseInt( m_markerRequest.substr( colon + 1 ), nth );
            }
            std::string error;
            if( !JumpToMarker( InNet, kind, nth, error ) )
            {
                OutError = "--replay-marker " + m_markerRequest + ": " + error;
                m_markerRequest.clear();
                return true;
            }
            const NetReplayMarker& marker = m_timeline->GetMarkers()[m_jumpMarker];
            Say( CLog::LogType::Info, Format( "Replay: marker %s %d at tick %u: %s", NetReplayTimeline::KindName( kind ), nth, marker.Tick, marker.Label.c_str() ) );
            m_markerRequest.clear();
        }
    }
    // A jump: the tick before, then a step onto the tick, then settle.
    if( m_jumpStage != 0 && ++m_jumpFrames >= kSettleFrames )
    {
        m_jumpFrames = 0;
        if( m_jumpStage == 1 )
        {
            playback->Step( 1 );
            m_jumpStage = 2;
        }
        else
        {
            m_jumpStage = m_jumpStage == 2 ? 3 : 0;
        }
    }

    Refresh( InNet );

    bool done = false;
    if( m_resimCheck )
    {
        StepResimCheck( InNet, OutError, done );
    }
    // --replay-report without --replay-check: once it rests on its tick.
    if( !m_reportPath.empty() && !m_reportWritten && !m_checkPending && !m_resimCheck && !IsSettling() && playback->IsPaused() && playback->GetPauseAt() < 0
        && m_shown && m_shown->Refreshes >= kRefreshFrames )
    {
        std::string error;
        if( !WriteReport( InNet, m_reportPath, error ) )
        {
            OutError = error;
            return true;
        }
    }
    DrawOverlays( InNet );
    return done;
}


bool ReplayViewer::OnChecked( NetCore& InNet, std::string& OutError )
{
    m_checkPending = false;
    if( m_reportPath.empty() || m_reportWritten )
    {
        return true;
    }
    return WriteReport( InNet, m_reportPath, OutError );
}


void ReplayViewer::StepResimCheck( NetCore& InNet, std::string& OutError, bool& OutDone )
{
    NetReplayPlayback& playback = *InNet.GetPlayback();
    if( !m_resimQueued )
    {
        m_resimQueued = true;
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> spans;
        std::string error;
        const uint32_t to = std::min( m_resimTo, playback.GetLastTick() );
        const uint32_t from = std::max( m_resimFrom, playback.GetFirstTick() );
        for( const uint32_t id : NetReplayResim::FindCharacters( *m_reader, from, to, &error, &spans ) )
        {
            const auto span = spans[id];
            m_resimQueue.push_back( { id, { std::max( from, span.first ), std::min( to, span.second ) } } );
        }
        if( !error.empty() || m_resimQueue.empty() )
        {
            OutError = "--resim-check: " + ( error.empty() ? Format( "no player's character has a recorded mover in ticks %u-%u (format 3 records them)", from, to ) : error );
            m_exitCode = 1;
            OutDone = true;
            return;
        }
        Say( CLog::LogType::Info, Format( "Resim: checking %zu character(s) over ticks %u-%u", m_resimQueue.size(), from, to ) );
        m_resimFrames = 0;
    }
    if( !m_resimQueue.empty() )
    {
        const auto& [id, span] = m_resimQueue.front();
        // Show its first tick, so its entity is in the world with its script started.
        if( playback.GetTick() != span.first || !playback.IsPaused() )
        {
            playback.Pause();
            playback.Seek( span.first );
            m_resimFrames = 0;
            return;
        }
        if( ++m_resimFrames < kResimSettleFrames )
        {
            return;
        }
        const NetReplayResim::Result result = InNet.ResimulateReplay( id, span.first, span.second );
        std::istringstream lines( NetReplayResim::Describe( result, 16 ) );
        for( std::string line; std::getline( lines, line ); )
        {
            Say( result.Error.empty() ? CLog::LogType::Info : CLog::LogType::Error, "Resim: " + line );
        }
        m_resimResults.push_back( result );
        m_resimQueue.erase( m_resimQueue.begin() );
        m_resimFrames = 0;
        return;
    }
    // Done: the exit code says how it went.
    size_t ran = 0;
    size_t diverged = 0;
    uint64_t ticks = 0;
    for( const NetReplayResim::Result& result : m_resimResults )
    {
        ran += result.Ran() ? 1 : 0;
        diverged += result.Ran() && result.Diverged() ? 1 : 0;
        ticks += result.Ticks;
    }
    const size_t unchecked = m_resimResults.size() - ran;
    if( ran == 0 )
    {
        OutError = "--resim-check: no character could be resimulated";
        m_exitCode = 1;
    }
    else
    {
        // A character that couldn't be checked fails the check (1): 0 promises every one matched.
        m_exitCode = unchecked ? 1 : diverged ? 3 : 0;
        std::string outcome = diverged ? Format( "%zu diverge", diverged ) : std::string( "every tick matches the recording" );
        if( unchecked )
        {
            outcome += Format( "; %zu couldn't be checked", unchecked );
            OutError = Format( "--resim-check: %zu of %zu character(s) couldn't be checked", unchecked, m_resimResults.size() );
        }
        Say( diverged || unchecked ? CLog::LogType::Warning : CLog::LogType::Info,
            Format( "Resim: %zu of %zu character(s) resimulated (%llu ticks): %s", ran, m_resimResults.size(), static_cast<unsigned long long>( ticks ), outcome.c_str() ) );
    }
    if( !m_reportPath.empty() )
    {
        std::string error;
        if( !WriteReport( InNet, m_reportPath, error ) )
        {
            OutError = error;
            m_exitCode = 1;
        }
    }
    m_resimCheck = false;
    OutDone = true;
}


std::string ReplayViewer::Label( uint32_t InNetId ) const
{
    if( InNetId == 0 )
    {
        return "-";
    }
    if( m_shown )
    {
        auto it = m_shown->Table.find( InNetId );
        if( it != m_shown->Table.end() && !it->second.Name.empty() )
        {
            return it->second.Name + "#" + std::to_string( InNetId );
        }
    }
    return "#" + std::to_string( InNetId );
}


std::string ReplayViewer::CommandText( const InputCommand& InCommand ) const
{
    std::string held;
    std::string axes;
    for( const Action& action : m_actions )
    {
        if( action.IsButton )
        {
            if( action.Index >= 0 && action.Index < 32 && ( InCommand.Buttons >> action.Index ) & 1u )
            {
                held += ( held.empty() ? "" : " " ) + action.Name;
            }
        }
        else
        {
            axes += ( axes.empty() ? "" : " " ) + action.Name + "(";
            for( int i = 0; i < action.AxisCount && action.Index + i < static_cast<int>( InCommand.Axes.size() ); ++i )
            {
                axes += Format( i ? ", %.2f" : "%.2f", InCommand.Axes[action.Index + i] );
            }
            axes += ")";
        }
    }
    if( m_actions.empty() )
    {
        held = Format( "buttons %08x", InCommand.Buttons );
        axes = Format( "axes %.2f %.2f %.2f %.2f", InCommand.Axes[0], InCommand.Axes[1], InCommand.Axes[2], InCommand.Axes[3] );
    }
    std::string text = ( held.empty() ? std::string( "-" ) : held ) + " | " + axes;
    if( InCommand.HasAim )
    {
        text += Format( " | aim %.2f %.2f", InCommand.Yaw, InCommand.Pitch );
    }
    if( InCommand.ViewTick >= 0.0 )
    {
        text += Format( " | view %.3f", InCommand.ViewTick );
    }
    return text;
}


std::vector<ReplayViewer::HitboxView> ReplayViewer::LiveHitboxes( NetCore& InNet ) const
{
    std::vector<HitboxView> views;
    if( !m_shown )
    {
        return views;
    }
    // The lag compensation's own capture of the world at the tick (NetLagCompensation::Record).
    NetLagCompensation scratch;
    scratch.Record( InNet.GetWorld(), m_shown->Tick );
    for( const NetLagCompensation::HitboxPose& pose : scratch.GetPosesAt( m_shown->Tick ) )
    {
        HitboxView view;
        view.NetId = NetCore::GetNetIdOrAncestor( pose.Entity );
        view.Name = pose.Entity ? pose.Entity->GetName() : std::string();
        view.Shape = pose.Shape;
        view.Position = pose.Position;
        view.Radius = pose.Radius;
        view.HalfHeight = pose.HalfHeight;
        view.HalfExtents = pose.HalfExtents;
        view.Up = pose.Rotation * Vector3( 0.f, 1.f, 0.f );
        view.Rotation = pose.Rotation;
        view.Occluder = pose.Occluder;
        views.push_back( view );
    }
    return views;
}


void ReplayViewer::DrawOverlays( NetCore& InNet )
{
#if !USING( ME_HEADLESS )
    if( !m_shown || !m_shown->Valid )
    {
        return;
    }
    const std::vector<HitboxView> live = ( m_drawHitboxes || m_drawShots ) ? LiveHitboxes( InNet ) : std::vector<HitboxView>();
    // The same shapes the client's Net Debugger draws (NetDebugDraw, NET-7).
    auto drawView = []( const HitboxView& InView, const Vector4& InColor ) {
        NetDebugDraw::Hitbox( InView.Shape, InView.Position, InView.Rotation, InView.Radius, InView.HalfHeight, InView.HalfExtents, InColor );
    };
    if( m_drawHitboxes )
    {
        for( const HitboxView& view : live )
        {
            drawView( view, Vector4( 0.85f, 0.85f, 0.85f, 1.f ) );
        }
    }
    if( m_drawShots )
    {
        for( size_t i = 0; i < m_shown->Record.Shots.size(); ++i )
        {
            if( m_selectedShot >= 0 && static_cast<size_t>( m_selectedShot ) != i )
            {
                continue;
            }
            const NetReplayShot& shot = m_shown->Record.Shots[i];
            const Vector3 end = shot.Hit || shot.Occluded ? shot.Point : shot.Origin + shot.Direction * shot.Range;
            const Vector4 rayColor = shot.Hit ? DebugDraw::Green : shot.Occluded ? DebugDraw::Orange : DebugDraw::Red;
            DebugDraw::Line( shot.Origin, end, rayColor, 0.f, DebugDraw::NoDepthTest );
            if( shot.Hit || shot.Occluded )
            {
                DebugDraw::Sphere( shot.Point, 0.08f, rayColor, 0.f, DebugDraw::NoDepthTest, 12 );
            }
            for( const NetReplayHitbox& hitbox : shot.Hitboxes )
            {
                const bool target = shot.Hit && hitbox.NetId == shot.Target && hitbox.Name == shot.TargetPart;
                NetDebugDraw::Hitbox( hitbox, target ? DebugDraw::Red : NetDebugDraw::kRewound );
                // Where it is live: the rewind's offset.
                for( const HitboxView& view : live )
                {
                    if( view.NetId == hitbox.NetId && view.Name == hitbox.Name )
                    {
                        DebugDraw::Line( hitbox.Position, view.Position, DebugDraw::Cyan, 0.f, DebugDraw::NoDepthTest );
                        if( !m_drawHitboxes )
                        {
                            drawView( view, NetDebugDraw::kLive );
                        }
                    }
                }
            }
        }
    }
    if( m_drawTrails )
    {
        for( const auto& [id, trail] : m_trails )
        {
            auto named = m_shown->Table.find( id );
            const uint8_t owner = named != m_shown->Table.end() ? named->second.Owner : 0;
            const Vector4 color = owner == 0 ? DebugDraw::Gray : owner % 2 ? DebugDraw::Cyan : DebugDraw::Magenta;
            for( size_t i = 1; i < trail.size(); ++i )
            {
                if( trail[i].first == trail[i - 1].first + 1 && ( trail[i].second - trail[i - 1].second ).Length() < 2.f )
                {
                    DebugDraw::Line( trail[i - 1].second, trail[i].second, color, 0.f, DebugDraw::NoDepthTest );
                }
            }
        }
    }
#else
    (void)InNet;
#endif
}


void ReplayViewer::SetCamera( CameraMode InMode, uint32_t InNetId )
{
    m_camera = InMode;
    m_cameraTarget = InNetId;
    if( InMode == CameraMode::Free )
    {
        m_freePlaced = false;
    }
}


void ReplayViewer::HidePovTarget( Transform& InBody )
{
    std::vector<Transform*> pending = { &InBody };
    while( !pending.empty() )
    {
        Transform* transform = pending.back();
        pending.pop_back();
        if( Entity* entity = transform->Parent.Get() )
        {
            bool hid = false;
            if( Mesh* mesh = entity->TryGetComponent<Mesh>(); mesh && mesh->IsEnabled() )
            {
                mesh->SetEnabled( false );
                hid = true;
            }
            if( Model* model = entity->TryGetComponent<Model>(); model && model->IsEnabled() )
            {
                model->SetEnabled( false );
                hid = true;
            }
            if( TextRenderer* text = entity->TryGetComponent<TextRenderer>(); text && text->IsEnabled() )
            {
                text->SetEnabled( false );
                hid = true;
            }
            if( hid )
            {
                m_povHidden.push_back( transform->Parent );
            }
        }
        for( Transform* child : transform->GetChildren() )
        {
            pending.push_back( child );
        }
    }
}


void ReplayViewer::ShowPovTarget()
{
    // What it hid (a component disabled since by something else is turned back on too: the POV's only).
    for( EntityHandle& entity : m_povHidden )
    {
        if( !entity )
        {
            continue;
        }
        if( Mesh* mesh = entity->TryGetComponent<Mesh>() )
        {
            mesh->SetEnabled( true );
        }
        if( Model* model = entity->TryGetComponent<Model>() )
        {
            model->SetEnabled( true );
        }
        if( TextRenderer* text = entity->TryGetComponent<TextRenderer>() )
        {
            text->SetEnabled( true );
        }
    }
    m_povHidden.clear();
}


void ReplayViewer::UpdateCamera( NetCore& InNet, float InDeltaSeconds )
{
    // A POV's target shows again once the camera leaves it.
    if( !m_povHidden.empty() && ( m_camera != CameraMode::Pov || m_cameraTarget != m_povTarget ) )
    {
        ShowPovTarget();
    }
    // The replay world's main camera (ME-297).
    Camera* main = Camera::GetMain( &InNet.GetWorld() );
    if( m_camera == CameraMode::Game || !main || !main->Parent )
    {
        return;
    }
    Transform* camera = main->Parent->TryGetComponent<Transform>();
    if( !camera )
    {
        return;
    }
    if( m_camera == CameraMode::Free )
    {
        if( !m_freePlaced )
        {
            m_freePosition = camera->GetWorldPosition();
            const Vector3 forward = camera->GetWorldRotation() * Vector3( 0.f, 0.f, 1.f );
            m_freeYaw = std::atan2( forward.x, forward.z ) * 180.f / 3.14159265f;
            m_freePitch = std::asin( std::clamp( forward.y, -1.f, 1.f ) ) * 180.f / 3.14159265f;
            m_freePlaced = true;
        }
#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
        const ImGuiIO& io = ImGui::GetIO();
        if( !io.WantCaptureKeyboard )
        {
            const Vector3 forward = AimDirection( m_freeYaw, m_freePitch );
            const Vector3 right = AimDirection( m_freeYaw + 90.f, 0.f );
            const float speed = ( io.KeyShift ? 20.f : 6.f ) * InDeltaSeconds;
            Vector3 move;
            if( ImGui::IsKeyDown( ImGuiKey_W ) ) move = move + forward;
            if( ImGui::IsKeyDown( ImGuiKey_S ) ) move = move - forward;
            if( ImGui::IsKeyDown( ImGuiKey_D ) ) move = move + right;
            if( ImGui::IsKeyDown( ImGuiKey_A ) ) move = move - right;
            if( ImGui::IsKeyDown( ImGuiKey_E ) ) move = move + Vector3( 0.f, 1.f, 0.f );
            if( ImGui::IsKeyDown( ImGuiKey_Q ) ) move = move - Vector3( 0.f, 1.f, 0.f );
            m_freePosition = m_freePosition + move * speed;
        }
        if( !io.WantCaptureMouse && ImGui::IsMouseDown( ImGuiMouseButton_Right ) )
        {
            m_freeYaw += io.MouseDelta.x * 0.15f;
            m_freePitch = std::clamp( m_freePitch - io.MouseDelta.y * 0.15f, -89.f, 89.f );
        }
#else
        (void)InDeltaSeconds;
#endif
        camera->SetWorldPosition( m_freePosition );
        camera->LookAt( m_freePosition + AimDirection( m_freeYaw, m_freePitch ) );
        return;
    }
    EntityHandle target = InNet.FindEntity( m_cameraTarget );
    Transform* transform = target ? target->TryGetComponent<Transform>() : nullptr;
    if( !transform )
    {
        return;
    }
    // The owner's recorded aim, else where the entity faces.
    const Vector3 at = transform->GetWorldPosition();
    float yaw = 0.f;
    float pitch = 0.f;
    bool aimed = false;
    if( m_shown )
    {
        auto named = m_shown->Table.find( m_cameraTarget );
        const NetReplayClient* client = named != m_shown->Table.end() && named->second.Owner ? FindClient( m_shown->Record, named->second.Owner ) : nullptr;
        if( client && client->Command.HasAim )
        {
            yaw = client->Command.Yaw;
            pitch = client->Command.Pitch;
            aimed = true;
        }
    }
    if( !aimed )
    {
        const Vector3 forward = transform->GetWorldRotation() * Vector3( 0.f, 0.f, 1.f );
        yaw = std::atan2( forward.x, forward.z ) * 180.f / 3.14159265f;
    }
    if( m_camera == CameraMode::Follow )
    {
        const Vector3 behind = AimDirection( yaw, 0.f ) * -4.f + Vector3( 0.f, 1.8f, 0.f );
        camera->SetWorldPosition( at + behind );
        camera->LookAt( at + Vector3( 0.f, 0.8f, 0.f ) );
    }
    else
    {
        // What a player's screen had at the tick shown (ME-343, as its killcams show it): its eye and aim from its
        // tick whose command's view tick that is (the others on its screen, its interpolation delay behind), not
        // the tick shown's own (1-42 degrees off what it was aiming at). A bot's: where it is and faces.
        Vector3 origin = at;
        uint8_t owner = 0;
        if( m_shown )
        {
            auto named = m_shown->Table.find( m_cameraTarget );
            owner = named != m_shown->Table.end() ? named->second.Owner : 0;
        }
        const NetReplayPlayback* playback = InNet.GetPlayback();
        if( owner != 0 && playback )
        {
            if( !m_povReader )
            {
                m_povSource = std::make_unique<NetReplayFileSource>();
                if( m_povSource->Open( m_path ) )
                {
                    m_povReader = std::make_unique<NetReplayReader>( *m_povSource );
                }
            }
            if( m_povTarget != m_cameraTarget || m_povClient != owner )
            {
                m_povSamples.clear();
                m_povTarget = m_cameraTarget;
                m_povClient = owner;
            }
            if( m_povReader )
            {
                KillcamPlayer::ReadKillerSamples( *m_povReader, owner, m_cameraTarget, playback->GetFirstTick(), playback->GetLastTick(), playback->GetTickRate(), playback->GetTime(),
                    m_povSamples );
                const KillcamPlayer::KillerView view = KillcamPlayer::FindKillerView( m_povSamples, playback->GetTime() );
                if( view.HasPosition )
                {
                    origin = view.Position;
                }
                if( view.HasAim )
                {
                    yaw = view.Yaw;
                    pitch = view.Pitch;
                }
            }
        }
        HidePovTarget( *transform );
        // Its eyes: where the game puts them (C#'s Net.Killcam.EyeHeight), else near the top of its capsule.
        const SingletonKillcam* game = InNet.GetWorld().FindSingleton<SingletonKillcam>();
        const CharacterController* controller = target->TryGetComponent<CharacterController>();
        const float height = game && game->EyeHeight >= 0.f ? game->EyeHeight : controller ? controller->Height * 0.5f - 0.15f : 1.6f;
        const Vector3 eye = origin + Vector3( 0.f, height, 0.f );
        camera->SetWorldPosition( eye );
        camera->LookAt( eye + AimDirection( yaw, pitch ) );
    }
}


std::string ReplayViewer::ResimText( const NetReplayResim::Result& InResult ) const
{
    return NetReplayResim::Describe( InResult, 6 );
}


json ReplayViewer::MakeReport( NetCore& InNet )
{
    json report;
    NetReplayPlayback* playback = InNet.GetPlayback();
    if( !playback || !m_shown || !m_shown->Valid )
    {
        report["error"] = "nothing shown";
        return report;
    }
    const NetReplayTick& record = m_shown->Record;
    const Shown* before = m_before && m_before->Valid ? m_before.get() : nullptr;
    report["replay"] = m_path;
    report["tick"] = m_shown->Tick;
    report["firstTick"] = playback->GetFirstTick();
    report["lastTick"] = playback->GetLastTick();
    report["beforeTick"] = before ? json( before->Tick ) : json();
    // Script fields of the tick before are only known when the world showed it.
    report["beforeShown"] = before && !before->Script.empty();
    if( const NetReplayTimeline* timeline = GetTimeline() )
    {
        json counts;
        for( uint8_t i = 0; i < static_cast<uint8_t>( NetReplayMarker::Kind::Count ); ++i )
        {
            const auto kind = static_cast<NetReplayMarker::Kind>( i );
            counts[NetReplayTimeline::KindName( kind )] = timeline->Count( kind );
        }
        report["markers"] = counts;
        if( m_jumpMarker >= 0 && static_cast<size_t>( m_jumpMarker ) < timeline->GetMarkers().size() )
        {
            const NetReplayMarker& marker = timeline->GetMarkers()[m_jumpMarker];
            report["marker"] = { { "tick", marker.Tick }, { "kind", NetReplayTimeline::KindName( marker.Type ) }, { "name", marker.Name }, { "label", marker.Label },
                { "entity", marker.Entity }, { "client", marker.Client } };
        }
        json here = json::array();
        for( const NetReplayMarker& marker : timeline->GetMarkers() )
        {
            if( marker.Tick == m_shown->Tick )
            {
                here.push_back( { { "kind", NetReplayTimeline::KindName( marker.Type ) }, { "label", marker.Label } } );
            }
        }
        report["markersHere"] = here;
    }

    // Inspector: every entity recorded at the tick.
    json entities = json::array();
    for( const auto& [id, state] : record.State.Entities )
    {
        json entry;
        auto named = m_shown->Table.find( id );
        entry["netId"] = id;
        entry["name"] = named != m_shown->Table.end() ? named->second.Name : std::string();
        entry["prefab"] = named != m_shown->Table.end() ? named->second.Prefab : std::string();
        entry["owner"] = named != m_shown->Table.end() ? named->second.Owner : 0;
        entry["position"] = VecJson( state.GetPosition() );
        const Quaternion rotation = state.GetRotation();
        entry["rotation"] = json::array( { rotation.x, rotation.y, rotation.z, rotation.w } );
        entry["fields"] = Hex( state.Fields );
        const NetEntityState* previous = nullptr;
        if( before )
        {
            auto it = before->Record.State.Entities.find( id );
            previous = it != before->Record.State.Entities.end() ? &it->second : nullptr;
        }
        entry["new"] = before && !previous;
        entry["poseChanged"] = previous && ( previous->Position != state.Position || previous->Rotation != state.Rotation );
        entry["changedBytes"] = previous ? ChangedBytes( state.Fields, previous->Fields ) : std::vector<size_t>();
        // C++ Replicated fields by name, and which changed.
        json fields = json::array();
        auto described = m_shown->Fields.find( id );
        if( described != m_shown->Fields.end() )
        {
            const std::vector<NetSchema::FieldValue>* old = nullptr;
            if( before )
            {
                auto it = before->Fields.find( id );
                old = it != before->Fields.end() ? &it->second : nullptr;
            }
            for( size_t i = 0; i < described->second.size(); ++i )
            {
                const NetSchema::FieldValue& value = described->second[i];
                const bool changed = old && i < old->size() && ( *old )[i].Value != value.Value;
                fields.push_back( { { "component", value.Component }, { "field", value.Field }, { "value", value.Value }, { "changed", changed } } );
            }
            const size_t start = std::min( m_shown->ScriptStart[id], state.Fields.size() );
            entry["scriptBytes"] = Hex( std::vector<uint8_t>( state.Fields.begin() + static_cast<std::ptrdiff_t>( start ), state.Fields.end() ) );
        }
        entry["replicated"] = fields;
        // The script's public fields as the world has them, and which changed since the tick before.
        json script = json::object();
        json scriptChanged = json::array();
        auto now = m_shown->Script.find( id );
        if( now != m_shown->Script.end() )
        {
            const std::map<std::string, std::string>* old = nullptr;
            if( before )
            {
                auto it = before->Script.find( id );
                old = it != before->Script.end() && !it->second.empty() ? &it->second : nullptr;
            }
            for( const auto& [name, value] : now->second )
            {
                script[name] = json::parse( value, nullptr, false );
                if( old )
                {
                    auto was = old->find( name );
                    if( was == old->end() || was->second != value )
                    {
                        scriptChanged.push_back( name );
                    }
                }
            }
        }
        entry["script"] = script;
        entry["scriptChanged"] = scriptChanged;
        if( const NetReplayMover* mover = FindMover( record, id ) )
        {
            const NetReplayMover* old = before ? FindMover( before->Record, id ) : nullptr;
            entry["mover"] = { { "position", VecJson( mover->State.Position ) }, { "velocity", VecJson( mover->State.Velocity ) },
                { "moveInput", VecJson( mover->State.MoveInput ) }, { "grounded", mover->State.Grounded }, { "wantsJump", mover->State.WantsJump },
                { "predicted", Hex( mover->Predicted ) }, { "predictedChanged", old ? ChangedBytes( mover->Predicted, old->Predicted ) : std::vector<size_t>() },
                { "moved", old ? json( !Same( old->State.Position, mover->State.Position ) ) : json() } };
        }
        EntityHandle entity = InNet.FindEntity( id );
        if( entity && entity->HasComponent<Transform>() )
        {
            entry["world"] = { { "position", VecJson( entity->GetComponent<Transform>().GetWorldPosition() ) } };
        }
        entities.push_back( entry );
    }
    report["entities"] = entities;

    // Input: each client's command this tick.
    json inputs = json::array();
    for( const NetReplayClient& client : record.Clients )
    {
        const NetReplayClient* old = before ? FindClient( before->Record, client.Id ) : nullptr;
        json pressed = json::array();
        json held = json::array();
        json axes = json::object();
        for( const Action& action : m_actions )
        {
            if( action.IsButton && action.Index >= 0 && action.Index < 32 )
            {
                const bool down = ( client.Command.Buttons >> action.Index ) & 1u;
                if( down )
                {
                    held.push_back( action.Name );
                    if( !old || !( ( old->Command.Buttons >> action.Index ) & 1u ) )
                    {
                        pressed.push_back( action.Name );
                    }
                }
            }
            else if( !action.IsButton )
            {
                json values = json::array();
                for( int i = 0; i < action.AxisCount && action.Index + i < static_cast<int>( client.Command.Axes.size() ); ++i )
                {
                    values.push_back( client.Command.Axes[action.Index + i] );
                }
                axes[action.Name] = values;
            }
        }
        uint32_t pawn = 0;
        for( const NetReplayMover& mover : record.Movers )
        {
            auto named = m_shown->Table.find( mover.NetId );
            if( named != m_shown->Table.end() && named->second.Owner == client.Id )
            {
                pawn = mover.NetId;
            }
        }
        inputs.push_back( { { "client", client.Id }, { "pawn", pawn }, { "buttons", client.Command.Buttons }, { "held", held }, { "pressed", pressed }, { "axes", axes },
            { "rawAxes", json::array( { client.Command.Axes[0], client.Command.Axes[1], client.Command.Axes[2], client.Command.Axes[3] } ) },
            { "hasAim", client.Command.HasAim }, { "yaw", client.Command.Yaw }, { "pitch", client.Command.Pitch }, { "viewTick", client.Command.ViewTick },
            { "missed", client.Missed }, { "roundTripMs", client.RoundTripMs }, { "packetLoss", client.PacketLoss }, { "slack", client.Slack },
            { "text", CommandText( client.Command ) } } );
    }
    report["inputs"] = inputs;

    // Lag compensation: each shot, its rewound hitboxes and the same ones live in the world.
    const std::vector<HitboxView> live = LiveHitboxes( InNet );
    json shots = json::array();
    for( const NetReplayShot& shot : record.Shots )
    {
        json hitboxes = json::array();
        for( const NetReplayHitbox& hitbox : shot.Hitboxes )
        {
            json entry = { { "netId", hitbox.NetId }, { "name", hitbox.Name }, { "shape", hitbox.Shape }, { "rewound", VecJson( hitbox.Position ) },
                { "radius", hitbox.Radius }, { "halfHeight", hitbox.HalfHeight }, { "occluder", hitbox.Occluder },
                { "target", shot.Hit && hitbox.NetId == shot.Target && hitbox.Name == shot.TargetPart } };
            for( const HitboxView& view : live )
            {
                if( view.NetId == hitbox.NetId && view.Name == hitbox.Name )
                {
                    entry["live"] = VecJson( view.Position );
                    entry["rewindOffset"] = ( view.Position - hitbox.Position ).Length();
                }
            }
            hitboxes.push_back( entry );
        }
        shots.push_back( { { "client", shot.Client }, { "shooter", shot.Shooter }, { "instigator", shot.Instigator }, { "origin", VecJson( shot.Origin ) },
            { "direction", VecJson( shot.Direction ) }, { "range", shot.Range }, { "viewTick", shot.ViewTick }, { "rewoundTicks", shot.RewoundTicks }, { "hit", shot.Hit },
            { "occluded", shot.Occluded }, { "clamped", shot.Clamped }, { "rejected", shot.Rejected }, { "target", shot.Target }, { "part", shot.TargetPart },
            { "point", VecJson( shot.Point ) }, { "distance", shot.Distance }, { "hitboxes", hitboxes },
            { "label", Label( shot.Shooter ) + ( shot.Hit ? " hit " + Label( shot.Target ) + " (" + shot.TargetPart + ")" : " missed" ) } } );
    }
    report["shots"] = shots;
    json events = json::array();
    for( const NetReplayEvent& event : record.Events )
    {
        events.push_back( { { "name", event.Name }, { "payload", event.Payload }, { "entity", event.Entity } } );
    }
    report["events"] = events;
    json resim = json::array();
    for( const NetReplayResim::Result& result : m_resimResults )
    {
        json divergences = json::array();
        for( const NetReplayResim::Divergence& divergence : result.Divergences )
        {
            divergences.push_back( { { "tick", divergence.Tick }, { "what", divergence.What } } );
        }
        resim.push_back( { { "netId", result.NetId }, { "name", result.Name }, { "client", result.Client }, { "from", result.From }, { "to", result.To },
            { "firstSeed", result.FirstSeed }, { "ticks", result.Ticks }, { "gaps", result.Gaps }, { "divergenceCount", result.DivergenceCount },
            { "firstDivergence", result.Divergences.empty() ? json() : json( result.Divergences.front().Tick ) }, { "divergences", divergences },
            { "error", result.Error } } );
    }
    report["resim"] = resim;
    report["camera"] = m_camera == CameraMode::Game ? "game" : m_camera == CameraMode::Free ? "free" : m_camera == CameraMode::Follow ? "follow" : "pov";
    report["cameraTarget"] = m_cameraTarget;
    report["heatmap"] = HeatmapOverlay::Get().Report();
    return report;
}


bool ReplayViewer::WriteReport( NetCore& InNet, const std::string& InPath, std::string& OutError )
{
    const json report = MakeReport( InNet );
    std::ofstream out( InPath );
    if( !out )
    {
        OutError = "--replay-report " + InPath + ": can't write it";
        return false;
    }
    out << report.dump( 2 ) << "\n";
    m_reportWritten = true;
    Say( CLog::LogType::Info, "Replay: report for tick " + std::to_string( m_shown ? m_shown->Tick : 0 ) + " written to " + InPath );
    return true;
}


std::string ReplayViewer::RunCommand( NetCore& InNet, const std::vector<std::string>& InArgs, bool& OutHandled )
{
    OutHandled = true;
    NetReplayPlayback* playback = InNet.GetPlayback();
    const std::string verb = InArgs.empty() ? std::string() : InArgs[0];
    auto kindMask = [&InArgs]( size_t InIndex, uint32_t& OutMask ) {
        NetReplayMarker::Kind kind;
        if( InArgs.size() > InIndex )
        {
            if( !NetReplayTimeline::ParseKind( InArgs[InIndex], kind ) )
            {
                return false;
            }
            OutMask = 1u << static_cast<uint32_t>( kind );
        }
        return true;
    };
    if( verb == "markers" || verb == "marker" || verb == "next" || verb == "prev" )
    {
        const NetReplayTimeline* timeline = GetTimeline();
        if( !timeline )
        {
            return m_timelineError.empty() ? "the timeline is still being built" : "no timeline: " + m_timelineError;
        }
        if( verb == "markers" )
        {
            uint32_t mask = ~0u;
            if( !kindMask( 1, mask ) )
            {
                return "replay markers [kill|join|leave|event|hit|miss|resync|hitch]";
            }
            std::string out;
            size_t listed = 0;
            for( const NetReplayMarker& marker : timeline->GetMarkers() )
            {
                if( ( mask >> static_cast<uint32_t>( marker.Type ) ) & 1u )
                {
                    if( ++listed <= 50 )
                    {
                        out += Format( "%s%u %s %s", out.empty() ? "" : "\n", marker.Tick, NetReplayTimeline::KindName( marker.Type ), marker.Label.c_str() );
                    }
                }
            }
            return listed > 50 ? out + Format( "\n... %zu more", listed - 50 ) : out.empty() ? "no markers" : out;
        }
        if( verb == "marker" )
        {
            NetReplayMarker::Kind kind;
            int nth = 0;
            if( InArgs.size() < 2 || !NetReplayTimeline::ParseKind( InArgs[1], kind ) || ( InArgs.size() > 2 && !ParseInt( InArgs[2], nth ) ) )
            {
                return "replay marker <kill|join|leave|event|hit|miss|resync|hitch> [n] (n from 0, negative from the end)";
            }
            std::string error;
            if( !JumpToMarker( InNet, kind, nth, error ) )
            {
                return error;
            }
            const NetReplayMarker& marker = timeline->GetMarkers()[m_jumpMarker];
            return Format( "tick %u: %s", marker.Tick, marker.Label.c_str() );
        }
        uint32_t mask = m_markerMask;
        if( !kindMask( 1, mask ) )
        {
            return "replay " + verb + " [kind]";
        }
        const int index = timeline->Neighbour( playback->GetTick(), verb == "next", mask );
        if( index < 0 )
        {
            return "no marker " + std::string( verb == "next" ? "after" : "before" ) + " tick " + std::to_string( playback->GetTick() );
        }
        m_jumpMarker = index;
        JumpTo( InNet, timeline->GetMarkers()[index].Tick );
        return Format( "tick %u: %s", timeline->GetMarkers()[index].Tick, timeline->GetMarkers()[index].Label.c_str() );
    }
    if( verb == "inspect" || verb == "input" || verb == "shots" )
    {
        Refresh( InNet );
        const json report = MakeReport( InNet );
        if( verb == "input" )
        {
            std::string out;
            for( const json& input : report["inputs"] )
            {
                out += Format( "%sclient %d pawn %s%s: %s", out.empty() ? "" : "\n", input["client"].get<int>(), Label( input["pawn"].get<uint32_t>() ).c_str(),
                    input["missed"].get<bool>() ? " (missed)" : "", input["text"].get<std::string>().c_str() );
            }
            return out.empty() ? "no commands at this tick" : out;
        }
        if( verb == "shots" )
        {
            std::string out;
            for( const json& shot : report["shots"] )
            {
                out += Format( "%s%s, rewound %.2f ticks", out.empty() ? "" : "\n", shot["label"].get<std::string>().c_str(), shot["rewoundTicks"].get<double>() );
                for( const json& hitbox : shot["hitboxes"] )
                {
                    out += "\n  " + hitbox["name"].get<std::string>() + "#" + std::to_string( hitbox["netId"].get<uint32_t>() ) + " rewound " + hitbox["rewound"].dump()
                        + ( hitbox.contains( "live" ) ? " live " + hitbox["live"].dump() + Format( " (%.3f m)", hitbox["rewindOffset"].get<float>() ) : "" );
                }
            }
            return out.empty() ? "no shots at this tick" : out;
        }
        uint32_t id = 0;
        if( InArgs.size() > 1 && ( !ParseUInt( InArgs[1], id ) || id == 0 ) )
        {
            return "replay inspect [netid]";
        }
        std::string out;
        for( const json& entity : report["entities"] )
        {
            if( id != 0 && entity["netId"].get<uint32_t>() != id )
            {
                continue;
            }
            out += Format( "%s%s owner %d at %s", out.empty() ? "" : "\n", Label( entity["netId"].get<uint32_t>() ).c_str(), entity["owner"].get<int>(), entity["position"].dump().c_str() );
            if( id != 0 )
            {
                for( const json& field : entity["replicated"] )
                {
                    out += "\n  " + field["component"].get<std::string>() + "." + field["field"].get<std::string>() + " = " + field["value"].get<std::string>()
                        + ( field["changed"].get<bool>() ? "  (changed)" : "" );
                }
                for( auto it = entity["script"].begin(); it != entity["script"].end(); ++it )
                {
                    const bool changed = std::find( entity["scriptChanged"].begin(), entity["scriptChanged"].end(), it.key() ) != entity["scriptChanged"].end();
                    out += "\n  " + it.key() + " = " + it.value().dump() + ( changed ? "  (changed)" : "" );
                }
                if( entity.contains( "mover" ) )
                {
                    out += "\n  mover " + entity["mover"].dump();
                }
            }
        }
        return out.empty() ? "nothing to inspect" : out;
    }
    if( verb == "resim" )
    {
        uint32_t id = m_selected;
        uint32_t to = playback->GetTick();
        uint32_t from = playback->GetReader().GetKeyframeTick( to );
        if( from == to && to > playback->GetFirstTick() )
        {
            from = playback->GetReader().GetKeyframeTick( to - 1 );     // on a keyframe: from the one before
        }
        for( size_t i = 1; i < InArgs.size(); ++i )
        {
            if( InArgs[i].find( ':' ) != std::string::npos )
            {
                from = playback->GetFirstTick();
                to = playback->GetLastTick();
                if( !ParseSpan( InArgs[i], from, to ) )
                {
                    return "replay resim [netid] [a:b]";
                }
            }
            else if( !ParseUInt( InArgs[i], id ) )
            {
                return "replay resim [netid] [a:b]";
            }
        }
        std::vector<uint32_t> ids;
        if( id != 0 )
        {
            ids.push_back( id );
        }
        else
        {
            ids = NetReplayResim::FindCharacters( *m_reader, from, to );
        }
        std::string out;
        for( const uint32_t character : ids )
        {
            const NetReplayResim::Result result = InNet.ResimulateReplay( character, from, to );
            out += ( out.empty() ? "" : "\n" ) + ResimText( result );
        }
        m_lastResim = out;
        return out.empty() ? "no player's character to resimulate" : out;
    }
    if( verb == "camera" )
    {
        uint32_t id = 0;
        const std::string mode = InArgs.size() > 1 ? InArgs[1] : std::string();
        if( mode == "game" || mode == "free" )
        {
            SetCamera( mode == "game" ? CameraMode::Game : CameraMode::Free );
        }
        else if( ( mode == "follow" || mode == "pov" ) && InArgs.size() > 2 && ParseUInt( InArgs[2], id ) )
        {
            SetCamera( mode == "follow" ? CameraMode::Follow : CameraMode::Pov, id );
        }
        else
        {
            return "replay camera game|free|follow <netid>|pov <netid>";
        }
        return "camera " + mode;
    }
    if( verb == "overlay" )
    {
        if( InArgs.size() < 3 || ( InArgs[2] != "on" && InArgs[2] != "off" ) )
        {
            return "replay overlay shots|hitboxes|trails|heatmap on|off";
        }
        const bool on = InArgs[2] == "on";
        if( InArgs[1] == "shots" ) m_drawShots = on;
        else if( InArgs[1] == "hitboxes" ) m_drawHitboxes = on;
        else if( InArgs[1] == "trails" ) { m_drawTrails = on; m_hasTrails = false; }
        else if( InArgs[1] == "heatmap" ) HeatmapOverlay::Get().SetShown( on );
        else return "replay overlay shots|hitboxes|trails|heatmap on|off";
        return InArgs[1] + ( on ? " on" : " off" );
    }
    if( verb == "heatmap" )
    {
        // ME-215: "replay heatmap" builds this replay's; the rest is the "heatmap" command's.
        HeatmapOverlay& overlay = HeatmapOverlay::Get();
        if( InArgs.size() < 2 || InArgs[1] == "build" )
        {
            if( !overlay.Build( { m_path } ) )
            {
                return "a heatmap is building already";
            }
            m_heatmapPending = true;
            return "building this replay's heatmap on the background lane";
        }
        return overlay.RunCommand( std::vector<std::string>( InArgs.begin() + 1, InArgs.end() ) );
    }
    if( verb == "report" )
    {
        if( InArgs.size() < 2 )
        {
            return "replay report <file.json>";
        }
        std::string error;
        return WriteReport( InNet, InArgs[1], error ) ? "written to " + InArgs[1] : error;
    }
    OutHandled = false;
    return {};
}


void ReplayViewer::DrawUI( NetCore& InNet )
{
#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
    NetReplayPlayback* playback = InNet.GetPlayback();
    if( !playback || playback->IsEmpty() )
    {
        return;
    }
    // Keys, unless a text field has them.
    if( !ImGui::GetIO().WantTextInput )
    {
        const int step = ImGui::GetIO().KeyShift ? 10 : 1;
        if( ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
        {
            playback->TogglePause();
        }
        if( ImGui::IsKeyPressed( ImGuiKey_RightArrow ) )
        {
            playback->Step( step );
        }
        if( ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) )
        {
            playback->Step( -step );
        }
        if( const NetReplayTimeline* timeline = GetTimeline() )
        {
            for( const bool forward : { false, true } )
            {
                if( ImGui::IsKeyPressed( forward ? ImGuiKey_RightBracket : ImGuiKey_LeftBracket, false ) )
                {
                    const int index = timeline->Neighbour( playback->GetTick(), forward, m_markerMask );
                    if( index >= 0 )
                    {
                        m_jumpMarker = index;
                        JumpTo( InNet, timeline->GetMarkers()[index].Tick );
                    }
                }
            }
        }
    }
    DrawReplayWindow( InNet );
    DrawInspector( InNet );
    DrawInput( InNet );
    DrawShots( InNet );
#else
    (void)InNet;
#endif
}


#if USING( ME_IMGUI ) && !USING( ME_EDITOR ) && USING( ME_GAME_TOOLS )
void ReplayViewer::DrawReplayWindow( NetCore& InNet )
{
    NetReplayPlayback* playback = InNet.GetPlayback();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + 10.f, viewport->Pos.y + viewport->Size.y - 10.f ), ImGuiCond_FirstUseEver, ImVec2( 0.f, 1.f ) );
    ImGui::SetNextWindowSize( ImVec2( std::min( 900.f, viewport->Size.x - 20.f ), 0.f ), ImGuiCond_FirstUseEver );
    if( ImGui::Begin( "Replay" ) )
    {
        if( ImGui::Button( playback->IsPaused() ? "Play" : "Pause" ) )
        {
            playback->TogglePause();
        }
        for( const int delta : { -10, -1, 1, 10 } )
        {
            ImGui::SameLine();
            const std::string label = ( delta > 0 ? "+" : "" ) + std::to_string( delta );
            if( ImGui::Button( label.c_str() ) )
            {
                playback->Step( delta );
            }
        }
        ImGui::SameLine();
        if( ImGui::Button( "< marker" ) )
        {
            bool handled = false;
            RunCommand( InNet, { "prev" }, handled );
        }
        ImGui::SameLine();
        if( ImGui::Button( "marker >" ) )
        {
            bool handled = false;
            RunCommand( InNet, { "next" }, handled );
        }
        ImGui::SameLine();
        float speed = static_cast<float>( playback->GetSpeed() );
        ImGui::SetNextItemWidth( 140.f );
        if( ImGui::SliderFloat( "Speed", &speed, static_cast<float>( NetReplayPlayback::kMinSpeed ), static_cast<float>( NetReplayPlayback::kMaxSpeed ), "%.2fx",
                ImGuiSliderFlags_Logarithmic ) )
        {
            playback->SetSpeed( speed );
        }
        ImGui::SameLine();
        const double seconds = ( playback->GetTime() - playback->GetFirstTick() ) / playback->GetTickRate();
        ImGui::Text( "tick %u  %.2f s", playback->GetTick(), seconds );

        DrawTimeline( InNet );

        bool looping = playback->IsLooping();
        if( ImGui::Checkbox( "Loop", &looping ) )
        {
            looping ? playback->SetLoop( playback->GetLoopFrom(), playback->GetLoopTo() ) : playback->ClearLoop();
        }
        ImGui::SameLine();
        int range[2] = { static_cast<int>( playback->GetLoopFrom() ), static_cast<int>( playback->GetLoopTo() ) };
        ImGui::SetNextItemWidth( 200.f );
        if( ImGui::DragInt2( "##LoopRange", range, 1.f, static_cast<int>( playback->GetFirstTick() ), static_cast<int>( playback->GetLastTick() ) ) )
        {
            playback->SetLoop( static_cast<uint32_t>( std::max( 0, range[0] ) ), static_cast<uint32_t>( std::max( 0, range[1] ) ) );
            if( !looping )
            {
                playback->ClearLoop();
            }
        }
        ImGui::SameLine();
        const char* modes[] = { "Game camera", "Free camera", "Follow", "POV" };
        int mode = static_cast<int>( m_camera );
        ImGui::SetNextItemWidth( 130.f );
        if( ImGui::Combo( "##Camera", &mode, modes, 4 ) )
        {
            SetCamera( static_cast<CameraMode>( mode ), m_cameraTarget ? m_cameraTarget : m_selected );
        }
        ImGui::SameLine();
        ImGui::Checkbox( "Shots", &m_drawShots );
        ImGui::SameLine();
        ImGui::Checkbox( "Hitboxes", &m_drawHitboxes );
        ImGui::SameLine();
        if( ImGui::Checkbox( "Trails", &m_drawTrails ) )
        {
            m_hasTrails = false;
        }
        // ME-215: the heatmap over the ground (built from this replay the first time it's ticked).
        {
            HeatmapOverlay& overlay = HeatmapOverlay::Get();
            ImGui::SameLine();
            bool heatmap = overlay.HasHeatmap() && overlay.IsShown();
            if( ImGui::Checkbox( overlay.IsBuilding() ? "Heatmap (building)" : "Heatmap", &heatmap ) )
            {
                if( heatmap && !overlay.HasHeatmap() && !overlay.IsBuilding() )
                {
                    overlay.Build( { m_path } );
                    m_heatmapPending = true;
                }
                overlay.SetShown( heatmap );
            }
            if( overlay.HasHeatmap() )
            {
                ImGui::SameLine();
                const char* layers[] = { "positions", "deaths", "kills" };
                int layer = static_cast<int>( overlay.GetLayer() );
                ImGui::SetNextItemWidth( 100.f );
                if( ImGui::Combo( "##HeatmapLayer", &layer, layers, 3 ) )
                {
                    overlay.SetLayer( static_cast<NetReplayHeatmap::Layer>( layer ) );
                }
                const std::vector<std::string> groups = overlay.GetHeatmap()->GetGroups();
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 130.f );
                if( ImGui::BeginCombo( "##HeatmapGroup", overlay.GetGroup().c_str() ) )
                {
                    for( const std::string& group : groups )
                    {
                        if( ImGui::Selectable( group.c_str(), group == overlay.GetGroup() ) )
                        {
                            overlay.SetGroup( group );
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        // The kinds the scrubber shows (and [ / ] visit).
        for( uint8_t i = 0; i < static_cast<uint8_t>( NetReplayMarker::Kind::Count ); ++i )
        {
            const auto kind = static_cast<NetReplayMarker::Kind>( i );
            bool on = ( m_markerMask >> i ) & 1u;
            if( i > 0 )
            {
                ImGui::SameLine();
            }
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( KindColor( kind ) ) );
            const size_t count = m_timelineReady ? m_timeline->Count( kind ) : 0;
            if( ImGui::Checkbox( Format( "%s %zu##kind%u", NetReplayTimeline::KindName( kind ), count, i ).c_str(), &on ) )
            {
                m_markerMask = on ? ( m_markerMask | ( 1u << i ) ) : ( m_markerMask & ~( 1u << i ) );
            }
            ImGui::PopStyleColor();
        }
        // The tick's own events.
        if( m_shown )
        {
            for( const NetReplayEvent& event : m_shown->Record.Events )
            {
                ImGui::TextWrapped( "event %s %s %s", event.Name.c_str(), event.Payload.c_str(), event.Entity ? Label( event.Entity ).c_str() : "" );
            }
        }
    }
    ImGui::End();
}


void ReplayViewer::DrawTimeline( NetCore& InNet )
{
    NetReplayPlayback* playback = InNet.GetPlayback();
    const NetReplayTimeline* timeline = GetTimeline();
    const uint32_t first = playback->GetFirstTick();
    const uint32_t last = std::max( playback->GetLastTick(), first + 1 );
    const float width = std::max( 100.f, ImGui::GetContentRegionAvail().x );
    const float markerHeight = 30.f;
    const float laneHeight = 34.f;
    const size_t lanes = timeline ? timeline->GetClients().size() : 0;
    const float height = markerHeight + lanes * laneHeight;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton( "##Timeline", ImVec2( width, height ) );
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    auto xOf = [&]( double InTick ) { return origin.x + static_cast<float>( ( InTick - first ) / static_cast<double>( last - first ) ) * width; };
    auto tickOf = [&]( float InX ) {
        const double t = std::clamp( ( InX - origin.x ) / width, 0.f, 1.f );
        return static_cast<uint32_t>( std::lround( first + t * ( last - first ) ) );
    };
    draw->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), IM_COL32( 25, 25, 30, 255 ) );
    if( playback->IsLooping() )
    {
        draw->AddRectFilled( ImVec2( xOf( playback->GetLoopFrom() ), origin.y ), ImVec2( xOf( playback->GetLoopTo() ), origin.y + height ), IM_COL32( 80, 80, 120, 90 ) );
    }
    // Markers, the tall kinds last (on top).
    std::vector<int> near;
    const float mouseX = ImGui::GetIO().MousePos.x;
    if( timeline )
    {
        for( int pass = 0; pass < 2; ++pass )
        {
            for( size_t i = 0; i < timeline->GetMarkers().size(); ++i )
            {
                const NetReplayMarker& marker = timeline->GetMarkers()[i];
                const bool tall = KindHeight( marker.Type ) >= 1.f;
                if( !( ( m_markerMask >> static_cast<uint32_t>( marker.Type ) ) & 1u ) || tall != ( pass == 1 ) )
                {
                    continue;
                }
                const float x = xOf( marker.Tick );
                const float x2 = std::max( x + 1.f, xOf( marker.Tick + marker.Ticks - 1 ) );
                const float top = origin.y + markerHeight * ( 1.f - KindHeight( marker.Type ) );
                draw->AddRectFilled( ImVec2( x - ( tall ? 1.f : 0.f ), top ), ImVec2( x2 + ( tall ? 1.f : 0.f ), origin.y + markerHeight ), KindColor( marker.Type ) );
                if( hovered && std::abs( mouseX - x ) <= 4.f )
                {
                    near.push_back( static_cast<int>( i ) );
                }
            }
        }
    }
    else
    {
        draw->AddText( ImVec2( origin.x + 6.f, origin.y + 8.f ), IM_COL32( 180, 180, 180, 255 ),
            m_timelineError.empty() ? "building the timeline..." : ( "no timeline: " + m_timelineError ).c_str() );
    }
    // Each client's link: RTT (green, against the highest), slack (blue, ticks against 10) and missed
    // commands (red ticks), one value per pixel column (the column's worst).
    if( timeline )
    {
        for( size_t lane = 0; lane < lanes; ++lane )
        {
            const NetReplayClientSeries& series = timeline->GetClients()[lane];
            const float top = origin.y + markerHeight + lane * laneHeight;
            const float bottom = top + laneHeight - 3.f;
            draw->AddLine( ImVec2( origin.x, top ), ImVec2( origin.x + width, top ), IM_COL32( 60, 60, 70, 255 ) );
            const float rttScale = std::max( 50.f, series.MaxRoundTripMs );
            const float slackScale = std::max( 10.f, series.MaxSlack );
            ImVec2 previousRtt( -1.f, 0.f );
            ImVec2 previousSlack( -1.f, 0.f );
            const int columns = static_cast<int>( width );
            for( int column = 0; column < columns; ++column )
            {
                const uint32_t from = tickOf( origin.x + column );
                const uint32_t to = std::max( from, tickOf( origin.x + column + 1 ) );
                float rtt = -1.f;
                float slack = -1.f;
                bool missed = false;
                for( uint32_t t = from; t <= to; ++t )
                {
                    if( t < series.FirstTick || t - series.FirstTick >= series.RoundTripMs.size() || std::isnan( series.RoundTripMs[t - series.FirstTick] ) )
                    {
                        continue;
                    }
                    rtt = std::max( rtt, series.RoundTripMs[t - series.FirstTick] );
                    slack = std::max( slack, series.Slack[t - series.FirstTick] );
                    missed = missed || series.Missed[t - series.FirstTick];
                }
                const float x = origin.x + column;
                if( rtt >= 0.f )
                {
                    const ImVec2 rttPoint( x, bottom - ( bottom - top - 4.f ) * std::min( 1.f, rtt / rttScale ) );
                    const ImVec2 slackPoint( x, bottom - ( bottom - top - 4.f ) * std::min( 1.f, slack / slackScale ) );
                    if( previousRtt.x >= 0.f )
                    {
                        draw->AddLine( previousRtt, rttPoint, IM_COL32( 90, 220, 120, 255 ) );
                        draw->AddLine( previousSlack, slackPoint, IM_COL32( 90, 150, 255, 200 ) );
                    }
                    previousRtt = rttPoint;
                    previousSlack = slackPoint;
                }
                else
                {
                    previousRtt.x = previousSlack.x = -1.f;
                }
                if( missed )
                {
                    draw->AddLine( ImVec2( x, bottom - 6.f ), ImVec2( x, bottom ), IM_COL32( 255, 70, 70, 255 ) );
                }
            }
            draw->AddText( ImVec2( origin.x + 4.f, top + 1.f ), IM_COL32( 200, 200, 200, 255 ),
                Format( "client %u  rtt <=%.0f ms  slack <=%.1f  misses %u  resyncs %u", series.Client, series.MaxRoundTripMs, series.MaxSlack, series.Misses, series.Resyncs ).c_str() );
        }
    }
    // The playhead.
    const float head = xOf( playback->GetTime() );
    draw->AddLine( ImVec2( head, origin.y ), ImVec2( head, origin.y + height ), IM_COL32_WHITE, 2.f );

    if( hovered )
    {
        const uint32_t tick = tickOf( mouseX );
        ImGui::BeginTooltip();
        ImGui::Text( "tick %u", tick );
        if( timeline )
        {
            for( const NetReplayClientSeries& series : timeline->GetClients() )
            {
                if( tick >= series.FirstTick && tick - series.FirstTick < series.RoundTripMs.size() && !std::isnan( series.RoundTripMs[tick - series.FirstTick] ) )
                {
                    const size_t at = tick - series.FirstTick;
                    ImGui::Text( "client %u: rtt %.1f ms, loss %.1f%%, slack %.2f%s", series.Client, series.RoundTripMs[at], series.PacketLoss[at] * 100.f, series.Slack[at],
                        series.Missed[at] ? ", missed" : "" );
                }
            }
        }
        for( size_t i = 0; i < near.size() && i < 8; ++i )
        {
            const NetReplayMarker& marker = timeline->GetMarkers()[near[i]];
            ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( KindColor( marker.Type ) ), "%u %s", marker.Tick, marker.Label.c_str() );
        }
        ImGui::EndTooltip();
        // Click a marker to jump to it (the tallest kind under the mouse), anywhere else to seek.
        if( ImGui::IsItemClicked( ImGuiMouseButton_Left ) && !near.empty() )
        {
            int best = near.front();
            for( const int index : near )
            {
                if( KindHeight( timeline->GetMarkers()[index].Type ) > KindHeight( timeline->GetMarkers()[best].Type ) )
                {
                    best = index;
                }
            }
            m_jumpMarker = best;
            JumpTo( InNet, timeline->GetMarkers()[best].Tick );
        }
        else if( ImGui::IsItemActive() && ImGui::IsMouseDown( ImGuiMouseButton_Left ) && near.empty() )
        {
            playback->Seek( tick );
        }
    }
}


void ReplayViewer::DrawInspector( NetCore& InNet )
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x - 10.f, viewport->Pos.y + 10.f ), ImGuiCond_FirstUseEver, ImVec2( 1.f, 0.f ) );
    ImGui::SetNextWindowSize( ImVec2( 420.f, viewport->Size.y * 0.55f ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( "Replay Inspector" ) || !m_shown )
    {
        ImGui::End();
        return;
    }
    const Shown* before = m_before && m_before->Valid ? m_before.get() : nullptr;
    ImGui::Text( "tick %u%s", m_shown->Tick, before ? Format( " (changes since %u%s)", before->Tick, before->Script.empty() ? ", recorded only" : "" ).c_str() : "" );
    if( ImGui::BeginListBox( "##Entities", ImVec2( -1.f, 110.f ) ) )
    {
        for( const auto& [id, state] : m_shown->Record.State.Entities )
        {
            const NetEntityState* old = nullptr;
            if( before )
            {
                auto it = before->Record.State.Entities.find( id );
                old = it != before->Record.State.Entities.end() ? &it->second : nullptr;
            }
            auto named = m_shown->Table.find( id );
            const bool changed = before && ( !old || *old != state );
            if( changed )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, kChanged );
            }
            if( ImGui::Selectable( Format( "%s  owner %u%s", Label( id ).c_str(), named != m_shown->Table.end() ? named->second.Owner : 0u, changed ? "  *" : "" ).c_str(), m_selected == id ) )
            {
                m_selected = id;
            }
            if( changed )
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndListBox();
    }
    auto found = m_shown->Record.State.Entities.find( m_selected );
    if( found == m_shown->Record.State.Entities.end() )
    {
        ImGui::TextDisabled( "Select an entity." );
        ImGui::End();
        return;
    }
    const uint32_t id = m_selected;
    const NetEntityState& state = found->second;
    const NetEntityState* old = nullptr;
    if( before )
    {
        auto it = before->Record.State.Entities.find( id );
        old = it != before->Record.State.Entities.end() ? &it->second : nullptr;
    }
    auto line = []( bool InChanged, const std::string& InText ) {
        InChanged ? ImGui::TextColored( kChanged, "%s", InText.c_str() ) : ImGui::TextUnformatted( InText.c_str() );
    };
    if( ImGui::Button( "Follow" ) )
    {
        SetCamera( CameraMode::Follow, id );
    }
    ImGui::SameLine();
    if( ImGui::Button( "POV" ) )
    {
        SetCamera( CameraMode::Pov, id );
    }
    const NetReplayMover* mover = FindMover( m_shown->Record, id );
    if( mover )
    {
        ImGui::SameLine();
        if( ImGui::Button( "Resim from keyframe" ) )
        {
            bool handled = false;
            RunCommand( InNet, { "resim", std::to_string( id ) }, handled );
        }
        ImGui::SameLine();
        if( ImGui::Button( "Resim all" ) )
        {
            bool handled = false;
            RunCommand( InNet, { "resim", std::to_string( id ), "0:" + std::to_string( InNet.GetPlayback()->GetLastTick() ) }, handled );
        }
    }
    if( !m_lastResim.empty() )
    {
        ImGui::TextWrapped( "%s", m_lastResim.c_str() );
    }
    ImGui::Separator();
    line( old && ( old->Position != state.Position ), "position " + Vec( state.GetPosition() ) );
    const Vector3 euler = Quaternion::ToEulerAngles( state.GetRotation() );
    line( old && old->Rotation != state.Rotation, "rotation " + Vec( euler ) );
    EntityHandle entity = InNet.FindEntity( id );
    if( entity && entity->HasComponent<Transform>() )
    {
        ImGui::TextDisabled( "world %s", Vec( entity->GetComponent<Transform>().GetWorldPosition() ).c_str() );
    }
    auto described = m_shown->Fields.find( id );
    if( described != m_shown->Fields.end() && !described->second.empty() && ImGui::CollapsingHeader( "Replicated fields (C++)", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const std::vector<NetSchema::FieldValue>* was = nullptr;
        if( before )
        {
            auto it = before->Fields.find( id );
            was = it != before->Fields.end() ? &it->second : nullptr;
        }
        for( size_t i = 0; i < described->second.size(); ++i )
        {
            const NetSchema::FieldValue& value = described->second[i];
            line( was && i < was->size() && ( *was )[i].Value != value.Value, value.Component + "." + value.Field + " = " + value.Value );
        }
    }
    auto script = m_shown->Script.find( id );
    if( script != m_shown->Script.end() && !script->second.empty() && ImGui::CollapsingHeader( "Script fields", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const std::map<std::string, std::string>* was = nullptr;
        if( before )
        {
            auto it = before->Script.find( id );
            was = it != before->Script.end() && !it->second.empty() ? &it->second : nullptr;
        }
        for( const auto& [name, value] : script->second )
        {
            const auto it = was ? was->find( name ) : std::map<std::string, std::string>::const_iterator();
            line( was && ( it == was->end() || it->second != value ), name + " = " + value );
        }
    }
    if( ImGui::CollapsingHeader( "Recorded bytes" ) )
    {
        // [u16 size][C++ (NetSchema) bytes][script bytes].
        const size_t start = std::min( m_shown->ScriptStart.count( id ) ? m_shown->ScriptStart.at( id ) : 0, state.Fields.size() );
        const std::vector<size_t> changed = old ? ChangedBytes( state.Fields, old->Fields ) : std::vector<size_t>();
        ImGui::TextWrapped( "fields %s (2 size, %zu C++, %zu script)", Hex( state.Fields ).c_str(), start >= 2 ? start - 2 : 0, state.Fields.size() - start );
        if( !changed.empty() )
        {
            std::string list;
            for( const size_t index : changed )
            {
                list += Format( "%s%zu", list.empty() ? "" : ",", index );
            }
            ImGui::TextColored( kChanged, "changed bytes %s", list.c_str() );
        }
    }
    if( mover && ImGui::CollapsingHeader( "Mover", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const NetReplayMover* was = before ? FindMover( before->Record, id ) : nullptr;
        line( was && !Same( was->State.Position, mover->State.Position ), "position " + Format( "(%.6f, %.6f, %.6f)", mover->State.Position.x, mover->State.Position.y, mover->State.Position.z ) );
        line( was && !Same( was->State.Velocity, mover->State.Velocity ), "velocity " + Vec( mover->State.Velocity ) );
        line( was && !Same( was->State.MoveInput, mover->State.MoveInput ), "move input " + Vec( mover->State.MoveInput ) );
        line( was && was->State.Grounded != mover->State.Grounded, std::string( "grounded " ) + ( mover->State.Grounded ? "yes" : "no" ) );
        const std::vector<size_t> changed = was ? ChangedBytes( mover->Predicted, was->Predicted ) : std::vector<size_t>();
        line( !changed.empty(), "[Predicted] " + Hex( mover->Predicted ) );
    }
    ImGui::End();
}


void ReplayViewer::DrawInput( NetCore& InNet )
{
    (void)InNet;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + 10.f, viewport->Pos.y + 10.f ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( "Replay Input", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) || !m_shown )
    {
        ImGui::End();
        return;
    }
    const Shown* before = m_before && m_before->Valid ? m_before.get() : nullptr;
    for( const NetReplayClient& client : m_shown->Record.Clients )
    {
        const NetReplayClient* old = before ? FindClient( before->Record, client.Id ) : nullptr;
        ImGui::Text( "client %u%s  rtt %.1f ms  loss %.1f%%  slack %.2f", client.Id, client.Missed ? " (missed: repeated)" : "", client.RoundTripMs, client.PacketLoss * 100.f, client.Slack );
        // Buttons: held ones lit, the ones pressed this tick in yellow.
        for( const Action& action : m_actions )
        {
            if( !action.IsButton || action.Index < 0 || action.Index >= 32 )
            {
                continue;
            }
            const bool down = ( client.Command.Buttons >> action.Index ) & 1u;
            const bool pressed = down && ( !old || !( ( old->Command.Buttons >> action.Index ) & 1u ) );
            ImGui::SameLine();
            ImGui::TextColored( pressed ? kChanged : down ? ImVec4( 0.4f, 1.f, 0.4f, 1.f ) : ImVec4( 0.4f, 0.4f, 0.4f, 1.f ), "%s", action.Name.c_str() );
        }
        ImGui::TextDisabled( "  %s", CommandText( client.Command ).c_str() );
    }
    ImGui::End();
}


void ReplayViewer::DrawShots( NetCore& InNet )
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + 10.f, viewport->Pos.y + 120.f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( ImVec2( std::min( 560.f, viewport->Size.x * 0.45f ), std::max( 120.f, viewport->Size.y * 0.3f ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( "Replay Shots" ) || !m_shown )
    {
        ImGui::End();
        return;
    }
    if( m_shown->Record.Shots.empty() )
    {
        ImGui::TextDisabled( "No lag-compensated shots this tick." );
        ImGui::End();
        return;
    }
    if( m_selectedShot >= static_cast<int>( m_shown->Record.Shots.size() ) )
    {
        m_selectedShot = -1;
    }
    if( ImGui::Selectable( "all shots", m_selectedShot < 0 ) )
    {
        m_selectedShot = -1;
    }
    const std::vector<HitboxView> live = LiveHitboxes( InNet );
    for( size_t i = 0; i < m_shown->Record.Shots.size(); ++i )
    {
        const NetReplayShot& shot = m_shown->Record.Shots[i];
        const std::string text = Format( "client %u %s%s %s, rewound %.2f ticks (view %.2f)", shot.Client, Label( shot.Shooter ).c_str(),
            shot.Instigator ? ( " (sweep, " + Label( shot.Instigator ) + ")" ).c_str() : "",
            shot.Hit ? ( "HIT " + Label( shot.Target ) + " (" + shot.TargetPart + ")" ).c_str() : shot.Occluded ? "occluded" : "missed", shot.RewoundTicks, shot.ViewTick );
        if( ImGui::Selectable( Format( "%s##shot%zu", text.c_str(), i ).c_str(), m_selectedShot == static_cast<int>( i ) ) )
        {
            m_selectedShot = static_cast<int>( i );
        }
        if( m_selectedShot == static_cast<int>( i ) || m_shown->Record.Shots.size() == 1 )
        {
            for( const NetReplayHitbox& hitbox : shot.Hitboxes )
            {
                std::string liveText = "not in the world";
                for( const HitboxView& view : live )
                {
                    if( view.NetId == hitbox.NetId && view.Name == hitbox.Name )
                    {
                        liveText = "live " + Vec( view.Position ) + Format( ", %.3f m", ( view.Position - hitbox.Position ).Length() );
                    }
                }
                const bool target = shot.Hit && hitbox.NetId == shot.Target && hitbox.Name == shot.TargetPart;
                ImGui::TextColored( target ? ImVec4( 1.f, 0.35f, 0.35f, 1.f ) : ImVec4( 1.f, 0.86f, 0.1f, 1.f ), "  %s %s rewound %s, %s", Label( hitbox.NetId ).c_str(), hitbox.Name.c_str(),
                    Vec( hitbox.Position ).c_str(), liveText.c_str() );
            }
            if( shot.Hit || shot.Occluded )
            {
                ImGui::Text( "  point %s, %.2f m", Vec( shot.Point ).c_str(), shot.Distance );
            }
        }
    }
    ImGui::End();
}
#endif
