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

    EditorCamera.Position = EditorSceneManager->GetEditorCameraTransform()->GetPosition();
    EditorCamera.Front = EditorSceneManager->GetEditorCameraTransform()->Front();
    EditorCamera.Up = EditorSceneManager->GetEditorCameraTransform()->Up();
    EditorCamera.OutputSize = Editor->GetWorldEditorRenderSize();
    EditorCamera.FOV = Camera::EditorCamera->GetFOV();
    EditorCamera.Near = Camera::EditorCamera->Near;
    EditorCamera.Far = Camera::EditorCamera->Far;
    EditorCamera.Skybox = Camera::CurrentCamera->Skybox;
    EditorCamera.ClearColor = Camera::CurrentCamera->ClearColor;
    EditorCamera.ClearType = Camera::CurrentCamera->ClearType;
    EditorCamera.Projection = Camera::EditorCamera->Projection;
    EditorCamera.OrthographicSize = Camera::EditorCamera->OrthographicSize;
    EditorCamera.ShouldCull = false;

    // #TODO: Clean this up
    Vector3 eye = { EditorCamera.Position.x, EditorCamera.Position.y, EditorCamera.Position.z };
    Vector3 at = { EditorCamera.Position.x + EditorCamera.Front.x, EditorCamera.Position.y + EditorCamera.Front.y, EditorCamera.Position.z + EditorCamera.Front.z };
    Vector3 up = { EditorCamera.Up.x, EditorCamera.Up.y, EditorCamera.Up.z };

    EditorCamera.View = glm::lookAtLH( eye.InternalVector, at.InternalVector, up.InternalVector );
    Frustum& camFrustum = Camera::EditorCamera->CameraFrustum;


    Matrix4 testMatrix;
    bx::mtxProj( &testMatrix.GetInternalMatrix()[0][0], EditorCamera.FOV, float( EditorCamera.OutputSize.x ) / float( EditorCamera.OutputSize.y ), std::max( EditorCamera.Near, 0.01f ), EditorCamera.Far, bgfx::getCaps()->homogeneousDepth );
    camFrustum.Update( testMatrix, EditorCamera.View, EditorCamera.FOV, EditorCamera.OutputSize, EditorCamera.Near, EditorCamera.Far );
    EditorCamera.ProjectionMatrix = testMatrix;

    EditorCamera.ViewFrustum = Camera::EditorCamera->CameraFrustum;

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
        EditorSceneManager = new EditorCore( Editor.get() );

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
