#include "PCH.h"
#include "Engine/KillcamPlayer.h"
#include "CLog.h"
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/Model.h"
#include "Components/Graphics/TextRenderer.h"
#include "Components/Networking/SingletonKillcam.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "ECS/Core.h"
#include "Engine/World.h"
#include "Net/NetKillcam.h"
#include "Net/NetReplayPlayback.h"
#include "Profiling/FrameStats.h"
#include "optick.h"
#if USING( ME_SCRIPTING )
#include "Scripting/Bindings/BindingContext.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <utility>

namespace
{
    constexpr float kPi = 3.14159265358979f;

    // From InFrom towards InTo the short way round, in degrees.
    float LerpAngle( float InFrom, float InTo, float InAlpha )
    {
        float delta = std::fmod( InTo - InFrom, 360.f );
        if( delta > 180.f )
        {
            delta -= 360.f;
        }
        else if( delta < -180.f )
        {
            delta += 360.f;
        }
        return InFrom + delta * InAlpha;
    }

    const NetReplayClient* FindClient( const NetReplayTick* InTick, uint8_t InClient )
    {
        if( !InTick || InClient == 0 )
        {
            return nullptr;
        }
        for( const NetReplayClient& client : InTick->Clients )
        {
            if( client.Id == InClient )
            {
                return &client;
            }
        }
        return nullptr;
    }

    std::string Seconds( double InSeconds )
    {
        std::ostringstream out;
        out.precision( 2 );
        out << std::fixed << InSeconds;
        return out.str();
    }
}


