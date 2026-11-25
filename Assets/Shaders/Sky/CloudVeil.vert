$input a_position

// A wide horizontal sheet in world space (DynamicSky::Draw): wherever the scene lies beyond it, the
// sky's clouds are laid over the scene (see CloudVeil.frag).
#include "../Common.sh"

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
