#include "PCH.h"
#include "Window/UWPWindow.h"
#include "Engine/Input.h"
#include "Logger.h"
#include <assert.h>

#if ME_PLATFORM_UWP

#include "Renderer.h"
#include "Device/D3D12Device.h"
#include <tchar.h>

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;

UWPWindow::UWPWindow(std::string title, int width, int height)
	: IWindow(title, width, height)
{
	CoreWindow^ window = CoreWindow::GetForCurrentThread();
}

UWPWindow::~UWPWindow()
{
}

bool UWPWindow::ShouldClose()
{
	return ExitRequested;
}

void UWPWindow::ParseMessageQueue()
{
	if (m_windowVisible)
	{
		CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
	}
	else
	{
		CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
	}
}

void UWPWindow::Swap()
{
}

#endif