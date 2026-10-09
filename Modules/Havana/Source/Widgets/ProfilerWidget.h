#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

// Frame time / GPU time history, the per-phase and per-core CPU breakdown of the last frame
// (FrameStats scopes) and per-view GPU timings.
class ProfilerWidget
	: public HavanaWidget
{
public:
	ProfilerWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;

private:
	bool m_paused = false;
};

#endif
