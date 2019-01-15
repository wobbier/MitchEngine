#include "PCH.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <glm.hpp>
#include <string>
#include <iostream>

#pragma region KeyboardInput

#if ME_PLATFORM_UWP
void Input::KeyCallback(Windows::System::VirtualKey key)
{
	Input& Instance = GetInstance();
	/*Instance.Keys[key].Id = ;
	Instance.Keys[key].Scancode = scancode;
	Instance.Keys[key].Action = action;
	Instance.Keys[key].Mode = mode;*/
}
#endif

bool Input::IsKeyDown(int key)
{
	if (Keys[key].Action == 0)
	{
		return false;
	}
	return true;
}

bool Input::IsKeyUp(int key)
{
	if (Keys[key].Action == 0)
	{
		return true;
	}
	return false;
}

#pragma endregion

#pragma region MouseInput

glm::vec2 Input::GetMousePosition()
{
	return Mouse.Position;
}


glm::vec2 Input::GetMouseScrollOffset()
{
	return Mouse.Scroll;
}

#pragma endregion