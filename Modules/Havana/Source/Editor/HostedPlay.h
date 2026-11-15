#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// A game's editor widget that starts play to drive the game from itself (a chart editor scrubbing
// the song) sets this first: the Game View is opened and brought to the front of its dock, but
// neither maximizes (Maximize On Play) nor takes the focus. Cleared when play ends.
namespace HostedPlay
{
    inline bool& Active()
    {
        static bool active = false;
        return active;
    }
}

#endif
