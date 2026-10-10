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


namespace AnimationTest
{
    // A clip holding node InNode at InPosition.
    Moonlight::AnimationChannel HoldChannel( const std::string& InNode, const Vector3& InPosition, float InDuration )
    {
        Moonlight::AnimationChannel channel;
        channel.NodeName = InNode;
        channel.PositionTimes = { 0.f, InDuration };
        channel.Positions = { InPosition, InPosition };
        return channel;
    }

    Moonlight::AnimationClip Hold( const std::string& InName, const std::vector<std::string>& InNodes, const Vector3& InPosition, float InDuration = 1.f )
    {
        Moonlight::AnimationClip clip;
        clip.Name = InName;
        clip.Duration = InDuration;
        for( const std::string& node : InNodes )
        {
            clip.Channels.push_back( HoldChannel( node, InPosition, InDuration ) );
        }
        return clip;
    }
}


TEST_CASE( "Animation: 2D blends weight the clips around the two parameters" )
{
    Rig rig( { Hold( "Idle", { "Arm" }, Vector3( 0.f, 0.f, 0.f ) ), Hold( "Right", { "Arm" }, Vector3( 10.f, 0.f, 0.f ) ), Hold( "Left", { "Arm" }, Vector3( -10.f, 0.f, 0.f ) ),
        Hold( "Forward", { "Arm" }, Vector3( 0.f, 0.f, 10.f ) ), Hold( "Back", { "Arm" }, Vector3( 0.f, 0.f, -10.f ) ) } );
    Animator& animator = rig.Anim();
    AnimatorState move;
    move.Name = "Move";
    move.BlendParameter = "X";
    move.BlendParameterY = "Y";
    move.BlendClips = { { "Idle", 0.f, 0.f }, { "Right", 1.f, 0.f }, { "Left", -1.f, 0.f }, { "Forward", 0.f, 1.f }, { "Back", 0.f, -1.f } };
    animator.States = { move };
    rig.Start();

    animator.SetFloat( "X", 0.f );
    animator.SetFloat( "Y", 0.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.f ).epsilon( 0.01 ) );
    CHECK( rig.ArmPosition().z == doctest::Approx( 0.f ).epsilon( 0.01 ) );

    animator.SetFloat( "X", 1.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 10.f ).epsilon( 0.01 ) );   // exactly on a sample

    animator.SetFloat( "X", 0.f );
    animator.SetFloat( "Y", 0.5f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().z == doctest::Approx( 5.f ).epsilon( 0.02 ) );   // halfway to Forward
    CHECK( std::abs( rig.ArmPosition().x ) < 0.01f );

    animator.SetFloat( "X", 0.5f );
    animator.SetFloat( "Y", 0.5f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x > 1.f );   // diagonal: both Right and Forward contribute
    CHECK( rig.ArmPosition().z > 1.f );
    CHECK( rig.ArmPosition().x == doctest::Approx( rig.ArmPosition().z ).epsilon( 0.01 ) );

    animator.SetFloat( "X", 3.f );   // far outside: the nearest edge wins
    animator.SetFloat( "Y", 0.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 10.f ).epsilon( 0.01 ) );
}


