#pragma once
#include <string>
#include <vector>
#include <cstdint>

class BGFXRenderer;

// Drives unattended runs from the command line:
//   --frames N            run N frames then exit (or screenshot then exit)
//   --screenshot <png>    capture the backbuffer after --frames and save it
//   --perf-report <json>  record per-frame CPU times and write a summary on exit
//   --warmup N            frames to ignore at the start of the perf report (default 30)
//   --exit                exit once the above finish (implied by --frames)
class AutomationRunner
{
public:
    void Init();

    bool IsActive() const { return m_isActive; }

    // Called once per frame after rendering. Returns true when the engine should quit.
    bool OnFrameEnd( BGFXRenderer& renderer, double frameMilliseconds );

    void Shutdown();

private:
    void WritePerfReport() const;

    bool m_isActive = false;
    int m_targetFrames = -1;
    int m_warmupFrames = 30;
    uint64_t m_frameIndex = 0;
    std::string m_screenshotPath;
    std::string m_perfReportPath;
    bool m_screenshotRequested = false;
    uint32_t m_screenshotBaseline = 0;
    int m_framesSinceScreenshotRequest = 0;
    std::vector<double> m_frameTimes;
};
