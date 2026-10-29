#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include "Tool.h"
#include "Tools/Projects/ProjectLibrary.h"
#include "Widgets/ProjectBrowser.h"
#include <string>
#include <vector>

// The Project Hub (SHIP-5, Engine/Docs/Projects.md): what Havana shows when it starts outside a
// project (or with --hub, File > Project Hub). Recent projects, a new project from a template, or
// any project folder; picking one closes the Hub and the editor opens it (RunHub in main.cpp).
//
// Unattended (--editor-exec <script>): the script's hub-* commands drive it (hub-create,
// hub-open, hub-remove, hub-add, hub-assert-template, hub-assert-recent, hub-screenshot; wait N).
// The editor that opens the project runs the same script and skips them.
class ProjectHub
    : public Tool
{
public:
    explicit ProjectHub( ToolCreationFlags& InFlags );

    void OnStart() override;
    void OnUpdate() override;
    void OnEnd() override;

    // The project picked (opened or created); empty when the Hub was closed without one.
    const std::string& GetChosenProject() const { return m_chosen; }
    // Unattended: a script line failed.
    bool HasFailures() const { return m_failures > 0; }

    // This editor executable and InArgs, each quoted, as one command line.
    static std::string EditorCommand( const std::vector<std::string>& InArgs );

private:
    void DrawProjects();
    void DrawNewProject();
    void DrawOpenOther();
    void DrawLocationPicker();

    void Open( const std::string& InRoot );
    bool Create( const ProjectLibrary::Template& InTemplate, const std::string& InParent, const std::string& InName );
    void SuggestName();

    // Unattended scripts.
    void TickAutomation();
    bool Execute( const std::string& InLine );
    void Fail( const std::string& InMessage );
    void Finish();

    ProjectLibrary::RecentProjects m_recent;
    std::vector<ProjectLibrary::Template> m_templates;
    std::string m_engineRoot;
    std::string m_chosen;
    std::string m_selectedProject;
    size_t m_selectedTemplate = 0;
    std::string m_newName;
    std::string m_newLocation;
    std::string m_createError;
    ProjectBrowser m_openBrowser;
    ProjectBrowser m_locationBrowser;
    bool m_openOtherRequested = false;
    bool m_locationRequested = false;

    std::vector<std::string> m_lines;
    size_t m_nextLine = 0;
    int m_waitFrames = 0;
    std::string m_resultPath;
    std::vector<std::string> m_failureMessages;
    int m_failures = 0;
    bool m_scripted = false;
    bool m_finished = false;
    // hub-screenshot: waiting for the renderer to deliver.
    uint32_t m_screenshotBaseline = 0;
    int m_screenshotWait = 0;
};

#endif
