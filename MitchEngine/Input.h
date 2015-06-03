#pragma once
#include "Singleton.h"
#include <GLFW/glfw3.h>
#include <map>
#include <glm.hpp>
namespace ma {
	struct Key {
		int Id;
		int Scancode;
		int Action;
		int Mode;
	};

	struct Mouse {
		glm::vec2 position;
	};

	class Input : public Singleton<Input> {
		friend class Singleton<Input>;
		friend class Window;
	private:
		static Input* Instance;

		std::map<int, Key> keys;
		Mouse mouse;

		static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
		static void MouseCallback(GLFWwindow* window, double xpos, double ypos);
	public:
		static Input* Get();

		bool IsKeyDown(int key);
		glm::vec2 GetMousePosition();
	};
}