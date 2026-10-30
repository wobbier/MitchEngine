#include <doctest/doctest.h>
#include "Components/Animation/Sequence.h"
#include "Components/Camera.h"
#include "Components/Transform.h"
#include "Cores/SequenceCore.h"
#include "Cores/AnimationCore.h"
#include "Components/Animation/Animator.h"
#include "Scene/AnimationClip.h"
#include "Engine/World.h"
#include "Events/EventReceiver.h"
#include "World/SceneSerializer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace
{
    struct Rig
    {
        std::shared_ptr<World> GameWorld = std::make_shared<World>();
        SequenceCore Core;
        EntityHandle Object;
        Rig()
        {
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Core );
            Object = GameWorld->CreateEntity( "Camera" );
            Object->AddComponent<Transform>().SetPosition( Vector3( 2.f, 3.f, -8.f ) );
            Object->AddComponent<Camera>();
            Object->AddComponent<Sequence>();
            Clip().FrameRate = 30;
            Clip().DurationFrames = 60;
            AddTrack( "Transform", "Position", Vector4( 0.f, 0.f, -8.f, 0.f ), Vector4( 0.f, 0.f, -2.f, 0.f ) );
            GameWorld->Simulate();
        }
        Sequence& Clip() { return Object->GetComponent<Sequence>(); }
        SequenceTrack& AddTrack( const std::string& InComponent, const std::string& InProperty, Vector4 InFrom, Vector4 InTo )
        {
            SequenceTrack track;
            track.Target = Object;
            track.Component = InComponent;
            track.Property = InProperty;
            track.Keys = { { 0, InFrom, SequenceInterpolation::Linear }, { 60, InTo, SequenceInterpolation::Linear } };
            Clip().Tracks.push_back( track );
            return Clip().Tracks.back();
        }
        float Z() { return Object->GetComponent<Transform>().GetPosition().z; }
        ~Rig() { GameWorld->Stop(); GameWorld->Destroy(); }
    };
}


TEST_CASE( "Sequence: transform and reflected camera fields play together and hold the final sample" )
{
    Rig rig;
    rig.AddTrack( "Camera", "FieldOfView", Vector4( 60.f ), Vector4( 30.f ) );
    rig.GameWorld->Start();
    rig.Core.Advance( 1.f );
    CHECK( rig.Z() == doctest::Approx( -5.f ) );
    CHECK( rig.Object->GetComponent<Camera>().GetFOV() == doctest::Approx( 45.f ) );
    CHECK( rig.Object->GetComponent<Transform>().GetMatrix().GetPosition().z == doctest::Approx( -5.f ) );
    rig.Core.Advance( 10.f );
    CHECK_FALSE( rig.Clip().IsPlaying() );
    CHECK( rig.Clip().GetTime() == doctest::Approx( 2.f ) );
    CHECK( rig.Z() == doctest::Approx( -2.f ) );
    rig.Core.Advance( 1.f );
    CHECK( rig.Z() == doctest::Approx( -2.f ) );
}


TEST_CASE( "Sequence: play pause resume seek stop and manual start have deterministic clocks" )
{
    Rig rig;
    rig.Clip().PlayOnStart = false;
    rig.GameWorld->Start();
    rig.Core.Advance( 1.f );
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    CHECK( rig.Object->GetComponent<Transform>().GetPosition().x == doctest::Approx( 2.f ) );
    rig.Clip().Play();
    rig.Core.Advance( 0.5f );
    rig.Clip().Pause();
    rig.Core.Advance( 1.f );
    CHECK( rig.Clip().GetTime() == doctest::Approx( 0.5f ) );
    rig.Clip().Play( false );
    rig.Core.Advance( 0.5f );
    CHECK( rig.Z() == doctest::Approx( -5.f ) );
    rig.Clip().Seek( 1.5f );
    CHECK( rig.Z() == doctest::Approx( -3.5f ) );
    rig.Clip().Stop();
    CHECK_FALSE( rig.Clip().IsPlaying() );
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Clip().Seek( -100.f );
    CHECK( rig.Clip().GetTime() == 0.f );
    rig.Clip().Seek( 100.f );
    CHECK( rig.Clip().GetTime() == doctest::Approx( 2.f ) );
    rig.Clip().Seek( std::numeric_limits<float>::quiet_NaN() );
    CHECK( rig.Clip().GetTime() == doctest::Approx( 2.f ) );
}


