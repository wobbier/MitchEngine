#include "PCH.h"
#include "Window/IWindow.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <assert.h>

#if ME_OPENGL

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glad.c>

#include "Renderer.h"

GLWindow::GLWindow(std::string title, int width, int height)
	: IWindow(title, width, height)
{
	// Init GLFW
	glfwInit();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

	window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr); // Windowed
	if (!window)
	{
		Logger::GetInstance().Log(Logger::LogType::Error, "Failed to create window.");
		glfwTerminate();
		assert(0);
	}

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, Window::FramebufferSizeCallback);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		assert(0);
	}
	//glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetKeyCallback(window, &Input::KeyCallback);
	glfwSetCursorPosCallback(window, &Input::MouseCallback);
	glfwSetScrollCallback(window, &Input::ScrollCallback);
}

GLWindow::~GLWindow()
{
}

bool GLWindow::ShouldClose()
{
	return (glfwWindowShouldClose(window) == 1) ? true : false;
}


void GLWindow::PollInput()
{
	glfwPollEvents();
}
void GLWindow::Swap()
{
	glfwMakeContextCurrent(window);
	glfwSwapBuffers(window);
}

void GLWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

#endif