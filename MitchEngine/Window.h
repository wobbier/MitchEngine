// 2015 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

using namespace std;
namespace ma {
	class Window {
	private:
	public:
		static int WINDOW_HEIGHT, WINDOW_WIDTH;
		GLFWwindow* window;
		Window(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
		~Window();

		bool ShouldClose();
	};
}