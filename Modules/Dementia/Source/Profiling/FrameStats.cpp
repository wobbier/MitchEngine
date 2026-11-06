#include "FrameStats.h"
#include "Jobs/BackgroundLane.h"
#include "Jobs/JobSystem.h"
#include "JSON.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    constexpr double kSmoothing = 0.1;

    double Smooth( double InPrevious, double InValue )
    {
        return InPrevious <= 0.0 ? InValue : InPrevious + ( InValue - InPrevious ) * kSmoothing;
    }

    double ToMilliseconds( uint64_t InNanoseconds )
    {
        return static_cast<double>( InNanoseconds ) / 1.0e6;
    }
}


FrameStats& FrameStats::Get()
{
    static FrameStats instance;
    return instance;
}


FrameStats::FrameStats()
    : m_frameHistory( kHistory, 0.f )
    , m_gpuHistory( kHistory, 0.f )
{
}


void FrameStats::BeginScope( const char* InName )
{
    m_open.push_back( { InName, std::chrono::steady_clock::now(), static_cast<int>( m_open.size() ) } );
}


void FrameStats::EndScope()
{
    if( m_open.empty() )
    {
        return;
    }
    const OpenScope scope = std::move( m_open.back() );
    m_open.pop_back();
    const double milliseconds = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - scope.Start ).count();

    // Same-named scopes in one frame (e.g. several fixed steps) accumulate.
    for( Scope& existing : m_currentScopes )
    {
        if( existing.Name == scope.Name && existing.Depth == scope.Depth )
        {
            existing.Milliseconds += milliseconds;
            return;
        }
    }
    m_currentScopes.push_back( { scope.Name, milliseconds, 0.0, scope.Depth } );
}


void FrameStats::EndFrame( double InFrameMilliseconds )
{
    // Carry smoothed values over by name.
    std::unordered_map<std::string, double> previous;
    for( const Scope& scope : m_lastScopes )
    {
        previous[scope.Name] = scope.Smoothed;
    }
    for( Scope& scope : m_currentScopes )
    {
        auto it = previous.find( scope.Name );
        scope.Smoothed = it == previous.end() ? scope.Milliseconds : it->second + ( scope.Milliseconds - it->second ) * kSmoothing;
    }
    m_lastScopes.swap( m_currentScopes );
    m_currentScopes.clear();

    // The job workers over the same frame. Reads and zeroes the counters: one reader, here.
    static Jobs::Utilization utilization;
    Jobs::JobSystem::GetUnstarted().TakeUtilization( utilization );
    Jobs::BackgroundLane& background = Jobs::BackgroundLane::GetUnstarted();
    background.AppendUtilization( utilization );
    m_jobs.Lanes.resize( utilization.Lanes.size() );
    m_jobs.WorkerCount = 0;
    m_jobs.Jobs = 0;
    m_jobs.WorkerBusyMilliseconds = 0.0;
    m_jobs.MainBusyMilliseconds = 0.0;
    m_jobs.MainJobs = 0;
    m_jobs.BackgroundCount = 0;
    m_jobs.BackgroundJobs = 0;
    m_jobs.BackgroundBusyMilliseconds = 0.0;
    m_jobs.BackgroundQueued = background.IsRunning() ? background.GetQueuedCount() : 0;
    for( size_t i = 0; i < utilization.Lanes.size(); ++i )
    {
        const Jobs::LaneUtilization& source = utilization.Lanes[i];
        JobLane& lane = m_jobs.Lanes[i];
        lane.Kind = source.Kind;
        if( lane.Name != source.Name )
        {
            lane.Name = source.Name;
        }
        lane.BusyMilliseconds = ToMilliseconds( source.BusyNanoseconds );
        lane.Smoothed = Smooth( lane.Smoothed, lane.BusyMilliseconds );
        lane.Jobs = source.Jobs;
        if( source.Kind == Jobs::LaneKind::Worker )
        {
            ++m_jobs.WorkerCount;
            m_jobs.WorkerBusyMilliseconds += lane.BusyMilliseconds;
            m_jobs.Jobs += source.Jobs;
        }
        else if( source.Kind == Jobs::LaneKind::Main )
        {
            m_jobs.MainBusyMilliseconds += lane.BusyMilliseconds;
            m_jobs.MainJobs += source.Jobs;
            m_jobs.Jobs += source.Jobs;
        }
        else if( source.Kind == Jobs::LaneKind::Background )
        {
            ++m_jobs.BackgroundCount;
            m_jobs.BackgroundBusyMilliseconds += lane.BusyMilliseconds;
            m_jobs.BackgroundJobs += source.Jobs;
        }
    }
    m_jobs.WaitMilliseconds = ToMilliseconds( utilization.WaitNanoseconds );
    m_jobs.SmoothedWorkerBusy = Smooth( m_jobs.SmoothedWorkerBusy, m_jobs.WorkerBusyMilliseconds );
    m_jobs.SmoothedWait = Smooth( m_jobs.SmoothedWait, m_jobs.WaitMilliseconds );
    m_jobs.SmoothedJobs = Smooth( m_jobs.SmoothedJobs, static_cast<double>( m_jobs.Jobs ) );

    m_frameMilliseconds = InFrameMilliseconds;
    m_smoothedFrame = m_smoothedFrame <= 0.0 ? InFrameMilliseconds : m_smoothedFrame + ( InFrameMilliseconds - m_smoothedFrame ) * kSmoothing;
    m_frameHistory[m_historyOffset] = static_cast<float>( InFrameMilliseconds );
    m_gpuHistory[m_historyOffset] = static_cast<float>( m_render.GpuMilliseconds );
    m_historyOffset = ( m_historyOffset + 1 ) % kHistory;
}


json FrameStats::ToJson( const JobStats& InStats, double InFrameMilliseconds )
{
    json busy = json::array();
    json jobsPerWorker = json::array();
    json backgroundBusy = json::array();
    for( const JobLane& lane : InStats.Lanes )
    {
        if( lane.Kind == Jobs::LaneKind::Worker )
        {
            busy.push_back( lane.BusyMilliseconds );
            jobsPerWorker.push_back( lane.Jobs );
        }
        else if( lane.Kind == Jobs::LaneKind::Background )
        {
            backgroundBusy.push_back( lane.BusyMilliseconds );
        }
    }
    const double capacity = static_cast<double>( InStats.WorkerCount ) * InFrameMilliseconds;
    const double backgroundCapacity = static_cast<double>( InStats.BackgroundCount ) * InFrameMilliseconds;
    const json background = { { "Count", InStats.BackgroundCount }, { "BusyMs", backgroundBusy }, { "Jobs", InStats.BackgroundJobs },
        { "Queued", InStats.BackgroundQueued },
        { "Utilization", backgroundCapacity > 0.0 ? InStats.BackgroundBusyMilliseconds / backgroundCapacity : 0.0 } };
    return { { "Count", InStats.WorkerCount }, { "BusyMs", busy }, { "Jobs", InStats.Jobs }, { "WaitMs", InStats.WaitMilliseconds },
        { "MainBusyMs", InStats.MainBusyMilliseconds }, { "MainJobs", InStats.MainJobs }, { "JobsPerWorker", jobsPerWorker },
        { "Utilization", capacity > 0.0 ? InStats.WorkerBusyMilliseconds / capacity : 0.0 }, { "Background", background } };
}
