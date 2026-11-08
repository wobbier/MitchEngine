#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <string>

// Widget Gallery (UX-3/UX-4): every MeWidgets widget on one page, with fixed sample values, for
// screenshot regression and for trying the juice levels. View > Widget Gallery.
class WidgetGalleryWidget
	: public HavanaWidget
{
public:
	WidgetGalleryWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;

	// Puts every sample back to its starting value (gallery-reset in flows).
	void ResetSamples();
	// Fires every kind of feedback at once: a flash on each field, a tick, a burst and a shake.
	void Poke();

private:
	bool m_poke = false;
};

#endif
