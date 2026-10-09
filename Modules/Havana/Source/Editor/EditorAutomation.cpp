#include "EditorAutomation.h"

#if USING( ME_EDITOR )

#include "EditorActions.h"
#include "EditorOperations.h"
#include "Selection.h"
#include "UndoStack.h"
#include "EditorApp.h"
#include "Components/Transform.h"
#include "Core/CommandLine.h"
#include "Cores/SceneCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/Scene.h"
#include "Renderer.h"
#include "File.h"
#include "CLog.h"
#include <cmath>
#include <sstream>

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


    bool NearlyEqual( const json& InA, const json& InB )
    {
        if( InA.is_number() && InB.is_number() )
        {
            return std::fabs( InA.get<double>() - InB.get<double>() ) <= 1e-3;
        }
        if( InA.is_array() && InB.is_array() )
        {
            if( InA.size() != InB.size() )
            {
                return false;
            }
            for( size_t i = 0; i < InA.size(); ++i )
            {
                if( !NearlyEqual( InA[i], InB[i] ) )
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
    if( m_waitFrames > 0 )
    {
        --m_waitFrames;
        return;
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
        if( !Execute( InApp, line ) )
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

    if( command == "wait" )
    {
        m_waitFrames = std::max( 1, std::atoi( args.c_str() ) );
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
    else if( command == "play" )
    {
        InApp.Play();
    }
    else if( command == "stop" )
    {
        InApp.Stop();
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
    else if( command == "assert-field" )
    {
        // assert-field Cube | Transform.Position | [0,1,0]   (or space separated when names have no spaces)
        auto [name, rest] = SplitArgs( args );
        auto [fieldPath, valueText] = SplitArgs( rest );
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
            if( !actual || !NearlyEqual( *actual, expected ) )
            {
                Fail( name + " " + fieldPath + " is " + ( actual ? actual->dump() : std::string( "<missing>" ) ) + ", expected " + valueText );
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
