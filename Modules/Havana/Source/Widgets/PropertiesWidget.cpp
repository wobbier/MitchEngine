#include "PropertiesWidget.h"
#include <optick.h>
#include <ECS/Component.h>
#include <HavanaEvents.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Utils/CommonUtils.h>

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
	if (evt.GetEventId() == ClearInspectEvent::GetEventId())
	{
		ClearSelection();
	}
	else if (evt.GetEventId() == InspectEvent::GetEventId())
	{
		const InspectEvent& event = static_cast<const InspectEvent&>(evt);

		SelectedCore = event.SelectedCore;
		SelectedEntity = event.SelectedEntity;
		SelectedTransform = event.SelectedTransform;
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

	ImGui::Begin("Properties", &IsOpen);
	{
		EntityHandle entity = SelectedEntity;
		if (SelectedTransform != nullptr)
		{
			entity = SelectedTransform->Parent;
		}

		if (entity)
		{
			OPTICK_CATEGORY("Inspect Entity", Optick::Category::Debug);
			entity->OnEditorInspect();
			for (BaseComponent* comp : entity->GetAllComponents())
			{
				bool shouldClose = true;
				if (ImGui::CollapsingHeader(comp->GetName().c_str(), &shouldClose, ImGuiTreeNodeFlags_DefaultOpen))
				{
					comp->OnEditorInspect();
				}
				if (!shouldClose)
				{
					entity->RemoveComponent(comp->GetName());
					GetEngine().GetWorld().lock()->Simulate();
				}
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

void PropertiesWidget::ClearSelection()
{
	SelectedTransform = nullptr;
	SelectedEntity = EntityHandle();
	SelectedCore = nullptr;
}

void PropertiesWidget::AddComponentPopup(EntityHandle inSelectedEntity)
{
	ImGui::PushItemWidth(-100);
	if (ImGui::Button("Add Component.."))
	{
		ImGui::OpenPopup("my_select_popup");
	}
	ImGui::PopItemWidth();

	if (ImGui::BeginPopup("my_select_popup"))
	{
		CommonUtils::DrawAddComponentList(inSelectedEntity);

		ImGui::EndPopup();
	}
}


