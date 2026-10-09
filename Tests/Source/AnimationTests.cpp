#include <doctest/doctest.h>
#include "Components/Animation/Animator.h"
#include "Components/Transform.h"
#include "Cores/AnimationCore.h"
#include "Engine/World.h"
#include "Events/EventReceiver.h"
#include "Scene/AnimationClip.h"
#include <cmath>
#include <memory>

namespace AnimationTest
{
    // A clip moving node InNode along X from InFrom to InTo over InDuration seconds.
    Moonlight::AnimationClip Slide( const std::string& InName, const std::string& InNode, float InFrom, float InTo, float InDuration )
    {
        Moonlight::AnimationClip clip;
        clip.Name = InName;
        clip.Duration = InDuration;
        Moonlight::AnimationChannel channel;
        channel.NodeName = InNode;
        channel.PositionTimes = { 0.f, InDuration };
        channel.Positions = { Vector3( InFrom, 0.f, 0.f ), Vector3( InTo, 0.f, 0.f ) };
        clip.Channels.push_back( channel );
        return clip;
    }

    struct Rig
    {
        AnimationCore Core;
        std::shared_ptr<World> GameWorld;
        EntityHandle Root;
        EntityHandle Arm;

        explicit Rig( std::vector<Moonlight::AnimationClip> InClips )
        {
            GameWorld = std::make_shared<World>();
            GameWorld->IsLoading = false;
            GameWorld->AddCore( Core );
            Root = GameWorld->CreateEntity( "Root" );
            Root->AddComponent<Transform>();
            Arm = GameWorld->CreateEntity( "Arm" );
            Transform& arm = Arm->AddComponent<Transform>();
            arm.SetParent( Root->GetComponent<Transform>() );
            arm.SetPosition( Vector3( 0.f, 5.f, 0.f ) );   // bind pose
            Root->AddComponent<Animator>().UseClips( std::make_shared<std::vector<Moonlight::AnimationClip>>( std::move( InClips ) ) );
        }

        Animator& Anim()
        {
            return Root->GetComponent<Animator>();
        }

        void Start()
        {
            GameWorld->Simulate();
            GameWorld->Start();
        }

        void Run( float InSeconds, float InStep = 1.f / 60.f )
        {
            const int steps = static_cast<int>( std::lround( InSeconds / InStep ) );
            for( int i = 0; i < steps; ++i )
            {
                Core.Advance( InStep );
            }
        }

        Vector3 ArmPosition()
        {
            return Arm->GetComponent<Transform>().GetPosition();
        }
    };

    struct EventLog
        : public EventReceiver
    {
        EventLog()
        {
            EventManager::GetInstance().RegisterReceiver( this, { AnimationEvent::GetEventId() } );
        }

        bool OnEvent( const BaseEvent& InEvent ) override
        {
            Names.push_back( static_cast<const AnimationEvent&>( InEvent ).Name );
            return false;
        }

        std::vector<std::string> Names;
    };
}

using namespace AnimationTest;

TEST_CASE( "Animation: clips sample with interpolation and keep unkeyed properties" )
{
    Moonlight::AnimationChannel channel;
    channel.PositionTimes = { 0.f, 1.f, 3.f };
    channel.Positions = { Vector3( 0.f, 0.f, 0.f ), Vector3( 2.f, 0.f, 0.f ), Vector3( 2.f, 4.f, 0.f ) };
    channel.RotationTimes = { 0.f, 2.f };
    channel.Rotations = { Quaternion( 0.f, 0.f, 0.f, 1.f ), Quaternion( 0.f, std::sin( 0.785398f ), 0.f, std::cos( 0.785398f ) ) };   // 0 -> 90 deg about Y

    Vector3 position;
    Quaternion rotation;
    Vector3 scale( 7.f, 7.f, 7.f );
    Moonlight::AnimationClip::Sample( channel, 0.5f, position, rotation, scale );
    CHECK( position.x == doctest::Approx( 1.f ) );
    CHECK( scale.x == doctest::Approx( 7.f ) );   // no scale keys: untouched
    Moonlight::AnimationClip::Sample( channel, 2.f, position, rotation, scale );
    CHECK( position.y == doctest::Approx( 2.f ) );
    // Halfway through the rotation keys: 45 degrees about Y.
    Moonlight::AnimationClip::Sample( channel, 1.f, position, rotation, scale );
    CHECK( rotation.y == doctest::Approx( std::sin( 0.392699f ) ).epsilon( 0.001 ) );
    // Clamped past the ends.
    Moonlight::AnimationClip::Sample( channel, 10.f, position, rotation, scale );
    CHECK( position.y == doctest::Approx( 4.f ) );
    Moonlight::AnimationClip::Sample( channel, -1.f, position, rotation, scale );
    CHECK( position.x == doctest::Approx( 0.f ) );
}

