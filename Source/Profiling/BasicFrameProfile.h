#pragma once
#include "Singleton.h"
#include "Engine/Clock.h"

// This should just be profiling?
#if USING( ME_BASIC_PROFILER )
#define ME_FRAMEPROFILE_START( name, category ) FrameProfile::GetInstance().Set( name, category );
#define ME_FRAMEPROFILE_STOP( name ) FrameProfile::GetInstance().Complete( name );
#else
#define ME_FRAMEPROFILE_START( name, category )
#define ME_FRAMEPROFILE_STOP( name )
#endif

enum class ProfileCategory : std::size_t
{
    Game = 0,
    UI,
    Physics,
    Rendering,
    Count
};

#if USING( ME_BASIC_PROFILER )

class ProfileInfo
{
public:
    ProfileCategory Category = ProfileCategory::Count;
    Clock Timer;
};

class FrameProfile
{
    friend class FrameProfileObject;

    ME_SINGLETON_DEFINITION( FrameProfile );

public:
#if USING( ME_EDITOR )
    static constexpr int kMinProfilerSize = 6;
#else
    static constexpr int kMinProfilerSize = 5;
#endif
    static constexpr int kMaxProfilerSize = 40;

    FrameProfile();

    void Start();
    void End( float frameDelta );
    void Dump();

    ImVec4 GetCategoryColor( ProfileCategory category );

    std::map<std::string, ProfileInfo> Profiles;
    std::map<std::string, ProfileInfo> ProfileDump;

    float MainDelta = 0.f;

    void Set( const std::string& name, ProfileCategory category );
    void Complete( const std::string& name );

    void Render( const Vector2& inPosition, const Vector2& inSize );
private:
    int CurrentProfilerSize = kMinProfilerSize;
    int PreviousTooltipHeight = 0;
};

class FrameProfileObject
{
public:
    FrameProfileObject( const std::string& name, ProfileCategory cat )
        : m_cat( cat )
        , m_name( name )
    {
        FrameProfile::GetInstance().Set( name, cat );
    }

    ~FrameProfileObject()
    {
        FrameProfile::GetInstance().Complete( m_name );
    }
    ProfileCategory m_cat;
    std::string m_name;
};

#endif

#if USING( ME_BASIC_PROFILER )
#define ME_FRAMEPROFILE_SCOPED(name, cat) FrameProfileObject(name, cat);
#else
#define ME_FRAMEPROFILE_SCOPED(name, cat)
#endif