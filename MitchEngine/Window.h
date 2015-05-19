#pragma once
#include <iostream>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

using namespace std;
namespace ma {
	class Window {
	private:
	public:
		static int WINDOW_HEIGHT, WINDOW_WIDTH;
		GLFWwindow* window;

		Window(char* title, int width, int height);
		~Window();

		bool ShouldClose();
	};
}