TEST_CASE( "Sequence: loops preserve overshoot and frame rate does not change playback" )
{
    Rig a, b;
    a.Clip().Loop = b.Clip().Loop = true;
    a.Clip().Speed = b.Clip().Speed = 2.f;
    a.GameWorld->Start();
    b.GameWorld->Start();
    a.Core.Advance( 3.25f );
    for( int i = 0; i < 390; ++i ) b.Core.Advance( 1.f / 120.f );
    CHECK( a.Clip().IsPlaying() );
    CHECK( a.Clip().GetTime() == doctest::Approx( 0.5f ) );
    CHECK( b.Clip().GetTime() == doctest::Approx( a.Clip().GetTime() ).epsilon( 1e-5 ) );
    CHECK( a.Z() == doctest::Approx( b.Z() ).epsilon( 1e-5 ) );
    a.Core.Advance( 0.f );
    a.Core.Advance( -1.f );
    a.Core.Advance( std::numeric_limits<float>::infinity() );
    CHECK( a.Clip().GetTime() == doctest::Approx( 0.5f ) );
}


TEST_CASE( "Sequence: fractional durations finish on the exact end frame" )
{
    Rig rig;
    rig.Clip().FrameRate = 60;
    rig.Clip().DurationFrames = 241;
    auto& track = rig.Clip().Tracks[0];
    track.Keys[1].Frame = 241;
    track.Keys[0].Interpolation = SequenceInterpolation::Hold;
    rig.GameWorld->Start();
    rig.Core.Advance( 10.f );
    CHECK( rig.Clip().GetFrame() == 241.f );
    CHECK( rig.Z() == doctest::Approx( -2.f ) );
}


TEST_CASE( "Sequence: keys hold at boundaries, ease, step, replace and sample unsorted data" )
{
    Rig rig;
    auto& track = rig.Clip().Tracks[0];
    Vector4 value;
    REQUIRE( track.Sample( -10.f, value ) );
    CHECK( value.z == doctest::Approx( -8.f ) );
    REQUIRE( track.Sample( 100.f, value ) );
    CHECK( value.z == doctest::Approx( -2.f ) );
    track.Keys[0].Interpolation = SequenceInterpolation::SmoothStep;
    REQUIRE( track.Sample( 15.f, value ) );
    CHECK( value.z == doctest::Approx( -7.0625f ) );
    track.Keys[0].Interpolation = SequenceInterpolation::Hold;
    REQUIRE( track.Sample( 59.9f, value ) );
    CHECK( value.z == doctest::Approx( -8.f ) );
    REQUIRE( track.Sample( 60.f, value ) );
    CHECK( value.z == doctest::Approx( -2.f ) );
    track.SetKey( { 0, Vector4( 9.f ), SequenceInterpolation::Linear } );
    CHECK( track.Keys.size() == 2 );
    std::reverse( track.Keys.begin(), track.Keys.end() );
    REQUIRE( track.Sample( 0.f, value ) );
    CHECK( value.x == doctest::Approx( 9.f ) );
    track.Keys.clear();
    CHECK_FALSE( track.Sample( 0.f, value ) );
    REQUIRE( track.Capture( 15 ) );
    CHECK( track.Keys[0].Value.x == doctest::Approx( 2.f ) );
}


TEST_CASE( "Sequence: rotations interpolate normalized quaternions along the shortest arc" )
{
    Rig rig;
    const Quaternion from = Quaternion::FromEulerDegrees( Vector3( 0.f, 170.f, 0.f ) );
    const Quaternion to = Quaternion::FromEulerDegrees( Vector3( 0.f, -170.f, 0.f ) );
    auto& track = rig.AddTrack( "Transform", "Rotation", Vector4( from.x, from.y, from.z, from.w ), Vector4( to.x, to.y, to.z, to.w ) );
    REQUIRE( track.Apply( 30.f ) );
    const auto& q = rig.Object->GetComponent<Transform>().GetRotation();
    CHECK( glm::length( q.InternalQuat ) == doctest::Approx( 1.f ) );
    CHECK( ( q * Vector3::Front ).z == doctest::Approx( -1.f ) );
    track.Keys[0].Value = Vector4();
    CHECK_FALSE( track.Apply( 15.f ) );
}


