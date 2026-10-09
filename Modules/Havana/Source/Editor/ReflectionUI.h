#pragma once
#include "Dementia.h"
#include "Reflection/Reflection.h"
#include <functional>
#include <string>
#include <vector>

#if USING( ME_EDITOR )

// Inspector UI generated from reflection data. Draws every visible field of a reflected type with a
// widget chosen from its PropertyType and metadata (ranges, angles, colours, enums, assets, nested
// structs, arrays, entity references). With several instances (multi-selection) mixed values are
// flagged and edits are copied to every instance.
namespace ReflectionUI
{
    struct Context
    {
        // Other instances of the same type edited together with the primary one.
        std::vector<void*> Others;
        // Top-level fields changed this frame (after propagation to Others).
        std::vector<std::string> ChangedFields;

        // Prefab overrides (optional): fields reported as overridden get a marker, and
        // right-clicking their label offers Apply/Revert (InApply = true for Apply).
        std::function<bool( const std::string& InField )> IsOverridden;
        std::function<void( const std::string& InField, bool InApply )> OnPrefabAction;
    };

    // Draws all fields; returns true when anything changed.
    bool DrawType( const Reflection::TypeInfo& InType, void* InInstance, Context& InContext );

    // Editor widgets for custom value types (Reflection::CustomTypeTraits<T>::Name), e.g. EntityHandle.
    // The drawer gets the label and the value pointer and returns true on change.
    // InField may be null (array elements).
    using CustomDrawer = std::function<bool( const char* InLabel, const Reflection::FieldInfo* InField, void* InValue )>;
    void RegisterCustomDrawer( const std::string& InCppName, CustomDrawer InDrawer );

    // Registers the built-in custom drawers (EntityHandle).
    void RegisterDefaultDrawers();
}

#endif
