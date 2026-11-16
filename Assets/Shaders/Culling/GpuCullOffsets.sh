// GPU culling, pass 2 (see GpuCull.sh): the body of GpuCullOffsets.comp and GpuCullOcclusionOffsets.comp.
// One workgroup per (batch, view) scans the batch's group counts in groupshared memory (CULL_GROUP
// threads per chunk, looping over chunks for batches with more groups than threads, carrying the
// running sum), gives each group its first output slot (y of its scratch entry), and writes the
// batch's total and its draw's indexed indirect arguments: (indexCount, instances, firstIndex 0,
// baseVertex 0), (firstInstance 0). The draw binds the (batch, view) region of the output as its
// instance data, so firstInstance is 0.
//
// CULL_OCCLUSION (ME-404; GpuCullOcclusion.sh), by u_occPhase:
//   1: phase 1 also keeps its total in w of the batch's first member state, for phase 2.
//   2: phase 2 also writes the merged colour draw's arguments, after its own (ArgsBase + batches):
//      phase 1's total plus its own, since phase 2's write pass appends its survivors to phase 1's
//      instances. s_totals.y carries phase 1's total to that write pass.

// Per batch, two vec4s: (memberStart, memberCount, groupStart, groupCount), (indexCount, 0, 0, 0).
BUFFER_RO(s_batchInfo, vec4, 0);
BUFFER_RW(s_groupScratch, vec4, 1);
BUFFER_WO(s_totals, vec4, 2);
BUFFER_WO(s_drawArgs, uvec4, 3);

SHARED uint s_scan[CULL_GROUP];

NUM_THREADS(256, 1, 1)
void main()
{
	uint local = gl_LocalInvocationID.x;
	// Workgroup = view * batches + batch: uniform, so the barriers below are always reached together.
	uint batches = cullUint(u_batches);
	uint item = gl_WorkGroupID.x;
	uint batch = item % batches;
	uint view = item / batches;
	uint info = (cullUint(u_batchBase) + batch) * 2u;
	vec4 info0 = s_batchInfo[info];
	vec4 info1 = s_batchInfo[info + 1u];
	uint first = view * cullUint(u_groups) + cullUint(info0.z);
	uint groupCount = cullUint(info0.w);
	uint carry = 0u;
	for (uint chunk = 0u; chunk < groupCount; chunk += CULL_GROUP)
	{
		uint g = chunk + local;
		uint count = 0u;
		vec4 entry = vec4(0.0, 0.0, 0.0, 0.0);
		if (g < groupCount)
		{
			entry = s_groupScratch[first + g];
			count = cullUint(entry.x);
		}
		// Inclusive scan of the chunk (Hillis-Steele; every thread reaches every barrier).
		s_scan[local] = count;
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
		if (g < groupCount)
		{
			s_groupScratch[first + g] = vec4(entry.x, float(carry + s_scan[local] - count), 0.0, 0.0);
		}
		carry += s_scan[CULL_GROUP - 1u];
		// Everyone has read the chunk's total before the next chunk overwrites the scan.
		barrier();
	}
	if (local == 0u)
	{
		uint earlier = 0u;
#if defined(CULL_OCCLUSION)
		// The batch's first member's state (occlusion culls have one view).
		uint head = cullUint(info0.x);
		vec4 state = s_occState[head];
		if (cullUint(u_occPhase) == 1u)
		{
			s_occState[head] = vec4(state.x, state.y, state.z, float(carry));
		}
		else
		{
			earlier = cullUint(state.w);
			uint merged = (cullUint(u_argsBase) + batches + item) * 2u;
			s_drawArgs[merged] = uvec4(cullUint(info1.x), earlier + carry, 0u, 0u);
			s_drawArgs[merged + 1u] = uvec4(0u, 0u, 0u, 0u);
		}
#endif
		s_totals[item] = vec4(float(carry), float(earlier), 0.0, 0.0);
		uint args = (cullUint(u_argsBase) + item) * 2u;
		s_drawArgs[args] = uvec4(cullUint(info1.x), carry, 0u, 0u);
		s_drawArgs[args + 1u] = uvec4(0u, 0u, 0u, 0u);
	}
}
