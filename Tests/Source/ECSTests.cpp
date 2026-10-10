#include <doctest/doctest.h>

#include "Engine/World.h"
#include "ECS/Core.h"
#include "Components/Transform.h"
#include "Math/Bounds.h"
#include "World/SceneSerializer.h"
#include <memory>
#include <string>
#include <vector>

namespace ECSTest
{
    struct LifecycleLog
    {
        std::vector<std::string> Events;
        void Clear() { Events.clear(); }
        bool Has( const std::string& event ) const
        {
            for( const std::string& e : Events )
            {
                if( e == event )
                {
                    return true;
                }
            }
            return false;
        }
    };

    LifecycleLog g_log;

    class Health
        : public Component<Health>
    {
        ME_REFLECTABLE( Health )
    public:
        Health() : Component( "Health" ) {}
        explicit Health( int value ) : Component( "Health" ), Value( value ) {}

        void Init() override { g_log.Events.push_back( "Init" ); }
        void OnEnable() override { g_log.Events.push_back( "Enable" ); }
        void OnDisable() override { g_log.Events.push_back( "Disable" ); }
        void OnDestroy() override { g_log.Events.push_back( "Destroy" ); }

        int Value = 100;
        EntityHandle Target;
    };

    class Frozen
        : public Component<Frozen>
    {
    public:
        Frozen() : Component( "Frozen" ) {}
        void OnSerialize( json& ) override {}
        void OnDeserialize( const json& ) override {}
    };

    class Shield
        : public Component<Shield>
    {
    public:
        Shield() : Component( "Shield" ) {}
        void OnSerialize( json& ) override {}
        void OnDeserialize( const json& ) override {}
    };

    class HealthCore
        : public Core<HealthCore>
    {
    public:
        HealthCore() : Base( ComponentFilter().Requires<Health>().Excludes<Frozen>() ) {}

        void OnEntityAdded( Entity& ) override { ++Added; }
        void OnEntityRemoved( Entity& ) override { ++Removed; }
        void OnEntityDestroyed( Entity& ) override { ++Destroyed; }

        int Added = 0;
        int Removed = 0;
        int Destroyed = 0;
    };

    class ProtectedCore
        : public Core<ProtectedCore>
    {
    public:
        ProtectedCore() : Base( ComponentFilter().RequiresOneOf<Shield>().RequiresOneOf<Frozen>() ) {}
    };

    std::shared_ptr<World> MakeWorld()
    {
        auto world = std::make_shared<World>();
        world->IsLoading = false;
        return world;
    }
}

using namespace ECSTest;

ME_REFLECT_BEGIN( ECSTest::Health )
    ME_FIELD( Value );
    ME_FIELD( Target );
ME_REFLECT_END()


TEST_CASE( "ECS: entities have generational ids and handles go stale" )
{
    auto world = MakeWorld();
    EntityHandle a = world->CreateEntity( "A" );
    REQUIRE( a );
    CHECK( a->GetName() == "A" );
    CHECK( world->GetEntityCount() == 1 );

    const EntityID oldId = a.GetID();
    a->MarkForDelete();
    a->MarkForDelete();   // double delete must be harmless
    CHECK( a );           // deferred until the sync point
    world->Simulate();
    CHECK_FALSE( a );
    CHECK( world->GetEntityCount() == 0 );

    // The slot is reused with a new generation; the old id never resolves to the new entity.
    EntityHandle b = world->CreateEntity( "B" );
    CHECK( b.GetID().Index == oldId.Index );
    CHECK( b.GetID().Generation != oldId.Generation );
    CHECK_FALSE( world->EntityExists( oldId ) );
    CHECK( world->FindEntityByIDValue( b.GetID().Value() ) == b );
}


