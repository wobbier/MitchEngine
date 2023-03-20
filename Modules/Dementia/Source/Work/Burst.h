#pragma once
#include "Dementia.h"

#include <functional>
#include <assert.h>

#if USING( ME_PLATFORM_WINDOWS )
#include <wtypes.h>
#endif

static const int kMaxBurstThreads = 32;
namespace Burst
{
#if USING( ME_PLATFORM_WINDOWS )
	int GetPhysicalProcessorCount();
	int GetMaxBurstThreads();
#endif

	void GenerateChunks(std::size_t size, std::size_t num, std::vector<std::pair<int, int>>& OutChunks);
};
