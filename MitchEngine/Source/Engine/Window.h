// 2015 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

class Window
{
private:
public:
	static int WINDOW_HEIGHT, WINDOW_WIDTH;
	Window(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~Window();

	bool ShouldClose();
	void PollInput();
	void Swap();

private:
	void MouseCallback(GLFWwindow* window, double xpos, double ypos);
	void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
	void ProcessInput(GLFWwindow *window);
	static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

private:
	GLFWwindow* window;
};