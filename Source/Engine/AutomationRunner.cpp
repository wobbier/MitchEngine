#include "PCH.h"
#include "AutomationRunner.h"
#include "Core/CommandLine.h"
#include "Renderer.h"
#include "CLog.h"
#include "JSON.h"
#include <algorithm>
#include <fstream>
#include <numeric>

namespace
{
    // Screenshots are resolved by bgfx a couple of frames after the request.
    constexpr int kMaxFramesToWaitForScreenshot = 30;
}


void AutomationRunner::Init()
{
    m_targetFrames = CommandLine::GetInt( "--frames", -1 );
    m_warmupFrames = CommandLine::GetInt( "--warmup", 30 );
    m_screenshotPath = CommandLine::GetString( "--screenshot" );
    m_perfReportPath = CommandLine::GetString( "--perf-report" );

    m_isActive = m_targetFrames > 0 || !m_screenshotPath.empty() || !m_perfReportPath.empty();
    if( m_isActive )
    {
        m_frameTimes.reserve( m_targetFrames > 0 ? m_targetFrames : 4096 );
        CLog::Log( CLog::LogType::Info, "Automation run: frames=" + std::to_string( m_targetFrames ) + " screenshot='" + m_screenshotPath + "' perf='" + m_perfReportPath + "'" );
    }
}


bool AutomationRunner::OnFrameEnd( BGFXRenderer& renderer, double frameMilliseconds )
{
    if( !m_isActive )
    {
        return false;
    }

    ++m_frameIndex;
    if( m_frameIndex > static_cast<uint64_t>( m_warmupFrames ) && !m_screenshotRequested )
    {
        m_frameTimes.push_back( frameMilliseconds );
    }

    if( m_targetFrames <= 0 || m_frameIndex < static_cast<uint64_t>( m_targetFrames ) )
    {
        return false;
    }

    if( m_screenshotPath.empty() )
    {
        return true;
    }

    if( !m_screenshotRequested )
    {
        m_screenshotBaseline = renderer.GetScreenshotCount();
        renderer.RequestScreenshot( m_screenshotPath );
        m_screenshotRequested = true;
        return false;
    }

    ++m_framesSinceScreenshotRequest;
    if( renderer.GetScreenshotCount() > m_screenshotBaseline )
    {
        return true;
    }

    if( m_framesSinceScreenshotRequest > kMaxFramesToWaitForScreenshot )
    {
        CLog::Log( CLog::LogType::Error, "Automation run: screenshot was never delivered by the renderer." );
        return true;
    }
    return false;
}


void AutomationRunner::Shutdown()
{
    if( !m_perfReportPath.empty() )
    {
        WritePerfReport();
    }
}


void AutomationRunner::WritePerfReport() const
{
    json report;
    report["Frames"] = m_frameTimes.size();
    report["WarmupFrames"] = m_warmupFrames;

    if( !m_frameTimes.empty() )
    {
        std::vector<double> sorted = m_frameTimes;
        std::sort( sorted.begin(), sorted.end() );
        auto percentile = [&sorted]( double p ) {
            const size_t index = std::min( sorted.size() - 1, static_cast<size_t>( p * static_cast<double>( sorted.size() - 1 ) + 0.5 ) );
            return sorted[index];
        };
        const double total = std::accumulate( sorted.begin(), sorted.end(), 0.0 );
        const double average = total / static_cast<double>( sorted.size() );

        report["AverageMs"] = average;
        report["AverageFps"] = average > 0.0 ? 1000.0 / average : 0.0;
        report["MinMs"] = sorted.front();
        report["MaxMs"] = sorted.back();
        report["P50Ms"] = percentile( 0.50 );
        report["P95Ms"] = percentile( 0.95 );
        report["P99Ms"] = percentile( 0.99 );
    }

    std::ofstream out( m_perfReportPath );
    out << report.dump( 4 ) << std::endl;
    CLog::Log( CLog::LogType::Info, "Wrote perf report: " + m_perfReportPath );
}
