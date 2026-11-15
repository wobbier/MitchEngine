// GPU culling, pass 3 (see GpuCull.sh): the body of GpuCullWrite.comp and GpuCullOcclusionWrite.comp
// (CULL_OCCLUSION: visibility comes from pass 1's s_occState instead of the frustum test).

BUFFER_RO(s_members, uint, 0);
BUFFER_RO(s_records, vec4, 1);
BUFFER_RO(s_groupScratch, vec4, 2);
BUFFER_RO(s_groupBatch, vec4, 3);
BUFFER_RO(s_batchInfo, vec4, 4);
BUFFER_RO(s_totals, vec4, 5);
BUFFER_WO(s_instances, vec4, 6);

SHARED uint s_scan[CULL_GROUP];

NUM_THREADS(256, 1, 1)
void main()
{
	uint local = gl_LocalInvocationID.x;
	uint group = gl_WorkGroupID.x;
	uint view = gl_WorkGroupID.y;
	uint id = s_members[cullUint(u_memberBase) + group * CULL_GROUP + local];
	uint record = id * CULL_RECORD_VEC4S;
	uint visible = 0u;
	if (id != CULL_NO_MEMBER)
	{
#if defined(CULL_OCCLUSION)
		// Pass 1's result (the same test here could round differently in another program).
		visible = s_occState[group * CULL_GROUP + local].x > 0.5 ? 1u : 0u;
#else
		visible = cullVisible(s_records[record + 4u], s_records[record + 5u], view) ? 1u : 0u;
#endif
	}

	// Inclusive scan of the visible flags (Hillis-Steele; every thread reaches every barrier).
	s_scan[local] = visible;
	barrier();
	for (uint offset = 1u; offset < CULL_GROUP; offset = offset * 2u)
	{
		uint add = 0u;
		if (local >= offset)
		{
			add = s_scan[local - offset];
		}
		barrier();
		s_scan[local] = s_scan[local] + add;
		barrier();
	}
	if (visible == 0u)
	{
		return;
	}

	uint batch = cullUint(s_groupBatch[cullUint(u_groupBase) + group].x);
	vec4 info0 = s_batchInfo[(cullUint(u_batchBase) + batch) * 2u];
	uint rank = cullUint(s_groupScratch[view * cullUint(u_groups) + group].y) + s_scan[local] - 1u;
	if (u_reverse > 0.5)
	{
		rank = cullUint(s_totals[view * cullUint(u_batches) + batch].x) - 1u - rank;
	}
	uint slot = cullUint(u_outBase) + view * cullUint(u_padded) + cullUint(info0.x) + rank;
	vec4 c0 = s_records[record + 0u];
	vec4 c1 = s_records[record + 1u];
	vec4 c2 = s_records[record + 2u];
	vec4 c3 = s_records[record + 3u];
	if (u_shadow > 0.5)
	{
		c0.w = 0.0;
		c1.w = 0.0;
		c2.w = 0.0;
	}
	s_instances[slot * 4u + 0u] = c0;
	s_instances[slot * 4u + 1u] = c1;
	s_instances[slot * 4u + 2u] = c2;
	s_instances[slot * 4u + 3u] = c3;
}
