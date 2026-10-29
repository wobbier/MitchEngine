#include "ProjectBrowser.h"

#if USING( ME_EDITOR )
#include "Engine/Project.h"
#include <ImGuiTheme.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;


void ProjectBrowser::Browse( const std::string& InFolder )
{
    std::error_code error;
    const fs::path folder = fs::absolute( InFolder, error ).lexically_normal();
    if( !fs::is_directory( folder, error ) )
    {
        return;
    }
    m_folder = folder.generic_string();
    while( m_folder.size() > 1 && m_folder.back() == '/' )
    {
        m_folder.pop_back();
    }
    m_selected.clear();
    m_entries.clear();
    for( fs::directory_iterator it( folder, fs::directory_options::skip_permission_denied, error ), end; !error && it != end; it.increment( error ) )
    {
        const std::string name = it->path().filename().string();
        if( name.empty() || name[0] == '.' || !it->is_directory( error ) )
        {
            continue;
        }
        const std::string path = it->path().generic_string();
        m_entries.push_back( { name, path, !Project::FindProjectRoot( path ).empty() } );
    }
    // Projects first, then by name.
    std::sort( m_entries.begin(), m_entries.end(), []( const Entry& InA, const Entry& InB ) {
        return InA.IsProject != InB.IsProject ? InA.IsProject : InA.Name < InB.Name;
    } );
}


std::string ProjectBrowser::Draw( const ImVec2& InListSize )
{
    if( ImGui::ArrowButton( "##Up", ImGuiDir_Up ) )
    {
        Browse( fs::path( m_folder ).parent_path().generic_string() );
    }
    ImGui::SetItemTooltip( "Parent folder" );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( -FLT_MIN );
    std::string typed = m_folder;
    if( ImGui::InputText( "##Folder", &typed, ImGuiInputTextFlags_EnterReturnsTrue ) )
    {
        Browse( typed );
    }

    std::string open;
    std::string browseTo;
    ImGui::BeginChild( "##Folders", InListSize, ImGuiChildFlags_Border );
    for( const Entry& entry : m_entries )
    {
        ImGui::PushID( entry.Path.c_str() );
        if( entry.IsProject )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, ImGuiTheme::AccentText() );
        }
        const std::string label = entry.IsProject ? entry.Name + "  (project)" : entry.Name + "/";
        if( ImGui::Selectable( label.c_str(), entry.Path == m_selected, ImGuiSelectableFlags_AllowDoubleClick ) )
        {
            m_selected = entry.Path;
            if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
                if( entry.IsProject ) open = entry.Path;
                else browseTo = entry.Path;
            }
        }
        if( entry.IsProject )
        {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    if( m_entries.empty() )
    {
        ImGui::TextDisabled( "No folders here." );
    }
    ImGui::EndChild();
    if( !browseTo.empty() )
    {
        Browse( browseTo );
    }
    return open.empty() ? std::string() : Project::FindProjectRoot( open );
}


std::string ProjectBrowser::GetCandidate() const
{
    return Project::FindProjectRoot( m_selected.empty() ? m_folder : m_selected );
}

#endif
