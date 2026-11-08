#include <doctest/doctest.h>
#include "Dementia.h"
#include "Reflection/Reflection.h"
#include <Widgets/Units.h>
#include <Widgets/ScrubField.h>
#include <Widgets/ItemLog.h>
#include <imgui.h>
#include <cmath>

using namespace MeWidgets;
using Reflection::Unit;

namespace
{
    double Parsed( Unit InUnit, const char* InText )
    {
        double value = -12345.0;
        std::string error;
        REQUIRE_MESSAGE( ParseValue( InUnit, InText, value, &error ), InText << ": " << error );
        return value;
    }

    std::string Rejected( Unit InUnit, const char* InText )
    {
        double value = 0.0;
        std::string error;
        CHECK_FALSE( ParseValue( InUnit, InText, value, &error ) );
        return error;
    }
}

TEST_CASE( "Units: values show in their unit with about four significant digits" )
{
    CHECK( FormatNumber( 12.3456 ) == "12.35" );
    CHECK( FormatNumber( 0.025 ) == "0.025" );
    CHECK( FormatNumber( 1500.0 ) == "1500" );
    CHECK( FormatNumber( 1.5 ) == "1.5" );
    CHECK( FormatNumber( -0.0 ) == "0" );
    CHECK( FormatNumber( 2.0, 2 ) == "2.00" );

    CHECK( FormatValue( Unit::Meters, 2.5 ) == "2.5 m" );
    CHECK( FormatValue( Unit::Degrees, 90.0 ) == "90\xC2\xB0" );
    CHECK( FormatValue( Unit::Radians, 3.14159265358979 / 2.0 ) == "90\xC2\xB0" );
    CHECK( FormatValue( Unit::Degrees, 21.29 ) == "21.3\xC2\xB0" );
    CHECK( FormatValue( Unit::Percent, 0.505 ) == "50.5%" );
    CHECK( FormatValue( Unit::Degrees, 0.26 ) == "0.3\xC2\xB0" );
    CHECK( FormatValue( Unit::Kelvin, 6500.4 ) == "6500 K" );
    CHECK( FormatValue( Unit::LinearGain, 0.5 ) == "-6.0 dB" );
    CHECK( FormatValue( Unit::LinearGain, 0.0 ) == "-inf dB" );
    CHECK( FormatValue( Unit::Percent, 0.5 ) == "50%" );
    CHECK( FormatValue( Unit::EV, 1.5 ) == "+1.5 EV" );
    CHECK( FormatValue( Unit::EV, -2.0 ) == "-2.0 EV" );
    CHECK( FormatValue( Unit::FStop, 2.8 ) == "f/2.8" );
    CHECK( FormatValue( Unit::FStop, 16.0 ) == "f/16" );
    CHECK( FormatValue( Unit::PitchRatio, 2.0 ) == "+12 st" );
    CHECK( FormatValue( Unit::Multiplier, 2.0 ) == "2\xC3\x97" );
    CHECK( FormatValue( Unit::None, 0.1 ) == "0.1" );
    // Values that round to zero carry no sign.
    CHECK( FormatValue( Unit::Degrees, -0.04 ) == "0\xC2\xB0" );
    CHECK( FormatValue( Unit::LinearGain, 0.9999 ) == "0.0 dB" );
    CHECK( FormatValue( Unit::EV, -0.04 ) == "0.0 EV" );
    CHECK( FormatValue( Unit::EV, 0.004, 2 ) == "0.00 EV" );
    // Huge values switch to scientific notation instead of being cut off.
    CHECK( FormatNumber( 1e100 ) == "1e+100" );
}

