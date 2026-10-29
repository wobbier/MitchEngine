#include "EditorApp.h"
#include "Hub/ProjectHub.h"
#include "Tools/Projects/ProjectLibrary.h"
#include "ECS/Component.h"
#include "Editor/ReflectionUI.h"
#include "Scripting/ScriptInspectorRows.h"
#include "Engine/Clock.h"
#include "Components/Transform.h"
#include "ECS/Entity.h"
#include <string>
#include "Engine/Input.h"
#include "Components/Camera.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Graphics/Model.h"
#include "Components/Lighting/Light.h"
#include "Components/Graphics/PostProcess.h"

#include <memory>
#include "Engine/World.h"
#include "Path.h"
#include "Cores/EditorCore.h"
#include "Engine/Engine.h"
#include "Cores/AnimationCore.h"
#include "Cores/SequenceCore.h"
#include "Widgets/SequenceWidget.h"
#include "Havana.h"
#include "Cores/SceneCore.h"
#include "Events/SceneEvents.h"
#include "RenderCommands.h"
#include "Device/FrameBuffer.h"
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
#include "Engine/AutomationRunner.h"
#include "Engine/Project.h"
#include "Engine/ProjectSettings.h"
#include "Utils/PlatformUtils.h"
#include "Editor/Selection.h"
#include "Editor/UndoStack.h"
#include "Widgets/Terrain/TerrainWidget.h"
#include "World/SceneSerializer.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include "Core/CommandLine.h"
#include "Scripting/ScriptEngine.h"
#include "File.h"
#include "CLog.h"
#include "Types/AssetType.h"
#include "bx/math.h"
#include <ImGuiTheme.h>

#if USING( ME_EDITOR )

namespace
{
    // Profiler captures stay out of the project root: they can reach gigabytes each.
    constexpr const char* kCaptureFolder = ".tmp/Captures";
    constexpr size_t kCapturesKept = 5;


    std::string CapturePath( const std::string& InFileName )
    {
        std::error_code ec;
        std::filesystem::create_directories( kCaptureFolder, ec );
        return std::string( kCaptureFolder ) + "/" + InFileName;
    }


    // Deletes all but the newest kCapturesKept F9 captures (.opt on Optick builds, .json on the trace shim).
    void PruneCaptures()
    {
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> captures;
        for( const auto& entry : std::filesystem::directory_iterator( kCaptureFolder, ec ) )
        {
            if( entry.is_regular_file( ec ) && entry.path().filename().string().rfind( "capture_", 0 ) == 0 )
            {
                captures.push_back( entry );
            }
        }
        if( captures.size() <= kCapturesKept )
        {
            return;
        }
        std::sort( captures.begin(), captures.end(), []( const auto& a, const auto& b ) {
            std::error_code timeError;
            return a.last_write_time( timeError ) > b.last_write_time( timeError );
        } );
        for( size_t i = kCapturesKept; i < captures.size(); ++i )
        {
            std::filesystem::remove( captures[i].path(), ec );
        }
    }
}


EditorApp::EditorApp( int argc, char** argv )
    : Game( argc, argv )
{
    Listen<NewSceneEvent>( [this]( const NewSceneEvent& InEvent ) { OnNewScene( InEvent ); } );
    Listen<SceneLoadedEvent>( [this]( const SceneLoadedEvent& InEvent ) { OnSceneLoaded( InEvent ); } );

#if USING( ME_SCRIPTING )
    // The script inspector's entity/asset rows use the editor's reflection rows (drag-drop, pick).
    ScriptInspectorRows::EntityRow = &ReflectionUI::EntityGuidRow;
    ScriptInspectorRows::AssetRow = &ReflectionUI::AssetPathRow;
#endif

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

}


void EditorApp::OnUpdate( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "EditorApp::OnUpdate", Optick::Category::GameLogic );

