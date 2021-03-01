#include "Burst.h"
#include "CLog.h"
#include "optick.h"
#include <synchapi.h>
#include <assert.h>
#include <algorithm>
#include "Dementia.h"
#include <process.h>
#include "Utils/StringUtils.h"

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
int m_perThreadInstanceData[kMaxPerChunkThreads];
int m_iPerChunkQueueOffset[kMaxPerChunkThreads]; // next free portion of the queue to add an entry to
HANDLE g_threads[kMaxPerChunkThreads];
HANDLE m_hBeginWorkSemaphore[kMaxPerChunkThreads];
HANDLE m_hEndWorkEvent[kMaxPerChunkThreads];
ChunkQueue m_chunkQueue[kMaxPerChunkThreads];

HMODULE GetKernelModule()
{
	MEMORY_BASIC_INFORMATION mbi = { 0 };
	VirtualQuery(VirtualQuery, &mbi, sizeof(mbi));
	return reinterpret_cast<HMODULE>(mbi.AllocationBase);
}

int Burst::GetPhysicalProcessorCount()
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


void Burst::InitializeWorkerThreads()
{
	ThreadCount = GetPhysicalProcessorCount() - 1;
	ThreadCount = std::min(ThreadCount, kMaxPerChunkThreads);
	ThreadCount = std::max(ThreadCount, 1);
	//m_numPerChunkRenderThreads = 2;

	for (int i = 0; i < ThreadCount; ++i)
	{
		m_perThreadInstanceData[i] = i;

		m_hBeginWorkSemaphore[i] = CreateSemaphore(nullptr, 0, kMaxPendingQueueEntries, nullptr);
		m_hEndWorkEvent[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		g_threads[i] = (HANDLE)_beginthreadex(nullptr, 0, &Burst::_BurstThread, &m_perThreadInstanceData[i], CREATE_SUSPENDED, nullptr);
		//PCWSTR asdasd = StringUtils::ToWString("Burst Thread " + i).c_str();

		ResumeThread(g_threads[i]);
	}

	if (ThreadCount > 0)
	{

	}
}

unsigned int Burst::_BurstThread(LPVOID lpParameter)
{
	OPTICK_THREAD("Burst Thread");

	const int instance = *(int*)lpParameter;
	const ChunkQueue& localQueue = m_chunkQueue[instance];

	int queueOffset = 0;
	forever
	{
		WaitForSingleObject(m_hBeginWorkSemaphore[instance], INFINITE);
		assert(queueOffset < g_iSceneQueueSizeInBytes);

		auto entry = reinterpret_cast<const WorkQueueEntryBase*>(&localQueue[queueOffset]);
		switch (entry->m_iType)
		{
		case Setup:
		{
			OPTICK_EVENT("B::Setup", Optick::Category::Wait);
			auto setupEntry = reinterpret_cast<const WorkQueueEntrySetup*>(entry);

			if (setupEntry->m_callBack)
			{
				setupEntry->m_callBack(instance);
			}

			queueOffset += sizeof(WorkQueueEntrySetup);
			break;
		}
		case Work:
		{
			OPTICK_EVENT("B::Job", Optick::Category::Wait);
			auto chunkEntry = reinterpret_cast<const LambdaWorkEntry*>(entry);

			if (chunkEntry->m_callBack)
			{
				chunkEntry->m_callBack(instance);
			}

			queueOffset += sizeof(LambdaWorkEntry);

			break;
		}
		case Finalize:
		{
			OPTICK_EVENT("B::Finish", Optick::Category::Wait);

			auto chunkEntry = reinterpret_cast<const LambdaWorkEntry*>(entry);
			if (chunkEntry->m_callBack)
			{
				chunkEntry->m_callBack(instance);
			}

			SetEvent(m_hEndWorkEvent[instance]);

			queueOffset = 0;
			break;
		}
		default:
			assert(false);
			break;

		}
	}
	return 0;
}

void Burst::PrepareWork()
{
	OPTICK_CATEGORY("Burst::PrepareWork", Optick::Category::Script);
	for (int i = 0; i < ThreadCount; ++i)
	{
		m_iPerChunkQueueOffset[i] = 0;

		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginWorkSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntrySetup);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		WorkQueueEntrySetup* entry = reinterpret_cast<WorkQueueEntrySetup*>(&workerQueue[queueOffset]);
		entry->m_iType = Setup;
		entry->m_callBack = nullptr;

		ReleaseSemaphore(semaphore, 1, nullptr);
	}
}

