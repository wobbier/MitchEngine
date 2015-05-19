#include "Window.h"

using namespace ma;

int Window::WINDOW_WIDTH = 960;
int Window::WINDOW_HEIGHT = 540;

Window::Window(std::string title, int width, int height) {
	WINDOW_HEIGHT = height;
	WINDOW_WIDTH = width;

	// Init GLFW
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

	window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr); // Windowed
	glfwMakeContextCurrent(window);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

Window::~Window() {

}

bool Window::ShouldClose() {
	return (glfwWindowShouldClose(window) == 1) ? true : false;
}