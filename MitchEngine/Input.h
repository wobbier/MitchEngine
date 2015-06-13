#pragma once
#include "Singleton.h"
#include <GLFW/glfw3.h>
#include <map>
#include <glm.hpp>
namespace ma {

	class Input : public Singleton<Input> {
		friend class Singleton<Input>;
		friend class Window;
	public:
		struct Key {
			int Id;
			int Scancode;
			int Action;
			int Mode;
		};

		struct MouseInfo {
			glm::vec2 Position;
		};

		bool IsKeyDown(int key);

		bool IsKeyUp(int key);
		glm::vec2 GetMousePosition();

	private:
		Input() = default;
		~Input() {}
		std::map<int, Key> Keys;
		MouseInfo Mouse;

		static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
		static void MouseCallback(GLFWwindow* window, double xpos, double ypos);
	};
}