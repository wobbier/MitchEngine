#include "Job.h"
#include <iostream>
#include "Brofiler.h"

void LoopTest(JobScheduler*, void*)
{
	for (int i = 0; i < 1000; ++i)
	{
		BROFILER_CATEGORY("LoopTest", Brofiler::Color::AliceBlue);

		int j = i + 1;
		j += 10;
	}
}

int main()
{
	for (;;)
	{
		BROFILER_FRAME("Main");
		Job job{ LoopTest, nullptr };
		job.Func(nullptr, job.Data);
		std::cin.get();
	}

	return 0;
}