#include "Input.h"
#include <glm.hpp>
#include <iostream>
namespace ma {
#pragma region KeyboardInput

	void Input::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode) {
		printf("%i, %i, %i, %i\n", key, scancode, action, mode);
		Input& Instance = Get();
		Instance.Keys[key].Id = key;
		Instance.Keys[key].Scancode = scancode;
		Instance.Keys[key].Action = action;
		Instance.Keys[key].Mode = mode;
	}

	bool Input::IsKeyDown(int key) {
		if (Keys[key].Action == 0) {
			return false;
		}
		return true;
	}

#pragma endregion

#pragma region MouseInput

	void Input::MouseCallback(GLFWwindow* window, double xpos, double ypos) {
		Input& Instance = Get();
		Instance.Mouse.Position = glm::vec2(xpos, ypos);
	}

	glm::vec2 Input::GetMousePosition() {
		return Mouse.Position;
	}

#pragma endregion
}