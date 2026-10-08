// 2018 Mitchell Andrews
#pragma once
#include <cstdint>

/*
Clock.h
High precision clock (std::chrono::steady_clock) measuring time between Update() calls.
Times are kept in double precision so long sessions don't lose resolution.
*/

class Clock
{
public:
    Clock();
    ~Clock();

    void Reset();

    void Update();

    // Seconds since the process-wide clock epoch.
    static double GetTimeInSecondsPrecise();

    float GetTimeInMilliseconds();
    float GetTimeInSeconds();

    // Time between the last two Update() calls.
    const float GetDeltaMilliseconds();
    const float GetDeltaSeconds();
    double GetDeltaSecondsPrecise() const;

    float GetPreviousTime() const;

private:
    double CurrentTime = 0.0;
    double PreviousTime = 0.0;
};
