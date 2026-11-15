// GPU culling, pass 1 (see GpuCull.sh): how many members of each group are inside each view.
// The body of GpuCullCount.comp (frustum) and GpuCullOcclusionCount.comp (CULL_OCCLUSION: frustum
// and depth pyramid, GpuCullOcclusion.sh, which also records each member's result for pass 3).

BUFFER_RO(s_members, uint, 0);
BUFFER_RO(s_records, vec4, 1);
BUFFER_WO(s_groupScratch, vec4, 2);

SHARED uint s_count[CULL_GROUP];

NUM_THREADS(256, 1, 1)
void main()
{
	uint local = gl_LocalInvocationID.x;
	uint group = gl_WorkGroupID.x;
	uint view = gl_WorkGroupID.y;
	uint id = s_members[cullUint(u_memberBase) + group * CULL_GROUP + local];
	uint visible = 0u;
#if defined(CULL_OCCLUSION)
	vec4 boundsMin = vec4(0.0, 0.0, 0.0, 0.0);
	vec4 boundsMax = vec4(0.0, 0.0, 0.0, 0.0);
	if (id != CULL_NO_MEMBER)
	{
		boundsMin = s_records[id * CULL_RECORD_VEC4S + 4u];
		boundsMax = s_records[id * CULL_RECORD_VEC4S + 5u];
	}
	visible = occlusionCount(group * CULL_GROUP + local, id != CULL_NO_MEMBER, boundsMin, boundsMax, view) ? 1u : 0u;
#else
	if (id != CULL_NO_MEMBER)
	{
		uint record = id * CULL_RECORD_VEC4S;
		visible = cullVisible(s_records[record + 4u], s_records[record + 5u], view) ? 1u : 0u;
	}
#endif
	s_count[local] = visible;
	barrier();
	for (uint stride = CULL_GROUP / 2u; stride > 0u; stride = stride / 2u)
	{
		if (local < stride)
		{
			s_count[local] = s_count[local] + s_count[local + stride];
		}
		barrier();
	}
	if (local == 0u)
	{
		s_groupScratch[view * cullUint(u_groups) + group] = vec4(float(s_count[0]), 0.0, 0.0, 0.0);
	}
}
