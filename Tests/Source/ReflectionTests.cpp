#include <doctest/doctest.h>

#include "Reflection/Reflection.h"
#include <string>
#include <vector>

namespace ReflectionTest
{
    enum class Shape : int
    {
        Box = 0,
        Sphere = 3,
        Capsule = 7
    };

    struct Inner
    {
        ME_REFLECTABLE( Inner )
    public:
        float Weight = 1.f;
        std::vector<int> Tags;
    };

    struct Base
    {
        ME_REFLECTABLE( Base )
    public:
        virtual ~Base() = default;
        bool Enabled = true;
    };

    class Thing
        : public Base
    {
        ME_REFLECTABLE( Thing )
    public:
        int Count = 3;
        uint8_t Small = 200;
        float m_fieldOfView = 45.f;
        double Precise = 0.5;
        std::string Label = "thing";
        Vector3 Position = Vector3( 1.f, 2.f, 3.f );
        Quaternion Rotation;
        Shape Kind = Shape::Sphere;
        Inner Settings;
        std::vector<Inner> Children;
        int RuntimeOnly = 0;

        int GetSecret() const { return m_secret; }

    private:
        int m_secret = 11;
    };
}

using namespace ReflectionTest;

ME_REFLECT_ENUM( ReflectionTest::Shape, { { "Box", Shape::Box }, { "Sphere", Shape::Sphere }, { "Capsule", Shape::Capsule } } )

ME_REFLECT_BEGIN( ReflectionTest::Inner )
    ME_FIELD( Weight ).Range( 0.f, 10.f );
    ME_FIELD( Tags );
ME_REFLECT_END()

ME_REFLECT_BEGIN( ReflectionTest::Base )
    ME_FIELD( Enabled );
ME_REFLECT_END()

ME_REFLECT_BEGIN( ReflectionTest::Thing )
    ME_REFLECT_BASE( ReflectionTest::Base );
    ME_FIELD( Count ).Tooltip( "How many" );
    ME_FIELD( Small );
    ME_FIELD_NAMED( m_fieldOfView, "FOV" ).Angle();
    ME_FIELD( Precise );
    ME_FIELD( Label );
    ME_FIELD( Position );
    ME_FIELD( Rotation );
    ME_FIELD( Kind );
    ME_FIELD( Settings );
    ME_FIELD( Children );
    ME_FIELD( RuntimeOnly ).NoSerialize();
    ME_FIELD( m_secret ).Hidden();
ME_REFLECT_END()
ME_REFLECT_REGISTER( ReflectionTest::Thing )


TEST_CASE( "Reflection describes fields, types and metadata" )
{
    const Reflection::TypeInfo& type = Thing::StaticType();
    CHECK( type.Name == "ReflectionTest::Thing" );
    CHECK( type.GetBaseType() == &Base::StaticType() );

    const Reflection::FieldInfo* count = type.FindField( "Count" );
    REQUIRE( count );
    CHECK( count->GetType() == Reflection::PropertyType::Int );
    CHECK( count->Tooltip == "How many" );

    const Reflection::FieldInfo* fov = type.FindField( "FOV" );
    REQUIRE( fov );
    CHECK( fov->Has( Reflection::FieldFlags::Angle ) );
    CHECK( fov->DisplayName == "FOV" );

    CHECK( type.FindField( "Enabled" ) != nullptr );
    CHECK( type.FindField( "Kind" )->GetType() == Reflection::PropertyType::Enum );
    CHECK( type.FindField( "Settings" )->GetType() == Reflection::PropertyType::Struct );
    CHECK( type.FindField( "Children" )->GetType() == Reflection::PropertyType::Array );
    CHECK( type.FindField( "m_secret" )->Has( Reflection::FieldFlags::Hidden ) );
    CHECK( type.FindField( "Missing" ) == nullptr );

    CHECK( Reflection::Registry::FindType( "ReflectionTest::Thing" ) == &type );
}


TEST_CASE( "Reflection prettifies member names" )
{
    CHECK( Reflection::Detail::PrettifyName( "m_fieldOfView" ) == "Field Of View" );
    CHECK( Reflection::Detail::PrettifyName( "mClearColor" ) == "Clear Color" );
    CHECK( Reflection::Detail::PrettifyName( "MaxHTTPRetries" ) == "Max HTTP Retries" );
    CHECK( Reflection::Detail::PrettifyName( "ShadowCascade2" ) == "Shadow Cascade 2" );
    CHECK( Reflection::Detail::PrettifyName( "is_trigger" ) == "Is trigger" );
}


