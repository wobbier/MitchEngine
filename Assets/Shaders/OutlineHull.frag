$input v_texcoord0

// Flat outline colour (linear HDR); pairs with OutlineHull(.Skinned).vert.
#include "Common.sh"

uniform vec4 u_outlineHull; // xyz linear colour, w width in metres

void main()
{
	gl_FragColor = vec4(u_outlineHull.xyz, 1.0);
}
