#pragma once
#include <stdint.h>
#include "SystemRegistry.h"

class UpdateContext
{
public:
    explicit UpdateContext() = default;
    inline float GetDeltaTime() const { return DeltaTime; }
    inline uint64_t GetFrameID() const { return FrameID; }

    template<typename T> 
    inline T* GetSystem() const { return m_SystemRegistry->GetSystem<T>(); }

protected:
    inline void UpdateDeltaTime(float inDeltaTime)
    {
        DeltaTime = inDeltaTime;
        FrameID++;
    }

    float DeltaTime = 1.f / 60.f;
    uint64_t FrameID = 0;

    SystemRegistry* m_SystemRegistry = nullptr;
};