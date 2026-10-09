#pragma once
// Per-frame performance numbers for in-engine display (editor stats overlay, profiler window,
// game debug tools): named CPU scopes recorded on the main thread, frame time history and the
// renderer's GPU/draw statistics.
//
//   { ME_STAT_SCOPE( "Physics" ); ... }
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class FrameStats
{
public:
    static FrameStats& Get();

    struct Scope
    {
        std::string Name;
        double Milliseconds = 0.0;
        double Smoothed = 0.0;   // exponential moving average, for readable displays
        int Depth = 0;
    };

    struct GpuView
    {
        std::string Name;
        double CpuMilliseconds = 0.0;
        double GpuMilliseconds = 0.0;
    };

    struct RenderStats
    {
        double GpuMilliseconds = 0.0;
        double RenderThreadMilliseconds = 0.0;
        uint32_t DrawCalls = 0;
        uint32_t Triangles = 0;
        uint32_t Instances = 0;
        int64_t GpuMemoryUsed = 0;
        int64_t GpuMemoryMax = 0;
        std::vector<GpuView> Views;
    };

    static constexpr size_t kHistory = 240;

    // Main thread. Scopes nest; times are wall clock.
    void BeginScope( const char* InName );
    void EndScope();

    // Called by the engine once per frame (after rendering).
    void EndFrame( double InFrameMilliseconds );

    const std::vector<Scope>& GetScopes() const { return m_lastScopes; }
    const std::vector<float>& GetFrameHistory() const { return m_frameHistory; }
    const std::vector<float>& GetGpuHistory() const { return m_gpuHistory; }
    size_t GetHistoryOffset() const { return m_historyOffset; }
    double GetFrameMilliseconds() const { return m_frameMilliseconds; }
    double GetSmoothedFrameMilliseconds() const { return m_smoothedFrame; }
    double GetFramesPerSecond() const { return m_smoothedFrame > 0.0 ? 1000.0 / m_smoothedFrame : 0.0; }

    RenderStats& GetRenderStats() { return m_render; }
    const RenderStats& GetRenderStats() const { return m_render; }

    // When true the renderer gathers per-view GPU timings (costs a little).
    bool DetailedGpuTimings = false;

private:
    FrameStats();

    struct OpenScope
    {
        std::string Name;
        std::chrono::steady_clock::time_point Start;
        int Depth = 0;
    };

    std::vector<OpenScope> m_open;
    std::vector<Scope> m_currentScopes;
    std::vector<Scope> m_lastScopes;
    std::vector<float> m_frameHistory;
    std::vector<float> m_gpuHistory;
    size_t m_historyOffset = 0;
    double m_frameMilliseconds = 0.0;
    double m_smoothedFrame = 0.0;
    RenderStats m_render;
};

class ScopedFrameStat
{
public:
    explicit ScopedFrameStat( const char* InName ) { FrameStats::Get().BeginScope( InName ); }
    ~ScopedFrameStat() { FrameStats::Get().EndScope(); }
    ScopedFrameStat( const ScopedFrameStat& ) = delete;
    ScopedFrameStat& operator=( const ScopedFrameStat& ) = delete;
};

#define ME_STAT_CONCAT_IMPL( a, b ) a##b
#define ME_STAT_CONCAT( a, b ) ME_STAT_CONCAT_IMPL( a, b )
#define ME_STAT_SCOPE( name ) ScopedFrameStat ME_STAT_CONCAT( s_meFrameStat_, __LINE__ )( name )