TEST_CASE( "Units: typed entry understands units, other units of the same kind and arithmetic" )
{
    CHECK( Parsed( Unit::Meters, "2" ) == doctest::Approx( 2.0 ) );
    CHECK( Parsed( Unit::Meters, "2 m" ) == doctest::Approx( 2.0 ) );
    CHECK( Parsed( Unit::Meters, "150 cm" ) == doctest::Approx( 1.5 ) );
    CHECK( Parsed( Unit::Meters, "150cm" ) == doctest::Approx( 1.5 ) );
    CHECK( Parsed( Unit::Meters, "1 km" ) == doctest::Approx( 1000.0 ) );
    CHECK( Parsed( Unit::Meters, "6 ft" ) == doctest::Approx( 1.8288 ) );
    CHECK( Parsed( Unit::Meters, "2*3 m" ) == doctest::Approx( 6.0 ) );
    CHECK( Parsed( Unit::Meters, "1 m + 50 cm" ) == doctest::Approx( 1.5 ) );
    CHECK( Parsed( Unit::Meters, "(1+2)/4" ) == doctest::Approx( 0.75 ) );
    CHECK( Parsed( Unit::Meters, "-2.5" ) == doctest::Approx( -2.5 ) );
    CHECK( Parsed( Unit::Meters, "10/2" ) == doctest::Approx( 5.0 ) );

    CHECK( Parsed( Unit::Degrees, "90\xC2\xB0" ) == doctest::Approx( 90.0 ) );
    CHECK( Parsed( Unit::Degrees, "0.5 turn" ) == doctest::Approx( 180.0 ) );
    CHECK( Parsed( Unit::Radians, "90 deg" ) == doctest::Approx( 3.14159265358979 / 2.0 ) );
    CHECK( Parsed( Unit::Kelvin, "5000K" ) == doctest::Approx( 5000.0 ) );
    CHECK( Parsed( Unit::LinearGain, "-6 dB" ) == doctest::Approx( 0.501187 ).epsilon( 1e-4 ) );
    CHECK( Parsed( Unit::LinearGain, "0" ) == doctest::Approx( 1.0 ) );
    CHECK( Parsed( Unit::LinearGain, "-inf" ) == doctest::Approx( 0.0 ) );
    CHECK( Parsed( Unit::Percent, "25%" ) == doctest::Approx( 0.25 ) );
    CHECK( Parsed( Unit::Kilograms, "500 g" ) == doctest::Approx( 0.5 ) );
    CHECK( Parsed( Unit::Seconds, "250 ms" ) == doctest::Approx( 0.25 ) );
    CHECK( Parsed( Unit::Seconds, "2 min" ) == doctest::Approx( 120.0 ) );
    CHECK( Parsed( Unit::MetersPerSecond, "36 km/h" ) == doctest::Approx( 10.0 ) );
    CHECK( Parsed( Unit::MetersPerSecond, "3 m/s" ) == doctest::Approx( 3.0 ) );
    CHECK( Parsed( Unit::FStop, "f/2.8" ) == doctest::Approx( 2.8 ) );
    CHECK( Parsed( Unit::PitchRatio, "+12 st" ) == doctest::Approx( 2.0 ) );
    CHECK( Parsed( Unit::PitchRatio, "1 oct" ) == doctest::Approx( 2.0 ) );
    // A unit on the divisor names the result's unit.
    CHECK( Parsed( Unit::Degrees, "1/4 turn" ) == doctest::Approx( 90.0 ) );
    CHECK( Parsed( Unit::Meters, "1/2 in" ) == doctest::Approx( 0.0127 ) );
    CHECK( Parsed( Unit::Meters, "150 cm / 2" ) == doctest::Approx( 0.75 ) );
    CHECK( Parsed( Unit::Meters, "1/(2 cm)" ) == doctest::Approx( 50.0 ) );
    // "k" is the colour-temperature shorthand for thousands; "K" is kelvin.
    CHECK( Parsed( Unit::Kelvin, "6.5k" ) == doctest::Approx( 6500.0 ) );
    CHECK( Parsed( Unit::Kelvin, "5600K" ) == doctest::Approx( 5600.0 ) );
    // "m" doesn't swallow the start of "min".
    CHECK( Rejected( Unit::Meters, "2 min" ).find( "isn't a length" ) != std::string::npos );

    CHECK( Rejected( Unit::Meters, "5 kg" ).find( "'kg' isn't a length" ) != std::string::npos );
    CHECK( Rejected( Unit::Meters, "" ).find( "type a number" ) != std::string::npos );
    CHECK( Rejected( Unit::Meters, "(1+2" ).find( "never closed" ) != std::string::npos );
    CHECK( Rejected( Unit::Meters, "1/0" ).find( "divide by zero" ) != std::string::npos );
    CHECK( Rejected( Unit::Meters, "abc" ).find( "isn't a length" ) == std::string::npos );
    CHECK( Rejected( Unit::Meters, "inf" ).find( "finite" ) != std::string::npos );

    // Whatever a value shows, typing it back gives the same value.
    for( Unit unit : { Unit::Meters, Unit::Degrees, Unit::Radians, Unit::LinearGain, Unit::Percent, Unit::EV, Unit::FStop, Unit::PitchRatio, Unit::Kilograms } )
    {
        const double stored = unit == Unit::LinearGain ? 0.25 : unit == Unit::PitchRatio ? 1.5 : 2.75;
        double back = 0.0;
        const std::string shown = FormatValue( unit, stored, 6 );
        REQUIRE_MESSAGE( ParseValue( unit, shown, back ), shown );
        CHECK_MESSAGE( back == doctest::Approx( stored ).epsilon( 1e-5 ), shown );
    }
}