TEST_CASE( "ECS: components add, get and defer removal" )
{
    auto world = MakeWorld();
    g_log.Clear();
    EntityHandle e = world->CreateEntity();

    Health& health = e->AddComponent<Health>( 42 );
    CHECK( health.Value == 42 );
    CHECK( g_log.Has( "Init" ) );
    CHECK( e->HasComponent<Health>() );
    CHECK( e->TryGetComponent<Health>() == &health );
    CHECK( e->TryGetComponent<Shield>() == nullptr );
    CHECK( &e->AddComponent<Health>() == &health );   // adding twice returns the existing one
    CHECK( health.Parent == e );

    world->Simulate();
    CHECK( g_log.Has( "Enable" ) );

    g_log.Clear();
    e->RemoveComponent<Health>();
    CHECK( e->HasComponent<Health>() );   // still there until the sync point
    world->Simulate();
    CHECK_FALSE( e->HasComponent<Health>() );
    CHECK( g_log.Has( "Disable" ) );
    CHECK( g_log.Has( "Destroy" ) );
}


TEST_CASE( "ECS: component addresses are stable across many additions" )
{
    auto world = MakeWorld();
    EntityHandle first = world->CreateEntity();
    Health* firstHealth = &first->AddComponent<Health>( 1 );
    for( int i = 0; i < 2000; ++i )
    {
        world->CreateEntity()->AddComponent<Health>( i );
    }
    CHECK( first->TryGetComponent<Health>() == firstHealth );
    CHECK( firstHealth->Value == 1 );
}


TEST_CASE( "ECS: core membership follows filters, activation and enabled state" )
{
    HealthCore core;   // declared first: cores must outlive the world that references them
    auto world = MakeWorld();
    world->AddCore( core );

    EntityHandle e = world->CreateEntity();
    e->AddComponent<Health>();
    world->Simulate();
    CHECK( core.GetEntities().size() == 1 );
    CHECK( core.Added == 1 );

    // Excludes<Frozen>
    e->AddComponent<Frozen>();
    world->Simulate();
    CHECK( core.GetEntities().empty() );
    CHECK( core.Removed == 1 );

    e->RemoveComponent<Frozen>();
    world->Simulate();
    CHECK( core.GetEntities().size() == 1 );

    // Disabled components don't count toward filters.
    e->GetComponent<Health>().SetEnabled( false );
    world->Simulate();
    CHECK( core.GetEntities().empty() );
    e->GetComponent<Health>().SetEnabled( true );
    world->Simulate();
    CHECK( core.GetEntities().size() == 1 );

    // Inactive entities leave every core.
    e->SetActive( false );
    world->Simulate();
    CHECK( core.GetEntities().empty() );
    CHECK_FALSE( e->IsActiveInHierarchy() );
    e->SetActive( true );
    world->Simulate();
    CHECK( core.GetEntities().size() == 1 );

    // Destroying removes and notifies.
    e->MarkForDelete();
    world->Simulate();
    CHECK( core.GetEntities().empty() );
    CHECK( core.Destroyed == 1 );
}


TEST_CASE( "ECS: RequiresOneOf rejects entities with none of the components" )
{
    ProtectedCore core;
    auto world = MakeWorld();
    world->AddCore( core );

    EntityHandle plain = world->CreateEntity();
    plain->AddComponent<Health>();
    EntityHandle shielded = world->CreateEntity();
    shielded->AddComponent<Shield>();
    world->Simulate();

    REQUIRE( core.GetEntities().size() == 1 );
    CHECK( core.GetEntities()[0] == *shielded.Get() );
}


TEST_CASE( "ECS: cores see entities that existed before the core was added" )
{
    HealthCore core;
    auto world = MakeWorld();
    EntityHandle e = world->CreateEntity();
    e->AddComponent<Health>();
    world->Simulate();

    world->AddCore( core );
    world->Simulate();
    CHECK( core.GetEntities().size() == 1 );
}


