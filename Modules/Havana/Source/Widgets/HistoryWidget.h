#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

// Undo history: click an entry to undo/redo up to it.
class HistoryWidget
	: public HavanaWidget
{
public:
	HistoryWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;
};

#endif
