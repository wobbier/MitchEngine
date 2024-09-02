#pragma once

#if USING( ME_GAME_TOOLS )

#include "Editor/WidgetRegistry.h"

class DebugTools
{
public:
    DebugTools();
    ~DebugTools();

    void Init();
    void Render();
    std::vector<SharedPtr<HavanaWidget>> CustomRegisteredWidgets;
};

#endif