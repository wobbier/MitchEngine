// 2015 Mitchell Andrews
#pragma once
#include <Windows.h>
#include "Utility/Singleton.h"

/*
Clock.h
A simple high precision engine clock that utilizes system cpu ticks to determine
how much time has time has passed since the last computation.
*/
namespace MAN
{
	class Clock : public Singleton<Clock>
	{
		friend class Singleton<Clock>;

	public:
		Clock();
		~Clock();

		float deltaTime;

		void Reset();

		// Converts clock ticks into milliseconds and returns it.
		float GetTimeInMilliseconds();

		// Converts clock ticks into seconds.
		float GetTimeInSeconds();

		// Get the tick value from the cpu.
		LARGE_INTEGER GetHardwareCounterValue();

		// Get ticks per second.
		LARGE_INTEGER GetResolution();

		// Get total amount of ticks.
		LARGE_INTEGER GetCurrentTicks();

		// the amount of ticks converted to seconds.
		float TicksToSeconds(LARGE_INTEGER _ticks);

		// Check if the cpu is ready to begin.
		bool Ready();

		// Get the current total time in seconds.
		float GetCurrentTime();

	private:
		LARGE_INTEGER mTicks;
		LARGE_INTEGER mTicksPerSecond;
		bool mHighResolutionSupport;
	};
}