TEST_CASE( "Animation: an Animator plays, loops and drives node Transforms" )
{
    Rig rig( { Slide( "Walk", "Arm", 0.f, 10.f, 1.f ) } );
    rig.Start();
    rig.Run( 0.5f );
    CHECK( rig.Anim().GetCurrentState() == "Walk" );
    CHECK( rig.ArmPosition().x == doctest::Approx( 5.f ).epsilon( 0.02 ) );
    CHECK( rig.ArmPosition().y == doctest::Approx( 0.f ) );   // keyed: the clip owns position
    rig.Run( 0.75f );   // 1.25 s: looped
    CHECK( rig.ArmPosition().x == doctest::Approx( 2.5f ).epsilon( 0.02 ) );
    CHECK( rig.Anim().GetNormalizedTime() == doctest::Approx( 1.25f ).epsilon( 0.02 ) );

    rig.Anim().Speed = 0.f;
    const float frozen = rig.ArmPosition().x;
    rig.Run( 0.5f );
    CHECK( rig.ArmPosition().x == doctest::Approx( frozen ) );
}

TEST_CASE( "Animation: transitions on parameters, triggers and exit times, with cross-fades" )
{
    Rig rig( { Slide( "Idle", "Arm", 0.f, 0.f, 1.f ), Slide( "Run", "Arm", 10.f, 10.f, 1.f ), Slide( "Jump", "Arm", 20.f, 20.f, 0.5f ) } );
    Animator& animator = rig.Anim();
    animator.States = { { "Idle", "Idle" }, { "Run", "Run" }, { "Jump", "Jump" } };
    animator.States[2].Loop = false;
    animator.Parameters = { { "Speed", AnimatorParameterType::Float, 0.f }, { "Jump", AnimatorParameterType::Trigger, 0.f } };
    AnimatorTransition toRun{ "Idle", "Run", "Speed", AnimatorCondition::Greater, 0.5f, 0.5f };
    AnimatorTransition toIdle{ "Run", "Idle", "Speed", AnimatorCondition::Less, 0.5f, 0.f };
    AnimatorTransition toJump{ "", "Jump", "Jump", AnimatorCondition::Trigger, 0.f, 0.f };
    AnimatorTransition backToIdle{ "Jump", "Idle", "", AnimatorCondition::Always, 0.f, 0.f, 1.f };   // when the jump finishes
    animator.Transitions = { toRun, toIdle, toJump, backToIdle };
    rig.Start();

    rig.Run( 0.2f );
    CHECK( animator.GetCurrentState() == "Idle" );
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.f ) );

    animator.SetFloat( "Speed", 2.f );
    rig.Run( 0.25f );   // halfway through a 0.5 s cross-fade (smoothstep: 0.5 at the middle)
    CHECK( animator.GetCurrentState() == "Run" );
    CHECK( animator.IsInTransition() );
    CHECK( rig.ArmPosition().x == doctest::Approx( 5.f ).epsilon( 0.1 ) );
    rig.Run( 0.5f );
    CHECK_FALSE( animator.IsInTransition() );
    CHECK( rig.ArmPosition().x == doctest::Approx( 10.f ) );

    animator.SetTrigger( "Jump" );
    rig.Run( 1.f / 60.f );
    CHECK( animator.GetCurrentState() == "Jump" );
    CHECK( animator.GetFloat( "Jump" ) == 0.f );   // consumed
    CHECK( rig.ArmPosition().x == doctest::Approx( 20.f ) );
    rig.Run( 0.6f );   // past the jump's end: back to idle via exit time, then on to run (Speed is still 2)
    CHECK( animator.GetCurrentState() != "Jump" );

    animator.SetFloat( "Speed", 0.f );
    rig.Run( 1.f );
    CHECK( animator.GetCurrentState() == "Idle" );
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.f ) );
}

TEST_CASE( "Animation: 1D blends, Play() with fades and event markers" )
{
    Rig rig( { Slide( "Slow", "Arm", 0.f, 0.f, 1.f ), Slide( "Fast", "Arm", 8.f, 8.f, 1.f ), Slide( "Wave", "Arm", 0.f, 1.f, 1.f ) } );
    Animator& animator = rig.Anim();
    AnimatorState locomotion;
    locomotion.Name = "Move";
    locomotion.BlendParameter = "Speed";
    locomotion.BlendClips = { { "Fast", 4.f }, { "Slow", 0.f } };   // unsorted on purpose
    AnimatorState wave;
    wave.Name = "Wave";
    wave.Clip = "Wave";
    animator.States = { locomotion, wave };
    animator.Events = { { "Wave", 0.5f, "Halfway" } };
    rig.Start();

    animator.SetFloat( "Speed", 1.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 2.f ) );    // a quarter of the way from Slow to Fast
    animator.SetFloat( "Speed", 10.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 8.f ) );    // clamped to the last clip

    EventLog log;
    animator.Play( "Wave" );
    rig.Run( 2.25f );   // two and a quarter loops: the marker fires twice
    CHECK( animator.GetCurrentState() == "Wave" );
    REQUIRE( log.Names.size() == 2 );
    CHECK( log.Names[0] == "Halfway" );

    // Playing a clip that isn't a state works too.
    animator.Play( "Fast", 0.f );
    rig.Run( 1.f / 60.f );
    CHECK( animator.GetCurrentState() == "Fast" );
    CHECK( rig.ArmPosition().x == doctest::Approx( 8.f ) );
}
