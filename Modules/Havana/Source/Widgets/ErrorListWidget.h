#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

// ME-233: the last failed C# build's diagnostics, until a build succeeds. Double-click opens the
// line in the code editor (Preferences > General > Code editor).
class ErrorListWidget
	: public HavanaWidget
{
public:
	ErrorListWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;
};

#endif
