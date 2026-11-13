#ifndef STANDARD_CUTOUT_SH
#define STANDARD_CUTOUT_SH

// The fragments a Standard surface drops, and the texture coordinate and base colour its alpha
// comes from. Standard.frag and its depth prepass (StandardDepth.frag) both go through these, so
// the two passes cut the same holes: the prepass writes depth and the colour pass shades only what
// matches it, so a fragment one keeps and the other drops shows as a hole or a speck.
// Change a test here, never in one of the two shaders.
#include "Era.sh"
#include "Dither.sh"

uniform vec4 u_baseColor;   // linear rgb, opacity
uniform vec4 u_emissive;    // rgb linear * intensity, w alpha cutoff (0 = off)
uniform vec4 u_uvTransform; // xy tiling, zw offset
uniform vec4 u_drawParams;  // x screen-door fade (Dither.sh), per draw

uniform vec4 u_portalClipPlane;

// Behind a portal's clip plane, or a texel the screen-door fade drops. Tested before any texture
// is read. (_fragCoord: shaderc only allows gl_FragCoord inside main.)
bool standardClipped(vec3 _worldPos, vec2 _fragCoord)
{
	if (dot(vec4(_worldPos, 1.0), u_portalClipPlane) < 0.0)
	{
		return true;
	}
	return ditherDrops(u_drawParams.x, _fragCoord);
}

// The mesh's uv (affine under u_eraParams.y) through the material's tiling and offset.
vec2 standardUv(vec2 _texcoord, vec3 _affine)
{
	vec2 meshUv = u_eraParams.y > 0.5 ? _affine.xy / max(_affine.z, 0.000001) : _texcoord;
	return meshUv * u_uvTransform.xy + u_uvTransform.zw;
}

// Linear base colour and opacity: the diffuse texel (three-point filtered under u_eraParams.z)
// times the material's.
vec4 standardBase(sampler2D _diffuse, vec2 _uv)
{
	return toLinear(u_eraParams.z > 0.5 ? eraThreePoint(_diffuse, _uv) : texture2D(_diffuse, _uv)) * u_baseColor;
}

// Alpha cutout: _alpha is standardBase().a times the opacity map's red.
bool standardCutout(float _alpha)
{
	return u_emissive.w > 0.0 && _alpha < u_emissive.w;
}

#endif
