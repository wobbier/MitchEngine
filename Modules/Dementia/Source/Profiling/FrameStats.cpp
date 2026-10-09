#include "FrameStats.h"
#include <algorithm>
#include <unordered_map>

namespace
{
    constexpr double kSmoothing = 0.1;
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

    m_frameMilliseconds = InFrameMilliseconds;
    m_smoothedFrame = m_smoothedFrame <= 0.0 ? InFrameMilliseconds : m_smoothedFrame + ( InFrameMilliseconds - m_smoothedFrame ) * kSmoothing;
    m_frameHistory[m_historyOffset] = static_cast<float>( InFrameMilliseconds );
    m_gpuHistory[m_historyOffset] = static_cast<float>( m_render.GpuMilliseconds );
    m_historyOffset = ( m_historyOffset + 1 ) % kHistory;
}
