#include <doctest/doctest.h>
#include <Widgets/ChoiceWidgets.h>
#include <Widgets/ItemLog.h>
#include "Reflection/Reflection.h"
#include "ECS/ComponentDetail.h"
#include "Engine/World.h"
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace MeWidgets;
using namespace MeWidgets::ChoiceLogic;

namespace
{
    // A monospaced font: 7 px a character.
    const Measure kMono = []( std::string_view InText ) { return 7.f * static_cast<float>( InText.size() ); };

    void Collect( const Reflection::TypeInfo* InType, std::set<const Reflection::TypeInfo*>& OutTypes )
    {
        if( !InType || !OutTypes.insert( InType ).second ) return;
        for( const auto& field : InType->Fields )
        {
            const Reflection::ValueOps* ops = field.Ops;
            while( ops && ops->Type == Reflection::PropertyType::Array ) ops = ops->Element;
            if( ops && ops->Type == Reflection::PropertyType::Struct && ops->GetStructType ) Collect( &ops->GetStructType(), OutTypes );
        }
        Collect( InType->GetBaseType(), OutTypes );
    }

    // Every reflected type, plus those the components expose (nested structs included).
    std::set<const Reflection::TypeInfo*> AllTypes()
    {
        std::set<const Reflection::TypeInfo*> types;
        // A copy: GetStructType registers a type the first time it's asked for, which can grow the
        // registry's list while this walks it.
        const std::vector<const Reflection::TypeInfo*> registered = Reflection::Registry::GetAllTypes();
        for( const Reflection::TypeInfo* type : registered ) Collect( type, types );
        auto world = std::make_shared<World>();
        EntityHandle entity = world->CreateEntity( "Choices" );
        entity->SetLoading( true );
        for( const auto& [name, info] : GetComponentRegistry() )
        {
            static const std::set<std::string> kSkip = { "Mesh", "Model", "UIDocument", "ScriptComponent" };
            if( kSkip.count( name ) ) continue;
            if( BaseComponent* component = entity->AddComponentByName( name ) )
            {
                Collect( component->GetTypeInfo(), types );
            }
        }
        return types;
    }
}

TEST_CASE( "Choice widgets: few values are segments, masks chips, the rest a combo" )
{
    CHECK( PickLook( 2, false ) == Look::Segmented );
    CHECK( PickLook( 5, false ) == Look::Segmented );
    CHECK( PickLook( 6, false ) == Look::Combo );
    CHECK( PickLook( 0, false ) == Look::Combo );
    // Masks with up to eight bits are chips; bigger ones keep the combo.
    CHECK( PickLook( 1, true ) == Look::Chips );
    CHECK( PickLook( 8, true ) == Look::Chips );
    CHECK( PickLook( 9, true ) == Look::Combo );
    CHECK( PickLook( 16, true ) == Look::Combo );
    CHECK( PickLook( 64, true ) == Look::Combo );
    CHECK( PickLook( 0, true ) == Look::Combo );
}

