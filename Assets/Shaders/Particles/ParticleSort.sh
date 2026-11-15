// Shared by the GPU alpha sort (Moonlight::GpuParticleSorter, GpuParticles.cpp): ParticleSortLocal.comp,
// ParticleSortGlobal.comp and ParticleSortGather.comp.
//
// One sort entry per vec4: (key, slot, 0, 0). The key is minus the particle's view-space depth (the
// same depth the CPU path sorts by, RenderParticles), so ascending order is back to front. Ties go by
// slot. Dead slots and the padding up to a power of two get SORT_DEAD_KEY and sort after every live
// particle; padding uses slots from the capacity up, so it sorts after the dead slots too.
//
// The network is a bitonic sort written as "flip" and "disperse" steps that all sort ascending:
//   for h = 2, 4, .. N:  flip(h), then disperse(d) for d = h/2, h/4, .. 2
// flip(h) compares i with its mirror inside each block of h; disperse(d) compares i with i + d/2
// inside each block of d. Steps whose blocks fit in SORT_BLOCK entries run in groupshared memory
// (ParticleSortLocal.comp); the rest run one step per dispatch (ParticleSortGlobal.comp).

// Entries one group sorts in groupshared memory (Moonlight::kGpuParticleSortBlock), two per thread.
#define SORT_BLOCK 512u
#define SORT_THREADS 256
#define SORT_DEAD_KEY 3.0e38

// a belongs after b.
bool sortAfter(vec2 a, vec2 b)
{
	return a.x > b.x || (a.x == b.x && a.y > b.y);
}

// The pair one thread compares in a step of size InSize: (i, j) with i < j.
uvec2 sortPair(uint thread, uint size, bool flip)
{
	uint halfSize = size / 2u;
	uint base = (thread / halfSize) * size;
	uint offset = thread % halfSize;
	uint i = base + offset;
	uint j = flip ? base + size - 1u - offset : i + halfSize;
	return uvec2(i, j);
}
