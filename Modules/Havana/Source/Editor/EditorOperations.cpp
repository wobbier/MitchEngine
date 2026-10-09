#include "EditorOperations.h"

#if USING( ME_EDITOR )

#include "Selection.h"
#include "UndoStack.h"
#include "Components/Transform.h"
#include "Cores/SceneCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/SceneSerializer.h"
#include "CLog.h"
#include <imgui.h>
#include <algorithm>
#include <unordered_map>
#include <cstring>

namespace EditorOps
{
    namespace
    {
        constexpr const char* kClipboardPrefix = "MitchEngine.Entities:";

        struct Placement
        {
            uint64_t GUID = 0;
            uint64_t ParentGUID = 0;
            int SiblingIndex = -1;
        };

        Placement CapturePlacement( Entity& InEntity )
        {
            Placement placement;
            placement.GUID = InEntity.GetGUID();
            placement.ParentGUID = GetParentGUID( InEntity );
            if( Transform* transform = InEntity.TryGetComponent<Transform>() )
            {
                placement.SiblingIndex = static_cast<int>( transform->GetSiblingIndex() );
            }
            return placement;
        }


        Transform* ParentFor( uint64_t InParentGUID )
        {
            if( InParentGUID == 0 )
            {
                return GetSceneRoot();
            }
            Transform* parent = FindTransform( InParentGUID );
            return parent ? parent : GetSceneRoot();
        }


        void ApplyPlacement( const Placement& InPlacement )
        {
            Transform* transform = FindTransform( InPlacement.GUID );
            if( !transform )
            {
                return;
            }
            if( Transform* parent = ParentFor( InPlacement.ParentGUID ) )
            {
                if( transform->GetParentTransform() != parent )
                {
                    transform->SetParent( *parent, false );
                }
            }
            if( InPlacement.SiblingIndex >= 0 )
            {
                transform->SetSiblingIndex( static_cast<size_t>( InPlacement.SiblingIndex ) );
            }
        }


        void DestroyByGUIDs( const std::vector<Placement>& InPlacements )
        {
            World& world = GetWorld();
            for( const Placement& placement : InPlacements )
            {
                if( EntityHandle handle = world.FindEntityByGUID( placement.GUID ) )
                {
                    handle->MarkForDelete();
                }
            }
            world.Simulate();
        }


        void RestoreFromData( const json& InData, const std::vector<Placement>& InPlacements )
        {
            World& world = GetWorld();
            SceneSerializer::LoadOptions options;
            options.RemapGUIDs = false;
            options.LoadCores = false;
            SceneSerializer::Deserialize( world, InData, options );
            for( const Placement& placement : InPlacements )
            {
                ApplyPlacement( placement );
            }
            world.Simulate();
        }


        // Create (InIsCreate) or delete of whole entity subtrees, restored with identical GUIDs.
        class EntityStructureCommand
            : public UndoCommand
        {
        public:
            EntityStructureCommand( std::string InName, json InData, std::vector<Placement> InPlacements, bool InIsCreate )
                : m_name( std::move( InName ) )
                , m_data( std::move( InData ) )
                , m_placements( std::move( InPlacements ) )
                , m_isCreate( InIsCreate )
                , m_selectionBefore( Selection::Get().SaveGUIDs() )
            {
            }

            void Undo() override { m_isCreate ? Remove() : Restore(); }
            void Redo() override { m_isCreate ? Restore() : Remove(); }
            std::string GetName() const override { return m_name; }

        private:
            void Remove()
            {
                DestroyByGUIDs( m_placements );
                // Undoing a create goes back to what was selected before it.
                if( m_isCreate )
                {
                    Selection::Get().RestoreGUIDs( GetWorld(), m_selectionBefore );
                }
            }
            void Restore()
            {
                RestoreFromData( m_data, m_placements );
                std::vector<EntityHandle> restored;
                for( const Placement& placement : m_placements )
                {
                    restored.push_back( GetWorld().FindEntityByGUID( placement.GUID ) );
                }
                Selection::Get().Set( restored );
            }

            std::string m_name;
            json m_data;
            std::vector<Placement> m_placements;
            bool m_isCreate;
            std::vector<uint64_t> m_selectionBefore;
        };


        struct ComponentState
        {
            uint64_t GUID = 0;
            std::string Type;
            json Before;
            json After;
        };


