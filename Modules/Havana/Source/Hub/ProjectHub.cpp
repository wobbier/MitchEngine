#include "Hub/ProjectHub.h"

#if USING( ME_EDITOR )
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/Project.h"
#include "ImGui/ImGuiRenderer.h"
#include "JSON.h"
#include "Renderer.h"
#include "SDL.h"
#include <ImGuiTheme.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    std::string Trim( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        return InText.substr( first, InText.find_last_not_of( " \t\r\n" ) - first + 1 );
    }


    // "command rest of line"
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


    // "a | b"
    std::pair<std::string, std::string> SplitArgs( const std::string& InText )
    {
        const size_t bar = InText.find( '|' );
        if( bar == std::string::npos )
        {
            return { Trim( InText ), {} };
        }
        return { Trim( InText.substr( 0, bar ) ), Trim( InText.substr( bar + 1 ) ) };
    }


    std::string Absolute( const std::string& InPath )
    {
        std::error_code error;
        std::string path = fs::absolute( InPath, error ).lexically_normal().generic_string();
        while( path.size() > 1 && path.back() == '/' )
        {
            path.pop_back();
        }
        return path;
    }


    std::string HomeFolder()
    {
#if USING( ME_PLATFORM_WINDOWS )
        const char* home = SDL_getenv( "USERPROFILE" );
#else
        const char* home = SDL_getenv( "HOME" );
#endif
        return home && *home ? Absolute( home ) : Absolute( "." );
    }


    std::string FormatDate( int64_t InSeconds )
    {
        if( InSeconds <= 0 )
        {
            return {};
        }
        const std::time_t time = static_cast<std::time_t>( InSeconds );
        char text[32] = {};
        if( const std::tm* local = std::localtime( &time ) )
        {
            std::strftime( text, sizeof( text ), "%Y-%m-%d %H:%M", local );
        }
        return text;
    }
}


ProjectHub::ProjectHub( ToolCreationFlags& InFlags )
    : Tool( InFlags )
{
}


std::string ProjectHub::EditorCommand( const std::vector<std::string>& InArgs )
{
#if USING( ME_PLATFORM_WINDOWS )
    std::string command = "\"" + Project::GetExecutableDirectory() + "Havana.exe\"";
#else
    std::string command = "\"" + Project::GetExecutableDirectory() + "Havana\"";
#endif
    for( const std::string& arg : InArgs )
    {
        command += " \"" + arg + "\"";
    }
    return command;
}


void ProjectHub::OnStart()
{
    m_engineRoot = Project::GetEngineRoot();
    const std::string font = m_engineRoot + "/Assets/Fonts/Inter-Regular.ttf";
    std::error_code error;
    if( ImGuiRenderer* imgui = m_renderer->GetImGuiRenderer(); imgui && fs::is_regular_file( font, error ) )
    {
        imgui->SetFont( font, ImGuiRenderer::kMinFontSize );
    }

    m_recent.Load();
    m_templates = ProjectLibrary::ListTemplates( m_engineRoot );
    // New projects go beside the most recent one, else in the home folder.
    m_newLocation = m_recent.GetEntries().empty() ? HomeFolder() : fs::path( m_recent.GetEntries().front().Root ).parent_path().generic_string();
    SuggestName();
    CLog::Log( CLog::LogType::Info, "Project Hub: " + std::to_string( m_recent.GetEntries().size() ) + " recent project(s) (" + m_recent.GetFile() + "), "
        + std::to_string( m_templates.size() ) + " template(s) in " + m_engineRoot + "/Templates" );

    const std::string scriptPath = CommandLine::GetString( "--editor-exec" );
    if( !scriptPath.empty() )
    {
        m_scripted = true;
        m_resultPath = CommandLine::GetString( "--editor-exec-result" );
        std::ifstream script( scriptPath );
        if( !script )
        {
            Fail( "script not found: " + scriptPath );
            Finish();
            return;
        }
        for( std::string line; std::getline( script, line ); )
        {
            m_lines.push_back( line );
        }
        CLog::Log( CLog::LogType::Info, "[editor-exec] Project Hub running " + scriptPath + " (" + std::to_string( m_lines.size() ) + " lines)" );
    }
}


void ProjectHub::OnEnd()
{
    // A script that never opened a project (or failed first) reports here; otherwise the editor does.
    if( m_scripted && !m_finished && ( m_chosen.empty() || m_failures > 0 ) )
    {
        Finish();
    }
}


void ProjectHub::SuggestName()
{
    std::error_code error;
    m_newName = "NewProject";
    for( int i = 1; fs::exists( fs::path( m_newLocation ) / m_newName, error ); ++i )
    {
        m_newName = "NewProject" + std::to_string( i );
    }
}


