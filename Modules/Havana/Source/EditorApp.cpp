#include "EditorApp.h"
#include "ECS/Component.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"

#include <memory>
#include "Engine/World.h"
#include "Path.h"
#include "Cores/EditorCore.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "Cores/SceneCore.h"
#include "Events/SceneEvents.h"
#include "RenderCommands.h"
#include "Events/Event.h"
#include "Math/Matrix4.h"
#include "Math/Frustrum.h"
#include "optick.h"
#include <ctime>
#include <Math/Quaternion.h>
#include "Events/HavanaEvents.h"
#include "Events/EditorEvents.h"
#include <Utils/EditorConfig.h>

#include "Editor/EditorComponentInfoCache.h"
#include "Editor/EditorOperations.h"
#include "Editor/Selection.h"
#include "Editor/UndoStack.h"
#include "World/SceneSerializer.h"
#include <imgui.h>
#include <filesystem>
#include "File.h"
#include "CLog.h"
#include "Types/AssetType.h"
#include "bx/math.h"

#if USING( ME_EDITOR )

EditorApp::EditorApp( int argc, char** argv )
    : Game( argc, argv )
{
    std::vector<TypeId> events;
    events.push_back( NewSceneEvent::GetEventId() );
    events.push_back( SceneLoadedEvent::GetEventId() );
    EventManager::GetInstance().RegisterReceiver( this, events );

    // 0x0100 is the default, so we put the transform component decently high up.
    REGISTER_EDITORCOMPONENTCACHE_ORDERDATA( Transform, 0x0F00 );

    // This would be first if uncommented.
    //REGISTER_EDITORCOMPONENTCACHE_ORDERDATA( RenderComponent, 0x000F );

    // Other examples...
    //REGISTER_EDITORCOMPONENTCACHE_ORDERDATA( EditorCameraComponent, 0x0300 );
    //REGISTER_EDITORCOMPONENTCACHE_ORDERDATA( CameraComponent, 0x000E );
}


EditorApp::~EditorApp()
{
}


void EditorApp::OnStart()
{
    Camera::EditorCamera->Near = 1.f;
}


void EditorApp::OnUpdate( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "EditorApp::OnUpdate", Optick::Category::GameLogic );

#if USING( ME_EDITOR )
    {
        static int s_startupFramesLeft = 10;
        if( s_startupFramesLeft > 0 )
        {
            --s_startupFramesLeft;
            if( s_startupFramesLeft == 0 )
            {
                OPTICK_STOP_CAPTURE();
                OPTICK_SAVE_CAPTURE( "startup.opt" );
            }
        }

        static constexpr int kCaptureFrames = 10;
        static int s_captureFramesLeft = 0;
        Input& editorInput = GetEngine().GetEditorInput();
        if( editorInput.WasKeyPressed( KeyCode::F9 ) && s_captureFramesLeft == 0 )
        {
            OPTICK_START_CAPTURE();
            s_captureFramesLeft = kCaptureFrames;
            CLog::GetInstance().Log( CLog::LogType::Info, "Optick capture started (10 frames)" );
        }

        if( s_captureFramesLeft > 0 )
        {
            --s_captureFramesLeft;
            if( s_captureFramesLeft == 0 )
            {
                OPTICK_STOP_CAPTURE();
                char filename[64];
                std::time_t t = std::time( nullptr );
                std::strftime( filename, sizeof( filename ), "capture_%Y%m%d_%H%M%S.opt", std::localtime( &t ) );
                OPTICK_SAVE_CAPTURE( filename );
                CLog::GetInstance().Log( CLog::LogType::Info, std::string( "Optick capture saved: " ) + filename );
            }
        }
    }
#endif

    Editor->NewFrame();
    Transform* root = GetEngine().SceneNodes->GetRootTransform();

    EditorSceneManager->Update( inUpdateContext, root );
    Editor->UpdateWorld( root, EditorSceneManager->GetEntities() );

    UpdateCameras();

    if( !m_checkedRecovery )
    {
        m_checkedRecovery = true;
        if( !m_automation.IsActive() )
        {
            CheckForRecovery();
        }
    }
    if( !m_automation.IsActive() )
    {
        UpdateAutosave( inUpdateContext.GetUnscaledDeltaTime() );
    }
    DrawEditorModals();
    m_automation.Tick( *this );
}


