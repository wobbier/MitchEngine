#include "PCH.h"
#include "Engine/KillcamPlayer.h"
#include "CLog.h"
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/TextRenderer.h"
#include "Components/Networking/SingletonKillcam.h"
#include "Components/Physics/CharacterController.h"
#include "Components/Transform.h"
#include "Cores/NetCore.h"
#include "ECS/Core.h"
#include "Engine/World.h"
#include "Net/NetKillcam.h"
#include "Net/NetReplayPlayback.h"
#include "optick.h"
#if USING( ME_SCRIPTING )
#include "Scripting/Bindings/BindingContext.h"
#endif
#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace
{
    constexpr float kPi = 3.14159265358979f;

    Vector3 AimDirection( float InYawDegrees, float InPitchDegrees )
    {
        const float yaw = InYawDegrees * kPi / 180.f;
        const float pitch = InPitchDegrees * kPi / 180.f;
        return Vector3( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) );
    }

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


KillcamPlayer::ReplayWorld::~ReplayWorld()
{
    // The world before its cores (they're the world's until it goes).
    Instance.reset();
    Cores.clear();
}


KillcamPlayer::~KillcamPlayer()
{
    if( m_replay )
    {
        End( "stopped" );
    }
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
    return m_replay ? m_replay->Instance.get() : nullptr;
}


