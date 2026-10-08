#include "PCH.h"
#include "Clock.h"
#include <chrono>


Clock::Clock()
{
    Reset();
}


Clock::~Clock()
{
}


void Clock::Reset()
{
    const double timeStamp = GetTimeInSecondsPrecise();
    CurrentTime = timeStamp;
    PreviousTime = timeStamp;
}


void Clock::Update()
{
    PreviousTime = CurrentTime;
    CurrentTime = GetTimeInSecondsPrecise();
}


double Clock::GetTimeInSecondsPrecise()
{
    static const auto startTime = std::chrono::steady_clock::now();
    return std::chrono::duration<double>( std::chrono::steady_clock::now() - startTime ).count();
}


float Clock::GetTimeInMilliseconds()
{
    return static_cast<float>( GetTimeInSecondsPrecise() * 1000.0 );
}


float Clock::GetTimeInSeconds()
{
    return static_cast<float>( GetTimeInSecondsPrecise() );
}


const float Clock::GetDeltaMilliseconds()
{
    return static_cast<float>( ( CurrentTime - PreviousTime ) * 1000.0 );
}


const float Clock::GetDeltaSeconds()
{
    return static_cast<float>( CurrentTime - PreviousTime );
}


double Clock::GetDeltaSecondsPrecise() const
{
    return CurrentTime - PreviousTime;
}


float Clock::GetPreviousTime() const
{
    return static_cast<float>( PreviousTime );
}
