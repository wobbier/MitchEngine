$input v_color0, v_texcoord0

// RmlUi: texture times premultiplied vertex color, blended ONE / INV_SRC_ALPHA over the
// composited frame. Textures arrive premultiplied (decoded images are premultiplied on load).

#include "../Common.sh"

SAMPLER2D(s_texture, 0);

void main()
{
	gl_FragColor = texture2D(s_texture, v_texcoord0) * v_color0;
}