        class ComponentStateCommand
            : public UndoCommand
        {
        public:
            ComponentStateCommand( std::string InName, std::vector<ComponentState> InStates, uint64_t InMergeKey )
                : m_name( std::move( InName ) )
                , m_states( std::move( InStates ) )
                , m_mergeKey( InMergeKey )
            {
            }

            void Undo() override
            {
                for( const ComponentState& state : m_states )
                {
                    ApplyComponent( state.GUID, state.Type, state.Before );
                }
            }

            void Redo() override
            {
                for( const ComponentState& state : m_states )
                {
                    ApplyComponent( state.GUID, state.Type, state.After );
                }
            }

            std::string GetName() const override { return m_name; }

            bool MergeWith( const UndoCommand& InNext ) override
            {
                const ComponentStateCommand* next = dynamic_cast<const ComponentStateCommand*>( &InNext );
                if( !next || m_mergeKey == 0 || next->m_mergeKey != m_mergeKey || next->m_states.size() != m_states.size() )
                {
                    return false;
                }
                for( size_t i = 0; i < m_states.size(); ++i )
                {
                    if( m_states[i].GUID != next->m_states[i].GUID || m_states[i].Type != next->m_states[i].Type )
                    {
                        return false;
                    }
                }
                for( size_t i = 0; i < m_states.size(); ++i )
                {
                    m_states[i].After = next->m_states[i].After;
                }
                return true;
            }

        private:
            std::string m_name;
            std::vector<ComponentState> m_states;
            uint64_t m_mergeKey;
        };


        class ComponentPresenceCommand
            : public UndoCommand
        {
        public:
            ComponentPresenceCommand( uint64_t InGUID, std::string InType, json InState, bool InIsAdd )
                : m_guid( InGUID )
                , m_type( std::move( InType ) )
                , m_state( std::move( InState ) )
                , m_isAdd( InIsAdd )
            {
            }

            void Undo() override { m_isAdd ? Detach() : Attach(); }
            void Redo() override { m_isAdd ? Attach() : Detach(); }
            std::string GetName() const override { return ( m_isAdd ? "Add " : "Remove " ) + m_type; }

        private:
            void Attach()
            {
                EntityHandle entity = GetWorld().FindEntityByGUID( m_guid );
                if( !entity )
                {
                    return;
                }
                entity->SetLoading( true );
                BaseComponent* component = entity->AddComponentByName( m_type );
                if( component )
                {
                    component->Deserialize( m_state );
                }
                entity->SetLoading( false );
                if( component )
                {
                    component->Init();
                }
                GetWorld().Simulate();
            }

            void Detach()
            {
                if( EntityHandle entity = GetWorld().FindEntityByGUID( m_guid ) )
                {
                    entity->RemoveComponent( m_type );
                    GetWorld().Simulate();
                }
            }

            uint64_t m_guid;
            std::string m_type;
            json m_state;
            bool m_isAdd;
        };


        struct ReparentEntry
        {
            Placement Before;
            Placement After;
            json TransformBefore;
            json TransformAfter;
        };


        class ReparentCommand
            : public UndoCommand
        {
        public:
            explicit ReparentCommand( std::vector<ReparentEntry> InEntries ) : m_entries( std::move( InEntries ) ) {}

            void Undo() override
            {
                for( auto it = m_entries.rbegin(); it != m_entries.rend(); ++it )
                {
                    Apply( it->Before, it->TransformBefore );
                }
                GetWorld().Simulate();
            }

            void Redo() override
            {
                for( const ReparentEntry& entry : m_entries )
                {
                    Apply( entry.After, entry.TransformAfter );
                }
                GetWorld().Simulate();
            }

            std::string GetName() const override { return "Reparent"; }

        private:
            static void Apply( const Placement& InPlacement, const json& InTransform )
            {
                ApplyPlacement( InPlacement );
                ApplyComponent( InPlacement.GUID, "Transform", InTransform );
            }

            std::vector<ReparentEntry> m_entries;
        };


        struct EntityState
        {
            std::string Name;
            bool Active = true;
            uint8_t Layer = 0;

            static EntityState Capture( Entity& InEntity ) { return { InEntity.GetName(), InEntity.IsActiveSelf(), InEntity.GetLayer() }; }
        };