TEST_CASE( "Sequence: nested fields and ranges use reflection and notify their component" )
{
    Rig rig;
    auto& fov = rig.AddTrack( "Camera", "FieldOfView", Vector4( -20.f ), Vector4( 300.f ) );
    REQUIRE( fov.Apply( 0.f ) );
    CHECK( rig.Object->GetComponent<Camera>().GetFOV() == doctest::Approx( 1.f ) );
    REQUIRE( fov.Apply( 60.f ) );
    CHECK( rig.Object->GetComponent<Camera>().GetFOV() == doctest::Approx( 179.f ) );
    auto& nested = rig.AddTrack( "Camera", "ProceduralSky.CloudCoverage", Vector4( 0.1f ), Vector4( 0.9f ) );
    REQUIRE( nested.Apply( 30.f ) );
    CHECK( rig.Object->GetComponent<Camera>().ProceduralSky.CloudCoverage == doctest::Approx( 0.5f ) );
    auto& near = rig.AddTrack( "Camera", "Near", Vector4( -2.f ), Vector4( -2.f ) );
    REQUIRE( near.Apply( 0.f ) );
    CHECK( rig.Object->GetComponent<Camera>().Near == doctest::Approx( 0.01f ) );
}


TEST_CASE( "Sequence: missing deleted disabled and unsupported bindings are safe" )
{
    Rig rig;
    SequenceTrack track = rig.Clip().Tracks[0];
    track.Property = "DoesNotExist";
    CHECK_FALSE( track.Apply( 30.f ) );
    track.Component = "Camera";
    track.Property = "Zoom"; // Hidden compatibility field
    CHECK_FALSE( track.Apply( 30.f ) );
    track.Property = "Projection"; // Enum, not continuously animatable
    CHECK_FALSE( track.Apply( 30.f ) );
    track = rig.Clip().Tracks[0];
    track.Enabled = false;
    CHECK_FALSE( track.Apply( 30.f ) );
    track.Enabled = true;
    EntityHandle other = rig.GameWorld->CreateEntity( "Other" );
    other->AddComponent<Transform>();
    rig.GameWorld->Simulate();
    track.Target = other;
    REQUIRE( track.Apply( 30.f ) );
    CHECK( other->GetComponent<Transform>().GetPosition().z == doctest::Approx( -5.f ) );
    other->SetActive( false );
    rig.GameWorld->Simulate();
    CHECK_FALSE( track.Apply( 30.f ) );
    other->MarkForDelete();
    rig.GameWorld->Simulate();
    CHECK_FALSE( track.Apply( 30.f ) );
    track.Target = rig.Object;
    rig.Object->RemoveComponent<Transform>();
    rig.GameWorld->Simulate();
    CHECK_FALSE( track.Apply( 30.f ) );
}


TEST_CASE( "Sequence: edit previews restore authored transform and camera values for save stop and play" )
{
    Rig rig;
    rig.AddTrack( "Camera", "FieldOfView", Vector4( 50.f ), Vector4( 30.f ) );
    auto& clip = rig.Clip();
    clip.StartPreview( 30.f );
    rig.Core.UpdatePreviews( 0.f );
    CHECK( rig.Z() == doctest::Approx( -5.f ) );
    CHECK( rig.Object->GetComponent<Camera>().GetFOV() == doctest::Approx( 40.f ) );
    rig.Core.RestorePreviewValues();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    CHECK( rig.Object->GetComponent<Transform>().GetPosition().x == doctest::Approx( 2.f ) );
    CHECK( rig.Object->GetComponent<Camera>().GetFOV() == doctest::Approx( 60.f ) );
    const json saved = SceneSerializer::SerializeWorld( *rig.GameWorld, nullptr );
    CHECK( saved["Entities"][0]["Components"][0]["Position"][0] == 2.f );
    rig.Core.UpdatePreviews( 0.f );
    CHECK( rig.Z() == doctest::Approx( -5.f ) );
    rig.Core.StopAllPreviews();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    clip.StartPreview( 30.f, true );
    rig.Core.UpdatePreviews( 0.25f );
    CHECK( clip.GetTime() == doctest::Approx( 1.25f ) );
    rig.GameWorld->Start();
    CHECK_FALSE( clip.IsPreviewing() );
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Core.Advance( 0.5f );
    CHECK( rig.Z() == doctest::Approx( -6.5f ) );
}