TEST_CASE( "ECS: destroying a parent destroys its children; activity is inherited" )
{
    auto world = MakeWorld();
    EntityHandle parent = world->CreateEntity( "Parent" );
    EntityHandle child = world->CreateEntity( "Child" );
    EntityHandle grandchild = world->CreateEntity( "Grandchild" );
    Transform& parentTransform = parent->AddComponent<Transform>();
    Transform& childTransform = child->AddComponent<Transform>();
    grandchild->AddComponent<Transform>().SetParent( childTransform );
    childTransform.SetParent( parentTransform );
    grandchild->AddComponent<Health>();
    world->Simulate();
    CHECK( grandchild->IsActiveInHierarchy() );

    parent->SetActive( false );
    world->Simulate();
    CHECK_FALSE( child->IsActiveInHierarchy() );
    CHECK_FALSE( grandchild->IsActiveInHierarchy() );
    CHECK( grandchild->IsActiveSelf() );

    parent->SetActive( true );
    world->Simulate();
    CHECK( grandchild->IsActiveInHierarchy() );

    g_log.Clear();
    parent->MarkForDelete();
    world->Simulate();
    CHECK_FALSE( parent );
    CHECK_FALSE( child );
    CHECK_FALSE( grandchild );
    CHECK( g_log.Has( "Destroy" ) );
    CHECK( world->GetEntityCount() == 0 );
}


TEST_CASE( "ECS: GUIDs are unique and resolve entity references" )
{
    auto world = MakeWorld();
    EntityHandle a = world->CreateEntity();
    EntityHandle b = world->CreateEntity();
    CHECK( a->GetGUID() != 0 );
    CHECK( a->GetGUID() != b->GetGUID() );
    CHECK( world->FindEntityByGUID( b->GetGUID() ) == b );

    Health& health = a->AddComponent<Health>();
    health.Target = b;

    json saved;
    a->GetComponent<Health>().Serialize( saved );
    CHECK( SceneSerializer::GUIDFromJson( saved["Target"] ) == b->GetGUID() );

    EntityHandle c = world->CreateEntity();
    Health& loaded = c->AddComponent<Health>();
    {
        SerializationWorldScope scope( world.get() );
        loaded.Deserialize( saved );
    }
    CHECK( loaded.Target == b );
}


TEST_CASE( "ECS: World::Each visits matching active entities" )
{
    auto world = MakeWorld();
    for( int i = 0; i < 10; ++i )
    {
        EntityHandle e = world->CreateEntity();
        e->AddComponent<Health>( i );
        if( i % 2 == 0 )
        {
            e->AddComponent<Shield>();
        }
        if( i == 4 )
        {
            e->SetActive( false );
        }
    }
    world->Simulate();

    int visited = 0;
    int sum = 0;
    world->Each<Health, Shield>( [&]( Entity&, Health& health, Shield& ) {
        ++visited;
        sum += health.Value;
    } );
    CHECK( visited == 4 );                  // 0, 2, 6, 8 (4 is inactive)
    CHECK( sum == 0 + 2 + 6 + 8 );
}


TEST_CASE( "Transform: moving a parent updates cached child matrices" )
{
    auto world = MakeWorld();
    Transform& parent = world->CreateEntity()->AddComponent<Transform>();
    Transform& child = world->CreateEntity()->AddComponent<Transform>();
    child.SetParent( parent );
    child.SetPosition( Vector3( 1.f, 0.f, 0.f ) );

    CHECK( child.GetWorldPosition().x == doctest::Approx( 1.f ) );   // caches the child matrix

    parent.SetPosition( Vector3( 10.f, 0.f, 0.f ) );                 // regression: child used to stay stale
    CHECK( child.GetWorldPosition().x == doctest::Approx( 11.f ) );

    parent.SetPosition( Vector3( 20.f, 0.f, 0.f ) );
    Transform::UpdateAll( *world );
    CHECK_FALSE( child.IsDirty() );
    CHECK( child.GetWorldPosition().x == doctest::Approx( 21.f ) );
}