void EditorApp::UpdateCameras()
{
    if( !Camera::CurrentCamera )
    {
        Camera::CurrentCamera = Camera::EditorCamera;
    }

    Moonlight::CameraData& EditorCamera = GetEngine().EditorCamera;
    SharedPtr<Transform> camTransform = EditorSceneManager->GetEditorCameraTransform();
    Camera* editorCamera = Camera::EditorCamera;

    EditorCamera.Position = camTransform->GetPosition();
    EditorCamera.Front = camTransform->Front();
    EditorCamera.Up = camTransform->Up();
    // this is why the game is fucked, need to tell it a custom render size from the world.
    EditorCamera.OutputSize = Editor->GetWorldEditorRenderSize();
    editorCamera->OutputSize = EditorCamera.OutputSize;
    EditorCamera.FOV = editorCamera->GetFOV();
    EditorCamera.Near = editorCamera->Near;
    EditorCamera.Far = editorCamera->Far;
    EditorCamera.Skybox = Camera::CurrentCamera->Skybox;
    EditorCamera.ClearColor = Camera::CurrentCamera->ClearColor;
    EditorCamera.ClearType = Camera::CurrentCamera->ClearType;
    EditorCamera.Projection = editorCamera->Projection;
    EditorCamera.OrthographicSize = editorCamera->OrthographicSize;
    //EditorCamera.ShouldCull = false;

    Vector3& eye = EditorCamera.Position;
    Vector3 at = eye + EditorCamera.Front;
    Vector3& up = EditorCamera.Up;

    EditorCamera.View = glm::lookAtLH( eye.InternalVector, at.InternalVector, up.InternalVector );
    Frustum& camFrustum = editorCamera->CameraFrustum;

    EditorCamera.ProjectionMatrix = editorCamera->GetProjectionMatrix();
    camFrustum.Update( EditorCamera.ProjectionMatrix, EditorCamera.View, EditorCamera.FOV, EditorCamera.OutputSize, EditorCamera.Near, EditorCamera.Far );

    EditorCamera.ViewFrustum = editorCamera->CameraFrustum;

    GetEngine().EditorCamera = EditorCamera;
}


void EditorApp::OnEnd()
{
    Editor->Save();
    EditorConfig::GetInstance().Save();
    // A clean exit: nothing to recover next time.
    ClearAutosave();
}


void EditorApp::OnInitialize()
{
    OPTICK_EVENT( "EditorApp::OnInitialize" );
    if( !Editor )
    {
        {
            OPTICK_EVENT( "EditorConfig::Load" );
            EditorConfig::GetInstance().Init();
            EditorConfig::GetInstance().Load();
        }
        InitialLevel = GetEngine().GetConfig().GetValue( "CurrentScene" );
        {
            OPTICK_EVENT( "Havana::Create" );
            Editor = MakeUnique<Havana>( &GetEngine(), this );
        }
        EditorSceneManager = new EditorCore( Editor.get() );
        m_automation.Init();

        NewSceneEvent evt;
        evt.Fire();
        GetEngine().GetWorld().lock()->AddCore<EditorCore>( *EditorSceneManager );
        GetEngine().LoadScene( InitialLevel );
    }
    else
    {
        GetEngine().GetWorld().lock()->AddCore<EditorCore>( *EditorSceneManager );
    }
}


void EditorApp::PreRender()
{
    Editor->Render( GetEngine().EditorCamera );
    Editor->GetInput().PostUpdate();
}


void EditorApp::PostRender()
{
}


void EditorApp::Play()
{
    if( m_isGameRunning )
    {
        return;
    }
    StartGame();
    m_isGamePaused = false;
    GetEngine().SetPaused( false );

    ImGui::SetWindowFocus( "Game View" );
    GetEngine().GetInput().Resume();
    Editor->GetInput().Stop();
}


void EditorApp::Stop()
{
    if( !m_isGameRunning )
    {
        return;
    }
    m_isGamePaused = false;
    GetEngine().SetPaused( false );
    StopGame();

    GetEngine().GetInput().Stop();
    Editor->GetInput().Resume();
}


void EditorApp::TogglePause()
{
    if( !m_isGameRunning )
    {
        return;
    }
    // Pausing freezes Update/FixedUpdate deltas; rendering and the editor keep running.
    m_isGamePaused = !m_isGamePaused;
    GetEngine().SetPaused( m_isGamePaused );
}


void EditorApp::StepFrame()
{
    if( !m_isGameRunning )
    {
        return;
    }
    if( !m_isGamePaused )
    {
        TogglePause();
    }
    GetEngine().StepFrame();
}


void EditorApp::StartGame()
{
    if( m_isGameRunning )
    {
        return;
    }
    m_playSnapshot = json();
    Scene* scene = GetEngine().CurrentScene;
    if( scene && EditorSceneManager && EditorSceneManager->RootTransform )
    {
        m_playSceneFilePath = scene->IsNewScene() ? std::string() : scene->FilePath.GetLocalPathString();
        m_playSnapshot = SceneSerializer::SerializeWorld( *GetEngine().GetWorld().lock(), EditorSceneManager->RootTransform );
    }
    m_playSelection = Selection::Get().SaveGUIDs();
    m_wasDirtyBeforePlay = UndoStack::Get().IsDirty();
    UndoStack::Get().SetSuspended( true );

    GetEngine().GetWorld().lock()->Start();
    m_isGameRunning = true;
}


