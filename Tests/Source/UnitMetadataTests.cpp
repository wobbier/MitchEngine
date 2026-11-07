#include <doctest/doctest.h>
#include "Reflection/Reflection.h"
#include "ECS/ComponentDetail.h"
#include "Engine/World.h"
#include <Widgets/Units.h>
#include <set>
#include <string>

namespace
{
    // Every reflected type a component exposes, nested structs included.
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

    std::set<const Reflection::TypeInfo*> ComponentTypes()
    {
        std::set<const Reflection::TypeInfo*> types;
        for( const Reflection::TypeInfo* type : Reflection::Registry::GetAllTypes() ) Collect( type, types );
        auto world = std::make_shared<World>();
        EntityHandle entity = world->CreateEntity( "Metadata" );
        // Loading defers Init, so components never touch cores or resources.
        entity->SetLoading( true );
        for( const auto& [name, info] : GetComponentRegistry() )
        {
            // Unreflected components that need a renderer or script runtime to construct.
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

// UX-4: every reflected type's unit metadata is consistent: soft ranges sit inside hard ranges,
// log fields have a usable range, defaults are in range, converting units only on fitting
// ranges, and no label still carries the unit its field now shows.
TEST_CASE( "Unit metadata: every reflected field's units, ranges and defaults agree" )
{
    using namespace Reflection;
    int tagged = 0;
    const char* kUnitSuffixes[] = { "(m)", "(deg)", "(degrees)", "(Hz)", "(mm)", "(m/s)", "(f-stop)", "(s)", "(kg)", "(ms)", "(N)", "(K)", "(dB)", "(%)", "(EV)", "(px)" };
    const std::set<const TypeInfo*> types = ComponentTypes();
    CHECK( types.size() > 30 );
    for( const TypeInfo* type : types )
    {
        for( const FieldInfo& field : type->Fields )
        {
            const std::string where = type->Name + "." + field.Name;
            if( field.FieldUnit != Unit::None )
            {
                ++tagged;
                for( const char* suffix : kUnitSuffixes )
                {
                    const std::string text = suffix;
                    const bool endsWith = field.DisplayName.size() >= text.size() && field.DisplayName.compare( field.DisplayName.size() - text.size(), text.size(), text ) == 0;
                    CHECK_MESSAGE( !endsWith, where << ": the label \"" << field.DisplayName << "\" repeats the unit the value shows" );
                }
            }
            if( field.HasSoftRange )
            {
                CHECK_MESSAGE( field.SoftMin < field.SoftMax, where << ": empty soft range" );
                if( field.HasRange )
                {
                    CHECK_MESSAGE( field.SoftMin >= field.Min - 1e-4f, where << ": soft range starts below the hard range" );
                    CHECK_MESSAGE( field.SoftMax <= field.Max + 1e-4f, where << ": soft range ends above the hard range" );
                }
            }
            if( field.LogScale )
            {
                const bool hasSpan = field.HasSoftRange || field.HasRange;
                const float min = field.HasSoftRange ? field.SoftMin : field.Min;
                CHECK_MESSAGE( hasSpan, where << ": a log field needs a range" );
                CHECK_MESSAGE( min >= 0.f, where << ": a log range can't start below zero" );
            }
            if( field.DefaultCount == 1 && field.HasRange )
            {
                CHECK_MESSAGE( field.DefaultValue[0] >= field.Min - 1e-4f, where << ": default below the range" );
                CHECK_MESSAGE( field.DefaultValue[0] <= field.Max + 1e-4f, where << ": default above the range" );
            }
            if( field.FieldUnit == Unit::Percent && field.HasRange )
            {
                // Percent stores 1 = 100%; a multiplier may go past it (150% saturation), not past 1000%.
                CHECK_MESSAGE( field.Max <= 10.f, where << ": Percent stores 1 = 100%, but the range goes to " << field.Max );
            }
            if( field.FieldUnit == Unit::LinearGain && field.HasRange )
            {
                CHECK_MESSAGE( field.Min >= 0.f, where << ": a linear gain can't be negative" );
            }
            if( field.FieldUnit != Unit::None )
            {
                // The default (if named) shows and parses back in its unit.
                if( field.DefaultCount == 1 )
                {
                    double back = 0.0;
                    const std::string shown = MeWidgets::FormatValue( field.FieldUnit, field.DefaultValue[0], 6 );
                    CHECK_MESSAGE( MeWidgets::ParseValue( field.FieldUnit, shown, back ), where << ": \"" << shown << "\" doesn't parse back" );
                }
            }
        }
    }
    MESSAGE( tagged << " fields carry a unit across " << types.size() << " reflected types" );
    CHECK( tagged > 150 );
}
