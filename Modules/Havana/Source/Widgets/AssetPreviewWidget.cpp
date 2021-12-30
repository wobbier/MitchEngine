#include "AssetPreviewWidget.h"
#include <Events/HavanaEvents.h>
#include <optick.h>
#include "Graphics/Texture.h"
#include "Mathf.h"
#include <Utils/ImGuiUtils.h>

AssetPreviewWidget::AssetPreviewWidget()
	: HavanaWidget("Preview")
{
	EventManager::GetInstance().RegisterReceiver(this, { PreviewResourceEvent::GetEventId() });
}

void AssetPreviewWidget::Init()
{
	IsOpen = false;
}

void AssetPreviewWidget::Destroy()
{
}

bool AssetPreviewWidget::OnEvent(const BaseEvent& evt)
{
	if (evt.GetEventId() == PreviewResourceEvent::GetEventId())
	{
		const PreviewResourceEvent& event = static_cast<const PreviewResourceEvent&>(evt);
		ViewTexture = event.Subject;
		IsOpen = true;
		return true;
	}
	return false;
}

void AssetPreviewWidget::Update()
{
}

void AssetPreviewWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}

	OPTICK_CATEGORY("Preview Texture", Optick::Category::Debug);

	ImGui::Begin("Preview", &IsOpen);
	if (ViewTexture)
	{
		ImGui::Image(ViewTexture->TexHandle, ImVec2(300, 300));
	}
	ImGui::End();
}

