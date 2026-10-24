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
#include "Editor/EditorOperations.h"
#include "Editor/ReflectionUI.h"
#include "Widgets/SceneViewWidget.h"
#include <imgui.h>
#include <Utils/HavanaUtils.h>
#include <algorithm>
#include <Renderer.h>
#include <Cores/PhysicsCore.h>
#include <Cores/NavigationCore.h>
#include <Cores/AnimationCore.h>
#include <Components/Animation/Animator.h>

#if USING( ME_EDITOR )

EditorCore::EditorCore( Havana* editor )
    : Base( ComponentFilter().Excludes<Transform>() )
    , m_editor( editor )
{
    SetIsSerializable( false );

    EditorCameraTransform = MakeShared<Transform>();
    EditorCamera = new Camera();

    Listen<SaveSceneEvent>( [this]( const SaveSceneEvent& InEvent ) { OnSaveScene( InEvent ); return true; } );
    RegisterViewActions();
}


EditorCore::~EditorCore()
{
    delete EditorCamera;
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
    view( "View.AlignWithView", "Align With View", ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_F, [this]() { AlignSelectionWithView(); } );
    view( "View.Perspective", "Toggle Perspective/Orthographic", ImGuiKey_Keypad5, [this]() { m_camera.SetOrthographic( !m_camera.IsOrthographic() ); } );
    view( "View.Top", "Top View", ImGuiKey_Keypad7, [this]() { m_camera.SetAngles( 89.9f, 0.f ); } );
    view( "View.Front", "Front View", ImGuiKey_Keypad1, [this]() { m_camera.SetAngles( 0.f, 0.f ); } );
    view( "View.Right", "Right View", ImGuiKey_Keypad3, [this]() { m_camera.SetAngles( 0.f, -90.f ); } );
    view( "View.Shadows", "Toggle Shadows", 0, []() { BGFXRenderer& renderer = GetEngine().GetRenderer(); renderer.Shadows.Enabled = !renderer.Shadows.Enabled; } );
    view( "View.ShadowCascades", "Show Shadow Cascades", 0, []() { BGFXRenderer& renderer = GetEngine().GetRenderer(); renderer.Shadows.DebugCascades = !renderer.Shadows.DebugCascades; } );
    view( "View.Physics", "Show Physics Shapes", 0, []() { PhysicsCore::DebugDrawEnabled = !PhysicsCore::DebugDrawEnabled; } );
    view( "View.Navigation", "Show Navigation", 0, []() { NavigationCore::DebugDrawEnabled = !NavigationCore::DebugDrawEnabled; } );
    EditorAction bake{ "Navigation.BakeAll", "Bake All NavMeshes", "Navigation", 0, 0, []() { if( NavigationCore* navigation = GetEngine().Navigation ) { navigation->BakeAll( true ); } } };
    actions.Register( bake );
    EditorAction cameraSettings{ "View.CameraSettings", "Scene Camera Settings", "Scene View", 0, 0, [this]() {
        Selection::Get().SetCore( this );
        if( m_editor ) m_editor->ShowWidget( "Properties" );
    } };
    actions.Register( cameraSettings );
    // Animation previews (the Animator inspector has the same controls).
    EditorAction preview{ "Animation.PreviewSelected", "Preview Selected Animator", "Animation", 0, 0, []() {
        EntityHandle selected = Selection::Get().GetActive();
        if( Animator* animator = selected ? selected->TryGetComponent<Animator>() : nullptr )
        {
            animator->StartPreview( animator->DefaultState );
        }
    } };
    actions.Register( preview );
    EditorAction stopPreviews{ "Animation.StopPreviews", "Stop Animation Previews", "Animation", 0, 0, []() { if( AnimationCore* animation = GetEngine().Animation ) { animation->StopAllPreviews(); } } };
    actions.Register( stopPreviews );
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


void EditorCore::AlignSelectionWithView()
{
    const std::vector<Entity*> roots = Selection::Get().GetRootEntities();
    if( roots.empty() )
    {
        return;
    }
    const Vector3 position = EditorCameraTransform->GetPosition();
    const Quaternion rotation = EditorCameraTransform->GetRotation();
    UndoTransaction transaction( "Align With View" );
    for( Entity* entity : roots )
    {
        Transform* transform = entity ? entity->TryGetComponent<Transform>() : nullptr;
        if( !transform )
        {
            continue;
        }
        const json before = EditorOps::CaptureComponent( *entity, "Transform" );
        transform->SetWorldPosition( position );
        transform->SetWorldRotation( rotation );
        EditorOps::RecordComponentEdit( *entity, "Transform", before, EditorOps::CaptureComponent( *entity, "Transform" ), "Align With View" );
    }
}


void EditorCore::SaveCameraState()
{
    EditorConfig& config = EditorConfig::GetInstance();
    config.CameraPosition = EditorCameraTransform->GetPosition();
    config.CameraRotation = Vector3( m_camera.GetPitch(), m_camera.GetYaw(), 0.f );
    config.SetPreference( "Camera.FlySpeed", m_camera.GetFlySpeed() );
    config.SetPreference( "Camera.LookSensitivity", m_camera.LookSensitivity );
    config.SetPreference( "Camera.FocusSmoothing", m_camera.FocusSmoothing );
    config.SetPreference( "Camera.UseSceneEnvironment", m_useSceneEnvironment );
    json camera;
    EditorCamera->Serialize( camera );
    // This camera is editor state and must never replace a scene's active camera on load.
    camera.erase( "IsCurrent" );
    config.SetPreference( "Camera.Settings", camera );
}


void EditorCore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "EditorCore::Update", Optick::Category::UI );

    if( FirstUpdate )
    {
        EditorConfig& config = EditorConfig::GetInstance();
        EditorCameraTransform->SetPosition( config.CameraPosition );
        EditorCameraTransform->SetRotation( config.CameraRotation );
        json camera = config.GetPreference<json>( "Camera.Settings", json::object() );
        if( camera.is_object() )
        {
            camera.erase( "IsCurrent" );
            EditorCamera->Deserialize( camera );
        }
        m_useSceneEnvironment = config.GetPreference<bool>( "Camera.UseSceneEnvironment", true );
        m_camera.Init( *EditorCameraTransform, *EditorCamera );
        m_camera.SetFlySpeed( config.GetPreference<float>( "Camera.FlySpeed", 10.f ) );
        m_camera.LookSensitivity = std::clamp( config.GetPreference<float>( "Camera.LookSensitivity", 0.15f ), 0.01f, 2.f );
        m_camera.FocusSmoothing = std::clamp( config.GetPreference<float>( "Camera.FocusSmoothing", 14.f ), 1.f, 60.f );
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
    if( sceneView && m_camera.IsNavigating() )
    {
        sceneView->WrapNavigationCursor();
    }

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


void EditorCore::OnSaveScene( const SaveSceneEvent& event )
{
    if( GetEngine().CurrentScene->IsNewScene() || event.SaveAs )
    {
        RequestAssetSelectionEvent evt( [this]( const Path& inPath ) {
            GetEngine().CurrentScene->Save( inPath.GetLocalPath().data(), RootTransform );
            UndoStack::Get().MarkSaved();
            GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), inPath.GetLocalPath().data() );
            GetEngine().GetConfig().SaveUserState();
            EditorConfig::GetInstance().AddRecentScene( std::string( inPath.GetLocalPath() ) );
            }, AssetType::Level, true );
        evt.Fire();
    }
    else
    {
        GetEngine().CurrentScene->Save( GetEngine().CurrentScene->FilePath.GetLocalPath().data(), RootTransform );
        UndoStack::Get().MarkSaved();
        GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), GetEngine().CurrentScene->FilePath.GetLocalPath().data() );
        GetEngine().GetConfig().SaveUserState();
    }
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
    ImGui::TextWrapped( "Distances are metres. These settings save with your editor preferences." );
    ImGui::SeparatorText( "Navigation" );
    Vector3 position = EditorCameraTransform->GetPosition();
    Vector2 angles( m_camera.GetPitch(), m_camera.GetYaw() );
    HavanaUtils::Label( "Position (m)" );
    bool poseChanged = HavanaUtils::DragAxes( "##Position", &position.x, 3, 0.1f, "%.3f" );
    HavanaUtils::Label( "Pitch / Yaw (deg)" );
    poseChanged |= HavanaUtils::DragAxes( "##Angles", &angles.x, 2, 0.5f, "%.2f" );
    if( poseChanged ) m_camera.SetPose( position, angles.x, angles.y );
    float speed = m_camera.GetFlySpeed();
    HavanaUtils::Label( "Fly Speed (m/s)" );
    if( ImGui::DragFloat( "##FlySpeed", &speed, 0.1f, 0.1f, 500.f ) )
    {
        m_camera.SetFlySpeed( speed );
    }
    HavanaUtils::Label( "Look Sensitivity" );
    ImGui::DragFloat( "##LookSensitivity", &m_camera.LookSensitivity, 0.01f, 0.01f, 2.f );
    HavanaUtils::Label( "Focus Smoothing" );
    ImGui::DragFloat( "##FocusSmoothing", &m_camera.FocusSmoothing, 0.1f, 1.f, 60.f );

    ImGui::SeparatorText( "Camera" );
    HavanaUtils::Label( "Use Scene Environment" );
    ImGui::Checkbox( "##UseSceneEnvironment", &m_useSceneEnvironment );
    if( m_useSceneEnvironment )
    {
        ImGui::TextWrapped( "Clear settings follow the main camera while Use Scene Environment is enabled." );
    }
    // This camera is outside the ECS component inspector, so draw its reflected fields here too.
    static const Reflection::TypeInfo lensSettings = []() {
        Reflection::TypeInfo type = Camera::StaticType();
        type.Fields.erase( std::remove_if( type.Fields.begin(), type.Fields.end(), []( const Reflection::FieldInfo& field ) {
            return field.Name == "ClearType" || field.Name == "ClearColor";
        } ), type.Fields.end() );
        return type;
    }();
    ReflectionUI::Context context;
    if( ReflectionUI::DrawType( m_useSceneEnvironment ? lensSettings : Camera::StaticType(), EditorCamera, context ) )
    {
        for( const std::string& field : context.ChangedFields ) EditorCamera->OnPropertyChanged( field );
    }
    if( !m_useSceneEnvironment ) EditorCamera->OnEditorInspect();
    SaveCameraState();
}

#endif