TEST_CASE( "Units: the log map is monotonic, invertible and reaches a zero minimum" )
{
    CHECK( LogMap::ToNormalized( 1.0, 1.0, 100.0 ) == doctest::Approx( 0.0 ) );
    CHECK( LogMap::ToNormalized( 10.0, 1.0, 100.0 ) == doctest::Approx( 0.5 ) );
    CHECK( LogMap::ToNormalized( 100.0, 1.0, 100.0 ) == doctest::Approx( 1.0 ) );
    CHECK( LogMap::FromNormalized( 0.5, 1.0, 100.0 ) == doctest::Approx( 10.0 ) );
    CHECK( LogMap::ToNormalized( 0.0, 0.0, 1000.0 ) == doctest::Approx( 0.0 ) );
    CHECK( LogMap::FromNormalized( 0.0, 0.0, 1000.0 ) == doctest::Approx( 0.0 ) );
    // A zero-based log range still gives the small values most of the travel.
    CHECK( LogMap::ToNormalized( 10.0, 0.0, 1000.0 ) > 0.3 );
    double previous = -1.0;
    for( int i = 0; i <= 100; ++i )
    {
        const double t = i / 100.0;
        const double value = LogMap::FromNormalized( t, 0.0, 1000.0 );
        CHECK( value >= previous );
        CHECK( LogMap::ToNormalized( value, 0.0, 1000.0 ) == doctest::Approx( t ).epsilon( 1e-6 ) );
        previous = value;
    }
    CHECK( NiceStep( 100.0 ) == doctest::Approx( 10.0 ) );
    CHECK( NiceStep( 3.0 ) == doctest::Approx( 0.2 ) );
    CHECK( NiceStep( 89.0 ) == doctest::Approx( 10.0 ) );
}

TEST_CASE( "Unit field: scrubs cover the soft range across the field, in log space for log fields" )
{
    NumberSpec linear;
    linear.HasRange = true;
    linear.Min = 0.0;
    linear.Max = 100.0;
    CHECK( ScrubValue( 10.0, 50.0, linear, 100.f ) == doctest::Approx( 60.0 ) );

    NumberSpec soft = linear;
    soft.HasSoftRange = true;
    soft.SoftMin = 0.0;
    soft.SoftMax = 10.0;
    CHECK( ScrubValue( 1.0, 100.0, soft, 100.f ) == doctest::Approx( 11.0 ) );
    // The hard range still clamps what a scrub can reach.
    CHECK( Constrain( 140.0, soft ) == doctest::Approx( 100.0 ) );

    NumberSpec log;
    log.HasRange = true;
    log.Min = 1.0;
    log.Max = 1000.0;
    log.Log = true;
    // A third of the field is a decade.
    CHECK( ScrubValue( 10.0, 100.0, log, 300.f ) == doctest::Approx( 100.0 ) );
    CHECK( ScrubValue( 100.0, -100.0, log, 300.f ) == doctest::Approx( 10.0 ) );
    CHECK( FillFraction( 10.0, log ) == doctest::Approx( 1.0 / 3.0 ) );

    NumberSpec unbounded;
    unbounded.Speed = 0.1;
    CHECK( ScrubValue( 1.0, 40.0, unbounded, 300.f ) == doctest::Approx( 5.0 ) );
    CHECK( FillFraction( 1.0, unbounded ) < 0.0 );

    NumberSpec whole;
    whole.Integer = true;
    CHECK( Constrain( 2.6, whole ) == doctest::Approx( 3.0 ) );

    // A clamp (a type's limits, a .Drag field's range) limits values without setting the span.
    NumberSpec clamped;
    clamped.Speed = 0.25;
    clamped.HasClamp = true;
    clamped.ClampMin = 0.0;
    clamped.ClampMax = 255.0;
    CHECK( ScrubValue( 10.0, 4.0, clamped, 300.f ) == doctest::Approx( 11.0 ) );
    CHECK( Constrain( 300.0, clamped ) == doctest::Approx( 255.0 ) );
    CHECK( FillFraction( 10.0, clamped ) < 0.0 );
}

namespace UnitFieldTest
{
    struct Probe
    {
        ME_REFLECTABLE( Probe )
    public:
        float Distance = 10.f;
        float Angle = 0.5f;
    };
}

ME_REFLECT_BEGIN( UnitFieldTest::Probe )
    ME_FIELD( Distance ).Unit( Unit::Meters ).Log().SoftRange( 0.5f, 50.f ).Range( 0.01f, 1000.f ).Default( 10.f ).Widget( Reflection::WidgetKind::Scrub );
    ME_FIELD( Angle ).Angle();
ME_REFLECT_END()