        class EntityStateCommand
            : public UndoCommand
        {
        public:
            EntityStateCommand( std::string InName, uint64_t InGUID, EntityState InBefore, EntityState InAfter )
                : m_name( std::move( InName ) ), m_guid( InGUID ), m_before( std::move( InBefore ) ), m_after( std::move( InAfter ) )
            {
            }

            void Undo() override { Apply( m_before ); }
            void Redo() override { Apply( m_after ); }
            std::string GetName() const override { return m_name; }

        private:
            void Apply( const EntityState& InState )
            {
                if( EntityHandle entity = GetWorld().FindEntityByGUID( m_guid ) )
                {
                    entity->SetName( InState.Name );
                    entity->SetActive( InState.Active );
                    entity->SetLayer( InState.Layer );
                    GetWorld().Simulate();
                }
            }

            std::string m_name;
            uint64_t m_guid;
            EntityState m_before;
            EntityState m_after;
        };


        std::vector<Placement> CapturePlacements( const std::vector<EntityHandle>& InEntities )
        {
            std::vector<Placement> placements;
            for( const EntityHandle& handle : InEntities )
            {
                if( Entity* entity = handle.Get() )
                {
                    placements.push_back( CapturePlacement( *entity ) );
                }
            }
            return placements;
        }
    }


    World& GetWorld()
    {
        return *GetEngine().GetWorld().lock();
    }


    Transform* GetSceneRoot()
    {
        return GetEngine().SceneNodes ? GetEngine().SceneNodes->GetRootTransform() : nullptr;
    }


    uint64_t GetParentGUID( Entity& InEntity )
    {
        Transform* transform = InEntity.TryGetComponent<Transform>();
        Transform* parent = transform ? transform->GetParentTransform() : nullptr;
        if( !parent || parent == GetSceneRoot() )
        {
            return 0;
        }
        Entity* parentEntity = parent->Parent.Get();
        return parentEntity ? parentEntity->GetGUID() : 0;
    }


    Transform* FindTransform( uint64_t InGUID )
    {
        EntityHandle handle = GetWorld().FindEntityByGUID( InGUID );
        return handle ? handle->TryGetComponent<Transform>() : nullptr;
    }


    EntityHandle CreateEntity( const std::string& InName, Transform* InParent, bool InWithTransform )
    {
        World& world = GetWorld();
        EntityHandle entity = world.CreateEntity( InName );
        if( InWithTransform )
        {
            Transform& transform = entity->AddComponent<Transform>();
            if( Transform* parent = InParent ? InParent : GetSceneRoot() )
            {
                transform.SetParent( *parent );
            }
        }
        world.Simulate();

        json data = SceneSerializer::SerializeEntities( world, { entity.Get() } );
        UndoStack::Get().Push( std::make_unique<EntityStructureCommand>( "Create " + InName, std::move( data ), CapturePlacements( { entity } ), true ) );
        Selection::Get().Set( entity );
        return entity;
    }


    std::vector<EntityHandle> CreateFromData( const json& InData, Transform* InParent, const std::string& InUndoName )
    {
        World& world = GetWorld();
        SceneSerializer::LoadOptions options;
        options.RemapGUIDs = true;
        options.LoadCores = false;
        options.Parent = InParent ? InParent : GetSceneRoot();
        std::vector<EntityHandle> roots = SceneSerializer::Deserialize( world, InData, options );
        world.Simulate();
        if( roots.empty() )
        {
            return roots;
        }

        std::vector<Entity*> rootEntities;
        for( const EntityHandle& root : roots )
        {
            rootEntities.push_back( root.Get() );
        }
        json data = SceneSerializer::SerializeEntities( world, rootEntities );
        UndoStack::Get().Push( std::make_unique<EntityStructureCommand>( InUndoName, std::move( data ), CapturePlacements( roots ), true ) );
        Selection::Get().Set( roots );
        return roots;
    }


    void DeleteEntities( const std::vector<Entity*>& InRoots )
    {
        if( InRoots.empty() )
        {
            return;
        }
        World& world = GetWorld();
        std::vector<EntityHandle> handles;
        for( Entity* entity : InRoots )
        {
            handles.push_back( entity->GetHandle() );
        }
        json data = SceneSerializer::SerializeEntities( world, InRoots );
        std::vector<Placement> placements = CapturePlacements( handles );

        DestroyByGUIDs( placements );
        Selection::Get().Clear();
        UndoStack::Get().Push( std::make_unique<EntityStructureCommand>( handles.size() == 1 ? "Delete" : "Delete " + std::to_string( handles.size() ) + " Entities", std::move( data ), std::move( placements ), false ) );
    }


