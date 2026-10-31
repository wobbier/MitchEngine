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
#include "Utils/ScriptTemplates.h"
#include "Mods/Mods.h"
#include "Engine/DevConsole.h"
#include "Core/WorkStatus.h"
#include "Editor/SplineGizmo.h"
#include "Components/Gameplay/Spline.h"
#include "Widgets/SequenceWidget.h"
#include "Widgets/PropertiesWidget.h"
#include "Cores/EditorCore.h"
#include "Widgets/SceneViewWidget.h"
#include "Widgets/SettingsWidgets.h"
#include "Widgets/BuildGameWidget.h"
#include "Engine/ProjectSettings.h"
#include "Utils/EditorConfig.h"
#include "Widgets/ShaderGraph/ShaderGraphAutomation.h"
#include "Widgets/Terrain/TerrainAutomation.h"
#include "Editor/UIAutomation.h"
#include "Editor/RenderTextureAutomation.h"
#include "Widgets/Tree/TreeAutomation.h"
#include "Widgets/ShaderGraph/ShaderGraphPreview.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Transform.h"
#include "Components/Camera.h"
#include "Components/Physics/Ragdoll.h"
#include "Components/Graphics/Model.h"
#include "Core/CommandLine.h"
#include "Cores/SceneCore.h"
#include "Engine/Engine.h"
#include "Engine/PlayerSettings.h"
#include "Engine/HardwareBudget.h"
#include "Components/Graphics/Mesh.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include <cstring>
#include <fstream>
#include "Graphics/SpriteAtlas.h"
#include "Window/SDLWindow.h"
#include "Editor/SpriteRectGizmo.h"
#include <array>
#include <map>
#include "Engine/SaveGames.h"
#include "Engine/LoadingScreen.h"
#include "Engine/Localization.h"
#include "Widgets/UIDesigner/UIDesignerWidget.h"
#include "Editor/TilemapPainter.h"
#include "Engine/LookPresets.h"
#include "Components/Graphics/Tilemap.h"
#include "Cores/UI/RmlUiCore.h"
#include <RmlUi/Core/Context.h>
#include "VFS/VFS.h"
#include "Engine/Input.h"
#include <SDL_keyboard.h>
#include <SDL.h>
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
        json state = EditorOps::CaptureComponent( *entity.Get(), type );
        // Runtime readouts that aren't saved: a mesh's screen-door fade (ME-279), e.g.
        // wait-field Fence | Mesh.DitherFade | != 0.0 ~ 0.5 | 300
        if( type == "Mesh" && state.is_object() )
        {
            if( const Mesh* mesh = entity->TryGetComponent<Mesh>() )
            {
                state["DitherFade"] = mesh->DitherFade;
            }
        }
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
    else if( command.rfind( "inspector-", 0 ) == 0 )
    {
        std::string error;
        if( !PropertiesWidget::Get() || !PropertiesWidget::Get()->ExecuteAutomation( command, args, error ) ) Fail( command + ": " + ( PropertiesWidget::Get() ? error : "no Properties panel" ) );
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
    else if( command == "look-apply" || command == "look-project" )
    {
        // look-apply Toon | materials   the preset's camera / post (and materials) on the open scene
        // look-project PS1              the project's look (its import defaults and cap layer); "" = none
        auto [name, option] = SplitArgs( args );
        LookPresets& looks = LookPresets::Get();
        if( looks.GetAll().empty() )
        {
            looks.Load();
        }
        const LookPresets::Preset* preset = looks.Find( name );
        if( !preset && !( command == "look-project" && name.empty() ) )
        {
            Fail( command + ": no look preset " + name );
        }
        else if( command == "look-project" )
        {
            ProjectSettings::Get().Look = name;
            LookPresets::ApplyProjectDefaults( preset );
        }
        else
        {
            const int changed = LookPresets::ApplyToWorld( *preset, EditorOps::GetWorld(), option == "materials" );
            UndoStack::Get().MarkDirty();
            CLog::Log( CLog::LogType::Info, "Look " + preset->Name + ": " + std::to_string( changed ) + " entities changed" );
        }
    }
    else if( command.rfind( "tile-", 0 ) == 0 )
    {
        // tile-set Ground | 3 -1 | 0          a cell's palette index (-1 erases)
        // tile-fill Ground | 0 0 9 1 | 0      a rectangle of cells
        // tile-paint Ground | 0.5 0.6 | 0     the World view's brush at a fraction of the view (one undo step)
        // tile-assert Ground | 3 -1 | 0       a cell holds that index (-1 = empty)
        // tile-assert-count Ground | 12       how many cells are filled
        auto [name, rest] = SplitArgs( args );
        auto [cells, valueText] = SplitArgs( rest );
        EntityHandle entity = FindEntity( name );
        Tilemap* tilemap = entity ? entity->TryGetComponent<Tilemap>() : nullptr;
        std::istringstream numbers( cells );
        std::vector<float> values;
        for( float value = 0.f; numbers >> value; )
        {
            values.push_back( value );
        }
        const int index = valueText.empty() ? 0 : std::atoi( valueText.c_str() );
        if( !tilemap )
        {
            Fail( command + ": no Tilemap on " + name );
        }
        else if( command == "tile-assert-count" )
        {
            if( tilemap->GetTileCount() != static_cast<size_t>( std::atoi( cells.c_str() ) ) )
            {
                Fail( name + " has " + std::to_string( tilemap->GetTileCount() ) + " tiles, not " + cells );
            }
        }
        else if( command == "tile-set" && values.size() == 2 )
        {
            const json before = EditorOps::CaptureComponent( *entity.Get(), "Tilemap" );
            tilemap->SetTile( static_cast<int>( values[0] ), static_cast<int>( values[1] ), index );
            EditorOps::RecordComponentEdit( *entity.Get(), "Tilemap", before, EditorOps::CaptureComponent( *entity.Get(), "Tilemap" ), "Set tile" );
        }
        else if( command == "tile-fill" && values.size() == 4 )
        {
            const json before = EditorOps::CaptureComponent( *entity.Get(), "Tilemap" );
            tilemap->FillRect( static_cast<int>( values[0] ), static_cast<int>( values[1] ), static_cast<int>( values[2] ), static_cast<int>( values[3] ), index );
            EditorOps::RecordComponentEdit( *entity.Get(), "Tilemap", before, EditorOps::CaptureComponent( *entity.Get(), "Tilemap" ), "Fill tiles" );
        }
        else if( command == "tile-paint" && values.size() == 2 )
        {
            SceneViewWidget* view = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
            if( !view || !TilemapPainter::PaintAt( *view, *entity.Get(), Vector2( values[0] * view->SceneViewRenderSize.x, values[1] * view->SceneViewRenderSize.y ), index ) )
            {
                Fail( "tile-paint: the point misses " + name + "'s plane" );
            }
        }
        else if( command == "tile-assert" && values.size() == 2 )
        {
            const int actual = tilemap->GetTile( static_cast<int>( values[0] ), static_cast<int>( values[1] ) );
            if( actual != index )
            {
                Fail( name + " cell " + cells + " holds " + std::to_string( actual ) + ", not " + valueText );
            }
        }
        else
        {
            Fail( command + ": bad arguments" );
        }
    }
    else if( command.rfind( "rtt-", 0 ) == 0 )
    {
        std::string error;
        bool retry = false;
        if( !RenderTextureAutomation::Execute( command, args, error, retry ) )
        {
            Fail( command + ": " + error );
        }
        else if( retry )
        {
            // Run this line again next frame (waits and GPU read-backs).
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
    else if( command == "ragdoll-build" || command == "ragdoll-clear" || command == "assert-ragdoll-bones" )
    {
        // GAME-5, on the selected entity's Ragdoll: build / clear its parts (one undo step), or
        // assert-ragdoll-bones 3 (how many bones have parts).
        EntityHandle selected = Selection::Get().GetActive();
        Ragdoll* ragdoll = selected ? selected->TryGetComponent<Ragdoll>() : nullptr;
        if( !ragdoll ) Fail( command + ": select an entity with a Ragdoll first" );
        else if( command == "assert-ragdoll-bones" )
        {
            if( static_cast<int>( ragdoll->Bones.size() ) != std::atoi( args.c_str() ) ) Fail( "assert-ragdoll-bones: the ragdoll has " + std::to_string( ragdoll->Bones.size() ) + " bones" );
        }
        else EditorOps::BuildRagdoll( *selected.Get(), command == "ragdoll-clear" );
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
    else if( command == "create-script" )
    {
        // create-script Assets/Scripts/Tests/New | Script, Networked Script   (Create > C# Script's
        // templates, all written at once: one rebuild; each class named like the menu names it)
        auto [folder, list] = SplitArgs( args );
        std::error_code error;
        std::filesystem::create_directories( Path( folder ).FullPath, error );
        std::stringstream labels( list );
        std::string label;
        while( std::getline( labels, label, ',' ) )
        {
            const ScriptTemplates::Template* entry = ScriptTemplates::Find( Trim( label ) );
            if( !entry )
            {
                Fail( "create-script: no template '" + Trim( label ) + "'" );
                continue;
            }
            const std::string name = ScriptNaming::UniqueName( entry->BaseName, []( const std::string& InName ) { return AssetBrowserWidget::IsScriptNameTaken( InName ); } );
            File( Path( folder + "/" + name + ".cs" ) ).Write( ScriptTemplates::Make( *entry, name, ScriptTemplates::ProjectNamespace( Project::GetName() ) ) );
            CLog::Log( CLog::LogType::Info, "[editor-exec] created " + folder + "/" + name + ".cs" );
        }
    }
    else if( command == "copy-file" )
    {
        // copy-file Assets/Textures/A.png | Assets/Textures/Tests/B.png   (under the project; overwrites)
        auto [from, to] = SplitArgs( args );
        std::error_code error;
        std::filesystem::create_directories( std::filesystem::path( Path( to ).FullPath ).parent_path(), error );
        if( !std::filesystem::copy_file( Path( from ).FullPath, Path( to ).FullPath, std::filesystem::copy_options::overwrite_existing, error ) || error )
        {
            Fail( "copy-file: couldn't copy " + from + " to " + to );
        }
    }
    else if( command == "write-file" )
    {
        // write-file Assets/Textures/Tests/X.png.meta | {"Format": "Auto"}   (under the project; overwrites)
        auto [file, text] = SplitArgs( args );
        std::error_code error;
        std::filesystem::create_directories( std::filesystem::path( Path( file ).FullPath ).parent_path(), error );
        File( Path( file ) ).Write( text );
    }
    else if( command == "material-texture" )
    {
        // material-texture Ground | Normal | Assets/Textures/X.png   (as the material inspector assigns it)
        auto [name, rest] = SplitArgs( args );
        auto [slot, file] = SplitArgs( rest );
        EntityHandle entity = FindEntity( name );
        Mesh* mesh = entity ? entity->TryGetComponent<Mesh>() : nullptr;
        int type = -1;
        for( int i = 0; i < Moonlight::TextureType::Count; ++i )
        {
            if( Moonlight::Texture::ToString( static_cast<Moonlight::TextureType>( i ) ) == slot )
            {
                type = i;
            }
        }
        if( !mesh || !mesh->MeshMaterial )
        {
            Fail( "material-texture: " + name + " has no Mesh with a material" );
        }
        else if( type < 0 )
        {
            Fail( "material-texture: no texture slot named " + slot );
        }
        else
        {
            mesh->MeshMaterial->AssignTexture( static_cast<Moonlight::TextureType>( type ), Path( file ) );
        }
    }
    else if( command == "assert-texture-format" )
    {
        // assert-texture-format Assets/Textures/X.png | BC5   (its cooked .dds: BC1, BC3, BC5, BC7,
        // Uncompressed or other; !BC5 for anything but)
        auto [file, expectedText] = SplitArgs( args );
        const bool negate = !expectedText.empty() && expectedText[0] == '!';
        const std::string expected = negate ? expectedText.substr( 1 ) : expectedText;
        std::ifstream dds( Path( file ).FullPath + ".dds", std::ios::binary );
        char header[148] = {};
        dds.read( header, sizeof( header ) );
        std::string format = "missing";
        if( dds.gcount() >= 128 && std::memcmp( header, "DDS ", 4 ) == 0 )
        {
            const std::string fourCC( header + 84, 4 );
            uint32_t dxgi = 0;
            std::memcpy( &dxgi, header + 128, sizeof( dxgi ) );
            if( fourCC == "DXT1" || ( fourCC == "DX10" && ( dxgi == 71 || dxgi == 72 ) ) ) format = "BC1";
            else if( fourCC == "DXT5" || ( fourCC == "DX10" && ( dxgi == 77 || dxgi == 78 ) ) ) format = "BC3";
            else if( fourCC == "ATI2" || fourCC == "BC5U" || ( fourCC == "DX10" && ( dxgi == 83 || dxgi == 84 ) ) ) format = "BC5";
            else if( fourCC == "DX10" && ( dxgi == 98 || dxgi == 99 ) ) format = "BC7";
            else if( fourCC == std::string( 4, '\0' ) || ( fourCC == "DX10" && ( dxgi == 28 || dxgi == 29 || dxgi == 87 || dxgi == 91 ) ) ) format = "Uncompressed";
            else format = "other (" + fourCC + ( fourCC == "DX10" ? " " + std::to_string( dxgi ) : std::string() ) + ")";
        }
        if( ( format == expected ) == negate )
        {
            Fail( file + " is cooked as " + format + ", expected " + expectedText );
        }
    }
    else if( command == "delete-file" )
    {
        // delete-file Assets/Scripts/Tests/New   (a file or folder under the project, e.g. what a flow created)
        // delete-file .tmp/Tests/X | optional     (no failure when it's already gone)
        auto [pathText, option] = SplitArgs( args );
        const std::filesystem::path target = std::filesystem::weakly_canonical( Path( pathText ).FullPath );
        const std::string root = std::filesystem::weakly_canonical( Project::GetRoot() ).generic_string() + "/";
        std::error_code error;
        if( pathText.empty() || target.generic_string().rfind( root, 0 ) != 0 || target.generic_string().size() <= root.size() )
        {
            Fail( "delete-file: " + args + " isn't under the project" );
        }
        else if( option == "optional" && !std::filesystem::exists( target ) )
        {
        }
        else if( std::filesystem::remove_all( target, error ) == 0 || error )
        {
            Fail( "delete-file: couldn't delete " + args );
        }
    }
    else if( command == "asset-trash-window" )
    {
        if( AssetBrowserWidget* browser = InApp.Editor ? InApp.Editor->GetAssetBrowser() : nullptr )
        {
            browser->m_showTrash = true;
        }
    }
    else if( command == "asset-rename" || command == "asset-move" || command == "asset-delete" || command == "asset-restore" )
    {
        // ED-13, through the Asset Browser's own operations (so they land on the undo history):
        // asset-rename Assets/X.txt | Y.txt     asset-move Assets/X.txt | Assets/Folder
        // asset-delete Assets/X.txt (to the Trash)   asset-restore Assets/X.txt (from the Trash)
        AssetBrowserWidget* browser = InApp.Editor ? InApp.Editor->GetAssetBrowser() : nullptr;
        auto [pathText, target] = SplitArgs( args );
        const std::string path = std::filesystem::path( Path( pathText ).FullPath ).lexically_normal().generic_string();
        if( !browser )
        {
            Fail( command + ": no asset browser" );
        }
        else if( command == "asset-rename" )
        {
            browser->m_renamingPath = path;
            std::snprintf( browser->m_renameBuffer, sizeof( browser->m_renameBuffer ), "%s", target.c_str() );
            browser->CommitRename();
        }
        else if( command == "asset-move" )
        {
            browser->m_draggedPaths = { path };
            browser->MoveSelectionTo( std::filesystem::path( Path( target ).FullPath ).lexically_normal().generic_string() );
        }
        else if( command == "asset-delete" )
        {
            browser->TrashPaths( { path } );
        }
        else if( !browser->RestoreFromTrash( path ) )
        {
            Fail( "asset-restore: couldn't restore " + pathText );
        }
    }
    else if( command == "wait-file" )
    {
        // wait-file .tmp/X/Run.png | 1800   (holds until the file exists, up to that many frames)
        auto [pathText, framesText] = SplitArgs( args );
        const int frames = framesText.empty() ? 600 : std::atoi( framesText.c_str() );
        if( std::filesystem::exists( Path( pathText ).FullPath ) )
        {
            m_waitFileFrames = 0;
        }
        else if( ++m_waitFileFrames > frames )
        {
            m_waitFileFrames = 0;
            Fail( "wait-file: no " + pathText + " after " + std::to_string( frames ) + " frames" );
        }
        else
        {
            --m_nextLine;
        }
    }
    else if( command == "assert-file" )
    {
        // assert-file Assets/X.txt | 1   (the file exists, or with 0 doesn't)
        auto [pathText, expectedText] = SplitArgs( args );
        const bool exists = std::filesystem::exists( Path( pathText ).FullPath );
        if( exists != ( expectedText != "0" ) )
        {
            Fail( pathText + ( exists ? " exists" : " doesn't exist" ) );
        }
    }
    else if( command == "project-settings-file" || command == "project-set" || command == "project-assert" )
    {
        // project-settings-file .tmp/Tests/ProjectSettings.json   saves go there, not the project's file
        // project-set Physics.Gravity | [0,-5,0]   a key of the settings file's JSON, then saves
        // project-assert Physics.Gravity | [0,-5,0]
        ProjectSettings& settings = ProjectSettings::Get();
        auto [key, valueText] = SplitArgs( args );
        if( command == "project-settings-file" )
        {
            settings.SetSavePath( Path( args ).FullPath );
        }
        else
        {
            json data = json::parse( settings.Serialize(), nullptr, false );
            std::string pointer = "/" + key;
            std::replace( pointer.begin(), pointer.end(), '.', '/' );
            const json value = json::parse( valueText, nullptr, false );
            if( value.is_discarded() )
            {
                Fail( command + ": not JSON: " + valueText );
            }
            else if( command == "project-set" )
            {
                data[json::json_pointer( pointer )] = value;
                settings.Deserialize( data.dump() );
                settings.Save();
            }
            else if( !data.contains( json::json_pointer( pointer ) ) || data[json::json_pointer( pointer )] != value )
            {
                Fail( "project setting " + key + " is " + ( data.contains( json::json_pointer( pointer ) ) ? data[json::json_pointer( pointer )].dump() : std::string( "missing" ) ) + ", expected " + valueText );
            }
        }
    }
    else if( command == "work-begin" || command == "work-end" || command == "notify" )
    {
        // work-begin Cooking | 0.4   a stand-in background task (ED-14 captures); work-end ends it
        // notify Something went wrong | View.ErrorList   a toast with that action
        static uint64_t s_task = 0;
        auto [text, extra] = SplitArgs( args );
        if( command == "work-begin" )
        {
            s_task = WorkStatus::Get().Begin( text );
            if( !extra.empty() )
            {
                WorkStatus::Get().SetProgress( s_task, static_cast<float>( std::atof( extra.c_str() ) ) );
            }
        }
        else if( command == "work-end" )
        {
            WorkStatus::Get().End( s_task );
        }
        else
        {
            WorkStatus::Get().Notify( WorkStatus::Level::Warning, text, extra, extra.empty() ? std::string() : "Show" );
        }
    }
    else if( command == "assert-work" )
    {
        // assert-work Building scripts | 1   (a background task with that label is running, or not)
        auto [label, expectedText] = SplitArgs( args );
        bool running = false;
        for( const WorkStatus::Task& task : WorkStatus::Get().GetTasks() )
        {
            running |= task.Label.find( label ) != std::string::npos;
        }
        if( running != ( expectedText == "1" || expectedText == "true" ) )
        {
            Fail( std::string( "background task '" ) + label + ( running ? "' is running" : "' isn't running" ) );
        }
    }
    else if( command == "palette-search" || command == "palette-run" )
    {
        // palette-search crate | Asset: Assets/Textures/Sprites/Crate.png   (ED-15: the command
        // palette lists a result whose "Category: Label" contains the text; palette-run runs it)
        auto [filter, expected] = SplitArgs( args );
        const EditorActions::PaletteItem* match = nullptr;
        const std::vector<EditorActions::PaletteItem> items = EditorActions::Get().Search( filter );
        for( const EditorActions::PaletteItem& item : items )
        {
            if( ( item.Category + ": " + item.Label ).find( expected ) != std::string::npos )
            {
                match = &item;
                break;
            }
        }
        if( !match )
        {
            Fail( command + ": nothing for '" + filter + "' reads '" + expected + "' (" + std::to_string( items.size() ) + " results)" );
        }
        else if( command == "palette-run" && match->Run )
        {
            match->Run();
        }
    }
    else if( command == "console" )
    {
        // console greet world   (PLAY-10: runs a developer console line, C++ or C# command)
        DevConsole::Get().Execute( args );
    }
    else if( command == "assert-console-output" )
    {
        // assert-console-output hello world   (the last console line's output contains it)
        if( DevConsole::Get().GetLastOutput().find( args ) == std::string::npos )
        {
            Fail( "console printed '" + DevConsole::Get().GetLastOutput() + "', expected it to contain '" + args + "'" );
        }
    }
    else if( command == "assert-mod-loaded" )
    {
        // assert-mod-loaded com.example.mod | 1   (MOD-6: loaded at start, per the player's choices)
        auto [id, expectedText] = SplitArgs( args );
        const std::vector<std::string>& loaded = ModManager::Get().GetLoadedIds();
        const bool isLoaded = std::find( loaded.begin(), loaded.end(), id ) != loaded.end();
        if( isLoaded != ( expectedText == "1" || expectedText == "true" ) )
        {
            Fail( "mod " + id + ( isLoaded ? " is loaded" : " isn't loaded" ) + ", expected " + expectedText );
        }
    }
    else if( command == "assert-script-type" )
    {
        // assert-script-type NewDataAsset   (a script or data asset class in the loaded scripts)
#if USING( ME_SCRIPTING )
        bool found = false;
        for( int i = 0; i < ScriptEngine::GetScriptCount() && !found; ++i )
        {
            found = ScriptEngine::GetScriptName( i ) == args;
        }
        for( const std::string& type : ScriptEngine::GetDataAssetTypes() )
        {
            found |= type == args;
        }
        if( !found )
        {
            Fail( "no script or data asset class named " + args );
        }
#endif
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
    else if( command.rfind( "designer-", 0 ) == 0 )
    {
        // designer-open UI            the UIDocument on that entity (its .rml source)
        // designer-select #title      an element by selector (it needs an id)
        // designer-set color | #ff3366   an inline style, live and in the source ("" removes)
        // designer-rule #hint | color: #ff3366   a declaration in a stylesheet rule ("color:" removes)
        // designer-insert button      a palette widget as the selection's last child (selects it)
        // designer-delete             removes the selected element
        // designer-pick 300 110       selects what's at a point (context pixels)
        // designer-preview Steam Deck the Game view's display preset
        // designer-assert-selected #title, designer-assert-preview 1280x800
        // designer-assert-computed color | #ff3366   (as RmlUi prints it)
        // designer-save, designer-undo
        UIDesignerWidget* designer = InApp.Editor ? InApp.Editor->GetUIDesigner() : nullptr;
        auto [first, second] = SplitArgs( args );
        std::string error;
        bool ok = designer != nullptr;
        if( !designer ) error = "no designer";
        else if( command == "designer-open" ) ok = designer->Open( args, error );
        else if( command == "designer-select" ) ok = designer->Select( args, error );
        else if( command == "designer-set" ) ok = designer->SetStyle( first, second, error );
        else if( command == "designer-rule" )
        {
            const size_t colon = second.find( ':' );
            ok = colon != std::string::npos;
            error = "usage: designer-rule <selector> | <property>: <value>";
            if( ok ) ok = designer->SetRule( first, Trim( second.substr( 0, colon ) ), Trim( second.substr( colon + 1 ) ), error );
        }
        else if( command == "designer-insert" ) ok = designer->Insert( args, error );
        else if( command == "designer-delete" ) ok = designer->DeleteSelected( error );
        else if( command == "designer-pick" )
        {
            float x = 0.f;
            float y = 0.f;
            ok = std::sscanf( args.c_str(), "%f %f", &x, &y ) == 2;
            error = "usage: designer-pick <x> <y>";
            if( ok ) ok = designer->PickAt( x, y, error );
        }
        else if( command == "designer-preview" ) ok = designer->SetPreview( args, error );
        else if( command == "designer-assert-selected" )
        {
            ok = "#" + designer->GetSelectedId() == args;
            error = "selected #" + designer->GetSelectedId() + ", not " + args;
        }
        else if( command == "designer-assert-computed" )
        {
            const std::string value = designer->GetComputed( first );
            ok = value == second;
            error = first + " is '" + value + "', not '" + second + "'";
        }
        else if( command == "designer-assert-preview" )
        {
            RmlUiCore* core = GetEngine().RmlUi;
            const Rml::Vector2i size = core && core->GetContext() ? core->GetContext()->GetDimensions() : Rml::Vector2i( 0, 0 );
            const std::string actual = std::to_string( size.x ) + "x" + std::to_string( size.y );
            ok = actual == args;
            error = "the UI is " + actual + ", not " + args;
        }
        else if( command == "designer-save" ) ok = designer->Save( error );
        else if( command == "designer-undo" ) { ok = designer->Undo(); error = "nothing to undo"; }
        else { ok = false; error = "unknown command"; }
        if( !ok )
        {
            Fail( command + ": " + error );
        }
    }
    else if( command == "assert-file-contains" )
    {
        // assert-file-contains Assets/UI/X.rml | color: #ff3366     (!= text: must not contain it)
        auto [path, text] = SplitArgs( args );
        const bool negate = text.rfind( "!=", 0 ) == 0;
        if( negate ) text = Trim( text.substr( 2 ) );
        std::string contents;
        if( !VFS::Get().ReadText( path, contents ) )
        {
            Fail( "assert-file-contains: can't read " + path );
        }
        else if( ( contents.find( text ) != std::string::npos ) == negate )
        {
            Fail( path + ( negate ? " still contains '" : " doesn't contain '" ) + text + "'" );
        }
    }
    else if( command == "imgui-key" || command == "imgui-focus" )
    {
        // imgui-focus Tour          focuses an editor window (as a click on it would)
        // imgui-key Tab | Space | Enter | Up | Down | Left | Right | Escape
        //                           one press (down then up) through ImGui's input queue
        if( command == "imgui-focus" )
        {
            ImGui::SetWindowFocus( args.c_str() );
        }
        else
        {
            static const std::map<std::string, ImGuiKey> kKeys = {
                { "Tab", ImGuiKey_Tab }, { "Space", ImGuiKey_Space }, { "Enter", ImGuiKey_Enter },
                { "Up", ImGuiKey_UpArrow }, { "Down", ImGuiKey_DownArrow }, { "Left", ImGuiKey_LeftArrow },
                { "Right", ImGuiKey_RightArrow }, { "Escape", ImGuiKey_Escape },
            };
            const auto key = kKeys.find( args );
            if( key == kKeys.end() )
            {
                Fail( "imgui-key: unknown key " + args );
            }
            else
            {
                ImGui::GetIO().AddKeyEvent( key->second, true );
                ImGui::GetIO().AddKeyEvent( key->second, false );
            }
        }
    }
    else if( command == "assert-window" )
    {
        // assert-window Profiler | true   (whether that editor panel is open)
        auto [name, expectedText] = SplitArgs( args );
        const int open = InApp.Editor ? InApp.Editor->IsWidgetOpen( name ) : -1;
        if( open < 0 )
        {
            Fail( "assert-window: no panel named " + name );
        }
        else if( ( open == 1 ) != ( expectedText == "true" ) )
        {
            Fail( "panel " + name + " is " + ( open == 1 ? "open" : "closed" ) + ", expected " + ( expectedText == "true" ? "open" : "closed" ) );
        }
    }
    else if( command == "build-game" )
    {
        // build-game <profile json> [| no-compile no-record run <game args>]   (the Build Game window
        // builds it as shown: loaded into the window, not saved; with run, Build & Run: a build that
        // succeeds starts through its launcher with the args after it, in its own folder)
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
            const size_t run = options.find( " run" ) != std::string::npos ? options.find( " run" ) + 4 : options.rfind( "run", 0 ) == 0 ? 3 : std::string::npos;
            if( !widget->StartBuild( run != std::string::npos, run != std::string::npos ? Trim( options.substr( run ) ) : std::string() ) )
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
    else if( command == "keep-values" )
    {
        // keep-values Transform   (in Play: the selected entity's component keeps its values when Play stops)
        EntityHandle active = Selection::Get().GetActive();
        if( !active || !active->GetComponentByName( args ) )
        {
            Fail( "keep-values: the selected entity has no " + args );
        }
        else
        {
            EditorOps::SetKeepPlayValues( active->GetGUID(), args, true );
        }
    }
    else if( command == "surface-place" )
    {
        // surface-place 0.5 0.5   (ED-8: the selection onto the surface at that fraction of the World
        // View, as Shift-dragging the move gizmo does; one undo step)
        auto [xText, yText] = SplitWord( args );
        SceneViewWidget* view = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
        std::vector<Entity*> roots = Selection::Get().GetRootEntities();
        std::vector<EditorOps::ComponentEdit> edits;
        for( Entity* root : roots ) edits.push_back( { root->GetGUID(), "Transform", EditorOps::CaptureComponent( *root, "Transform" ), json() } );
        if( !view || roots.empty() || !view->PlaceSelectionOnSurface( Vector2( static_cast<float>( std::atof( xText.c_str() ) ), static_cast<float>( std::atof( yText.c_str() ) ) ) ) )
        {
            Fail( "surface-place: nothing selected, or no surface there" );
        }
        else
        {
            for( EditorOps::ComponentEdit& edit : edits )
            {
                if( EntityHandle entity = EditorOps::GetWorld().FindEntityByGUID( edit.GUID ) ) edit.After = EditorOps::CaptureComponent( *entity.Get(), "Transform" );
            }
            EditorOps::RecordComponentEdits( edits, "Place on Surface" );
        }
    }
    else if( command == "asset-costs" )
    {
        // asset-costs .tmp/costs.json [| 20]   (NEXT-11: what the loaded assets cost, biggest first)
        auto [path, topText] = SplitArgs( args );
        const size_t top = topText.empty() ? SIZE_MAX : static_cast<size_t>( std::atoi( topText.c_str() ) );
        const json report = HardwareBudget::AssetCostsToJson( HardwareBudget::CollectAssetCosts(), top );
        File( Path( path ) ).Write( report.dump( 4 ) );
        CLog::Log( CLog::LogType::Info, "[editor-exec] asset-costs: " + std::to_string( report["Assets"].size() ) + " assets, " + HardwareBudget::FormatBytes( report["Total"].get<uint64_t>() ) + " -> " + path );
    }
    else if( command == "vertex-snap" )
    {
        // vertex-snap [x, y, z]   (ED-8: the selection's pivot onto the mesh vertex nearest that point
        // on screen, as V-dragging the move gizmo does; one undo step)
        const json near = json::parse( args, nullptr, false );
        SceneViewWidget* view = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
        std::vector<Entity*> roots = Selection::Get().GetRootEntities();
        std::vector<EditorOps::ComponentEdit> edits;
        for( Entity* root : roots ) edits.push_back( { root->GetGUID(), "Transform", EditorOps::CaptureComponent( *root, "Transform" ), json() } );
        if( !near.is_array() || near.size() != 3 || !view || roots.empty() || !view->SnapSelectionToVertex( Vector3( near[0].get<float>(), near[1].get<float>(), near[2].get<float>() ) ) )
        {
            Fail( "vertex-snap: nothing selected, or no vertex near " + args );
        }
        else
        {
            for( EditorOps::ComponentEdit& edit : edits )
            {
                if( EntityHandle entity = EditorOps::GetWorld().FindEntityByGUID( edit.GUID ) ) edit.After = EditorOps::CaptureComponent( *entity.Get(), "Transform" );
            }
            EditorOps::RecordComponentEdits( edits, "Snap to Vertex" );
        }
    }
    else if( command == "measure" )
    {
        // measure 0.3 0.5 | 0.7 0.5   (ED-8: the Measure tool between two points of the World View;
        // logs "Measure: <metres> m from [x, y, z] to [x, y, z]")
        auto [fromText, toText] = SplitArgs( args );
        auto [fx, fy] = SplitWord( fromText );
        auto [tx, ty] = SplitWord( toText );
        SceneViewWidget* view = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
        const float length = view ? view->Measure( Vector2( std::stof( fx ), std::stof( fy ) ), Vector2( std::stof( tx ), std::stof( ty ) ) ) : -1.f;
        if( length < 0.f )
        {
            Fail( "measure: no World View" );
        }
        else
        {
            char text[160];
            const Vector3 a = view->GetMeasureFrom();
            const Vector3 b = view->GetMeasureTo();
            std::snprintf( text, sizeof( text ), "Measure: %.3f m from [%.2f, %.2f, %.2f] to [%.2f, %.2f, %.2f]", length, a.x, a.y, a.z, b.x, b.y, b.z );
            CLog::Log( CLog::LogType::Info, text );
        }
    }
    else if( command == "view-option" )
    {
        // view-option DebugDraw | true   (the World View's view option, for this session)
        auto [id, value] = SplitArgs( args );
        SceneViewWidget* view = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
        if( !view || !view->SetViewOption( id, value == "true" || value == "1" ) )
        {
            Fail( "view-option: no World View option " + id );
        }
    }
    else if( command == "assert-scene-camera" )
    {
        // assert-scene-camera [0, 5, -10] ~ 0.01   (the World View camera's position; != [..]: anywhere else)
        std::string valueText = args;
        const bool negate = valueText.rfind( "!=", 0 ) == 0;
        if( negate ) valueText = Trim( valueText.substr( 2 ) );
        double tolerance = 1e-3;
        if( const size_t tilde = valueText.find( " ~ " ); tilde != std::string::npos )
        {
            tolerance = std::atof( valueText.c_str() + tilde + 3 );
            valueText = Trim( valueText.substr( 0, tilde ) );
        }
        const json expected = json::parse( valueText, nullptr, false );
        SharedPtr<Transform> camera = InApp.EditorSceneManager ? InApp.EditorSceneManager->GetEditorCameraTransform() : nullptr;
        if( !camera || !expected.is_array() || expected.size() != 3 )
        {
            Fail( "assert-scene-camera: expected [x, y, z] (and a World View)" );
        }
        else
        {
            const Vector3 position = camera->GetPosition();
            const Vector3 want( expected[0].get<float>(), expected[1].get<float>(), expected[2].get<float>() );
            if( ( ( position - want ).Length() > tolerance ) != negate )
            {
                Fail( std::string( "assert-scene-camera: " ) + ( negate ? "still " : "" ) + "at [" + std::to_string( position.x ) + ", " + std::to_string( position.y ) + ", " + std::to_string( position.z ) + "], expected " + valueText );
            }
        }
    }
    else if( command == "prefab-open" )
    {
        // prefab-open Assets/Crate.prefab   (ED-6: prefab mode on that prefab)
        if( !InApp.OpenPrefab( args ) ) Fail( "prefab-open: couldn't open " + args );
    }
    else if( command == "prefab-save" )
    {
        // prefab-save   (writes the prefab open in prefab mode)
        if( !InApp.SavePrefab() ) Fail( "prefab-save: no prefab open" );
    }
    else if( command == "prefab-close" )
    {
        // prefab-close [save]   (back to the scene; unsaved prefab changes are dropped unless "save")
        if( !InApp.IsEditingPrefab() ) Fail( "prefab-close: no prefab open" );
        else InApp.ClosePrefab( args == "save" );
    }
    else if( command == "prefab-variant" )
    {
        // prefab-variant Assets/Crate.prefab | Assets/Red Crate.prefab   (ED-6: a variant of the first)
        auto [base, path] = SplitArgs( args );
        if( !PrefabTools::CreateVariant( base, path ) ) Fail( "prefab-variant: couldn't make " + path + " from " + base );
    }
    else if( command == "assert-prefab-base" )
    {
        // assert-prefab-base Assets/Red Crate.prefab | Assets/Crate.prefab   (or none: not a variant)
        auto [asset, base] = SplitArgs( args );
        const std::string actual = SceneSerializer::GetPrefabBase( asset );
        const std::string expected = base == "none" ? std::string() : SceneSerializer::NormalizePrefabPath( base );
        if( actual != expected ) Fail( "assert-prefab-base: " + asset + " is based on '" + actual + "', expected '" + expected + "'" );
    }
    else if( command == "assert-prefab-mode" )
    {
        // assert-prefab-mode Assets/Crate.prefab   (or 0: not in prefab mode)
        const std::string open = InApp.GetEditingPrefab();
        const bool ok = args == "0" ? open.empty() : open == SceneSerializer::NormalizePrefabPath( args );
        if( !ok ) Fail( "assert-prefab-mode: editing '" + open + "', expected " + args );
    }
    else if( command == "pick-lock" )
    {
        // pick-lock Crate | 1   (the World View can't pick it or its children; 0 unlocks)
        auto [name, value] = SplitArgs( args );
        EntityHandle entity = FindEntity( name );
        entity ? Selection::Get().SetPickLocked( *entity.Get(), value != "0" && value != "false" ) : Fail( "pick-lock: no entity named " + name );
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
    else if( command == "simulate" )
    {
        // simulate   (Play without scripts; stop ends it)
        InApp.Simulate();
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
    else if( command == "assert-action" )
    {
        // assert-action Jump | true   (whether the game input's action reads pressed)
        auto [actionName, expectedText] = SplitArgs( args );
        Input& input = GetEngine().GetInput();
        if( !input.GetActions().HasAction( actionName ) )
        {
            Fail( "assert-action: no action named " + actionName );
        }
        else if( input.GetAction( actionName ).IsPressed() != ( expectedText == "true" ) )
        {
            Fail( "action " + actionName + " is " + ( input.GetAction( actionName ).IsPressed() ? "pressed" : "not pressed" ) + ", expected " + ( expectedText == "true" ? "pressed" : "not pressed" ) );
        }
    }
    else if( command == "data-asset-create" || command == "data-asset-set" || command == "data-asset-assert" )
    {
        // data-asset-create Assets/X.asset | WeaponStats        (an empty one: the class's defaults)
        // data-asset-set Assets/X.asset | Damage | 25           (edits the file, as the inspector saves)
        // data-asset-assert Assets/X.asset | Damage | 25
        auto [path, rest] = SplitArgs( args );
        std::string text;
        json asset = VFS::Get().ReadText( path, text ) ? json::parse( text, nullptr, false ) : json();
        if( command == "data-asset-create" )
        {
            asset = { { "Type", Trim( rest ) }, { "Fields", json::object() } };
            if( !VFS::Get().WriteText( path, asset.dump( 4 ) ) )
            {
                Fail( "data-asset-create: can't write " + path );
            }
        }
        else if( !asset.is_object() || !asset.contains( "Fields" ) || !asset["Fields"].is_object() )
        {
            Fail( command + ": " + path + " isn't a data asset" );
        }
        else
        {
            auto [field, valueText] = SplitArgs( rest );
            const json value = json::parse( valueText, nullptr, false );
            if( value.is_discarded() )
            {
                Fail( command + ": bad value " + valueText );
            }
            else if( command == "data-asset-set" )
            {
                asset["Fields"][field] = value;
                VFS::Get().WriteText( path, asset.dump( 4 ) );
            }
            else if( !asset["Fields"].contains( field ) || !NearlyEqual( asset["Fields"][field], value ) )
            {
                Fail( "data asset " + path + " " + field + " is " + ( asset["Fields"].contains( field ) ? asset["Fields"][field].dump() : std::string( "<unset>" ) ) + ", expected " + valueText );
            }
        }
    }
    else if( command == "loc-assert-missing" || command == "loc-clear-missing" )
    {
        // loc-assert-missing 0   (text looked up with no translation since the language was set)
        Localization& localization = Localization::Get();
        if( command == "loc-clear-missing" )
        {
            localization.ClearMissing();
        }
        else if( static_cast<int>( localization.GetMissing().size() ) != std::atoi( args.c_str() ) )
        {
            std::string list;
            for( const std::string& missing : localization.GetMissing() )
            {
                list += ( list.empty() ? "" : ", " ) + ( "'" + missing + "'" );
            }
            Fail( std::to_string( localization.GetMissing().size() ) + " untranslated in '" + localization.GetLanguage() + "', expected " + args + ": " + list );
        }
    }
    else if( command == "load-scene-async" )
    {
        // load-scene-async Assets/Scenes/X.lvl | 2   (the loading screen stays up at least 2 s)
        auto [scene, holdText] = SplitArgs( args );
        LoadingScreen::Get().MinimumSeconds = holdText.empty() ? 0.f : static_cast<float>( std::atof( holdText.c_str() ) );
        if( !LoadingScreen::Get().LoadSceneAsync( scene ) )
        {
            Fail( "load-scene-async: couldn't start loading " + scene );
        }
    }
    else if( command == "play-splash" )
    {
        // play-splash Assets/UI/Loading/Splash.rml | 1.5   (one splash screen for 1.5 s)
        auto [document, secondsText] = SplitArgs( args );
        ProjectSettings::SplashScreen splash;
        splash.Document = document;
        splash.Seconds = secondsText.empty() ? splash.Seconds : static_cast<float>( std::atof( secondsText.c_str() ) );
        LoadingScreen::Get().PlaySplash( { splash } );
    }
    else if( command == "save-game" || command == "load-game" || command == "delete-save" )
    {
        // save-game slot1 | Name   (snapshots and waits for the write, so the next line can read it)
        // load-game slot1          (queued: wait-log "SaveGame: loaded slot1" before asserting)
        // delete-save slot1        (fine when the slot doesn't exist)
        auto [slot, name] = SplitArgs( args );
        SaveGames& saves = SaveGames::Get();
        if( command == "save-game" )
        {
            if( !saves.Save( slot, name ) || !saves.WaitForWrites() )
            {
                Fail( "save-game: couldn't save slot " + slot );
            }
        }
        else if( command == "load-game" && !saves.Load( slot ) )
        {
            Fail( "load-game: no save in slot " + slot );
        }
        else if( command == "delete-save" && saves.Exists( slot ) && !saves.Delete( slot ) )
        {
            Fail( "delete-save: couldn't delete slot " + slot );
        }
    }
    else if( command == "assert-save" || command == "save-assert" )
    {
        // assert-save slot1 | Name | "Flow save"          a key of the slot's metadata
        // save-assert slot1 | Crate 0 | Transform.Position ~ 0.05
        //     the live entity's field matches what the slot saved for the entity of that name
        auto [slot, rest] = SplitArgs( args );
        json meta;
        json world;
        std::string error;
        if( !SaveGames::ReadSlot( SaveGames::GetSlotDirectory( slot ), meta, world, &error ) )
        {
            Fail( command + ": " + error );
        }
        else if( command == "assert-save" )
        {
            auto [key, valueText] = SplitArgs( rest );
            const json expected = json::parse( valueText, nullptr, false );
            const json actual = meta.contains( key ) ? meta[key] : json();
            if( expected.is_discarded() || !NearlyEqual( actual, expected, 1e-3 ) )
            {
                Fail( "save " + slot + " " + key + " is " + actual.dump() + ", expected " + valueText );
            }
        }
        else
        {
            auto [name, fieldText] = SplitArgs( rest );
            double tolerance = 1e-3;
            if( const size_t tilde = fieldText.find( " ~ " ); tilde != std::string::npos )
            {
                tolerance = std::atof( fieldText.c_str() + tilde + 3 );
                fieldText = Trim( fieldText.substr( 0, tilde ) );
            }
            std::string type;
            json::json_pointer pointer;
            EntityHandle entity = FindEntity( name );
            const json* saved = nullptr;
            if( SplitFieldPath( fieldText, type, pointer ) && world.contains( "Entities" ) )
            {
                for( const json& entry : world["Entities"] )
                {
                    if( entry.value( "Name", std::string() ) != name || !entry.contains( "Components" ) )
                    {
                        continue;
                    }
                    for( const json& component : entry["Components"] )
                    {
                        if( component.value( "Type", std::string() ) == type )
                        {
                            saved = Resolve( component, pointer );
                        }
                    }
                }
            }
            const json live = entity ? EditorOps::CaptureComponent( *entity.Get(), type ) : json();
            const json* actual = entity ? Resolve( live, pointer ) : nullptr;
            if( !saved || !actual )
            {
                Fail( "save-assert: " + std::string( !saved ? "slot " + slot + " has no " : "no live " ) + name + " " + fieldText );
            }
            else if( !NearlyEqual( *actual, *saved, tolerance ) )
            {
                Fail( name + " " + fieldText + " is " + actual->dump() + ", saved " + saved->dump() );
            }
        }
    }
    else if( command == "assert-spline" )
    {
        // assert-spline Path | 0.5 | 1 0 2     the curve's world point at t (within 0.05)
        // assert-spline Path | point 2 | 1 0 2   a control point, local
        // assert-spline Path | length | 6.2      its world length (within 0.05)
        // assert-spline Path | point 1 | != 2 0 0  anywhere else (a drag moved it)
        auto [name, rest] = SplitArgs( args );
        auto [where, expectedRaw] = SplitArgs( rest );
        const bool differs = expectedRaw.rfind( "!=", 0 ) == 0;
        const std::string expectedText = differs ? expectedRaw.substr( 2 ) : expectedRaw;
        EntityHandle entity = FindEntity( name );
        Spline* spline = entity ? entity->TryGetComponent<Spline>() : nullptr;
        std::istringstream numbers( expectedText );
        Vector3 expected;
        numbers >> expected.x >> expected.y >> expected.z;
        if( !spline )
        {
            Fail( "assert-spline: no spline on " + name );
        }
        else if( where == "length" )
        {
            if( std::abs( spline->GetLength() - expected.x ) > 0.05f )
            {
                Fail( "assert-spline: length is " + std::to_string( spline->GetLength() ) );
            }
        }
        else
        {
            const bool control = where.rfind( "point ", 0 ) == 0;
            const size_t index = control ? static_cast<size_t>( std::atoi( where.c_str() + 6 ) ) : 0;
            const Vector3 actual = control ? ( index < spline->Points.size() ? spline->Points[index] : Vector3( 1e9f, 0.f, 0.f ) ) : spline->GetPoint( static_cast<float>( std::atof( where.c_str() ) ) );
            if( ( ( actual - expected ).Length() > 0.05f ) != differs )
            {
                Fail( "assert-spline: " + where + " is " + std::to_string( actual.x ) + " " + std::to_string( actual.y ) + " " + std::to_string( actual.z ) );
            }
        }
    }
    else if( command == "sprite-handle-mouse" || command == "spline-handle-mouse" || command == "scene-mouse-offset" || command == "scene-mouse-down" || command == "scene-mouse-up" )
    {
        // Drives the World view's mouse through SDL as a user would (ImGui reads these events):
        //   sprite-handle-mouse right   the pointer onto a handle of the selected sprite's rect gizmo
        //   scene-mouse-offset 60 0     moves it by that many screen pixels
        //   scene-mouse-down / scene-mouse-up   the left button
        SDLWindow* window = static_cast<SDLWindow*>( GetEngine().GetWindow() );
        SDL_Window* handle = window ? window->WindowHandle : nullptr;
        static Vector2 s_pointer;
        if( command == "spline-handle-mouse" )
        {
            // spline-handle-mouse 2   the pointer onto point 2 of the selected spline
            SceneViewWidget* sceneView = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
            if( !sceneView || !SplineGizmo::GetHandleScreenPosition( *sceneView, std::atoi( args.c_str() ), s_pointer ) )
            {
                Fail( "spline-handle-mouse: no selected spline, or no point " + args );
            }
        }
        else if( command == "sprite-handle-mouse" )
        {
            SceneViewWidget* sceneView = InApp.Editor ? InApp.Editor->GetSceneView() : nullptr;
            const int which = SpriteRectGizmo::HandleFromName( args );
            if( !sceneView || which < 0 || !SpriteRectGizmo::GetHandleScreenPosition( *sceneView, which, s_pointer ) )
            {
                Fail( "sprite-handle-mouse: no selected sprite, or no handle named " + args );
            }
        }
        else if( command == "scene-mouse-offset" )
        {
            auto [xText, yText] = SplitWord( args );
            s_pointer = s_pointer + Vector2( static_cast<float>( std::atof( xText.c_str() ) ), static_cast<float>( std::atof( yText.c_str() ) ) );
        }
        if( handle )
        {
            // ImGui's coordinates are the desktop's with viewports on, else the main window's.
            int windowX = 0;
            int windowY = 0;
            const bool viewports = ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) != 0;
            if( viewports )
            {
                SDL_GetWindowPosition( handle, &windowX, &windowY );
            }
            const int localX = static_cast<int>( std::lround( s_pointer.x ) ) - windowX;
            const int localY = static_cast<int>( std::lround( s_pointer.y ) ) - windowY;
            SDL_WarpMouseInWindow( handle, localX, localY );
            SDL_Event event{};
            if( command == "scene-mouse-down" || command == "scene-mouse-up" )
            {
                event.type = command == "scene-mouse-down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.windowID = SDL_GetWindowID( handle );
                event.button.button = SDL_BUTTON_LEFT;
                event.button.state = command == "scene-mouse-down" ? SDL_PRESSED : SDL_RELEASED;
                event.button.clicks = 1;
                event.button.x = localX;
                event.button.y = localY;
            }
            else
            {
                event.type = SDL_MOUSEMOTION;
                event.motion.windowID = SDL_GetWindowID( handle );
                event.motion.x = localX;
                event.motion.y = localY;
            }
            SDL_PushEvent( &event );
        }
    }
    else if( command == "atlas-pack" )
    {
        // atlas-pack Assets/Textures/Sprites/Physics2D.spriteatlas | 10   (the inspector's Pack;
        // fails unless that many sprites were packed)
        auto [atlas, countText] = SplitArgs( args );
        std::string error;
        const int count = Moonlight::PackSpriteAtlasFile( atlas, error );
        if( count < 0 )
        {
            Fail( "atlas-pack: " + error );
        }
        else if( !countText.empty() && count != std::atoi( countText.c_str() ) )
        {
            Fail( "atlas-pack: packed " + std::to_string( count ) + " sprite(s), expected " + countText );
        }
    }
    else if( command == "budget-profile" )
    {
        // budget-profile Steam Deck: the hardware profile whose budget lines are measured ("": none).
        // Measuring only: the profile isn't enforced.
        HardwareBudget::Get().SetEnforced( false );
        HardwareBudget::Get().SetActiveProfile( args );
    }
    else if( command == "budget-mark" || command == "budget-assert" )
    {
        // budget-mark                                  remembers every budget line's measure
        // budget-assert Resolution | width | x0.5 ~ 0.02   a ratio of the marked value
        // budget-assert Resolution | width | 960 ~ 2       or a value (fields: used, width, height)
        static std::map<std::string, std::array<double, 3>> s_marks;
        auto measure = []( const HardwareBudget::Line& InLine ) {
            return std::array<double, 3>{ InLine.Used, static_cast<double>( InLine.Width ), static_cast<double>( InLine.Height ) };
        };
        if( command == "budget-mark" )
        {
            for( const HardwareBudget::Line& line : HardwareBudget::Get().GetLines() )
            {
                s_marks[line.Name] = measure( line );
            }
        }
        else
        {
            std::vector<std::string> parts;
            for( size_t start = 0; start <= args.size(); )
            {
                const size_t bar = args.find( '|', start );
                const size_t end = bar == std::string::npos ? args.size() : bar;
                std::string part = args.substr( start, end - start );
                part.erase( 0, part.find_first_not_of( ' ' ) );
                part.erase( part.find_last_not_of( ' ' ) + 1 );
                parts.push_back( part );
                start = end + 1;
            }
            const HardwareBudget::Line* found = nullptr;
            for( const HardwareBudget::Line& line : HardwareBudget::Get().GetLines() )
            {
                found = parts.size() == 3 && parts[0] == line.Name ? &line : found;
            }
            const int field = parts.size() == 3 ? ( parts[1] == "used" ? 0 : parts[1] == "width" ? 1 : parts[1] == "height" ? 2 : -1 ) : -1;
            if( !found || field < 0 )
            {
                Fail( "budget-assert: expected <line> | used|width|height | <value or xRatio> [~ tolerance], got " + args );
            }
            else
            {
                std::string expectedText = parts[2];
                double tolerance = 1e-6;
                if( const size_t tilde = expectedText.find( '~' ); tilde != std::string::npos )
                {
                    tolerance = std::atof( expectedText.c_str() + tilde + 1 );
                    expectedText = expectedText.substr( 0, tilde );
                }
                const double actual = measure( *found )[field];
                double expected = std::atof( expectedText.c_str() + ( expectedText.rfind( 'x', 0 ) == 0 ? 1 : 0 ) );
                bool ok = true;
                if( expectedText.rfind( 'x', 0 ) == 0 )
                {
                    auto mark = s_marks.find( found->Name );
                    ok = mark != s_marks.end() && mark->second[field] > 0.0;
                    // A ratio: compared as one, so the tolerance is relative.
                    const double ratio = ok ? actual / mark->second[field] : 0.0;
                    if( !ok || std::abs( ratio - expected ) > tolerance )
                    {
                        Fail( "budget " + parts[0] + " " + parts[1] + " is " + std::to_string( actual ) + ", " + std::to_string( ratio ) + " of the mark; expected " + parts[2] );
                    }
                }
                else if( std::abs( actual - expected ) > tolerance )
                {
                    Fail( "budget " + parts[0] + " " + parts[1] + " is " + std::to_string( actual ) + ", expected " + parts[2] );
                }
            }
        }
    }
    else if( command.rfind( "settings-", 0 ) == 0 )
    {
        // settings-set Audio.Music | 0.25      any PlayerSettings key, a JSON value
        // settings-assert Audio.Music | 0.25   numbers within 1e-3; Input.Bindings reads the live rebinds
        // settings-save, settings-reset, settings-reload (reads the file again, the way a boot does)
        PlayerSettings& settings = PlayerSettings::Get();
        auto [key, valueText] = SplitArgs( args );
        const json value = json::parse( valueText, nullptr, false );
        std::string error;
        if( command == "settings-set" )
        {
            if( value.is_discarded() || !settings.SetValue( key, value, &error ) )
            {
                Fail( "settings-set: " + ( value.is_discarded() ? "bad value " + valueText : error ) );
            }
        }
        else if( command == "settings-assert" )
        {
            const json actual = settings.GetValue( key );
            if( value.is_discarded() )
            {
                Fail( "settings-assert: bad value " + valueText );
            }
            else if( !NearlyEqual( actual, value, 1e-3 ) )
            {
                Fail( "setting " + key + " is " + actual.dump() + ", expected " + value.dump() );
            }
        }
        else if( command == "settings-save" )
        {
            if( !settings.Save() )
            {
                Fail( "settings-save: the settings are in memory only (run with --user-dir <folder>)" );
            }
        }
        else if( command == "settings-reload" )
        {
            if( !settings.IsPersistent() || !settings.Load() )
            {
                Fail( "settings-reload: " + std::string( settings.IsPersistent() ? "can't read " + settings.GetPath() : "the settings are in memory only (run with --user-dir <folder>)" ) );
            }
        }
        else if( command == "settings-reset" )
        {
            settings.Reset();
        }
        else
        {
            Fail( "Unknown command: " + command );
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
    else if( command == "assert-build-errors" )
    {
        // assert-build-errors 1   (what the Error List shows: the last failed script build's errors)
#if USING( ME_SCRIPTING )
        const size_t errors = ScriptEngine::GetBuildErrorCount();
        if( errors != static_cast<size_t>( std::atoll( args.c_str() ) ) )
        {
            Fail( "build error count " + std::to_string( errors ) + " != " + args );
        }
#else
        CLog::Log( CLog::LogType::Info, "[editor-exec] SKIP line " + std::to_string( m_nextLine ) + ": assert-build-errors (no scripting in this build)" );
#endif
    }
    else if( command == "assert-paused" )
    {
        const bool expected = args == "1" || args == "true";
        if( InApp.IsGamePaused() != expected )
        {
            Fail( std::string( "pause state is " ) + ( InApp.IsGamePaused() ? "paused" : "running" ) );
        }
    }
    else if( command == "pause" )
    {
        // pause (toggles, like the toolbar button)
        InApp.TogglePause();
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
