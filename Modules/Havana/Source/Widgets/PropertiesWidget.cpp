#include "PropertiesWidget.h"
#include <optick.h>
#include <ECS/Component.h>
#include <HavanaEvents.h>
#include <Engine/Engine.h>
#include <Engine/World.h>
#include <Utils/CommonUtils.h>

#if ME_EDITOR

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
		AssetBrowserPath = Path(event.AssetBrowserPath);
		if (AssetBrowserPath.Exists)
		{
			SharedPtr<Resource> res = ResourceCache::GetInstance().GetCached(AssetBrowserPath);
			if (res)
			{
				metafile = res->GetMetadata();
			}
			else
			{
				metafile = ResourceCache::GetInstance().LoadMetadata(AssetBrowserPath);
			}
		}
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

		if (metafile)
		{
			metafile->OnEditorInspect();

			if (ImGui::Button("Save"))
			{
				metafile->Save();
				metafile->Export();

				SharedPtr<Resource> res = ResourceCache::GetInstance().GetCached(metafile->FilePath);
				if (res)
				{
					res->Reload();
				}
			}
		}
	}
	ImGui::End();
}

void PropertiesWidget::ClearSelection()
{
	SelectedTransform = nullptr;
	SelectedEntity = EntityHandle();
	SelectedCore = nullptr;
	AssetBrowserPath = Path();
	delete metafile;
	metafile = nullptr;
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

#endif