TEST_CASE( "Sequence: previews restore when disabled removed or no longer inspected" )
{
    Rig rig;
    rig.Clip().StartPreview( 30.f );
    rig.Clip().SetEnabled( false );
    rig.GameWorld->Simulate();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Clip().SetEnabled( true );
    rig.GameWorld->Simulate();
    rig.Clip().StartPreview( 30.f );
    for( int i = 0; i < 32; ++i ) rig.Core.UpdatePreviews( 0.f );
    CHECK_FALSE( rig.Clip().IsPreviewing() );
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Clip().StartPreview( 30.f );
    rig.Object->RemoveComponent<Sequence>();
    rig.GameWorld->Simulate();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
}


TEST_CASE( "Sequence: a paused preview accepts pose edits and captures them without losing the authored baseline" )
{
    Rig rig;
    rig.Clip().StartPreview( 30.f );
    rig.Object->GetComponent<Transform>().SetPosition( Vector3( 4.f, 5.f, -3.f ) );
    rig.Core.UpdatePreviews( 0.1f );
    CHECK( rig.Z() == doctest::Approx( -3.f ) );
    REQUIRE( rig.Clip().Tracks[0].Capture( 30 ) );
    rig.Clip().StopPreview();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Clip().StartPreview( 30.f );
    CHECK( rig.Z() == doctest::Approx( -3.f ) );
    CHECK( rig.Object->GetComponent<Transform>().GetPosition().x == doctest::Approx( 4.f ) );
    rig.Core.RestorePreviewValues();
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    rig.Core.UpdatePreviews( 0.f );
    CHECK( rig.Z() == doctest::Approx( -3.f ) );
}


TEST_CASE( "Sequence: scene round trips and duplication remap track targets" )
{
    Rig rig;
    json saved = SceneSerializer::SerializeWorld( *rig.GameWorld, nullptr );
    World loaded;
    loaded.IsLoading = false;
    SceneSerializer::LoadOptions options;
    options.RemapGUIDs = true;
    const auto roots = SceneSerializer::Deserialize( loaded, saved, options );
    loaded.Simulate();
    REQUIRE( roots.size() == 1 );
    Sequence& copy = roots[0]->GetComponent<Sequence>();
    REQUIRE( copy.Tracks.size() == 1 );
    CHECK( roots[0]->GetGUID() != rig.Object->GetGUID() );
    CHECK( copy.Tracks[0].Target == roots[0] );
    copy.Seek( 1.f );
    CHECK( roots[0]->GetComponent<Transform>().GetPosition().z == doctest::Approx( -5.f ) );
    CHECK( rig.Z() == doctest::Approx( -8.f ) );
    loaded.Destroy();
}