TEST_CASE( "Animation: layers override masked bones by weight" )
{
    Rig rig( { Hold( "Walk", { "Arm", "Hand", "Leg" }, Vector3( 1.f, 0.f, 0.f ) ), Hold( "Wave", { "Arm", "Hand", "Leg" }, Vector3( 5.f, 0.f, 0.f ) ) } );
    EntityHandle hand = rig.GameWorld->CreateEntity( "Hand" );
    hand->AddComponent<Transform>().SetParent( rig.Arm->GetComponent<Transform>() );
    EntityHandle leg = rig.GameWorld->CreateEntity( "Leg" );
    leg->AddComponent<Transform>().SetParent( rig.Root->GetComponent<Transform>() );

    Animator& animator = rig.Anim();
    AnimatorState walk;
    walk.Name = "Walk";
    walk.Clip = "Walk";
    AnimatorState wave;
    wave.Name = "Wave";
    wave.Clip = "Wave";
    wave.Layer = "Upper";
    animator.States = { walk, wave };
    AnimatorLayer upper;
    upper.Name = "Upper";
    upper.DefaultState = "Wave";
    upper.Mask = { "Arm" };   // the arm and everything below it
    animator.Layers = { upper };
    rig.Start();
    rig.Run( 0.1f );

    auto x = []( EntityHandle InEntity ) { return InEntity->GetComponent<Transform>().GetPosition().x; };
    CHECK( animator.GetCurrentState() == "Walk" );
    CHECK( animator.GetCurrentState( "Upper" ) == "Wave" );
    CHECK( x( rig.Arm ) == doctest::Approx( 5.f ) );
    CHECK( x( hand ) == doctest::Approx( 5.f ) );
    CHECK( x( leg ) == doctest::Approx( 1.f ) );   // outside the mask: the base layer

    animator.SetLayerWeight( "Upper", 0.5f );
    rig.Run( 0.1f );
    CHECK( x( rig.Arm ) == doctest::Approx( 3.f ) );
    CHECK( animator.GetLayerWeight( "Upper" ) == doctest::Approx( 0.5f ) );

    animator.SetLayerWeight( "Upper", 0.f );
    rig.Run( 0.1f );
    CHECK( x( hand ) == doctest::Approx( 1.f ) );
}


TEST_CASE( "Animation: root motion moves the entity instead of the root bone" )
{
    // The hips slide 2 m along X each second, looping.
    Moonlight::AnimationClip walk;
    walk.Name = "Walk";
    walk.Duration = 1.f;
    Moonlight::AnimationChannel hips;
    hips.NodeName = "Hips";
    hips.PositionTimes = { 0.f, 1.f };
    hips.Positions = { Vector3( 0.f, 1.f, 0.f ), Vector3( 2.f, 1.f, 0.f ) };
    walk.Channels.push_back( hips );
    Rig rig( { walk } );
    EntityHandle hipsEntity = rig.GameWorld->CreateEntity( "Hips" );
    hipsEntity->AddComponent<Transform>().SetParent( rig.Root->GetComponent<Transform>() );
    hipsEntity->GetComponent<Transform>().SetPosition( Vector3( 0.f, 1.f, 0.f ) );

    Animator& animator = rig.Anim();
    animator.ApplyRootMotion = true;
    animator.RootBone = "Hips";
    rig.Start();
    rig.Run( 1.5f );   // crosses the loop point

    const Vector3 root = rig.Root->GetComponent<Transform>().GetWorldPosition();
    CHECK( root.x == doctest::Approx( 3.f ).epsilon( 0.03 ) );   // 1.5 s at 2 m/s, continuous over the wrap
    CHECK( root.y == doctest::Approx( 0.f ) );
    const Vector3 bone = hipsEntity->GetComponent<Transform>().GetPosition();
    CHECK( std::abs( bone.x ) < 0.01f );   // the bone stays above the entity
    CHECK( bone.y == doctest::Approx( 1.f ) );   // vertical motion stays in the bone
    CHECK( animator.GetRootMotion().x == doctest::Approx( 2.f / 60.f ).epsilon( 0.05 ) );

    // Off: the bone travels and the entity stays.
    animator.ApplyRootMotion = false;
    const float before = rig.Root->GetComponent<Transform>().GetWorldPosition().x;
    rig.Run( 0.25f );
    CHECK( rig.Root->GetComponent<Transform>().GetWorldPosition().x == doctest::Approx( before ) );
    CHECK( animator.GetRootMotion().x > 0.f );   // still reported
}