TEST_CASE( "Transform: world space setters, rotation and reparenting" )
{
    auto world = MakeWorld();
    Transform& parent = world->CreateEntity()->AddComponent<Transform>();
    Transform& child = world->CreateEntity()->AddComponent<Transform>();
    parent.SetPosition( Vector3( 0.f, 5.f, 0.f ) );
    parent.SetRotation( Vector3( 0.f, 90.f, 0.f ) );
    parent.SetScale( 2.f );
    child.SetParent( parent );

    child.SetWorldPosition( Vector3( 3.f, 5.f, 4.f ) );
    const Vector3 world0 = child.GetWorldPosition();
    CHECK( world0.x == doctest::Approx( 3.f ).epsilon( 0.001 ) );
    CHECK( world0.z == doctest::Approx( 4.f ).epsilon( 0.001 ) );

    // Reparenting with keepWorldTransform preserves the world pose.
    Transform& other = world->CreateEntity()->AddComponent<Transform>();
    other.SetPosition( Vector3( -7.f, 1.f, 2.f ) );
    child.SetParent( other, true );
    const Vector3 world1 = child.GetWorldPosition();
    CHECK( world1.x == doctest::Approx( 3.f ).epsilon( 0.001 ) );
    CHECK( world1.y == doctest::Approx( 5.f ).epsilon( 0.001 ) );
    CHECK( world1.z == doctest::Approx( 4.f ).epsilon( 0.001 ) );

    // Cycles are refused.
    other.SetParent( child );
    CHECK( other.GetParentTransform() == nullptr );

    // LookAt points Front() at the target.
    Transform& looker = world->CreateEntity()->AddComponent<Transform>();
    looker.LookAt( Vector3( 10.f, 0.f, 0.f ) );
    const Vector3 front = looker.Front();
    CHECK( front.x == doctest::Approx( 1.f ).epsilon( 0.001 ) );
    CHECK( std::abs( front.z ) < 0.001f );

    // Translate in local space follows the rotation.
    looker.Translate( Vector3( 0.f, 0.f, 2.f ), TransformSpace::Self );
    CHECK( looker.GetWorldPosition().x == doctest::Approx( 2.f ).epsilon( 0.001 ) );
}


TEST_CASE( "Transform: serializes rotation as a quaternion and reads legacy Euler" )
{
    auto world = MakeWorld();
    Transform& transform = world->CreateEntity()->AddComponent<Transform>();
    transform.Deserialize( json{ { "Position", { 1.f, 2.f, 3.f } }, { "Rotation", { 0.f, 90.f, 0.f } }, { "Scale", { 1.f, 1.f, 1.f } } } );
    CHECK( transform.GetRotationEuler().y == doctest::Approx( 90.f ).epsilon( 0.01 ) );

    json saved;
    transform.Serialize( saved );
    CHECK( saved["Rotation"].size() == 4 );

    Transform& copy = world->CreateEntity()->AddComponent<Transform>();
    copy.Deserialize( saved );
    CHECK( copy.GetRotation().y == doctest::Approx( transform.GetRotation().y ) );
    CHECK( copy.GetPosition().z == doctest::Approx( 3.f ) );
}


TEST_CASE( "Bounds: AABB, sphere and ray tests" )
{
    AABB box( Vector3( -1.f ), Vector3( 1.f ) );
    CHECK( box.Contains( Vector3( 0.5f ) ) );
    CHECK_FALSE( box.Contains( Vector3( 2.f ) ) );

    Ray ray( Vector3( -5.f, 0.f, 0.f ), Vector3( 1.f, 0.f, 0.f ) );
    CHECK( ray.Intersect( box ) == doctest::Approx( 4.f ) );
    Ray miss( Vector3( -5.f, 3.f, 0.f ), Vector3( 1.f, 0.f, 0.f ) );
    CHECK( miss.Intersect( box ) < 0.f );

    Sphere sphere( Vector3( 0.f, 0.f, 10.f ), 2.f );
    Ray forward( Vector3( 0.f ), Vector3( 0.f, 0.f, 1.f ) );
    CHECK( forward.Intersect( sphere ) == doctest::Approx( 8.f ) );

    Matrix4 translate( glm::translate( glm::mat4( 1.f ), glm::vec3( 10.f, 0.f, 0.f ) ) );
    AABB moved = box.Transformed( translate );
    CHECK( moved.Min.x == doctest::Approx( 9.f ) );
    CHECK( moved.Max.x == doctest::Approx( 11.f ) );
}


