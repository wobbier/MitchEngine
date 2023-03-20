#include "PCH.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <WinUser.h>
#endif

#include "Engine/Input.h"
#include "CLog.h"
#include <string>
#include <iostream>
#include "SDL.h"
#include "Engine.h"
#include <Window/IWindow.h>
#include <Window/SDLWindow.h>

#pragma region Class

Input::Input()
{
	KeyboardState = SDL_GetKeyboardState(nullptr);
	MouseState = SDL_GetMouseState(nullptr, nullptr);// use these params

	std::vector<TypeId> events;
	events.push_back(MouseScrollEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);

	PreviousKeyboardState = static_cast<const uint8_t*>(malloc(sizeof(uint8_t) * SDL_NUM_SCANCODES));
	PostUpdate();
}

bool Input::OnEvent(const BaseEvent& evt)
{
	if (CaptureInput)
	{
		if (evt.GetEventId() == MouseScrollEvent::GetEventId())
		{
			const MouseScrollEvent& event = static_cast<const MouseScrollEvent&>(evt);
			MouseScroll = MouseScroll + event.Scroll;
		}
	}
	return false;
}

void Input::Pause()
{
	CaptureInput = false;
}

void Input::Resume()
{
	CaptureInput = true;
	SetMouseCapture(WantsToCaptureMouse);
}

void Input::Stop()
{
	RelativeMousePosition = Vector2();
	//SDL_CaptureMouse(SDL_FALSE);
	//SDL_SetRelativeMouseMode(SDL_FALSE);
	//SDL_ShowCursor(SDL_ENABLE);
	CaptureInput = false;
}

void Input::Update()
{
	if (CaptureInput)
	{
		int mouseX = 0;
		int mouseY = 0;
		MouseState = SDL_GetMouseState(&mouseX, &mouseY);
		MousePosition = Vector2(mouseX, mouseY);
		int relativeMouse[2] = {0, 0};
		SDL_GetRelativeMouseState(&relativeMouse[0], &relativeMouse[1]);
		RelativeMousePosition = Vector2(relativeMouse[0], relativeMouse[1]);
	}
}

void Input::PostUpdate()
{
	if (CaptureInput)
	{
		PreviousKeyboardState = static_cast<const uint8_t*>(memcpy((void*)PreviousKeyboardState, (void*)KeyboardState, sizeof(uint8_t) * SDL_NUM_SCANCODES));
	}
	else
	{
		PreviousKeyboardState = static_cast<const uint8_t*>(memset((void*)PreviousKeyboardState, 0, sizeof(uint8_t) * SDL_NUM_SCANCODES));
	}
	PreviousMouseState = MouseState;
}

#pragma endregion

#pragma region KeyboardInput

bool Input::IsKeyDown(KeyCode key)
{
	return CaptureInput && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyPressed(KeyCode key)
{
	return CaptureInput && !PreviousKeyboardState[(uint32_t)key] && KeyboardState[(uint32_t)key];
}

bool Input::WasKeyReleased(KeyCode key)
{
	return CaptureInput && PreviousKeyboardState[(uint32_t)key] && !KeyboardState[(uint32_t)key];
}

#pragma endregion

#pragma region MouseInput

Vector2 Input::GetMousePosition() const
{
	return MousePosition;
}

Vector2 Input::GetGlobalMousePosition() const
{
	int mouse_x_global, mouse_y_global;
	SDL_GetGlobalMouseState(&mouse_x_global, &mouse_y_global);
	return { mouse_x_global, mouse_y_global };
}

Vector2 Input::GetRelativeMousePosition() const
{
	return RelativeMousePosition;
}

void Input::SetMousePosition(const Vector2& InPosition)
{
	// this just needs to be redone generically, too lazy atm
	if (CaptureInput)
	{
#if ME_EDITOR && USING( ME_PLATFORM_WIN64 )
		if (!GameWindow)
		{
			GameWindow = GetEngine().GetWindow();
		}
		auto win = static_cast<SDLWindow*>(GameWindow)->WindowHandle;
		SDL_WarpMouseInWindow(win, GameWindow->GetSize().x / 2.f, GameWindow->GetSize().y / 2.f);
		//Vector2 pos = Offset + InPosition;
		//SetCursorPos(static_cast<int>(pos.x), static_cast<int>(pos.y));
#endif
	}
}

Vector2 Input::GetMouseOffset()
{
	return Offset;
}

Vector2 Input::GetMouseScrollOffset()
{
	return MouseScroll;
}

void Input::SetMouseCapture(bool Capture)
{
	if (CaptureInput)
	{
		if (Capture)
		{
			SDL_SetRelativeMouseMode(SDL_TRUE);
			//SDL_CaptureMouse(SDL_TRUE);
			//SDL_ShowCursor(SDL_DISABLE);
		}
		else
		{
			SDL_SetRelativeMouseMode(SDL_FALSE);
			//SDL_CaptureMouse(SDL_FALSE);
			//SDL_ShowCursor(SDL_ENABLE);
		}
	}
	WantsToCaptureMouse = Capture;
}

void Input::SetMouseOffset(const Vector2& InOffset)
{
	Offset = InOffset;
}

bool Input::IsMouseButtonDown(MouseButton mouseButton)
{
	return MouseState & SDL_BUTTON((uint32_t)mouseButton);
}

bool Input::WasMouseButtonPressed(MouseButton mouseButton)
{
	return IsMouseButtonDown(mouseButton) && !(PreviousMouseState & SDL_BUTTON((uint32_t)mouseButton));
}

#pragma endregion
