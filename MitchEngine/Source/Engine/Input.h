#pragma once
#include "Singleton.h"
#include <map>
#include <glm.hpp>

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

#if ME_PLATFORM_UWP
	static void KeyCallback(Windows::System::VirtualKey key);
#endif

private:
	Input() = default;
	~Input() {}
	std::map<int, Key> Keys;
	MouseInfo Mouse;

};