    void DeleteSelection()
    {
        DeleteEntities( Selection::Get().GetRootEntities() );
    }


    void DuplicateSelection()
    {
        World& world = GetWorld();
        std::vector<Entity*> roots = Selection::Get().GetRootEntities();
        if( roots.empty() )
        {
            return;
        }

        UndoTransaction transaction( "Duplicate" );
        std::vector<EntityHandle> created;
        for( Entity* root : roots )
        {
            Transform* transform = root->TryGetComponent<Transform>();
            Transform* parent = transform ? transform->GetParentTransform() : nullptr;
            const size_t index = transform ? transform->GetSiblingIndex() : 0;

            std::vector<EntityHandle> copies = CreateFromData( SceneSerializer::SerializeEntities( world, { root } ), parent, "Duplicate" );
            for( EntityHandle& copy : copies )
            {
                if( Transform* copyTransform = copy->TryGetComponent<Transform>() )
                {
                    copyTransform->SetSiblingIndex( index + 1 );
                }
                created.push_back( copy );
            }
        }
        Selection::Get().Set( created );
    }


    void CopySelection()
    {
        std::vector<Entity*> roots = Selection::Get().GetRootEntities();
        if( roots.empty() )
        {
            return;
        }
        json data = SceneSerializer::SerializeEntities( GetWorld(), roots );
        const std::string text = std::string( kClipboardPrefix ) + data.dump();
        ImGui::SetClipboardText( text.c_str() );
    }


    void CutSelection()
    {
        CopySelection();
        DeleteSelection();
    }


    bool ClipboardHasEntities()
    {
        const char* text = ImGui::GetClipboardText();
        return text && std::string( text ).rfind( kClipboardPrefix, 0 ) == 0;
    }


    void Paste( Transform* InParent )
    {
        const char* text = ImGui::GetClipboardText();
        if( !text || std::string( text ).rfind( kClipboardPrefix, 0 ) != 0 )
        {
            return;
        }
        json data = json::parse( text + std::strlen( kClipboardPrefix ), nullptr, false );
        if( data.is_discarded() )
        {
            return;
        }

        Transform* parent = InParent;
        if( !parent )
        {
            // Paste next to the active selection, like most editors.
            if( Transform* active = Selection::Get().GetActiveTransform() )
            {
                parent = active->GetParentTransform();
            }
        }
        CreateFromData( data, parent, "Paste" );
    }


    void Reparent( const std::vector<Entity*>& InEntities, Transform* InNewParent, int InSiblingIndex, bool InKeepWorldTransform )
    {
        Transform* newParent = InNewParent ? InNewParent : GetSceneRoot();
        if( !newParent )
        {
            return;
        }

        std::vector<ReparentEntry> entries;
        int index = InSiblingIndex;
        for( Entity* entity : InEntities )
        {
            Transform* transform = entity ? entity->TryGetComponent<Transform>() : nullptr;
            if( !transform || transform == newParent || newParent->IsDescendantOf( *transform ) )
            {
                continue;
            }

            ReparentEntry entry;
            entry.Before = CapturePlacement( *entity );
            transform->Serialize( entry.TransformBefore );

            transform->SetParent( *newParent, InKeepWorldTransform );
            if( index >= 0 )
            {
                transform->SetSiblingIndex( static_cast<size_t>( index++ ) );
            }

            entry.After = CapturePlacement( *entity );
            transform->Serialize( entry.TransformAfter );
            entries.push_back( std::move( entry ) );
        }
        if( entries.empty() )
        {
            return;
        }
        GetWorld().Simulate();
        UndoStack::Get().Push( std::make_unique<ReparentCommand>( std::move( entries ) ) );
    }


    void Rename( Entity& InEntity, const std::string& InName )
    {
        if( InEntity.GetName() == InName )
        {
            return;
        }
        EntityState before = EntityState::Capture( InEntity );
        InEntity.SetName( InName );
        UndoStack::Get().Push( std::make_unique<EntityStateCommand>( "Rename", InEntity.GetGUID(), before, EntityState::Capture( InEntity ) ) );
    }


    void SetActive( Entity& InEntity, bool InActive )
    {
        if( InEntity.IsActiveSelf() == InActive )
        {
            return;
        }
        EntityState before = EntityState::Capture( InEntity );
        InEntity.SetActive( InActive );
        GetWorld().Simulate();
        UndoStack::Get().Push( std::make_unique<EntityStateCommand>( InActive ? "Activate" : "Deactivate", InEntity.GetGUID(), before, EntityState::Capture( InEntity ) ) );
    }