TEST_CASE( "Animation: additive layers add their motion on top of the layers below" )
{
    // The additive clip's first frame is its reference: its motion from there is what's added.
    Moonlight::AnimationClip turn;
    turn.Name = "Turn";
    turn.Duration = 1.f;
    Moonlight::AnimationChannel turnChannel = HoldChannel( "Arm", Vector3( 1.f, 0.f, 0.f ), 1.f );
    const Quaternion yaw( 0.f, std::sin( 0.785398f ), 0.f, std::cos( 0.785398f ) );    // 90 deg about Y
    turnChannel.RotationTimes = { 0.f };
    turnChannel.Rotations = { yaw };
    turn.Channels.push_back( turnChannel );
    Moonlight::AnimationClip nod;
    nod.Name = "Nod";
    nod.Duration = 1.f;
    Moonlight::AnimationChannel nodChannel;
    nodChannel.NodeName = "Arm";
    nodChannel.PositionTimes = { 0.f, 1.f };
    nodChannel.Positions = { Vector3( 3.f, 0.f, 0.f ), Vector3( 5.f, 0.f, 0.f ) };     // +2 along X over the clip
    const Quaternion pitch( std::sin( 0.785398f ), 0.f, 0.f, std::cos( 0.785398f ) );  // 90 deg about X
    nodChannel.RotationTimes = { 0.f, 1.f };
    nodChannel.Rotations = { Quaternion( 0.f, 0.f, 0.f, 1.f ), pitch };
    nod.Channels.push_back( nodChannel );

    Rig rig( { turn, nod } );
    Animator& animator = rig.Anim();
    AnimatorState base;
    base.Name = "Turn";
    base.Clip = "Turn";
    AnimatorState nodState;
    nodState.Name = "Nod";
    nodState.Clip = "Nod";
    nodState.Loop = false;
    nodState.Layer = "Breathing";
    animator.States = { base, nodState };
    AnimatorLayer layer;
    layer.Name = "Breathing";
    layer.DefaultState = "Nod";
    layer.Blending = AnimatorLayerBlending::Additive;
    animator.Layers = { layer };
    rig.Start();

    // Halfway through: +1 along X added to the base's 1, not replaced by the clip's 4.
    rig.Run( 0.5f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 2.f ).epsilon( 0.02 ) );
    // At the end: the full pitch applied in the bone's frame, after the base's yaw.
    rig.Run( 1.f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 3.f ).epsilon( 0.01 ) );
    const Quaternion expected = yaw * pitch;
    const Quaternion rotation = rig.Arm->GetComponent<Transform>().GetRotation();
    CHECK( std::abs( rotation.x * expected.x + rotation.y * expected.y + rotation.z * expected.z + rotation.w * expected.w ) == doctest::Approx( 1.f ).epsilon( 0.001 ) );

    // Weight scales the added motion.
    animator.SetLayerWeight( "Breathing", 0.5f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 2.f ).epsilon( 0.01 ) );
    animator.SetLayerWeight( "Breathing", 0.f );
    rig.Run( 0.1f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 1.f ).epsilon( 0.01 ) );
}

TEST_CASE( "Animation: editor previews pose in edit mode and always give the authored pose back" )
{
    Rig rig( { Slide( "Lean", "Arm", 0.f, 10.f, 10.f ) } );
    Animator& animator = rig.Anim();
    rig.GameWorld->Simulate();

    // Held at a time, then playing.
    animator.StartPreview( "Lean" );
    animator.PreviewPlaying = false;
    animator.PreviewTime = 3.f;
    rig.Core.UpdatePreviews( 1.f / 60.f );
    CHECK( animator.IsPreviewing() );
    CHECK( rig.ArmPosition().x == doctest::Approx( 3.f ) );
    CHECK( animator.GetPreviewDuration() == doctest::Approx( 10.f ) );
    animator.PreviewPlaying = true;
    rig.Core.UpdatePreviews( 0.5f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 3.5f ) );

    // Saving writes the authored pose; the preview poses again on its next update.
    rig.Core.RestorePreviewPoses();
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.f ) );
    CHECK( rig.ArmPosition().y == doctest::Approx( 5.f ) );
    rig.Core.UpdatePreviews( 0.f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 3.5f ) );

    // Stopping restores the bind pose.
    animator.StopPreview();
    rig.Core.UpdatePreviews( 1.f / 60.f );
    CHECK_FALSE( animator.IsPreviewing() );
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.f ) );
    CHECK( rig.ArmPosition().y == doctest::Approx( 5.f ) );

    // A preview nobody inspects ends on its own (selecting something else).
    animator.StartPreview( "Lean" );
    rig.Core.UpdatePreviews( 1.f / 60.f );
    CHECK( rig.ArmPosition().y == doctest::Approx( 0.f ) );
    for( int i = 0; i <= AnimationCore::kPreviewIdleUpdates; ++i )
    {
        rig.Core.UpdatePreviews( 1.f / 60.f );
    }
    CHECK_FALSE( animator.IsPreviewing() );
    CHECK( rig.ArmPosition().y == doctest::Approx( 5.f ) );

    // Entering play ends previews first: play binds to the authored pose.
    animator.StartPreview( "Lean" );
    animator.PreviewTime = 6.f;
    rig.Core.UpdatePreviews( 0.f );
    CHECK( rig.ArmPosition().x > 5.f );
    rig.GameWorld->Start();
    CHECK_FALSE( animator.IsPreviewing() );
    CHECK( rig.ArmPosition().y == doctest::Approx( 5.f ) );
    rig.Run( 0.5f );
    CHECK( rig.ArmPosition().x == doctest::Approx( 0.5f ).epsilon( 0.05 ) );   // playing from the start
}


