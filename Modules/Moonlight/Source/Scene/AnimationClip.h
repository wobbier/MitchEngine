#pragma once
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <string>
#include <vector>

namespace Moonlight
{
    // Keyframes for one node (bone), each property with its own key times (seconds).
    struct AnimationChannel
    {
        std::string NodeName;
        std::vector<float> PositionTimes;
        std::vector<Vector3> Positions;
        std::vector<float> RotationTimes;
        std::vector<Quaternion> Rotations;
        std::vector<float> ScaleTimes;
        std::vector<Vector3> Scales;
    };

    // A flipbook key (LOOK-S4): from Time on, the Animator's SpriteRenderer shows Sprite of Sheet.
    struct SpriteKey
    {
        float Time = 0.f;
        std::string Sheet;
        std::string Sprite;
    };

    // A named animation: one channel per animated node, sampled with linear / spherical interpolation,
    // and/or sprite keys (a flipbook from a .spriteanim).
    struct AnimationClip
    {
        std::string Name;
        float Duration = 0.f;   // seconds
        std::vector<AnimationChannel> Channels;
        std::vector<SpriteKey> Sprites;
        // The loop setting for the state made for this clip when an Animator has no States.
        bool Loop = true;

        // The sprite key in effect at InTime seconds (the last at or before it; null without keys).
        const SpriteKey* SampleSprite( float InTime ) const;

        // Samples a channel at InTime seconds (clamped to the keys). Properties without keys keep
        // the values passed in (the node's bind pose).
        static void Sample( const AnimationChannel& InChannel, float InTime, Vector3& InOutPosition, Quaternion& InOutRotation, Vector3& InOutScale );
        // Index of the channel animating InNodeName, or -1.
        int FindChannel( const std::string& InNodeName ) const;
    };
}
