#include <doctest/doctest.h>
#include "Components/Audio/AudioListener.h"
#include "Components/Audio/AudioSource.h"
#include "Components/Transform.h"
#include "Cores/AudioCore.h"
#include "Engine/World.h"
#include "Events/AudioEvents.h"
#include "Path.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>

#if USING( ME_FMOD )

namespace AudioTest
{
    // A mono 16-bit sine tone, written once per run.
    Path ToneClip()
    {
        static const std::string kPath = ".tmp/Tests/AudioTone.wav";
        static bool written = false;
        if( !written )
        {
            std::filesystem::create_directories( ".tmp/Tests" );
            const uint32_t rate = 48000;
            const uint32_t samples = rate * 2;
            const uint32_t dataBytes = samples * 2;
            std::ofstream file( kPath, std::ios::binary );
            auto u32 = [&file]( uint32_t InValue ) { file.write( reinterpret_cast<const char*>( &InValue ), 4 ); };
            auto u16 = [&file]( uint16_t InValue ) { file.write( reinterpret_cast<const char*>( &InValue ), 2 ); };
            file.write( "RIFF", 4 );
            u32( 36 + dataBytes );
            file.write( "WAVEfmt ", 8 );
            u32( 16 );
            u16( 1 );           // PCM
            u16( 1 );           // mono
            u32( rate );
            u32( rate * 2 );
            u16( 2 );
            u16( 16 );
            file.write( "data", 4 );
            u32( dataBytes );
            for( uint32_t i = 0; i < samples; ++i )
            {
                u16( static_cast<uint16_t>( static_cast<int16_t>( std::sin( i * 2.0 * 3.14159265 * 440.0 / rate ) * 16000.0 ) ) );
            }
            written = true;
        }
        return Path( kPath );
    }

    // Mixes a few blocks so voices start and their audibility is computed.
    void Mix( AudioCore& InCore, int InFrames = 3, float InDelta = 1.f / 60.f, bool InPaused = false )
    {
        for( int i = 0; i < InFrames; ++i )
        {
            InCore.Tick( InDelta, InPaused );
        }
    }

    struct Scene
    {
        AudioCore Core{ AudioOutput::Manual };
        std::shared_ptr<World> GameWorld;
        EntityHandle Listener;

        Scene()
        {
            GameWorld = std::make_shared<World>();
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Core );
            Listener = GameWorld->CreateEntity( "Listener" );
            Listener->AddComponent<Transform>();
            Listener->AddComponent<AudioListener>();
        }

        EntityHandle AddSource( const Vector3& InPosition )
        {
            EntityHandle entity = GameWorld->CreateEntity( "Source" );
            entity->AddComponent<Transform>().SetPosition( InPosition );
            AudioSource& source = entity->AddComponent<AudioSource>( ToneClip().GetLocalPathString() );
            source.Loop = true;
            return entity;
        }

        void Start()
        {
            GameWorld->Simulate();
            GameWorld->Start();
        }
    };
}

using namespace AudioTest;

