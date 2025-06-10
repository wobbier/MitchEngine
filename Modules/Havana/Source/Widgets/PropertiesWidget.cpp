#include "PropertiesWidget.h"
#include <optick.h>
#include <ECS/Component.h>
#include <Events/HavanaEvents.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Utils/CommonUtils.h>
#include <Events/SceneEvents.h>
#include "Editor/EditorComponentInfoCache.h"
#include <Utils/HavanaUtils.h>

#if USING( ME_EDITOR )

PropertiesWidget::PropertiesWidget()
	: HavanaWidget("Properties")
{
	EventManager::GetInstance().RegisterReceiver(this, { InspectEvent::GetEventId(), ClearInspectEvent::GetEventId() });
}

void PropertiesWidget::Init()
{
}

void PropertiesWidget::Destroy()
{
}

bool PropertiesWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == ClearInspectEvent::GetEventId() || evt.GetEventId() == NewSceneEvent::GetEventId())
	{
		ClearSelection();
	}
	else if (evt.GetEventId() == InspectEvent::GetEventId())
	{
		const InspectEvent& event = static_cast<const InspectEvent&>(evt);

		ClearSelection();

		SelectedCore = event.SelectedCore;
		SelectedEntity = event.SelectedEntity;
		SelectedTransform = event.SelectedTransform;
		m_isDirty = true;
	}
	return false;
}

void PropertiesWidget::Update()
{
}

void PropertiesWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });
	ImGui::Begin("Properties", &IsOpen);
	ImGui::PopStyleVar();
	{
		EntityHandle entity = SelectedEntity;
		if (SelectedTransform.lock())
		{
			entity = SelectedTransform.lock()->Parent;
		}

		if (entity)
		{
			OPTICK_CATEGORY("Inspect Entity", Optick::Category::Debug);
            if( ImGui::CollapsingHeader( "Entity Properties", nullptr, ImGuiTreeNodeFlags_None) )
            {
                ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, ImGui::GetStyle().ItemSpacing.y } );
                ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 20.f, 0.f } );
                ImGui::BeginGroup();

				HavanaUtils::Label( "Active" );
				bool isActive = GetEngine().GetWorld().lock()->IsActive( *entity.Get() );
				if( ImGui::Checkbox( "##Active", &isActive ) )
				{
					entity->SetActive( isActive );
                }

                entity->OnEditorInspect();

                ImGui::EndGroup();
                ImGui::PopStyleVar( 2 );
                ImGui::Text( "\n" );
            }

			const auto componentList = entity->GetAllComponents();
            // Cache the component pointers if we've modified our components or we've changed the focused GameObject.
            if( m_isDirty || entity->GetAllComponents().size() != m_components.size() )
            {
                m_components.reserve( componentList.size() );
                m_components.assign( componentList.begin(), componentList.end() );

                if( m_components.size() > 1 )
                {
                    const EditorComponentCache::ComponentInfoMap& componentData = EditorComponentCache::GetAllComponentsInfo();

                    std::sort( m_components.begin(), m_components.end(), [&componentData]( const BaseComponent* a, const BaseComponent* b ) {
                        int32_t first = EditorComponentCache::kDefaultSortingOrder;
                        int32_t second = EditorComponentCache::kDefaultSortingOrder;
                        bool foundFirst = componentData.find( a->GetTypeId() ) != componentData.end();
                        if( foundFirst )
                        {
                            first = componentData.at( a->GetTypeId() ).Order;
                        }

                        bool foundSecond = componentData.find( b->GetTypeId() ) != componentData.end();
                        if( foundSecond )
                        {
                            second = componentData.at( b->GetTypeId() ).Order;
                        }

                        return first > second;
                        } );
                }

                m_isDirty = false;
            }

			for (BaseComponent* comp : m_components )
			{
				DrawComponentProperties(comp, entity);
			}
			AddComponentPopup(SelectedEntity);
		}

		if (SelectedCore != nullptr)
		{
			OPTICK_CATEGORY("Core::OnEditorInspect", Optick::Category::GameLogic);
			SelectedCore->OnEditorInspect();
		}
	}
	ImGui::End();
}

void PropertiesWidget::DrawComponentProperties(BaseComponent* comp, EntityHandle entity)
{
	bool shouldClose = true;
	if (ImGui::CollapsingHeader(comp->GetName().c_str(), &shouldClose, ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, ImGui::GetStyle().ItemSpacing.y });
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20.f, 0.f });
		ImGui::BeginGroup();

		comp->OnEditorInspect();
		ImGui::EndGroup();
		ImGui::PopStyleVar(2);
		ImGui::Text("\n");
	}
	if (!shouldClose)
	{
		entity->RemoveComponent(comp->GetName());
		GetEngine().GetWorld().lock()->Simulate();
	}
}

void PropertiesWidget::ClearSelection()
{
	SelectedTransform.reset();
	SelectedEntity = EntityHandle();
	SelectedCore = nullptr;
}

void PropertiesWidget::AddComponentPopup(EntityHandle inSelectedEntity)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {20.f, 10.f});
	if (ImGui::Button("Add Component..", {-1.f, 26.f}))
	{
		ImGui::OpenPopup("my_select_popup");
	}
	ImGui::PopStyleVar();

	if (ImGui::BeginPopup("my_select_popup"))
	{
		CommonUtils::DrawAddComponentList(inSelectedEntity);

		ImGui::EndPopup();
	}
}

#endif