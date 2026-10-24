$input v_texcoord0

// Edge outlines over the opaque scene: view-depth gaps (silhouettes) and depth-direction folds
// (creases), from the depth buffer alone like SSAO. Alpha-blended onto the HDR colour before fog.
#include "../Common.sh"
#include "SSAOCommon.sh"

SAMPLER2D(s_input0, 0); // scene depth

uniform vec4 u_outline;       // xyz linear colour
uniform vec4 u_outlineParams; // x thickness (pixels), y depth threshold (metres per metre of depth), z crease threshold (0..2)

vec3 edgeViewPosition(vec2 uv)
{
	return aoViewPosition(uv, texture2DLod(s_input0, uv, 0.0).r);
}

void main()
{
	vec2 texel = u_ssaoScreen.xy * u_outlineParams.x;
	vec2 dx = vec2(texel.x, 0.0);
	vec2 dy = vec2(0.0, texel.y);
	vec3 p = edgeViewPosition(v_texcoord0);
	vec3 left = p - edgeViewPosition(v_texcoord0 - dx);
	vec3 right = edgeViewPosition(v_texcoord0 + dx) - p;
	vec3 down = p - edgeViewPosition(v_texcoord0 - dy);
	vec3 up = edgeViewPosition(v_texcoord0 + dy) - p;

	// Silhouette: the largest view-depth gap, with a distance-scaled limit so slanted floors
	// seen from afar don't read as edges.
	float viewScale = u_ssaoProjection.z > 0.5 ? 1.0 : max(abs(p.z), 1.0);
	float depthGap = max(max(abs(left.z), abs(right.z)), max(abs(down.z), abs(up.z)));
	float edge = step(u_outlineParams.y * viewScale, depthGap);

	// Crease: on a flat surface the two steps of an axis line up; a fold breaks that. Flat faces
	// carry sub-LSB depth noise, so a fold only counts when its z change clears a small floor.
	float zNoise = 2e-4 * viewScale;
	float foldX = max(abs(left.z), abs(right.z)) > zNoise ? 1.0 - abs(dot(normalize(left), normalize(right))) : 0.0;
	float foldY = max(abs(down.z), abs(up.z)) > zNoise ? 1.0 - abs(dot(normalize(down), normalize(up))) : 0.0;
	edge = max(edge, step(u_outlineParams.z, max(foldX, foldY)));

	gl_FragColor = vec4(u_outline.xyz, edge);
}
