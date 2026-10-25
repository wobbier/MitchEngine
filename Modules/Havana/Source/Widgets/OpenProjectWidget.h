#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <string>
#include <vector>

// File > Open Project (SHIP-4, Engine/Docs/Projects.md): browse to a project folder (one holding
// Project/Game.meproj) and reopen the editor on it. Switching projects starts a fresh editor
// (`Havana --project <folder>`), so nothing of this project (assets, scripts, caches) carries over.
class OpenProjectWidget
    : public HavanaWidget
{
public:
    explicit OpenProjectWidget( class EditorApp* InApp );

    void Init() override {}
    void Destroy() override {}
    void Update() override {}
    void Render() override;

private:
    struct Entry
    {
        std::string Name;
        std::string Path;
        bool IsProject = false;
    };

    void Browse( const std::string& InFolder );

    class EditorApp* m_app = nullptr;
    std::string m_folder;
    std::string m_selected;
    std::vector<Entry> m_entries;
    bool m_browsed = false;
};

#endif
