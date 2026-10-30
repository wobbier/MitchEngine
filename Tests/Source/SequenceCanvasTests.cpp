#include <doctest/doctest.h>
#include "Dementia.h"
#if USING( ME_EDITOR )
#include "../../Modules/Havana/Source/Widgets/SequenceTimeline.h"
#include <imgui.h>

TEST_CASE( "Sequence canvas: mouse marquee selects keys across tracks and dragging moves the selection together" )
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    struct Cleanup
    {
        ImGuiContext* Context;
        ImGuiContext* Previous;
        ~Cleanup() { ImGui::DestroyContext( Context ); ImGui::SetCurrentContext( Previous ); }
    } cleanup{ context, previous };
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 1280.f, 800.f );
    io.DeltaTime = 1.f / 60.f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    Sequence sequence;
    sequence.DurationFrames = 60;
    sequence.Tracks.resize( 2 );
    for( auto& track : sequence.Tracks ) track.Keys = { { 10, Vector4( 1.f ), SequenceInterpolation::Linear }, { 20, Vector4( 2.f ), SequenceInterpolation::Linear }, { 40, Vector4( 3.f ), SequenceInterpolation::Linear } };
    SequenceTimeline timeline;
    int frame = 0, track = 0;
    auto tick = [&]( ImVec2 mouse, bool down, bool disabled = false ) {
        io.AddMousePosEvent( mouse.x, mouse.y );
        io.AddMouseButtonEvent( ImGuiMouseButton_Left, down );
        ImGui::NewFrame();
        ImGui::SetNextWindowPos( ImVec2( 5.f, 5.f ) );
        ImGui::SetNextWindowSize( ImVec2( 1100.f, 700.f ) );
        ImGui::Begin( "Sequence Canvas Test" );
        ImGui::BeginDisabled( disabled );
        timeline.Draw( sequence, frame, track );
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::Render();
    };
    tick( ImVec2( -100.f, -100.f ), false );
    const ImVec2 from = timeline.KeyPosition( 5.f, -0.35f );
    const ImVec2 to = timeline.KeyPosition( 25.f, 1.35f );
    tick( from, false );
    tick( from, true );
    tick( to, true );
    tick( to, false );
    REQUIRE( timeline.Selected.size() == 4 );
    const ImVec2 key = timeline.KeyPosition( 10.f, 0.f );
    const ImVec2 moved = timeline.KeyPosition( 15.f, 0.f );
    tick( key, false );
    tick( key, true );
    tick( moved, true );
    tick( moved, false );
    REQUIRE( timeline.Selected.size() == 4 );
    for( const auto& lane : sequence.Tracks )
    {
        REQUIRE( lane.Keys.size() == 3 );
        CHECK( lane.Keys[0].Frame == 15 );
        CHECK( lane.Keys[1].Frame == 25 );
        CHECK( lane.Keys[2].Frame == 40 );
    }
    // A runtime transition must cancel an in-progress edit rather than moving live tracks.
    const ImVec2 selectedKey = timeline.KeyPosition( 15.f, 0.f );
    tick( selectedKey, false );
    tick( selectedKey, true );
    tick( timeline.KeyPosition( 18.f, 0.f ), true, true );
    tick( timeline.KeyPosition( 18.f, 0.f ), false, true );
    CHECK( sequence.Tracks[0].Keys[0].Frame == 15 );
    CHECK( sequence.Tracks[1].Keys[0].Frame == 15 );

    // GAME-4: double-clicking the Events lane adds a marker; dragging it re-sorts the list and
    // Escape puts it back. Keys stay where they were.
    SequenceMarker existing;
    existing.Frame = 30;
    existing.Name = "Existing";
    sequence.Markers = { existing };
    timeline.Selected.clear();
    const ImVec2 lane = timeline.MarkerPosition( 12.f );
    tick( lane, false );
    tick( lane, true );
    tick( lane, false );
    tick( lane, true );
    tick( lane, false );
    REQUIRE( sequence.Markers.size() == 2 );
    CHECK( sequence.Markers[0].Frame == 12 );
    CHECK( timeline.SelectedMarker == 0 );
    tick( lane, true );
    tick( timeline.MarkerPosition( 45.f ), true );
    tick( timeline.MarkerPosition( 45.f ), false );
    REQUIRE( sequence.Markers.size() == 2 );
    CHECK( sequence.Markers[0].Name == "Existing" );
    CHECK( sequence.Markers[1].Frame == 45 );
    CHECK( timeline.SelectedMarker == 1 );
    const ImVec2 dragged = timeline.MarkerPosition( 45.f );
    tick( dragged, false );
    tick( dragged, true );
    tick( timeline.MarkerPosition( 5.f ), true );
    CHECK( sequence.Markers[0].Frame == 5 );
    io.AddKeyEvent( ImGuiKey_Escape, true );
    tick( timeline.MarkerPosition( 5.f ), true );
    io.AddKeyEvent( ImGuiKey_Escape, false );
    tick( timeline.MarkerPosition( 5.f ), false );
    CHECK( sequence.Markers[1].Frame == 45 );
    CHECK( sequence.Markers[0].Frame == 30 );
    CHECK( sequence.Tracks[0].Keys[0].Frame == 15 );
}
#endif
