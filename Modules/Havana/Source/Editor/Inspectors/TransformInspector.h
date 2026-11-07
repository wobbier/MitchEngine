#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

// The transform inspector (UX-11). Position / Rotation / Scale are reflected vector rows, so they
// already scrub per axis from their X/Y/Z handles and offer Reset / Copy / Paste on their label;
// this adds a uniform-scale lock on Scale (preference Inspector.ScaleLock) and, on Rotation, a
// dial toggle (preference Inspector.RotationDials) with a row of X/Y/Z dials under it.
namespace TransformInspector
{
    void Register();
}

#endif
