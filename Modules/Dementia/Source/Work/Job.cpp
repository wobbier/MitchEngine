#include "Job.h"

Job::Job(void(*jobFunction)(Job&), Job* parent /*= nullptr*/)
	: JobFuntion{ jobFunction }
	, ParentJob{ parent }
	, UnfinishedJobs{ 1 }
{
	if (ParentJob)
	{
		ParentJob->UnfinishedJobs++;
	}
}

void Job::Run()
{
	auto jobFunc = JobFuntion;
	if (JobFuntion)
	{
		JobFuntion(*this);
	}

	if (JobFuntion == jobFunc)
	{
		// No callback set
		JobFuntion = nullptr;
	}

	Finish();
}

bool Job::IsFinished() const
{
	return UnfinishedJobs == 0;
}

void Job::Finish()
{
	UnfinishedJobs--;

	if (IsFinished())
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

