#include "BuildGameWidget.h"

#if USING( ME_EDITOR )
#include "CLog.h"
#include "JSON.h"
#include "Path.h"
#include "Tools/Build/BuildGame.h"
#include "Utils/PlatformUtils.h"
#include <ImGuiTheme.h>
#include <Utils/HavanaUtils.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include "SDL.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    BuildGameWidget* s_instance = nullptr;
    constexpr size_t kMaxLines = 5000;
    const char* const kConfigurations[] = { "Retail", "Release", "Debug" };


    // The build runs as another invocation of this editor's own executable.
    std::string EditorExecutable()
    {
        std::string base;
        if( char* path = SDL_GetBasePath() )
        {
            base = path;
            SDL_free( path );
        }
#if USING( ME_PLATFORM_WINDOWS )
        return base + "Havana.exe";
#else
        return base + "Havana";
#endif
    }


    std::string Quote( const std::string& InText )
    {
        return "\"" + InText + "\"";
    }


    ImVec4 LineColor( const std::string& InLine )
    {
        if( InLine.find( "[! Error !]" ) != std::string::npos || InLine.find( "error:" ) != std::string::npos || InLine.find( "FAILED" ) != std::string::npos )
        {
            return ImGuiTheme::ErrorText();
        }
        if( InLine.find( "[Warning]" ) != std::string::npos || InLine.find( "warning:" ) != std::string::npos )
        {
            return ImGuiTheme::WarningText();
        }
        if( InLine.find( "Build Game: == " ) != std::string::npos || InLine.find( "Build Game finished" ) != std::string::npos )
        {
            return ImGuiTheme::InfoText();
        }
        return ImGui::GetStyleColorVec4( ImGuiCol_Text );
    }
}


BuildGameWidget::BuildGameWidget()
    : HavanaWidget( "Build Game" )
{
    IsOpen = false;
    s_instance = this;
}


BuildGameWidget::~BuildGameWidget()
{
    if( s_instance == this )
    {
        s_instance = nullptr;
    }
    if( m_thread.joinable() )
    {
        // A build still running when the editor closes finishes on its own; the thread only
        // touches the state it shares.
        if( m_state->Running.load() )
        {
            m_thread.detach();
        }
        else
        {
            m_thread.join();
        }
    }
}


BuildGameWidget* BuildGameWidget::Get()
{
    return s_instance;
}


ProjectSettings::BuildProfile* BuildGameWidget::GetSelected()
{
    std::vector<ProjectSettings::BuildProfile>& profiles = ProjectSettings::Get().BuildProfiles;
    if( profiles.empty() )
    {
        // A project without profiles builds with the defaults; this one is saved once edited
        // and saved.
        profiles.emplace_back();
    }
    m_selected = std::clamp( m_selected, 0, static_cast<int>( profiles.size() ) - 1 );
    return &profiles[m_selected];
}


bool BuildGameWidget::IsBuilding() const
{
    return m_state->Running.load();
}


bool BuildGameWidget::LastBuildSucceeded() const
{
    std::lock_guard<std::mutex> lock( m_state->Mutex );
    return m_state->Finished && m_state->ExitCode == 0;
}


std::string BuildGameWidget::GetOutputDirectory() const
{
    return m_outputDirectory;
}


void BuildGameWidget::UseProfile( const ProjectSettings::BuildProfile& InProfile, bool InCompile, bool InRecord )
{
    std::vector<ProjectSettings::BuildProfile>& profiles = ProjectSettings::Get().BuildProfiles;
    auto found = std::find_if( profiles.begin(), profiles.end(), [&InProfile]( const ProjectSettings::BuildProfile& InEntry ) { return InEntry.Name == InProfile.Name; } );
    if( found == profiles.end() )
    {
        profiles.push_back( InProfile );
        found = profiles.end() - 1;
    }
    else
    {
        *found = InProfile;
    }
    m_selected = static_cast<int>( found - profiles.begin() );
    m_compile = InCompile;
    m_record = InRecord;
    IsOpen = true;
}


void BuildGameWidget::Update()
{
    if( !m_runWhenDone || m_state->Running.load() )
    {
        return;
    }
    bool finished = false;
    int exitCode = 0;
    {
        std::lock_guard<std::mutex> lock( m_state->Mutex );
        finished = m_state->Finished;
        exitCode = m_state->ExitCode;
    }
    if( finished )
    {
        m_runWhenDone = false;
        if( exitCode == 0 )
        {
            RunLastBuild( m_runArgs );
        }
    }
}


