#include "PrefabTools.h"

#if USING( ME_EDITOR )

#include "EditorOperations.h"
#include "Selection.h"
#include "UndoStack.h"
#include "Components/Transform.h"
#include "ECS/Component.h"
#include "Engine/World.h"
#include "World/SceneSerializer.h"
#include "File.h"
#include "Path.h"
#include "CLog.h"
#include <cmath>
#include <functional>
#include <unordered_map>

namespace PrefabTools
{
    namespace
    {
        using GuidMap = std::unordered_map<std::string, std::string>;

        World::EntityRecord* Record( Entity& InEntity )
        {
            return InEntity.GetWorld()->GetRecord( InEntity.GetId() );
        }


        bool NearlyEqual( const json& InA, const json& InB )
        {
            if( InA.is_number() && InB.is_number() )
            {
                const double a = InA.get<double>();
                const double b = InB.get<double>();
                return std::fabs( a - b ) <= 1e-4 * std::max( 1.0, std::max( std::fabs( a ), std::fabs( b ) ) );
            }
            if( InA.is_array() && InB.is_array() )
            {
                if( InA.size() != InB.size() )
                {
                    return false;
                }
                for( size_t i = 0; i < InA.size(); ++i )
                {
                    if( !NearlyEqual( InA[i], InB[i] ) )
                    {
                        return false;
                    }
                }
                return true;
            }
            if( InA.is_object() && InB.is_object() )
            {
                if( InA.size() != InB.size() )
                {
                    return false;
                }
                for( auto it = InA.begin(); it != InA.end(); ++it )
                {
                    auto other = InB.find( it.key() );
                    if( other == InB.end() || !NearlyEqual( it.value(), *other ) )
                    {
                        return false;
                    }
                }
                return true;
            }
            return InA == InB;
        }


        void RemapStrings( json& InOutValue, const GuidMap& InMap )
        {
            if( InOutValue.is_string() )
            {
                auto it = InMap.find( InOutValue.get<std::string>() );
                if( it != InMap.end() )
                {
                    InOutValue = it->second;
                }
            }
            else if( InOutValue.is_array() || InOutValue.is_object() )
            {
                for( json& child : InOutValue )
                {
                    RemapStrings( child, InMap );
                }
            }
        }


        void CollectSubtree( Entity& InRoot, std::vector<Entity*>& OutEntities )
        {
            OutEntities.push_back( &InRoot );
            if( Transform* transform = InRoot.TryGetComponent<Transform>() )
            {
                for( Transform* child : transform->GetChildren() )
                {
                    if( Entity* entity = child->Parent.Get() )
                    {
                        CollectSubtree( *entity, OutEntities );
                    }
                }
            }
        }


        // Instance GUID <-> prefab file GUID for the entities of one instance.
        void BuildMaps( Entity& InRoot, const std::string& InAsset, GuidMap& OutToSource, GuidMap& OutToInstance )
        {
            std::vector<Entity*> entities;
            CollectSubtree( InRoot, entities );
            for( Entity* entity : entities )
            {
                World::EntityRecord* record = Record( *entity );
                if( record && record->PrefabAsset == InAsset )
                {
                    const std::string instanceGUID = SceneSerializer::GUIDToString( entity->GetGUID() );
                    const std::string sourceGUID = SceneSerializer::GUIDToString( record->PrefabSource );
                    OutToSource[instanceGUID] = sourceGUID;
                    OutToInstance[sourceGUID] = instanceGUID;
                }
            }
        }


        const json* FindEntityJson( const json& InData, uint64_t InGUID )
        {
            if( !InData.contains( "Entities" ) )
            {
                return nullptr;
            }
            for( const json& entity : InData["Entities"] )
            {
                if( SceneSerializer::GUIDFromJson( entity.value( "GUID", json() ) ) == InGUID )
                {
                    return &entity;
                }
            }
            return nullptr;
        }


        json* FindEntityJson( json& InData, uint64_t InGUID )
        {
            return const_cast<json*>( FindEntityJson( static_cast<const json&>( InData ), InGUID ) );
        }


