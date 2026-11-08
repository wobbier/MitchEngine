#include <doctest/doctest.h>
#include "Dementia.h"
#include <Widgets/ItemLog.h>
#include <Utils/HavanaUtils.h>
#include <imgui.h>

using namespace MeWidgets;

TEST_CASE( "ItemLog: queries parse scopes, the row label suffix, an index and raw IDs" )
{
    const ItemLog::Query plain = ItemLog::Parse( "Intensity" );
    CHECK( plain.Name == "Intensity" );
    CHECK( plain.Scopes.empty() );
    CHECK( plain.Index == 1 );
    CHECK_FALSE( plain.WantLabel );

    const ItemLog::Query scoped = ItemLog::Parse( " Light / Inner Cone Angle:label@2 " );
    REQUIRE( scoped.Scopes.size() == 1 );
    CHECK( scoped.Scopes[0] == "Light" );
    CHECK( scoped.Name == "Inner Cone Angle" );
    CHECK( scoped.WantLabel );
    CHECK( scoped.Index == 2 );

    CHECK( ItemLog::Parse( "0x1A2B3C4D" ).Id == 0x1A2B3C4Du );
    // An @ that isn't followed by digits is part of the name.
    CHECK( ItemLog::Parse( "Mail@home" ).Name == "Mail@home" );

    CHECK( ItemLog::VisibleLabel( "Bake##3" ) == "Bake" );
    CHECK( ItemLog::VisibleLabel( "###hidden" ).empty() );
    CHECK( ItemLog::VisibleLabel( "Plain" ) == "Plain" );
}

TEST_CASE( "ItemLog: matching uses labels, rows, scopes and drops a unit suffix" )
{
    ItemLog::Item axis;
    axis.Label = "Y";
    axis.Row = "Position (m)";
    axis.Group = "Transform";
    axis.Section = "Transform";
    axis.Window = "Properties";

    CHECK( ItemLog::Matches( axis, ItemLog::Parse( "Position" ) ) );
    CHECK( ItemLog::Matches( axis, ItemLog::Parse( "Transform/Position/Y" ) ) );
    CHECK( ItemLog::Matches( axis, ItemLog::Parse( "properties/transform/position (m)" ) ) );
    CHECK_FALSE( ItemLog::Matches( axis, ItemLog::Parse( "Light/Position" ) ) );
    CHECK_FALSE( ItemLog::Matches( axis, ItemLog::Parse( "Position:label" ) ) );

    ItemLog::Item label = axis;
    label.Label = "Position (m)";
    label.IsRowLabel = true;
    CHECK( ItemLog::Matches( label, ItemLog::Parse( "Transform/Position:label" ) ) );
    CHECK_FALSE( ItemLog::Matches( label, ItemLog::Parse( "Transform/Position" ) ) );

    // A field inside a category tree still answers to its component.
    ItemLog::Item shadows;
    shadows.Label = "";
    shadows.Row = "Cast Shadows";
    shadows.Group = "Light";
    shadows.Section = "Shadows";
    shadows.Window = "Properties";
    CHECK( ItemLog::Matches( shadows, ItemLog::Parse( "Light/Cast Shadows" ) ) );
    CHECK( ItemLog::Matches( shadows, ItemLog::Parse( "Shadows/Cast Shadows" ) ) );
}

