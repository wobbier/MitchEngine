#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The Project Hub's model (SHIP-5, Engine/Docs/Projects.md): the per-user list of recent projects,
// the engine's project templates, and creating a project from one. Havana's Hub
// (Modules/Havana/Source/Hub/ProjectHub) draws it; the editor adds every project it opens.
namespace ProjectLibrary
{
    // <engine>/Templates/<Id>/: a project folder (Project/Game.meproj, Assets/, ...) plus a
    // Template.json naming and describing it: {"Name": "Empty", "Description": "...", "Order": 0}.
    struct Template
    {
        std::string Id;
        std::string Name;
        std::string Description;
        // Absolute, '/'-separated.
        std::string Directory;
        int Order = 0;
    };

    // The engine's templates, by Order then Name.
    std::vector<Template> ListTemplates( const std::string& InEngineRoot );
    // By Id or Name, case-insensitively; null when there is none.
    const Template* FindTemplate( const std::vector<Template>& InTemplates, const std::string& InIdOrName );

    // A new project's name doubles as its folder name: letters, digits, '-' and '_', starting with
    // a letter or digit, at most 64 characters.
    bool IsValidProjectName( const std::string& InName, std::string& OutError );

    // Copies the template to <InParentFolder>/<InName> (which must not exist, or be empty) and names
    // the project: Game.meproj's ProjectName, the product name and the window title. Template.json
    // and anything derived (.tmp/, .build/, Builds/, bin/, obj/) stay behind. Returns the new
    // project's root, or empty with OutError set (and nothing left behind).
    std::string CreateProject( const Template& InTemplate, const std::string& InParentFolder, const std::string& InName, std::string& OutError );

    // A project's ProjectName from its Game.meproj, else its folder name.
    std::string ReadProjectName( const std::string& InRoot );

    struct RecentProject
    {
        // Absolute, '/'-separated.
        std::string Root;
        std::string Name;
        // Seconds since the Unix epoch.
        int64_t LastOpened = 0;
    };

    // Most recently opened first. Entries whose folder is gone stay listed (the Hub offers to remove
    // them), since a project on an unplugged drive comes back.
    class RecentProjects
    {
    public:
        static constexpr size_t kMaxEntries = 20;

        explicit RecentProjects( std::string InFile = GetDefaultFile() );

        // --recent-projects <file> when given; unattended runs use .tmp/Hub/RecentProjects.json under
        // the working directory, so tests never touch the user's list; otherwise the per-user
        // RecentProjects.json in the OS preference folder (SDL_GetPrefPath "MitchEngine/Havana").
        static std::string GetDefaultFile();

        void Load();
        bool Save() const;

        // Moves the project to the front (adding it), with its current name and the time now.
        void Add( const std::string& InRoot );
        bool Remove( const std::string& InRoot );
        bool Contains( const std::string& InRoot ) const;

        const std::vector<RecentProject>& GetEntries() const { return m_entries; }
        const std::string& GetFile() const { return m_file; }

    private:
        std::string m_file;
        std::vector<RecentProject> m_entries;
    };
}