TEST_CASE( "Animation: root rotation turns the entity and holds the root's heading" )
{
    // The root bone turns 90 degrees about Y each second (looping), without travelling.
    Moonlight::AnimationClip turnClip;
    turnClip.Name = "Turn";
    turnClip.Duration = 1.f;
    Moonlight::AnimationChannel channel;
    channel.NodeName = "Arm";
    channel.PositionTimes = { 0.f };
    channel.Positions = { Vector3( 0.f, 5.f, 0.f ) };
    channel.RotationTimes = { 0.f, 0.5f, 1.f };
    channel.Rotations = { Quaternion( 0.f, 0.f, 0.f, 1.f ), Quaternion::AngleAxis( 0.785398f, Vector3( 0.f, 1.f, 0.f ) ), Quaternion::AngleAxis( 1.570796f, Vector3( 0.f, 1.f, 0.f ) ) };
    turnClip.Channels.push_back( channel );

    auto yawDot = []( const Quaternion& InA, float InRadians ) {
        const Quaternion expected = Quaternion::AngleAxis( InRadians, Vector3( 0.f, 1.f, 0.f ) );
        return std::abs( InA.x * expected.x + InA.y * expected.y + InA.z * expected.z + InA.w * expected.w );
    };

    {
        Rig rig( { turnClip } );
        rig.Anim().ApplyRootMotion = true;
        rig.Start();
        rig.Run( 0.5f );
        // The entity turned 45 degrees; the bone keeps its bind heading above it.
        CHECK( yawDot( rig.Root->GetComponent<Transform>().GetWorldRotation(), 0.785398f ) == doctest::Approx( 1.f ).epsilon( 0.002 ) );
        CHECK( yawDot( rig.Arm->GetComponent<Transform>().GetRotation(), 0.f ) == doctest::Approx( 1.f ).epsilon( 0.002 ) );
        CHECK( rig.Anim().GetRootTurn() == doctest::Approx( 90.f / 60.f ).epsilon( 0.05 ) );
        // Across the loop's wrap the turn keeps accumulating: 135 degrees after 1.5 s.
        rig.Run( 1.f );
        CHECK( yawDot( rig.Root->GetComponent<Transform>().GetWorldRotation(), 2.356194f ) == doctest::Approx( 1.f ).epsilon( 0.002 ) );
    }
    {
        // RootRotation off: the bone turns, the entity doesn't.
        Rig rig( { turnClip } );
        rig.Anim().ApplyRootMotion = true;
        rig.Anim().RootRotation = false;
        rig.Start();
        rig.Run( 0.5f );
        CHECK( yawDot( rig.Root->GetComponent<Transform>().GetWorldRotation(), 0.f ) == doctest::Approx( 1.f ).epsilon( 0.002 ) );
        CHECK( yawDot( rig.Arm->GetComponent<Transform>().GetRotation(), 0.785398f ) == doctest::Approx( 1.f ).epsilon( 0.002 ) );
        CHECK( rig.Anim().GetRootTurn() == doctest::Approx( 90.f / 60.f ).epsilon( 0.05 ) );   // still reported
    }
}
