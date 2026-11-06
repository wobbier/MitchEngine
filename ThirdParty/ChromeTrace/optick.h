#pragma once

// Drop-in Optick replacement that writes Chrome Trace JSON (chrome://tracing / Perfetto)
// Outputs <capture_name>.json instead of .opt

#define ME_CHROMETRACE 1

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Stub Optick::Category so OPTICK_CATEGORY("name", Optick::Category::Foo) compiles unchanged
namespace Optick
{
    namespace Category
    {
        enum Type
        {
            None, GameLogic, Rendering, IO, Physics, Animation, Cloth, Debug,
            Scene, Input, Camera, Visibility, Wait, WaitEmpty, Testing,
            Audio, UI, GPU_Scene
        };
    }
}

namespace ChromeTrace
{

inline int64_t NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch() ).count();
}

inline int ThreadIndex()
{
    static thread_local int s_id = -1;
    if( s_id == -1 )
    {
        static std::atomic<int> s_next{ 0 };
        s_id = s_next.fetch_add( 1, std::memory_order_relaxed );
    }
    return s_id;
}

struct Event
{
    std::string name;   // owned copy — safe against dangling const char* from temporaries
    const char* cat;    // always a string literal from the macro, safe as pointer
    int64_t     ts;
    int64_t     dur;
    int         tid;
};

class Tracer
{
public:
    static Tracer& Get()
    {
        static Tracer inst;
        return inst;
    }

    void Start()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_events.clear();
        // Short-lived threads (std::async) come and go; forget the ones already gone.
        for( auto it = m_threads.begin(); it != m_threads.end(); )
        {
            it = it->second.exitTs != 0 ? m_threads.erase( it ) : std::next( it );
        }
        m_captureStart = NowUs();
        m_recording.store( true, std::memory_order_release );
    }

    // OPTICK_THREAD: names the calling thread's lane. Registered threads are written to every
    // capture, in registration order, whether or not they recorded anything (ME-321).
    void RegisterThread( const char* name )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        Thread& thread = m_threads[ThreadIndex()];
        thread.name = name ? name : "";
        thread.sortIndex = m_nextSortIndex++;
        thread.exitTs = 0;
    }

    // The thread is exiting: captures started after this leave its lane out.
    void UnregisterThread()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        auto it = m_threads.find( ThreadIndex() );
        if( it != m_threads.end() ) it->second.exitTs = NowUs();
    }

    void Stop()
    {
        m_recording.store( false, std::memory_order_release );
    }

    bool IsRecording() const
    {
        return m_recording.load( std::memory_order_acquire );
    }

    void Push( const char* name, const char* cat, int64_t ts, int64_t dur )
    {
        if( !IsRecording() ) return;
        std::lock_guard<std::mutex> lock( m_mutex );
        m_events.push_back( { name, cat, ts, dur, ThreadIndex() } );
    }

    // JSON-escape a string (handles quotes, backslashes, and control chars)
    static std::string JsonEscape( const std::string& s )
    {
        std::string out;
        out.reserve( s.size() );
        for( unsigned char c : s )
        {
            if      ( c == '"'  ) out += "\\\"";
            else if ( c == '\\' ) out += "\\\\";
            else if ( c == '\n' ) out += "\\n";
            else if ( c == '\r' ) out += "\\r";
            else if ( c == '\t' ) out += "\\t";
            else if ( c < 0x20  ) { char buf[8]; snprintf(buf,sizeof(buf),"\\u%04x",c); out+=buf; }
            else                  out += c;
        }
        return out;
    }

    void Save( const char* path )
    {
        std::string out( path ? path : "trace" );
        // Strip the file's extension only: a dot in a folder name (".tmp") isn't one.
        const auto dot = out.rfind( '.' );
        const auto slash = out.find_last_of( "/\\" );
        if( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) ) out = out.substr( 0, dot );
        out += ".json";

        std::lock_guard<std::mutex> lock( m_mutex );
        FILE* f = fopen( out.c_str(), "w" );
        if( !f )
        {
            fprintf( stderr, "[ChromeTrace] Failed to open %s\n", out.c_str() );
            return;
        }

        fprintf( f, "{\"traceEvents\":[\n" );
        bool first = true;
        // Every registered thread's lane, named and ordered, with a marker at the capture's start so
        // viewers show it even when the thread did nothing during the capture.
        const int64_t markerTs = m_captureStart > 0 ? m_captureStart : ( m_events.empty() ? NowUs() : m_events.front().ts );
        // Lanes in registration order, except that names differing only by a number ("Job Worker 2",
        // "Job Worker 10") keep numeric order: workers register in whatever order they start.
        std::vector<std::pair<int, const Thread*>> lanes;
        for( const auto& [tid, thread] : m_threads )
        {
            if( thread.exitTs == 0 || thread.exitTs >= m_captureStart ) lanes.push_back( { tid, &thread } );
        }
        auto stem = []( const std::string& n ) { return n.substr( 0, n.find_last_not_of( "0123456789" ) + 1 ); };
        auto number = []( const std::string& n ) { const size_t d = n.find_last_not_of( "0123456789" ) + 1; return d < n.size() ? std::stol( n.substr( d ) ) : -1L; };
        std::map<std::string, int> firstOfStem;
        for( const auto& lane : lanes )
        {
            const std::string key = stem( lane.second->name );
            auto it = firstOfStem.find( key );
            if( it == firstOfStem.end() || lane.second->sortIndex < it->second ) firstOfStem[key] = lane.second->sortIndex;
        }
        std::sort( lanes.begin(), lanes.end(), [&]( const auto& a, const auto& b ) {
            const int ga = firstOfStem[stem( a.second->name )], gb = firstOfStem[stem( b.second->name )];
            if( ga != gb ) return ga < gb;
            const long na = number( a.second->name ), nb = number( b.second->name );
            return na != nb ? na < nb : a.second->sortIndex < b.second->sortIndex;
        } );
        int laneIndex = 0;
        for( const auto& [tid, threadPtr] : lanes )
        {
            const Thread& thread = *threadPtr;
            if( !first ) fputc( ',', f );
            fprintf( f, "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%d,\"args\":{\"name\":\"%s\"}}\n",
                tid, JsonEscape( thread.name ).c_str() );
            fprintf( f, ",{\"name\":\"thread_sort_index\",\"ph\":\"M\",\"pid\":1,\"tid\":%d,\"args\":{\"sort_index\":%d}}\n",
                tid, laneIndex++ );
            fprintf( f, ",{\"name\":\"Capture\",\"cat\":\"thread\",\"ph\":\"i\",\"s\":\"t\",\"ts\":%.3f,\"pid\":1,\"tid\":%d}\n",
                (double)markerTs, tid );
            first = false;
        }
        for( const auto& e : m_events )
        {
            if( !first ) fputc( ',', f );
            fprintf( f,
                "{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%d}\n",
                JsonEscape( e.name ).c_str(), e.cat, (double)e.ts, (double)e.dur, e.tid );
            first = false;
        }
        fprintf( f, "]}\n" );
        fclose( f );
        fprintf( stdout, "[ChromeTrace] Saved %zu events -> %s\n", m_events.size(), out.c_str() );
    }