void Burst::PrepareWork(std::function<void(int Index)> PerThreadPrep)
{
	OPTICK_CATEGORY("Burst::PrepareWork", Optick::Category::Script);
	for (int i = 0; i < ThreadCount; ++i)
	{
		m_iPerChunkQueueOffset[i] = 0;

		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginWorkSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntrySetup);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		WorkQueueEntrySetup* entry = reinterpret_cast<WorkQueueEntrySetup*>(&workerQueue[queueOffset]);
		entry->m_iType = Setup;
		entry->m_callBack = PerThreadPrep;

		ReleaseSemaphore(semaphore, 1, nullptr);
	}
}

static int nextAvailableChunkQueue = 0;
void Burst::AddWork2(Burst::LambdaWorkEntry& NewWork, int InSize)
{
	OPTICK_CATEGORY("B::Add Work", Optick::Category::Debug);

	ChunkQueue& WorkerQueue = GetChunkQueue(nextAvailableChunkQueue);
	int iQueueOffset = GetChunkQueueOffset(nextAvailableChunkQueue);
	HANDLE hSemaphore = GetBeginSemaphore(nextAvailableChunkQueue);

	SetChunkOffset(nextAvailableChunkQueue, InSize);
	assert(GetChunkQueueOffset(nextAvailableChunkQueue) < g_iSceneQueueSizeInBytes);

	auto pEntry = reinterpret_cast<Burst::LambdaWorkEntry*>(&WorkerQueue[iQueueOffset]);

	pEntry->m_iType = Work;
	pEntry->m_callBack = NewWork.m_callBack;
	ReleaseSemaphore(hSemaphore, 1, nullptr);

	nextAvailableChunkQueue = ++nextAvailableChunkQueue % ThreadCount;
}

void Burst::FinalizeWork()
{
	OPTICK_CATEGORY("Burst::FinalizeWork", Optick::Category::Script);
	for (int i = 0; i < ThreadCount; ++i)
	{
		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginWorkSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntryFinalize);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		auto entry = reinterpret_cast<WorkQueueEntryFinalize*>(&workerQueue[queueOffset]);
		entry->m_iType = Finalize;
		entry->m_callBack = nullptr;

		ReleaseSemaphore(semaphore, 1, nullptr);
	}
	WaitForMultipleObjects(ThreadCount, m_hEndWorkEvent, TRUE, INFINITE);

}

void Burst::FinalizeWork(std::function<void(int Index)> PerThreadFinal)
{
	OPTICK_CATEGORY("Burst::FinalizeWork", Optick::Category::Script);
	for (int i = 0; i < ThreadCount; ++i)
	{
		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginWorkSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntryFinalize);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		auto entry = reinterpret_cast<WorkQueueEntryFinalize*>(&workerQueue[queueOffset]);
		entry->m_iType = Finalize;
		entry->m_callBack = PerThreadFinal;

		ReleaseSemaphore(semaphore, 1, nullptr);
	}
	WaitForMultipleObjects(ThreadCount, m_hEndWorkEvent, TRUE, INFINITE);
}

ChunkQueue& Burst::GetChunkQueue(int i)
{
	return m_chunkQueue[i];
}

int Burst::GetChunkQueueOffset(int i)
{
	return m_iPerChunkQueueOffset[i];
}

void Burst::SetChunkOffset(int i, int size)
{
	m_iPerChunkQueueOffset[i] += size;
}

void Burst::GenerateChunks(std::size_t size, std::size_t num, std::vector<std::pair<int, int>>& OutChunks)
{
	OPTICK_CATEGORY("GenerateChunks", Optick::Category::Rendering);
	OutChunks.clear();

	if (size <= 0)
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

HANDLE Burst::GetBeginSemaphore(int i)
{
	return m_hBeginWorkSemaphore[i];
}

int Burst::GetNumThreads()
{
	return ThreadCount;
}
