#include "EditorAutomation.h"

#if USING( ME_EDITOR )

#include "EditorActions.h"
#include "EditorOperations.h"
#include "Engine/Project.h"
#include "PrefabTools.h"
#include "World/SceneSerializer.h"
#include "Selection.h"
#include "Components/Scripting/ScriptComponent.h"
#include "Scripting/ScriptEngine.h"
#include "VFS/Pak.h"
#include "UndoStack.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/SequenceWidget.h"
#include "Widgets/SceneViewWidget.h"
#include "Widgets/SettingsWidgets.h"
#include "Widgets/BuildGameWidget.h"
#include "Engine/ProjectSettings.h"
#include "Utils/EditorConfig.h"
#include "Widgets/ShaderGraph/ShaderGraphAutomation.h"
#include "Widgets/Terrain/TerrainAutomation.h"
#include "Editor/UIAutomation.h"
#include "Widgets/Tree/TreeAutomation.h"
#include "Widgets/ShaderGraph/ShaderGraphPreview.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Graphics/Model.h"
#include "Core/CommandLine.h"
#include "Cores/SceneCore.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include <SDL_keyboard.h>
#include "Engine/World.h"
#include "World/Scene.h"
#include "Renderer.h"
#include "File.h"
#include "CLog.h"
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <sstream>
#include "Profiling/FrameStats.h"
#include <optick.h>

namespace
{
    std::string Trim( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        const size_t last = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( first, last - first + 1 );
    }


    // Splits off the first whitespace-separated word; the rest keeps its spaces (names, JSON).
    std::pair<std::string, std::string> SplitWord( const std::string& InText )
    {
        const std::string text = Trim( InText );
        const size_t space = text.find_first_of( " \t" );
        if( space == std::string::npos )
        {
            return { text, {} };
        }
        return { text.substr( 0, space ), Trim( text.substr( space + 1 ) ) };
    }


    // "Name" finds the first entity with that name; "Parent/Child" walks the hierarchy from the
    // scene root.
    EntityHandle FindEntity( const std::string& InPath )
    {
        if( InPath.find( '/' ) == std::string::npos )
        {
            return EditorOps::GetWorld().FindEntityByName( InPath );
        }
        Transform* current = EditorOps::GetSceneRoot();
        std::stringstream stream( InPath );
        std::string segment;
        while( current && std::getline( stream, segment, '/' ) )
        {
            current = current->GetChildByName( segment );
        }
        return current ? current->Parent : EntityHandle();
    }


    // Two-argument commands: "a | b" when an argument contains spaces, otherwise "a b".
    std::pair<std::string, std::string> SplitArgs( const std::string& InText )
    {
        const size_t bar = InText.find( '|' );
        if( bar != std::string::npos )
        {
            return { Trim( InText.substr( 0, bar ) ), Trim( InText.substr( bar + 1 ) ) };
        }
        return SplitWord( InText );
    }


    bool NearlyEqual( const json& InA, const json& InB, double InTolerance = 1e-3 )
    {
        if( InA.is_number() && InB.is_number() )
        {
            return std::fabs( InA.get<double>() - InB.get<double>() ) <= InTolerance;
        }
        if( InA.is_array() && InB.is_array() )
        {
            if( InA.size() != InB.size() )
            {
                return false;
            }
            for( size_t i = 0; i < InA.size(); ++i )
            {
                if( !NearlyEqual( InA[i], InB[i], InTolerance ) )
                {
                    return false;
                }
            }
            return true;
        }
        return InA == InB;
    }


    // nlohmann 3.6 has no contains( json_pointer ).
    const json* Resolve( const json& InValue, const json::json_pointer& InPointer )
    {
        try
        {
            return &InValue.at( InPointer );
        }
        catch( ... )
        {
            return nullptr;
        }
    }


    // "Transform.Position" -> component "Transform", JSON pointer "/Position".
    bool SplitFieldPath( const std::string& InPath, std::string& OutType, json::json_pointer& OutPointer )
    {
        const size_t dot = InPath.find( '.' );
        if( dot == std::string::npos )
        {
            return false;
        }
        OutType = InPath.substr( 0, dot );
        std::string pointer = "/" + InPath.substr( dot + 1 );
        for( char& c : pointer )
        {
            if( c == '.' )
            {
                c = '/';
            }
        }
        OutPointer = json::json_pointer( pointer );
        return true;
    }


    // Evaluates an assert-field condition ("value", "!= value", "value ~ tol") without failing:
    // wait-field polls it until it holds. OutDetail carries the mismatch for the timeout message.
    bool FieldConditionHolds( const std::string& InEntity, const std::string& InFieldPath, std::string InValueText, std::string& OutDetail )
    {
        const bool negate = InValueText.rfind( "!=", 0 ) == 0;
        if( negate )
        {
            InValueText = Trim( InValueText.substr( 2 ) );
        }
        double tolerance = 1e-3;
        if( const size_t tilde = InValueText.find( " ~ " ); tilde != std::string::npos )
        {
            tolerance = std::atof( InValueText.c_str() + tilde + 3 );
            InValueText = InValueText.substr( 0, tilde );
        }
        EntityHandle entity = FindEntity( InEntity );
        std::string type;
        json::json_pointer pointer;
        const json expected = json::parse( InValueText, nullptr, false );
        if( !entity || !SplitFieldPath( InFieldPath, type, pointer ) || expected.is_discarded() )
        {
            OutDetail = "bad arguments or no entity named " + InEntity;
            return false;
        }
        const json state = EditorOps::CaptureComponent( *entity.Get(), type );
        const json* actual = Resolve( state, pointer );
        if( !actual || NearlyEqual( *actual, expected, tolerance ) == negate )
        {
            OutDetail = InEntity + " " + InFieldPath + " is " + ( actual ? actual->dump() : std::string( "<missing>" ) );
            return false;
        }
        return true;
    }
}


