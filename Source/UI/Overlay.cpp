#include "PCH.h"
#include <AppCore/Overlay.h>
#include <AppCore/Window.h>

#include "RefCountedImpl.h"
#include "UI/OverlayImpl.h"

namespace ultralight
{
	Ref<Overlay> Overlay::Create(Ref<Window> window, uint32_t width, uint32_t height, int x, int y)
	{
		return AdoptRef(*new OverlayImpl(window, width, height, x, y));
	}

	Ref<Overlay> Overlay::Create(Ref<Window> window, Ref<View> view, int x, int y)
	{
		return AdoptRef(*new OverlayImpl(window, view, x, y));
	}

	Overlay::~Overlay()
	{
	}
}
