#include "PCH.h"
#include <WinUser.h>
#include "Engine/Input.h"
#include "CLog.h"
#include <string>
#include <iostream>

#pragma region KeyboardInput

#pragma endregion

#pragma region MouseInput
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
	Vector2 newPosition;// = Vector2(MouseState.x, MouseState.y);
	return newPosition;
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
	return Vector2();
}

void Input::SetMouseCapture(bool Capture)
{
	if (CaptureInput)
	{
		if (Capture)
		{
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
	//Mouse = std::make_unique<DirectX::Mouse>();
	//Controller = std::make_unique<DirectX::GamePad>();
	//Keyboard = std::make_unique<DirectX::Keyboard>();
	//Controller->Resume();
#if ME_PLATFORM_UWP
	//Mouse->SetWindow(CoreWindow::GetForCurrentThread());
	//Keyboard->SetWindow(CoreWindow::GetForCurrentThread());
#endif
}

#pragma endregion