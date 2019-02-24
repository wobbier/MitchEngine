#pragma once

class JobScheduler;

using JobFunc = void(*)(JobScheduler* scheduler, void* data);

struct Job
{
	JobFunc Func;
	void* Data;
};