#include "AssetPreviewWidget.h"
#include <HavanaEvents.h>
#include <optick.h>

AssetPreviewWidget::AssetPreviewWidget()
	: HavanaWidget("Preview")
{
	EventManager::GetInstance().RegisterReceiver(this, { PreviewResourceEvent::GetEventId() });
}

void AssetPreviewWidget::Init()
{
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
		return true;
	}
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

	ImGui::Begin("Preview");
	{
		if (ViewTexture)
		{
			//if (ViewTexture->TexHandle != bgfx::kInvalidHandle)
			{
				OPTICK_CATEGORY("Preview Texture", Optick::Category::Debug);
				// Get the current cursor position (where your window is)
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImVec2 maxPos = ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y);
				Vector2 RenderSize = Vector2(ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

				// Ask ImGui to draw it as an image:
				// Under OpenGL the ImGUI image type is GLuint
				// So make sure to use "(void *)tex" but not "&tex"
				/*ImGui::GetWindowDrawList()->AddImage(
					(void*)ViewTexture->TexHandle,
					ImVec2(pos.x, pos.y),
					ImVec2(maxPos));*/
					//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

			}
		}
	}
	ImGui::End();
}

