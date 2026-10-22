#ifndef DOF_COMMON_SH
#define DOF_COMMON_SH
uniform vec4 u_dofLens;        // focus (m), thin-lens scale (px), maximum radius (px), debug CoC
uniform vec4 u_dofSplit;       // split X, full feather width, compose second lens, unused
uniform vec4 u_dofProjection;  // P22, P32, homogeneous depth, camera far
uniform vec4 u_dofScreen;      // full size xy, half size zw (rounded up)
uniform vec4 u_dofPass;        // 0 horizontal/far, 1 vertical/near
uniform vec4 u_dofJitter;      // depth uv offset matching TAA's unjittered colour

vec2 dofFullUV(vec2 _pixel)
{
	return (clamp(_pixel, vec2_splat(0.0), u_dofScreen.xy - 1.0) + 0.5) / u_dofScreen.xy;
}
#endif