void ProjectHub::Open( const std::string& InRoot )
{
    m_recent.Add( InRoot );
    m_recent.Save();
    m_chosen = InRoot;
    CLog::Log( CLog::LogType::Info, "Project Hub: opening " + InRoot );
    RequestClose();
}


bool ProjectHub::Create( const ProjectLibrary::Template& InTemplate, const std::string& InParent, const std::string& InName )
{
    const std::string root = ProjectLibrary::CreateProject( InTemplate, InParent, InName, m_createError );
    if( root.empty() )
    {
        CLog::Log( CLog::LogType::Error, "Project Hub: " + m_createError );
        return false;
    }
    m_createError.clear();
    Open( root );
    return true;
}


void ProjectHub::OnUpdate()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->WorkPos );
    ImGui::SetNextWindowSize( viewport->WorkSize );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin( "Project Hub", nullptr, flags );

    ImGui::TextUnformatted( "Project Hub" );
    ImGui::SameLine();
    ImGui::TextDisabled( "MitchEngine at %s", m_engineRoot.c_str() );
    ImGui::Separator();

    if( ImGui::BeginTable( "##Hub", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV ) )
    {
        ImGui::TableSetupColumn( "Projects", ImGuiTableColumnFlags_WidthStretch, 0.58f );
        ImGui::TableSetupColumn( "New", ImGuiTableColumnFlags_WidthStretch, 0.42f );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawProjects();
        ImGui::TableNextColumn();
        DrawNewProject();
        ImGui::EndTable();
    }
    DrawOpenOther();
    DrawLocationPicker();
    ImGui::End();

    TickAutomation();
}