private:
    struct Thread
    {
        std::string name;
        int         sortIndex = 0;
        int64_t     exitTs = 0;     // 0 while the thread runs
    };

    std::atomic<bool>     m_recording{ false };
    std::mutex            m_mutex;
    std::vector<Event>    m_events;
    std::map<int, Thread> m_threads;
    int                   m_nextSortIndex = 0;
    int64_t               m_captureStart = 0;
};

// OPTICK_THREAD's scope: registers the thread for its lifetime, like Optick's ThreadScope.
struct ThreadScope
{
    explicit ThreadScope( const char* name ) { Tracer::Get().RegisterThread( name ); }
    ~ThreadScope() { Tracer::Get().UnregisterThread(); }
    ThreadScope( const ThreadScope& ) = delete;
    ThreadScope& operator=( const ThreadScope& ) = delete;
};

// Costs one atomic load when no capture is running: names are only touched while recording.
// Literal names are kept by pointer; OPTICK_EVENT_DYNAMIC copies runtime names so temporaries are safe.
struct Scope
{
    const char* name = nullptr;
    const char* cat = nullptr;
    int64_t     start = -1;
    std::string ownedName;

    Scope( const char* n, const char* c, bool copyName = false )
        : name( n )
        , cat( c )
    {
        if( Tracer::Get().IsRecording() )
        {
            if( copyName && n )
            {
                ownedName = n;
                name = ownedName.c_str();
            }
            start = NowUs();
        }
    }

    ~Scope()
    {
        if( start >= 0 )
        {
            Tracer::Get().Push( name ? name : "", cat, start, NowUs() - start );
        }
    }
};

} // namespace ChromeTrace

// OPTICK_EVENT accepts 1 or 2 args: (name) or (name, category)
#define _CT_CONCAT_IMPL( a, b ) a##b
#define _CT_CONCAT( a, b ) _CT_CONCAT_IMPL( a, b )
#define _CT_EVENT_1( name )      ChromeTrace::Scope _CT_CONCAT( _ct_, __LINE__ )( name, "event" )
#define _CT_EVENT_2( name, cat ) ChromeTrace::Scope _CT_CONCAT( _ct_, __LINE__ )( name, #cat )
#define _CT_EVENT_SEL( _1, _2, X, ... ) X
#define OPTICK_EVENT( ... )      _CT_EVENT_SEL( __VA_ARGS__, _CT_EVENT_2, _CT_EVENT_1 )( __VA_ARGS__ );

#define OPTICK_EVENT_DYNAMIC( name ) ChromeTrace::Scope _CT_CONCAT( _ct_, __LINE__ )( name, "event", true );
#define OPTICK_CATEGORY( name, cat ) ChromeTrace::Scope _CT_CONCAT( _ct_, __LINE__ )( name, #cat );
#define OPTICK_FRAME( name )         ChromeTrace::Scope _CT_CONCAT( _ct_, __LINE__ )( name, "frame" );
#define OPTICK_THREAD( name )        ChromeTrace::ThreadScope _CT_CONCAT( _ct_thread_, __LINE__ )( name );
#define OPTICK_START_CAPTURE()       ChromeTrace::Tracer::Get().Start()
#define OPTICK_STOP_CAPTURE()        ChromeTrace::Tracer::Get().Stop()
#define OPTICK_SAVE_CAPTURE( path )  ChromeTrace::Tracer::Get().Save( path )
