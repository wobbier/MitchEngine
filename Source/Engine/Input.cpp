#include "PCH.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <string>
#include <iostream>
#include <WinUser.h>

#pragma region KeyboardInput

#pragma endregion

#pragma region MouseInput

Vector2 Input::GetMousePosition()
{
	Vector2 newPosition = Vector2(Mouse->GetState().x, Mouse->GetState().y);
	return newPosition;
}


Vector2 Input::GetMouseScrollOffset()
{
	return Vector2();
}

Input::Input()
{
	Controller = std::make_unique<DirectX::GamePad>();
	Mouse = std::make_unique<DirectX::Mouse>();
	Keyboard = std::make_unique<DirectX::Keyboard>();
	Controller->Resume();
#if ME_PLATFORM_UWP
	Mouse->SetWindow(CoreWindow::GetForCurrentThread());
	Keyboard->SetWindow(CoreWindow::GetForCurrentThread());
#endif
}

#pragma endregion