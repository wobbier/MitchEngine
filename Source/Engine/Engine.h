#pragma once
#include "World.h"
#include "Dementia.h"
#include "Clock.h"
#include "File.h"
#include "Events/EventReceiver.h"
#include "World/Scene.h"
#include "Camera/CameraData.h"
#include <string>
#include <chrono>
#include "Config/EngineConfig.h"
#include "Input.h"
#include "Work/Burst.h"
#include "Jobs/JobSystem.h"
#include "Core/ISystem.h"

#if USING( ME_GAME_TOOLS )
#include "Tools/DebugTools.h"
#endif
#include "Core/FrameRenderData.h"
#include "AutomationRunner.h"

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

    void Run();
    // Orderly teardown after the main loop: world, jobs, logging.
    void Shutdown();
    virtual bool OnEvent( const BaseEvent& evt );

    BGFXRenderer& GetRenderer() const;

    std::weak_ptr<World> GetWorld() const;

    bool IsRunning() const;
    void Quit();
    const bool IsInitialized() const;

    IWindow* GetWindow();

    Game* GetGame() const;

    EngineConfig& GetConfig();
    Input& GetInput();

    Jobs::JobSystem& GetJobSystem();

    class CameraCore* Cameras = nullptr;
    class SceneCore* SceneNodes = nullptr;
    class RenderCore* ModelRenderer = nullptr;
    class AudioCore* AudioThread = nullptr;
    class UICore* UI = nullptr;
    Clock GameClock;
    Moonlight::CameraData EditorCamera;
    Scene* CurrentScene = nullptr;
    // Scaled delta of the current frame (0 while paused).
    float DeltaTime = 0.f;

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
private:
    Input m_input;
    std::shared_ptr<World> GameWorld;
    bool Running = false;
    IWindow* GameWindow = nullptr;
    EngineConfig engineConfig;
    Game* m_game = nullptr;
    void LimitFrameRate( std::chrono::steady_clock::time_point frameStart );

    double m_fixedAccumulator = 0.0;
    float m_fixedTimeStep = 1.f / 60.f;
    int m_maxFixedStepsPerFrame = 8;
    float m_maxFrameDelta = 0.25f;
    float m_timeScale = 1.f;
    float m_maxFrameRate = 0.f;
    bool m_isPaused = false;
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
};

Engine& GetEngine();
