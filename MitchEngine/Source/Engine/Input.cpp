#include "PCH.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <string>
#include <iostream>
#include <Windows.h>
#include <WinUser.h>

#pragma region KeyboardInput

#if ME_PLATFORM_UWP
void Input::KeyCallback(VirtualKey key)
{
	Input& Instance = GetInstance();
}
#endif

bool Input::IsKeyDown(KeyCode key)
{
	BROFILER_CATEGORY("Input::IsKeyDown", Brofiler::Color::YellowGreen);
#if ME_PLATFORM_WIN64
	SHORT keyState = GetAsyncKeyState(key);
	if ((1 << 15) & keyState)
#else
	CoreVirtualKeyStates keyPress = Windows::UI::Core::CoreWindow::GetForCurrentThread()->GetAsyncKeyState(key);
	if (keyPress == CoreVirtualKeyStates::Down)
#endif
	{
		return true;
	}
	return false;
}

bool Input::IsKeyUp(KeyCode key)
{
	return !IsKeyDown(key);
}

#pragma endregion

#pragma region MouseInput

Vector2 Input::GetMousePosition()
{
	Vector2 newPosition = Mouse.Position;
#if ME_PLATFORM_WIN64
	POINT position;
	if (GetCursorPos(&position))
	{
		newPosition.SetX(position.x);
		newPosition.SetY(position.y);
	}
#else
	newPosition.SetX(CoreWindow::GetForCurrentThread()->PointerPosition.X);
	newPosition.SetY(CoreWindow::GetForCurrentThread()->PointerPosition.Y);
#endif

	return newPosition;
}


Vector2 Input::GetMouseScrollOffset()
{
	return Mouse.Scroll;
}

#pragma endregion