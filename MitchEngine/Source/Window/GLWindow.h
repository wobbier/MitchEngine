// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#if ME_OPENGL

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class GLWindow
	: public IWindow
{
public:
	static int WINDOW_HEIGHT, WINDOW_WIDTH;
	Window(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~Window();

	virtual bool ShouldClose() final;
	virtual void PollInput() final;
	virtual void Swap() final;

private:
	void MouseCallback(GLFWwindow* window, double xpos, double ypos);
	void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
	void ProcessInput(GLFWwindow *window);
	static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

	GLFWwindow * window;
};

#endif