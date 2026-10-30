#include <doctest/doctest.h>
#include "Components/Physics/Colliders.h"
#include "Components/Physics/PhysicsJoint.h"
#include "Components/Physics/Ragdoll.h"
#include "Components/Physics/Rigidbody.h"
#include "Components/Transform.h"
#include "Cores/PhysicsCore.h"
#include "Engine/World.h"
#include "Core/UpdateContext.h"

namespace RagdollTest
{
    struct TestContext
        : public UpdateContext
    {
        using UpdateContext::FixedDeltaTime;
        using UpdateContext::InterpolationAlpha;
        using UpdateContext::IsFixedStepActive;
    };

    // An owner with a three-bone chain (Root -> Mid -> Tip, 1 m apart, up +Y), like the skinned
    // test tube: Tip is a leaf bone, sized like Mid.
    struct Rig
    {
        PhysicsCore Physics;
        std::shared_ptr<World> GameWorld = std::make_shared<World>();
        EntityHandle Owner, Root, Mid, Tip;

        Rig()
        {
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Physics );
            Owner = Node( "Owner", Vector3( 0.f, 5.f, 0.f ), EntityHandle() );
            Root = Node( "Root", Vector3(), Owner );
            Mid = Node( "Mid", Vector3( 0.f, 1.f, 0.f ), Root );
            Tip = Node( "Tip", Vector3( 0.f, 1.f, 0.f ), Mid );
            Owner->AddComponent<Ragdoll>();
        }

        EntityHandle Node( const std::string& InName, const Vector3& InPosition, EntityHandle InParent )
        {
            EntityHandle entity = GameWorld->CreateEntity( InName );
            Transform& transform = entity->AddComponent<Transform>();
            transform.SetPosition( InPosition );
            if( InParent ) transform.SetParent( InParent->GetComponent<Transform>() );
            return entity;
        }

        Ragdoll& Doll() { return Owner->GetComponent<Ragdoll>(); }

        void Run( float InSeconds )
        {
            TestContext context;
            context.FixedDeltaTime = 1.f / 60.f;
            context.InterpolationAlpha = 1.f;
            for( int i = 0; i < static_cast<int>( InSeconds * 60.f + 0.5f ); ++i )
            {
                GameWorld->Simulate();
                context.IsFixedStepActive = true;
                Physics.FixedUpdate( context );
                context.IsFixedStepActive = false;
                Physics.Update( context );
            }
        }

        ~Rig() { GameWorld->Stop(); GameWorld->Destroy(); }
    };
}

using namespace RagdollTest;

TEST_CASE( "Ragdoll: Build gives each bone a body, a capsule along it and a cone joint to its parent" )
{
    Rig rig;
    REQUIRE( rig.Doll().Build() == 3 );
    CHECK( rig.Doll().Bones == std::vector<EntityHandle>{ rig.Root, rig.Mid, rig.Tip } );
    for( EntityHandle bone : { rig.Root, rig.Mid, rig.Tip } )
    {
        REQUIRE( bone->HasComponent<Rigidbody>() );
        CHECK( bone->GetComponent<Rigidbody>().Type == BodyType::Kinematic );
        const CapsuleCollider& capsule = bone->GetComponent<CapsuleCollider>();
        CHECK( capsule.Direction == CapsuleAxis::Y );
        CHECK( capsule.Center.y == doctest::Approx( 0.5f ) );
        CHECK( capsule.Radius == doctest::Approx( 0.2f ) );
    }
    CHECK_FALSE( rig.Root->HasComponent<PhysicsJoint>() );
    REQUIRE( rig.Tip->HasComponent<PhysicsJoint>() );
    const PhysicsJoint& joint = rig.Tip->GetComponent<PhysicsJoint>();
    CHECK( joint.Type == JointType::BallSocket );
    CHECK( joint.ConnectedBody == rig.Mid );
    CHECK( joint.UseLimits );
    CHECK( joint.UpperLimit == doctest::Approx( 45.f ) );
    float mass = 0.f;
    for( EntityHandle bone : rig.Doll().Bones ) mass += bone->GetComponent<Rigidbody>().Mass;
    CHECK( mass == doctest::Approx( 40.f ) );

    rig.Doll().Clear();
    rig.GameWorld->Simulate();   // removals apply on the next Simulate
    CHECK( rig.Doll().Bones.empty() );
    CHECK_FALSE( rig.Mid->HasComponent<Rigidbody>() );
    CHECK_FALSE( rig.Mid->HasComponent<PhysicsJoint>() );
}


TEST_CASE( "Ragdoll: Simulating hands the bones to physics, and recovering blends back to the pose" )
{
    Rig rig;
    rig.Doll().Build();
    rig.GameWorld->Simulate();
    rig.GameWorld->Start();
    rig.Run( 0.5f );
    // Kinematic: the chain stays where the pose put it.
    CHECK( rig.Tip->GetComponent<Transform>().GetWorldPosition().y == doctest::Approx( 7.f ) );
    rig.Doll().Blend( 0.f );   // what AnimationCore does each frame: the first one records the rest pose

    rig.Doll().Simulating = true;
    rig.Doll().OnPropertyChanged( "Simulating" );
    CHECK( rig.Mid->GetComponent<Rigidbody>().Type == BodyType::Dynamic );
    rig.Run( 1.f );
    const float fallen = rig.Tip->GetComponent<Transform>().GetWorldPosition().y;
    CHECK( fallen < 4.f );
    // The joints hold: the tip is still about a bone's length from the mid bone.
    const float span = ( rig.Tip->GetComponent<Transform>().GetWorldPosition() - rig.Mid->GetComponent<Transform>().GetWorldPosition() ).Length();
    CHECK( span == doctest::Approx( 1.f ).epsilon( 0.1 ) );

    // Recover: kinematic again, then the blend starts at the fallen pose and ends at the animation's.
    const Vector3 fallenLocal = rig.Root->GetComponent<Transform>().GetPosition();
    rig.Doll().Simulating = false;
    rig.Doll().OnPropertyChanged( "Simulating" );
    CHECK( rig.Mid->GetComponent<Rigidbody>().Type == BodyType::Kinematic );
    CHECK( rig.Doll().IsBlending() );
    // No Animator drives Root here: the blend's target is its rest pose (the origin).
    rig.Doll().Blend( 0.f );
    CHECK( ( rig.Root->GetComponent<Transform>().GetPosition() - fallenLocal ).Length() < 1e-3f );
    rig.Doll().Blend( 0.2f );
    const float halfway = ( rig.Root->GetComponent<Transform>().GetPosition() - fallenLocal ).Length();
    CHECK( halfway > 0.f );
    CHECK( halfway < fallenLocal.Length() );
    rig.Doll().Blend( 1.f );
    CHECK_FALSE( rig.Doll().IsBlending() );
    CHECK( rig.Root->GetComponent<Transform>().GetPosition().Length() < 1e-4f );
    CHECK( rig.Tip->GetComponent<Transform>().GetPosition().y == doctest::Approx( 1.f ) );
}
