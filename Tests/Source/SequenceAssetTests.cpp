#include <doctest/doctest.h>
#include "Animation/SequenceAsset.h"
#include "Animation/SequenceEditing.h"
#include "Components/Transform.h"
#include "Cores/SequenceCore.h"
#include "Resource/ResourceCache.h"
#include "World/SceneSerializer.h"
#include <filesystem>
#include <fstream>
#if USING( ME_EDITOR )
#include "../../Modules/Havana/Source/Editor/SequenceDocument.h"
#endif

namespace
{
    struct ClipRig
    {
        std::shared_ptr<World> GameWorld = std::make_shared<World>();
        EntityHandle Owner;
        ClipRig()
        {
            GameWorld->IsLoading = false;
            Owner = GameWorld->CreateEntity( "Camera" );
            Owner->AddComponent<Transform>();
            auto& player = Owner->AddComponent<Sequence>();
            SequenceTrack track;
            track.Target = Owner;
            track.Keys = { { 0, Vector4( 0.f ), SequenceInterpolation::Linear }, { 60, Vector4( 6.f ), SequenceInterpolation::Linear } };
            player.Tracks.push_back( track );
            player.DurationFrames = 60;
            GameWorld->Simulate();
        }
        Sequence& Player() { return Owner->GetComponent<Sequence>(); }
        ~ClipRig() { GameWorld->Destroy(); }
    };

    std::vector<SequenceTrack> EditTracks()
    {
        std::vector<SequenceTrack> tracks( 3 );
        for( auto& lane : tracks ) lane.Keys = { { 0, Vector4( 1.f ), SequenceInterpolation::Linear }, { 10, Vector4( 2.f ), SequenceInterpolation::SmoothStep }, { 20, Vector4( 3.f ), SequenceInterpolation::Hold } };
        return tracks;
    }
}


TEST_CASE( "Sequence asset: export separates shared keys from instance GUIDs and playback" )
{
    ClipRig rig;
    std::string error;
    REQUIRE( rig.Player().ExportClip( ".tmp/Tests/Sequence/Export.sequence", error ) );
    SequenceClipData clip;
    REQUIRE( clip.Load( rig.Player().Clip, error ) );
    REQUIRE( clip.Tracks.size() == 1 );
    CHECK( clip.Tracks[0].Binding == "Camera" );
    CHECK_FALSE( clip.ToJson()["Tracks"][0].contains( "Target" ) );
    json player;
    rig.Player().Serialize( player );
    CHECK( player["Clip"] == ".tmp/Tests/Sequence/Export.sequence" );
    CHECK_FALSE( player.contains( "Tracks" ) );
    CHECK_FALSE( player.contains( "FrameRate" ) );
    CHECK( player["Bindings"][0]["Target"] == SceneSerializer::GUIDToString( rig.Owner->GetGUID() ) );
    CHECK( ResourceCache::GetInstance().LoadMetadata( Path( rig.Player().Clip ) )->GUID != 0 );
}


TEST_CASE( "Sequence asset: the same clip binds to different objects and duplication remaps only bindings" )
{
    ClipRig rig;
    std::string error;
    REQUIRE( rig.Player().ExportClip( ".tmp/Tests/Sequence/Rebind.sequence", error ) );
    auto other = rig.GameWorld->CreateEntity( "Other" );
    other->AddComponent<Transform>();
    auto& second = other->AddComponent<Sequence>();
    second.Clip = rig.Player().Clip;
    second.Bindings = { { "Camera", other } };
    REQUIRE( second.RefreshClip( true ) );
    rig.GameWorld->Simulate();
    second.Seek( 1.f );
    CHECK( other->GetComponent<Transform>().GetPosition().x == doctest::Approx( 3.f ) );
    CHECK( rig.Owner->GetComponent<Transform>().GetPosition().x == 0.f );
    json saved = SceneSerializer::SerializeWorld( *rig.GameWorld, nullptr );
    World copied;
    copied.IsLoading = false;
    SceneSerializer::LoadOptions options;
    options.RemapGUIDs = true;
    SceneSerializer::Deserialize( copied, saved, options );
    copied.Simulate();
    auto newOwner = copied.FindEntityByName( "Camera" );
    REQUIRE( newOwner );
    auto& copy = newOwner->GetComponent<Sequence>();
    CHECK( copy.Clip == rig.Player().Clip );
    REQUIRE( copy.Bindings.size() == 1 );
    CHECK( copy.Bindings[0].Target == newOwner );
    CHECK( copy.Tracks[0].Target == newOwner );
    copy.Seek( 2.f );
    CHECK( newOwner->GetComponent<Transform>().GetPosition().x == doctest::Approx( 6.f ) );
    copied.Destroy();
}