#if USING( ME_EDITOR )
    {
        // --capture-startup records the first 10 frames (main() starts the capture).
        static int s_startupFramesLeft = CommandLine::Has( "--capture-startup" ) ? 10 : 0;
        if( s_startupFramesLeft > 0 )
        {
            --s_startupFramesLeft;
            if( s_startupFramesLeft == 0 )
            {
                OPTICK_STOP_CAPTURE();
                OPTICK_SAVE_CAPTURE( CapturePath( "startup.opt" ).c_str() );
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
                char filename[64] = "capture.opt";
                const std::time_t t = std::time( nullptr );
                std::tm localTime{};
#if USING( ME_PLATFORM_WINDOWS )
                if( localtime_s( &localTime, &t ) == 0 )
#else
                if( localtime_r( &t, &localTime ) )
#endif
                {
                    std::strftime( filename, sizeof( filename ), "capture_%Y%m%d_%H%M%S.opt", &localTime );
                }
                const std::string path = CapturePath( filename );
                OPTICK_SAVE_CAPTURE( path.c_str() );
                PruneCaptures();
                CLog::GetInstance().Log( CLog::LogType::Info, "Optick capture saved: " + path );
            }
        }
    }
#endif

    Editor->NewFrame();
    Transform* root = GetEngine().SceneNodes->GetRootTransform();

    EditorSceneManager->Update( inUpdateContext, root );
    if( !IsGameRunning() ) Camera::EditorCamera->ResetSkyClock();
    Camera::EditorCamera->GetSkySettings( IsGameRunning() ? inUpdateContext.GetDeltaTime() : 0.f );
    Editor->UpdateWorld( root, EditorSceneManager->GetEntities() );

    UpdateCameras();

    if( !m_checkedRecovery )
    {
        m_checkedRecovery = true;
        if( !AutomationRunner::IsUnattendedRun() )
        {
            CheckForRecovery();
            if( TerrainWidget::Get() ) TerrainWidget::Get()->RequestStartupRecovery();
        }
    }
    if( !AutomationRunner::IsUnattendedRun() )
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
    Camera* environmentCamera = EditorSceneManager->UsesSceneEnvironment() ? Camera::CurrentCamera : editorCamera;
    EditorCamera.Skybox = environmentCamera->Skybox;
    EditorCamera.ClearColor = environmentCamera->ClearColor;
    EditorCamera.ClearType = environmentCamera->ClearType;
    EditorCamera.ProceduralSky = environmentCamera->GetSkySettings();
    // The scene view shows the main camera's image settings (without its auto exposure state).
    {
        const PostProcess* post = Camera::CurrentCamera->Parent ? Camera::CurrentCamera->Parent->TryGetComponent<PostProcess>() : nullptr;
        EditorCamera.Post = ( post && post->IsEnabled() ) ? post->ToSettings() : Moonlight::PostProcessSettings();
    }
    EditorCamera.Projection = editorCamera->Projection;
    EditorCamera.OrthographicSize = editorCamera->OrthographicSize;
    // Its own pass switches (World View camera settings): heavy scenes can edit without them.
    EditorCamera.SunShadows = editorCamera->SunShadows;
    EditorCamera.AmbientOcclusion = editorCamera->AmbientOcclusion;
    EditorCamera.VolumetricFog = editorCamera->VolumetricFog;
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
    Editor->Destroy();
    // Only an interactive session owns the user's recovery files.
    if( !AutomationRunner::IsUnattendedRun() )
    {
        ClearAutosave();
    }
}


