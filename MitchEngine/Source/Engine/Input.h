#pragma once
#include "Utility/Singleton.h"
#include <map>
#include <glm.hpp>

#if ME_PLATFORM_WIN64
#include "GLFW/glfw3.h"
#endif
class Input : public Singleton<Input>
{
	friend class Singleton<Input>;
	friend class Window;
public:
	struct Key
	{
		int Id;
		int Scancode;
		int Action;
		int Mode;
	};

	struct MouseInfo
	{
		glm::vec2 Position;
		glm::vec2 Scroll;
	};

	bool IsKeyDown(int key);

	bool IsKeyUp(int key);
	glm::vec2 GetMousePosition();
	glm::vec2 GetMouseScrollOffset();

#if ME_PLATFORM_WIN64
	static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
	static void MouseCallback(GLFWwindow* window, double xpos, double ypos);
	static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
#endif // ME_PLATFORM_WIN64

private:
	Input() = default;
	~Input() {}
	std::map<int, Key> Keys;
	MouseInfo Mouse;

};
