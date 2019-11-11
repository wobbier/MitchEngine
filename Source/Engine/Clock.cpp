#include "PCH.h"
#include "Clock.h"
#include <chrono>

Clock& GetClock()
{
	return Clock::GetInstance();
}

Clock::Clock()
{
	Reset();
}

Clock::~Clock()
{
}

void Clock::Reset()
{
	const float timeStamp = GetTimeInSeconds();

	CurrentTime = timeStamp;
	PreviousTime = timeStamp;
}

void Clock::Update()
{
	const float timeStamp = GetTimeInSeconds();

	PreviousTime = CurrentTime;
	CurrentTime = timeStamp;
}

float Clock::GetTimeInMilliseconds()
{
	return GetTimeInSeconds() / 1000.0f;
}

float Clock::GetTimeInSeconds()
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	const auto timeStamp = std::chrono::high_resolution_clock::now();
	const auto totalSeconds = timeStamp - startTime;

	const float DeltaSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(totalSeconds).count();

	return DeltaSeconds;
}

const float Clock::GetDeltaMilliseconds()
{
	return (CurrentTime - PreviousTime) / 1000.f;
}

const float Clock::GetDeltaSeconds()
{
	return (CurrentTime - PreviousTime);
}
