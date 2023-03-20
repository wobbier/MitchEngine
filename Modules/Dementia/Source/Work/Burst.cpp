#include "Burst.h"
#include "CLog.h"
#include "optick.h"
#include "Dementia.h"
#include "Utils/StringUtils.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <synchapi.h>
#include <assert.h>
#include <algorithm>
#include <process.h>
#endif
#include <utility>

namespace Burst
{
#if USING( ME_PLATFORM_WINDOWS )
	typedef BOOL(WINAPI* LPFN_GLPI)(
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
		PDWORD);

	inline DWORD CountBits(ULONG_PTR bitMask)
	{
		DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
		DWORD bitSetCount = 0;
		ULONG_PTR bitTest = ULONG_PTR(1) << LSHIFT;
		DWORD i;

		for (i = 0; i < LSHIFT; ++i)
		{
			bitSetCount += ((bitMask & bitTest) ? 1 : 0);
			bitTest /= 2;
		}

		return bitSetCount;
	}

	HMODULE GetKernelModule()
	{
		MEMORY_BASIC_INFORMATION mbi = { 0 };
		VirtualQuery(VirtualQuery, &mbi, sizeof(mbi));
		return reinterpret_cast<HMODULE>(mbi.AllocationBase);
	}

	int GetPhysicalProcessorCount()
	{
		DWORD processorCoreCount = 0;

#if ME_PLATFORM_UWP
		HMODULE handle = GetKernelModule();
#else
		HMODULE handle = GetModuleHandle(L"kernel32");
#endif
		auto procInfo = reinterpret_cast<LPFN_GLPI>(GetProcAddress(handle, "GetLogicalProcessorInformation"));
		if (!procInfo)
		{
			YIKES("Failed to get logical processor information.");
			return processorCoreCount;
		}

		bool done = false;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = nullptr;
		DWORD returnLength = 0;

		while (!done)
		{
			BOOL rc = procInfo(buffer, &returnLength);
			if (rc == FALSE)
			{
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				{
					if (buffer)
					{
						free(buffer);
					}

					buffer = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>(malloc(returnLength));
					if (!buffer)
					{
						return processorCoreCount;
					}
				}
				else
				{
					return processorCoreCount;
				}
			}
			else
			{
				done = true;
			}
		}

		assert(buffer);

		DWORD byteOffset = 0;
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION info = buffer;
		while (byteOffset < returnLength)
		{
			if (info->Relationship == RelationProcessorCore)
			{
				if (info->ProcessorCore.Flags)
				{
					// Hyperthreading or SMT enabled.
					processorCoreCount += 1;
				}
				else
				{
					processorCoreCount += CountBits(info->ProcessorMask);
				}
			}

			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			info++;
		}

		return processorCoreCount;
	}


	int GetMaxBurstThreads()
	{
		int ThreadCount = 0;
		ThreadCount = GetPhysicalProcessorCount() - 1;
		ThreadCount = std::min(ThreadCount, kMaxBurstThreads);
		ThreadCount = std::max(ThreadCount, 1);
		return ThreadCount;
	}
#endif

	void GenerateChunks(std::size_t size, std::size_t num, std::vector<std::pair<int, int>>& OutChunks)
	{
		OPTICK_CATEGORY("GenerateChunks", Optick::Category::Rendering);
		OutChunks.clear();

		if (size == 0)
		{
			return;
		}

		if (size <= num)
		{
			OutChunks.push_back(std::make_pair(0, static_cast<int>(size)));
			return;
		}

		OutChunks.reserve(num);

		size_t length = size / num;
		size_t remain = size % num;

		size_t begin = 0;
		size_t end = 0;

		for (size_t i = 0; i < std::min(num, size); ++i)
		{
			end += (remain > 0) ? (length + !!(remain--)) : length;

			OutChunks.push_back(std::make_pair(static_cast<int>(begin), static_cast<int>(end)));

			begin = end;
		}
	}
}
