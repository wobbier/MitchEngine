#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <functional>
#include <string>
#include <vector>

#if USING( ME_EDITOR )

class Entity;

// Prefab instance workflow: creating prefabs from entities, override detection against the prefab
// file, and Apply / Revert (per field or whole instance) and Unpack. An instance's entities carry a
// link (asset + source GUID in the prefab file); overrides are any top-level component values that
// differ from the source entity's. The instance root's position and rotation never count as
// overrides.
namespace PrefabTools
{
    // Prefab asset the entity is linked to ("" when not an instance).
    std::string GetPrefabAsset( Entity& InEntity );
    // The outermost entity of the instance containing InEntity (null when not an instance).
    Entity* FindInstanceRoot( Entity& InEntity );

    // The entity's source data in its prefab (null json when unavailable).
    json GetSourceEntity( Entity& InEntity );
    json GetSourceComponent( Entity& InEntity, const std::string& InType );

    // Top-level component keys whose instance value differs from the prefab.
    std::vector<std::string> GetOverriddenFields( Entity& InEntity, const std::string& InType );
    bool IsFieldOverridden( Entity& InEntity, const std::string& InType, const std::string& InField );
    bool HasOverrides( Entity& InRoot );

    // Writes the subtree to InAssetPath and links it as an instance of the new prefab.
    bool CreatePrefab( Entity& InRoot, const std::string& InAssetPath );

    // Per-field: copy the value into the prefab file (and unmodified instances) / back from it.
    void ApplyField( Entity& InEntity, const std::string& InType, const std::string& InField );
    void RevertField( Entity& InEntity, const std::string& InType, const std::string& InField );

    // Whole instance.
    void ApplyAll( Entity& InEntity );
    void RevertAll( Entity& InEntity );
    void Unpack( Entity& InEntity );

    // ED-6, variants: writes InPath as a variant of InBase (no differences yet). Saving a variant
    // (Apply, prefab mode) writes only its differences; edits to a base reach its variants'
    // instances in the world wherever the variant doesn't override them.
    bool CreateVariant( const std::string& InBase, const std::string& InPath );

    // ED-6, prefab mode. Rewrites the prefab from an instance (as Apply All does, without touching
    // other instances or the undo history); OutBefore / OutAfter are the file's contents.
    bool SavePrefabFromInstance( Entity& InRoot, json& OutBefore, json& OutAfter );
    // Pushes a prefab edit (before -> after) into the world's instances as one undoable step.
    void RecordPrefabEdit( const std::string& InAsset, const json& InBefore, const json& InAfter );
    // The prefab being edited in prefab mode ("" when none) and how to save it (EditorApp sets
    // both; Ctrl+S saves the prefab while it's open).
    void SetPrefabMode( const std::string& InAsset, std::function<bool()> InSave );
    const std::string& GetPrefabModeAsset();
    bool SavePrefabMode();

    // Prefab assets (or a folder of them) moved: instances in the world follow the move.
    void OnAssetsMoved( const std::string& InFrom, const std::string& InTo );
}

#endif
