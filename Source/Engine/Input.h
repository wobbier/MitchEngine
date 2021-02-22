#pragma once
#include "Math/Vector2.h"
//
//
//#if ME_PLATFORM_UWP
//using namespace Windows::UI::Core;
//using namespace Windows::System;
//#endif

class Input
{
	friend class Window;
	friend class Engine;
	friend class Havana;
public:
	Input();
	~Input() = default;

	//const DirectX::Keyboard::State& GetKeyboardState() const;
	//DirectX::Mouse::State GetMouseState();
	//DirectX::GamePad::State GetControllerState(unsigned int PlayerId = 0);

	Vector2 GetMousePosition();
	void SetMousePosition(const Vector2& InPosition);
	Vector2 GetMouseOffset();

	Vector2 GetMouseScrollOffset();

	void SetMouseCapture(bool Capture);
	void SetMouseOffset(const Vector2& InOffset);

	//DirectX::Mouse& GetMouse();

	void Pause();
	void Resume();
	void Stop();

private:
	void Update();

	bool CaptureInput = true;

	//DirectX::Keyboard::State KeyboardState;
	//DirectX::Mouse::State MouseState;
	//DirectX::GamePad::State ControllerState;

	Vector2 Offset;

	//static std::unique_ptr<DirectX::Mouse> Mouse;
	//static std::unique_ptr<DirectX::Keyboard> Keyboard;
	//static std::unique_ptr<DirectX::GamePad> Controller;
	bool WantsToCaptureMouse = false;
};
