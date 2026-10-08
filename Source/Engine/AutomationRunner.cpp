#include "PCH.h"
#include "AutomationRunner.h"
#include "Core/CommandLine.h"
#include "Renderer.h"
#include "CLog.h"
#include "JSON.h"
#include "optick.h"
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
    m_tracePath = CommandLine::GetString( "--trace" );

    m_isActive = m_targetFrames > 0 || !m_screenshotPath.empty() || !m_perfReportPath.empty() || !m_tracePath.empty();
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
    const double nowSeconds = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    if( m_lastFrameEndSeconds >= 0.0 && m_frameIndex > static_cast<uint64_t>( m_warmupFrames ) && !m_screenshotRequested )
    {
        m_wallFrameTimes.push_back( ( nowSeconds - m_lastFrameEndSeconds ) * 1000.0 );
    }
    m_lastFrameEndSeconds = nowSeconds;
    if( !m_tracePath.empty() && !m_isTracing && m_frameIndex == static_cast<uint64_t>( m_warmupFrames ) )
    {
        OPTICK_START_CAPTURE();
        m_isTracing = true;
    }
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
    if( m_isTracing )
    {
        OPTICK_STOP_CAPTURE();
        OPTICK_SAVE_CAPTURE( m_tracePath.c_str() );
        m_isTracing = false;
    }
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

    if( !m_wallFrameTimes.empty() )
    {
        const double wallTotal = std::accumulate( m_wallFrameTimes.begin(), m_wallFrameTimes.end(), 0.0 );
        const double wallAverage = wallTotal / static_cast<double>( m_wallFrameTimes.size() );
        report["WallAverageMs"] = wallAverage;
        report["WallAverageFps"] = wallAverage > 0.0 ? 1000.0 / wallAverage : 0.0;
    }

    std::ofstream out( m_perfReportPath );
    out << report.dump( 4 ) << std::endl;
    CLog::Log( CLog::LogType::Info, "Wrote perf report: " + m_perfReportPath );
}