bool BuildGameWidget::RunLastBuild( const std::string& InArgs )
{
    std::ifstream infoFile( m_outputDirectory + "/BuildInfo.json" );
    const json info = json::parse( infoFile, nullptr, false );
    const std::string launcher = !info.is_discarded() && info.contains( "Launcher" ) ? info["Launcher"].get<std::string>() : std::string();
    if( launcher.empty() )
    {
        return false;
    }
    CLog::Log( CLog::LogType::Info, "Build Game: running " + m_outputDirectory + "/" + launcher );
    PlatformUtils::RunDetached( Quote( m_outputDirectory + "/" + launcher ) + ( InArgs.empty() ? std::string() : " " + InArgs ) );
    return true;
}


bool BuildGameWidget::StartBuild( bool InRunWhenDone, const std::string& InRunArgs )
{
    namespace fs = std::filesystem;
    if( m_state->Running.load() )
    {
        return false;
    }
    if( m_thread.joinable() )
    {
        m_thread.join();
    }
    const ProjectSettings::BuildProfile profile = *GetSelected();

    // The child reads the profile as shown, so unsaved edits build too.
    std::error_code error;
    const fs::path handoff = fs::absolute( ".tmp/BuildGame/Editor", error );
    fs::create_directories( handoff, error );
    const std::string profileFile = ( handoff / "Profile.json" ).generic_string();
    m_cancelFile = ( handoff / "Cancel" ).generic_string();
    fs::remove( m_cancelFile, error );
    if( !ProjectSettings::WriteBuildProfile( profileFile, profile ) )
    {
        CLog::Log( CLog::LogType::Error, "Build Game: couldn't write " + profileFile );
        return false;
    }
    m_outputDirectory = fs::absolute( BuildGame::OutputDirectory( profile ), error ).lexically_normal().generic_string();

    std::string command = Quote( EditorExecutable() ) + " --build-game --profile " + Quote( profileFile ) + " --cancel-file " + Quote( m_cancelFile ) + " --verbose";
    command += m_compile ? "" : " --no-compile";
    command += m_record ? "" : " --no-record";
#if USING( ME_PLATFORM_WINDOWS )
    // cmd /c drops the first and last quote of a command line that starts with one.
    command = Quote( command );
#endif
    CLog::Log( CLog::LogType::Info, "Build Game: building '" + profile.Name + "' (" + profile.Configuration + ") into " + m_outputDirectory );

    m_state = std::make_shared<BuildState>();
    // Build & Run: only once the build is really under way (an earlier build's result must not run).
    m_runWhenDone = InRunWhenDone;
    m_runArgs = InRunArgs;
    m_state->Running = true;
    m_thread = std::thread( [state = m_state, command, name = profile.Name, output = m_outputDirectory]() {
        const int exitCode = PlatformUtils::RunCommand( command, [&state]( const std::string& InLine ) {
            // Every asset the cook reads logs a line; they bury the build's own output.
            if( InLine.find( "[File IO]" ) != std::string::npos )
            {
                return;
            }
            const size_t header = InLine.find( "Build Game: == " );
            if( header != std::string::npos )
            {
                const std::string step = InLine.substr( header + 15 );
                for( int index = 0; index < static_cast<int>( BuildGame::Step::Count ); ++index )
                {
                    if( step == BuildGame::StepName( static_cast<BuildGame::Step>( index ) ) )
                    {
                        state->Step = index;
                    }
                }
            }
            std::lock_guard<std::mutex> lock( state->Mutex );
            state->Lines.push_back( InLine );
            if( state->Lines.size() > kMaxLines )
            {
                state->Lines.pop_front();
            }
        } );
        {
            std::lock_guard<std::mutex> lock( state->Mutex );
            state->ExitCode = exitCode;
            state->Finished = true;
        }
        if( exitCode == 0 )
        {
            CLog::Log( CLog::LogType::Info, "Build Game: '" + name + "' succeeded -> " + output );
        }
        else
        {
            CLog::Log( CLog::LogType::Error, "Build Game: '" + name + "' FAILED (exit " + std::to_string( exitCode ) + ")" );
        }
        state->Running = false;
    } );
    return true;
}