TEST_CASE( "Reflection: unit, log, soft range, default and widget metadata round-trip" )
{
    const Reflection::TypeInfo& type = UnitFieldTest::Probe::StaticType();
    const Reflection::FieldInfo* distance = nullptr;
    const Reflection::FieldInfo* angle = nullptr;
    for( const auto& field : type.Fields )
    {
        if( field.Name == "Distance" ) distance = &field;
        if( field.Name == "Angle" ) angle = &field;
    }
    REQUIRE( distance );
    REQUIRE( angle );
    CHECK( distance->FieldUnit == Unit::Meters );
    CHECK( distance->LogScale );
    CHECK( distance->HasSoftRange );
    CHECK( distance->SoftMin == doctest::Approx( 0.5f ) );
    CHECK( distance->SoftMax == doctest::Approx( 50.f ) );
    CHECK( distance->HasRange );
    CHECK( distance->DefaultCount == 1 );
    CHECK( distance->DefaultValue[0] == doctest::Approx( 10.f ) );
    CHECK( distance->Widget == Reflection::WidgetKind::Scrub );
    CHECK( angle->FieldUnit == Unit::None );
    CHECK( angle->Has( Reflection::FieldFlags::Angle ) );
    CHECK_FALSE( angle->LogScale );
}

TEST_CASE( "Unit field: a drag scrubs, a click types, a label double-click resets" )
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
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    ItemLog::Enable( true );

    NumberSpec spec;
    spec.FieldUnit = Unit::Meters;
    spec.HasRange = true;
    spec.Min = 0.0;
    spec.Max = 100.0;
    spec.HasDefault = true;
    spec.Default = 10.0;
    double value = 10.0;
    int edits = 0;
    auto frame = [&]( ImVec2 mouse, bool down, const char* text = nullptr, bool enter = false ) {
        io.AddMousePosEvent( mouse.x, mouse.y );
        io.AddMouseButtonEvent( ImGuiMouseButton_Left, down );
        if( text ) io.AddInputCharactersUTF8( text );
        if( enter ) io.AddKeyEvent( ImGuiKey_Enter, true );
        ImGui::NewFrame();
        ImGui::SetNextWindowPos( ImVec2( 0.f, 0.f ) );
        ImGui::SetNextWindowSize( ImVec2( 600.f, 200.f ) );
        ImGui::Begin( "Unit Field Test" );
        if( UnitRow( "Range", value, spec ) ) ++edits;
        ImGui::End();
        ImGui::Render();
        if( enter ) io.AddKeyEvent( ImGuiKey_Enter, false );
    };
    frame( ImVec2( -10.f, -10.f ), false );
    frame( ImVec2( -10.f, -10.f ), false );
    std::string error;
    const ItemLog::Item* field = ItemLog::Find( "Range", error );
    REQUIRE_MESSAGE( field, error );
    CHECK( field->Value == "10 m" );
    const ImVec2 center = field->Rect.GetCenter();
    const float fieldWidth = field->Rect.GetWidth();
    const ItemLog::Item* label = ItemLog::Find( "Range:label", error );
    REQUIRE( label );
    const ImVec2 labelCenter = label->Rect.GetCenter();

    // Drag the value box a fifth of its width: a fifth of the 0..100 range.
    frame( center, false );
    frame( center, true );
    for( int i = 1; i <= 4; ++i ) frame( ImVec2( center.x + fieldWidth * 0.05f * i, center.y ), true );
    frame( ImVec2( center.x + fieldWidth * 0.2f, center.y ), false );
    CHECK( value == doctest::Approx( 30.0 ).epsilon( 0.02 ) );
    CHECK( edits > 0 );

    // Click without moving: the box turns into text entry; "250 cm" + Enter sets 2.5.
    frame( center, false );
    frame( center, true );
    frame( center, false );
    frame( center, false );
    frame( center, false, "250 cm", true );
    frame( center, false );
    CHECK( value == doctest::Approx( 2.5 ) );

    // A typo keeps the value.
    frame( center, true );
    frame( center, false );
    frame( center, false );
    frame( center, false, "2 kg", true );
    frame( center, false );
    CHECK( value == doctest::Approx( 2.5 ) );

    // Double-click the label: back to the default.
    frame( labelCenter, false );
    frame( labelCenter, true );
    frame( labelCenter, false );
    frame( labelCenter, true );
    frame( labelCenter, false );
    CHECK( value == doctest::Approx( 10.0 ) );

    // Shift scrubs the label ten times finer.
    io.AddKeyEvent( ImGuiMod_Shift, true );
    frame( labelCenter, false );
    frame( labelCenter, true );
    frame( ImVec2( labelCenter.x + fieldWidth * 0.5f, labelCenter.y ), true );
    frame( ImVec2( labelCenter.x + fieldWidth * 0.5f, labelCenter.y ), false );
    io.AddKeyEvent( ImGuiMod_Shift, false );
    CHECK( value == doctest::Approx( 15.0 ).epsilon( 0.02 ) );
}