TEST_CASE( "Sequence: dolly zoom examples hold subject size through the entire shot and change the background" )
{
    for( const char* path : { "Assets/Scenes/Tests/SequenceTest.lvl", "Assets/Scenes/LostTemple.lvl" } )
    {
        DOCTEST_INFO( path );
        std::ifstream stream( path );
        if( !stream.good() )
        {
            MESSAGE( "skipped: the project has no " << path << " (MitchGame's example scenes)" );
            continue;
        }
        const json scene = json::parse( stream );
        const json* cameraEntity = nullptr;
        const json* sequenceData = nullptr;
        double depth = 12.;
        for( const auto& entity : scene["Entities"] )
        {
            if( !entity.value( "Active", true ) ) continue;
            for( const auto& component : entity["Components"] )
            {
                if( component["Type"] == "Sequence" ) { cameraEntity = &entity; sequenceData = &component; }
            }
        }
        REQUIRE( cameraEntity );
        REQUIRE( sequenceData );
        for( const auto& component : ( *cameraEntity )["Components"] )
            if( component["Type"] == "PostProcess" && component.contains( "DoFFocusDistance" ) ) depth = component["DoFFocusDistance"].get<double>();

        Rig rig;
        std::unordered_map<uint64_t, uint64_t> remap{ { SceneSerializer::GUIDFromJson( ( *cameraEntity )["GUID"] ), rig.Object->GetGUID() } };
        SerializationWorldScope scope( rig.GameWorld.get(), &remap );
        rig.Clip().Deserialize( *sequenceData );
        rig.Clip().Apply( 0.f );
        Transform& transform = rig.Object->GetComponent<Transform>();
        Camera& camera = rig.Object->GetComponent<Camera>();
        const Vector3 forward = transform.Front(), up = transform.Up();
        const Vector3 subject = transform.GetWorldPosition() + forward * static_cast<float>( depth );
        const Vector3 background = subject + forward * 20.f;
        auto projectedHeight = [&]( const Vector3& point ) {
            const Vector3 top = transform.InverseTransformPoint( point + up * 0.7f );
            const Vector3 bottom = transform.InverseTransformPoint( point - up * 0.7f );
            REQUIRE( top.z > 0.f );
            REQUIRE( bottom.z > 0.f );
            return std::abs( ( double( top.y ) / top.z - double( bottom.y ) / bottom.z ) / std::tan( glm::radians( double( camera.GetFOV() ) ) * 0.5 ) );
        };
        const double subjectHeight = projectedHeight( subject );
        const double backgroundHeight = projectedHeight( background );
        // Fractional frames catch the breathing that interpolating degrees instead of focal
        // length introduces even when both endpoint FOVs are correct.
        for( int i = 0; i <= rig.Clip().DurationFrames * 4; ++i )
        {
            rig.Clip().Apply( i * 0.25f );
            CHECK( projectedHeight( subject ) == doctest::Approx( subjectHeight ).epsilon( 0.00005 ) );
        }
        rig.Clip().Apply( 90.f );
        CHECK( std::abs( projectedHeight( background ) / backgroundHeight - 1. ) > 0.05 );
        rig.Clip().Apply( static_cast<float>( rig.Clip().DurationFrames ) );
        CHECK( projectedHeight( background ) == doctest::Approx( backgroundHeight ).epsilon( 0.00005 ) );

        for( const auto& entity : scene["Entities"] )
            if( entity["Name"] == "Dolly Zoom Subject" )
            {
                if( sequenceData->contains( "Clip" ) )
                    for( const auto& binding : ( *sequenceData )["Bindings"] ) CHECK( binding["Target"] != entity["GUID"] );
                else for( const auto& track : ( *sequenceData )["Tracks"] ) CHECK( track["Target"] != entity["GUID"] );
            }
    }
}


TEST_CASE( "Sequence: focal length interpolation pairs with linear motion and rejects invalid lens angles" )
{
    Rig rig;
    auto& position = rig.Clip().Tracks[0];
    position.Keys[0].Value = Vector4( 0.f, 0.f, -12.f, 0.f );
    position.Keys[1].Value = Vector4( 0.f, 0.f, -6.f, 0.f );
    const float endFov = glm::degrees( 2.f * std::atan( 2.f * std::tan( glm::radians( 52.f ) * 0.5f ) ) );
    auto& fov = rig.AddTrack( "Camera", "FieldOfView", Vector4( 52.f ), Vector4( endFov ) );
    fov.Keys[0].Interpolation = SequenceInterpolation::FocalLength;
    rig.Clip().Apply( 15.f );
    const double depth = -rig.Z();
    const double field = rig.Object->GetComponent<Camera>().GetFOV();
    CHECK( depth * std::tan( glm::radians( field ) * 0.5 ) == doctest::Approx( 12. * std::tan( glm::radians( 52. ) * 0.5 ) ).epsilon( 1e-6 ) );
    fov.Keys[0].Value.x = 0.f;
    CHECK_FALSE( fov.Apply( 15.f ) );
    fov.Keys[0].Value.x = 52.f;
    fov.Keys[1].Value.x = 180.f;
    CHECK_FALSE( fov.Apply( 15.f ) );
    fov.Keys[1].Value.x = endFov;
    fov.Property = "Near";
    CHECK_FALSE( fov.Apply( 15.f ) );
}


namespace
{
    // Markers by name, notifications by kind.
    struct SequenceLog
        : public EventReceiver
    {
        SequenceLog()
        {
            Listen<SequenceEvent>( [this]( const SequenceEvent& InEvent ) {
                static const char* kinds[] = { "Marker", "Started", "Finished", "Interrupted", "Looped" };
                Entries.push_back( InEvent.Kind == SequenceEventKind::Marker ? InEvent.Name : kinds[static_cast<int>( InEvent.Kind )] );
                Events.push_back( InEvent );
            } );
        }

        std::vector<std::string> Entries;
        std::vector<SequenceEvent> Events;
    };

