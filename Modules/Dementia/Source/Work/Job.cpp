#include "Job.h"

Job::Job(void(*jobFunction)(Job&), Job* parent /*= nullptr*/)
	: JobFuntion{ jobFunction }
	, ParentJob{ parent }
	, UnfinishedJobs{ 1 }
{
	if (ParentJob)
	{
		ParentJob->IncrementUnfinishedChildrenJobs();
	}
}

void Job::Run()
{
	auto jobFunc = JobFuntion;
	if (JobFuntion)
	{
		JobFuntion(*this);

		if (JobFuntion == jobFunc)
		{
			// No callback set
			JobFuntion = nullptr;
		}

		Finish();
	}
}

bool Job::IsFinished() const
{
	return UnfinishedJobs.load(std::memory_order_seq_cst) == 0;
}

bool Job::IsLastJob()
{
	return UnfinishedJobs.load(std::memory_order_seq_cst) == 1;
}

bool Job::DecrementUnfinishedChildrenJobs()
{
	return UnfinishedJobs.fetch_sub(
		1, std::memory_order_seq_cst) == 1;
}

void Job::IncrementUnfinishedChildrenJobs()
{
	UnfinishedJobs.fetch_add(1, std::memory_order_seq_cst);
}

void Job::Finish()
{
	if (DecrementUnfinishedChildrenJobs())
	{
		if (ParentJob)
		{
			ParentJob->Finish();
		}

		if (JobFuntion)
		{
			// Finished Callback
			JobFuntion(*this);
		}
	}
}

void Job::OnFinished(void(*jobCallback)(Job&))
{
	JobFuntion = jobCallback;
}