Vector3 KillcamPlayer::AimDirection( float InYawDegrees, float InPitchDegrees )
{
    const float yaw = InYawDegrees * kPi / 180.f;
    const float pitch = InPitchDegrees * kPi / 180.f;
    return Vector3( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
}


KillcamPlayer::ViewPoint KillcamPlayer::Project( const Vector3& InEye, float InYawDegrees, float InPitchDegrees, float InFieldOfView, float InAspect, const Vector3& InPoint )
{
    // The view's axes: forward along the aim, right level with it (the aim a quarter turn on), up from both.
    const Vector3 forward = AimDirection( InYawDegrees, InPitchDegrees );
    const Vector3 right = AimDirection( InYawDegrees + 90.f, 0.f );
    const Vector3 up = forward.Cross( right );
    const Vector3 to = InPoint - InEye;
    ViewPoint point;
    point.Depth = to.Dot( forward );
    const float halfHeight = std::tan( InFieldOfView * 0.5f * kPi / 180.f );
    const float halfWidth = halfHeight * ( InAspect > 0.f ? InAspect : 1.f );
    if( point.Depth > 0.f && halfHeight > 0.f )
    {
        point.X = to.Dot( right ) / ( point.Depth * halfWidth );
        point.Y = to.Dot( up ) / ( point.Depth * halfHeight );
        point.Inside = std::abs( point.X ) <= 1.f && std::abs( point.Y ) <= 1.f;
    }
    return point;
}


KillcamPlayer::KillerView KillcamPlayer::FindKillerView( const std::vector<KillerSample>& InSamples, double InTime )
{
    KillerView view;
    view.Tick = InTime;
    if( InSamples.empty() )
    {
        return view;
    }
    const auto key = []( const KillerSample& InSample ) { return InSample.ViewTick >= 0.0 ? InSample.ViewTick : static_cast<double>( InSample.Tick ); };
    size_t index = 0;
    while( index < InSamples.size() && key( InSamples[index] ) < InTime )
    {
        ++index;
    }
    const KillerSample* from = nullptr;
    const KillerSample* to = nullptr;
    float alpha = 0.f;
    if( index == InSamples.size() )
    {
        from = to = &InSamples.back();
    }
    else if( index == 0 )
    {
        from = to = &InSamples.front();
    }
    else
    {
        from = &InSamples[index - 1];
        to = &InSamples[index];
        const double span = key( *to ) - key( *from );
        alpha = span > 0.0 ? static_cast<float>( std::clamp( ( InTime - key( *from ) ) / span, 0.0, 1.0 ) ) : 1.f;
    }
    view.Tick = from->Tick + ( static_cast<double>( to->Tick ) - from->Tick ) * alpha;
    view.ViewTick = key( *from ) + ( key( *to ) - key( *from ) ) * alpha;
    // Each part from both ticks when both have it, else from the one that does.
    if( from->HasAim && to->HasAim )
    {
        view.HasAim = true;
        view.Yaw = LerpAngle( from->Yaw, to->Yaw, alpha );
        view.Pitch = from->Pitch + ( to->Pitch - from->Pitch ) * alpha;
    }
    else if( from->HasAim || to->HasAim )
    {
        const KillerSample& one = from->HasAim ? *from : *to;
        view.HasAim = true;
        view.Yaw = one.Yaw;
        view.Pitch = one.Pitch;
    }
    if( from->HasPosition && to->HasPosition )
    {
        view.HasPosition = true;
        view.Position = from->Position + ( to->Position - from->Position ) * alpha;
    }
    else if( from->HasPosition || to->HasPosition )
    {
        view.HasPosition = true;
        view.Position = from->HasPosition ? from->Position : to->Position;
    }
    return view;
}


KillcamPlayer::KillerSample KillcamPlayer::MakeKillerSample( const NetReplayTick& InTick, uint8_t InClient, uint32_t InKillerNetId )
{
    KillerSample sample;
    sample.Tick = InTick.Tick;
    if( const NetReplayClient* client = FindClient( &InTick, InClient ) )
    {
        sample.HasAim = client->Command.HasAim;
        sample.Yaw = client->Command.Yaw;
        sample.Pitch = client->Command.Pitch;
        sample.ViewTick = client->Command.ViewTick;
    }
    auto state = InTick.State.Entities.find( InKillerNetId );
    if( state != InTick.State.Entities.end() )
    {
        sample.HasPosition = true;
        sample.Position = state->second.GetPosition();
    }
    return sample;
}


void KillcamPlayer::ReadKillerSamples( NetReplayReader& InReader, uint8_t InClient, uint32_t InKillerNetId, uint32_t InFirst, uint32_t InLast, uint16_t InTickRate, double InTime,
    std::vector<KillerSample>& InOutSamples )
{
    const uint32_t whole = static_cast<uint32_t>( std::max( 0.0, std::floor( InTime ) ) );
    const uint32_t from = whole > InFirst ? whole - 1 : InFirst;
    // Moved back (a seek): read from there again.
    if( !InOutSamples.empty() && InOutSamples.front().Tick > from )
    {
        InOutSamples.clear();
    }
    InOutSamples.erase( InOutSamples.begin(), std::find_if( InOutSamples.begin(), InOutSamples.end(), [from]( const KillerSample& InSample ) { return InSample.Tick >= from; } ) );
    if( InReader.IsEmpty() )
    {
        return;
    }
    // As far as the first tick whose view tick passes InTime (a few ticks of its client's interpolation
    // delay), no further than InLast, what's in, or two seconds.
    const uint32_t last = std::min( { InLast, InReader.GetLastTick(), whole + 2 * static_cast<uint32_t>( InTickRate ) } );
    uint32_t next = InOutSamples.empty() ? from : InOutSamples.back().Tick + 1;
    NetReplayTick tick;
    while( next <= last )
    {
        if( !InOutSamples.empty() && InOutSamples.back().Tick > whole )
        {
            const KillerSample& newest = InOutSamples.back();
            if( ( newest.ViewTick >= 0.0 ? newest.ViewTick : static_cast<double>( newest.Tick ) ) > InTime + 1.0 )
            {
                break;
            }
        }
        if( InReader.ReadTick( next, tick ) )
        {
            InOutSamples.push_back( MakeKillerSample( tick, InClient, InKillerNetId ) );
        }
        ++next;
    }
}


void KillcamPlayer::ReadSamples( double InTime )
{
    if( m_ahead )
    {
        ReadKillerSamples( *m_ahead, m_status.KillerClient, m_status.KillerNetId, m_status.FirstTick, m_status.LastTick, m_status.TickRate, InTime, m_samples );
    }
}


double KillcamPlayer::EdgeViewTick()
{
    if( !m_ahead || m_status.KillerClient == 0 || m_ahead->IsEmpty() )
    {
        return -1.0;
    }
    const uint32_t last = m_ahead->GetLastTick();
    if( last >= m_status.LastTick )
    {
        return -1.0;
    }
    if( last != m_edgeTick )
    {
        // Once per chunk that lands: the last tick in, read for its command's view tick.
        m_edgeTick = last;
        NetReplayTick tick;
        const KillerSample sample = m_ahead->ReadTick( last, tick ) ? MakeKillerSample( tick, m_status.KillerClient, m_status.KillerNetId ) : KillerSample();
        m_edgeView = sample.ViewTick >= 0.0 ? sample.ViewTick : static_cast<double>( last );
    }
    return m_edgeView;
}


void KillcamPlayer::FindKillShot()
{
    m_status.HasShot = false;
    NetReplayTick tick;
    if( !m_ahead || !m_ahead->ReadTick( m_status.DeathTick, tick ) )
    {
        return;
    }
    const Vector3 aim = AimDirection( m_status.Yaw, m_status.Pitch );
    float best = -2.f;
    for( const NetReplayShot& shot : tick.Shots )
    {
        // Its own trigger pull (a projectile's sweep starts somewhere else).
        if( shot.Shooter != m_status.KillerNetId || shot.Instigator != 0 || shot.Direction.LengthSquared() <= 0.f )
        {
            continue;
        }
        const float along = shot.Direction.Normalized().Dot( aim );
        if( along > best )
        {
            best = along;
            m_status.HasShot = true;
            m_status.ShotOrigin = shot.Origin;
            m_status.ShotDirection = shot.Direction.Normalized();
        }
    }
}


std::string KillcamPlayer::DescribeView() const
{
    if( !m_status.HasView )
    {
        return std::string();
    }
    std::ostringstream line;
    line.precision( 2 );
    line << std::fixed << "the camera at killer #" << m_status.KillerNetId << "'s eye at its tick " << m_status.KillerTick << " (" << m_status.Eye.x << ", " << m_status.Eye.y << ", " << m_status.Eye.z << ") aiming yaw "
         << m_status.Yaw << " pitch " << m_status.Pitch << ", the killer at (" << m_status.KillerAt.x << ", " << m_status.KillerAt.y << ", " << m_status.KillerAt.z << "), the victim #"
         << m_status.VictimNetId;
    if( !m_status.HasVictim )
    {
        line << " not in the world";
        return line.str();
    }
    const ViewPoint& view = m_status.VictimView;
    line << " at (" << m_status.VictimAt.x << ", " << m_status.VictimAt.y << ", " << m_status.VictimAt.z << ") ";
    if( view.Depth <= 0.f )
    {
        line << "behind the camera";
    }
    else
    {
        line << ( view.Inside ? "on screen" : "off screen" ) << " at (" << view.X << ", " << view.Y << ") " << view.Depth << " m ahead";
    }
    return line.str();
}


KillcamPlayer::ReplayWorld::~ReplayWorld()
{
    // The world before its cores (they're the world's until it goes).
    Instance.reset();
    Cores.clear();
}


KillcamPlayer::~KillcamPlayer()
{
    Release();
    if( SharedPtr<World> live = m_live.lock() )
    {
        if( SingletonKillcam* singleton = live->FindSingleton<SingletonKillcam>() )
        {
            singleton->Player = nullptr;
        }
    }
}


World* KillcamPlayer::GetWorld() const
{
    return m_playing ? m_replay->Instance.get() : nullptr;
}


SharedPtr<World> KillcamPlayer::GetWorldPtr() const
{
    return m_playing ? m_replay->Instance : SharedPtr<World>();
}


void KillcamPlayer::Bind( World& InLive )
{
    SharedPtr<World> previous = m_live.lock();
    if( previous.get() == &InLive )
    {
        return;
    }
    if( previous )
    {
        if( SingletonKillcam* singleton = previous->FindSingleton<SingletonKillcam>() )
        {
            singleton->Player = nullptr;
        }
    }
    m_live = InLive.GetSharedPtr();
    SingletonKillcam& singleton = InLive.Singleton<SingletonKillcam>();
    singleton.Player = this;
    singleton.InReplayWorld = false;
}


void KillcamPlayer::Update( World& InLive, double InFrameSeconds )
{
    OPTICK_EVENT( "KillcamPlayer::Update" );
    Bind( InLive );
    NetCore* live = InLive.GetCore<NetCore>();
    const NetKillcamReceiver* killcam = live && live->GetClient() ? &live->GetKillcam() : nullptr;
    if( std::exchange( m_skipRequested, false ) )
    {
        if( m_playing )
        {
            End( "skipped" );
        }
        // None playing: the newest received, not yet played (still arriving, or AutoPlay off), is let go (ME-343).
        else if( killcam && killcam->GetId() != 0 && killcam->GetId() != m_lastSeen && live->IsSessionLive() )
        {
            m_lastSeen = killcam->GetId();
            ++m_stats.SkippedUnplayed;
            CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_lastSeen ) + " skipped before it played (" + std::to_string( killcam->GetChunksReady() ) + " of "
                + std::to_string( killcam->GetChunkCount() ) + " chunks in)" );
            CancelDownload( &InLive, m_lastSeen );
        }
    }
    // No session, no killcams: the kept world goes too (the next session may be another scene, other scripts).
    if( m_replay && ( !live || !live->IsSessionLive() ) )
    {
        Release();
    }
    // Killcam ids are a server's own (each server process counts from 1, and the receiver starts over at 0
    // on Connect): with no session, or a receiver that has started over, whatever id comes next is new.
    // A session never comes back live without a Connect, so the killcam looked at can't play again.
    if( !killcam || killcam->GetId() == 0 || !live->IsSessionLive() )
    {
        m_lastSeen = 0;
    }
    // A newer killcam, readable: it plays now (replacing one playing).
    if( killcam && killcam->GetId() != 0 && killcam->GetId() != m_lastSeen && killcam->HasHeader() && killcam->GetChunksReady() > 0 && m_settings.AutoPlay
        && live->IsSessionLive() )
    {
        m_lastSeen = killcam->GetId();
        if( m_playing )
        {
            End( "replaced" );
        }
        Start( InLive, *killcam );
    }
    if( !m_playing )
    {
        return;
    }

    NetReplayPlayback& playback = *m_replay->Net->GetPlayback();
    // Still streaming while the receiver has more of this one coming.
    const bool same = killcam && killcam->GetId() == m_status.Id;
    m_status.Complete = !same || killcam->IsComplete();
    playback.SetStreaming( !m_status.Complete );
    // A player's killcam still streaming waits where the killer's last tick in was looking (ME-343): its view
    // of a tick past that hasn't arrived.
    playback.SetHoldAt( m_status.Complete ? -1.0 : EdgeViewTick() );
    const uint64_t stalledBefore = playback.GetStalledFrames();
    {
#if USING( ME_SCRIPTING )
        // Spawns run their scripts' creation in the replay world.
        ScriptBindings::WorldScope scope( m_replay->Instance );
#endif
        // The playhead moves, the stream's ticks go through the replication path, the world is posed.
        m_replay->Net->BeginTick();
        m_replay->Net->WriteInterpolated( InFrameSeconds );
        m_replay->Instance->Simulate();
    }
    m_status.Tick = playback.GetTime();
    m_status.Elapsed += InFrameSeconds;
    const uint64_t stalls = playback.GetStalledFrames() - stalledBefore;
    m_stats.StalledFrames += stalls;
    m_stalled = stalls > 0 ? m_stalled + InFrameSeconds : 0.0;
    PlaceView();
    // The killer's view of the kill (its tick reaches the death: the command that killed): where the victim was
    // in it (MatchSmoke checks it's on screen).
    // And the killer's own shot then, against that eye and aim (MatchSmoke checks the eye is where the game fires from).
    if( !m_deathShown && m_status.HasView && m_status.KillerTick >= m_status.DeathTick )
    {
        m_deathShown = true;
        ++m_stats.DeathViews;
        FindKillShot();
        std::ostringstream shot;
        shot.precision( 2 );
        shot << std::fixed;
        if( m_status.HasShot )
        {
            const Vector3 off = m_status.Eye - m_status.ShotOrigin;
            const float along = std::clamp( m_status.ShotDirection.Dot( AimDirection( m_status.Yaw, m_status.Pitch ) ), -1.f, 1.f );
            shot << "; its shot from (" << m_status.ShotOrigin.x << ", " << m_status.ShotOrigin.y << ", " << m_status.ShotOrigin.z << "): the eye " << off.y << " m above it, "
                 << Vector3( off.x, 0.f, off.z ).Length() << " m beside it, the aim " << std::acos( along ) * 180.f / kPi << " degrees off it";
        }
        else
        {
            shot << "; no shot of its own at the death tick";
        }
        CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_status.Id ) + " death view at tick " + Seconds( m_status.Tick ) + ": " + DescribeView() + shot.str() );
    }

    if( !playback.GetError().empty() )
    {
        End( "failed: " + playback.GetError() );
        return;
    }
    if( m_stalled > m_settings.MaxStallSeconds )
    {
        End( "failed: the stream stalled at tick " + std::to_string( playback.GetTick() ) );
        return;
    }
    // Played: the last tick has shown for its hold.
    if( m_status.Complete && playback.IsPaused() && playback.GetTick() >= playback.GetLastTick() )
    {
        m_held += InFrameSeconds;
        if( m_held >= HoldSeconds() )
        {
            End( "played" );
        }
    }
    else
    {
        m_held = 0.0;
    }
}


