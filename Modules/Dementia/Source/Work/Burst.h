#pragma once
#include <wtypes.h>
#include <functional>
#include <assert.h>


// The work item queue for each per-chunk worker thread
const int                   g_iSceneQueueSizeInBytes = 16 * 1024;
typedef unsigned char                ChunkQueue[g_iSceneQueueSizeInBytes];

static const int kMaxPerChunkThreads = 32;
static const int m_maxPendingQueueEntries = 1024;     // Max value of g_hBeginPerChunkRenderDeferredSemaphore

class Burst
{
public:
	// The different types of job in the per-chunk work queues
	enum WorkQueueEntryType
	{
		WORK_QUEUE_ENTRY_TYPE_SETUP,
		WORK_QUEUE_ENTRY_TYPE_CHUNK,
		WORK_QUEUE_ENTRY_TYPE_FINALIZE,

		WORK_QUEUE_ENTRY_TYPE_COUNT
	};

	// The contents of the work queues depend on the job type...
	struct WorkQueueEntryBase
	{
		WorkQueueEntryType          m_iType;
	};

	// Work item params for scene setup
	struct WorkQueueEntrySetup : public WorkQueueEntryBase
	{
	};

	// Work item params for chunk render
	struct LambdaWorkEntry : public WorkQueueEntryBase
	{
		std::function<void()> m_callBack;
	};

	// Work item params for scene finalize
	struct WorkQueueEntryFinalize : public WorkQueueEntryBase
	{
	};

	Burst() = default;

	int GetPhysicalProcessorCount();
	void InitializeWorkerThreads();
	static unsigned int _PerChunkRenderDeferredProc(LPVOID lpParameter);

	void PrepareWork();

	void AddWork2(Burst::LambdaWorkEntry& NewWork, int InSize);

	void FinalizeWork();

	int m_numPerChunkRenderThreads = 0;

	ChunkQueue& GetChunkQueue(int i);
	int GetChunkQueueOffset(int i);

	void SetChunkOffset(int i, int size);

	void GenerateChunks(std::size_t size, int num, std::vector<std::pair<int, int>>& OutChunks);

	HANDLE GetBeginSemaphore(int i);
};