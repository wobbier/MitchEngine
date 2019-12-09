#pragma once
#include "Singleton.h"
#include <map>
#include "Math/Vector2.h"
#include <Mouse.h>
#include <Keyboard.h>
#include <GamePad.h>

#if ME_PLATFORM_UWP
using namespace Windows::UI::Core;
using namespace Windows::System;
#endif

class Input
{
	friend class Window;
public:
	DirectX::Keyboard::State GetKeyboardState()
	{
		return Keyboard->GetState();
	}

	DirectX::Mouse::State GetMouseState()
	{
		return Mouse->GetState();
	}

	DirectX::GamePad::State GetControllerState(unsigned int PlayerId = 0)
	{
		return Controller->GetState(PlayerId);
	}

	Vector2 GetMousePosition();
	Vector2 GetMouseScrollOffset();

	DirectX::Mouse& GetMouse()
	{
		return *Mouse.get();
	}

private:
	Input();
	~Input() = default;

	std::unique_ptr<DirectX::Mouse> Mouse;
	std::unique_ptr<DirectX::Keyboard> Keyboard;
	std::unique_ptr<DirectX::GamePad> Controller;

	ME_SINGLETON_DEFINITION(Input)
};