void KillcamPlayer::Start( World& InLive, const NetKillcamReceiver& InKillcam )
{
    OPTICK_EVENT( "KillcamPlayer::Start" );
    m_status = Status();
    m_status.Id = InKillcam.GetId();
    m_status.KillerNetId = InKillcam.GetKillerNetId();
    m_status.VictimNetId = InKillcam.GetVictimNetId();
    m_status.KillerClient = InKillcam.GetKillerClient();
    m_status.FirstTick = InKillcam.GetFirstTick();
    m_status.DeathTick = InKillcam.GetDeathTick();
    m_status.LastTick = std::max( InKillcam.GetDeathTick(), InKillcam.GetLastTick() );
    m_status.Complete = InKillcam.IsComplete();
    m_held = 0.0;
    m_stalled = 0.0;
    m_deathShown = false;
    // One that can't play stops coming too (ME-343): it's never tried again (m_lastSeen has it).
    auto fail = [&]( const std::string& InWhy ) {
        ++m_stats.Failed;
        CLog::Log( CLog::LogType::Warning, "Net: killcam " + std::to_string( m_status.Id ) + " can't play: " + InWhy );
        CancelDownload( &InLive, m_status.Id );
    };

    const auto began = std::chrono::steady_clock::now();
    // The kept world is another live world's (the player moved on): not this one's scene.
    if( m_replay && m_builtFor.lock().get() != &InLive )
    {
        DestroyWorld();
    }
    // The live session's command layout: the recording's version check folds it in with the scripts'.
    NetCore* live = InLive.GetCore<NetCore>();
    std::string error;
    bool kept = m_replay != nullptr;
    if( !kept && !Build( InLive, error ) )
    {
        fail( error.empty() ? "the replay world couldn't be made" : error );
        return;
    }
    if( live )
    {
        m_replay->Net->SetCommandSchema( live->GetCommandSchema() );
    }
    bool started = StartPlayback( InKillcam, error );
    if( !started && kept && !m_freshRefused )
    {
        // The kept world doesn't fit this stream (its placed networked entities aren't the recording's): a fresh one.
        // Once a fresh one hasn't fitted either, the streams are what don't fit, and no world is built for them.
        CLog::Log( CLog::LogType::Warning, "Net: killcam " + std::to_string( m_status.Id ) + ": the kept replay world doesn't fit it (" + error + "); building another" );
        ++m_stats.WorldsDiscarded;
        DestroyWorld();
        kept = false;
        error.clear();
        if( !Build( InLive, error ) )
        {
            fail( error.empty() ? "the replay world couldn't be made" : error );
            return;
        }
        if( live )
        {
            m_replay->Net->SetCommandSchema( live->GetCommandSchema() );
        }
        started = StartPlayback( InKillcam, error );
    }
    if( !started )
    {
        // A refused stream leaves the world as it was (StartPlayback changes nothing then): it's kept for the next.
        m_freshRefused = m_freshRefused || !kept;
        fail( error );
        if( !m_settings.KeepWorld )
        {
            DestroyWorld();
        }
        return;
    }
    if( kept )
    {
        ++m_stats.WorldsReused;
    }
    m_status.WorldKept = kept;
    m_status.ReadyMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - began ).count();
    // The killer's view reads its ticks ahead of the playhead from the same stream.
    m_samples.clear();
    m_edgeTick = 0;
    m_edgeView = -1.0;
    m_aheadSource = std::make_unique<NetKillcamSource>( InKillcam.GetSource() );
    m_ahead = std::make_unique<NetReplayReader>( *m_aheadSource );
    NetReplayPlayback& playback = *m_replay->Net->GetPlayback();
    playback.SetStreaming( !m_status.Complete );
    playback.Play();
    m_status.TickRate = playback.GetTickRate();
    m_status.Tick = playback.GetTime();
    m_status.Duration = static_cast<double>( m_status.LastTick - std::min( m_status.FirstTick, m_status.LastTick ) ) / m_status.TickRate + HoldSeconds();
    m_playing = true;
    m_live = InLive.GetSharedPtr();
    ++m_stats.Started;
    CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_status.Id ) + " playing: killer #" + std::to_string( m_status.KillerNetId ) + ", victim #"
        + std::to_string( m_status.VictimNetId ) + ", ticks " + std::to_string( m_status.FirstTick ) + "-" + std::to_string( m_status.DeathTick ) + " ("
        + ( m_status.LastTick > m_status.DeathTick ? "and " + std::to_string( m_status.LastTick - m_status.DeathTick ) + " after the death, " : std::string() )
        + Seconds( m_status.Duration ) + " s, " + std::to_string( InKillcam.GetChunksReady() ) + " of " + std::to_string( InKillcam.GetChunkCount() ) + " chunks in; "
        + ( kept ? "the replay world kept" : "the replay world built" ) + ", ready in " + Seconds( m_status.ReadyMs ) + " ms)" );
    Fire( true, std::string() );
    PlaceView();
}


