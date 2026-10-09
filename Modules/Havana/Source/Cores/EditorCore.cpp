#include <Cores/EditorCore.h>

#include <Components/Camera.h>
#include <Components/Transform.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Events/SceneEvents.h>
#include <Havana.h>
#include <Events/HavanaEvents.h>
#include <optick.h>
#include <World/Scene.h>
#include <Utils/EditorConfig.h>
#include "Events/EditorEvents.h"
#include "Editor/Selection.h"
#include "Editor/UndoStack.h"
#include "Editor/EditorActions.h"
#include "Editor/SceneTools.h"
#include "Widgets/SceneViewWidget.h"
#include <imgui.h>

#if USING( ME_EDITOR )

EditorCore::EditorCore( Havana* editor )
    : Base( ComponentFilter().Excludes<Transform>() )
    , m_editor( editor )
{
    SetIsSerializable( false );

    EditorCameraTransform = MakeShared<Transform>();
    EditorCamera = new Camera();

    EventManager::GetInstance().RegisterReceiver( this, { SaveSceneEvent::GetEventId() } );
    RegisterViewActions();
}


EditorCore::~EditorCore()
{
}


void EditorCore::Init()
{
    Camera::EditorCamera = EditorCamera;
}


void EditorCore::RegisterViewActions()
{
    EditorActions& actions = EditorActions::Get();
    auto view = [&actions]( const char* id, const char* name, ImGuiKeyChord key, std::function<void()> execute ) {
        EditorAction action{ id, name, "Scene View", key, 0, std::move( execute ) };
        action.Context = ActionContext::Scene;
        actions.Register( action );
    };
    view( "View.FrameSelected", "Frame Selected", ImGuiKey_F, [this]() { FrameSelection(); } );
    view( "View.Perspective", "Toggle Perspective/Orthographic", ImGuiKey_Keypad5, [this]() { m_camera.SetOrthographic( !m_camera.IsOrthographic() ); } );
    view( "View.Top", "Top View", ImGuiKey_Keypad7, [this]() { m_camera.SetAngles( 89.9f, 0.f ); } );
    view( "View.Front", "Front View", ImGuiKey_Keypad1, [this]() { m_camera.SetAngles( 0.f, 0.f ); } );
    view( "View.Right", "Right View", ImGuiKey_Keypad3, [this]() { m_camera.SetAngles( 0.f, -90.f ); } );
}


void EditorCore::FrameSelection()
{
    AABB bounds = SceneTools::ComputeSelectionBounds();
    if( !bounds.IsValid() && RootTransform && RootTransform->Parent )
    {
        bounds = SceneTools::ComputeWorldBounds( *RootTransform->Parent.Get(), true );
    }
    m_camera.Focus( bounds );
}


void EditorCore::SaveCameraState()
{
    EditorConfig& config = EditorConfig::GetInstance();
    config.CameraPosition = EditorCameraTransform->GetPosition();
    config.CameraRotation = Vector3( m_camera.GetPitch(), m_camera.GetYaw(), 0.f );
    config.SetPreference( "Camera.FlySpeed", m_camera.GetFlySpeed() );
}


