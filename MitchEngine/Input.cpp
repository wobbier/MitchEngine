#include "Input.h"
#include <glm.hpp>
#include <iostream>
namespace ma {
	Input* Input::Instance = nullptr;

	Input* Input::Get() {
		if (Instance) {
			return Instance;
		}
		else {
			return Instance = new Input();
		}
	}
#pragma region KeyboardInput

	void Input::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode) {
		printf("%i, %i, %i, %i\n", key, scancode, action, mode);
		Input* Instance = Get();
		Instance->keys[key].Id = key;
		Instance->keys[key].Scancode = scancode;
		Instance->keys[key].Action = action;
		Instance->keys[key].Mode = mode;
	}

	bool Input::IsKeyDown(int key) {
		if (keys[key].Action == 0) {
			return false;
		}
		return true;
	}

#pragma endregion

#pragma region MouseInput

	void Input::MouseCallback(GLFWwindow* window, double xpos, double ypos) {
		Input* Instance = Get();
		Instance->mouse.position = glm::vec2(xpos, ypos);
	}

	glm::vec2 Input::GetMousePosition() {
		return mouse.position;
	}

#pragma endregion
}