bool KillcamPlayer::StartPlayback( const NetKillcamReceiver& InKillcam, std::string& OutError )
{
#if USING( ME_SCRIPTING )
    // Spawns at the first tick create their scripts in the replay world.
    ScriptBindings::WorldScope scope( m_replay->Instance );
#endif
    return m_replay->Net->StartPlayback( std::make_unique<NetKillcamSource>( InKillcam.GetSource() ), &OutError );
}


bool KillcamPlayer::Prepare( World& InLive )
{
    Bind( InLive );
    if( m_replay && !m_playing && m_builtFor.lock().get() != &InLive )
    {
        DestroyWorld();
        m_prepared = false;
    }
    if( m_replay )
    {
        return true;
    }
    // Once a session: a world gone since is the next killcam's to build, never a frame of live play's.
    if( m_prepared || !m_settings.KeepWorld )
    {
        return false;
    }
    m_prepared = true;
    std::string error;
    if( !Build( InLive, error ) )
    {
        CLog::Log( CLog::LogType::Warning, "Net: the killcam replay world can't be built ahead: " + ( error.empty() ? std::string( "the replay world couldn't be made" ) : error ) );
        return false;
    }
    return true;
}


bool KillcamPlayer::Build( World& InLive, std::string& OutError )
{
    OPTICK_EVENT( "KillcamPlayer::Build" );
    const auto began = std::chrono::steady_clock::now();
    auto replay = std::make_unique<ReplayWorld>();
    auto net = std::make_unique<NetCore>();
    replay->Net = net.get();
    replay->Cores.push_back( std::move( net ) );
    replay->Instance = MakeShared<World>();
    World& world = *replay->Instance;
    world.IsReplayWorld = true;
    world.IsLoading = false;
    world.AddCore( *replay->Net );
    if( NetCore* live = InLive.GetCore<NetCore>() )
    {
        replay->Net->SetCommandSchema( live->GetCommandSchema() );
    }
    SingletonKillcam& singleton = world.Singleton<SingletonKillcam>();
    singleton.Player = this;
    singleton.InReplayWorld = true;
    {
#if USING( ME_SCRIPTING )
        ScriptBindings::WorldScope scope( replay->Instance );
#endif
        if( m_populate && !m_populate( *replay, OutError ) )
        {
            // What it made goes inside its own scope (scripts' OnDestroy).
            world.Destroy();
            return false;
        }
        world.IsLoading = false;
        world.Simulate();
        world.Start();
    }
    // What it was built with: each killcam's spawns (and its scripts' makings) go after it (ResetWorld).
    m_built.clear();
    world.ForEachEntity( [this]( Entity& InEntity ) { m_built.push_back( InEntity.GetId().Value() ); } );
    std::sort( m_built.begin(), m_built.end() );
    m_hidden.clear();
    m_replay = std::move( replay );
    m_builtFor = InLive.GetSharedPtr();
    ++m_stats.WorldsBuilt;
    CLog::Log( CLog::LogType::Info, "Net: killcam replay world built: " + std::to_string( m_built.size() ) + " entities in "
        + Seconds( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - began ).count() ) + " ms" );
    return true;
}