void EditorApp::StopGame()
{
    if( !m_isGameRunning )
    {
        return;
    }
    if( GetEngine().GetWorld().lock() )
    {
        GetEngine().GetWorld().lock()->Destroy();
    }
    m_isGameRunning = false;
    GetEngine().GetWorld().lock()->Stop();
    Selection::Get().Clear();

    m_isRestoringSnapshot = true;
    NewSceneEvent evt;
    evt.Fire();
    if( !m_playSnapshot.is_null() )
    {
        // Restore the pre-play state under the scene's real path.
        GetEngine().LoadSceneFromData( m_playSnapshot, m_playSceneFilePath );
        m_playSnapshot = json();
    }
    else
    {
        InitialLevel = GetEngine().GetConfig().GetValue( "CurrentScene" );
        GetEngine().LoadScene( InitialLevel );
    }
    m_isRestoringSnapshot = false;

    UndoStack::Get().SetSuspended( false );
    if( m_wasDirtyBeforePlay )
    {
        UndoStack::Get().MarkDirty();
    }
    Selection::Get().RestoreGUIDs( *GetEngine().GetWorld().lock(), m_playSelection );
}


namespace
{
    const char* kAutosaveDir = ".tmp/Autosave";
    const char* kAutosaveScene = ".tmp/Autosave/Autosave.lvl";
    const char* kAutosaveSession = ".tmp/Autosave/Session.json";

    std::string CurrentScenePath()
    {
        Scene* scene = GetEngine().CurrentScene;
        return ( scene && !scene->IsNewScene() ) ? scene->FilePath.GetLocalPathString() : std::string();
    }
}


void EditorApp::RunWithUnsavedCheck( std::function<void()> InAction )
{
    if( m_isGameRunning )
    {
        Stop();
    }
    if( !UndoStack::Get().IsDirty() )
    {
        InAction();
        return;
    }
    m_pendingAction = std::move( InAction );
    m_openUnsavedPrompt = true;
}


void EditorApp::RequestNewScene()
{
    RunWithUnsavedCheck( []() {
        NewSceneEvent evt;
        evt.Queue();
    } );
}


void EditorApp::RequestOpenScene( const std::string& InScenePath )
{
    RunWithUnsavedCheck( [InScenePath]() {
        LoadSceneEvent evt;
        evt.Level = InScenePath;
        evt.Queue();
        GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), InScenePath );
        EditorConfig::GetInstance().AddRecentScene( InScenePath );
    } );
}


void EditorApp::RequestOpenSceneDialog()
{
    RequestAssetSelectionEvent evt( [this]( Path selectedAsset ) {
        RequestOpenScene( std::string( selectedAsset.GetLocalPath() ) );
    }, AssetType::Level );
    evt.Fire();
}


void EditorApp::SaveScene( bool InSaveAs )
{
    if( m_isGameRunning )
    {
        BRUH( "Stop play mode before saving the scene." );
        return;
    }
    SaveSceneEvent evt;
    evt.SaveAs = InSaveAs;
    evt.Fire();
    EditorConfig::GetInstance().AddRecentScene( CurrentScenePath() );
}


void EditorApp::RequestQuit()
{
    RunWithUnsavedCheck( []() { GetEngine().Quit( true ); } );
}


bool EditorApp::OnQuitRequested()
{
    if( !UndoStack::Get().IsDirty() && !m_isGameRunning )
    {
        return true;
    }
    // Stops play mode, then quits or asks about unsaved changes.
    RequestQuit();
    return false;
}