TEST_CASE( "ItemLog: records last frame's items with rows, sections and values" )
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    struct Cleanup
    {
        ImGuiContext* Context;
        ImGuiContext* Previous;
        ~Cleanup() { ItemLog::Enable( false ); ImGui::DestroyContext( Context ); ImGui::SetCurrentContext( Previous ); }
    } cleanup{ context, previous };
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 1280.f, 800.f );
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    ItemLog::Enable( true );

    float speed = 2.f;
    bool loud = true;
    auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos( ImVec2( 10.f, 10.f ) );
        ImGui::SetNextWindowSize( ImVec2( 600.f, 400.f ) );
        ImGui::Begin( "Probe" );
        ImGui::CollapsingHeader( "##header" );
        ItemLog::MarkSection( "Engine" );
        HavanaUtils::Label( "Speed (m/s)" );
        ImGui::DragFloat( "##value", &speed );
        ItemLog::SetLastItemValue( "2 m/s" );
        HavanaUtils::Label( "Loud" );
        ImGui::Checkbox( "##value", &loud );
        ImGui::Button( "Bake##1" );
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();

    std::string error;
    const ItemLog::Item* value = ItemLog::Find( "Engine/Speed", error );
    REQUIRE_MESSAGE( value, error );
    CHECK( value->Value == "2 m/s" );
    CHECK( value->Window == "Probe" );
    CHECK_FALSE( value->IsRowLabel );

    const ItemLog::Item* label = ItemLog::Find( "Speed:label", error );
    REQUIRE( label );
    CHECK( label->Rect.Max.x <= value->Rect.Min.x );

    const ItemLog::Item* check = ItemLog::Find( "Probe/Loud", error );
    REQUIRE( check );
    CHECK( ( check->Flags & ImGuiItemStatusFlags_Checkable ) != 0 );
    CHECK( ( check->Flags & ImGuiItemStatusFlags_Checked ) != 0 );

    const ItemLog::Item* bake = ItemLog::Find( "Bake", error );
    REQUIRE( bake );
    // The button sits on its own line, below the rows: it inherits no row name.
    CHECK( bake->Row.empty() );
    CHECK_FALSE( ItemLog::Find( "Missing", error ) );
    CHECK( error.find( "no item matches" ) != std::string::npos );
    CHECK_FALSE( ItemLog::Find( "Speed@2", error ) );

    ItemLog::Enable( false );
    CHECK( ItemLog::LastFrame().empty() );
}

TEST_CASE( "ItemLog: open tree nodes scope what's inside them only; closed ones and ended sections scope nothing" )
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    struct Cleanup
    {
        ImGuiContext* Context;
        ImGuiContext* Previous;
        ~Cleanup() { ItemLog::Enable( false ); ImGui::DestroyContext( Context ); ImGui::SetCurrentContext( Previous ); }
    } cleanup{ context, previous };
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 1280.f, 800.f );
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    ItemLog::Enable( true );

    bool flag = false;
    auto frame = [&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos( ImVec2( 10.f, 10.f ) );
        ImGui::SetNextWindowSize( ImVec2( 600.f, 160.f ) );
        ImGui::Begin( "Scopes" );
        ImGui::CollapsingHeader( "##header" );
        ItemLog::MarkSection( "Light" );
        ImGui::SetNextItemOpen( true );
        if( ImGui::TreeNode( "Shadows" ) )
        {
            ImGui::Checkbox( "Cast", &flag );
            ImGui::TreePop();
        }
        ImGui::Button( "After Tree" );
        ImGui::SetNextItemOpen( false );
        if( ImGui::TreeNode( "Closed" ) )
        {
            ImGui::TreePop();
        }
        ImGui::Button( "After Closed" );
        ItemLog::EndSection();
        ImGui::Button( "Outside" );
        // Far below the window's bottom edge: clipped.
        ImGui::Dummy( ImVec2( 10.f, 400.f ) );
        ImGui::Button( "Far Below" );
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();

    std::string error;
    CHECK( ItemLog::Find( "Light/Shadows/Cast", error ) );
    const ItemLog::Item* after = ItemLog::Find( "After Tree", error );
    REQUIRE( after );
    CHECK( after->Section == "Light" );
    CHECK_FALSE( ItemLog::Find( "Shadows/After Tree", error ) );
    CHECK_FALSE( ItemLog::Find( "Closed/After Closed", error ) );
    CHECK( ItemLog::Find( "Light/After Closed", error ) );
    const ItemLog::Item* outside = ItemLog::Find( "Outside", error );
    REQUIRE( outside );
    CHECK( outside->Group.empty() );

    // A clipped button skips its label hook; it keeps the label it had while visible (here it
    // never was, so it stays unnamed until it is scrolled into view once).
    const ItemLog::Item* visible = ItemLog::Find( "After Closed", error );
    REQUIRE( visible );
    CHECK( visible->IsPointable() );
    CHECK_FALSE( ItemLog::Find( "Far Below", error ) );
}