bool KillcamPlayer::ResetWorld()
{
    OPTICK_EVENT( "KillcamPlayer::ResetWorld" );
    World& world = *m_replay->Instance;
    size_t whole = 0;
    {
#if USING( ME_SCRIPTING )
        // Scripts' OnDestroy run against their own world.
        ScriptBindings::WorldScope scope( m_replay->Instance );
#endif
        m_replay->Net->Shutdown();
        // What the killcam spawned (and its scripts made) goes; what the world was built with stays.
        std::vector<EntityHandle> extra;
        world.ForEachEntity( [&]( Entity& InEntity ) {
            if( std::binary_search( m_built.begin(), m_built.end(), InEntity.GetId().Value() ) )
            {
                ++whole;
            }
            else
            {
                extra.push_back( world.GetEntity( InEntity.GetId() ) );
            }
        } );
        for( const EntityHandle& entity : extra )
        {
            if( entity )
            {
                world.DestroyEntity( entity );
            }
        }
        // A placed killer's body comes back (a spawned one's went with it).
        for( const auto& [id, parts] : m_hidden )
        {
            Entity* entity = world.GetEntityRaw( EntityID::FromValue( id ) );
            if( !entity )
            {
                continue;
            }
            if( Mesh* mesh = ( parts & 1 ) ? entity->TryGetComponent<Mesh>() : nullptr )
            {
                mesh->SetEnabled( true );
            }
            if( Model* model = ( parts & 2 ) ? entity->TryGetComponent<Model>() : nullptr )
            {
                model->SetEnabled( true );
            }
            if( TextRenderer* text = ( parts & 4 ) ? entity->TryGetComponent<TextRenderer>() : nullptr )
            {
                text->SetEnabled( true );
            }
        }
        m_hidden.clear();
        world.Simulate();
    }
    return whole == m_built.size();
}