SharedPtr<World> KillcamPlayer::GetWorldPtr() const
{
    return m_replay ? m_replay->Instance : SharedPtr<World>();
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
    if( std::exchange( m_skipRequested, false ) && m_replay )
    {
        End( "skipped" );
    }
    if( m_replay && ( !live || !live->IsSessionLive() ) )
    {
        End( "stopped" );
    }
    // A newer killcam, readable: it plays now (replacing one playing).
    if( killcam && killcam->GetId() != 0 && killcam->GetId() != m_lastSeen && killcam->HasHeader() && killcam->GetChunksReady() > 0 && m_settings.AutoPlay
        && live->IsSessionLive() )
    {
        m_lastSeen = killcam->GetId();
        if( m_replay )
        {
            End( "replaced" );
        }
        Start( InLive, *killcam );
    }
    if( !m_replay )
    {
        return;
    }

    NetReplayPlayback& playback = *m_replay->Net->GetPlayback();
    // Still streaming while the receiver has more of this one coming.
    const bool same = killcam && killcam->GetId() == m_status.Id;
    m_status.Complete = !same || killcam->IsComplete();
    playback.SetStreaming( !m_status.Complete );
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
    // Played: the death tick has shown for HoldSeconds.
    if( m_status.Complete && playback.IsPaused() && playback.GetTick() >= playback.GetLastTick() )
    {
        m_held += InFrameSeconds;
        if( m_held >= m_settings.HoldSeconds )
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
    m_status.Complete = InKillcam.IsComplete();
    m_held = 0.0;
    m_stalled = 0.0;
    auto fail = [&]( const std::string& InWhy ) {
        ++m_stats.Failed;
        CLog::Log( CLog::LogType::Warning, "Net: killcam " + std::to_string( m_status.Id ) + " can't play: " + InWhy );
    };

    auto replay = std::make_unique<ReplayWorld>();
    auto net = std::make_unique<NetCore>();
    replay->Net = net.get();
    replay->Cores.push_back( std::move( net ) );
    replay->Instance = MakeShared<World>();
    World& world = *replay->Instance;
    world.IsReplayWorld = true;
    world.IsLoading = false;
    world.AddCore( *replay->Net );
    // The live session's command layout: the recording's version check folds it in with the scripts'.
    if( NetCore* live = InLive.GetCore<NetCore>() )
    {
        replay->Net->SetCommandSchema( live->GetCommandSchema() );
    }
    SingletonKillcam& singleton = world.Singleton<SingletonKillcam>();
    singleton.Player = this;
    singleton.InReplayWorld = true;
    std::string error;
    {
#if USING( ME_SCRIPTING )
        ScriptBindings::WorldScope scope( replay->Instance );
#endif
        if( m_populate && !m_populate( *replay, error ) )
        {
            fail( error.empty() ? "the replay world couldn't be made" : error );
            // What it made goes inside its own scope (scripts' OnDestroy).
            world.Destroy();
            return;
        }
        world.IsLoading = false;
        world.Simulate();
        world.Start();
        if( !replay->Net->StartPlayback( std::make_unique<NetKillcamSource>( InKillcam.GetSource() ), &error ) )
        {
            fail( error );
            world.Destroy();
            return;
        }
    }
    NetReplayPlayback& playback = *replay->Net->GetPlayback();
    playback.SetStreaming( !m_status.Complete );
    playback.Play();
    m_status.TickRate = playback.GetTickRate();
    m_status.Tick = playback.GetTime();
    m_status.Duration = static_cast<double>( m_status.DeathTick - std::min( m_status.FirstTick, m_status.DeathTick ) ) / m_status.TickRate + m_settings.HoldSeconds;
    m_replay = std::move( replay );
    m_live = InLive.GetSharedPtr();
    ++m_stats.Started;
    CLog::Log( CLog::LogType::Info, "Net: killcam " + std::to_string( m_status.Id ) + " playing: killer #" + std::to_string( m_status.KillerNetId ) + ", victim #"
        + std::to_string( m_status.VictimNetId ) + ", ticks " + std::to_string( m_status.FirstTick ) + "-" + std::to_string( m_status.DeathTick ) + " ("
        + Seconds( m_status.Duration ) + " s, " + std::to_string( InKillcam.GetChunksReady() ) + " of " + std::to_string( InKillcam.GetChunkCount() ) + " chunks in)" );
    Fire( true, std::string() );
    PlaceView();
}


void KillcamPlayer::Stop()
{
    if( m_replay )
    {
        End( "stopped" );
    }
}


void KillcamPlayer::End( const std::string& InReason )
{
    if( !m_replay )
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
    m_status.HasView = false;
    CLog::Log( InReason.rfind( "failed", 0 ) == 0 ? CLog::LogType::Warning : CLog::LogType::Info,
        "Net: killcam " + std::to_string( m_status.Id ) + " ended (" + InReason + ") after " + Seconds( m_status.Elapsed ) + " s at tick " + std::to_string( static_cast<uint32_t>( m_status.Tick ) ) );
    Fire( false, InReason );
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
    if( !m_replay || !m_settings.KillerView )
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
    // The killer's client's recorded aim, between the tick shown and the next; a bot's facing.
    float yaw = 0.f;
    float pitch = 0.f;
    const NetReplayClient* now = FindClient( playback->GetRecord(), m_status.KillerClient );
    if( now && now->Command.HasAim )
    {
        yaw = now->Command.Yaw;
        pitch = now->Command.Pitch;
        const NetReplayClient* next = FindClient( playback->GetNextRecord(), m_status.KillerClient );
        if( next && next->Command.HasAim )
        {
            const float alpha = static_cast<float>( playback->GetTime() - std::floor( playback->GetTime() ) );
            yaw = LerpAngle( yaw, next->Command.Yaw, alpha );
            pitch = pitch + ( next->Command.Pitch - pitch ) * alpha;
        }
    }
    else
    {
        const Vector3 forward = body->GetWorldRotation() * Vector3( 0.f, 0.f, 1.f );
        yaw = std::atan2( forward.x, forward.z ) * 180.f / kPi;
    }
    const CharacterController* controller = killer->TryGetComponent<CharacterController>();
    const float eyeHeight = m_settings.EyeHeight >= 0.f ? m_settings.EyeHeight : controller ? controller->Height * 0.5f - 0.15f : 1.6f;
    const Vector3 eye = body->GetWorldPosition() + Vector3( 0.f, eyeHeight, 0.f );
    m_status.HasView = true;
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
                if( Mesh* mesh = entity->TryGetComponent<Mesh>(); mesh && mesh->IsEnabled() )
                {
                    mesh->SetEnabled( false );
                }
                if( TextRenderer* text = entity->TryGetComponent<TextRenderer>(); text && text->IsEnabled() )
                {
                    text->SetEnabled( false );
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
}
