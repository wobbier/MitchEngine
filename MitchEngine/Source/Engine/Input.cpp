#include "PCH.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <glm.hpp>
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

glm::vec2 Input::GetMousePosition()
{
	
#if ME_PLATFORM_WIN64
	POINT position;
	if (GetCursorPos(&position))
#else
	glm::vec2 position;
	position.x = CoreWindow::GetForCurrentThread()->PointerPosition.X;
	position.y = CoreWindow::GetForCurrentThread()->PointerPosition.Y;
#endif
	{
		return glm::vec2(position.x, position.y);
	}

	return Mouse.Position;
}


glm::vec2 Input::GetMouseScrollOffset()
{
	return Mouse.Scroll;
}

#pragma endregion