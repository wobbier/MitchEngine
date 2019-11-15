// 2018 Mitchell Andrews
#pragma once
#include <iostream>
#include <string>

#include "IWindow.h"
#include "Math/Vector2.h"

#if ME_PLATFORM_UWP

using namespace Windows::UI::Core;
//using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
//using namespace Windows::Graphics::Display;

class UWPWindow
	: public IWindow
{
	ref class UWPWindowMessageHandler
	{
	internal:
		UWPWindowMessageHandler(UWPWindow* window);
	public:
		void OnWindowSizeChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::WindowSizeChangedEventArgs^ args);
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);
		void OnWindowClosed(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CoreWindowEventArgs^ args);
		void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);

		void OnDpiChanged(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);
		void OnDisplayContentsInvalidated(Windows::Graphics::Display::DisplayInformation^ sender, Platform::Object^ args);

	private:
		UWPWindow* m_window;
	};

public:
	UWPWindow(std::string title, int width = WINDOW_WIDTH, int height = WINDOW_HEIGHT);
	~UWPWindow();

	virtual bool ShouldClose() final;
	virtual void ParseMessageQueue() final;
	virtual void Swap() final;
	virtual Vector2 GetSize() const final;
	virtual void SetTitle(const std::string& title) final;

	void SetVisibility(bool Visible);

	virtual Vector2 GetPosition() final;
	virtual void Maximize() final;
	virtual void Minimize() final;
	virtual void ExitMaximize() final;
	virtual bool IsFullscreen() final;


	virtual void Exit() override;

private:
	bool ExitRequested = false;

	UWPWindowMessageHandler^ MessageHandler;
};

#endif