TEST_CASE( "Audio: one-shots overlap up to their voice limit, and buses scale them" )
{
    AudioCore core( AudioOutput::Manual );
    REQUIRE( core.IsAvailable() );
    CHECK( core.IsSilent() );

    AudioVoice first = core.PlayOneShot( ToneClip(), AudioPlayParams(), 2 );
    AudioVoice second = core.PlayOneShot( ToneClip(), AudioPlayParams(), 2 );
    Mix( core );
    CHECK( first.IsValid() );
    CHECK( second.IsValid() );
    CHECK( first.GetChannel() != second.GetChannel() );
    CHECK( first.GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );

    // A third voice of the same clip steals the oldest.
    AudioVoice third = core.PlayOneShot( ToneClip(), AudioPlayParams(), 2 );
    Mix( core );
    CHECK_FALSE( first.IsValid() );
    CHECK( second.IsValid() );
    CHECK( third.IsValid() );

    core.SetBusVolume( AudioBus::SFX, 0.5f );
    Mix( core );
    CHECK( third.GetAudibility() == doctest::Approx( 0.5f ).epsilon( 0.01 ) );
    core.SetBusVolume( AudioBus::Master, 0.5f );
    Mix( core );
    CHECK( third.GetAudibility() == doctest::Approx( 0.25f ).epsilon( 0.01 ) );   // buses nest under Master
    core.SetBusMuted( AudioBus::SFX, true );
    Mix( core );
    CHECK( third.GetAudibility() == doctest::Approx( 0.f ) );

    // Other buses are unaffected.
    AudioPlayParams music;
    music.Bus = AudioBus::Music;
    AudioVoice song = core.PlayOneShot( ToneClip(), music );
    Mix( core );
    CHECK( song.GetAudibility() == doctest::Approx( 0.5f ).epsilon( 0.01 ) );

    core.StopAll();
    Mix( core, 1 );
    CHECK_FALSE( second.IsValid() );
    CHECK_FALSE( song.IsValid() );
}

TEST_CASE( "Audio: 3D sources fade with distance from the listener; 2D sources don't" )
{
    Scene scene;
    EntityHandle near = scene.AddSource( Vector3( 0.5f, 0.f, 0.f ) );
    EntityHandle mid = scene.AddSource( Vector3( 10.f, 0.f, 0.f ) );
    EntityHandle far = scene.AddSource( Vector3( 0.f, 0.f, 30.f ) );
    EntityHandle flat = scene.AddSource( Vector3( 0.f, 0.f, 30.f ) );
    for( EntityHandle entity : { near, mid, far } )
    {
        AudioSource& source = entity->GetComponent<AudioSource>();
        source.SpatialBlend = 1.f;
        source.Rolloff = AudioRolloff::Linear;
        source.MinDistance = 1.f;
        source.MaxDistance = 20.f;
    }
    for( EntityHandle entity : { near, mid, far, flat } )
    {
        entity->GetComponent<AudioSource>().PlayOnAwake = true;
    }
    scene.Start();
    Mix( scene.Core );

    CHECK( near->GetComponent<AudioSource>().IsPlaying() );
    CHECK( near->GetComponent<AudioSource>().GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );
    // Linear: 1 - (10 - 1) / (20 - 1)
    CHECK( mid->GetComponent<AudioSource>().GetAudibility() == doctest::Approx( 1.f - 9.f / 19.f ).epsilon( 0.02 ) );
    CHECK( far->GetComponent<AudioSource>().GetAudibility() == doctest::Approx( 0.f ) );
    CHECK( flat->GetComponent<AudioSource>().GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );

    // Moving the listener (or the source) is picked up on the next update.
    scene.Listener->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.f, 25.f ) );
    Mix( scene.Core );
    CHECK( far->GetComponent<AudioSource>().GetAudibility() > 0.6f );
    CHECK( near->GetComponent<AudioSource>().GetAudibility() == doctest::Approx( 0.f ) );

    // Volume and mute follow the component.
    AudioSource& flatSource = flat->GetComponent<AudioSource>();
    flatSource.Volume = 0.25f;
    Mix( scene.Core );
    CHECK( flatSource.GetAudibility() == doctest::Approx( 0.25f ).epsilon( 0.01 ) );
    flatSource.Mute = true;
    Mix( scene.Core );
    CHECK( flatSource.GetAudibility() == doctest::Approx( 0.f ) );
}