TEST_CASE( "Choice widgets: segments fit while each shows three characters" )
{
    // "Kinematic" needs "Kin." (28 px) at the least; "X" just 7.
    CHECK( MinLabelWidth( "Kinematic", kMono ) == doctest::Approx( 28.f ) );
    CHECK( MinLabelWidth( "X", kMono ) == doctest::Approx( 7.f ) );
    CHECK( MinLabelWidth( "Box", kMono ) == doctest::Approx( 21.f ) );
    CHECK( CellWidth( 100.f, 4, 0.f ) == doctest::Approx( 25.f ) );
    CHECK( CellWidth( 100.f, 3, 5.f ) == doctest::Approx( 30.f ) );
    CHECK( CellWidth( 10.f, 4, 5.f ) == doctest::Approx( 0.f ) );

    const std::string_view bodies[] = { "Static", "Kinematic", "Dynamic" };
    // 3 cells of 36 px, 4 px padding each side: 28 px of room, just enough.
    CHECK( CellsFit( 108.f, 0.f, 4.f, bodies, nullptr, 3, 14.f, kMono ) );
    CHECK_FALSE( CellsFit( 105.f, 0.f, 4.f, bodies, nullptr, 3, 14.f, kMono ) );
    // An icon needs only its own width.
    const bool icons[] = { true, true, true };
    CHECK( CellsFit( 70.f, 0.f, 4.f, bodies, icons, 3, 14.f, kMono ) );
    CHECK_FALSE( CellsFit( 100.f, 0.f, 4.f, bodies, nullptr, 0, 14.f, kMono ) );
    // Names that shorten alike don't fit until the cells are wide enough to tell them apart.
    const std::string_view plays[] = { "PlayAudio", "PlayAnimation" };
    // 2 cells of 50 - 8 = 42 px: both "PlayA." (42 px).
    CHECK_FALSE( CellsFit( 100.f, 0.f, 4.f, plays, nullptr, 2, 14.f, kMono ) );
    // 2 of 57 - 8 = 49 px: "PlayAu." and "PlayAn.".
    CHECK( CellsFit( 114.f, 0.f, 4.f, plays, nullptr, 2, 14.f, kMono ) );
}

TEST_CASE( "Choice widgets: names shorten with a dot, whole when they fit" )
{
    CHECK( ShortLabel( "Dynamic", 49.f, kMono ) == "Dynamic" );
    CHECK( ShortLabel( "Dynamic", 48.f, kMono ) == "Dynam." );
    CHECK( ShortLabel( "Kinematic", 28.f, kMono ) == "Kin." );
    // Trailing spaces go before the dot.
    CHECK( ShortLabel( "Not Walkable", 35.f, kMono ) == "Not." );
    CHECK( ShortLabel( "Orthographic", 1.f, kMono ) == "O." );
    CHECK( ShortLabel( "", 1.f, kMono ).empty() );
    // Never a "~".
    CHECK( ShortLabel( "Perspective", 40.f, kMono ).find( '~' ) == std::string::npos );
}

TEST_CASE( "Choice widgets: arrows step the choice and stop at the ends" )
{
    CHECK( StepIndex( 0, 3, 1 ) == 1 );
    CHECK( StepIndex( 2, 3, 1 ) == 2 );
    CHECK( StepIndex( 0, 3, -1 ) == 0 );
    CHECK( StepIndex( 2, 3, -1 ) == 1 );
    // Mixed (no choice): Right starts at the first, Left at the last.
    CHECK( StepIndex( -1, 3, 1 ) == 0 );
    CHECK( StepIndex( -1, 3, -1 ) == 2 );
    CHECK( StepIndex( 1, 0, 1 ) == -1 );
}