void EditorAutomation::Init()
{
    const std::string scriptPath = CommandLine::GetString( "--editor-exec" );
    if( scriptPath.empty() )
    {
        return;
    }
    File script{ Path( scriptPath ) };
    script.Read();
    if( script.Data.empty() )
    {
        YIKES( "[editor-exec] Script not found or empty: " + scriptPath );
        return;
    }
    std::stringstream stream( script.Data );
    std::string line;
    while( std::getline( stream, line ) )
    {
        m_lines.push_back( line );
    }
    m_resultPath = CommandLine::GetString( "--editor-exec-result" );
    m_isActive = true;
    CLog::Log( CLog::LogType::Info, "[editor-exec] Running " + scriptPath + " (" + std::to_string( m_lines.size() ) + " lines)" );
}


void EditorAutomation::Fail( const std::string& InMessage )
{
    ++m_failures;
    const std::string message = "line " + std::to_string( m_nextLine ) + ": " + InMessage;
    m_failureMessages.push_back( message );
    YIKES( "[editor-exec] FAIL " + message );
}


void EditorAutomation::Tick( EditorApp& InApp )
{
    if( !m_isActive || m_isFinished )
    {
        return;
    }
    if( m_recordProfile )
    {
        const auto& stats = FrameStats::Get();
        const auto& render = stats.GetRenderStats();
        json scopes = json::object();
        for( const auto& scope : stats.GetScopes() ) scopes[scope.Name] = scope.Milliseconds;
        m_profileFrames.push_back( { { "FrameMs", stats.GetFrameMilliseconds() },
            { "GpuMs", render.GpuMilliseconds }, { "RenderThreadMs", render.RenderThreadMilliseconds },
            { "DrawCalls", render.DrawCalls }, { "Triangles", render.Triangles }, { "Scopes", scopes } } );
    }
    if( m_waitFrames > 0 )
    {
        --m_waitFrames;
        return;
    }
    if( m_stableTimeoutFrames > 0 )
    {
        const size_t count = EditorOps::GetWorld().GetEntityCount();
        if( count != m_stableLastCount )
        {
            m_stableLastCount = count;
            m_stableRun = 0;
        }
        else
        {
            ++m_stableRun;
        }
        if( m_stableRun < m_stableFramesNeeded )
        {
            if( --m_stableTimeoutFrames == 0 )
            {
                Fail( "wait-count-stable timed out; entity count still changing at " + std::to_string( count ) );
            }
            else
            {
                return;
            }
        }
        m_stableTimeoutFrames = 0;
    }
    if( m_waitFieldFrames > 0 )
    {
        std::string detail;
        if( FieldConditionHolds( m_waitFieldEntity, m_waitFieldPath, m_waitFieldValue, detail ) )
        {
            m_waitFieldFrames = 0;
        }
        else if( --m_waitFieldFrames > 0 )
        {
            return;
        }
        else
        {
            Fail( "wait-field timed out: " + detail + ", expected " + m_waitFieldValue );
        }
    }
    if( m_waitLogFrames > 0 )
    {
        // Messages logged since the wait-log command, newest last.
        CLog& log = CLog::GetInstance();
        bool found = false;
        {
            std::lock_guard<std::recursive_mutex> lock( log.GetMutex() );
            const uint64_t fresh = std::min<uint64_t>( log.GetTotalMessageCount() - m_waitLogFrom, CLog::Messages.size() );
            for( size_t i = CLog::Messages.size() - fresh; i < CLog::Messages.size() && !found; ++i )
            {
                found = CLog::Messages[i].Message.find( m_waitLogText ) != std::string::npos;
            }
        }
        if( !found && --m_waitLogFrames > 0 )
        {
            return;
        }
        if( !found )
        {
            Fail( "wait-log timed out waiting for '" + m_waitLogText + "'" );
        }
        m_waitLogFrames = 0;
    }

    // Run lines until one asks to wait a frame (every command waits one frame by default so its
    // effects - sync points, deferred destroys, queued events - settle before the next).
    while( m_nextLine < m_lines.size() )
    {
        const std::string line = Trim( m_lines[m_nextLine++] );
        if( line.empty() || line[0] == '#' )
        {
            continue;
        }
        m_previousCommandLogCount = m_commandLogCount;
        m_commandLogCount = CLog::GetInstance().GetTotalMessageCount();
        const bool waitLog = line.rfind( "wait-log", 0 ) == 0;
        const bool keepGoing = Execute( InApp, line );
        m_lastWasWaitLog = waitLog;
        if( !keepGoing )
        {
            return;
        }
        m_waitFrames = std::max( m_waitFrames, 1 );
        return;
    }
    Finish();
}