void KillcamPlayer::DestroyWorld()
{
    if( !m_replay )
    {
        return;
    }
    OPTICK_EVENT( "KillcamPlayer::DestroyWorld" );
    std::unique_ptr<ReplayWorld> replay = std::move( m_replay );
    {
#if USING( ME_SCRIPTING )
        // Its scripts' OnDestroy run against their own world.
        ScriptBindings::WorldScope scope( replay->Instance );
#endif
        replay->Net->Shutdown();
        replay->Instance->Stop();
        replay->Instance->Destroy();
    }
    replay.reset();
    m_built.clear();
    m_hidden.clear();
    m_builtFor.reset();
}


void KillcamPlayer::Stop()
{
    if( m_playing )
    {
        End( "stopped" );
    }
}


void KillcamPlayer::Release()
{
    if( m_playing )
    {
        End( "stopped", false );
    }
    DestroyWorld();
    m_prepared = false;
    m_freshRefused = false;
}


void KillcamPlayer::End( const std::string& InReason, bool InKeep )
{
    if( !m_playing )
    {
        return;
    }
    OPTICK_EVENT( "KillcamPlayer::End" );
    if( InReason == "played" ) ++m_stats.Played;
    else if( InReason == "skipped" ) ++m_stats.Skipped;
    else if( InReason == "replaced" ) ++m_stats.Replaced;
    else if( InReason == "stopped" ) ++m_stats.Stopped;
    else ++m_stats.Failed;
    m_skipRequested = false;
    m_playing = false;
    // One that won't be shown stops coming (ME-343): skipped, stopped or failed before its stream was whole. A
    // replaced one's receiver has the newer already (the server cancelled the older itself).
    if( InReason != "played" && InReason != "replaced" )
    {
        SharedPtr<World> live = m_live.lock();
        CancelDownload( live.get(), m_status.Id );
    }
    {
        ME_STAT_SCOPE( "Killcam Reset" );
        if( !InKeep || !m_settings.KeepWorld )
        {
            DestroyWorld();
        }
        else if( !ResetWorld() )
        {
            // A placed entity went in the killcam: the next one gets a world as built.
            ++m_stats.WorldsDiscarded;
            CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_status.Id ) + ": the replay world lost an entity it was built with; it's built again for the next" );
            DestroyWorld();
        }
    }
    m_ahead.reset();
    m_aheadSource.reset();
    m_samples.clear();
    m_status.HasView = false;
    CLog::Log( InReason.rfind( "failed", 0 ) == 0 ? CLog::LogType::Warning : CLog::LogType::Info,
        "Net: killcam " + std::to_string( m_status.Id ) + " ended (" + InReason + ") after " + Seconds( m_status.Elapsed ) + " s at tick " + std::to_string( static_cast<uint32_t>( m_status.Tick ) ) );
    Fire( false, InReason );
}


