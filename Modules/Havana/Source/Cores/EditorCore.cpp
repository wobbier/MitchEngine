#include <Cores/EditorCore.h>

#include <Components/Camera.h>
#include <Components/Graphics/Mesh.h>
#include <Components/Graphics/Model.h>
#include <Components/Transform.h>
#include <Cores/Rendering/RenderCore.h>
#include <Engine/Engine.h>
#include <Events/SceneEvents.h>
#include <Havana.h>
#include <Events/HavanaEvents.h>
#include <Mathf.h>
#include <optick.h>
#include <Window/IWindow.h>
#include <World/Scene.h>
#include <Utils/EditorConfig.h>
#include "Events/EditorEvents.h"

#if USING( ME_EDITOR )

EditorCore::EditorCore( Havana* editor )
    : Base( ComponentFilter().Excludes<Transform>() )
    , m_editor( editor )
{
    SetIsSerializable( false );

    EditorCameraTransform = MakeShared<Transform>();
    EditorCamera = new Camera();

    std::vector<TypeId> events;
    events.push_back( SaveSceneEvent::GetEventId() );
    events.push_back( NewSceneEvent::GetEventId() );
    events.push_back( InspectEvent::GetEventId() );
    //events.push_back(Moonlight::PickingEvent::GetEventId());
    EventManager::GetInstance().RegisterReceiver( this, events );
}

EditorCore::~EditorCore()
{
}

void EditorCore::Init()
{
    Camera::EditorCamera = EditorCamera;

    EditorCameraTransform->SetWorldPosition( EditorConfig::GetInstance().CameraPosition );
    EditorCameraTransform->SetRotation( EditorConfig::GetInstance().CameraRotation );
}

void EditorCore::Update( const UpdateContext& inUpdateContext )
{
    OPTICK_CATEGORY( "EditorCore::Update", Optick::Category::UI );

    Input& input = m_editor->GetInput();

    if ( m_editor->IsWorldViewFocused() || FirstUpdate )
    {
        if ( input.IsKeyDown( KeyCode::F ) && FocusedTransform.lock() )
        {
            IsFocusingTransform = true;

            // Keep a note of the time the movement started.
            startTime = inUpdateContext.GetTotalTime();

            // Calculate the journey length.
            FocusPositionStart = EditorCameraTransform->GetWorldPosition();
            FocusPositionEnd = FocusedTransform.lock()->GetWorldPosition();

            totalTime = 0.f;
        }

        if ( FirstUpdate )
        {
            Vector3& rot = EditorConfig::GetInstance().CameraRotation;
            EditorCameraTransform->SetPosition( EditorConfig::GetInstance().CameraPosition );
            EditorCameraTransform->SetRotation( rot );
            EditorCamera->Yaw = -rot.y;
            EditorCamera->Pitch = rot.x;
            //EditorConfig::GetInstance().CameraPosition = Vector3(EditorCameraTransform->GetPosition());
            //EditorConfig::GetInstance().CameraRotation = Vector3(EditorCameraTransform->GetRotationEuler());
            FirstUpdate = false;
            return;
        }

        if ( !IsFocusingTransform )
        {
            if ( !input.IsMouseButtonDown( MouseButton::Right ) )
            {
                return;
            }

            m_flyingSpeed = Mathf::Clamp(1.f, 100.f, m_flyingSpeed + input.GetMouseScrollDelta().y);
            float CameraSpeed = m_flyingSpeed;
            if ( input.IsKeyDown( KeyCode::LeftShift ) )
            {
                CameraSpeed = m_flyingSpeed * m_speedModifier;
            }
            CameraSpeed *= inUpdateContext.GetDeltaTime();

            const Vector3& Front = EditorCameraTransform->Front();

            if ( input.IsKeyDown( KeyCode::W ) )
            {
                EditorCameraTransform->Translate( Front * CameraSpeed );
            }
            if ( input.IsKeyDown( KeyCode::S ) )
            {
                EditorCameraTransform->Translate( ( Front * CameraSpeed ) * -1.f );
            }
            if ( input.IsKeyDown( KeyCode::A ) )
            {
                EditorCameraTransform->Translate( -EditorCameraTransform->Right() * CameraSpeed );
            }
            if ( input.IsKeyDown( KeyCode::D ) )
            {
                EditorCameraTransform->Translate( EditorCameraTransform->Right() * CameraSpeed );
            }
            if ( input.IsKeyDown( KeyCode::Space ) )
            {
                EditorCameraTransform->Translate( Vector3::Up * CameraSpeed );
            }
            if ( input.IsKeyDown( KeyCode::E ) )
            {
                EditorCameraTransform->Translate( EditorCameraTransform->Up() * CameraSpeed );
            }
            if ( input.IsKeyDown( KeyCode::Q ) )
            {
                EditorCameraTransform->Translate( -EditorCameraTransform->Up() * CameraSpeed );
            }

            Vector2 currentState = input.GetRelativeMousePosition();
            float XOffset = ( ( currentState.x ) * m_lookSensitivity ) * inUpdateContext.GetDeltaTime();
            float YOffest = ( ( currentState.y ) * m_lookSensitivity ) * inUpdateContext.GetDeltaTime();

            const float Yaw = EditorCamera->Yaw -= XOffset;
            float Pitch = EditorCamera->Pitch + YOffest;

            Pitch = Mathf::Clamp( -89.f, 89.f, Pitch );

            EditorCamera->Pitch = Pitch;
            EditorCameraTransform->SetRotation( Vector3( Pitch, -Yaw, 0.0f ) );
            EditorConfig::GetInstance().CameraPosition = Vector3( EditorCameraTransform->GetPosition() );
            EditorConfig::GetInstance().CameraRotation = Vector3( EditorCameraTransform->GetRotationEuler() );
        }
        else
        {
            totalTime += inUpdateContext.GetDeltaTime();

            float fracJourney = totalTime / m_focusDuration;
            if( fracJourney >= 1.f )
            {
                EditorCameraTransform->SetPosition( FocusPositionEnd );
                IsFocusingTransform = false;
            }
            else
            {
                Vector3 lerp = Mathf::Lerp( FocusPositionStart, FocusPositionEnd, fracJourney );
                EditorCameraTransform->SetPosition( lerp );
            }
        }
    }
}