void ProjectHub::DrawProjects()
{
    ImGui::SeparatorText( "Recent Projects" );
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    std::string open;
    std::string remove;
    ImGui::BeginChild( "##Recent", ImVec2( 0.f, -footer ) );
    for( const ProjectLibrary::RecentProject& entry : m_recent.GetEntries() )
    {
        const bool missing = Project::FindProjectRoot( entry.Root ).empty();
        ImGui::PushID( entry.Root.c_str() );
        ImGui::BeginGroup();
        if( ImGui::Selectable( "##Row", entry.Root == m_selectedProject, ImGuiSelectableFlags_AllowDoubleClick, ImVec2( 0.f, ImGui::GetTextLineHeight() * 2.f + ImGui::GetStyle().ItemSpacing.y ) ) )
        {
            m_selectedProject = entry.Root;
            if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !missing )
            {
                open = entry.Root;
            }
        }
        if( ImGui::BeginPopupContextItem() )
        {
            if( ImGui::MenuItem( "Open", nullptr, false, !missing ) ) open = entry.Root;
            if( ImGui::MenuItem( "Remove from List" ) ) remove = entry.Root;
            ImGui::EndPopup();
        }
        ImGui::SameLine( ImGui::GetStyle().ItemSpacing.x );
        ImGui::BeginGroup();
        if( missing )
        {
            ImGui::TextDisabled( "%s (missing)", entry.Name.c_str() );
        }
        else
        {
            ImGui::TextUnformatted( entry.Name.c_str() );
        }
        const std::string date = FormatDate( entry.LastOpened );
        ImGui::TextDisabled( "%s%s%s", entry.Root.c_str(), date.empty() ? "" : "  -  ", date.c_str() );
        ImGui::EndGroup();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if( m_recent.GetEntries().empty() )
    {
        ImGui::TextDisabled( "No recent projects. Create one, or open a project folder." );
    }
    ImGui::EndChild();

    const bool selectedOpens = !m_selectedProject.empty() && !Project::FindProjectRoot( m_selectedProject ).empty();
    ImGui::BeginDisabled( !selectedOpens );
    if( ImGui::Button( "Open" ) )
    {
        open = m_selectedProject;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled( !m_recent.Contains( m_selectedProject ) );
    if( ImGui::Button( "Remove from List" ) )
    {
        remove = m_selectedProject;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if( ImGui::Button( "Open Other..." ) )
    {
        m_openOtherRequested = true;
    }

    if( !remove.empty() )
    {
        m_recent.Remove( remove );
        m_recent.Save();
        m_selectedProject.clear();
    }
    if( !open.empty() )
    {
        Open( open );
    }
}


void ProjectHub::DrawNewProject()
{
    ImGui::SeparatorText( "New Project" );
    if( m_templates.empty() )
    {
        ImGui::TextDisabled( "No templates in %s/Templates", m_engineRoot.c_str() );
        return;
    }
    m_selectedTemplate = std::min( m_selectedTemplate, m_templates.size() - 1 );
    ImGui::BeginChild( "##Templates", ImVec2( 0.f, ImGui::GetContentRegionAvail().y * 0.5f ), ImGuiChildFlags_Border );
    for( size_t i = 0; i < m_templates.size(); ++i )
    {
        const ProjectLibrary::Template& entry = m_templates[i];
        ImGui::PushID( entry.Id.c_str() );
        if( ImGui::Selectable( entry.Name.c_str(), i == m_selectedTemplate ) )
        {
            m_selectedTemplate = i;
        }
        if( !entry.Description.empty() )
        {
            ImGui::Indent();
            ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
            ImGui::TextWrapped( "%s", entry.Description.c_str() );
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    const float labelWidth = ImGuiTheme::Em( 5.f );
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted( "Name" );
    ImGui::SameLine( labelWidth );
    ImGui::SetNextItemWidth( -FLT_MIN );
    ImGui::InputText( "##Name", &m_newName );

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted( "Location" );
    ImGui::SameLine( labelWidth );
    const float browseWidth = ImGui::CalcTextSize( "Browse..." ).x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SetNextItemWidth( -( browseWidth + ImGui::GetStyle().ItemSpacing.x ) );
    ImGui::InputText( "##Location", &m_newLocation );
    ImGui::SameLine();
    if( ImGui::Button( "Browse..." ) )
    {
        m_locationRequested = true;
    }

    std::string problem;
    std::error_code error;
    const fs::path target = fs::path( m_newLocation ) / m_newName;
    if( ProjectLibrary::IsValidProjectName( m_newName, problem ) && fs::exists( target, error ) && !( fs::is_directory( target, error ) && fs::is_empty( target, error ) ) )
    {
        problem = target.generic_string() + " already exists";
    }
    if( problem.empty() )
    {
        ImGui::TextDisabled( "Creates %s", target.generic_string().c_str() );
    }
    else
    {
        ImGui::TextColored( ImGuiTheme::WarningText(), "%s", problem.c_str() );
    }
    if( !m_createError.empty() )
    {
        ImGui::TextColored( ImGuiTheme::ErrorText(), "%s", m_createError.c_str() );
    }
    ImGui::BeginDisabled( !problem.empty() || m_newLocation.empty() );
    if( ImGui::Button( "Create" ) )
    {
        Create( m_templates[m_selectedTemplate], m_newLocation, m_newName );
    }
    ImGui::EndDisabled();
}


void ProjectHub::DrawOpenOther()
{
    if( m_openOtherRequested )
    {
        m_openOtherRequested = false;
        m_openBrowser.Browse( m_newLocation );
        ImGui::OpenPopup( "Open Project" );
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize( ImVec2( viewport->WorkSize.x * 0.7f, viewport->WorkSize.y * 0.7f ), ImGuiCond_Appearing );
    ImGui::SetNextWindowPos( viewport->GetCenter(), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    if( !ImGui::BeginPopupModal( "Open Project" ) )
    {
        return;
    }
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    std::string open = m_openBrowser.Draw( ImVec2( 0.f, -footer ) );
    const std::string candidate = m_openBrowser.GetCandidate();
    ImGui::BeginDisabled( candidate.empty() );
    if( ImGui::Button( "Open" ) )
    {
        open = candidate;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if( ImGui::Button( "Cancel" ) )
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if( candidate.empty() )
    {
        ImGui::TextDisabled( "Pick a project: a folder with %s", Project::kProjectFile );
    }
    else
    {
        ImGui::TextDisabled( "%s", candidate.c_str() );
    }
    if( !open.empty() )
    {
        ImGui::CloseCurrentPopup();
        Open( open );
    }
    ImGui::EndPopup();
}


void ProjectHub::DrawLocationPicker()
{
    if( m_locationRequested )
    {
        m_locationRequested = false;
        m_locationBrowser.Browse( m_newLocation.empty() ? HomeFolder() : m_newLocation );
        ImGui::OpenPopup( "Project Location" );
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize( ImVec2( viewport->WorkSize.x * 0.7f, viewport->WorkSize.y * 0.7f ), ImGuiCond_Appearing );
    ImGui::SetNextWindowPos( viewport->GetCenter(), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    if( !ImGui::BeginPopupModal( "Project Location" ) )
    {
        return;
    }
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    m_locationBrowser.Draw( ImVec2( 0.f, -footer ) );
    if( ImGui::Button( "Use This Folder" ) )
    {
        m_newLocation = m_locationBrowser.GetFolder();
        m_createError.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Cancel" ) )
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


void ProjectHub::TickAutomation()
{
    if( !m_scripted || m_finished || !m_chosen.empty() )
    {
        return;
    }
    if( m_screenshotWait > 0 )
    {
        if( GetScreenshotCount() > m_screenshotBaseline )
        {
            m_screenshotWait = 0;
        }
        else if( --m_screenshotWait == 0 )
        {
            Fail( "hub-screenshot: the renderer never delivered the screenshot" );
        }
        else
        {
            return;
        }
    }
    if( m_waitFrames > 0 )
    {
        --m_waitFrames;
        return;
    }
    while( m_nextLine < m_lines.size() )
    {
        const std::string line = Trim( m_lines[m_nextLine++] );
        if( line.empty() || line[0] == '#' )
        {
            continue;
        }
        const std::string command = SplitWord( line ).first;
        if( command.rfind( "hub-", 0 ) != 0 && command != "wait" )
        {
            // The rest of the script belongs to the editor, which needs a project.
            Fail( "'" + command + "' runs in the editor: open or create a project first (hub-open, hub-create)" );
            Finish();
            RequestClose();
            return;
        }
        Execute( line );
        // Every command waits a frame, so its effects show before the next.
        m_waitFrames = std::max( m_waitFrames, 1 );
        return;
    }
    Finish();
    RequestClose();
}


bool ProjectHub::Execute( const std::string& InLine )
{
    auto [command, args] = SplitWord( InLine );
    if( command == "wait" )
    {
        // wait 10   (frames)
        m_waitFrames = std::max( 0, std::atoi( args.c_str() ) );
    }
    else if( command == "hub-assert-template" )
    {
        // hub-assert-template Empty
        if( !ProjectLibrary::FindTemplate( m_templates, args ) )
        {
            Fail( "no template named " + args + " in " + m_engineRoot + "/Templates" );
        }
    }
    else if( command == "hub-create" )
    {
        // hub-create Empty | NewGame   (New Project > Create: the template, then the new folder)
        auto [templateName, folder] = SplitArgs( args );
        const ProjectLibrary::Template* found = ProjectLibrary::FindTemplate( m_templates, templateName );
        if( !found )
        {
            Fail( "hub-create: no template named " + templateName );
        }
        else if( folder.empty() )
        {
            Fail( "hub-create: name the new project's folder (hub-create <template> | <folder>)" );
        }
        else
        {
            const fs::path target( Absolute( folder ) );
            if( !Create( *found, target.parent_path().generic_string(), target.filename().generic_string() ) )
            {
                Fail( "hub-create: " + m_createError );
            }
        }
    }
    else if( command == "hub-open" )
    {
        // hub-open ../Other   (a recent project, or Open Other...)
        const std::string root = Project::FindProjectRoot( args );
        if( root.empty() ) Fail( "hub-open: " + args + " is not a project" );
        else Open( root );
    }
    else if( command == "hub-add" )
    {
        // hub-add ../Other   (lists a project as if it had been opened)
        m_recent.Add( Absolute( args ) );
        m_recent.Save();
    }
    else if( command == "hub-remove" )
    {
        // hub-remove ../Other   (Remove from List; the folder stays)
        if( !m_recent.Remove( Absolute( args ) ) ) Fail( "hub-remove: " + args + " is not in the recent list" );
        m_recent.Save();
    }
    else if( command == "hub-assert-recent" )
    {
        // hub-assert-recent ../Other | 1   (1 listed, 0 not; the list is re-read from disk)
        auto [folder, expected] = SplitArgs( args );
        ProjectLibrary::RecentProjects saved( m_recent.GetFile() );
        saved.Load();
        const bool want = expected.empty() || expected == "1";
        if( saved.Contains( Absolute( folder ) ) != want )
        {
            Fail( "hub-assert-recent: " + folder + ( want ? " is not" : " is" ) + " in " + m_recent.GetFile() );
        }
    }
    else if( command == "hub-screenshot" )
    {
        // hub-screenshot .tmp/Hub.png
        m_screenshotBaseline = GetScreenshotCount();
        RequestScreenshot( Absolute( args ) );
        m_screenshotWait = 60;
    }
    else
    {
        Fail( "unknown command " + command );
    }
    return true;
}


void ProjectHub::Fail( const std::string& InMessage )
{
    ++m_failures;
    const std::string message = "line " + std::to_string( m_nextLine ) + ": " + InMessage;
    m_failureMessages.push_back( message );
    CLog::Log( CLog::LogType::Error, "[editor-exec] FAIL " + message );
}


void ProjectHub::Finish()
{
    if( m_finished )
    {
        return;
    }
    m_finished = true;
    if( m_failures == 0 )
    {
        CLog::Log( CLog::LogType::Info, "[editor-exec] PASSED" );
    }
    else
    {
        CLog::Log( CLog::LogType::Error, "[editor-exec] FAILED (" + std::to_string( m_failures ) + " failures)" );
    }
    if( !m_resultPath.empty() )
    {
        json result;
        result["Passed"] = m_failures == 0;
        result["Failures"] = m_failureMessages;
        std::error_code error;
        fs::create_directories( fs::path( Absolute( m_resultPath ) ).parent_path(), error );
        std::ofstream( Absolute( m_resultPath ) ) << result.dump( 4 );
    }
    CLog::GetInstance().Flush();
}

#endif
