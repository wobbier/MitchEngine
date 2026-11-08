#include "PCH.h"
#include "Engine/HeatmapOverlay.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Components/Physics/CharacterController.h"
#include "Cores/PhysicsCore.h"
#include "Debug/DebugDraw.h"
#include "Engine/DevConsole.h"
#include "Engine/Project.h"
#include "Engine/World.h"
#include "Jobs/BackgroundLane.h"
#include "Net/NetReplay.h"
#include "Net/NetReplayDump.h"
#include "optick.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace
{
    // How far above the lowest sample a cell's ray starts, how far it looks down, and where a cell
    // with no ground under it goes (a capsule's centre to its feet).
    constexpr float kRayAbove = 0.5f;
    constexpr float kRayLength = 6.f;
    constexpr float kNoGroundDrop = 0.9f;
    constexpr float kLift = 0.04f;      // over the ground, so it isn't fighting the floor's depth
    constexpr float kInset = 0.06f;     // of a cell, each side: the grid reads as cells

    // "project://.tmp/Heatmaps/x.json" (the edscript's way to name the project's files), else as given.
    std::string Resolve( const std::string& InPath )
    {
        const std::string prefix = "project://";
        return InPath.rfind( prefix, 0 ) == 0 ? Project::GetRoot() + "/" + InPath.substr( prefix.size() ) : InPath;
    }

    std::string Fixed( double InValue, int InDecimals = 1 )
    {
        char text[64];
        std::snprintf( text, sizeof( text ), "%.*f", InDecimals, InValue );
        return text;
    }
}


HeatmapOverlay& HeatmapOverlay::Get()
{
    static HeatmapOverlay s_overlay;
    return s_overlay;
}


void HeatmapOverlay::RegisterCommands()
{
    static bool registered = false;
    if( registered )
    {
        return;
    }
    registered = true;
    DevConsole::Get().Register( "heatmap",
        "a replay heatmap over the world (ME-215): heatmap [load <file.json> [layer] [group]|build <replay|folder> ...|layer positions|deaths|kills"
        "|group <name>|groups|on|off|clear|cancel|opacity <0..1>]",
        []( const std::vector<std::string>& InArgs ) { return Get().RunCommand( InArgs ); } );
}


bool HeatmapOverlay::Load( const std::string& InPath, std::string& OutError )
{
    auto heatmap = std::make_unique<NetReplayHeatmap>();
    if( !heatmap->Load( InPath, &OutError ) )
    {
        return false;
    }
    Set( std::move( *heatmap ), InPath );
    return true;
}


bool HeatmapOverlay::Build( const std::vector<std::string>& InPaths, const NetReplayHeatmap::Settings& InSettings )
{
    if( m_build.valid() )
    {
        return false;
    }
    m_buildError.clear();
    NetReplayDump::Options options;
    options.Path = InPaths.empty() ? std::string() : InPaths.front();
    options.MorePaths.assign( InPaths.begin() + ( InPaths.empty() ? 0 : 1 ), InPaths.end() );
    const std::vector<std::string> paths = NetReplayDump::ExpandPaths( options );
    auto cancel = std::make_shared<std::atomic<bool>>( false );
    m_cancel = cancel;
    // Low: every chunk of every replay is decoded; scene reads and replay writes mustn't wait on it.
    m_build = Jobs::BackgroundLane::Get().Submit( "Replay Heatmap", [paths, InSettings, cancel]() -> Built {
        Built built;
        built.Heatmap = std::make_unique<NetReplayHeatmap>( InSettings );
        std::string skipped;
        for( const std::string& path : paths )
        {
            if( cancel->load( std::memory_order_relaxed ) )
            {
                break;
            }
            NetReplayFileSource source;
            std::string error;
            if( !source.Open( path ) )
            {
                skipped += "; skipped " + path + ": " + source.GetError();
                continue;
            }
            NetReplayReader reader( source );
            if( !built.Heatmap->AddReplay( reader, path, &error, cancel.get() ) && !cancel->load( std::memory_order_relaxed ) )
            {
                skipped += "; skipped " + path + ": " + error;
            }
        }
        if( cancel->load( std::memory_order_relaxed ) )
        {
            built.Error = "the build was cancelled";
            built.Heatmap.reset();
            return built;
        }
        const size_t read = built.Heatmap->GetSources().size();
        if( read == 0 )
        {
            built.Error = ( paths.empty() ? std::string( "no replays found" ) : "none of the " + std::to_string( paths.size() ) + " replay(s) read" ) + skipped;
            built.Heatmap.reset();
        }
        built.Source = std::to_string( read ) + " replay(s)" + skipped;
        return built;
    }, Jobs::BackgroundPriority::Low );
    return true;
}