namespace CoreOrderTest
{
    // Priorities would run them Zeta(-5), Alpha(0), Beta(0), Gamma(10); the constraints say
    // Gamma before Alpha, and Zeta after Beta.
    class OrderAlphaCore : public Core<OrderAlphaCore>
    {
    public:
        OrderAlphaCore() : Base( ComponentFilter() ) { RunsAfter( "OrderGammaCore" ); }
    };

    class OrderBetaCore : public Core<OrderBetaCore>
    {
    public:
        OrderBetaCore() : Base( ComponentFilter() ) {}
    };

    class OrderGammaCore : public Core<OrderGammaCore>
    {
    public:
        OrderGammaCore() : Base( ComponentFilter() ) { Priority = 10; }
    };

    class OrderZetaCore : public Core<OrderZetaCore>
    {
    public:
        OrderZetaCore() : Base( ComponentFilter() ) { Priority = -5; RunsAfter<OrderBetaCore>(); RunsBefore( "NotInThisWorld" ); }
    };

    // A cycle: each waits for the other.
    class CycleOneCore : public Core<CycleOneCore>
    {
    public:
        CycleOneCore() : Base( ComponentFilter() ) { RunsAfter( "CycleTwoCore" ); }
    };

    class CycleTwoCore : public Core<CycleTwoCore>
    {
    public:
        CycleTwoCore() : Base( ComponentFilter() ) { RunsAfter( "CycleOneCore" ); }
    };

    std::vector<std::string> LoadedOrder( const World& InWorld )
    {
        std::vector<std::string> names;
        for( const BaseCore* core : InWorld.GetLoadedCores() )
        {
            names.push_back( core->GetName() );
        }
        return names;
    }
}

ME_REGISTER_CORE( CoreOrderTest::OrderAlphaCore )
ME_REGISTER_CORE( CoreOrderTest::OrderBetaCore )
ME_REGISTER_CORE( CoreOrderTest::OrderGammaCore )
ME_REGISTER_CORE( CoreOrderTest::OrderZetaCore )
ME_REGISTER_CORE( CoreOrderTest::CycleOneCore )
ME_REGISTER_CORE( CoreOrderTest::CycleTwoCore )

TEST_CASE( "ECS: cores run in priority order unless RunsAfter / RunsBefore say otherwise" )
{
    using namespace CoreOrderTest;
    auto world = MakeWorld();
    for( const char* name : { "OrderAlphaCore", "OrderBetaCore", "OrderGammaCore", "OrderZetaCore" } )
    {
        REQUIRE( world->AddCoreByName( name ) );
    }
    // Beta is first free by priority, then Zeta (waiting on Beta, priority -5) and Gamma (10)
    // before the Alpha waiting on it.
    CHECK( LoadedOrder( *world ) == std::vector<std::string>{ "OrderBetaCore", "OrderZetaCore", "OrderGammaCore", "OrderAlphaCore" } );

    // A cycle still orders every core (by priority, then name).
    auto cyclic = MakeWorld();
    REQUIRE( cyclic->AddCoreByName( "CycleTwoCore" ) );
    REQUIRE( cyclic->AddCoreByName( "CycleOneCore" ) );
    CHECK( LoadedOrder( *cyclic ) == std::vector<std::string>{ "CycleOneCore", "CycleTwoCore" } );
}
