#include "PCH.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <glm.hpp>
#include <string>
#include <iostream>

#pragma region KeyboardInput

#if ME_OPENGL
void Input::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	//printf("%i, %i, %i, %i\n", key, scancode, action, mode);
	Input& Instance = GetInstance();
	Instance.Keys[key].Id = key;
	Instance.Keys[key].Scancode = scancode;
	Instance.Keys[key].Action = action;
	Instance.Keys[key].Mode = mode;
}
#endif

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

#if ME_OPENGL
void Input::MouseCallback(GLFWwindow* window, double xpos, double ypos)
{
	Input& Instance = GetInstance();
	Instance.Mouse.Position = glm::vec2(xpos, ypos);
	//Logger::Get().Log(Logger::LogType::Debug, std::to_string(Instance.Mouse.Position.x));
}


void Input::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	Input& Instance = GetInstance();
	Instance.Mouse.Scroll -= glm::vec2(xoffset, yoffset);
}
#endif

glm::vec2 Input::GetMousePosition()
{
	return Mouse.Position;
}


glm::vec2 Input::GetMouseScrollOffset()
{
	return Mouse.Scroll;
}

#pragma endregion