    void AddMarker( Sequence& InSequence, int InFrame, const std::string& InName )
    {
        SequenceMarker marker;
        marker.Frame = InFrame;
        marker.Name = InName;
        InSequence.Markers.push_back( marker );
        InSequence.OnPropertyChanged( "Markers" );
    }
}


TEST_CASE( "Sequence markers: each crossing fires once, in frame then list order, at any frame rate" )
{
    using Log = std::vector<std::string>;
    const Log expected = { "Started", "Start", "Mid", "Mid 2", "End", "Finished" };
    for( float step : { 1.f / 120.f, 1.f / 7.f, 5.f } )
    {
        Rig rig;
        SequenceLog log;
        AddMarker( rig.Clip(), 30, "Mid" );
        AddMarker( rig.Clip(), 60, "End" );
        AddMarker( rig.Clip(), 0, "Start" );
        AddMarker( rig.Clip(), 30, "Mid 2" );
        rig.GameWorld->Start();
        for( float time = 0.f; time < 3.f; time += step ) rig.Core.Advance( step );
        CHECK( log.Entries == expected );
        REQUIRE( log.Events.size() == expected.size() );
        CHECK( log.Events[1].Receiver == rig.Object );
        CHECK( log.Events[1].Player == rig.Object );
        CHECK( log.Events[2].Frame == 30 );
    }
}


TEST_CASE( "Sequence markers: loops fire every lap with Looped between laps, stepped or in one hitch" )
{
    const std::vector<std::string> expected = { "Started", "A", "B", "Looped", "A", "B", "Looped", "A" };
    for( int steps : { 1, 270 } )
    {
        Rig rig;
        SequenceLog log;
        rig.Clip().Loop = true;
        AddMarker( rig.Clip(), 0, "A" );
        AddMarker( rig.Clip(), 60, "B" );
        rig.GameWorld->Start();
        for( int i = 0; i < steps; ++i ) rig.Core.Advance( 4.5f / steps );
        CHECK( log.Entries == expected );
    }
}


TEST_CASE( "Sequence markers: seek, pause and preview never fire; stop and restart interrupt" )
{
    {
        Rig rig;
        SequenceLog log;
        AddMarker( rig.Clip(), 30, "Mid" );
        AddMarker( rig.Clip(), 45, "Late" );
        rig.GameWorld->Start();
        rig.Core.Advance( 0.5f );
        rig.Clip().Seek( 1.f );                     // onto Mid: it is not crossed
        rig.Core.Advance( 0.1f );
        rig.Clip().Pause();
        rig.Core.Advance( 1.f );
        rig.Clip().Play( false );                   // a resume, not a start
        rig.Core.Advance( 0.5f );                   // crosses Late (1.5 s)
        rig.Clip().Play();                          // restart while playing
        rig.Core.Advance( 0.1f );
        rig.Clip().Stop();
        rig.Clip().Stop();                          // already stopped: nothing
        CHECK( log.Entries == std::vector<std::string>{ "Started", "Late", "Interrupted", "Started", "Interrupted" } );
    }
    {
        Rig rig;
        SequenceLog log;
        AddMarker( rig.Clip(), 30, "Mid" );
        rig.Clip().StartPreview( 0.f, true );
        rig.Core.UpdatePreviews( 3.f );
        rig.Clip().StopPreview();
        CHECK( log.Entries.empty() );
    }
}


TEST_CASE( "Sequence markers: receivers resolve targets and bindings; unbound slots have none" )
{
    Rig rig;
    SequenceLog log;
    EntityHandle door = rig.GameWorld->CreateEntity( "Door" );
    door->AddComponent<Transform>();
    SequenceMarker open;
    open.Frame = 10;
    open.Name = "Open";
    open.Target = door;
    open.Payload = "fast";
    open.Number = 2.5f;
    SequenceMarker slot;
    slot.Frame = 20;
    slot.Name = "Slot";
    slot.Binding = "Lamp";
    rig.Clip().Markers = { open, slot };
    rig.Clip().ResolveBindings();
    rig.GameWorld->Start();
    rig.Core.Advance( 1.f );
    REQUIRE( log.Events.size() == 3 );
    CHECK( log.Events[1].Receiver == door );
    CHECK( log.Events[1].Payload == "fast" );
    CHECK( log.Events[1].Number == doctest::Approx( 2.5f ) );
    CHECK_FALSE( log.Events[2].Receiver );
    rig.Clip().SetBinding( "Lamp", door );
    rig.Clip().Play();
    rig.Core.Advance( 1.f );
    CHECK( log.Events.back().Receiver == door );
}


