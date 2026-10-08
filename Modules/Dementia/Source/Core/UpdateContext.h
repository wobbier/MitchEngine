#pragma once
#include <stdint.h>
#include "SystemRegistry.h"

// Per-frame timing and system access handed to every update.
//
//   GetDeltaTime()          scaled frame delta (0 while paused); use for gameplay in Update.
//   GetUnscaledDeltaTime()  real frame delta (UI, cameras, editor tools).
//   GetFixedDeltaTime()     the fixed simulation step; FixedUpdate always advances by this.
//   GetInterpolationAlpha() 0..1 progress toward the next fixed step, for smoothing render state.
class UpdateContext
{
public:
    explicit UpdateContext() = default;

    inline float GetDeltaTime() const {
        return IsFixedStepActive ? FixedDeltaTime : DeltaTime;
    };

    inline float GetUnscaledDeltaTime() const {
        return UnscaledDeltaTime;
    }

    inline float GetFixedDeltaTime() const {
        return FixedDeltaTime;
    }

    inline float GetInterpolationAlpha() const {
        return InterpolationAlpha;
    }

    inline float GetTimeScale() const {
        return TimeScale;
    }

    inline bool IsPaused() const {
        return Paused;
    }

    // True while FixedUpdate callbacks run.
    inline bool IsFixedStep() const {
        return IsFixedStepActive;
    }

    // Scaled game time in seconds.
    inline float GetTotalTime() const {
        return static_cast<float>( TotalTime );
    };

    inline double GetTotalTimePrecise() const {
        return TotalTime;
    }

    inline double GetUnscaledTotalTime() const {
        return UnscaledTotalTime;
    }

    inline uint64_t GetFrameID() const {
        return FrameID;
    };

    template<typename T>
    inline T* GetSystem() const {
        return m_SystemRegistry->GetSystem<T>();
    }

protected:
    inline void UpdateDeltaTime( float inDeltaTime )
    {
        BeginFrame( inDeltaTime, inDeltaTime, 1.f, false );
    }

    inline void BeginFrame( float inScaledDelta, float inUnscaledDelta, float inTimeScale, bool inPaused )
    {
        DeltaTime = inScaledDelta;
        UnscaledDeltaTime = inUnscaledDelta;
        TimeScale = inTimeScale;
        Paused = inPaused;
        TotalTime += inScaledDelta;
        UnscaledTotalTime += inUnscaledDelta;
        FrameID++;
    }

    float DeltaTime = 1.f / 60.f;
    float UnscaledDeltaTime = 1.f / 60.f;
    float FixedDeltaTime = 1.f / 60.f;
    float InterpolationAlpha = 0.f;
    float TimeScale = 1.f;
    bool Paused = false;
    bool IsFixedStepActive = false;
    double TotalTime = 0.0;
    double UnscaledTotalTime = 0.0;
    uint64_t FrameID = 0;
    SystemRegistry* m_SystemRegistry = nullptr;
};
