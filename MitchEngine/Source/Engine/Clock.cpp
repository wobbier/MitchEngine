#include "PCH.h"
#include "Clock.h"

Clock::Clock()
{
	Reset();
}

Clock::~Clock()
{
}

void Clock::Reset()
{
	mTicks = GetHardwareCounterValue();
	mHighResolutionSupport = false;
	mHighResolutionSupport = (bool)QueryPerformanceFrequency(&mTicksPerSecond);
	mTicksPerSecond.QuadPart /= 1000.0f;
	deltaTime = 0.0001f;
}

LARGE_INTEGER Clock::GetHardwareCounterValue()
{
	LARGE_INTEGER v;
	QueryPerformanceCounter(&v);
	return v;
}

float Clock::GetTimeInMilliseconds()
{
	return GetTimeInSeconds() / 1000.0f;
}

float Clock::GetTimeInSeconds()
{
	LARGE_INTEGER ticks = GetHardwareCounterValue();
	double time = (ticks.QuadPart - mTicks.QuadPart) / mTicksPerSecond.QuadPart;
	mTicks = ticks;
	return (float)time;
}

float Clock::TicksToSeconds(LARGE_INTEGER _ticks)
{
	return ((float)_ticks.QuadPart - mTicks.QuadPart) / ((float)mTicksPerSecond.QuadPart);
}

LARGE_INTEGER Clock::GetResolution()
{
	return mTicksPerSecond;
}

LARGE_INTEGER Clock::GetCurrentTicks()
{
	return GetHardwareCounterValue();
}

float Clock::GetCurrentTime()
{
	return TicksToSeconds(GetCurrentTicks());
}

bool Clock::Ready()
{
	return mHighResolutionSupport;
}