void HeatmapOverlay::Set( NetReplayHeatmap InHeatmap, const std::string& InSource )
{
    m_heatmap = std::make_unique<NetReplayHeatmap>( std::move( InHeatmap ) );
    m_source = InSource;
    const std::vector<std::string> groups = m_heatmap->GetGroups();
    if( std::find( groups.begin(), groups.end(), m_group ) == groups.end() )
    {
        m_group = "all";
    }
    m_shown = true;
    Invalidate();
    WarnIfOtherScene();
}


void HeatmapOverlay::Clear()
{
    Cancel();
    m_heatmap.reset();
    m_source.clear();
    Invalidate();
}


bool HeatmapOverlay::Cancel()
{
    if( !m_build.valid() || !m_cancel || m_cancel->load() )
    {
        return false;
    }
    m_cancel->store( true );
    return true;
}


void HeatmapOverlay::ApplyCommandLine()
{
    if( !CommandLine::Has( "--heatmap-overlay" ) )
    {
        return;
    }
    const std::string path = CommandLine::GetString( "--heatmap-overlay" );
    std::string error;
    if( path.empty() || !Load( Resolve( path ), error ) )
    {
        CLog::Log( CLog::LogType::Error, "Heatmap: --heatmap-overlay " + path + ": " + ( path.empty() ? std::string( "which file? (ReplayDump --heatmap's JSON)" ) : error ) );
        return;
    }
    // LAYER[:GROUP]
    const std::string pick = CommandLine::GetString( "--heatmap-overlay-layer" );
    if( !pick.empty() )
    {
        const size_t colon = pick.find( ':' );
        NetReplayHeatmap::Layer layer;
        if( NetReplayHeatmap::ParseLayer( pick.substr( 0, colon ), layer ) )
        {
            SetLayer( layer );
        }
        else
        {
            CLog::Log( CLog::LogType::Warning, "Heatmap: --heatmap-overlay-layer " + pick + ": no such layer (positions, deaths, kills)" );
        }
        if( colon != std::string::npos && !SetGroup( pick.substr( colon + 1 ) ) )
        {
            CLog::Log( CLog::LogType::Warning, "Heatmap: --heatmap-overlay-layer " + pick + ": the heatmap has no group " + pick.substr( colon + 1 ) );
        }
    }
    CLog::Log( CLog::LogType::Info, "Heatmap: " + Describe() );
}


void HeatmapOverlay::OnSceneLoaded( const std::string& InScene )
{
    m_scene = InScene;
    Invalidate();
    WarnIfOtherScene();
}


bool HeatmapOverlay::MatchesScene() const
{
    return !m_heatmap || m_heatmap->GetMap().empty() || m_scene.empty() || m_heatmap->GetMap() == m_scene;
}


void HeatmapOverlay::WarnIfOtherScene()
{
    if( !MatchesScene() )
    {
        CLog::Log( CLog::LogType::Warning, "Heatmap: recorded on " + m_heatmap->GetMap() + ", drawn over " + m_scene + ": the cells aren't this scene's places" );
    }
}


void HeatmapOverlay::SetLayer( NetReplayHeatmap::Layer InLayer )
{
    if( InLayer != m_layer )
    {
        m_layer = InLayer;
        Invalidate();
    }
}


bool HeatmapOverlay::SetGroup( const std::string& InGroup )
{
    if( m_heatmap )
    {
        const std::vector<std::string> groups = m_heatmap->GetGroups();
        if( std::find( groups.begin(), groups.end(), InGroup ) == groups.end() )
        {
            return false;
        }
    }
    if( InGroup != m_group )
    {
        m_group = InGroup;
        Invalidate();
    }
    return true;
}


void HeatmapOverlay::SetOpacity( float InOpacity )
{
    const float opacity = std::clamp( InOpacity, 0.05f, 1.f );
    if( opacity != m_opacity )
    {
        m_opacity = opacity;
        Invalidate();
    }
}


