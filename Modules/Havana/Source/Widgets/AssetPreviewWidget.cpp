#include "AssetPreviewWidget.h"
#include <Events/HavanaEvents.h>
#include <optick.h>
#include "Graphics/Texture.h"
#include "Mathf.h"
#include <Utils/ImGuiUtils.h>
#include "Components/Camera.h"
#include "Events/Event.h"
#include "Events/EditorEvents.h"

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

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Preview", &IsOpen);
	if (ViewTexture)
	{
		SceneViewRenderLocation = Vector2(ImGui::GetCursorPos().x, ImGui::GetCursorPos().y);

		Vector2 availableSpace = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y - SceneViewRenderLocation.y);
		Vector2 viewportRenderSize = availableSpace;
		float scale = 1.f;

		SceneViewRenderSize = {ViewTexture->mWidth, ViewTexture->mHeight};
		Vector2 calculatedImageSize = viewportRenderSize;

		// rs > ri ? (wi * hs/hi, hs) : (ws, hi * ws/wi)

		float ri = SceneViewRenderSize.x / SceneViewRenderSize.y;
		float rs = availableSpace.x / availableSpace.y;

		if (rs > ri)
		{
			calculatedImageSize.x = SceneViewRenderSize.x * availableSpace.y / SceneViewRenderSize.y;
		}
		else
		{
			calculatedImageSize.y = SceneViewRenderSize.y * availableSpace.x / SceneViewRenderSize.x;
		}

		calculatedImageSize.x = Mathf::Clamp(0.f, SceneViewRenderSize.x, calculatedImageSize.x);
		calculatedImageSize.y = std::min(SceneViewRenderSize.y, calculatedImageSize.y);
		Vector2 OGCursorPos = SceneViewRenderLocation;
		SceneViewRenderLocation.x += (viewportRenderSize.x - calculatedImageSize.x) / 2.f;
		SceneViewRenderLocation.y += (viewportRenderSize.y - calculatedImageSize.y) / 2.f;
		viewportRenderSize = calculatedImageSize;


		//Moonlight::FrameBuffer* currentView = (MainCamera) ? MainCamera->Buffer : nullptr;

		if (bgfx::isValid(ViewTexture->TexHandle))
		{
			ImGui::SetCursorPos(ImVec2(SceneViewRenderLocation.x, SceneViewRenderLocation.y));
			ImVec2 maxPos = ImVec2(SceneViewRenderLocation.x + ImGui::GetWindowSize().x, SceneViewRenderLocation.y + ImGui::GetWindowSize().y);

			ImGui::Image(ViewTexture->TexHandle,
				ImVec2(viewportRenderSize.x * scale, (viewportRenderSize.y * scale)),
				ImVec2(0, 0),
				ImVec2(Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.x / ViewTexture->mWidth), Mathf::Clamp(0.f, 1.0f, SceneViewRenderSize.y / ViewTexture->mHeight)));

            ImGui::SetCursorPos(ImVec2(OGCursorPos.x, OGCursorPos.y));
            {
                char sizeString[256];
                sprintf(sizeString, "Texture Size: X: %f, Y: %f", SceneViewRenderSize.x, SceneViewRenderSize.y);
                ImGui::Text(sizeString);
            }
            {
                char sizeString[256];
                sprintf(sizeString, "View Size: X: %f, Y: %f", Camera::CurrentCamera->OutputSize.x, Camera::CurrentCamera->OutputSize.y);
                ImGui::Text(sizeString);
            }
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

