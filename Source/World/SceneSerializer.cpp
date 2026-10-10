#include "PCH.h"
#include "SceneSerializer.h"
#include <algorithm>
#include "Engine/World.h"
#include "ECS/Core.h"
#include "Components/Transform.h"
#include "File.h"
#include "Path.h"
#include "CLog.h"
#include "optick.h"
#include "Resource/AssetDatabase.h"
#include "Utils/GUID.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace SceneSerializer
{
    namespace
    {
        bool IsLatest( const json& InData )
        {
            return InData.is_object() && InData.contains( "Entities" ) && InData.value( "Version", 0 ) == kVersion;
        }


        void ConvertV1Object( const json& InObject, uint64_t InParentGUID, json& OutEntities )
        {
            if( !InObject.is_object() )
            {
                return;
            }

            const uint64_t guid = World::GenerateGUID();
            json entity;
            entity["GUID"] = GUIDToString( guid );
            entity["Name"] = InObject.value( "Name", std::string() );
            if( InParentGUID != 0 )
            {
                entity["Parent"] = GUIDToString( InParentGUID );
            }
            if( InObject.contains( "DestroyOnLoad" ) && InObject["DestroyOnLoad"].is_boolean() )
            {
                entity["DestroyOnLoad"] = InObject["DestroyOnLoad"];
            }

            json components = json::array();
            if( InObject.contains( "Components" ) && InObject["Components"].is_array() )
            {
                for( const json& component : InObject["Components"] )
                {
                    if( component.is_object() )
                    {
                        components.push_back( component );
                    }
                }
            }
            entity["Components"] = std::move( components );
            OutEntities.push_back( std::move( entity ) );

            if( InObject.contains( "Children" ) )
            {
                const json& children = InObject["Children"];
                if( children.is_array() )
                {
                    for( const json& child : children )
                    {
                        ConvertV1Object( child, guid, OutEntities );
                    }
                }
                else if( children.is_object() )
                {
                    // Files written by the old editor duplicate bug stored one child as an object.
                    ConvertV1Object( children, guid, OutEntities );
                }
            }
        }


        void CollectDepthFirst( Entity* InEntity, std::vector<Entity*>& OutEntities, std::unordered_set<uint64_t>& OutGUIDs )
        {
            if( !InEntity || !InEntity->IsValid() || OutGUIDs.count( InEntity->GetGUID() ) )
            {
                return;
            }
            OutEntities.push_back( InEntity );
            OutGUIDs.insert( InEntity->GetGUID() );
            if( Transform* transform = InEntity->TryGetComponent<Transform>() )
            {
                for( Transform* child : transform->GetChildren() )
                {
                    CollectDepthFirst( child->Parent.Get(), OutEntities, OutGUIDs );
                }
            }
        }


        json SerializeEntity( World& InWorld, Entity& InEntity, const std::unordered_set<uint64_t>& InIncluded )
        {
            json out;
            out["GUID"] = GUIDToString( InEntity.GetGUID() );
            out["Name"] = InEntity.GetName();

            Transform* transform = InEntity.TryGetComponent<Transform>();
            if( transform && transform->GetParentTransform() )
            {
                if( Entity* parent = transform->GetParentTransform()->Parent.Get() )
                {
                    if( InIncluded.count( parent->GetGUID() ) )
                    {
                        out["Parent"] = GUIDToString( parent->GetGUID() );
                    }
                }
            }

            if( !InEntity.IsActiveSelf() )
            {
                out["Active"] = false;
            }
            out["DestroyOnLoad"] = InEntity.GetDestroyOnLoad();
            if( InEntity.GetLayer() != 0 )
            {
                out["Layer"] = InEntity.GetLayer();
            }

            if( const World::EntityRecord* record = InWorld.GetRecord( InEntity.GetId() ) )
            {
                if( !record->PrefabAsset.empty() )
                {
                    out["Prefab"] = { { "Asset", record->PrefabAsset }, { "Source", GUIDToString( record->PrefabSource ) } };
                }
            }

            // Transform first, the rest in component-type order.
            json components = json::array();
            if( transform )
            {
                json transformJson;
                transform->Serialize( transformJson );
                components.push_back( std::move( transformJson ) );
            }
            for( BaseComponent* component : InEntity.GetAllComponents() )
            {
                if( component == transform )
                {
                    continue;
                }
                json componentJson;
                component->Serialize( componentJson );
                components.push_back( std::move( componentJson ) );
            }
            out["Components"] = std::move( components );
            return out;
        }


        struct PrefabCacheData
        {
            std::mutex Mutex;
            std::unordered_map<std::string, std::shared_ptr<const json>> Entries;
        };

        PrefabCacheData& GetPrefabCache()
        {
            static PrefabCacheData cache;
            return cache;
        }
    }


    Vector3 ReadVector3( const json& InValue, const Vector3& InFallback )
    {
        if( InValue.is_array() && InValue.size() == 3 && InValue[0].is_number() && InValue[1].is_number() && InValue[2].is_number() )
        {
            return Vector3( InValue[0].get<float>(), InValue[1].get<float>(), InValue[2].get<float>() );
        }
        return InFallback;
    }


    // Retired component formats -> their replacements (one old component may become several).
    std::vector<json> UpgradeComponent( const json& InComponent, const json& InEntity )
    {
        const std::string type = InComponent.value( "Type", std::string() );
        if( type == "DirectionalLight" )
        {
            // Pre-Wave-3 sun component: now a directional Light (direction comes from the transform).
            json light = { { "Type", "Light" }, { "LightType", "Directional" }, { "Intensity", 3.0 }, { "CastShadows", true } };
            if( InComponent.contains( "Diffuse" ) && InComponent["Diffuse"].is_array() )
            {
                light["Color"] = InComponent["Diffuse"];
            }
            return { light };
        }
        if( type == "Rigidbody" && InComponent.contains( "ColliderType" ) )
        {
            // Bullet-era Rigidbody: the shape lived on the body and ignored the Transform's scale
            // (Scale = box half extents, spheres had radius 1). Now: Rigidbody + a collider sized
            // in the entity's local space.
            Vector3 transformScale( 1.f, 1.f, 1.f );
            for( const json& component : InEntity.value( "Components", json::array() ) )
            {
                if( component.is_object() && component.value( "Type", std::string() ) == "Transform" )
                {
                    transformScale = ReadVector3( component.value( "Scale", json() ), transformScale );
                }
            }
            auto safeDivide = []( float a, float b ) { return std::abs( b ) > 1e-6f ? a / std::abs( b ) : a; };
            const float mass = InComponent.value( "Mass", 10.f );
            json body = { { "Type", "Rigidbody" }, { "BodyType", mass > 0.f ? "Dynamic" : "Static" }, { "Mass", std::max( mass, 0.f ) } };
            json collider;
            if( InComponent.value( "ColliderType", std::string( "Box" ) ) == "Sphere" )
            {
                const float largest = std::max( { std::abs( transformScale.x ), std::abs( transformScale.y ), std::abs( transformScale.z ) } );
                collider = { { "Type", "SphereCollider" }, { "Radius", safeDivide( 1.f, largest ) } };
            }
            else
            {
                const Vector3 half = ReadVector3( InComponent.value( "Scale", json() ), Vector3( 1.f, 1.f, 1.f ) );
                collider = { { "Type", "BoxCollider" }, { "Size", { safeDivide( 2.f * half.x, transformScale.x ), safeDivide( 2.f * half.y, transformScale.y ), safeDivide( 2.f * half.z, transformScale.z ) } } };
            }
            if( mass <= 0.f )
            {
                return { collider };   // static: a collider alone makes a static body
            }
            return { body, collider };
        }
        if( type == "CharacterController" && InComponent.contains( "JumpForce" ) )
        {
            // Bullet-era controller fields don't carry over; defaults fit a human-sized capsule.
            return { json{ { "Type", "CharacterController" } } };
        }
        return { InComponent };
    }


    std::string GUIDToString( uint64_t InGUID )
    {
        char buffer[17];
        std::snprintf( buffer, sizeof( buffer ), "%016llx", static_cast<unsigned long long>( InGUID ) );
        return buffer;
    }


    uint64_t GUIDFromJson( const json& InValue )
    {
        if( InValue.is_string() )
        {
            const std::string& text = InValue.get_ref<const std::string&>();
            return text.empty() ? 0 : std::strtoull( text.c_str(), nullptr, 16 );
        }
        if( InValue.is_number_unsigned() || InValue.is_number_integer() )
        {
            return InValue.get<uint64_t>();
        }
        return 0;
    }


    json MigrateToLatest( const json& InData )
    {
        if( IsLatest( InData ) )
        {
            return InData;
        }

        json out;
        out["Version"] = kVersion;
        out["Cores"] = json::array();
        out["Entities"] = json::array();
        if( !InData.is_object() )
        {
            return out;
        }

        if( InData.contains( "Cores" ) && InData["Cores"].is_array() )
        {
            out["Cores"] = InData["Cores"];
        }

        if( InData.contains( "Entities" ) )
        {
            // A v2 file written by a newer/older minor revision: keep the entities as they are.
            out["Entities"] = InData["Entities"];
        }
        else if( InData.contains( "Scene" ) && InData["Scene"].is_array() )
        {
            for( const json& object : InData["Scene"] )
            {
                ConvertV1Object( object, 0, out["Entities"] );
            }
        }
        else if( InData.contains( "Components" ) )
        {
            // v1 prefab: a single root object.
            ConvertV1Object( InData, 0, out["Entities"] );
        }
        return out;
    }


    json SerializeEntities( World& InWorld, const std::vector<Entity*>& InRoots )
    {
        std::vector<Entity*> ordered;
        std::unordered_set<uint64_t> included;
        for( Entity* root : InRoots )
        {
            CollectDepthFirst( root, ordered, included );
        }

        json out;
        out["Version"] = kVersion;
        out["Entities"] = json::array();
        for( Entity* entity : ordered )
        {
            out["Entities"].push_back( SerializeEntity( InWorld, *entity, included ) );
        }
        return out;
    }


    json SerializeWorld( World& InWorld, Transform* InSceneRoot )
    {
        // Entities of additive scenes (SceneStreaming) belong to their own files.
        std::vector<Entity*> roots;
        if( InSceneRoot )
        {
            for( Transform* child : InSceneRoot->GetChildren() )
            {
                Entity* entity = child->Parent.Get();
                if( entity && InWorld.GetEntityScene( entity->GetId() ) == 0 )
                {
                    roots.push_back( entity );
                }
            }
        }
        Entity* sceneRootEntity = InSceneRoot ? InSceneRoot->Parent.Get() : nullptr;
        InWorld.ForEachEntity( [&InWorld, &roots, sceneRootEntity]( Entity& entity ) {
            if( &entity != sceneRootEntity && !entity.HasComponent<Transform>() && InWorld.GetEntityScene( entity.GetId() ) == 0 )
            {
                roots.push_back( &entity );
            }
        } );

        json out = SerializeEntities( InWorld, roots );

        json cores = json::array();
#if USING( ME_EDITOR )
        for( BaseCore* core : InWorld.GetAllCores() )
        {
            if( core->GetIsSerializable() )
            {
                json coreDef;
                core->Serialize( coreDef );
                cores.push_back( std::move( coreDef ) );
            }
        }
#endif
        out["Cores"] = std::move( cores );
        return out;
    }


    std::vector<EntityHandle> Deserialize( World& InWorld, const json& InData, const LoadOptions& InOptions )
    {
        OPTICK_EVENT( "SceneSerializer::Deserialize" );
        json migrated;
        const json* data = &InData;
        if( !IsLatest( InData ) )
        {
            migrated = MigrateToLatest( InData );
            data = &migrated;
        }

        if( InOptions.LoadCores && data->contains( "Cores" ) )
        {
            for( const json& coreJson : ( *data )["Cores"] )
            {
                if( !coreJson.is_object() || !coreJson.contains( "Type" ) )
                {
                    continue;
                }
                if( BaseCore* core = InWorld.AddCoreByName( coreJson["Type"].get<std::string>() ) )
                {
                    core->Deserialize( coreJson );
                }
                else
                {
                    YIKES( "Core not registered, are you missing a dependency? " + coreJson["Type"].get<std::string>() );
                }
            }
        }

        std::vector<EntityHandle> roots;
        if( !data->contains( "Entities" ) || !( *data )["Entities"].is_array() )
        {
            return roots;
        }
        const json& entities = ( *data )["Entities"];
        const size_t count = entities.size();

        // A: decide every entity's GUID up front so references resolve regardless of order.
        std::unordered_map<uint64_t, uint64_t> remap;
        std::vector<uint64_t> targetGUIDs( count, 0 );
        for( size_t i = 0; i < count; ++i )
        {
            const uint64_t fileGUID = GUIDFromJson( entities[i].value( "GUID", json() ) );
            uint64_t target = fileGUID;
            if( fileGUID == 0 || InOptions.RemapGUIDs || InWorld.FindEntityByGUID( fileGUID ) )
            {
                target = World::GenerateGUID();
            }
            targetGUIDs[i] = target;
            if( fileGUID != 0 )
            {
                remap[fileGUID] = target;
            }
        }

        // B: create the entities.
        std::vector<EntityHandle> created( count );
        for( size_t i = 0; i < count; ++i )
        {
            const json& entityJson = entities[i];
            EntityHandle handle = InWorld.CreateEntityWithGUID( targetGUIDs[i], entityJson.value( "Name", std::string() ) );
            Entity* entity = handle.Get();
            entity->SetLoading( true );
            entity->SetDestroyOnLoad( entityJson.value( "DestroyOnLoad", true ) );
            entity->SetLayer( static_cast<uint8_t>( entityJson.value( "Layer", 0 ) ) );

            if( World::EntityRecord* record = InWorld.GetRecord( entity->GetId() ) )
            {
                if( entityJson.contains( "Prefab" ) && entityJson["Prefab"].is_object() )
                {
                    record->PrefabAsset = entityJson["Prefab"].value( "Asset", std::string() );
                    record->PrefabSource = GUIDFromJson( entityJson["Prefab"].value( "Source", json() ) );
                }
                else if( !InOptions.PrefabAsset.empty() )
                {
                    record->PrefabAsset = InOptions.PrefabAsset;
                    record->PrefabSource = GUIDFromJson( entityJson.value( "GUID", json() ) );
                }
            }
            created[i] = handle;
        }

        // C: components (entity references resolve through the remap table).
        std::vector<std::vector<BaseComponent*>> addedComponents( count );
        {
            SerializationWorldScope scope( &InWorld, &remap );
            for( size_t i = 0; i < count; ++i )
            {
                const json& entityJson = entities[i];
                if( !entityJson.contains( "Components" ) || !entityJson["Components"].is_array() )
                {
                    continue;
                }
                for( const json& rawComponentJson : entityJson["Components"] )
                {
                    if( !rawComponentJson.is_object() || !rawComponentJson.contains( "Type" ) )
                    {
                        continue;
                    }
                    for( const json& componentJson : UpgradeComponent( rawComponentJson, entityJson ) )
                    {
                        BaseComponent* component = created[i]->AddComponentByName( componentJson["Type"].get<std::string>() );
                        if( !component )
                        {
                            continue;
                        }
                        {
                            OPTICK_EVENT_DYNAMIC( component->GetName().c_str() );
                            component->Deserialize( componentJson );
                        }
                        addedComponents[i].push_back( component );
                    }
                }
            }
        }

        // D: hierarchy.
        for( size_t i = 0; i < count; ++i )
        {
            Transform* transform = created[i]->TryGetComponent<Transform>();
            const uint64_t parentGUID = GUIDFromJson( entities[i].value( "Parent", json() ) );
            auto parentIt = parentGUID != 0 ? remap.find( parentGUID ) : remap.end();
            EntityHandle parentEntity = parentIt != remap.end() ? InWorld.FindEntityByGUID( parentIt->second ) : EntityHandle();
            Transform* parentTransform = parentEntity ? parentEntity->TryGetComponent<Transform>() : nullptr;

            if( transform && parentTransform )
            {
                transform->SetParent( *parentTransform );
            }
            else
            {
                if( transform && InOptions.Parent )
                {
                    transform->SetParent( *InOptions.Parent );
                }
                roots.push_back( created[i] );
            }
        }

        // E: Init in file order (parents before children), then activate.
        for( size_t i = 0; i < count; ++i )
        {
            for( BaseComponent* component : addedComponents[i] )
            {
                OPTICK_EVENT_DYNAMIC( component->GetName().c_str() );
                component->Init();
            }
            created[i]->SetActive( entities[i].value( "Active", true ) );
            created[i]->SetLoading( false );
        }

        return roots;
    }


    std::string NormalizePrefabPath( const std::string& InPath )
    {
        if( InPath.empty() )
        {
            return InPath;
        }
        std::string local = Path( InPath ).GetLocalPathString();
        std::replace( local.begin(), local.end(), '\\', '/' );
        return local;
    }


    std::shared_ptr<const json> LoadPrefabData( const std::string& InPrefabPath )
    {
        const std::string key = NormalizePrefabPath( InPrefabPath );
        {
            PrefabCacheData& cache = GetPrefabCache();
            std::lock_guard<std::mutex> lock( cache.Mutex );
            auto it = cache.Entries.find( key );
            if( it != cache.Entries.end() )
            {
                return it->second;
            }
        }

        File file{ Path( key ) };
        std::string contents = file.Read();
        if( contents.empty() )
        {
            // Moved in the editor this session: links to the old path still resolve.
            const std::string moved = AssetDatabase::Get().FindMovedPath( key );
            if( !moved.empty() )
            {
                File movedFile{ Path( moved ) };
                contents = movedFile.Read();
            }
        }
        if( contents.empty() )
        {
            YIKES( "Prefab not found or empty: " + key );
            return {};
        }
        json parsed = json::parse( contents, nullptr, false );
        if( parsed.is_discarded() )
        {
            YIKES( "Prefab is not valid JSON: " + key );
            return {};
        }
        RemapAssetReferences( parsed, key );
        std::shared_ptr<const json> data = std::make_shared<const json>( MigrateToLatest( parsed ) );

        PrefabCacheData& cache = GetPrefabCache();
        std::lock_guard<std::mutex> lock( cache.Mutex );
        cache.Entries[key] = data;
        return data;
    }


    EntityHandle InstantiatePrefab( World& InWorld, const std::string& InPrefabPath, Transform* InParent )
    {
        OPTICK_EVENT( "SceneSerializer::InstantiatePrefab" );
        std::shared_ptr<const json> data = LoadPrefabData( InPrefabPath );
        if( !data )
        {
            return {};
        }

        LoadOptions options;
        options.RemapGUIDs = true;
        options.LoadCores = false;
        options.Parent = InParent;
        options.PrefabAsset = NormalizePrefabPath( InPrefabPath );
        std::vector<EntityHandle> roots = Deserialize( InWorld, *data, options );
        return roots.empty() ? EntityHandle() : roots.front();
    }


    namespace
    {
        // Replaces GUID strings (entity references, parent links) using the remap table.
        void RemapGUIDStrings( json& InOutValue, const std::unordered_map<std::string, std::string>& InRemap )
        {
            if( InOutValue.is_string() )
            {
                auto it = InRemap.find( InOutValue.get<std::string>() );
                if( it != InRemap.end() )
                {
                    InOutValue = it->second;
                }
            }
            else if( InOutValue.is_array() || InOutValue.is_object() )
            {
                for( json& child : InOutValue )
                {
                    RemapGUIDStrings( child, InRemap );
                }
            }
        }
    }


    json SerializePrefab( World& InWorld, Entity& InRoot, const std::string& InPrefabPath, std::unordered_map<uint64_t, uint64_t>* OutInstanceToSource )
    {
        const std::string asset = NormalizePrefabPath( InPrefabPath );
        json data = SerializeEntities( InWorld, { &InRoot } );

        // Instance GUID -> GUID inside the prefab file.
        std::unordered_map<std::string, std::string> remap;
        for( json& entity : data["Entities"] )
        {
            const std::string instanceGUID = entity.value( "GUID", std::string() );
            std::string fileGUID = instanceGUID;
            auto prefab = entity.find( "Prefab" );
            if( prefab != entity.end() && prefab->is_object() && NormalizePrefabPath( prefab->value( "Asset", std::string() ) ) == asset )
            {
                fileGUID = prefab->value( "Source", instanceGUID );
                entity.erase( "Prefab" );
            }
            remap[instanceGUID] = fileGUID;
            if( OutInstanceToSource )
            {
                ( *OutInstanceToSource )[GUIDFromJson( instanceGUID )] = GUIDFromJson( fileGUID );
            }
        }
        for( json& entity : data["Entities"] )
        {
            entity["GUID"] = remap[entity.value( "GUID", std::string() )];
            if( entity.contains( "Parent" ) )
            {
                RemapGUIDStrings( entity["Parent"], remap );
            }
            if( entity.contains( "Components" ) )
            {
                RemapGUIDStrings( entity["Components"], remap );
            }
        }
        return data;
    }


    void ClearPrefabCache()
    {
        PrefabCacheData& cache = GetPrefabCache();
        std::lock_guard<std::mutex> lock( cache.Mutex );
        cache.Entries.clear();
    }


    namespace
    {
        // Strings that could be asset paths (a folder and an extension).
        void CollectPathStrings( const json& InValue, std::unordered_set<std::string>& OutPaths )
        {
            if( InValue.is_string() )
            {
                const std::string& text = InValue.get_ref<const std::string&>();
                if( text.find( '/' ) != std::string::npos && text.find( '.' ) != std::string::npos )
                {
                    OutPaths.insert( text );
                }
            }
            else if( InValue.is_array() || InValue.is_object() )
            {
                for( const json& child : InValue )
                {
                    CollectPathStrings( child, OutPaths );
                }
            }
        }


        void ReplaceStrings( json& InOutValue, const std::unordered_map<std::string, std::string>& InReplacements, int& OutCount )
        {
            if( InOutValue.is_string() )
            {
                auto it = InReplacements.find( InOutValue.get_ref<const std::string&>() );
                if( it != InReplacements.end() )
                {
                    InOutValue = it->second;
                    ++OutCount;
                }
            }
            else if( InOutValue.is_array() || InOutValue.is_object() )
            {
                for( json& child : InOutValue )
                {
                    ReplaceStrings( child, InReplacements, OutCount );
                }
            }
        }
    }


    json CollectAssetReferences( const json& InData )
    {
        std::unordered_set<std::string> paths;
        if( InData.is_object() )
        {
            for( auto it = InData.begin(); it != InData.end(); ++it )
            {
                if( it.key() != "AssetReferences" )
                {
                    CollectPathStrings( it.value(), paths );
                }
            }
        }
        else
        {
            CollectPathStrings( InData, paths );
        }

        AssetDatabase& database = AssetDatabase::Get();
        json table = json::object();
        for( const std::string& path : paths )
        {
            const uint64_t guid = database.FindGUID( path );
            if( guid == 0 )
            {
                continue;
            }
            const std::string key = GUIDToString( guid );
            // One asset under two paths (an old one not rewritten yet): keep the stale one, it's
            // the one the next load has to remap.
            if( !table.contains( key ) || database.FindPath( guid ) == table[key].get<std::string>() )
            {
                table[key] = path;
            }
        }
        return table;
    }


    int RemapAssetReferences( json& InOutData, const std::string& InContext )
    {
        if( !InOutData.is_object() )
        {
            return 0;
        }
        auto table = InOutData.find( "AssetReferences" );
        if( table == InOutData.end() || !table->is_object() )
        {
            return 0;
        }

        std::unordered_map<std::string, std::string> moved;
        for( auto it = table->begin(); it != table->end(); ++it )
        {
            if( !it.value().is_string() )
            {
                continue;
            }
            const std::string path = it.value().get<std::string>();
            if( Path( path ).Exists )
            {
                continue;
            }
            // Game builds scan the asset metadata only now, when something is actually missing.
            AssetDatabase::Get().EnsureScanned();
            const std::string current = AssetDatabase::Get().FindPath( GUIDFromJson( it.key() ) );
            if( current.empty() || current == path || !Path( current ).Exists )
            {
                continue;
            }
            moved[path] = current;
        }
        if( moved.empty() )
        {
            return 0;
        }

        int count = 0;
        ReplaceStrings( InOutData, moved, count );
        for( const auto& [from, to] : moved )
        {
            CLog::Log( CLog::LogType::Info, InContext + ": asset '" + from + "' moved to '" + to + "'" );
        }
        return count;
    }


    void PrepareForSave( json& InOutData, const std::string& InPath, bool InIsPrefab )
    {
        if( !InOutData.is_object() )
        {
            return;
        }
        if( InIsPrefab )
        {
            const std::string asset = NormalizePrefabPath( InPath );
            uint64_t guid = GUIDFromJson( InOutData.value( "AssetGUID", json() ) );
            if( guid == 0 )
            {
                guid = AssetDatabase::Get().FindGUID( asset );
            }
            if( guid == 0 && Path( asset ).Exists )
            {
                if( std::shared_ptr<const json> existing = LoadPrefabData( asset ) )
                {
                    guid = GUIDFromJson( existing->value( "AssetGUID", json() ) );
                }
            }
            if( guid == 0 )
            {
                guid = ::GUID::Generate();
            }
            InOutData["AssetGUID"] = GUIDToString( guid );
            AssetDatabase::Get().Register( asset, guid );
        }
        InOutData.erase( "AssetReferences" );
        json references = CollectAssetReferences( InOutData );
        if( !references.empty() )
        {
            InOutData["AssetReferences"] = std::move( references );
        }
    }
}
