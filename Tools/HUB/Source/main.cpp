#define SDL_MAIN_HANDLED
#include "Window/SDLWindow.h"
#include "BGFXRenderer.h"
#include <Camera/CameraData.h>
#include <Engine/Input.h>
#include "MitchHub.h"
#include <SDL_mouse.h>
#include <imgui.h>

#define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE    (SDL_VERSION_ATLEAST(2,0,4) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS))
#define SDL_HAS_MOUSE_FOCUS_CLICKTHROUGH    SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_WINDOW_ALPHA                SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_ALWAYS_ON_TOP               SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_USABLE_DISPLAY_BOUNDS       SDL_VERSION_ATLEAST(2,0,5)
#define SDL_HAS_PER_MONITOR_DPI             SDL_VERSION_ATLEAST(2,0,4)
#define SDL_HAS_VULKAN                      SDL_VERSION_ATLEAST(2,0,6)
void UpdateMonitors()
{
	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
	platform_io.Monitors.resize(0);
	int display_count = SDL_GetNumVideoDisplays();
	for (int n = 0; n < display_count; n++)
	{
		// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
		ImGuiPlatformMonitor monitor;
		SDL_Rect r;
		SDL_GetDisplayBounds(n, &r);
		monitor.MainPos = monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
		monitor.MainSize = monitor.WorkSize = ImVec2((float)r.w, (float)r.h);
#if SDL_HAS_USABLE_DISPLAY_BOUNDS
		SDL_GetDisplayUsableBounds(n, &r);
		monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
		monitor.WorkSize = ImVec2((float)r.w, (float)r.h);
#endif
#if SDL_HAS_PER_MONITOR_DPI
		float dpi = 0.0f;
		if (!SDL_GetDisplayDPI(n, &dpi, NULL, NULL))
			monitor.DpiScale = dpi / 96.0f;
#endif
		platform_io.Monitors.push_back(monitor);
	}
}

extern bool ImGui_ImplSDL2_InitForD3D(SDL_Window* window);
extern void ImGui_ImplSDL2_NewFrame(SDL_Window* window);
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

	SDLWindow* win = new SDLWindow("ME HUB", ResizeFunc, 500, 300, Vector2(1920, 1080));
	win->SetBorderless(true);

	RendererCreationSettings set;
	set.WindowPtr = win->GetWindowPtr();
	set.InitAssets = false;
	Renderer->Create(set);

	ImGui_ImplSDL2_InitForD3D(win->WindowHandle);

	Input input;
	MitchHub hub(&input, win, Renderer->GetImGuiRenderer());
	//UpdateMonitors();

	while (!win->ShouldClose())
	{
		win->ParseMessageQueue();

		// Setup display size (every frame to accommodate for window resizing)
		int w, h;
		int display_w, display_h;
		SDL_GetWindowSize(win->WindowHandle, &w, &h);
		if (SDL_GetWindowFlags(win->WindowHandle) & SDL_WINDOW_MINIMIZED)
			w = h = 0;
		ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);

		SDL_GL_GetDrawableSize(win->WindowHandle, &display_w, &display_h);
		if (w > 0 && h > 0)
			ImGui::GetIO().DisplayFramebufferScale = ImVec2((float)display_w / w, (float)display_h / h);

		// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
		static Uint64 frequency = SDL_GetPerformanceFrequency();
		Uint64 current_time = SDL_GetPerformanceCounter();

		input.Update();
		ImGuiIO& io = ImGui::GetIO();

		// Update mouse buttons
		int mouse_x_local, mouse_y_local;
		Uint32 mouse_buttons = SDL_GetMouseState(&mouse_x_local, &mouse_y_local);

		int mouse_x_global, mouse_y_global;
		SDL_GetGlobalMouseState(&mouse_x_global, &mouse_y_global);
		Vector2 mousePos = input.GetMousePosition();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			// Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
			mousePos = Vector2((float)mouse_x_global, (float)mouse_y_global);
		}
		else
		{
			// Single-viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
			// Unlike local position obtained earlier this will be valid when straying out of bounds.
			//int window_x, window_y;
			//SDL_GetWindowPosition(mouse_window, &window_x, &window_y);
			//mousePos = Vector2((float)(mouse_x_global - window_x), (float)(mouse_y_global - window_y));
		}

		Renderer->BeginFrame(mousePos, (input.IsMouseButtonDown(MouseButton::Left) ? 0x01 : 0)
			| (input.IsMouseButtonDown(MouseButton::Right) ? 0x02 : 0)
			| (input.IsMouseButtonDown(MouseButton::Middle) ? 0x04 : 0)
			, (int32_t)input.GetMouseScrollOffset().y
			, win->GetSize()
			, -1
			, 255);

		ImGui_ImplSDL2_NewFrame(win->WindowHandle);

		hub.Draw();

		Moonlight::CameraData cam;
		Renderer->Render(cam);
		input.PostUpdate();
	}

	return 0;
}