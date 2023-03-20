#include "PropertiesWidget.h"
#include <optick.h>
#include <ECS/Component.h>
#include <Events/HavanaEvents.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Utils/CommonUtils.h>
#include <Events/SceneEvents.h>

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
			entity->OnEditorInspect();
			if (entity->HasComponent<Transform>())
			{
				SelectedTransform = entity->GetComponent<Transform>().shared_from_this();
				DrawComponentProperties(SelectedTransform.lock().get(), entity);
			}

			for (BaseComponent* comp : entity->GetAllComponents())
			{
				if (comp != SelectedTransform.lock().get())
				{
					DrawComponentProperties(comp, entity);
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