TEST_CASE( "Sequence asset: reload updates existing players and malformed reload keeps last good data" )
{
    ClipRig rig;
    std::string error;
    const std::string path = ".tmp/Tests/Sequence/Reload.sequence";
    REQUIRE( rig.Player().ExportClip( path, error ) );
    auto resource = ResourceCache::GetInstance().Get<SequenceResource>( Path( path ) );
    auto data = resource->Data;
    data.Tracks[0].Keys[1].Value = Vector4( 12.f );
    REQUIRE( data.Save( path, error ) );
    const uint32_t before = resource->Version;
    ResourceCache::GetInstance().OnFilesChanged( { Path( path ).FullPath } );
    CHECK( resource->Version > before );
    REQUIRE( rig.Player().RefreshClip() );
    rig.Player().Seek( 1.f );
    CHECK( rig.Owner->GetComponent<Transform>().GetPosition().x == doctest::Approx( 6.f ) );
    std::ofstream( Path( path ).FullPath ) << "{bad json";
    const uint32_t good = resource->Version;
    resource->Reload();
    CHECK( resource->Version == good );
    CHECK_FALSE( resource->Error.empty() );
    REQUIRE( rig.Player().RefreshClip() );
    CHECK( rig.Player().Tracks[0].Keys[1].Value.x == 12.f );
}


TEST_CASE( "Sequence asset: schema rejects scene targets duplicate frames unknown versions and invalid values" )
{
    ClipRig rig;
    rig.Player().PrepareAssetBindings();
    const json valid = SequenceClipData::Capture( rig.Player() ).ToJson();
    std::string error;
    SequenceClipData data;
    REQUIRE( data.FromJson( valid, error ) );
    for( int bad = 0; bad < 5; ++bad )
    {
        json changed = valid;
        if( bad == 0 ) changed["Version"] = 999;
        if( bad == 1 ) changed["Tracks"][0]["Target"] = "some-scene-guid";
        if( bad == 2 ) changed["Tracks"][0]["Keys"][1]["Frame"] = 0;
        if( bad == 3 ) changed["Tracks"][0]["Keys"][1]["Value"][0] = "bad";
        if( bad == 4 ) changed["Tracks"][0]["Binding"] = "";
        CHECK_FALSE( data.FromJson( changed, error ) );
        CHECK( data.ToJson() == valid );
    }
}

