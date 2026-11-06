#pragma once
#include "World.h"
#include "Dementia.h"
#include "Clock.h"
#include "Events/EventReceiver.h"
#include "World/Scene.h"
#include "World/SceneStreaming.h"
#include "Camera/CameraData.h"
#include <string>
#include <chrono>
#include "Config/EngineConfig.h"
#include "Input.h"
#include "Jobs/JobSystem.h"
#include "Core/ISystem.h"

#if USING( ME_GAME_TOOLS )
#include "Tools/DebugTools.h"
#endif
#include "Core/FrameRenderData.h"
#include "AutomationRunner.h"
#include "NetLaunch.h"
#include "KillcamPlayer.h"
#include "Resource/FileWatcher.h"
#include "ScriptReloadGate.h"

class Game;
class IWindow;
class BGFXRenderer;

class Engine
    : public EventReceiver
    , public ISystem
{
    class EngineUpdateContext : public UpdateContext
    {
        friend Engine;
    };

public:
    ME_SYSTEM_ID( Engine );

    Engine();
    ~Engine();

    void Init( Game* game );

    void InitGame();

    void StopGame();

    void LoadScene( const std::string& Level );
    // Loads a scene from already-parsed data; the scene's FilePath is set to InFilePath (may be empty).
    void LoadSceneFromData( const json& InData, const std::string& InFilePath );

    // Additive scenes on top of the main one (World/SceneStreaming.h): their entities unload together
    // and never save into the main scene; loading another main scene drops them. Async reads and
    // parses on a worker thread and instantiates at the start of a later frame (deterministic runs
    // wait for it). Returns the scene's id (0 when a synchronous load fails). AdditiveSceneEvent
    // reports loads and unloads.
    uint16_t LoadSceneAdditive( const std::string& InPath, bool InAsync = false );
    bool UnloadAdditiveScene( uint16_t InSceneId );
    SceneStreaming& GetSceneStreaming()
    {
        return m_sceneStreaming;
    }
    // The file an entity's scene came from: its additive scene's, else the main scene's.
    std::string GetEntityScenePath( const Entity& InEntity ) const;

    void Run();
    // What the process should exit with once Run returns (non-zero: a --server / --connect that couldn't start).
    int GetExitCode() const;
    // The command line's networking: the session, the replay viewer, the Net Debugger (NET-5).
    NetLaunch& GetNetLaunch() { return m_netLaunch; }
    // ME-323 phase 3: the killcams the game world's session receives, played into a replay world of
    // their own and presented instead of the game world while they play (the Player; not Havana yet).
    KillcamPlayer& GetKillcam() { return m_killcam; }
    // Orderly teardown after the main loop: world, jobs, logging.
    void Shutdown();

    BGFXRenderer& GetRenderer() const;

    std::weak_ptr<World> GetWorld() const;
    // The game world's main camera (ME-297: Camera::GetMain of GameWorld), for the editor and the
    // engine's own UI; a core or script reads its own world's.
    class Camera* GetMainCamera() const;

    bool IsRunning() const;
    // Requests shutdown at the start of the next frame. Unless forced, Game::OnQuitRequested may veto.
    void Quit( bool InForce = false );
    const bool IsInitialized() const;

    IWindow* GetWindow();

    Game* GetGame() const;

    EngineConfig& GetConfig();
    Input& GetInput();

    Jobs::JobSystem& GetJobSystem();

    class CameraCore* Cameras = nullptr;
    class SceneCore* SceneNodes = nullptr;
    class RenderCore* ModelRenderer = nullptr;
    class ParticleCore* Particles = nullptr;
    class TerrainCore* Terrains = nullptr;
    class PhysicsCore* Physics = nullptr;
    class Physics2DCore* Physics2D = nullptr;
    // Networking (NET-2): idle until a session is hosted or joined.
    class NetCore* Net = nullptr;
    // Per-entity C++ collision callbacks (Physics/CollisionRouter.h).
    class CollisionRouter* Collisions = nullptr;
    class AnimationCore* Animation = nullptr;
    class SequenceCore* Sequences = nullptr;
    class NavigationCore* Navigation = nullptr;
    class AudioCore* AudioThread = nullptr;
    // C# scripts (null without scripting).
    class ScriptCore* Scripts = nullptr;
    class RmlUiCore* RmlUi = nullptr;
    Clock GameClock;
    Moonlight::CameraData EditorCamera;
    Scene* CurrentScene = nullptr;
    // Scaled delta of the current frame (0 while paused).
    float DeltaTime = 0.f;
    // This frame's real (unpaused, unscaled) seconds.
    float UnscaledDeltaTime = 0.f;

    // Time control. Pause/step affect Update and FixedUpdate deltas; rendering keeps running.
    void SetTimeScale( float InTimeScale );
    float GetTimeScale() const { return m_timeScale; }
    void SetPaused( bool InPaused );
    bool IsPaused() const { return m_isPaused; }
    // While paused, advances the simulation by exactly one fixed step on the next frame.
    void StepFrame();
    void SetFixedTimeStep( float InSeconds );
    float GetFixedTimeStep() const { return m_fixedTimeStep; }
    // 0 = uncapped.
    void SetMaxFrameRate( float InFramesPerSecond );
    float GetMaxFrameRate() const { return m_maxFrameRate; }
    // PLAY-9: crash reports an earlier session left (found at start-up, then marked seen), oldest
    // first: what a game's upload hook would send.
    const std::vector<std::string>& GetPreviousCrashReports() const { return m_previousCrashReports; }
private:
    Input m_input;
    std::vector<std::string> m_previousCrashReports;
    std::shared_ptr<World> GameWorld;
    bool Running = false;
    IWindow* GameWindow = nullptr;
    EngineConfig engineConfig;
    Game* m_game = nullptr;
    // A monotonic clock for the network layer, in seconds.
    double NetSeconds() const;
    void LimitFrameRate( std::chrono::steady_clock::time_point frameStart );
    // Logs the threads the engine starts and their bound (ME-319).
    void LogThreadInventory();
    void LoadSceneInternal( const std::string& SceneFile, const json* InData );
    // ME-323: a killcam's replay world: the scene's placed entities and what presents them.
    bool PopulateKillcamWorld( KillcamPlayer::ReplayWorld& InReplay, std::string& OutError );
    // Each frame after the game world's late update: the killcam playing, and which world is presented.
    void UpdateKillcam( const UpdateContext& InContext );
    // After the game world's cameras: the replay world's, with the game's UI over them.
    void LateUpdateKillcam( const UpdateContext& InContext );

    double m_fixedAccumulator = 0.0;
    float m_fixedTimeStep = 1.f / 60.f;
    int m_maxFixedStepsPerFrame = 8;
    float m_maxFrameDelta = 0.25f;
    // --frame-time: every frame advances exactly this much (deterministic captures); 0 = real time.
    float m_fixedFrameDelta = 0.f;
    float m_timeScale = 1.f;
    float m_maxFrameRate = 0.f;
    bool m_isPaused = false;
    // Game builds: the ImGui debug overlay (Debug Tools window, profiler bar). F3 toggles it;
    // --debug-overlay starts with it shown, --no-debug-overlay keeps it hidden.
    bool m_debugOverlayVisible = false;
    bool m_deferredSubsystemsStarted = false;
    bool m_windowModeFromCommandLine = false;   // --window-mode isn't saved as the player's choice
    bool m_forceQuit = false;
    bool m_stepRequested = false;
    bool m_isInitialized = false;
    ME_SINGLETON_DEFINITION( Engine )

    BGFXRenderer* NewRenderer = nullptr;


    EngineUpdateContext updateContext;
    SystemRegistry systemRegistry;

#if USING( ME_EDITOR )
    Input m_editorInput;
public:
    Input& GetEditorInput();
#endif
#if USING( ME_GAME_TOOLS )
    DebugTools m_debugTools;
#endif
    FrameRenderData m_frameRenderSettings;

private:
    AutomationRunner m_automation;
    NetLaunch m_netLaunch;
    KillcamPlayer m_killcam;
    // --killcam-capture PNG [--killcam-capture-at S]: a screenshot that far into the first killcam.
    std::string m_killcamCapturePath;
    double m_killcamCaptureAt = 2.0;
    bool m_killcamCaptured = false;
    SceneStreaming m_sceneStreaming;
#if USING( ME_TOOLS )
    void PollAssetChanges();
    FileWatcher m_assetWatcher;
    // ME-311: .cs edits don't hot reload while a network session is live.
    ScriptReloadGate m_scriptReloadGate;
#endif
};

Engine& GetEngine();
