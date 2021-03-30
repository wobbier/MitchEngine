#pragma once
#include <vector>

#include "Job.h"

typedef void(*JobFunc)(Job&);

class Pool
{
public:
	Pool(std::size_t InMaxJobs);

	Job* Allocate();
	bool IsFull() const;
	void Clear();

	Job* CreateJob(JobFunc InJobFunc);
	Job* CreateJobAsChild(JobFunc InJobFunc, Job* InParent);

	template<typename Data>
	Job* CreateJob(JobFunc InJobFunc, const Data& InData);
	template<typename Data>
	Job* CreateJobAsChild(JobFunc InJobFunc, const Data& InData, Job* InParent);

	template<typename Function>
	Job* CreateClosureJob(Function InJobFunc, Job* InParent = nullptr);
	template<typename Function>
	Job* CreateClosureJobAsChild(Function InJobFunc, Job* InParent);

private:
	std::size_t AllocatedJobs;
	std::vector<Job> Storage;
};

template<typename Data>
Job* Pool::CreateJob(JobFunc InJobFunc, const Data& InData)
{

	return nullptr;
}

template<typename Data>
Job* Pool::CreateJobAsChild(JobFunc InJobFunc, const Data& InData, Job* InParent)
{

	return nullptr;
}

template<typename Function>
Job* Pool::CreateClosureJob(Function InJobFunc, Job* InParent)
{
	auto jobFunc = [](Job& job)
	{
		const auto& function = job.GetData<Function>();

		function(job);

		function.~Function();
	};

	Job* job = Allocate();
	new(job) Job{jobFunc, InParent};
	job->ConstructData<Function>(InJobFunc);
	return job;
}

template<typename Function>
Job* Pool::CreateClosureJobAsChild(Function InJobFunc, Job* InParent)
{
	return CreateClosureJob(InJobFunc, InParent);
}
