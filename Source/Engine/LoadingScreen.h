#pragma once
#include "Dementia.h"
#include "Engine/ProjectSettings.h"
#include "JSON.h"
#include <future>
#include <string>
#include <vector>

// Loading screens and splashes (PLAY-5).
//
// LoadSceneAsync swaps the main scene without a hitch you can see: the file is read and parsed on
// a worker while the current scene keeps running under the loading document, then the new scene
// is instantiated, and the screen stays up until the assets it started loading (models, textures,
// sounds: ResourceCache::GetPendingLoadCount) have landed. PlaySplash shows documents in turn
// (the project's Splash list at boot in game builds).
//
// The documents are RmlUi overlays (RmlUiCore::ShowOverlay), so they survive the scene change, and
// they bind the "loading" data model:
//   progress   0..1        percent  0..100        width  "42%" (for data-style-width)
//   scene      the scene's file name               phase  "Reading", "Loading assets", "Ready"
//   product    ProjectSettings::ProductName
// Main thread only.
class LoadingScreen
{
public:
    static constexpr const char* kModel = "loading";

    enum class Phase : uint8_t
    {
        Idle,
        Reading,        // the worker reads and parses the scene file
        Settling,       // instantiated; waiting for its asset loads
        Holding,        // done, kept up for MinimumSeconds
    };

    static LoadingScreen& Get();

    // Starts loading InScene. InDocument: "" = the project's LoadingDocument, "none" = no screen.
    // False when the scene doesn't exist or a load is already running.
    bool LoadSceneAsync( const std::string& InScene, const std::string& InDocument = "" );
    bool IsLoading() const { return m_phase != Phase::Idle; }
    Phase GetPhase() const { return m_phase; }
    // 0..1 (1 while idle).
    float GetProgress() const { return m_progress; }

    // Shows each document for its Seconds, one after another.
    void PlaySplash( const std::vector<ProjectSettings::SplashScreen>& InScreens );
    bool IsShowingSplash() const { return m_splashIndex < m_splash.size(); }

    // Keeps a loading screen up at least this long (a game that wants its art seen; captures);
    // the bar fills across it, never ahead of the real progress.
    float MinimumSeconds = 0.f;

    // Once a frame, after rendering.
    void Update( float InUnscaledDeltaSeconds );

    static const char* GetPhaseName( Phase InPhase );

private:
    void SetProgress( float InProgress );
    void Finish();

    Phase m_phase = Phase::Idle;
    std::string m_scene;
    std::string m_document;
    std::future<json> m_reading;
    float m_progress = 1.f;
    float m_elapsed = 0.f;
    int m_settledFrames = 0;
    size_t m_peakPending = 0;
    int m_loggedPercent = -1;

    std::vector<ProjectSettings::SplashScreen> m_splash;
    size_t m_splashIndex = 0;
    float m_splashElapsed = 0.f;
};
