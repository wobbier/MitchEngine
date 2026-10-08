#include "PCH.h"

#include "Scene.h"
#include "SceneSerializer.h"
#include "Engine/Engine.h"
#include "CLog.h"


Scene::Scene( const std::string& SceneFilePath )
    : FilePath( std::move( SceneFilePath ) )
{
    CurrentLevel = File( FilePath );
}


void Scene::UnLoad()
{
    GameWorld = nullptr;
}


bool Scene::Load( SharedPtr<World> InWorld )
{
    OPTICK_EVENT( "Scene::Load" );
    GameWorld = InWorld;

    if( CurrentLevel.FilePath.GetLocalPath().size() > 0 )
    {
        OPTICK_EVENT( "Scene::Load::ReadFile" );
        CurrentLevel.Read();
    }

    if( CurrentLevel.Data.empty() )
    {
        return false;
    }

    json level;
    {
        OPTICK_EVENT( "Scene::Load::JSONParse" );
        level = json::parse( CurrentLevel.Data, nullptr, false );
    }
    if( level.is_discarded() )
    {
        YIKES( "Scene file is not valid JSON: " + FilePath.GetLocalPathString() );
        return false;
    }

    // No sync points while the scene is half built.
    GameWorld->IsLoading = true;
    {
        OPTICK_EVENT( "Scene::Load::Entities" );
        SceneSerializer::LoadOptions options;
        options.LoadCores = true;
        SceneSerializer::Deserialize( *GameWorld, level, options );
    }
    GameWorld->IsLoading = false;
    return true;
}


bool Scene::IsNewScene()
{
    return FilePath.GetLocalPath().empty();
}


void Scene::Save( const std::string& fileName, Transform* root )
{
#if USING( ME_EDITOR )
    FilePath = Path( fileName );
    SaveCopy( fileName, root );
#endif
}


void Scene::SaveCopy( const std::string& fileName, Transform* root )
{
#if USING( ME_EDITOR )
    json world = SceneSerializer::SerializeWorld( *GetEngine().GetWorld().lock(), root );
    File worldFile{ Path( fileName ) };
    worldFile.Write( world.dump( 4 ) );
#endif
}
