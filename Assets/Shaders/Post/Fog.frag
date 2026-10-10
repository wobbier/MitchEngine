$input v_texcoord0

// Composites the fog onto the opaque HDR scene (blend: src + dst * src.a), before transparents:
// the raymarched volume upsampled with depth awareness, or analytic height fog when it isn't
// volumetric.
#include "../Common.sh"
#include "../Lighting.sh"
#include "../Fog.sh"

SAMPLER2D(s_input0, 0);   // volumetric fog (half resolution, blurred)
SAMPLER2D(s_input1, 1);   // camera depth

float depthAt(vec2 _uv)
{
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	return fogViewDepth(texture2D(s_input1, (floor(_uv / fullTexel) + 0.5) * fullTexel).r);
}

void main()
{
	vec2 uv = v_texcoord0;
	float viewZ = depthAt(uv);
	if (u_fogVolume.w < 0.5)
	{
		vec3 dir;
		float distance;
		float viewZPerMetre;
		float originViewZ;
		vec3 origin = fogRay(uv, viewZ, dir, distance, viewZPerMetre, originViewZ);
		vec3 sunRadiance = vec3_splat(0.0);
		if (u_lightParams.x > 0.5)
		{
			sunRadiance = u_dirLightColor[0].rgb * henyeyGreenstein(dot(dir, -u_dirLightDirection[0].xyz), u_fogVolume.y) * u_fogVolume.x;
		}
		gl_FragColor = fogClampOpacity(fogAnalytic(origin, dir, 0.0, distance, sunRadiance));
		return;
	}

	// Joint bilateral upsample: the four nearest half-resolution samples, weighted by bilinear
	// position and by how close their depth is to this pixel's.
	vec2 halfSize = u_fogScreen.zw;
	vec2 coord = uv * halfSize - 0.5;
	vec2 base = floor(coord);
	vec2 f = coord - base;
	vec4 sum = vec4_splat(0.0);
	float weights = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		vec2 offset = vec2(mod(float(i), 2.0), floor(float(i) * 0.5));
		vec2 tapUv = (base + offset + 0.5) / halfSize;
		float bilinear = mix(1.0 - f.x, f.x, offset.x) * mix(1.0 - f.y, f.y, offset.y);
		float w = bilinear / (0.02 + abs(depthAt(tapUv) - viewZ) / max(viewZ, 1e-3) * 20.0);
		sum += texture2DLod(s_input0, tapUv, 0.0) * w;
		weights += w;
	}
	gl_FragColor = weights > 1e-6 ? sum / weights : texture2DLod(s_input0, uv, 0.0);
}
