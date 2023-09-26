// 2018 Mitchell Andrews
#pragma once

/*
Clock.h
A simple high precision engine clock that utilizes system cpu ticks to determine
how much time has time has passed since the last computation.
*/

class Clock
{
public:
    Clock();
    ~Clock();

    void Reset();

    void Update();

    // Converts clock ticks into milliseconds and returns it.
    float GetTimeInMilliseconds();

    // Converts clock ticks into seconds.
    float GetTimeInSeconds();

    const float GetDeltaMilliseconds();
    const float GetDeltaSeconds();

    float GetPreviousTime() const;

private:
    float CurrentTime = 0;
    float PreviousTime = 0;
};