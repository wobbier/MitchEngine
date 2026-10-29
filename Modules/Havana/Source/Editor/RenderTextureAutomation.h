#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>

// Editor automation (`rtt-*` edscript commands) for render textures. <t> is a .rendertexture
// path, or @<Entity> for that camera's target. Pixel checks read the texture back from the GPU
// (they retry until the copy arrives); a pixel is lit when a channel is above 24 (of 255).
//   rtt-assert-size <t> | <width> | <height> [| <frames>]   retries until it's that size (a reload)
//   rtt-mark <t>                               remembers the render count and the image
//   rtt-wait <t> [| <renders> [| <frames>]]    retries until that many renders since the mark
//                                              (1; without a mark, in all), failing after frames (600)
//   rtt-assert-renders <t> | <frames> | <min> | <max>   renders over the next frames (update interval)
//   rtt-assert-changed <t> [| <fraction>]      after a render since the mark, at least this fraction
//                                              of pixels (0.01) differ from the marked image
//   rtt-assert-lit <t> | <fraction> [| x y w h]      at least this fraction of pixels is lit (in the
//                                              normalized rect, from the top left)
//   rtt-assert-dark <t> | <fraction> | x y w h      at most this fraction of the rect's pixels is lit
//   rtt-capture <t> | <path.png>              writes the image
namespace RenderTextureAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry );
}
#endif