void EditorApp::DrawEditorModals()
{
    if( m_openUnsavedPrompt )
    {
        ImGui::OpenPopup( "Unsaved Changes" );
        m_openUnsavedPrompt = false;
    }
    if( m_openRecoveryPrompt )
    {
        ImGui::OpenPopup( "Recover Unsaved Work" );
        m_openRecoveryPrompt = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    if( ImGui::BeginPopupModal( "Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        const std::string scenePath = CurrentScenePath();
        ImGui::Text( "Save changes to %s before continuing?", scenePath.empty() ? "the untitled scene" : scenePath.c_str() );
        ImGui::Spacing();
        std::function<void()> runPending;
        if( !scenePath.empty() )
        {
            if( ImGui::Button( "Save", ImVec2( 120.f, 0.f ) ) )
            {
                SaveScene( false );
                ImGui::CloseCurrentPopup();
                runPending.swap( m_pendingAction );
            }
        }
        else if( ImGui::Button( "Save As...", ImVec2( 120.f, 0.f ) ) )
        {
            // The save dialog is asynchronous; the user repeats the action afterwards.
            SaveScene( true );
            m_pendingAction = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Don't Save", ImVec2( 120.f, 0.f ) ) )
        {
            ImGui::CloseCurrentPopup();
            UndoStack::Get().MarkSaved();
            runPending.swap( m_pendingAction );
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImVec2( 120.f, 0.f ) ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            m_pendingAction = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();

        if( runPending )
        {
            runPending();
        }
    }

    ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    if( ImGui::BeginPopupModal( "Recover Unsaved Work", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
    {
        ImGui::Text( "Havana didn't shut down cleanly last time." );
        ImGui::Text( "An autosave of %s is available.", m_recoveryScenePath.empty() ? "an untitled scene" : m_recoveryScenePath.c_str() );
        ImGui::Spacing();
        if( ImGui::Button( "Recover", ImVec2( 120.f, 0.f ) ) )
        {
            GetEngine().LoadSceneFromData( m_recoveryData, m_recoveryScenePath );
            UndoStack::Get().MarkDirty();
            m_recoveryData = json();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Discard", ImVec2( 120.f, 0.f ) ) )
        {
            ClearAutosave();
            m_recoveryData = json();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}


void EditorApp::UpdateAutosave( float InDeltaSeconds )
{
    const float interval = EditorConfig::GetInstance().GetPreference<float>( "Autosave.IntervalSeconds", 120.f );
    if( interval <= 0.f || m_isGameRunning || !UndoStack::Get().IsDirty() || !EditorSceneManager || !EditorSceneManager->RootTransform )
    {
        m_autosaveTimer = 0.f;
        return;
    }
    m_autosaveTimer += InDeltaSeconds;
    if( m_autosaveTimer < interval )
    {
        return;
    }
    m_autosaveTimer = 0.f;

    std::error_code error;
    std::filesystem::create_directories( Path( kAutosaveDir ).FullPath, error );
    json scene = SceneSerializer::SerializeWorld( *GetEngine().GetWorld().lock(), EditorSceneManager->RootTransform );
    File( Path( kAutosaveScene ) ).Write( scene.dump() );
    json session;
    session["Scene"] = CurrentScenePath();
    session["Time"] = static_cast<int64_t>( std::time( nullptr ) );
    File( Path( kAutosaveSession ) ).Write( session.dump( 4 ) );
    CLog::Log( CLog::LogType::Info, "Autosaved scene to " + std::string( kAutosaveScene ) );
}


void EditorApp::CheckForRecovery()
{
    Path sessionPath( kAutosaveSession );
    Path scenePath( kAutosaveScene );
    if( !sessionPath.Exists || !scenePath.Exists )
    {
        return;
    }
    File sessionFile( sessionPath );
    sessionFile.Read();
    json session = json::parse( sessionFile.Data, nullptr, false );
    File sceneFile( scenePath );
    sceneFile.Read();
    json scene = json::parse( sceneFile.Data, nullptr, false );
    if( session.is_discarded() || scene.is_discarded() )
    {
        ClearAutosave();
        return;
    }
    m_recoveryScenePath = session.value( "Scene", std::string() );
    m_recoveryData = std::move( scene );
    m_openRecoveryPrompt = true;
}


void EditorApp::ClearAutosave()
{
    std::error_code error;
    std::filesystem::remove( Path( kAutosaveSession ).FullPath, error );
    std::filesystem::remove( Path( kAutosaveScene ).FullPath, error );
}


const bool EditorApp::IsGameRunning() const
{
    return m_isGameRunning;
}


const bool EditorApp::IsGamePaused() const
{
    return m_isGamePaused;
}


bool EditorApp::OnEvent( const BaseEvent& evt )
{
    if( evt.GetEventId() == NewSceneEvent::GetEventId() )
    {
        if( !m_isRestoringSnapshot )
        {
            EditorOps::ResetForNewScene();
        }
        GetEngine().LoadScene( "" );
        GetEngine().InitGame();
        GetEngine().GetWorld().lock()->Simulate();
    }
    else if( evt.GetEventId() == SceneLoadedEvent::GetEventId() )
    {
        const SceneLoadedEvent& test = static_cast<const SceneLoadedEvent&>( evt );

        if( !m_isRestoringSnapshot && !m_isGameRunning )
        {
            EditorOps::ResetForNewScene();
        }
        const std::string scenePath = test.LoadedScene->FilePath.GetLocalPathString();
        Editor->SetWindowTitle( "Havana - " + ( scenePath.empty() ? std::string( "Untitled" ) : scenePath ) );
        if( m_isGameRunning )
        {
            GetEngine().GetWorld().lock()->Start();
        }
    }

    return false;
}

#endif