TEST_CASE( "Audio: game buses pause with the engine; stopping the world stops the game's sounds" )
{
    Scene scene;
    EntityHandle music = scene.AddSource( Vector3() );
    AudioSource& source = music->GetComponent<AudioSource>();
    source.PlayOnAwake = true;
    source.Bus = AudioBus::Music;
    scene.Start();
    Mix( scene.Core, 10 );
    REQUIRE( source.IsPlaying() );

    const unsigned int before = source.GetPositionMs();
    Mix( scene.Core, 10, 1.f / 60.f, true );
    CHECK( scene.Core.IsPaused() );
    const unsigned int paused = source.GetPositionMs();
    Mix( scene.Core, 10, 1.f / 60.f, true );
    CHECK( source.GetPositionMs() == paused );
    CHECK( paused >= before );

    // UI sounds keep playing while paused.
    AudioPlayParams ui;
    ui.Bus = AudioBus::UI;
    AudioVoice click = scene.Core.PlayOneShot( ToneClip(), ui );
    Mix( scene.Core, 5, 1.f / 60.f, true );
    CHECK( click.GetPositionMs() > 0 );

    Mix( scene.Core, 10 );
    CHECK_FALSE( scene.Core.IsPaused() );
    CHECK( source.GetPositionMs() > paused );

    scene.GameWorld->Stop();
    Mix( scene.Core, 1 );
    CHECK_FALSE( source.IsPlaying() );
    CHECK_FALSE( click.IsValid() );
}

TEST_CASE( "Audio: sources spawned during play start on their own; one-shots follow the source" )
{
    Scene scene;
    scene.Start();
    Mix( scene.Core );

    EntityHandle spawned = scene.AddSource( Vector3( 0.f, 0.f, 0.5f ) );
    AudioSource& source = spawned->GetComponent<AudioSource>();
    source.PlayOnAwake = true;
    source.Loop = false;
    source.SpatialBlend = 1.f;
    source.Rolloff = AudioRolloff::Linear;
    source.MaxDistance = 10.f;
    scene.GameWorld->Simulate();
    Mix( scene.Core );
    CHECK( source.IsPlaying() );

    source.MaxOneShots = 3;
    for( int i = 0; i < 5; ++i )
    {
        source.PlayOneShot( 0.5f );
    }
    Mix( scene.Core );
    AudioVoice shot = source.PlayOneShot( 0.5f );
    Mix( scene.Core );
    CHECK( shot.GetAudibility() == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    spawned->GetComponent<Transform>().SetPosition( Vector3( 0.f, 0.f, 20.f ) );
    Mix( scene.Core );
    CHECK( shot.GetAudibility() == doctest::Approx( 0.f ) );   // carried out of range with the source

    // PlayAudioEvent without a callback is a one-shot too.
    PlayAudioEvent event( ToneClip().GetLocalPathString() );
    event.Volume = 0.75f;
    event.Fire();
    event.Fire();
    Mix( scene.Core );

    // Destroying the entity stops its voices.
    spawned->MarkForDelete();
    scene.GameWorld->Simulate();
    Mix( scene.Core );
    CHECK_FALSE( shot.IsValid() );
}


TEST_CASE( "Audio: streamed sources open their own streams; their one-shots use the loaded clip" )
{
    Scene scene;
    EntityHandle first = scene.AddSource( Vector3( 0.f, 0.f, 0.f ) );
    EntityHandle second = scene.AddSource( Vector3( 0.f, 0.f, 0.f ) );
    for( EntityHandle entity : { first, second } )
    {
        AudioSource& source = entity->GetComponent<AudioSource>();
        source.Stream = true;
        source.PlayOnAwake = true;
    }
    scene.Start();
    Mix( scene.Core );
    AudioSource& a = first->GetComponent<AudioSource>();
    AudioSource& b = second->GetComponent<AudioSource>();
    CHECK( a.IsStreaming() );
    CHECK( b.IsStreaming() );
    CHECK( a.IsPlaying() );
    CHECK( b.IsPlaying() );   // the same file, two independent streams
    CHECK( a.GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );
    CHECK( b.GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );

    // A one-shot doesn't steal the stream from the source's own voice.
    AudioVoice shot = a.PlayOneShot( 0.5f );
    Mix( scene.Core );
    CHECK( shot.GetAudibility() == doctest::Approx( 0.5f ).epsilon( 0.02 ) );
    CHECK( a.IsPlaying() );
    CHECK( a.GetAudibility() == doctest::Approx( 1.f ).epsilon( 0.01 ) );
}

#endif
