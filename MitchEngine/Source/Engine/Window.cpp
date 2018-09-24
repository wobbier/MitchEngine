#include "PCH.h"
#include "Engine/Window.h"
#include "Engine/Input.h"
#include "Utility/Logger.h"
#include <assert.h>
/*
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glad.c>
#ifdef MAN_EDITOR
#include "Graphics/UI/imgui.h"
#include "Graphics/UI/imgui_impl_glfw.h"
#include "Graphics/UI/imgui_impl_opengl3.h"
#endif

int Window::WINDOW_WIDTH = 960;
int Window::WINDOW_HEIGHT = 540;

Window::Window(std::string title, int width, int height)
{
	WINDOW_HEIGHT = height;
	WINDOW_WIDTH = width;

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

#ifdef MAN_ENABLE_RENDERDOC
	RenderDoc = new RenderDocManager();
#endif
	
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

#ifdef MAN_EDITOR

	// Setup Dear ImGui binding
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	const char* glsl_version = "#version 130";
	ImGui_ImplOpenGL3_Init(glsl_version);
	glfwSetKeyCallback(window, ImGui_ImplGlfw_KeyCallback);
	glfwSetCursorPosCallback(window, ImGui_ImplGlfw_MousePosCallback);

	// Setup style
	ImGui::StyleColorsDark();
#endif
}

Window::~Window()
{
}

bool Window::ShouldClose()
{
	return (glfwWindowShouldClose(window) == 1) ? true : false;
}


void Window::PollInput()
{
	glfwPollEvents();
}
void Window::Swap()
{
	glfwMakeContextCurrent(window);
	glfwSwapBuffers(window);
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}
*/