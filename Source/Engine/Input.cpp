#include "PCH.h"
#include <WinUser.h>
#include "Engine/Input.h"
#include "CLog.h"
#include <string>
#include <iostream>
#include "SDL.h"

#pragma region KeyboardInput

#pragma endregion

#pragma region MouseInput

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

//
//const DirectX::Keyboard::State& Input::GetKeyboardState() const
//{
//	return KeyboardState;
//}
//
//DirectX::Mouse::State Input::GetMouseState()
//{
//	return MouseState;
//}
//
//DirectX::GamePad::State Input::GetControllerState(unsigned int PlayerId /*= 0*/)
//{
//	return Controller->GetState(PlayerId);
//}

Vector2 Input::GetMousePosition()
{
	return MousePosition;
}


void Input::SetMousePosition(const Vector2& InPosition)
{
	if (CaptureInput)
	{
#if ME_EDITOR
		Vector2 pos = Offset + InPosition;
		SetCursorPos(pos.x, pos.y);
#endif
		Update();
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
			SDL_SCANCODE_RETURN;
			//Mouse->SetMode(DirectX::Mouse::MODE_RELATIVE);
		}
		else
		{
			//Mouse->SetMode(DirectX::Mouse::MODE_ABSOLUTE);
		}
		WantsToCaptureMouse = Capture;
	}
}

void Input::SetMouseOffset(const Vector2& InOffset)
{
	Offset = InOffset;
}

bool Input::IsMouseButtonDown(MouseButton mouseButton)
{
	return MouseState & SDL_BUTTON((uint32_t)mouseButton);
}

bool Input::IsKeyDown(KeyCode key)
{
	return KeyboardState[(uint32_t)key];
}

//
//DirectX::Mouse& Input::GetMouse()
//{
//	return *Mouse.get();
//}

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
	//Mouse->SetMode(DirectX::Mouse::MODE_ABSOLUTE);
	CaptureInput = false;
}

void Input::Update()
{
	if (CaptureInput)
	{
		KeyboardState = SDL_GetKeyboardState(nullptr);
		int mouseX = 0;
		int mouseY = 0;
		MouseState = SDL_GetMouseState(&mouseX, &mouseY);// use these params
		MousePosition = Vector2(mouseX, mouseY);
		if (KeyboardState)
		{
			int i = 0;
			++i;
		}
		//KeyboardState = Keyboard->GetState();
		//MouseState = Mouse->GetState();
	}
}

//
//std::unique_ptr<DirectX::Mouse> Input::Mouse = std::make_unique<DirectX::Mouse>();
//
//std::unique_ptr<DirectX::Keyboard> Input::Keyboard = std::make_unique<DirectX::Keyboard>();
//
//std::unique_ptr<DirectX::GamePad> Input::Controller = std::make_unique<DirectX::GamePad>();

Input::Input()
{
	KeyboardState = SDL_GetKeyboardState(nullptr);
	MouseState = SDL_GetMouseState(nullptr, nullptr);// use these params
//Mouse = std::make_unique<DirectX::Mouse>();
	//Controller = std::make_unique<DirectX::GamePad>();
	//Keyboard = std::make_unique<DirectX::Keyboard>();
	//Controller->Resume();
#if ME_PLATFORM_UWP
	//Mouse->SetWindow(CoreWindow::GetForCurrentThread());
	//Keyboard->SetWindow(CoreWindow::GetForCurrentThread());
#endif

	std::vector<TypeId> events;
	events.push_back(MouseScrollEvent::GetEventId());
	EventManager::GetInstance().RegisterReceiver(this, events);
}

#pragma endregion