        json* FindComponentJson( json& InEntity, const std::string& InType )
        {
            if( !InEntity.contains( "Components" ) )
            {
                return nullptr;
            }
            for( json& component : InEntity["Components"] )
            {
                if( component.value( "Type", std::string() ) == InType )
                {
                    return &component;
                }
            }
            return nullptr;
        }


        bool IsRootPlacement( Entity& InEntity, const std::string& InType, const std::string& InField )
        {
            return InType == "Transform" && ( InField == "Position" || InField == "Rotation" ) && FindInstanceRoot( InEntity ) == &InEntity;
        }


        bool WritePrefab( const std::string& InAsset, const json& InData )
        {
            File file{ Path( InAsset ) };
            file.Write( InData.dump( 4 ) );
            SceneSerializer::ClearPrefabCache();
            return true;
        }


        // Pushes prefab changes (old -> new file contents) into every instance in the world: values
        // still equal to the old prefab take the new value; components the prefab gained are added.
        void PropagateToInstances( const std::string& InAsset, const json& InOld, const json& InNew )
        {
            World& world = EditorOps::GetWorld();
            std::vector<Entity*> linked;
            world.ForEachEntity( [&]( Entity& entity ) {
                World::EntityRecord* record = Record( entity );
                if( record && record->PrefabAsset == InAsset )
                {
                    linked.push_back( &entity );
                }
            } );

            for( Entity* entity : linked )
            {
                World::EntityRecord* record = Record( *entity );
                const json* oldEntity = FindEntityJson( InOld, record->PrefabSource );
                const json* newEntity = FindEntityJson( InNew, record->PrefabSource );
                if( !newEntity || !newEntity->contains( "Components" ) )
                {
                    continue;
                }
                Entity* root = FindInstanceRoot( *entity );
                GuidMap toSource, toInstance;
                BuildMaps( root ? *root : *entity, InAsset, toSource, toInstance );

                for( const json& newComponent : ( *newEntity )["Components"] )
                {
                    const std::string type = newComponent.value( "Type", std::string() );
                    json remapped = newComponent;
                    RemapStrings( remapped, toInstance );

                    json oldComponent;
                    if( oldEntity )
                    {
                        json oldCopy = *oldEntity;
                        if( json* found = FindComponentJson( oldCopy, type ) )
                        {
                            oldComponent = *found;
                            RemapStrings( oldComponent, toInstance );
                        }
                    }

                    BaseComponent* component = entity->GetComponentByName( type );
                    if( !component )
                    {
                        if( oldComponent.is_null() )
                        {
                            // New in the prefab: add it to the instance.
                            entity->SetLoading( true );
                            if( BaseComponent* added = entity->AddComponentByName( type ) )
                            {
                                added->Deserialize( remapped );
                                entity->SetLoading( false );
                                added->Init();
                            }
                            entity->SetLoading( false );
                        }
                        continue;
                    }

                    json instanceState;
                    component->Serialize( instanceState );
                    bool changed = false;
                    for( auto it = remapped.begin(); it != remapped.end(); ++it )
                    {
                        const std::string& key = it.key();
                        if( key == "Type" || IsRootPlacement( *entity, type, key ) )
                        {
                            continue;
                        }
                        const bool unmodified = !oldComponent.is_null() && oldComponent.contains( key ) && instanceState.contains( key ) && NearlyEqual( instanceState[key], oldComponent[key] );
                        if( unmodified && !NearlyEqual( instanceState[key], it.value() ) )
                        {
                            instanceState[key] = it.value();
                            changed = true;
                        }
                    }
                    if( changed )
                    {
                        EditorOps::ApplyComponent( entity->GetGUID(), type, instanceState );
                    }
                }
            }
            world.Simulate();
            UndoStack::Get().MarkDirty();
        }