void EditorCore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "EditorCore::Update", Optick::Category::UI );

    if( FirstUpdate )
    {
        EditorConfig& config = EditorConfig::GetInstance();
        EditorCameraTransform->SetPosition( config.CameraPosition );
        EditorCameraTransform->SetRotation( config.CameraRotation );
        m_camera.Init( *EditorCameraTransform, *EditorCamera );
        m_camera.SetFlySpeed( config.GetPreference<float>( "Camera.FlySpeed", 10.f ) );
        FirstUpdate = false;
    }

    SceneViewWidget* sceneView = m_editor ? m_editor->GetSceneView() : nullptr;
    const ImGuiIO& io = ImGui::GetIO();
    EditorCameraController::Input input;
    if( sceneView )
    {
        input.ViewportHovered = sceneView->IsViewportHovered && !sceneView->IsUsingGuizmo;
        input.ViewportFocused = sceneView->IsFocused;
        input.ViewportSize = sceneView->SceneViewRenderSize;
    }
    input.MouseDelta = Vector2( io.MouseDelta.x, io.MouseDelta.y );
    input.Wheel = input.ViewportHovered || m_camera.IsNavigating() ? io.MouseWheel : 0.f;
    input.Left = ImGui::IsMouseDown( ImGuiMouseButton_Left );
    input.Middle = ImGui::IsMouseDown( ImGuiMouseButton_Middle );
    input.Right = ImGui::IsMouseDown( ImGuiMouseButton_Right );
    input.LeftPressed = ImGui::IsMouseClicked( ImGuiMouseButton_Left );
    input.MiddlePressed = ImGui::IsMouseClicked( ImGuiMouseButton_Middle );
    input.RightPressed = ImGui::IsMouseClicked( ImGuiMouseButton_Right );
    input.Alt = io.KeyAlt;
    input.Shift = io.KeyShift;
    if( !io.WantTextInput )
    {
        input.Forward = ImGui::IsKeyDown( ImGuiKey_W );
        input.Back = ImGui::IsKeyDown( ImGuiKey_S );
        input.StrafeLeft = ImGui::IsKeyDown( ImGuiKey_A );
        input.StrafeRight = ImGui::IsKeyDown( ImGuiKey_D );
        input.Up = ImGui::IsKeyDown( ImGuiKey_E ) || ImGui::IsKeyDown( ImGuiKey_Space );
        input.Down = ImGui::IsKeyDown( ImGuiKey_Q );
    }
    m_camera.Update( inUpdateContext.GetUnscaledDeltaTime(), input );

    // Persist the view now and then (and on navigation end) rather than every frame.
    m_saveTimer += inUpdateContext.GetUnscaledDeltaTime();
    if( m_saveTimer > 1.f )
    {
        m_saveTimer = 0.f;
        SaveCameraState();
    }

    EditorConfig& config = EditorConfig::GetInstance();
    SceneTools::OverlaySettings overlays;
    overlays.ShowGrid = config.GetPreference<bool>( "Scene.ShowGrid", true );
    overlays.ShowGizmos = config.GetPreference<bool>( "Scene.ShowGizmos", true );
    overlays.ShowSelection = config.GetPreference<bool>( "Scene.ShowSelection", true );
    SceneTools::DrawOverlays( EditorCameraTransform->GetPosition(), overlays );
}


void EditorCore::Update( const UpdateContext& inUpdateContext, Transform* rootTransform )
{
    OPTICK_EVENT( "EditorCore::Update" );

    RootTransform = rootTransform;

    Update( inUpdateContext );
}


bool EditorCore::OnEvent( const BaseEvent& evt )
{
    if( evt.GetEventId() == SaveSceneEvent::GetEventId() )
    {
        const SaveSceneEvent& event = static_cast<const SaveSceneEvent&>( evt );
        if( GetEngine().CurrentScene->IsNewScene() || event.SaveAs )
        {
            RequestAssetSelectionEvent evt( [this]( const Path& inPath ) {
                GetEngine().CurrentScene->Save( inPath.GetLocalPath().data(), RootTransform );
                UndoStack::Get().MarkSaved();
                GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), inPath.GetLocalPath().data() );
                GetEngine().GetConfig().Save();
                EditorConfig::GetInstance().AddRecentScene( std::string( inPath.GetLocalPath() ) );
                }, AssetType::Level, true );
            evt.Fire();
        }
        else
        {
            GetEngine().CurrentScene->Save( GetEngine().CurrentScene->FilePath.GetLocalPath().data(), RootTransform );
            UndoStack::Get().MarkSaved();
            GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), GetEngine().CurrentScene->FilePath.GetLocalPath().data() );
            GetEngine().GetConfig().Save();
        }
        return true;
    }
    return false;
}


Havana* EditorCore::GetEditor() const
{
    return m_editor;
}


SharedPtr<Transform> EditorCore::GetEditorCameraTransform() const
{
    return EditorCameraTransform;
}


void EditorCore::OnEditorInspect()
{
    BaseCore::OnEditorInspect();

    ImGui::Text( "Scene Camera" );
    float speed = m_camera.GetFlySpeed();
    if( ImGui::DragFloat( "Fly Speed", &speed, 0.1f, 0.1f, 500.f ) )
    {
        m_camera.SetFlySpeed( speed );
    }
    ImGui::DragFloat( "Look Sensitivity", &m_camera.LookSensitivity, 0.01f, 0.01f, 2.f );
    ImGui::DragFloat( "Focus Smoothing", &m_camera.FocusSmoothing, 0.1f, 1.f, 60.f );

    EditorCamera->OnEditorInspect();
}

#endif