TEST_CASE( "Sequence clips: named assets switch timing and reuse bindings without changing the authored default" )
{
    ClipRig rig;
    auto& player = rig.Player();
    std::string error;
    REQUIRE( player.ExportClip( ".tmp/Tests/Sequence/NamedDefault.sequence", error ) );
    const std::string authored = player.Clip;
    SequenceClipData alternate = SequenceClipData::Capture( player );
    alternate.Name = "Pull";
    alternate.FrameRate = 60;
    alternate.DurationFrames = 120;
    alternate.Tracks[0].Keys = { { 0, Vector4( 10.f ), SequenceInterpolation::Linear }, { 120, Vector4( 20.f ), SequenceInterpolation::Linear } };
    const std::string path = ".tmp/Tests/Sequence/NamedPull.sequence";
    REQUIRE( alternate.Save( path, error ) );
    player.Clips = { { "Push", authored }, { "Pull", path } };
    REQUIRE( player.PlayClip( "Push" ) );
    player.Seek( 1.f );
    CHECK( rig.Owner->GetComponent<Transform>().GetPosition().x == doctest::Approx( 3.f ) );
    REQUIRE( player.PlayClip( "Pull", false ) );
    CHECK( player.GetTime() == 0.f );
    CHECK( player.FrameRate == 60 );
    CHECK( player.DurationFrames == 120 );
    CHECK( player.Tracks[0].Target == rig.Owner );
    CHECK( rig.Owner->GetComponent<Transform>().GetPosition().x == 10.f );
    player.Seek( 1.f );
    player.Pause();
    REQUIRE( player.PlayClip( "Pull", false ) );
    CHECK( player.GetTime() == 1.f );
    CHECK( rig.Owner->GetComponent<Transform>().GetPosition().x == 15.f );
    REQUIRE( player.PlayAsset( authored ) );
    CHECK( player.GetTime() == 0.f );
    CHECK( player.Name == "Sequence" );
    REQUIRE( player.PlayClip( "Pull" ) );
    json saved;
    player.Serialize( saved );
    CHECK( saved["Clip"] == authored );
    CHECK( saved["Clips"].size() == 2 );
    CHECK_FALSE( saved.contains( "Tracks" ) );
    auto copy = rig.GameWorld->CreateEntity( "Restored" );
    auto& restored = copy->AddComponent<Sequence>();
    restored.Deserialize( saved );
    CHECK( restored.Clip == authored );
    CHECK( restored.GetActiveClipPath() == authored );
    CHECK( restored.HasClip( "Pull" ) );
    CHECK( restored.Name == "Sequence" );
}


TEST_CASE( "Sequence clips: invalid names and assets leave active playback and pose intact" )
{
    ClipRig rig;
    auto& player = rig.Player();
    std::string error;
    REQUIRE( player.ExportClip( ".tmp/Tests/Sequence/NamedErrors.sequence", error ) );
    player.Clips = { { "Good", player.Clip }, { "Duplicate", player.Clip }, { "Duplicate", player.Clip }, { "Empty", "" } };
    REQUIRE( player.PlayClip( "Good" ) );
    player.Seek( 1.f );
    const auto pose = rig.Owner->GetComponent<Transform>().GetPosition();
    CHECK_FALSE( player.HasClip( "" ) );
    CHECK_FALSE( player.HasClip( "Duplicate" ) );
    CHECK_FALSE( player.HasClip( "Empty" ) );
    CHECK_FALSE( player.PlayClip( "Missing" ) );
    CHECK_FALSE( player.PlayClip( "Duplicate" ) );
    CHECK_FALSE( player.PlayAsset( "" ) );
    CHECK_FALSE( player.PlayAsset( ".tmp/Tests/Sequence/DoesNotExist.sequence" ) );
    CHECK( player.IsPlaying() );
    CHECK( player.GetTime() == 1.f );
    const auto& current = rig.Owner->GetComponent<Transform>().GetPosition();
    CHECK( current.x == pose.x );
    CHECK( current.y == pose.y );
    CHECK( current.z == pose.z );
    REQUIRE( player.PlayClip( "Good" ) );
    CHECK( player.GetTime() == 0.f );
}


TEST_CASE( "Sequence editing: marquee selects across rows reversed rectangles and additive selections" )
{
    auto tracks = EditTracks();
    SequenceEditing::Selection selection;
    SequenceEditing::BoxSelect( tracks, 15.f, 5.f, 1, 0, selection );
    CHECK( selection.size() == 2 );
    CHECK( SequenceEditing::Contains( selection, { 0, 10 } ) );
    CHECK( SequenceEditing::Contains( selection, { 1, 10 } ) );
    SequenceEditing::BoxSelect( tracks, 0.f, 20.f, 2, 2, selection, true );
    CHECK( selection.size() == 5 );
    SequenceEditing::BoxSelect( tracks, 0.f, 20.f, 2, 2, selection, true );
    CHECK( selection.size() == 5 );
}