bool EditorAutomation::Execute( EditorApp& InApp, const std::string& InLine )
{
    auto [command, args] = SplitWord( InLine );
    World& world = EditorOps::GetWorld();

    if( ( command.rfind( "terrain-", 0 ) == 0 || command.rfind( "foliage-", 0 ) == 0 ) )
    {
        std::string error;
        if( !TerrainAutomation::Execute( command, args, error ) ) Fail( command + ": " + error );
    }
    else if( command.rfind( "ui-", 0 ) == 0 || command == "input-press" )
    {
        std::string error;
        if( !UIAutomation::Execute( command, args, error ) ) Fail( command + ": " + error );
    }
    else if( command.rfind( "tree-", 0 ) == 0 )
    {
        std::string error;
        bool retry = false;
        if( !TreeAutomation::Execute( command, args, error, retry ) ) Fail( command + ": " + error );
        else if( retry ) --m_nextLine;
    }
    else if( command.rfind( "seq-", 0 ) == 0 )
    {
        std::string error;
        if( !SequenceWidget::Get() || !SequenceWidget::Get()->ExecuteAutomation( command, args, error ) ) Fail( command + ": " + error );
    }
    else if( command.rfind( "sg-", 0 ) == 0 )
    {
        std::string error;
        bool retry = false;
        if( !ShaderGraphAutomation::Execute( command, args, error, retry ) )
        {
            Fail( command + ": " + error );
        }
        else if( retry )
        {
            // Run this line again next frame (sg-wait-* commands).
            --m_nextLine;
        }
    }
    else if( command == "wait" )
    {
        m_waitFrames = std::max( 1, std::atoi( args.c_str() ) );
    }
    else if( command == "model-compact" || command == "model-expose" )
    {
        EntityHandle selected = Selection::Get().GetActive();
        if( !selected ) Fail( command + ": select a model first" );
        else if( command == "model-compact" )
        {
            if( !EditorOps::CompactModel( *selected.Get() ) ) Fail( "model-compact: selected model is not ready or is already compact" );
        }
        else if( !EditorOps::ExposeModelNode( *selected.Get(), args ) ) Fail( "model-expose: no matching model node" );
    }
    else if( command == "show-assets" )
    {
        if( InApp.Editor && InApp.Editor->GetAssetBrowser() )
        {
            InApp.Editor->GetAssetBrowser()->ShowFolder( args );
        }
    }
    else if( command == "pack-assets" )
    {
        // pack-assets .tmp/Pak/L.mpak | Assets/Shaders,Assets/Config,...   (logical roots the VFS
        // serves, game-over-engine baked in; omit the list to pack all of Assets; game runs
        // mount the result with --pak)
        auto [pakPath, rootList] = SplitArgs( args );
        std::vector<std::string> roots;
        std::string remaining = rootList.empty() ? std::string( "Assets" ) : rootList;
        while( !remaining.empty() )
        {
            const size_t comma = remaining.find( ',' );
            roots.push_back( Trim( remaining.substr( 0, comma ) ) );
            remaining = comma == std::string::npos ? std::string() : remaining.substr( comma + 1 );
        }
        if( !Mpak::WriteFromVfs( pakPath, roots ) )
        {
            Fail( "pack-assets: couldn't write " + pakPath );
        }
    }
    else if( command == "wait-field" )
    {
        // wait-field Main Camera | Camera.FieldOfView | != 52 | 600   (assert-field's forms with
        // a frame budget: holds the flow until the condition is true, fails at the timeout —
        // for state that arrives on its own schedule, like a sequence's first animated frame)
        auto [name, rest] = SplitArgs( args );
        auto [fieldPath, valueAndFrames] = SplitArgs( rest );
        auto [valueText, framesText] = SplitArgs( valueAndFrames );
        m_waitFieldEntity = name;
        m_waitFieldPath = fieldPath;
        m_waitFieldValue = valueText;
        m_waitFieldFrames = std::max( 1, std::atoi( framesText.c_str() ) );
    }
    else if( command == "wait-count-stable" )
    {
        // wait-count-stable 90 | 3600   (frames the entity count must hold, frames before giving
        // up). Settles async scene population (streamed models, prefabs) before a mark-count.
        auto [stableText, timeoutText] = SplitArgs( args );
        m_stableFramesNeeded = std::max( 1, std::atoi( stableText.c_str() ) );
        m_stableTimeoutFrames = std::max( m_stableFramesNeeded + 1, std::atoi( timeoutText.c_str() ) );
        m_stableRun = 0;
        m_stableLastCount = world.GetEntityCount();
    }
    else if( command == "wait-log" )
    {
        // wait-log Scripts: hot reloaded | 600   (frames before giving up). Matches messages logged
        // since the previous command started, so "action X" then "wait-log <what X logs>" can't
        // miss a line X logged at once; a run of wait-logs shares that window, so several lines X
        // logged at once can each be waited for.
        auto [text, framesText] = SplitArgs( args );
        m_waitLogText = text;
        m_waitLogFrames = std::max( 1, std::atoi( framesText.c_str() ) );
        if( !m_lastWasWaitLog )
        {
            m_waitLogFrom = m_previousCommandLogCount;
        }
    }
    else if( command == "replace-in-file" )
    {
        // replace-in-file Assets/Scripts/Foo.cs | old text | new text   (drives hot reload tests)
        auto [file, rest] = SplitArgs( args );
        auto [from, to] = SplitArgs( rest );
        Path path( file );
        File source( path );
        std::string contents = source.Read();
        const size_t at = contents.find( from );
        if( at == std::string::npos )
        {
            Fail( "replace-in-file: '" + from + "' not found in " + file );
        }
        else
        {
            contents.replace( at, from.size(), to );
            source.Write( contents );
        }
    }
    else if( command == "show-asset" )
    {
        if( InApp.Editor && InApp.Editor->GetAssetBrowser() )
        {
            InApp.Editor->GetAssetBrowser()->ShowAsset( args );
        }
    }
    else if( command == "assert-browser-layout" || command == "assert-browser-asset" || command == "wait-asset-preview" )
    {
        AssetBrowserWidget* browser = InApp.Editor ? InApp.Editor->GetAssetBrowser() : nullptr;
        if( !browser || !browser->IsOpen ) Fail( "asset browser is not open" );
        else if( command == "assert-browser-layout" )
        {
            if( args != ( browser->m_folderView ? "Folder" : "Project" ) ) Fail( "unexpected asset browser layout" );
        }
        else if( command == "assert-browser-asset" )
        {
            const std::string full = Path( args ).FullPath;
            const bool found = std::any_of( browser->m_entries.begin(), browser->m_entries.end(), [&]( const auto& entry ) { return entry.FullPath == full; } );
            if( !found ) Fail( "asset browser does not list " + args );
        }
        else
        {
            ImVec2 uv0, uv1;
            const auto& preview = browser->m_graphPreview;
            if( AssetBrowserWidget::GetAssetType( browser->m_detailsPath ) != AssetType::ShaderGraph )
            {
                Fail( "select a shader graph before waiting for its preview" );
                m_assetPreviewWaitFrames = 0;
            }
            else if( !browser->m_graphPreviewError.empty() )
            {
                Fail( browser->m_graphPreviewError );
                m_assetPreviewWaitFrames = 0;
            }
            else if( preview && !preview->IsCompiling() && !preview->GetShaderErrors().empty() )
            {
                Fail( preview->GetShaderErrors().front().Message );
                m_assetPreviewWaitFrames = 0;
            }
            else if( !browser->m_showDetails || browser->m_graphPreviewDirty || !preview || preview->IsCompiling() || !preview->GetMain( uv0, uv1 ) )
            {
                if( ++m_assetPreviewWaitFrames < 1800 ) --m_nextLine;
                else
                {
                    Fail( "asset browser preview did not render" );
                    m_assetPreviewWaitFrames = 0;
                }
            }
            else m_assetPreviewWaitFrames = 0;
        }
    }
    else if( command == "focus-window" )
    {
        ImGui::SetWindowFocus( args.c_str() );
    }
    else if( command == "show-window" )
    {
        // show-window Log   (opens an editor panel by its title)
        if( InApp.Editor )
        {
            InApp.Editor->ShowWidget( args );
        }
    }
    else if( command == "build-game" )
    {
        // build-game <profile json> [| no-compile no-record]   (the Build Game window builds it as
        // shown: loaded into the window, not saved)
        const size_t bar = args.find( '|' );
        const std::string profileJson = Trim( bar == std::string::npos ? args : args.substr( 0, bar ) );
        const std::string options = bar == std::string::npos ? std::string() : args.substr( bar + 1 );
        ProjectSettings::BuildProfile profile;
        BuildGameWidget* widget = BuildGameWidget::Get();
        if( !widget )
        {
            Fail( "build-game: no Build Game window" );
        }
        else if( !ProjectSettings::ParseBuildProfile( profileJson, profile ) )
        {
            Fail( "build-game: expected a build profile JSON object" );
        }
        else
        {
            widget->UseProfile( profile, options.find( "no-compile" ) == std::string::npos, options.find( "no-record" ) == std::string::npos );
            if( !widget->StartBuild() )
            {
                Fail( "build-game: a build is already running" );
            }
        }
    }
    else if( command == "build-game-wait" )
    {
        // build-game-wait   (holds until the window's build ends; fails unless it made a build)
        BuildGameWidget* widget = BuildGameWidget::Get();
        std::error_code error;
        if( !widget )
        {
            Fail( "build-game-wait: no Build Game window" );
        }
        else if( widget->IsBuilding() )
        {
            --m_nextLine;
        }
        else if( !widget->LastBuildSucceeded() )
        {
            Fail( "build-game-wait: the build failed (its output is in the Build Game window and .tmp/BuildGame/Staging/<profile>/Build.log)" );
        }
        else if( !std::filesystem::exists( widget->GetOutputDirectory() + "/BuildInfo.json", error ) )
        {
            Fail( "build-game-wait: no BuildInfo.json in " + widget->GetOutputDirectory() );
        }
    }
    else if( command.rfind( "hub-", 0 ) == 0 )
    {
        // The Project Hub ran these before it opened this project (Hub/ProjectHub.cpp); the editor
        // runs the same script from the top and skips them.
    }
    else if( command == "assert-project" )
    {
        // assert-project NewGame   (the open project's name: its Game.meproj ProjectName)
        if( Project::GetName() != args )
        {
            Fail( "assert-project: the open project is " + Project::GetName() + " (" + Project::GetRoot() + "), expected " + args );
        }
    }
    else if( command == "project-hub" )
    {
        // project-hub   (File > Project Hub; an unattended run logs the reopen command)
        InApp.RequestProjectHub();
    }
    else if( command == "open-project" )
    {
        // open-project Engine/Samples/ScriptOnly   (File > Open Project; an unattended run logs the
        // reopen command instead of starting another editor)
        InApp.RequestOpenProject( args );
    }
    else if( command == "preference" )
    {
        // preference Appearance.FontSize | 28   (for this session: unattended runs never save it)
        auto [key, value] = SplitArgs( args );
        const json parsed = json::parse( value, nullptr, false );
        if( key.empty() || parsed.is_discarded() )
        {
            Fail( "preference: expected Key | <json value>" );
        }
        else
        {
            EditorConfig::GetInstance().SetPreference( key, parsed );
            if( key.rfind( "Appearance.", 0 ) == 0 )
            {
                PreferencesWidget::ApplyAppearancePreferences();
            }
        }
    }
    else if( command == "trace-start" )
    {
        OPTICK_START_CAPTURE();
    }
    else if( command == "trace-stop" )
    {
        OPTICK_STOP_CAPTURE();
        if( args.empty() ) Fail( "trace-stop: expected an output trace path" );
        else OPTICK_SAVE_CAPTURE( args.c_str() );
    }
    else if( command == "profile-start" )
    {
        m_profileFrames = json::array();
        m_recordProfile = true;
    }
    else if( command == "profile-stop" )
    {
        m_recordProfile = false;
        if( args.empty() ) Fail( "profile-stop: expected an output JSON path" );
        else File( Path( args ) ).Write( m_profileFrames.dump() );
    }
    else if( command == "log" )
    {
        CLog::Log( CLog::LogType::Info, "[editor-exec] " + args );
    }
    else if( command == "action" )
    {
        if( !EditorActions::Get().Find( args ) )
        {
            Fail( "unknown action " + args );
        }
        else if( !EditorActions::Get().Execute( args ) )
        {
            Fail( "action disabled: " + args );
        }
    }
    else if( command == "undo" )
    {
        UndoStack::Get().Undo();
    }
    else if( command == "redo" )
    {
        UndoStack::Get().Redo();
    }
    else if( command == "select" || command == "select-add" )
    {
        if( command == "select" )
        {
            Selection::Get().Clear();
        }
        std::stringstream names( args );
        std::string name;
        while( std::getline( names, name, ',' ) )
        {
            EntityHandle entity = FindEntity( Trim( name ) );
            if( !entity )
            {
                Fail( "no entity named " + name );
                continue;
            }
            Selection::Get().Add( entity );
        }
    }
    else if( command == "pick" )
    {
        // pick 0.5 0.5   (clicks the scene view at that fraction of its size; GPU picking selects)
        auto [xText, yText] = SplitWord( args );
        SceneViewWidget* sceneView = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
        if( !sceneView )
        {
            Fail( "pick: no scene view" );
        }
        else
        {
            sceneView->RequestClick( Vector2( static_cast<float>( std::atof( xText.c_str() ) ), static_cast<float>( std::atof( yText.c_str() ) ) ) );
        }
    }
    else if( command == "select-none" )
    {
        Selection::Get().Clear();
    }
    else if( command == "create" )
    {
        auto [name, parentName] = SplitArgs( args );
        Transform* parent = nullptr;
        if( !parentName.empty() )
        {
            EntityHandle parentEntity = FindEntity( parentName );
            parent = parentEntity ? parentEntity->TryGetComponent<Transform>() : nullptr;
            if( !parent )
            {
                Fail( "no parent named " + parentName );
            }
        }
        EditorOps::CreateEntity( name, parent );
    }
    else if( command == "rename" )
    {
        EntityHandle active = Selection::Get().GetActive();
        active ? EditorOps::Rename( *active.Get(), args ) : Fail( "rename: nothing selected" );
    }
    else if( command == "reparent" )
    {
        auto [childName, parentName] = SplitArgs( args );
        EntityHandle child = FindEntity( childName );
        EntityHandle parent = parentName == "root" ? EntityHandle() : FindEntity( parentName );
        if( !child || ( parentName != "root" && !parent ) )
        {
            Fail( "reparent: missing entity" );
        }
        else
        {
            EditorOps::Reparent( { child.Get() }, parent ? parent->TryGetComponent<Transform>() : nullptr );
        }
    }
    else if( command == "add-component" || command == "remove-component" )
    {
        EntityHandle active = Selection::Get().GetActive();
        if( !active )
        {
            Fail( command + ": nothing selected" );
        }
        else if( command == "add-component" )
        {
            if( !EditorOps::AddComponent( *active.Get(), args ) )
            {
                Fail( "unknown component " + args );
            }
        }
        else
        {
            EditorOps::RemoveComponent( *active.Get(), args );
        }
    }
    else if( command == "set" )
    {
        // set Transform.Position [1,2,3]   (on the active entity, undoable like an inspector edit)
        auto [fieldPath, valueText] = SplitWord( args );
        EntityHandle active = Selection::Get().GetActive();
        std::string type;
        json::json_pointer pointer;
        json value = json::parse( valueText, nullptr, false );
        if( !active || !SplitFieldPath( fieldPath, type, pointer ) || value.is_discarded() )
        {
            Fail( "set: bad arguments" );
        }
        else
        {
            json before = EditorOps::CaptureComponent( *active.Get(), type );
            json after = before;
            after[pointer] = value;
            if( !EditorOps::ApplyComponent( active->GetGUID(), type, after ) )
            {
                Fail( "set: entity has no " + type );
            }
            else
            {
                EditorOps::RecordComponentEdit( *active.Get(), type, before, EditorOps::CaptureComponent( *active.Get(), type ), "Edit " + type );
            }
        }
    }
    else if( command == "set-ref" )
    {
        // set-ref TwoBoneIK.Tip | Waver/Root/Mid/Tip   (an entity reference field, by the entity's path)
        auto [fieldPath, entityPath] = SplitArgs( args );
        EntityHandle referenced = FindEntity( entityPath );
        if( !referenced )
        {
            Fail( "set-ref: no entity named " + entityPath );
        }
        else
        {
            return Execute( InApp, "set " + fieldPath + " \"" + SceneSerializer::GUIDToString( referenced->GetGUID() ) + "\"" );
        }
    }
    else if( command == "script-set" || command == "script-assert" || command == "script-set-ref" )
    {
#if USING( ME_SCRIPTING )
        // script-set Fancy.Inner | 3        edits the active entity's live script fields by path
        // script-assert Items.1 | 2.5 ~ 0.1 (numbers within 1e-3 by default; != negates)
        // script-set-ref Target | Probe     fills an EntityRef field's Guid with that entity's GUID
        EntityHandle active = Selection::Get().GetActive();
        ScriptComponent* script = active ? active->TryGetComponent<ScriptComponent>() : nullptr;
        int handle = script ? script->GetHandle() : -1;
        if( script && handle < 0 )
        {
            script->Init();   // .NET booted after the scene loaded (first-start builds): create now
            handle = script->GetHandle();
        }
        auto [fieldPath, valueText] = SplitArgs( args );
        json fields = handle >= 0 ? json::parse( ScriptEngine::GetFieldsJson( handle ), nullptr, false ) : json();
        std::string pointerText = "/" + fieldPath;
        for( char& c : pointerText )
        {
            if( c == '.' )
            {
                c = '/';
            }
        }
        if( handle < 0 || fields.is_discarded() || fieldPath.empty() )
        {
            Fail( command + ": select an entity with a live ScriptComponent first" );
        }
        else if( command == "script-assert" )
        {
            const bool negate = valueText.rfind( "!=", 0 ) == 0;
            if( negate )
            {
                valueText = Trim( valueText.substr( 2 ) );
            }
            double tolerance = 1e-3;
            if( const size_t tilde = valueText.find( " ~ " ); tilde != std::string::npos )
            {
                tolerance = std::atof( valueText.c_str() + tilde + 3 );
                valueText = valueText.substr( 0, tilde );
            }
            const json expected = json::parse( valueText, nullptr, false );
            const json* actual = Resolve( fields, json::json_pointer( pointerText ) );
            if( expected.is_discarded() )
            {
                Fail( "script-assert: bad value " + valueText );
            }
            else if( !actual || NearlyEqual( *actual, expected, tolerance ) == negate )
            {
                Fail( "script field " + fieldPath + " is " + ( actual ? actual->dump() : std::string( "<missing>" ) ) + ", expected " + ( negate ? "!= " : "" ) + expected.dump() );
            }
        }
        else
        {
            json value;
            if( command == "script-set-ref" )
            {
                EntityHandle referenced = FindEntity( valueText );
                if( !referenced )
                {
                    Fail( "script-set-ref: no entity named " + valueText );
                    return true;
                }
                // An EntityRef field serializes as { "Guid": "<16 hex>" }.
                pointerText += "/Guid";
                value = SceneSerializer::GUIDToString( referenced->GetGUID() );
            }
            else
            {
                value = json::parse( valueText, nullptr, false );
                if( value.is_discarded() )
                {
                    Fail( "script-set: bad value " + valueText );
                    return true;
                }
            }
            fields[json::json_pointer( pointerText )] = value;
            ScriptEngine::SetFieldsJson( handle, fields.dump() );
        }
#else
        Fail( command + ": this build has no scripting" );
#endif
    }
    else if( command == "assert-near" )
    {
        // assert-near Waver/Root/Mid/Tip | Waver/IKTarget | 0.01   (world positions, within metres)
        auto [first, rest] = SplitArgs( args );
        auto [second, toleranceText] = SplitArgs( rest );
        EntityHandle a = FindEntity( first );
        EntityHandle b = FindEntity( second );
        Transform* aTransform = a ? a->TryGetComponent<Transform>() : nullptr;
        Transform* bTransform = b ? b->TryGetComponent<Transform>() : nullptr;
        const float tolerance = toleranceText.empty() ? 0.01f : static_cast<float>( std::atof( toleranceText.c_str() ) );
        if( !aTransform || !bTransform )
        {
            Fail( "assert-near: missing " + ( aTransform ? second : first ) );
        }
        else
        {
            const float distance = ( aTransform->GetWorldPosition() - bTransform->GetWorldPosition() ).Length();
            if( distance > tolerance )
            {
                Fail( "assert-near: " + first + " is " + std::to_string( distance ) + " m from " + second );
            }
        }
    }
    else if( command == "create-prefab" )
    {
        auto [name, path] = SplitArgs( args );
        EntityHandle entity = FindEntity( name );
        if( !entity || !PrefabTools::CreatePrefab( *entity.Get(), path ) )
        {
            Fail( "create-prefab failed for " + name );
        }
    }
    else if( command == "instantiate" )
    {
        // Undoable instantiation, like dropping the prefab in the hierarchy.
        EntityHandle instance = world.CreateFromPrefab( args, nullptr );
        if( !instance )
        {
            Fail( "instantiate failed: " + args );
        }
        else
        {
            json data = SceneSerializer::SerializeEntities( world, { instance.Get() } );
            instance->MarkForDelete();
            world.Simulate();
            EditorOps::CreateFromData( data, nullptr, "Instantiate Prefab" );
        }
    }
    else if( command == "prefab-apply" || command == "prefab-revert" || command == "prefab-unpack" )
    {
        EntityHandle entity = FindEntity( args );
        if( !entity || !PrefabTools::FindInstanceRoot( *entity.Get() ) )
        {
            Fail( command + ": " + args + " is not a prefab instance" );
        }
        else if( command == "prefab-apply" )
        {
            PrefabTools::ApplyAll( *entity.Get() );
        }
        else if( command == "prefab-revert" )
        {
            PrefabTools::RevertAll( *entity.Get() );
        }
        else
        {
            PrefabTools::Unpack( *entity.Get() );
        }
    }
    else if( command == "assert-prefab" )
    {
        auto [name, asset] = SplitArgs( args );
        EntityHandle entity = FindEntity( name );
        const std::string actual = entity ? PrefabTools::GetPrefabAsset( *entity.Get() ) : std::string( "<missing>" );
        const std::string expected = asset == "none" ? std::string() : SceneSerializer::NormalizePrefabPath( asset );
        if( actual != expected )
        {
            Fail( name + " prefab link is '" + actual + "', expected '" + expected + "'" );
        }
    }
    else if( command == "assert-overridden" )
    {
        // assert-overridden Cube | Transform.Scale | 1
        auto [name, rest] = SplitArgs( args );
        auto [fieldPath, expectedText] = SplitArgs( rest );
        EntityHandle entity = FindEntity( name );
        const size_t dot = fieldPath.find( '.' );
        if( !entity || dot == std::string::npos )
        {
            Fail( "assert-overridden: bad arguments" );
        }
        else
        {
            const bool overridden = PrefabTools::IsFieldOverridden( *entity.Get(), fieldPath.substr( 0, dot ), fieldPath.substr( dot + 1 ) );
            if( overridden != ( expectedText == "1" || expectedText == "true" ) )
            {
                Fail( name + " " + fieldPath + " overridden = " + ( overridden ? "true" : "false" ) );
            }
        }
    }
    else if( command == "play" )
    {
        InApp.Play();
    }
    else if( command == "stop" )
    {
        InApp.Stop();
    }
    else if( command == "input-hold" || command == "input-release" )
    {
        // input-hold Keyboard/W   (the game's actions read the key as held until input-release)
        // input-release Keyboard/W, or input-release alone for every held key
        Input& input = GetEngine().GetInput();
        const std::string prefix = "Keyboard/";
        const SDL_Scancode key = args.rfind( prefix, 0 ) == 0 ? SDL_GetScancodeFromName( args.substr( prefix.size() ).c_str() ) : SDL_SCANCODE_UNKNOWN;
        if( command == "input-release" && args.empty() )
        {
            input.ClearSimulatedKeys();
        }
        else if( key == SDL_SCANCODE_UNKNOWN )
        {
            Fail( command + ": unknown key '" + args + "' (Keyboard/<key name>, as in an input map)" );
        }
        else
        {
            input.SetSimulatedKey( static_cast<KeyCode>( key ), command == "input-hold" );
        }
    }
    else if( command == "load" )
    {
        GetEngine().LoadScene( args );
    }
    else if( command == "save-as" )
    {
        if( Scene* scene = GetEngine().CurrentScene )
        {
            scene->Save( args, EditorOps::GetSceneRoot() );
            UndoStack::Get().MarkSaved();
        }
    }
    else if( command == "screenshot" )
    {
        GetEngine().GetRenderer().RequestScreenshot( args );
        m_waitFrames = 3;
    }
    else if( command == "quit" )
    {
        m_nextLine = m_lines.size();
        Finish();
        return false;
    }
    else if( command == "assert-exists" || command == "assert-missing" )
    {
        const bool exists = FindEntity( args ).IsValid();
        if( exists != ( command == "assert-exists" ) )
        {
            Fail( command + " " + args );
        }
    }
    else if( command == "assert-count" )
    {
        const size_t expected = static_cast<size_t>( std::atoll( args.c_str() ) );
        if( world.GetEntityCount() != expected )
        {
            Fail( "entity count " + std::to_string( world.GetEntityCount() ) + " != " + args );
        }
    }
    else if( command == "mark-count" )
    {
        m_markedCount = world.GetEntityCount();
    }
    else if( command == "assert-count-delta" )
    {
        const long long expected = std::atoll( args.c_str() );
        const long long delta = static_cast<long long>( world.GetEntityCount() ) - static_cast<long long>( m_markedCount );
        if( delta != expected )
        {
            Fail( "entity count changed by " + std::to_string( delta ) + ", expected " + args );
        }
    }
    else if( command == "assert-selected" )
    {
        const size_t expected = static_cast<size_t>( std::atoll( args.c_str() ) );
        if( Selection::Get().Count() != expected )
        {
            Fail( "selected count " + std::to_string( Selection::Get().Count() ) + " != " + args );
        }
    }
    else if( command == "assert-active" )
    {
        EntityHandle active = Selection::Get().GetActive();
        if( !active || active->GetName() != args )
        {
            Fail( "active entity is " + ( active ? active->GetName() : std::string( "<none>" ) ) + ", expected " + args );
        }
    }
    else if( command == "assert-dirty" )
    {
        const bool expected = args == "1" || args == "true";
        if( UndoStack::Get().IsDirty() != expected )
        {
            Fail( "dirty flag is " + std::string( UndoStack::Get().IsDirty() ? "true" : "false" ) );
        }
    }
    else if( command == "assert-playing" )
    {
        const bool expected = args == "1" || args == "true";
        if( InApp.IsGameRunning() != expected )
        {
            Fail( "play state mismatch" );
        }
    }
    else if( command == "assert-parent" )
    {
        auto [childName, parentName] = SplitArgs( args );
        EntityHandle child = FindEntity( childName );
        Transform* transform = child ? child->TryGetComponent<Transform>() : nullptr;
        const std::string actual = !transform ? "<missing>" : ( EditorOps::GetParentGUID( *child.Get() ) == 0 ? "root" : transform->GetParentTransform()->Parent->GetName() );
        if( actual != parentName )
        {
            Fail( childName + " parent is " + actual + ", expected " + parentName );
        }
    }
    else if( command == "assert-children" )
    {
        auto [name, countText] = SplitArgs( args );
        EntityHandle entity = FindEntity( name );
        Transform* transform = entity ? entity->TryGetComponent<Transform>() : nullptr;
        const size_t expected = static_cast<size_t>( std::atoll( countText.c_str() ) );
        if( !transform || transform->GetChildren().size() != expected )
        {
            Fail( name + " child count " + ( transform ? std::to_string( transform->GetChildren().size() ) : std::string( "<missing>" ) ) + " != " + countText );
        }
    }
    else if( command == "assert-audio" )
    {
#if USING( ME_FMOD )
        // assert-audio Name | 1   (the entity's AudioSource voice is playing: 1, or not: 0)
        auto [name, expectedText] = SplitArgs( args );
        EntityHandle entity = FindEntity( name );
        AudioSource* source = entity ? entity->TryGetComponent<AudioSource>() : nullptr;
        const bool expected = std::atoi( expectedText.c_str() ) != 0;
        if( !source || source->IsPlaying() != expected )
        {
            Fail( name + " audio " + ( source ? ( source->IsPlaying() ? "playing" : "stopped" ) : std::string( "<no AudioSource>" ) ) + ", expected " + ( expected ? "playing" : "stopped" ) );
        }
#else
        // Without the FMOD SDK (proprietary, gitignored, absent on CI) nothing ever plays.
        CLog::Log( CLog::LogType::Info, "[editor-exec] SKIP line " + std::to_string( m_nextLine ) + ": assert-audio (no FMOD in this build)" );
#endif
    }
    else if( command == "assert-occlusion" )
    {
#if USING( ME_FMOD )
        // assert-occlusion Name | 0.7 ~ 0.05   (the entity's AudioSource occlusion, within a tolerance; 0.05 by default)
        auto [name, valueText] = SplitArgs( args );
        double tolerance = 0.05;
        if( const size_t tilde = valueText.find( " ~ " ); tilde != std::string::npos )
        {
            tolerance = std::atof( valueText.c_str() + tilde + 3 );
            valueText = valueText.substr( 0, tilde );
        }
        EntityHandle entity = FindEntity( name );
        AudioSource* source = entity ? entity->TryGetComponent<AudioSource>() : nullptr;
        const double expected = std::atof( valueText.c_str() );
        if( !source || std::abs( source->GetOcclusion() - expected ) > tolerance )
        {
            Fail( name + " occlusion " + ( source ? std::to_string( source->GetOcclusion() ) : std::string( "<no AudioSource>" ) ) + ", expected " + valueText );
        }
#else
        CLog::Log( CLog::LogType::Info, "[editor-exec] SKIP line " + std::to_string( m_nextLine ) + ": assert-occlusion (no FMOD in this build)" );
#endif
    }
    else if( command == "assert-sky-time" || command == "assert-sky-animation-time" )
    {
        // Observe the evaluated clock, which deliberately does not change the authored Time field.
        auto [name, valueText] = SplitArgs( args );
        const bool negate = valueText.rfind( "!=", 0 ) == 0;
        if( negate ) valueText = Trim( valueText.substr( 2 ) );
        EntityHandle entity = FindEntity( name );
        Camera* camera = name == "@editor-camera" ? Camera::EditorCamera : ( entity ? entity->TryGetComponent<Camera>() : nullptr );
        const bool viewport = name == "@scene-view";
        const json expected = json::parse( valueText, nullptr, false );
        if( ( !camera && !viewport ) || !expected.is_number() )
        {
            Fail( "assert-sky-time: expected a camera and an hour value" );
        }
        else
        {
            const auto sky = viewport ? GetEngine().EditorCamera.ProceduralSky : camera->GetSkySettings();
            const float actual = command == "assert-sky-time" ? sky.Time : sky.AnimationTime;
            if( NearlyEqual( json( actual ), expected, 1e-3 ) == negate )
            {
                Fail( name + " " + command + " is " + std::to_string( actual ) + ", expected " + ( negate ? "not " : "" ) + valueText );
            }
        }
    }
    else if( command == "assert-field" )
    {
        // assert-field Cube | Transform.Position | [0,1,0]   (or space separated when names have no spaces)
        // assert-field Cube | Transform.Position.1 | 0.5 ~ 0.02   (numbers within a tolerance)
        // assert-field Cube | Transform.Rotation | != [0,0,0]   (anything but that value)
        auto [name, rest] = SplitArgs( args );
        auto [fieldPath, valueText] = SplitArgs( rest );
        const bool negate = valueText.rfind( "!=", 0 ) == 0;
        if( negate )
        {
            valueText = Trim( valueText.substr( 2 ) );
        }
        double tolerance = 1e-3;
        if( const size_t tilde = valueText.find( " ~ " ); tilde != std::string::npos )
        {
            tolerance = std::atof( valueText.c_str() + tilde + 3 );
            valueText = valueText.substr( 0, tilde );
        }
        EntityHandle entity = FindEntity( name );
        std::string type;
        json::json_pointer pointer;
        json expected = json::parse( valueText, nullptr, false );
        if( !entity || !SplitFieldPath( fieldPath, type, pointer ) || expected.is_discarded() )
        {
            Fail( "assert-field: bad arguments" );
        }
        else
        {
            json state = EditorOps::CaptureComponent( *entity.Get(), type );
            const json* actual = Resolve( state, pointer );
            if( !actual || NearlyEqual( *actual, expected, tolerance ) == negate )
            {
                Fail( name + " " + fieldPath + " is " + ( actual ? actual->dump() : std::string( "<missing>" ) ) + ", expected " + ( negate ? "not " : "" ) + valueText );
            }
        }
    }
    else
    {
        Fail( "unknown command " + command );
    }
    return true;
}


void EditorAutomation::Finish()
{
    if( m_isFinished )
    {
        return;
    }
    m_isFinished = true;
    if( m_failures == 0 )
    {
        CLog::Log( CLog::LogType::Info, "[editor-exec] PASSED" );
    }
    else
    {
        YIKES( "[editor-exec] FAILED (" + std::to_string( m_failures ) + " failures)" );
    }
    if( !m_resultPath.empty() )
    {
        json result;
        result["Passed"] = m_failures == 0;
        result["Failures"] = m_failureMessages;
        File( Path( m_resultPath ) ).Write( result.dump( 4 ) );
    }
    CLog::GetInstance().Flush();
    GetEngine().Quit( true );
}

#endif