void HeatmapOverlay::BuildCells( World& InWorld )
{
    OPTICK_EVENT( "HeatmapOverlay::BuildCells" );
    m_cells.clear();
    m_builtFor = &InWorld;
    if( !m_heatmap )
    {
        return;
    }
    const NetReplayHeatmap::Grid grid = m_heatmap->GetGrid( m_layer, m_group );
    if( grid.Max <= 0.f )
    {
        return;
    }
    PhysicsCore* physics = InWorld.GetCore<PhysicsCore>();
    for( int row = 0; row < grid.Height; ++row )
    {
        for( int column = 0; column < grid.Width; ++column )
        {
            const float value = grid.At( column, row );
            if( value <= 0.f )
            {
                continue;
            }
            const int x = grid.MinX + column;
            const int z = grid.MinZ + row;
            Vector3 centre = m_heatmap->CellCentre( x, z );
            if( !std::isfinite( centre.y ) )
            {
                centre.y = 0.f;
            }
            // The ground under the cell's lowest sample: the nearest hit that isn't a character.
            float ground = centre.y - kNoGroundDrop;
            if( physics )
            {
                const Vector3 origin( centre.x, centre.y + kRayAbove, centre.z );
                float nearest = kRayLength + 1.f;
                for( const RaycastHit& hit : physics->RaycastAll( origin, Vector3( 0.f, -1.f, 0.f ), kRayLength ) )
                {
                    if( hit.Entity && hit.Entity->HasComponent<CharacterController>() )
                    {
                        continue;
                    }
                    if( hit.Distance < nearest )
                    {
                        nearest = hit.Distance;
                        ground = hit.Position.y;
                    }
                }
            }
            centre.y = ground + kLift;
            const float t = std::sqrt( value / grid.Max );
            const Vector3 colour = NetReplayHeatmap::Ramp( t );
            m_cells.push_back( { centre, value, Vector4( colour.x, colour.y, colour.z, m_opacity * ( 0.5f + 0.5f * t ) ) } );
        }
    }
}


