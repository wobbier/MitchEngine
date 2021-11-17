#define SDL_MAIN_HANDLED
#include "Window/SDLWindow.h"
#include "BGFXRenderer.h"
#include <Camera/CameraData.h>
#include <Engine/Input.h>
#include "MitchHub.h"

int main(int argc, char** argv)
{
	BGFXRenderer* Renderer = new BGFXRenderer();

	std::function<void(const Vector2&)> ResizeFunc = [Renderer](const Vector2& NewSize)
	{
		if (Renderer)
		{
			Renderer->WindowResized(NewSize);
		}
	};

	SDLWindow* win = new SDLWindow("lmao", ResizeFunc, 500, 300, Vector2(1280, 720));

	RendererCreationSettings set;
	set.WindowPtr = win->GetWindowPtr();
	set.InitAssets = false;
	Renderer->Create(set);
	win->SetBorderless(true);
	Input input;
	MitchHub hub(&input, win);

	while (!win->ShouldClose())
	{
		win->ParseMessageQueue();
		input.Update();
		Renderer->BeginFrame(input.GetMousePosition(), (input.IsMouseButtonDown(MouseButton::Left) ? 0x01 : 0)
			| (input.IsMouseButtonDown(MouseButton::Right) ? 0x02 : 0)
			| (input.IsMouseButtonDown(MouseButton::Middle) ? 0x04 : 0)
			, (int32_t)input.GetMouseScrollOffset().y
			, win->GetSize()
			, -1
			, 255);

		hub.Draw();

		Moonlight::CameraData cam;
		Renderer->Render(cam);
		input.PostUpdate();
	}

	Vector2 lmao;
	lmao.x = 2.f;
	return 0;
}