#pragma once

// Drop-in Optick replacement that writes Chrome Trace JSON (chrome://tracing / Perfetto)
// Outputs <capture_name>.json instead of .opt

#include <atomic>
#include <chrono>
#include <cstdio>
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
        m_recording.store( true, std::memory_order_release );
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
        auto dot = out.rfind( '.' );
        if( dot != std::string::npos ) out = out.substr( 0, dot );
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
    std::atomic<bool>  m_recording{ false };
    std::mutex         m_mutex;
    std::vector<Event> m_events;
};

struct Scope
{
    std::string name;   // copied at construction so temporaries are safe
    const char* cat;
    int64_t     start;

    Scope( const char* n, const char* c ) : name( n ? n : "" ), cat( c ), start( NowUs() ) {}
    ~Scope() { Tracer::Get().Push( name.c_str(), cat, start, NowUs() - start ); }
};

} // namespace ChromeTrace

// OPTICK_EVENT accepts 1 or 2 args: (name) or (name, category)
#define _CT_EVENT_1( name )      ChromeTrace::Scope _ct_##__LINE__( name, "event" )
#define _CT_EVENT_2( name, cat ) ChromeTrace::Scope _ct_##__LINE__( name, #cat )
#define _CT_EVENT_SEL( _1, _2, X, ... ) X
#define OPTICK_EVENT( ... )      _CT_EVENT_SEL( __VA_ARGS__, _CT_EVENT_2, _CT_EVENT_1 )( __VA_ARGS__ );

#define OPTICK_CATEGORY( name, cat ) ChromeTrace::Scope _ct_##__LINE__( name, #cat );
#define OPTICK_FRAME( name )         ChromeTrace::Scope _ct_##__LINE__( name, "frame" );
#define OPTICK_THREAD( name )        ( (void)0 )
#define OPTICK_START_CAPTURE()       ChromeTrace::Tracer::Get().Start()
#define OPTICK_STOP_CAPTURE()        ChromeTrace::Tracer::Get().Stop()
#define OPTICK_SAVE_CAPTURE( path )  ChromeTrace::Tracer::Get().Save( path )