TEST_CASE( "Reflection JSON round trip" )
{
    Thing source;
    source.Enabled = false;
    source.Count = -42;
    source.Small = 7;
    source.m_fieldOfView = 70.f;
    source.Precise = 0.125;
    source.Label = "hello";
    source.Position = Vector3( -1.f, 0.5f, 9.f );
    source.Rotation = Quaternion( 0.f, 0.7071068f, 0.f, 0.7071068f );
    source.Kind = Shape::Capsule;
    source.Settings.Weight = 4.f;
    source.Settings.Tags = { 1, 2, 3 };
    source.Children.resize( 2 );
    source.Children[1].Weight = 9.f;
    source.RuntimeOnly = 99;

    json data;
    Reflection::ToJson( Thing::StaticType(), &source, data );

    CHECK( data["Enabled"] == false );
    CHECK( data["Count"] == -42 );
    CHECK( data["Kind"] == "Capsule" );
    CHECK( data["Position"].size() == 3 );
    CHECK( data["Settings"]["Tags"].size() == 3 );
    CHECK_FALSE( data.contains( "RuntimeOnly" ) );

    Thing loaded;
    Reflection::FromJson( Thing::StaticType(), &loaded, data );
    CHECK( loaded.Enabled == false );
    CHECK( loaded.Count == -42 );
    CHECK( loaded.Small == 7 );
    CHECK( loaded.m_fieldOfView == doctest::Approx( 70.f ) );
    CHECK( loaded.Precise == doctest::Approx( 0.125 ) );
    CHECK( loaded.Label == "hello" );
    CHECK( loaded.Position.x == doctest::Approx( -1.f ) );
    CHECK( loaded.Position.z == doctest::Approx( 9.f ) );
    CHECK( loaded.Rotation.y == doctest::Approx( 0.7071068f ) );
    CHECK( loaded.Kind == Shape::Capsule );
    CHECK( loaded.Settings.Weight == doctest::Approx( 4.f ) );
    CHECK( loaded.Settings.Tags == std::vector<int>{ 1, 2, 3 } );
    REQUIRE( loaded.Children.size() == 2 );
    CHECK( loaded.Children[1].Weight == doctest::Approx( 9.f ) );
    CHECK( loaded.RuntimeOnly == 0 );
}


TEST_CASE( "Reflection keeps defaults for missing keys and survives bad types" )
{
    Thing thing;
    json partial = { { "Count", 5 }, { "Label", 12 }, { "Kind", "NotAShape" } };
    Reflection::FromJson( Thing::StaticType(), &thing, partial );
    CHECK( thing.Count == 5 );
    CHECK( thing.Label == "thing" );
    CHECK( thing.Kind == Shape::Sphere );
    CHECK( thing.Position.y == doctest::Approx( 2.f ) );

    // Enums also accept their integer value.
    Reflection::FromJson( Thing::StaticType(), &thing, json{ { "Kind", 0 } } );
    CHECK( thing.Kind == Shape::Box );
}


TEST_CASE( "Reflection resolves property paths" )
{
    Thing thing;
    thing.Children.resize( 3 );
    thing.Settings.Tags = { 4, 5 };

    CHECK( Reflection::SetPathJson( Thing::StaticType(), &thing, "Settings.Weight", 2.5f ) );
    CHECK( thing.Settings.Weight == doctest::Approx( 2.5f ) );

    CHECK( Reflection::SetPathJson( Thing::StaticType(), &thing, "Children[2].Weight", 8.f ) );
    CHECK( thing.Children[2].Weight == doctest::Approx( 8.f ) );

    CHECK( Reflection::SetPathJson( Thing::StaticType(), &thing, "Settings.Tags[1]", 50 ) );
    CHECK( thing.Settings.Tags[1] == 50 );

    CHECK( Reflection::SetPathJson( Thing::StaticType(), &thing, "Enabled", false ) );
    CHECK( thing.Enabled == false );

    CHECK( Reflection::GetPathJson( Thing::StaticType(), &thing, "Children[2].Weight" ) == 8.f );
    CHECK_FALSE( Reflection::SetPathJson( Thing::StaticType(), &thing, "Children[9].Weight", 1.f ) );
    CHECK_FALSE( Reflection::SetPathJson( Thing::StaticType(), &thing, "Nope", 1 ) );
    CHECK_FALSE( Reflection::SetPathJson( Thing::StaticType(), &thing, "Count.Sub", 1 ) );
}


TEST_CASE( "Reflection diffs and copies" )
{
    Thing a;
    Thing b;
    CHECK( Reflection::DiffFields( Thing::StaticType(), &a, &b ).empty() );

    b.Count = 77;
    b.Settings.Weight = 3.f;
    b.Enabled = false;
    const std::vector<std::string> diff = Reflection::DiffFields( Thing::StaticType(), &a, &b );
    CHECK( diff.size() == 3 );

    Reflection::CopyFields( Thing::StaticType(), &b, &a );
    CHECK( a.Count == 77 );
    CHECK( a.Settings.Weight == doctest::Approx( 3.f ) );
    CHECK( Reflection::DiffFields( Thing::StaticType(), &a, &b ).empty() );
}