void EditorApp::OnInitialize()
{
    OPTICK_EVENT( "EditorApp::OnInitialize" );
    if( !Editor )
    {
        {
            OPTICK_EVENT( "EditorConfig::Load" );
            EditorConfig::GetInstance().ReadOnly = AutomationRunner::IsUnattendedRun();
            EditorConfig::GetInstance().Init();
            EditorConfig::GetInstance().Load();
        }
        // The Project Hub lists every project the editor opens (interactive runs only: unattended ones
        // never touch the user's list).
        if( !AutomationRunner::IsUnattendedRun() )
        {
            ProjectLibrary::RecentProjects recent;
            recent.Load();
            recent.Add( Project::GetRoot() );
            recent.Save();
        }
        InitialLevel = GetEngine().GetConfig().GetValue( "CurrentScene" );
        // A project opened for the first time (or whose last scene is gone) opens its startup scene.
        if( ( InitialLevel.empty() || !Path( InitialLevel ).Exists ) && !ProjectSettings::Get().StartupScene.empty() )
        {
            InitialLevel = ProjectSettings::Get().StartupScene;
        }
        {
            OPTICK_EVENT( "Havana::Create" );
            Editor = MakeUnique<Havana>( &GetEngine(), this );
        }
        EditorSceneManager = new EditorCore( Editor.get() );
        m_automation.Init();

        NewSceneEvent evt;
        evt.Fire();
        GetEngine().GetWorld().lock()->AddCore<EditorCore>( *EditorSceneManager );
        // With --scene the engine loads that scene right after OnStart: don't load (and stream in)
        // the last opened one first.
        if( !CommandLine::Has( "--scene" ) )
        {
            GetEngine().LoadScene( InitialLevel );
        }
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
    if( TerrainWidget::Get() && !TerrainWidget::Get()->PrepareForPlay() ) return;
    StartGame();
    m_isGamePaused = false;
    GetEngine().SetPaused( false );

    Editor->SetPlayMode( true );
    Editor->GetInput().Stop();
    GetEngine().GetInput().Resume();
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
    Editor->SetPlayMode( false );

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
#if USING( ME_SCRIPTING )
    if( ScriptEngine::HasBuildErrors() )
    {
        // Like a compiler, Play waits for code that builds: the last good scripts would run instead.
        YIKES( "Can't play: the C# scripts don't compile. Fix the errors in the Log, then press Play again." );
        return;
    }
#endif
    m_playSnapshot = json();
    // Animation previews end, so the snapshot (and play's bind pose) is the authored pose.
    if( GetEngine().Animation )
    {
        GetEngine().Animation->StopAllPreviews();
    }
    if( GetEngine().Sequences ) GetEngine().Sequences->StopAllPreviews();
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

    // The scene snapshot recreates scene cameras, but the editor camera and its render targets
    // survive Play. Restore their sky state before the first stopped frame is drawn.
    Camera::EditorCamera->ResetSkyClock();
    UpdateCameras();
    if( Moonlight::FrameBuffer* buffer = GetEngine().EditorCamera.Buffer )
    {
        buffer->TemporalValid = false;
        buffer->TemporalFrame = 0;
        buffer->CloudValid = false;
        buffer->CloudFrame = 0;
        buffer->FogHistoryValid = false;
        buffer->FogFrame = 0;
    }

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
    if( TerrainWidget::Get() && TerrainWidget::Get()->HasUnsaved() )
    {
        TerrainWidget::Get()->ConfirmUnsaved( [this, action = std::move( InAction )]() mutable { RunWithUnsavedCheck( std::move( action ) ); } );
        return;
    }
    if( SequenceWidget::Get() && SequenceWidget::Get()->HasUnsaved() )
    {
        SequenceWidget::Get()->ConfirmUnsaved( [this, action = std::move( InAction )]() mutable { RunWithUnsavedCheck( std::move( action ) ); } );
        return;
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


void EditorApp::RequestOpenProject( const std::string& InFolder )
{
    const std::string root = Project::FindProjectRoot( InFolder );
    if( root.empty() )
    {
        YIKES( "Open Project: " + InFolder + " is not a project (no " + Project::kProjectFile + ")" );
        return;
    }
    if( root == Project::GetRoot() )
    {
        CLog::Log( CLog::LogType::Info, "Open Project: " + root + " is already open" );
        return;
    }
    RunWithUnsavedCheck( [root]() {
        const std::string command = ProjectHub::EditorCommand( { "--project", root } );
        if( AutomationRunner::IsUnattendedRun() )
        {
            CLog::Log( CLog::LogType::Info, "Open Project: would reopen the editor with " + command );
            return;
        }
        CLog::Log( CLog::LogType::Info, "Open Project: reopening the editor on " + root );
        PlatformUtils::RunDetached( command );
        GetEngine().Quit( true );
    } );
}


void EditorApp::RequestProjectHub()
{
    RunWithUnsavedCheck( []() {
        const std::string command = ProjectHub::EditorCommand( { "--hub" } );
        if( AutomationRunner::IsUnattendedRun() )
        {
            CLog::Log( CLog::LogType::Info, "Project Hub: would reopen the editor with " + command );
            return;
        }
        CLog::Log( CLog::LogType::Info, "Project Hub: closing the editor for the Hub" );
        PlatformUtils::RunDetached( command );
        GetEngine().Quit( true );
    } );
}


void EditorApp::RequestQuit()
{
    RunWithUnsavedCheck( []() { GetEngine().Quit( true ); } );
}


bool EditorApp::OnQuitRequested()
{
    if( !UndoStack::Get().IsDirty() && !m_isGameRunning && !( TerrainWidget::Get() && TerrainWidget::Get()->HasUnsaved() ) && !( SequenceWidget::Get() && SequenceWidget::Get()->HasUnsaved() ) )
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
            if( ImGui::Button( "Save", ImGuiTheme::DialogButtonSize() ) )
            {
                SaveScene( false );
                ImGui::CloseCurrentPopup();
                runPending.swap( m_pendingAction );
            }
        }
        else if( ImGui::Button( "Save As...", ImGuiTheme::DialogButtonSize() ) )
        {
            // The save dialog is asynchronous; the user repeats the action afterwards.
            SaveScene( true );
            m_pendingAction = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Don't Save", ImGuiTheme::DialogButtonSize() ) )
        {
            ImGui::CloseCurrentPopup();
            UndoStack::Get().MarkSaved();
            runPending.swap( m_pendingAction );
        }
        ImGui::SameLine();
        if( ImGui::Button( "Cancel", ImGuiTheme::DialogButtonSize() ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
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
        if( ImGui::Button( "Recover", ImGuiTheme::DialogButtonSize() ) )
        {
            GetEngine().LoadSceneFromData( m_recoveryData, m_recoveryScenePath );
            UndoStack::Get().MarkDirty();
            m_recoveryData = json();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if( ImGui::Button( "Discard", ImGuiTheme::DialogButtonSize() ) )
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
    // A saved (or undone-to-saved) scene has nothing to recover; drop the stale snapshot.
    // Kept while the recovery prompt is still open so Discard/Recover decides.
    if( m_hasAutosave && !UndoStack::Get().IsDirty() && !m_isGameRunning && m_recoveryData.is_null() )
    {
        ClearAutosave();
    }

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
    if( GetEngine().Animation )
    {
        GetEngine().Animation->RestorePreviewPoses();   // previews pose again next update
    }
    if( GetEngine().Sequences ) GetEngine().Sequences->RestorePreviewValues();
    json scene = SceneSerializer::SerializeWorld( *GetEngine().GetWorld().lock(), EditorSceneManager->RootTransform );
    File( Path( kAutosaveScene ) ).Write( scene.dump() );
    json session;
    session["Scene"] = CurrentScenePath();
    session["Time"] = static_cast<int64_t>( std::time( nullptr ) );
    File( Path( kAutosaveSession ) ).Write( session.dump( 4 ) );
    m_hasAutosave = true;
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
    // The scene was saved after the snapshot was taken (an exit that skipped OnEnd left it behind).
    if( !m_recoveryScenePath.empty() )
    {
        Path savedScene( m_recoveryScenePath );
        std::error_code savedError, autosaveError;
        const auto savedTime = std::filesystem::last_write_time( savedScene.FullPath, savedError );
        const auto autosaveTime = std::filesystem::last_write_time( scenePath.FullPath, autosaveError );
        if( savedScene.Exists && !savedError && !autosaveError && savedTime >= autosaveTime )
        {
            ClearAutosave();
            return;
        }
    }
    m_hasAutosave = true;
    m_recoveryData = std::move( scene );
    m_openRecoveryPrompt = true;
}


void EditorApp::ClearAutosave()
{
    std::error_code error;
    std::filesystem::remove( Path( kAutosaveSession ).FullPath, error );
    std::filesystem::remove( Path( kAutosaveScene ).FullPath, error );
    m_hasAutosave = false;
}


const bool EditorApp::IsGameRunning() const
{
    return m_isGameRunning;
}


const bool EditorApp::IsGamePaused() const
{
    return m_isGamePaused;
}


void EditorApp::OnNewScene( const NewSceneEvent& )
{
    if( !m_isRestoringSnapshot )
    {
        EditorOps::ResetForNewScene();
    }
    GetEngine().LoadScene( "" );
    GetEngine().InitGame();
    GetEngine().GetWorld().lock()->Simulate();
}


void EditorApp::OnSceneLoaded( const SceneLoadedEvent& InEvent )
{
    if( !m_isRestoringSnapshot && !m_isGameRunning )
    {
        EditorOps::ResetForNewScene();
    }
    const std::string scenePath = InEvent.LoadedScene->FilePath.GetLocalPathString();
    Editor->SetWindowTitle( "Havana - " + ( scenePath.empty() ? std::string( "Untitled" ) : scenePath ) );
    if( m_isGameRunning )
    {
        GetEngine().GetWorld().lock()->Start();
    }
}

#endif