        // Unpack: restores/clears prefab links.
        class PrefabLinkCommand
            : public UndoCommand
        {
        public:
            struct Link
            {
                uint64_t GUID = 0;
                std::string Asset;
                uint64_t Source = 0;
            };

            explicit PrefabLinkCommand( std::vector<Link> InLinks ) : m_links( std::move( InLinks ) ) {}

            void Undo() override { Apply( true ); }
            void Redo() override { Apply( false ); }
            std::string GetName() const override { return "Unpack Prefab"; }

        private:
            void Apply( bool InLinked )
            {
                World& world = EditorOps::GetWorld();
                for( const Link& link : m_links )
                {
                    EntityHandle entity = world.FindEntityByGUID( link.GUID );
                    World::EntityRecord* record = entity ? Record( *entity.Get() ) : nullptr;
                    if( record )
                    {
                        record->PrefabAsset = InLinked ? link.Asset : std::string();
                        record->PrefabSource = InLinked ? link.Source : 0;
                    }
                }
            }

            std::vector<Link> m_links;
        };
    }


    std::string GetPrefabAsset( Entity& InEntity )
    {
        World::EntityRecord* record = Record( InEntity );
        return record ? record->PrefabAsset : std::string();
    }


    Entity* FindInstanceRoot( Entity& InEntity )
    {
        const std::string asset = GetPrefabAsset( InEntity );
        if( asset.empty() )
        {
            return nullptr;
        }
        Entity* current = &InEntity;
        while( Transform* transform = current->TryGetComponent<Transform>() )
        {
            Transform* parent = transform->GetParentTransform();
            Entity* parentEntity = parent ? parent->Parent.Get() : nullptr;
            if( !parentEntity || GetPrefabAsset( *parentEntity ) != asset )
            {
                break;
            }
            current = parentEntity;
        }
        return current;
    }


    json GetSourceEntity( Entity& InEntity )
    {
        World::EntityRecord* record = Record( InEntity );
        if( !record || record->PrefabAsset.empty() )
        {
            return json();
        }
        std::shared_ptr<const json> data = SceneSerializer::LoadPrefabData( record->PrefabAsset );
        const json* entity = data ? FindEntityJson( *data, record->PrefabSource ) : nullptr;
        return entity ? *entity : json();
    }


    json GetSourceComponent( Entity& InEntity, const std::string& InType )
    {
        json entity = GetSourceEntity( InEntity );
        if( entity.is_null() )
        {
            return json();
        }
        json* component = FindComponentJson( entity, InType );
        if( !component )
        {
            return json();
        }
        // Express entity references in this instance's GUIDs.
        Entity* root = FindInstanceRoot( InEntity );
        GuidMap toSource, toInstance;
        BuildMaps( root ? *root : InEntity, GetPrefabAsset( InEntity ), toSource, toInstance );
        json result = *component;
        RemapStrings( result, toInstance );
        return result;
    }


    std::vector<std::string> GetOverriddenFields( Entity& InEntity, const std::string& InType )
    {
        std::vector<std::string> fields;
        if( GetPrefabAsset( InEntity ).empty() )
        {
            return fields;
        }
        const json instance = EditorOps::CaptureComponent( InEntity, InType );
        const json source = GetSourceComponent( InEntity, InType );
        for( auto it = instance.begin(); it != instance.end(); ++it )
        {
            const std::string& key = it.key();
            if( key == "Type" || IsRootPlacement( InEntity, InType, key ) )
            {
                continue;
            }
            if( source.is_null() || !source.contains( key ) || !NearlyEqual( source[key], it.value() ) )
            {
                fields.push_back( key );
            }
        }
        return fields;
    }


    bool IsFieldOverridden( Entity& InEntity, const std::string& InType, const std::string& InField )
    {
        if( GetPrefabAsset( InEntity ).empty() || IsRootPlacement( InEntity, InType, InField ) )
        {
            return false;
        }
        const json instance = EditorOps::CaptureComponent( InEntity, InType );
        const json source = GetSourceComponent( InEntity, InType );
        if( !instance.contains( InField ) )
        {
            return false;
        }
        return source.is_null() || !source.contains( InField ) || !NearlyEqual( source[InField], instance[InField] );
    }