TEST_CASE( "Sequence camera cuts: the active cut's camera is current while sampled, and previews restore it" )
{
    Rig rig;
    EntityHandle wide = rig.GameWorld->CreateEntity( "Wide" );
    wide->AddComponent<Transform>();
    wide->AddComponent<Camera>();
    EntityHandle close = rig.GameWorld->CreateEntity( "Close" );
    close->AddComponent<Transform>();
    close->AddComponent<Camera>();
    rig.Object->GetComponent<Camera>().SetCurrent();
    SequenceCut first;
    first.Frame = 15;
    first.Target = wide;
    SequenceCut second;
    second.Frame = 45;
    second.Target = close;
    rig.Clip().Cuts = { second, first };
    rig.Clip().OnPropertyChanged( "Cuts" );
    REQUIRE( rig.Clip().Cuts[0].Frame == 15 );
    CHECK_FALSE( rig.Clip().GetCutCamera( 10.f ) );
    CHECK( rig.Clip().GetCutCamera( 15.f ) == wide );
    CHECK( rig.Clip().GetCutCamera( 44.9f ) == wide );
    CHECK( rig.Clip().GetCutCamera( 60.f ) == close );

    // An edit preview switches cameras and gives the scene its own back.
    rig.Clip().StartPreview( 30.f );
    CHECK( wide->GetComponent<Camera>().IsCurrent() );
    rig.Clip().StartPreview( 50.f );
    CHECK( close->GetComponent<Camera>().IsCurrent() );
    rig.Clip().StopPreview();
    CHECK( rig.Object->GetComponent<Camera>().IsCurrent() );

    // Playback: before the first cut the scene's camera stays; the last cut holds at the end.
    rig.GameWorld->Start();
    rig.Core.Advance( 0.25f );
    CHECK( rig.Object->GetComponent<Camera>().IsCurrent() );
    rig.Core.Advance( 0.5f );
    CHECK( wide->GetComponent<Camera>().IsCurrent() );
    rig.Core.Advance( 5.f );
    CHECK( close->GetComponent<Camera>().IsCurrent() );
    rig.Object->GetComponent<Camera>().SetCurrent();
    rig.Core.Advance( 1.f );                       // finished: gameplay owns the camera again
    CHECK( rig.Object->GetComponent<Camera>().IsCurrent() );
}


TEST_CASE( "Sequence markers: a PlayAnimation cue plays the receiver's Animator state with no script" )
{
    Rig rig;
    AnimationCore animation;
    rig.GameWorld->AddCore( animation );
    EntityHandle actor = rig.GameWorld->CreateEntity( "Actor" );
    actor->AddComponent<Transform>();
    std::vector<Moonlight::AnimationClip> clips( 2 );
    clips[0].Name = "Idle";
    clips[1].Name = "Wave";
    for( auto& clip : clips ) clip.Duration = 1.f;
    actor->AddComponent<Animator>().UseClips( std::make_shared<std::vector<Moonlight::AnimationClip>>( std::move( clips ) ) );
    SequenceMarker wave;
    wave.Frame = 30;
    wave.Name = "Wave";
    wave.Target = actor;
    wave.Action = SequenceMarkerAction::PlayAnimation;
    wave.Payload = "Wave";
    rig.Clip().Markers = { wave };
    rig.GameWorld->Simulate();
    rig.GameWorld->Start();
    rig.Core.Advance( 0.5f );
    animation.Advance( 0.5f );
    CHECK( actor->GetComponent<Animator>().GetCurrentState() == "Idle" );
    rig.Core.Advance( 0.6f );
    animation.Advance( 0.1f );
    CHECK( actor->GetComponent<Animator>().GetCurrentState() == "Wave" );
}
