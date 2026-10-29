#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

// Engine-native RmlUi elements (UI-8), registered with the factory at startup:
//   <sprite sheet="Assets/X.spritesheet" name="X_3"/>   a sprite of a sheet (its region)
//   <sprite anim="Assets/X_Walk.spriteanim" [fps="12"]/> a flipbook, looping on the UI clock
//   <viewport src="Assets/Textures/RenderTexture/Studio.rendertexture"/>
//                                                      a render texture's live image (minimaps,
//                                                      character previews: a camera renders it)
//   <input-glyph action="Jump" [index="0"]/>           the action's binding as text (a key cap),
//                                                      following rebinds
// Sprites and viewports size themselves from the region or texture (CSS width / height override);
// paths are project paths (Assets/..., Engine/Assets/...), not relative to the document.
namespace RmlElements
{
    void Register();
}

#endif