TEST_CASE( "Choice widgets: mask chips toggle bits and summarize like the combo" )
{
    CHECK( ToggleBit( 0b101u, 1 ) == 0b111u );
    CHECK( ToggleBit( 0b111u, 0 ) == 0b110u );
    CHECK( ToggleBit( 5u, 64 ) == 5u );
    CHECK( ToggleBit( 0u, 63 ) == ( 1ull << 63 ) );
    // Everything / Nothing touch only the choice bits.
    CHECK( SetAll( 0b1000'0001u, 0b0111u, true ) == 0b1000'0111u );
    CHECK( SetAll( 0b1000'0111u, 0b0111u, false ) == 0b1000'0000u );

    const std::string names[] = { "Walkable", "Not Walkable", "Jump" };
    CHECK( MaskSummary( 0b111u, 0b111u, names, 3 ) == "Everything" );
    CHECK( MaskSummary( 0b1000u, 0b111u, names, 3 ) == "Nothing" );
    CHECK( MaskSummary( 0b100u, 0b111u, names, 3 ) == "Jump" );
    CHECK( MaskSummary( 0b011u, 0b111u, names, 3 ) == "Mixed (2)" );

    // Chips show every name whole ("Not Walkable" is 84 px) and wrap onto as many lines as it takes.
    const std::string_view chips[] = { "Walkable", "Not Walkable", "Jump", "Water" };
    CHECK( ChipColumns( 400.f, 2.f, 4.f, chips, 4, kMono ) == 4 );
    // 4 columns of (200 - 6) / 4 - 8 = 40.5 px and 3 of 57 are too small; 2 of 91 fit.
    CHECK( ChipColumns( 200.f, 2.f, 4.f, chips, 4, kMono ) == 2 );
    // One a line: 95 - 8 = 87 px.
    CHECK( ChipColumns( 95.f, 2.f, 4.f, chips, 4, kMono ) == 1 );
    CHECK( ChipColumns( 90.f, 2.f, 4.f, chips, 4, kMono ) == 0 );
    CHECK( ChipColumns( 40.f, 2.f, 4.f, chips, 0, kMono ) == 0 );
    // Sixteen chips ("Area 15" is 49 px): five a line, four lines.
    std::string_view areas[16];
    std::vector<std::string> areaNames;
    for( int i = 0; i < 16; ++i ) areaNames.push_back( "Area " + std::to_string( i ) );
    for( int i = 0; i < 16; ++i ) areas[i] = areaNames[i];
    CHECK( ChipColumns( 300.f, 2.f, 4.f, areas, 16, kMono ) == 5 );
}

// Every reflected enum and named-choice field is drawn the way UX-13 says, in an inspector value
// column of kColumn px (mono, 7 px a character: about 16 em at a 13 px font): an enum or named
// choice with up to five values fits as segments, and the segments' shortened names stay
// distinct (no two read "Lin."; two enums need a wider column, kWide); every mask fits as chips with distinct whole names. The names
// are the ones the inspector shows (ReflectionUI skips choice slots with an empty name), and none
// is empty or has a "~". The known fields land where UX-13 expects.
TEST_CASE( "Choice widgets: every reflected enum and choice field fits its look" )
{
    using namespace Reflection;
    constexpr float kColumn = 210.f;
    constexpr float kWideColumn = 340.f;
    constexpr float kPadding = 4.f;
    constexpr float kGap = 4.f;
    std::map<std::string, Look> looks;
    int enums = 0;
    int segmented = 0;
    int chipped = 0;
    for( const TypeInfo* type : AllTypes() )
    {
        for( const FieldInfo& field : type->Fields )
        {
            const std::string where = type->Name + "." + field.Name;
            const ValueOps* ops = field.Ops;
            while( ops && ops->Type == PropertyType::Array ) ops = ops->Element;
            if( !ops ) continue;
            std::vector<std::string> names;
            Look look = Look::Combo;
            if( ops->Type == PropertyType::Enum )
            {
                const EnumInfo* info = ops->GetEnumInfo();
                REQUIRE_MESSAGE( info, where << ": an enum field without reflected values" );
                CHECK_MESSAGE( !info->Values.empty(), where << ": an enum with no values" );
                for( const auto& [name, value] : info->Values )
                {
                    CHECK_MESSAGE( !name.empty(), where << ": an unnamed value" );
                    names.push_back( name );
                }
                look = PickLook( static_cast<int>( names.size() ), false );
                ++enums;
            }
            else if( field.ChoiceName && field.ChoiceCount > 0 )
            {
                for( int i = 0; i < field.ChoiceCount && i < 64; ++i )
                {
                    std::string name = field.ChoiceName( i );
                    if( !name.empty() ) names.push_back( std::move( name ) );
                }
                look = PickLook( static_cast<int>( names.size() ), field.ChoiceMask );
            }
            else
            {
                continue;
            }
            looks[where] = look;
            for( const std::string& name : names )
            {
                CHECK_MESSAGE( name.find( '~' ) == std::string::npos, where << ": " << name );
            }
            std::vector<std::string_view> views( names.begin(), names.end() );
            const int count = static_cast<int>( views.size() );
            if( look == Look::Segmented )
            {
                ++segmented;
                // These names only differ late ("SmoothStep" / "SmoothFocalLength", "PlayAudio" /
                // "PlayAnimation"): a combo in a normal column, segments in a wide one.
                static const std::set<std::string> kWide = { "SequenceKey.Interpolation", "SequenceMarker.Action" };
                const float column = kWide.count( where ) ? kWideColumn : kColumn;
                if( kWide.count( where ) )
                {
                    CHECK_MESSAGE( !CellsFit( kColumn, 0.f, kPadding, views.data(), nullptr, count, 14.f, kMono ), where << ": fits now; drop it from kWide" );
                }
                CHECK_MESSAGE( CellsFit( column, 0.f, kPadding, views.data(), nullptr, count, 14.f, kMono ), where << ": its segments don't fit" );
                const float room = CellWidth( column, count, 0.f ) - 2.f * kPadding;
                std::set<std::string> shown;
                for( const std::string& name : names ) shown.insert( ShortLabel( name, room, kMono ) );
                CHECK_MESSAGE( shown.size() == names.size(), where << ": two segments read the same" );
            }
            else if( look == Look::Chips )
            {
                ++chipped;
                CHECK_MESSAGE( ChipColumns( kColumn, kGap, kPadding, views.data(), count, kMono ) > 0, where << ": its chips don't fit" );
                CHECK_MESSAGE( std::set<std::string>( names.begin(), names.end() ).size() == names.size(), where << ": two chips have one name" );
            }
        }
    }
    CHECK( enums > 20 );
    CHECK( segmented > 10 );
    auto lookOf = [&]( const char* InWhere ) {
        const auto found = looks.find( InWhere );
        REQUIRE_MESSAGE( found != looks.end(), InWhere << " isn't reflected" );
        return found->second;
    };
    CHECK( lookOf( "Rigidbody.BodyType" ) == Look::Segmented );
    CHECK( lookOf( "AudioSource.Rolloff" ) == Look::Segmented );
    CHECK( lookOf( "Camera.Projection" ) == Look::Segmented );
    CHECK( lookOf( "PostProcess.Tonemapper" ) == Look::Segmented );
    CHECK( lookOf( "Light.LightType" ) == Look::Combo );
    CHECK( lookOf( "NavMeshAgent.AreaMask" ) == Look::Combo );
    CHECK( lookOf( "NavMeshSurface.IncludeLayers" ) == Look::Combo );
}

// A segment named by its value only answers to a query scoped by its row, so a field with the same
// name as an enum value (PhysicsJoint's Distance field and its Joint Type's Distance segment) keeps
// resolving to the field.
TEST_CASE( "Choice widgets: segment names need their row in the query" )
{
    using namespace MeWidgets::ItemLog;
    Item segment;
    segment.Label = "Distance";
    segment.Row = "Joint Type";
    segment.Group = "PhysicsJoint";
    segment.NeedsRowScope = true;
    Item field;
    field.Row = "Distance";
    field.Group = "PhysicsJoint";
    CHECK_FALSE( Matches( segment, Parse( "PhysicsJoint/Distance" ) ) );
    CHECK_FALSE( Matches( segment, Parse( "Distance" ) ) );
    CHECK( Matches( field, Parse( "PhysicsJoint/Distance" ) ) );
    CHECK( Matches( segment, Parse( "PhysicsJoint/Joint Type/Distance" ) ) );
    CHECK( Matches( segment, Parse( "Joint Type/Distance" ) ) );
    // The row query still finds the segments' row (the control itself comes first in a frame).
    CHECK( Matches( segment, Parse( "PhysicsJoint/Joint Type" ) ) );
    segment.NeedsRowScope = false;
    CHECK( Matches( segment, Parse( "PhysicsJoint/Distance" ) ) );
}
