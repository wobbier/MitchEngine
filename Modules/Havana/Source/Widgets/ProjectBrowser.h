#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <imgui.h>
#include <string>
#include <vector>

// A folder list that tells project folders (ones holding Project/Game.meproj) apart: File > Open
// Project and the Project Hub's Open and Location pickers (SHIP-4/5, Engine/Docs/Projects.md).
class ProjectBrowser
{
public:
    void Browse( const std::string& InFolder );

    // The folder bar (up, typed path) and the folder list, InListSize big (ImGui child sizing).
    // Double-clicking a folder browses into it; double-clicking a project returns its root.
    std::string Draw( const ImVec2& InListSize );

    // The project the selection names, else the browsed folder when it is a project; empty if none.
    std::string GetCandidate() const;
    const std::string& GetFolder() const { return m_folder; }

private:
    struct Entry
    {
        std::string Name;
        std::string Path;
        bool IsProject = false;
    };

    std::string m_folder;
    std::string m_selected;
    std::vector<Entry> m_entries;
};

#endif