void KillcamPlayer::CancelDownload( World* InLive, uint32_t InId )
{
    NetCore* net = InLive ? InLive->GetCore<NetCore>() : nullptr;
    if( net && InId != 0 && net->GetKillcam().GetId() == InId )
    {
        net->CancelKillcamDownload();
    }
}


void KillcamPlayer::Fire( bool InStarted, const std::string& InReason )
{
    KillcamEvent event;
    event.Type = InStarted ? KillcamEvent::Kind::Started : KillcamEvent::Kind::Ended;
    event.Id = m_status.Id;
    event.KillerNetId = m_status.KillerNetId;
    event.VictimNetId = m_status.VictimNetId;
    event.Reason = InReason;
    SharedPtr<World> live = m_live.lock();
    event.Source = live.get();
    event.Fire();
}


void KillcamPlayer::PlaceView()
{
    m_status.HasView = false;
    m_status.HasVictim = false;
    if( !m_playing || !m_settings.KillerView )
    {
        return;
    }
    World& world = *m_replay->Instance;
    NetCore& net = *m_replay->Net;
    const NetReplayPlayback* playback = net.GetPlayback();
    EntityHandle killer = net.FindEntity( m_status.KillerNetId );
    Transform* body = killer ? killer->TryGetComponent<Transform>() : nullptr;
    if( !playback || !body )
    {
        return;
    }
    // A player's: its eye and aim at the tick its client was looking at the time shown (its command's view
    // tick: the world on its screen then), read ahead in the stream. A bot's: where it is and faces now.
    float yaw = 0.f;
    float pitch = 0.f;
    Vector3 origin = body->GetWorldPosition();
    m_status.KillerTick = playback->GetTime();
    m_status.KillerViewTick = playback->GetTime();
    bool aimed = false;
    if( m_status.KillerClient != 0 )
    {
        ReadSamples( playback->GetTime() );
        const KillerView view = FindKillerView( m_samples, playback->GetTime() );
        m_status.KillerTick = view.Tick;
        m_status.KillerViewTick = view.ViewTick;
        if( view.HasPosition )
        {
            origin = view.Position;
        }
        if( view.HasAim )
        {
            yaw = view.Yaw;
            pitch = view.Pitch;
            aimed = true;
        }
    }
    if( !aimed )
    {
        const Vector3 forward = body->GetWorldRotation() * Vector3( 0.f, 0.f, 1.f );
        yaw = std::atan2( forward.x, forward.z ) * 180.f / kPi;
    }
    // The eye: the settings', the game's (C#'s Net.Killcam.EyeHeight, on the live world), else the engine's guess.
    float eyeHeight = m_settings.EyeHeight;
    if( eyeHeight < 0.f )
    {
        SharedPtr<World> live = m_live.lock();
        const SingletonKillcam* game = live ? live->FindSingleton<SingletonKillcam>() : nullptr;
        eyeHeight = game ? game->EyeHeight : -1.f;
    }
    if( eyeHeight < 0.f )
    {
        const CharacterController* controller = killer->TryGetComponent<CharacterController>();
        eyeHeight = controller ? controller->Height * 0.5f - 0.15f : 1.6f;
    }
    const Vector3 eye = origin + Vector3( 0.f, eyeHeight, 0.f );
    m_status.KillerAt = origin;
    m_status.Eye = eye;
    m_status.Yaw = yaw;
    m_status.Pitch = pitch;

    if( m_settings.HideKiller )
    {
        // Its own body would fill the view: everything drawn under the killer goes.
        std::vector<Transform*> pending = { body };
        while( !pending.empty() )
        {
            Transform* transform = pending.back();
            pending.pop_back();
            if( Entity* entity = transform->Parent.Get() )
            {
                uint8_t parts = 0;
                if( Mesh* mesh = entity->TryGetComponent<Mesh>(); mesh && mesh->IsEnabled() )
                {
                    mesh->SetEnabled( false );
                    parts |= 1;
                }
                // A compact model draws its parts from the Model itself, not from Mesh entities.
                if( Model* model = entity->TryGetComponent<Model>(); model && model->IsEnabled() )
                {
                    model->SetEnabled( false );
                    parts |= 2;
                }
                if( TextRenderer* text = entity->TryGetComponent<TextRenderer>(); text && text->IsEnabled() )
                {
                    text->SetEnabled( false );
                    parts |= 4;
                }
                // Shown again when the world is reset for the next killcam (a placed killer stays).
                if( parts != 0 )
                {
                    m_hidden.emplace_back( entity->GetId().Value(), parts );
                }
            }
            for( Transform* child : transform->GetChildren() )
            {
                pending.push_back( child );
            }
        }
    }

    Camera* main = Camera::GetMain( &world );
    Transform* camera = main && main->Parent ? main->Parent->TryGetComponent<Transform>() : nullptr;
    if( !camera )
    {
        return;
    }
    camera->SetWorldPosition( eye );
    camera->LookAt( eye + AimDirection( yaw, pitch ) );
    // Only now is the view the killer's: a world without a camera of its own shows something else.
    m_status.HasView = true;

    // Where the victim is in it.
    EntityHandle victim = net.FindEntity( m_status.VictimNetId );
    Transform* victimBody = victim ? victim->TryGetComponent<Transform>() : nullptr;
    m_status.HasVictim = victimBody != nullptr;
    if( victimBody )
    {
        const Vector2 size = main->GetViewportSize();
        m_status.VictimAt = victimBody->GetWorldPosition();
        m_status.VictimView = Project( eye, yaw, pitch, main->GetFOV(), size.x > 0.f && size.y > 0.f ? size.x / size.y : 16.f / 9.f, m_status.VictimAt );
    }
}
