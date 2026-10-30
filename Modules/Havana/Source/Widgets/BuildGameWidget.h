#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )
#include "Engine/ProjectSettings.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// File > Build Game (SHIP-3, Engine/Docs/Building.md): edits the project's build profiles and builds
// one by running `Havana --build-game --profile <file>` as a child process, so a build (compile,
// cook, packaging) never stalls or crashes the editor. Its output streams into the window.
class BuildGameWidget
    : public HavanaWidget
{
public:
    BuildGameWidget();
    ~BuildGameWidget();

    // The open editor's window (automation), or null.
    static BuildGameWidget* Get();

    void Init() override {}
    void Destroy() override {}
    // Build & Run (ED-9): runs the build once it succeeds (called from Render).
    void Update() override;
    void Render() override;

    // Builds the selected profile (as shown, saved or not); false while a build runs. With
    // InRunWhenDone (Build & Run), a build that succeeds is started through its launcher, with
    // InRunArgs passed on (automation: a capture and --exit).
    bool StartBuild( bool InRunWhenDone = false, const std::string& InRunArgs = "" );
    // Starts the last build through its launcher; false without one.
    bool RunLastBuild( const std::string& InArgs = "" );
    bool IsBuilding() const;
    // After a build: whether it succeeded, and where it went.
    bool LastBuildSucceeded() const;
    std::string GetOutputDirectory() const;

    // Automation: shows InProfile (replacing the profile of that name, or added; not saved), selects
    // it and sets the build options.
    void UseProfile( const ProjectSettings::BuildProfile& InProfile, bool InCompile, bool InRecord );

private:
    // What the build thread shares with the window. The thread holds its own reference, so a
    // build still running when the editor closes never touches a destroyed window.
    struct BuildState
    {
        std::mutex Mutex;
        std::deque<std::string> Lines;
        std::atomic<bool> Running{ false };
        std::atomic<int> Step{ -1 };
        int ExitCode = 0;
        bool Finished = false;
    };

    void DrawProfiles();
    void DrawProfile( ProjectSettings::BuildProfile& InProfile );
    void DrawBuild();
    ProjectSettings::BuildProfile* GetSelected();

    int m_selected = 0;
    bool m_compile = true;
    bool m_record = true;
    bool m_followOutput = true;
    bool m_runWhenDone = false;
    std::string m_runArgs;
    std::string m_outputDirectory;
    std::string m_cancelFile;
    std::shared_ptr<BuildState> m_state = std::make_shared<BuildState>();
    std::thread m_thread;
};

#endif