TEST_CASE( "Sequence editing: bulk moves preserve spacing values and interpolation across tracks" )
{
    auto original = EditTracks();
    SequenceEditing::Selection selected{ { 0, 10 }, { 0, 20 }, { 2, 10 } }, movedSelection;
    std::vector<SequenceTrack> moved;
    REQUIRE( SequenceEditing::Move( original, selected, 7, 60, moved, movedSelection ) );
    CHECK( moved[0].Keys[1].Frame == 17 );
    CHECK( moved[0].Keys[2].Frame == 27 );
    CHECK( moved[0].Keys[1].Value.x == 2.f );
    CHECK( moved[0].Keys[1].Interpolation == SequenceInterpolation::SmoothStep );
    CHECK( moved[1].Keys[1].Frame == 10 );
    CHECK( original[0].Keys[1].Frame == 10 );
    CHECK( SequenceEditing::Contains( movedSelection, { 2, 17 } ) );
}


TEST_CASE( "Sequence editing: collisions reject atomically and boundary clamping preserves group offsets" )
{
    const auto original = EditTracks();
    auto moved = original;
    SequenceEditing::Selection selection{ { 0, 10 }, { 1, 20 } }, after;
    CHECK_FALSE( SequenceEditing::Move( original, selection, 10, 60, moved, after ) );
    CHECK( moved[0].Keys[1].Frame == 10 );
    CHECK( moved[1].Keys[2].Frame == 20 );
    int delta = 0;
    REQUIRE( SequenceEditing::Move( original, selection, 100, 60, moved, after, &delta ) );
    CHECK( delta == 40 );
    CHECK( moved[0].Keys[2].Frame == 50 );
    CHECK( moved[1].Keys[2].Frame == 60 );
    SequenceEditing::Selection contiguous{ { 0, 0 }, { 0, 10 } };
    REQUIRE( SequenceEditing::Move( original, contiguous, -100, 60, moved, after, &delta ) );
    CHECK( delta == 0 );
    CHECK_FALSE( SequenceEditing::Move( original, { { 20, 10 } }, 1, 60, moved, after ) );
}


TEST_CASE( "Sequence editing: deleting the selection leaves other tracks and keys intact" )
{
    auto tracks = EditTracks();
    REQUIRE( SequenceEditing::Delete( tracks, { { 0, 10 }, { 2, 20 } } ) );
    CHECK( tracks[0].Keys.size() == 2 );
    CHECK( tracks[1].Keys.size() == 3 );
    CHECK( tracks[2].Keys.size() == 2 );
    CHECK( tracks[0].Keys.back().Frame == 20 );
    CHECK_FALSE( SequenceEditing::Delete( tracks, { { 30, 10 } } ) );
}


#if USING( ME_EDITOR )
TEST_CASE( "Sequence document: grouped edits undo in one step and asset save state is independent of scene history" )
{
    ClipRig rig;
    std::string error;
    const std::string path = ".tmp/Tests/Sequence/Document.sequence";
    REQUIRE( rig.Player().ExportClip( path, error ) );
    SequenceDocument document;
    REQUIRE( document.Open( path ) );
    const bool sceneDirty = UndoStack::Get().IsDirty();
    const json before = document.Data.ToJson();
    document.Data.Tracks[0].Keys[1].Frame = 50;
    document.Data.DurationFrames = 80;
    document.Commit( before, "Move Selected Keys" );
    CHECK( document.History.GetCursor() == 1 );
    CHECK( document.IsDirty() );
    CHECK( UndoStack::Get().IsDirty() == sceneDirty );
    document.History.Undo();
    CHECK( document.Data.ToJson() == before );
    CHECK_FALSE( document.IsDirty() );
    document.History.Redo();
    REQUIRE( document.Save() );
    CHECK_FALSE( document.IsDirty() );
    SequenceClipData saved;
    REQUIRE( saved.Load( path, error ) );
    CHECK( saved.Tracks[0].Keys[1].Frame == 50 );
    document.History.Undo();
    CHECK( document.IsDirty() );
    REQUIRE( saved.Load( path, error ) );
    CHECK( saved.Tracks[0].Keys[1].Frame == 50 );
}
#endif