    bool HasOverrides( Entity& InRoot )
    {
        std::vector<Entity*> entities;
        CollectSubtree( InRoot, entities );
        const std::string asset = GetPrefabAsset( InRoot );
        for( Entity* entity : entities )
        {
            if( GetPrefabAsset( *entity ) != asset )
            {
                return true;   // added entity or nested instance
            }
            for( BaseComponent* component : entity->GetAllComponents() )
            {
                if( !GetOverriddenFields( *entity, component->GetName() ).empty() )
                {
                    return true;
                }
            }
        }
        return false;
    }


    bool CreatePrefab( Entity& InRoot, const std::string& InAssetPath )
    {
        const std::string asset = SceneSerializer::NormalizePrefabPath( InAssetPath );
        std::unordered_map<uint64_t, uint64_t> instanceToSource;
        json data = SceneSerializer::SerializePrefab( EditorOps::GetWorld(), InRoot, asset, &instanceToSource );
        if( !WritePrefab( asset, data ) )
        {
            return false;
        }
        // The subtree becomes an instance of the new prefab (nested instances keep their links).
        std::vector<Entity*> entities;
        CollectSubtree( InRoot, entities );
        for( Entity* entity : entities )
        {
            World::EntityRecord* record = Record( *entity );
            if( record && record->PrefabAsset.empty() )
            {
                record->PrefabAsset = asset;
                record->PrefabSource = instanceToSource[entity->GetGUID()];
            }
        }
        UndoStack::Get().MarkDirty();
        CLog::Log( CLog::LogType::Info, "Created prefab " + asset );
        return true;
    }


    void ApplyField( Entity& InEntity, const std::string& InType, const std::string& InField )
    {
        World::EntityRecord* record = Record( InEntity );
        if( !record || record->PrefabAsset.empty() )
        {
            return;
        }
        const std::string asset = record->PrefabAsset;
        std::shared_ptr<const json> current = SceneSerializer::LoadPrefabData( asset );
        if( !current )
        {
            return;
        }
        const json oldData = *current;
        json newData = oldData;
        json* entityJson = FindEntityJson( newData, record->PrefabSource );
        if( !entityJson )
        {
            BRUH( "Apply: the entity isn't part of " + asset + "; apply the whole instance instead." );
            return;
        }

        Entity* root = FindInstanceRoot( InEntity );
        GuidMap toSource, toInstance;
        BuildMaps( root ? *root : InEntity, asset, toSource, toInstance );
        json value = EditorOps::CaptureComponent( InEntity, InType );
        RemapStrings( value, toSource );

        json* component = FindComponentJson( *entityJson, InType );
        if( !component )
        {
            ( *entityJson )["Components"].push_back( value );
        }
        else if( value.contains( InField ) )
        {
            ( *component )[InField] = value[InField];
        }
        WritePrefab( asset, newData );
        PropagateToInstances( asset, oldData, newData );
    }


    void RevertField( Entity& InEntity, const std::string& InType, const std::string& InField )
    {
        const json source = GetSourceComponent( InEntity, InType );
        if( source.is_null() || !source.contains( InField ) )
        {
            return;
        }
        const json before = EditorOps::CaptureComponent( InEntity, InType );
        json after = before;
        after[InField] = source[InField];
        EditorOps::ApplyComponent( InEntity.GetGUID(), InType, after );
        EditorOps::RecordComponentEdit( InEntity, InType, before, EditorOps::CaptureComponent( InEntity, InType ), "Revert " + InField );
    }


    void ApplyAll( Entity& InEntity )
    {
        Entity* root = FindInstanceRoot( InEntity );
        if( !root )
        {
            return;
        }
        const std::string asset = GetPrefabAsset( *root );
        std::shared_ptr<const json> current = SceneSerializer::LoadPrefabData( asset );
        const json oldData = current ? *current : json();

        std::unordered_map<uint64_t, uint64_t> instanceToSource;
        json newData = SceneSerializer::SerializePrefab( EditorOps::GetWorld(), *root, asset, &instanceToSource );
        WritePrefab( asset, newData );

        // Entities added to this instance are now part of the prefab.
        std::vector<Entity*> entities;
        CollectSubtree( *root, entities );
        for( Entity* entity : entities )
        {
            World::EntityRecord* record = Record( *entity );
            if( record && record->PrefabAsset.empty() )
            {
                record->PrefabAsset = asset;
                record->PrefabSource = instanceToSource[entity->GetGUID()];
            }
        }
        PropagateToInstances( asset, oldData, newData );
        CLog::Log( CLog::LogType::Info, "Applied instance to " + asset );
    }


