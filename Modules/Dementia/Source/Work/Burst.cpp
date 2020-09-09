#include "Burst.h"
#include "CLog.h"
#include "optick.h"
#include <synchapi.h>
#include <assert.h>
#include <algorithm>
#include "Dementia.h"
#include <process.h>

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
int m_perChunkThreadInstanceData[kMaxPerChunkThreads];
int m_iPerChunkQueueOffset[kMaxPerChunkThreads]; // next free portion of the queue to add an entry to
HANDLE m_hPerChunkRenderDeferredThread[kMaxPerChunkThreads];
HANDLE m_hBeginPerChunkRenderDeferredSemaphore[kMaxPerChunkThreads];
HANDLE m_hEndPerChunkRenderDeferredEvent[kMaxPerChunkThreads];
ChunkQueue m_chunkQueue[kMaxPerChunkThreads];

int Burst::GetPhysicalProcessorCount()
{
	DWORD processorCoreCount = 0;

	HMODULE handle = GetModuleHandle(L"kernel32");

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
	m_numPerChunkRenderThreads = GetPhysicalProcessorCount() - 1;
	m_numPerChunkRenderThreads = std::min(m_numPerChunkRenderThreads, kMaxPerChunkThreads);
	m_numPerChunkRenderThreads = std::max(m_numPerChunkRenderThreads, 1);
	//m_numPerChunkRenderThreads = 2;

	for (int i = 0; i < m_numPerChunkRenderThreads; ++i)
	{
		m_perChunkThreadInstanceData[i] = i;

		m_hBeginPerChunkRenderDeferredSemaphore[i] = CreateSemaphore(nullptr, 0, m_maxPendingQueueEntries, nullptr);
		m_hEndPerChunkRenderDeferredEvent[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);

		m_hPerChunkRenderDeferredThread[i] = (HANDLE)_beginthreadex(nullptr, 0, &Burst::_PerChunkRenderDeferredProc, &m_perChunkThreadInstanceData[i], CREATE_SUSPENDED, nullptr);

		ResumeThread(m_hPerChunkRenderDeferredThread[i]);
	}

	if (m_numPerChunkRenderThreads > 0)
	{

	}
}

unsigned int Burst::_PerChunkRenderDeferredProc(LPVOID lpParameter)
{
	OPTICK_THREAD("Burst Thread");

	const int instance = *(int*)lpParameter;
	const ChunkQueue& localQueue = m_chunkQueue[instance];

	int queueOffset = 0;
	forever
	{
		WaitForSingleObject(m_hBeginPerChunkRenderDeferredSemaphore[instance], INFINITE);
		assert(queueOffset < g_iSceneQueueSizeInBytes);

		auto entry = reinterpret_cast<const WorkQueueEntryBase*>(&localQueue[queueOffset]);
		switch (entry->m_iType)
		{
		case WORK_QUEUE_ENTRY_TYPE_SETUP:
		{
			OPTICK_EVENT("WORK_QUEUE_ENTRY_TYPE_SETUP", Optick::Category::Rendering);
			auto pSetupEntry = reinterpret_cast<const WorkQueueEntrySetup*>(entry);

			queueOffset += sizeof(WorkQueueEntrySetup);
			break;
		}
		case WORK_QUEUE_ENTRY_TYPE_CHUNK:
		{
			OPTICK_EVENT("WORK_QUEUE_ENTRY_TYPE_CHUNK", Optick::Category::Rendering);
			auto chunkEntry = reinterpret_cast<const LambdaWorkEntry*>(entry);

			if (chunkEntry->m_callBack)
			{
				chunkEntry->m_callBack();
			}

			queueOffset += sizeof(LambdaWorkEntry);

			break;
		}
		case WORK_QUEUE_ENTRY_TYPE_FINALIZE:
		{
			OPTICK_EVENT("WORK_QUEUE_ENTRY_TYPE_FINALIZE", Optick::Category::Rendering);

			SetEvent(m_hEndPerChunkRenderDeferredEvent[instance]);

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
	for (int i = 0; i < m_numPerChunkRenderThreads; ++i)
	{
		m_iPerChunkQueueOffset[i] = 0;

		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginPerChunkRenderDeferredSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntrySetup);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		WorkQueueEntrySetup* entry = reinterpret_cast<WorkQueueEntrySetup*>(&workerQueue[queueOffset]);
		entry->m_iType = WORK_QUEUE_ENTRY_TYPE_SETUP;

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

	pEntry->m_iType = WORK_QUEUE_ENTRY_TYPE_CHUNK;
	pEntry->m_callBack = NewWork.m_callBack;
	ReleaseSemaphore(hSemaphore, 1, nullptr);

	nextAvailableChunkQueue = ++nextAvailableChunkQueue % m_numPerChunkRenderThreads;
}

void Burst::FinalizeWork()
{
	OPTICK_CATEGORY("Burst::FinalizeWork", Optick::Category::Script);
	for (int i = 0; i < m_numPerChunkRenderThreads; ++i)
	{
		ChunkQueue& workerQueue = m_chunkQueue[i];
		int queueOffset = m_iPerChunkQueueOffset[i];
		HANDLE semaphore = m_hBeginPerChunkRenderDeferredSemaphore[i];

		m_iPerChunkQueueOffset[i] += sizeof(WorkQueueEntryFinalize);
		assert(m_iPerChunkQueueOffset[i] < g_iSceneQueueSizeInBytes);

		auto entry = reinterpret_cast<WorkQueueEntryFinalize*>(&workerQueue[queueOffset]);
		entry->m_iType = WORK_QUEUE_ENTRY_TYPE_FINALIZE;

		ReleaseSemaphore(semaphore, 1, nullptr);
	}
	WaitForMultipleObjects(m_numPerChunkRenderThreads, m_hEndPerChunkRenderDeferredEvent, TRUE, INFINITE);

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
		OutChunks.push_back(std::make_pair(0, size));
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

		OutChunks.push_back(std::make_pair(begin, end));

		begin = end;
	}
}

HANDLE Burst::GetBeginSemaphore(int i)
{
	return m_hBeginPerChunkRenderDeferredSemaphore[i];
}