void BuildGameWidget::Render()
{
    // Build & Run (Havana renders this widget every frame, open or not; it doesn't update it).
    Update();
    if( !IsOpen )
    {
        return;
    }
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 42.f ), ImGuiTheme::Em( 36.f ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
    {
        ImGui::End();
        return;
    }
    DrawProfiles();
    ImGui::Separator();
    DrawBuild();
    ImGui::End();
}


void BuildGameWidget::DrawProfiles()
{
    ProjectSettings& settings = ProjectSettings::Get();
    ProjectSettings::BuildProfile* selected = GetSelected();
    const bool building = m_state->Running.load();

    const float listWidth = ImGuiTheme::Em( 10.f );
    const float height = ImGuiTheme::Em( 15.f );
    ImGui::BeginChild( "Profiles", ImVec2( listWidth, height ), ImGuiChildFlags_Border );
    for( int index = 0; index < static_cast<int>( settings.BuildProfiles.size() ); ++index )
    {
        ImGui::PushID( index );
        if( ImGui::Selectable( settings.BuildProfiles[index].Name.c_str(), index == m_selected ) && !building )
        {
            m_selected = index;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild( "Profile", ImVec2( 0.f, height ) );
    ImGui::BeginDisabled( building );
    DrawProfile( *selected );
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::BeginDisabled( building );
    if( ImGui::Button( "Add" ) )
    {
        ProjectSettings::BuildProfile profile;
        profile.Name = "Profile " + std::to_string( settings.BuildProfiles.size() + 1 );
        settings.BuildProfiles.push_back( profile );
        m_selected = static_cast<int>( settings.BuildProfiles.size() ) - 1;
    }
    ImGui::SameLine();
    if( ImGui::Button( "Duplicate" ) )
    {
        ProjectSettings::BuildProfile copy = *selected;
        copy.Name += " Copy";
        copy.OutputDirectory.clear();
        settings.BuildProfiles.push_back( copy );
        m_selected = static_cast<int>( settings.BuildProfiles.size() ) - 1;
    }
    ImGui::SameLine();
    if( ImGui::Button( "Delete" ) && settings.BuildProfiles.size() > 1 )
    {
        settings.BuildProfiles.erase( settings.BuildProfiles.begin() + m_selected );
        m_selected = std::max( 0, m_selected - 1 );
    }
    ImGui::SameLine();
    if( ImGui::Button( "Save Profiles" ) )
    {
        settings.Save();
    }
    ImGui::SetItemTooltip( "Profiles live in Assets/Config/ProjectSettings.json. A build uses the profile as shown, saved or not." );
    ImGui::EndDisabled();
}


void BuildGameWidget::DrawProfile( ProjectSettings::BuildProfile& InProfile )
{
    HavanaUtils::Label( "Name" );
    ImGui::InputText( "##Name", &InProfile.Name );

    HavanaUtils::Label( "Configuration" );
    if( ImGui::BeginCombo( "##Configuration", InProfile.Configuration.c_str() ) )
    {
        for( const char* configuration : kConfigurations )
        {
            if( ImGui::Selectable( configuration, InProfile.Configuration == configuration ) )
            {
                InProfile.Configuration = configuration;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip( "Retail ships. Release and Debug keep the tools code (automation, cooking)." );

    HavanaUtils::Label( "Output folder" );
    ProjectSettings::BuildProfile defaults = InProfile;
    defaults.OutputDirectory.clear();
    ImGui::InputTextWithHint( "##Output", BuildGame::OutputDirectory( defaults ).c_str(), &InProfile.OutputDirectory );
    ImGui::SetItemTooltip( "Replaced each build. Only an earlier build or an empty folder is ever cleared." );

    HavanaUtils::Label( "Content" );
    ImGui::Checkbox( "One Game.mpak##Pak", &InProfile.Pak );
    ImGui::SetItemTooltip( "Off: the cooked files ship loose." );

    HavanaUtils::Label( "Archive" );
    ImGui::Checkbox( "##Archive", &InProfile.Archive );
    ImGui::SetItemTooltip( "Also packs the build into one .tar.gz (.zip on Windows) next to it." );

    HavanaUtils::Label( "Scenes" );
    if( ImGui::SmallButton( "Add" ) )
    {
        InProfile.Scenes.emplace_back();
    }
    if( InProfile.Scenes.empty() )
    {
        ImGui::SameLine();
        ImGui::TextDisabled( "None: the project's build scenes and startup scene" );
    }
    for( size_t i = 0; i < InProfile.Scenes.size(); )
    {
        ImGui::PushID( static_cast<int>( i ) );
        HavanaUtils::SkipLabel();
        ImGui::SetNextItemWidth( -ImGuiTheme::Em( 2.5f ) );
        ImGui::InputTextWithHint( "##Scene", "Assets/Scenes/Level.lvl", &InProfile.Scenes[i] );
        ImGui::SameLine();
        const bool removed = ImGui::SmallButton( "X" );
        ImGui::PopID();
        if( removed )
        {
            InProfile.Scenes.erase( InProfile.Scenes.begin() + i );
        }
        else
        {
            ++i;
        }
    }
}


void BuildGameWidget::DrawBuild()
{
    const bool building = m_state->Running.load();
    ImGui::BeginDisabled( building );
    ImGui::Checkbox( "Compile the game", &m_compile );
    ImGui::SetItemTooltip( "Off: package the configuration's existing executable." );
    ImGui::SameLine();
    ImGui::Checkbox( "Record scenes", &m_record );
    ImGui::SetItemTooltip( "Plays each scene through the game for a few seconds (a small window opens), so assets only scripts load by name ship too." );
    ImGui::SameLine();
    if( ImGui::Button( "Build" ) )
    {
        StartBuild();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Build & Run" ) )
    {
        StartBuild( true );
    }
    ImGui::SetItemTooltip( "Builds, then starts the game from its build folder if the build succeeds." );
    ImGui::EndDisabled();
    if( building )
    {
        ImGui::SameLine();
        if( ImGui::Button( "Cancel" ) )
        {
            std::ofstream( m_cancelFile ).put( '\n' );
        }
        ImGui::SetItemTooltip( "Stops the build after its current step." );
    }

    int exitCode = 0;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock( m_state->Mutex );
        exitCode = m_state->ExitCode;
        finished = m_state->Finished;
    }
    const int step = m_state->Step.load();
    if( building || finished )
    {
        const int stepCount = static_cast<int>( BuildGame::Step::Count );
        const float fraction = finished && exitCode == 0 ? 1.f : static_cast<float>( std::max( step, 0 ) ) / static_cast<float>( stepCount );
        const std::string label = finished ? ( exitCode == 0 ? std::string( "Done" ) : std::string( "Failed" ) )
            : step >= 0 ? std::string( BuildGame::StepName( static_cast<BuildGame::Step>( step ) ) ) + " (" + std::to_string( step + 1 ) + "/" + std::to_string( stepCount ) + ")"
            : std::string( "Starting" );
        ImGui::ProgressBar( fraction, ImVec2( -1.f, 0.f ), label.c_str() );
    }
    if( finished && !building )
    {
        if( exitCode == 0 )
        {
            ImGui::TextColored( ImGuiTheme::SuccessText(), "Built into %s", m_outputDirectory.c_str() );
            if( ImGui::Button( "Open Folder" ) )
            {
                PlatformUtils::OpenFolder( Path( m_outputDirectory, true ) );
            }
            ImGui::SameLine();
            if( ImGui::Button( "Run" ) )
            {
                RunLastBuild();
            }
        }
        else
        {
            ImGui::TextColored( ImGuiTheme::ErrorText(), "The build failed (exit %d); the full log is in .tmp/BuildGame/Staging/<profile>/Build.log", exitCode );
        }
    }

    ImGui::Checkbox( "Follow output", &m_followOutput );
    ImGui::BeginChild( "Output", ImVec2( 0.f, 0.f ), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar );
    {
        std::lock_guard<std::mutex> lock( m_state->Mutex );
        ImGuiListClipper clipper;
        clipper.Begin( static_cast<int>( m_state->Lines.size() ) );
        while( clipper.Step() )
        {
            for( int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line )
            {
                const std::string& text = m_state->Lines[line];
                ImGui::PushStyleColor( ImGuiCol_Text, LineColor( text ) );
                ImGui::TextUnformatted( text.c_str(), text.c_str() + text.size() );
                ImGui::PopStyleColor();
            }
        }
    }
    if( m_followOutput && building )
    {
        ImGui::SetScrollHereY( 1.f );
    }
    ImGui::EndChild();
}

#endif
