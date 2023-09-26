#pragma once
#include <stdint.h>

class UpdateContext
{
public:
    explicit UpdateContext() = default;
    inline float GetDeltaTime() const {
        return DeltaTime;
    };
    inline uint64_t GetFrameID() const {
        return FrameID;
    };

protected:
    inline void UpdateDeltaTime( float inDeltaTime )
    {
        DeltaTime = inDeltaTime;
        FrameID++;
    }

    float DeltaTime = 1.f / 60.f;
    uint64_t FrameID = 0;
};