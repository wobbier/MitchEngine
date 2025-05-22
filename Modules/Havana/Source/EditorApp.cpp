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
#include <Core/JobQueueOld.h>
#include <Math/Quaternion.h>
#include "Events/HavanaEvents.h"
#include <Utils/EditorConfig.h>

#include "Editor/EditorComponentInfoCache.h"
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

    Editor->NewFrame();
    Transform* root = GetEngine().SceneNodes->GetRootTransform();

    EditorSceneManager->Update( inUpdateContext, root );
    Editor->UpdateWorld( root, EditorSceneManager->GetEntities() );

    UpdateCameras();
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
}


void EditorApp::OnInitialize()
{
    if( !Editor )
    {
        EditorConfig::GetInstance().Init();
        EditorConfig::GetInstance().Load();
        InitialLevel = GetEngine().GetConfig().GetValue( "CurrentScene" );
        Editor = MakeUnique<Havana>( &GetEngine(), this );
        EditorSceneManager = ME_NEW EditorCore( Editor.get() );

        Editor->SetGameCallbacks( [this]()
            {
                StartGame();
                m_isGamePaused = false;
                m_isGameRunning = true;
                //Editor->SetViewportMode(ViewportMode::Game);
            }
            , [this]()
            {
                m_isGamePaused = true;
            }
            , [this]()
            {
                m_isGamePaused = false;
                //Editor->SetViewportMode(ViewportMode::World);
                ClearInspectEvent evt;
                evt.Fire();
                StopGame();
                //GetEngine().LoadScene("Assets/Alley.lvl");
            } );

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


void EditorApp::StartGame()
{
    if( !m_isGameRunning )
    {
        GetEngine().GetWorld().lock()->Start();
        m_isGameRunning = true;
    }
}


void EditorApp::StopGame()
{
    if( m_isGameRunning )
    {
        if( GetEngine().GetWorld().lock() )
        {
            GetEngine().GetWorld().lock()->Destroy();
        }
        m_isGameRunning = false;
        GetEngine().GetWorld().lock()->Stop();
        NewSceneEvent evt;
        evt.Fire();
        InitialLevel = GetEngine().GetConfig().GetValue( "CurrentScene" );
        GetEngine().LoadScene( InitialLevel );
    }
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
        GetEngine().LoadScene( "" );
        GetEngine().InitGame();
        GetEngine().GetWorld().lock()->Simulate();
    }
    else if( evt.GetEventId() == SceneLoadedEvent::GetEventId() )
    {
        const SceneLoadedEvent& test = static_cast<const SceneLoadedEvent&>( evt );

        Editor->SetWindowTitle( "Havana - " + test.LoadedScene->FilePath.GetLocalPathString() );
        if( m_isGameRunning )
        {
            GetEngine().GetWorld().lock()->Start();
        }
    }

    return false;
}

#endif
