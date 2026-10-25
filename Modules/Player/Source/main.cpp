// The Player (SHIP-4, Engine/Docs/Projects.md): the engine with no C++ game module, so it runs
// any C#-only project. `Player --project <folder>` (or from inside the folder) boots the
// project's StartupScene, and its scripts do the rest. Projects with C++ game code build their
// own Game_EntryPoint instead.
#include "Game.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/SceneEvents.h"


class PlayerGame final : public Game
{
public:
    PlayerGame( int argc, char** argv )
        : Game( argc, argv )
    {
    }

    void OnInitialize() override
    {
        // An empty scene until the startup scene loads (what every game module does).
        NewSceneEvent evt;
        evt.Fire();
        GetEngine().GetWorld().lock()->Start();
        GetEngine().GetWorld().lock()->Simulate();
        GetEngine().GetWorld().lock()->Start();
    }

    void OnStart() override {}
    void OnUpdate( const UpdateContext& ) override {}
    void OnEnd() override {}
    void PreRender() override {}
    void PostRender() override {}
};


ME_APPLICATION_MAIN( PlayerGame )
