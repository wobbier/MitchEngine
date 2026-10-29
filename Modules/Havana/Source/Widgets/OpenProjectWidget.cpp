#include "OpenProjectWidget.h"

#if USING( ME_EDITOR )
#include "EditorApp.h"
#include "Engine/Project.h"
#include <ImGuiTheme.h>
#include <imgui.h>
#include <filesystem>

namespace fs = std::filesystem;


OpenProjectWidget::OpenProjectWidget( EditorApp* InApp )
    : HavanaWidget( "Open Project" )
    , m_app( InApp )
{
    IsOpen = false;
}


void OpenProjectWidget::Render()
{
    if( !IsOpen )
    {
        m_browsed = false;
        return;
    }
    if( !m_browsed )
    {
        // Start beside the open project, where its siblings usually live.
        m_browser.Browse( fs::path( Project::GetRoot() ).parent_path().generic_string() );
        m_browsed = true;
    }
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 34.f ), ImGuiTheme::Em( 26.f ) ), ImGuiCond_FirstUseEver );
    if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled( "Open: %s (%s)", Project::GetName().c_str(), Project::GetRoot().c_str() );
    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    std::string open = m_browser.Draw( ImVec2( 0.f, -footer ) );

    const std::string candidate = m_browser.GetCandidate();
    const bool current = candidate == Project::GetRoot();
    ImGui::BeginDisabled( candidate.empty() || current );
    if( ImGui::Button( "Open" ) )
    {
        open = candidate;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if( candidate.empty() )
    {
        ImGui::TextDisabled( "Pick a project: a folder with %s", Project::kProjectFile );
    }
    else
    {
        ImGui::TextDisabled( current ? "%s is already open" : "Reopens the editor on %s", candidate.c_str() );
    }

    if( !open.empty() && m_app )
    {
        m_app->RequestOpenProject( open );
    }
    ImGui::End();
}

#endif