    void SetLayer( Entity& InEntity, uint8_t InLayer )
    {
        if( InEntity.GetLayer() == InLayer )
        {
            return;
        }
        EntityState before = EntityState::Capture( InEntity );
        InEntity.SetLayer( InLayer );
        UndoStack::Get().Push( std::make_unique<EntityStateCommand>( "Set Layer", InEntity.GetGUID(), before, EntityState::Capture( InEntity ) ) );
    }


    BaseComponent* AddComponent( Entity& InEntity, const std::string& InTypeName )
    {
        if( InEntity.GetComponentByName( InTypeName ) )
        {
            return InEntity.GetComponentByName( InTypeName );
        }
        BaseComponent* component = InEntity.AddComponentByName( InTypeName );
        if( !component )
        {
            return nullptr;
        }
        GetWorld().Simulate();
        json state;
        component->Serialize( state );
        UndoStack::Get().Push( std::make_unique<ComponentPresenceCommand>( InEntity.GetGUID(), InTypeName, std::move( state ), true ) );
        return component;
    }


    void RemoveComponent( Entity& InEntity, const std::string& InTypeName )
    {
        if( InTypeName == "Transform" )
        {
            BRUH( "Transform can't be removed from an entity in the editor." );
            return;
        }
        BaseComponent* component = InEntity.GetComponentByName( InTypeName );
        if( !component )
        {
            return;
        }
        json state;
        component->Serialize( state );
        InEntity.RemoveComponent( InTypeName );
        GetWorld().Simulate();
        UndoStack::Get().Push( std::make_unique<ComponentPresenceCommand>( InEntity.GetGUID(), InTypeName, std::move( state ), false ) );
    }


    void RecordComponentEdit( Entity& InEntity, const std::string& InTypeName, const json& InBefore, const json& InAfter, const std::string& InUndoName, uint64_t InMergeKey )
    {
        if( InBefore == InAfter )
        {
            return;
        }
        ComponentState state{ InEntity.GetGUID(), InTypeName, InBefore, InAfter };
        UndoStack::Get().Push( std::make_unique<ComponentStateCommand>( InUndoName, std::vector<ComponentState>{ state }, InMergeKey ) );
    }


    void RecordComponentEdits( const std::vector<ComponentEdit>& InEdits, const std::string& InUndoName )
    {
        std::vector<ComponentState> states;
        for( const ComponentEdit& edit : InEdits )
        {
            if( edit.Before != edit.After )
            {
                states.push_back( { edit.GUID, edit.Type, edit.Before, edit.After } );
            }
        }
        if( !states.empty() )
        {
            UndoStack::Get().Push( std::make_unique<ComponentStateCommand>( InUndoName, std::move( states ), 0 ) );
        }
    }


    json GetComponentDefaults( const std::string& InTypeName )
    {
        static std::unordered_map<std::string, json> s_defaults;
        auto cached = s_defaults.find( InTypeName );
        if( cached != s_defaults.end() )
        {
            return cached->second;
        }
        World& world = GetWorld();
        json defaults;
        EntityHandle temp = world.CreateEntity( "__ComponentDefaults" );
        // Loading defers Init, so the temporary component never touches resources or cores.
        temp->SetLoading( true );
        if( BaseComponent* component = temp->AddComponentByName( InTypeName ) )
        {
            component->Serialize( defaults );
        }
        temp->MarkForDelete();
        world.Simulate();
        s_defaults[InTypeName] = defaults;
        return defaults;
    }


    json CaptureComponent( Entity& InEntity, const std::string& InTypeName )
    {
        json state;
        if( BaseComponent* component = InEntity.GetComponentByName( InTypeName ) )
        {
            component->Serialize( state );
        }
        return state;
    }


    bool ApplyComponent( uint64_t InEntityGUID, const std::string& InTypeName, const json& InState )
    {
        EntityHandle entity = GetWorld().FindEntityByGUID( InEntityGUID );
        BaseComponent* component = entity ? entity->GetComponentByName( InTypeName ) : nullptr;
        if( !component || InState.is_null() )
        {
            return false;
        }
        component->Deserialize( InState );
        component->OnPropertyChanged( "" );
        return true;
    }


    void ResetForNewScene()
    {
        Selection::Get().Clear();
        UndoStack::Get().Clear();
    }
}

#endif
