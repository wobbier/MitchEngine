#pragma once
#include "ECS/Component.h"

class KillcamPlayer;

// The killcam a world plays, as a world singleton (ME-323 phase 3): C#'s Net.Killcam reads its
// script world's. The engine's KillcamPlayer sets it in the live world (whose session receives
// killcams) and in the replay world it plays one into, both pointing at the player; InReplayWorld
// tells the two apart (scripts that run in a replay world, [ReplayWorld], present there only).
class SingletonKillcam final
    : public Component<SingletonKillcam>
{
public:
    SingletonKillcam()
        : Component( "SingletonKillcam" )
    {
    }

    KillcamPlayer* Player = nullptr;
    bool InReplayWorld = false;
    // ME-343: where the game puts its eye above a pawn's origin (C#'s Net.Killcam.EyeHeight, set on the live
    // world): the killer's eye in its killcams. < 0: the engine's guess (KillcamPlayer::Settings::EyeHeight).
    float EyeHeight = -1.f;
};
