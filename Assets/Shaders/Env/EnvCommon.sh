#ifndef __ENVCOMMON_SH__
#define __ENVCOMMON_SH__
// Fragment-path helpers: faces are addressed through gl_FragCoord, so the same code works whichever
// way a backend orients render targets. The compute filters (Env*.comp) use EnvMath.sh directly.
#include "EnvMath.sh"

// _fragCoord is gl_FragCoord.xy (only readable in main).
vec3 currentFaceDirection(vec2 _fragCoord)
{
	return cubeFaceDirection(u_envFace.x, _fragCoord * u_viewTexel.xy);
}

#endif // __ENVCOMMON_SH__
