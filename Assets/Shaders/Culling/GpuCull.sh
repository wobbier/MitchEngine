// GPU-driven culling of retained static meshes (ME-389; Moonlight::GpuMeshCulling, GpuMeshCulling.cpp).
// Shared by GpuCullCount.comp, GpuCullOffsets.comp and GpuCullWrite.comp.
//
// A cull set is a list of instance batches (the opaque batches, or the shadow caster batches). Its
// members (mesh command ids) sit in s_members, ascending inside each batch, and every batch starts on a
// whole group of CULL_GROUP entries (padding entries are CULL_NO_MEMBER), so a group belongs to one
// batch. One set is culled against up to CULL_MAX_VIEWS views at once (dispatch y = the view):
//   1. GpuCullCount: per (group, view), how many of its members are inside the view.
//   2. GpuCullOffsets: per (batch, view), the running sum of its groups' counts (each group's first
//      output slot), the total, and the draw's indexed indirect arguments (total instances).
//   3. GpuCullWrite: every visible member writes its instance matrix to its rank in the (batch, view)
//      region of the output, so the instances keep member order (the CPU path's order) and the result
//      doesn't depend on thread timing. Reversed under a depth prepass, like the CPU path.
// Counts and offsets travel as floats (exact to 2^24), buffers are vec4 (D3D11 types them RGBA32F),
// ids are uint in an index buffer.
#include "bgfx_compute.sh"

#define CULL_GROUP 256u
#define CULL_MAX_VIEWS 16u
#define CULL_NO_MEMBER 0xffffffffu
// Per mesh command id: the instance matrix (four columns; InstanceData in the first three w), the
// world bounds min (w: 1 when valid) and max.
#define CULL_RECORD_VEC4S 6u

// Packed by Moonlight::GpuMeshCulling::Cull: three vec4s of counts, then six planes per view.
uniform vec4 u_gpuCull[3 + 6 * 16];
#define u_memberBase u_gpuCull[0].x
#define u_padded     u_gpuCull[0].y
#define u_groups     u_gpuCull[0].z
#define u_batches    u_gpuCull[0].w
#define u_batchBase  u_gpuCull[1].x
#define u_views      u_gpuCull[1].y
#define u_argsBase   u_gpuCull[1].z
#define u_outBase    u_gpuCull[1].w
#define u_reverse    u_gpuCull[2].x
#define u_shadow     u_gpuCull[2].y
#define u_groupBase  u_gpuCull[2].z

uint cullUint(float value)
{
	return uint(value + 0.5);
}

// The CPU's Frustum::Intersects(AABB): out only when the box's corner furthest along a plane's normal
// is behind it.
bool cullVisible(vec4 boundsMin, vec4 boundsMax, uint view)
{
	if (boundsMin.w < 0.5)
	{
		return false;
	}
	for (uint p = 0u; p < 6u; ++p)
	{
		vec4 plane = u_gpuCull[3u + view * 6u + p];
		vec3 corner = vec3(
			plane.x >= 0.0 ? boundsMax.x : boundsMin.x,
			plane.y >= 0.0 ? boundsMax.y : boundsMin.y,
			plane.z >= 0.0 ? boundsMax.z : boundsMin.z);
		if (dot(plane.xyz, corner) + plane.w < 0.0)
		{
			return false;
		}
	}
	return true;
}
