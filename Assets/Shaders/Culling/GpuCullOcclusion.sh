// Occlusion against a camera's depth pyramid (ME-400; Moonlight::GpuMeshCulling::CullOcclusion and
// HiZBoxVisible in GpuMeshCulling.cpp, which this mirrors). Included by GpuCullOcclusionCount.comp
// after GpuCull.sh.
//
// Two phases per camera (opaque set, one view):
//   phase 1: in the frustum and visible against last frame's pyramid (with last frame's
//            view-projection). In the frustum but rejected: a candidate for phase 2.
//   phase 2: a candidate visible against this frame's pyramid (built from the depth prepass after
//            phase 1's draws).
//   phase 3: phase 2 without a pyramid (it could not be built): every candidate.
// s_occState (per member of the set: x visible in this phase, y candidate) carries phase 1's
// rejects to phase 2 and each member's result to pass 3, which reads it instead of testing again.
//
// The pyramid (GpuCullDepthPyramid.comp): the farthest depth per texel, mip 0 the previous powers of
// two of the depth size, HIZ_MIPS_PER_PASS mips per texture (s_hiz0 has mips 0-4, s_hiz1 5-9...).

#define HIZ_MIPS_PER_PASS 5u
#define HIZ_TEXTURES 3u

BUFFER_RW(s_occState, vec4, 11);
SAMPLER2D(s_hiz0, 12);
SAMPLER2D(s_hiz1, 13);
SAMPLER2D(s_hiz2, 14);

// [0..3]: the view-projection's columns; [4]: mip 0 width, height, mips, phase;
// [5]: x 1 for homogeneous depth (GL), y 1 when texture row 0 is the image's bottom (GL), z depth epsilon,
//      w 1 when phase 1 must reject everything (a check of phase 2).
uniform vec4 u_gpuCullOcc[6];

float hizTexel(uint mip, uint x, uint y)
{
	uint slot = mip / HIZ_MIPS_PER_PASS;
	float level = float(mip - slot * HIZ_MIPS_PER_PASS);
	float width = float(max(cullUint(u_gpuCullOcc[4].x) >> mip, 1u));
	float height = float(max(cullUint(u_gpuCullOcc[4].y) >> mip, 1u));
	vec2 uv = vec2((float(x) + 0.5) / width, (float(y) + 0.5) / height);
	if (slot == 0u)
	{
		return texture2DLod(s_hiz0, uv, level).x;
	}
	if (slot == 1u)
	{
		return texture2DLod(s_hiz1, uv, level).x;
	}
	return texture2DLod(s_hiz2, uv, level).x;
}

// False only when the box is certainly behind every depth under its screen rectangle.
bool hizVisible(vec3 boundsMin, vec3 boundsMax)
{
	vec4 c0 = u_gpuCullOcc[0];
	vec4 c1 = u_gpuCullOcc[1];
	vec4 c2 = u_gpuCullOcc[2];
	vec4 c3 = u_gpuCullOcc[3];
	vec3 low = vec3(1.0e30, 1.0e30, 1.0e30);
	vec3 high = vec3(-1.0e30, -1.0e30, -1.0e30);
	for (uint corner = 0u; corner < 8u; ++corner)
	{
		vec3 p = vec3(
			(corner & 1u) != 0u ? boundsMax.x : boundsMin.x,
			(corner & 2u) != 0u ? boundsMax.y : boundsMin.y,
			(corner & 4u) != 0u ? boundsMax.z : boundsMin.z);
		vec4 clip = c0 * p.x + c1 * p.y + c2 * p.z + c3;
		// A corner at or behind the eye: the box crosses the near plane.
		if (clip.w <= 1.0e-6)
		{
			return true;
		}
		vec3 ndc = clip.xyz / clip.w;
		low = min(low, ndc);
		high = max(high, ndc);
	}
	bool homogeneous = u_gpuCullOcc[5].x > 0.5;
	if (low.z < (homogeneous ? -1.0 : 0.0))
	{
		return true;
	}
	float nearest = homogeneous ? low.z * 0.5 + 0.5 : low.z;

	// The rectangle in texture space (with a little slack for rounding).
	float u0 = low.x * 0.5 + 0.5 - 1.0e-5;
	float u1 = high.x * 0.5 + 0.5 + 1.0e-5;
	float v0 = 0.5 - high.y * 0.5 - 1.0e-5;
	float v1 = 0.5 - low.y * 0.5 + 1.0e-5;
	if (u_gpuCullOcc[5].y > 0.5)
	{
		v0 = low.y * 0.5 + 0.5 - 1.0e-5;
		v1 = high.y * 0.5 + 0.5 + 1.0e-5;
	}
	// Off the image (it can be, against last frame's view): nothing is known there.
	if (u1 < 0.0 || u0 > 1.0 || v1 < 0.0 || v0 > 1.0)
	{
		return true;
	}
	uint width = cullUint(u_gpuCullOcc[4].x);
	uint height = cullUint(u_gpuCullOcc[4].y);
	uint mips = cullUint(u_gpuCullOcc[4].z);
	uint x0 = uint(clamp(floor(u0 * float(width)), 0.0, float(width - 1u)));
	uint x1 = uint(clamp(floor(u1 * float(width)), 0.0, float(width - 1u)));
	uint y0 = uint(clamp(floor(v0 * float(height)), 0.0, float(height - 1u)));
	uint y1 = uint(clamp(floor(v1 * float(height)), 0.0, float(height - 1u)));
	// The finest mip where the rectangle spans at most 2x2 texels.
	uint mip = 0u;
	for (uint k = 0u; k < 16u; ++k)
	{
		if ((x1 >> mip) - (x0 >> mip) <= 1u && (y1 >> mip) - (y0 >> mip) <= 1u)
		{
			break;
		}
		mip = mip + 1u;
	}
	if (mip >= mips)
	{
		return true;
	}
	x0 = x0 >> mip;
	x1 = x1 >> mip;
	y0 = y0 >> mip;
	y1 = y1 >> mip;
	float farthest = max(max(hizTexel(mip, x0, y0), hizTexel(mip, x1, y0)), max(hizTexel(mip, x0, y1), hizTexel(mip, x1, y1)));
	return nearest <= farthest + u_gpuCullOcc[5].z;
}

// Pass 1 of an occlusion cull for one member (InValid: not padding): whether this phase draws it.
// Records the result (and in phase 1 whether phase 2 must test it) in s_occState.
bool occlusionCount(uint member, bool valid, vec4 boundsMin, vec4 boundsMax, uint view)
{
	uint phase = cullUint(u_gpuCullOcc[4].w);
	bool visible = false;
	bool candidate = false;
	if (valid)
	{
		if (phase == 1u)
		{
			bool inside = cullVisible(boundsMin, boundsMax, view);
			// u_gpuCullOcc[5].w (--occlusion-phase2-only, a check): phase 1 rejects everything.
			visible = inside && u_gpuCullOcc[5].w < 0.5 && hizVisible(boundsMin.xyz, boundsMax.xyz);
			candidate = inside && !visible;
		}
		else
		{
			candidate = s_occState[member].y > 0.5;
			visible = candidate && (phase == 3u || hizVisible(boundsMin.xyz, boundsMax.xyz));
		}
	}
	s_occState[member] = vec4(visible ? 1.0 : 0.0, candidate ? 1.0 : 0.0, 0.0, 0.0);
	return visible;
}
