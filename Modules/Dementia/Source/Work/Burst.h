#pragma once
#include "Dementia.h"

#include <functional>
#include <assert.h>

#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
#include <wtypes.h>
#endif

static const int kMaxBurstThreads = 32;
namespace Burst
{
#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
	int GetPhysicalProcessorCount();
	int GetMaxBurstThreads();
#endif

	void GenerateChunks(std::size_t size, std::size_t num, std::vector<std::pair<int, int>>& OutChunks);
};