    void RevertAll( Entity& InEntity )
    {
        Entity* root = FindInstanceRoot( InEntity );
        if( !root )
        {
            return;
        }
        const std::string asset = GetPrefabAsset( *root );
        UndoTransaction transaction( "Revert Prefab Instance" );

        std::vector<Entity*> entities;
        CollectSubtree( *root, entities );
        std::vector<Entity*> added;
        for( Entity* entity : entities )
        {
            if( GetPrefabAsset( *entity ) != asset )
            {
                // Added in the instance (nested instances count as added unless the prefab has them).
                if( GetSourceEntity( *entity ).is_null() || GetPrefabAsset( *entity ).empty() )
                {
                    added.push_back( entity );
                }
                continue;
            }
            json source = GetSourceEntity( *entity );
            if( source.is_null() )
            {
                continue;
            }
            // Components the instance added.
            for( BaseComponent* component : entity->GetAllComponents() )
            {
                if( !FindComponentJson( source, component->GetName() ) && component->GetName() != "Transform" )
                {
                    EditorOps::RemoveComponent( *entity, component->GetName() );
                }
            }
            // Component values and components the instance removed.
            for( const json& sourceComponent : source["Components"] )
            {
                const std::string type = sourceComponent.value( "Type", std::string() );
                if( !entity->GetComponentByName( type ) )
                {
                    EditorOps::AddComponent( *entity, type );
                }
                const json reverted = GetSourceComponent( *entity, type );
                const json before = EditorOps::CaptureComponent( *entity, type );
                json after = reverted;
                if( entity == root && type == "Transform" )
                {
                    // Keep the instance where it was placed.
                    after["Position"] = before.value( "Position", json() );
                    after["Rotation"] = before.value( "Rotation", json() );
                }
                EditorOps::ApplyComponent( entity->GetGUID(), type, after );
                EditorOps::RecordComponentEdit( *entity, type, before, EditorOps::CaptureComponent( *entity, type ), "Revert " + type );
            }
            const std::string sourceName = source.value( "Name", entity->GetName() );
            if( entity != root )
            {
                EditorOps::Rename( *entity, sourceName );
            }
        }

        // Remove added subtrees (only their topmost entities).
        std::vector<Entity*> addedRoots;
        for( Entity* entity : added )
        {
            Transform* transform = entity->TryGetComponent<Transform>();
            Transform* parent = transform ? transform->GetParentTransform() : nullptr;
            Entity* parentEntity = parent ? parent->Parent.Get() : nullptr;
            if( !parentEntity || std::find( added.begin(), added.end(), parentEntity ) == added.end() )
            {
                addedRoots.push_back( entity );
            }
        }
        EditorOps::DeleteEntities( addedRoots );
        Selection::Get().Set( root->GetHandle() );
    }


    void Unpack( Entity& InEntity )
    {
        Entity* root = FindInstanceRoot( InEntity );
        if( !root )
        {
            return;
        }
        const std::string asset = GetPrefabAsset( *root );
        std::vector<Entity*> entities;
        CollectSubtree( *root, entities );
        std::vector<PrefabLinkCommand::Link> links;
        for( Entity* entity : entities )
        {
            World::EntityRecord* record = Record( *entity );
            if( record && record->PrefabAsset == asset )
            {
                links.push_back( { entity->GetGUID(), record->PrefabAsset, record->PrefabSource } );
                record->PrefabAsset.clear();
                record->PrefabSource = 0;
            }
        }
        UndoStack::Get().Push( std::make_unique<PrefabLinkCommand>( std::move( links ) ) );
    }
}

#endif
