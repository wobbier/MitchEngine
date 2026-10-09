#pragma once
#include "Dementia.h"
#include "ECS/EntityHandle.h"
#include "JSON.h"
#include <string>
#include <vector>

#if USING( ME_EDITOR )

class World;
class Transform;
class Entity;
class BaseComponent;

// Undoable editor operations on the scene. Every structural edit made by the editor goes through
// here so it lands on the UndoStack and marks the scene dirty.
namespace EditorOps
{
    World& GetWorld();
    // The scene root transform (parent of every top-level entity).
    Transform* GetSceneRoot();
    // 0 for the scene root / no parent.
    uint64_t GetParentGUID( Entity& InEntity );
    Transform* FindTransform( uint64_t InGUID );

    EntityHandle CreateEntity( const std::string& InName, Transform* InParent = nullptr, bool InWithTransform = true );
    // Instantiates serialized entities (SceneSerializer data) with fresh GUIDs as an undoable create.
    std::vector<EntityHandle> CreateFromData( const json& InData, Transform* InParent, const std::string& InUndoName );

    void DeleteEntities( const std::vector<Entity*>& InRoots );
    void DeleteSelection();
    void DuplicateSelection();
    void CopySelection();
    void CutSelection();
    void Paste( Transform* InParent = nullptr );
    bool ClipboardHasEntities();

    // Moves entities under a new parent (null = scene root) at InSiblingIndex (-1 = last).
    void Reparent( const std::vector<Entity*>& InEntities, Transform* InNewParent, int InSiblingIndex = -1, bool InKeepWorldTransform = true );

    void Rename( Entity& InEntity, const std::string& InName );
    void SetActive( Entity& InEntity, bool InActive );
    void SetLayer( Entity& InEntity, uint8_t InLayer );

    BaseComponent* AddComponent( Entity& InEntity, const std::string& InTypeName );
    void RemoveComponent( Entity& InEntity, const std::string& InTypeName );

    struct ComponentEdit
    {
        uint64_t GUID = 0;
        std::string Type;
        json Before;
        json After;
    };
    // Records already-applied edits of several components as one undo step (multi-edit).
    void RecordComponentEdits( const std::vector<ComponentEdit>& InEdits, const std::string& InUndoName );
    // Default serialized state of a component type (from a temporary, uninitialized instance).
    json GetComponentDefaults( const std::string& InTypeName );

    // Records an edit of one component that has already been applied (inspector, gizmo).
    void RecordComponentEdit( Entity& InEntity, const std::string& InTypeName, const json& InBefore, const json& InAfter, const std::string& InUndoName, uint64_t InMergeKey = 0 );

    // Snapshot / restore a component's serialized state by entity GUID + registry type name.
    json CaptureComponent( Entity& InEntity, const std::string& InTypeName );
    bool ApplyComponent( uint64_t InEntityGUID, const std::string& InTypeName, const json& InState );

    // Clears the selection and undo history (new scene / scene load).
    void ResetForNewScene();
}

#endif
