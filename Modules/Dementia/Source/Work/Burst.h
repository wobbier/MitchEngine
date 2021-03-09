#pragma once
#include "Dementia.h"

#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64

#include <wtypes.h>
#include <functional>
#include <assert.h>

static const int kMaxBurstThreads = 32;
namespace Burst
{
	int GetPhysicalProcessorCount();
	int GetMaxBurstThreads();

	void GenerateChunks(std::size_t size, std::size_t num, std::vector<std::pair<int, int>>& OutChunks);
};
#endif