void EditorCore::Update( const UpdateContext& inUpdateContext, Transform* rootTransform )
{
    OPTICK_EVENT( "EditorCore::Update" );

    RootTransform = rootTransform;

    Update( inUpdateContext );
}

bool EditorCore::OnEvent( const BaseEvent& evt )
{
    if ( evt.GetEventId() == SaveSceneEvent::GetEventId() )
    {
        const SaveSceneEvent& event = static_cast<const SaveSceneEvent&>( evt );
        if ( GetEngine().CurrentScene->IsNewScene() || event.SaveAs )
        {
            RequestAssetSelectionEvent evt( [this]( const Path& inPath ) {
                GetEngine().CurrentScene->Save( inPath.GetLocalPath().data(), RootTransform );
                GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), inPath.GetLocalPath().data() );
                GetEngine().GetConfig().Save();
                }, AssetType::Level, true );
            evt.Fire();
        }
        else
        {
            GetEngine().CurrentScene->Save( GetEngine().CurrentScene->FilePath.GetLocalPath().data(), RootTransform );
            GetEngine().GetConfig().SetValue( std::string( "CurrentScene" ), GetEngine().CurrentScene->FilePath.GetLocalPath().data() );
            GetEngine().GetConfig().Save();
        }
        return true;
    }
    if( evt.GetEventId() == InspectEvent::GetEventId() )
    {
        const InspectEvent& event = static_cast<const InspectEvent&>( evt );

        FocusedTransform = event.SelectedTransform;
    }
    /*else if (evt.GetEventId() == Moonlight::PickingEvent::GetEventId())
    {
        const Moonlight::PickingEvent& casted = static_cast<const Moonlight::PickingEvent&>(evt);

        auto ents = GetEngine().GetWorld().lock()->GetCore(RenderCore::GetTypeId())->GetEntities();
        for (auto& ent : ents)
        {
            if (!ent.HasComponent<Mesh>())
            {
                continue;
            }
            if (ent.GetComponent<Mesh>().Id == casted.Id)
            {
                Transform* meshTransform = &ent.GetComponent<Transform>();
                std::stack<Transform*> parentEnts;
                parentEnts.push(meshTransform->GetParent());

                static Transform* selectedParentObjec = nullptr;

                while (parentEnts.size() > 0)
                {
                    Transform* parent = parentEnts.top();
                    parentEnts.pop();
                    if (!parent)
                    {
                        continue;
                    }

                    if (parent->Parent->HasComponent<Model>())
                    {
                        Transform* selectedModel = &parent->Parent->GetComponent<Transform>();
                        if (m_editor->SelectedTransform == nullptr || selectedParentObjec != selectedModel)
                        {
                            m_editor->SelectedTransform = selectedModel;
                            selectedParentObjec = selectedModel;
                            break;
                        }

                        if (meshTransform != m_editor->SelectedTransform)
                        {
                            m_editor->SelectedTransform = meshTransform;
                        }
                        else
                        {
                            m_editor->SelectedTransform = selectedModel;
                        }
                        break;
                    }
                    else
                    {
                        parentEnts.push(parent->GetParent());
                    }
                }
            }
        }
        return true;
    }*/

    return false;
}

void EditorCore::OnEntityAdded( Entity& NewEntity )
{
    Base::OnEntityAdded( NewEntity );
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

    ImGui::Text( "Camera Settings" );
    ImGui::DragFloat( "Flying Speed", &m_flyingSpeed );
    ImGui::DragFloat( "Speed Modifier", &m_speedModifier );
    ImGui::DragFloat( "Focus Duration", &m_focusDuration );
    ImGui::DragFloat( "Look Sensitivity", &m_lookSensitivity, 0.01f );

    EditorCameraTransform->OnEditorInspect();

    EditorCamera->OnEditorInspect();
}

#endif