void HeatmapOverlay::Draw( World& InWorld )
{
    if( m_build.valid() && m_build.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
    {
        Built built = m_build.get();
        const bool cancelled = m_cancel && m_cancel->load();
        m_cancel.reset();
        if( cancelled )
        {
            // Cleared or cancelled meanwhile: what it built (if it finished first) isn't wanted.
            CLog::Log( CLog::LogType::Info, "Heatmap: the build was cancelled" );
        }
        else if( built.Heatmap )
        {
            Set( std::move( *built.Heatmap ), built.Source );
            CLog::Log( CLog::LogType::Info, "Heatmap: built from " + built.Source + ": " + Describe() );
        }
        else
        {
            m_buildError = built.Error;
            CLog::Log( CLog::LogType::Error, "Heatmap: " + m_buildError );
        }
    }
    if( !m_heatmap || !m_shown )
    {
        return;
    }
    if( m_builtFor != &InWorld )
    {
        BuildCells( InWorld );
    }
    const float half = m_heatmap->GetSettings().CellSize * ( 0.5f - kInset );
    for( const Cell& cell : m_cells )
    {
        const Vector3 a( cell.Centre.x - half, cell.Centre.y, cell.Centre.z - half );
        const Vector3 b( cell.Centre.x + half, cell.Centre.y, cell.Centre.z - half );
        const Vector3 c( cell.Centre.x + half, cell.Centre.y, cell.Centre.z + half );
        const Vector3 d( cell.Centre.x - half, cell.Centre.y, cell.Centre.z + half );
        DebugDraw::Triangle( a, b, c, cell.Colour );
        DebugDraw::Triangle( a, c, d, cell.Colour );
    }
}


std::string HeatmapOverlay::Describe() const
{
    if( !m_heatmap )
    {
        return m_build.valid() ? "heatmap: building" : m_buildError.empty() ? "heatmap: none loaded" : "heatmap: none (" + m_buildError + ")";
    }
    std::string text = std::string( "heatmap " ) + ( m_shown ? "shown" : "hidden" ) + ": " + NetReplayHeatmap::LayerName( m_layer ) + " of " + m_group + ", "
        + Fixed( m_heatmap->GetTotal( m_layer, m_group ), m_layer == NetReplayHeatmap::Layer::Positions ? 1 : 0 )
        + ( m_layer == NetReplayHeatmap::Layer::Positions ? " s" : "" ) + " in " + std::to_string( m_cells.size() ) + " cells drawn, from " + m_source + "\n";
    if( !MatchesScene() )
    {
        text += "  warning: recorded on " + m_heatmap->GetMap() + ", drawn over " + m_scene + "\n";
    }
    return text + m_heatmap->Describe();
}


json HeatmapOverlay::Report() const
{
    json report;
    report["loaded"] = m_heatmap != nullptr;
    report["building"] = m_build.valid();
    if( !m_heatmap )
    {
        if( !m_buildError.empty() )
        {
            report["error"] = m_buildError;
        }
        return report;
    }
    report["shown"] = m_shown;
    report["source"] = m_source;
    report["layer"] = NetReplayHeatmap::LayerName( m_layer );
    report["group"] = m_group;
    report["groups"] = m_heatmap->GetGroups();
    report["total"] = m_heatmap->GetTotal( m_layer, m_group );
    report["cellsDrawn"] = m_cells.size();
    report["replays"] = m_heatmap->GetSources().size();
    report["map"] = m_heatmap->GetMap();
    report["scene"] = m_scene;
    report["sceneMatches"] = MatchesScene();
    float hottest = 0.f;
    Vector3 at;
    for( const Cell& cell : m_cells )
    {
        if( cell.Value > hottest )
        {
            hottest = cell.Value;
            at = cell.Centre;
        }
    }
    if( hottest > 0.f )
    {
        report["hottest"] = { { "value", hottest }, { "at", { at.x, at.y, at.z } } };
    }
    return report;
}


std::string HeatmapOverlay::RunCommand( const std::vector<std::string>& InArgs )
{
    const std::string verb = InArgs.empty() ? std::string() : InArgs[0];
    if( verb.empty() || verb == "status" )
    {
        return Describe();
    }
    if( verb == "load" )
    {
        if( InArgs.size() < 2 )
        {
            return "heatmap load <file.json> [layer] [group]";
        }
        std::string error;
        if( !Load( Resolve( InArgs[1] ), error ) )
        {
            return "can't load " + InArgs[1] + ": " + error;
        }
        NetReplayHeatmap::Layer layer;
        if( InArgs.size() > 2 && NetReplayHeatmap::ParseLayer( InArgs[2], layer ) )
        {
            SetLayer( layer );
        }
        if( InArgs.size() > 3 && !SetGroup( InArgs[3] ) )
        {
            return "loaded, but it has no group " + InArgs[3] + "\n" + Describe();
        }
        return Describe();
    }
    if( verb == "build" )
    {
        if( InArgs.size() < 2 )
        {
            return "heatmap build <replay.mreplay|folder> ...";
        }
        std::vector<std::string> paths;
        for( size_t i = 1; i < InArgs.size(); ++i )
        {
            paths.push_back( Resolve( InArgs[i] ) );
        }
        if( !Build( paths ) )
        {
            return "a heatmap is building already";
        }
        return "building a heatmap from " + std::to_string( InArgs.size() - 1 ) + " path(s) on the background lane";
    }
    if( verb == "layer" )
    {
        NetReplayHeatmap::Layer layer;
        if( InArgs.size() < 2 || !NetReplayHeatmap::ParseLayer( InArgs[1], layer ) )
        {
            return "heatmap layer positions|deaths|kills";
        }
        SetLayer( layer );
        return Describe();
    }
    if( verb == "group" )
    {
        if( InArgs.size() < 2 || !SetGroup( InArgs[1] ) )
        {
            return "heatmap group <name>: one of the heatmap's groups (heatmap groups)";
        }
        return Describe();
    }
    if( verb == "groups" )
    {
        std::string text;
        for( const std::string& group : m_heatmap ? m_heatmap->GetGroups() : std::vector<std::string>() )
        {
            text += ( text.empty() ? "" : " " ) + group;
        }
        return text.empty() ? "no heatmap loaded" : text;
    }
    if( verb == "on" || verb == "off" )
    {
        SetShown( verb == "on" );
        return Describe();
    }
    if( verb == "clear" )
    {
        Clear();
        return "heatmap cleared";
    }
    if( verb == "cancel" )
    {
        return Cancel() ? "cancelling the heatmap build" : "no heatmap is building";
    }
    if( verb == "opacity" )
    {
        if( InArgs.size() < 2 )
        {
            return "heatmap opacity <0..1>";
        }
        SetOpacity( static_cast<float>( std::atof( InArgs[1].c_str() ) ) );
        return "opacity " + Fixed( m_opacity, 2 );
    }
    return "heatmap [load <file.json> [layer] [group]|build <replay|folder> ...|layer positions|deaths|kills|group <name>|groups|on|off|clear|cancel|opacity <0..1>]";
}
