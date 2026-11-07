#pragma once
#include "Dementia.h"
#include "Reflection/Reflection.h"
#include <functional>
#include <string>
#include <vector>

#if USING( ME_EDITOR )
#include <Widgets/ScrubField.h>

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

        // Field search (ED-7): lower-case text; top-level fields whose name or label doesn't
        // contain it are skipped. Empty: every field.
        std::string Filter;
        // Optional: the top-level fields drawn (editor automation asserts on them).
        std::vector<std::string>* Drawn = nullptr;
        // Optional (UX-2): the type's default values as JSON (a new instance serialized).
        // Double-clicking a number's label restores its default from here unless the field
        // names one with .Default().
        const json* Defaults = nullptr;
    };

    // Draws all fields; returns true when anything changed.
    bool DrawType( const Reflection::TypeInfo& InType, void* InInstance, Context& InContext );
    // Whether any field DrawType would show matches a lower-case search.
    bool AnyFieldMatches( const Reflection::TypeInfo& InType, void* InInstance, const std::string& InFilter );

    // Editor widgets for custom value types (Reflection::CustomTypeTraits<T>::Name), e.g. EntityHandle.
    // The drawer gets the label and the value pointer and returns true on change.
    // InField may be null (array elements).
    using CustomDrawer = std::function<bool( const char* InLabel, const Reflection::FieldInfo* InField, void* InValue )>;
    void RegisterCustomDrawer( const std::string& InCppName, CustomDrawer InDrawer );

    // Field decorators (UX-5): an extra row drawn right after a field of a reflected type, inside
    // the component's group (so a gesture on it is one undo step), for widgets that show what a
    // value means (a colour temperature bar under Light.Color, a falloff graph under Light.Range).
    // InInstance is the reflected object (a component: static_cast it from BaseComponent*). Return
    // true when the decorator changed any of InFieldsItEdits: they are then reported changed and
    // copied to the other selected instances like a normal edit. Decorators are skipped while
    // the inspector's field search is active. Several per field draw in registration order.
    using FieldDecorator = std::function<bool( void* InInstance, const Reflection::TypeInfo& InType )>;
    void RegisterFieldDecorator( const std::string& InTypeName, const std::string& InFieldName, std::vector<std::string> InFieldsItEdits, FieldDecorator InDecorator );
    // Same, with a fix-up run on each other selected instance after the changed fields are copied
    // to it, to keep a rule across fields the gesture left alone (UX-10: a capsule's Height never
    // under 2 x Radius when only one of the two was copied).
    using DecoratorFixUp = std::function<void( void* InInstance, const Reflection::TypeInfo& InType )>;
    void RegisterFieldDecorator( const std::string& InTypeName, const std::string& InFieldName, std::vector<std::string> InFieldsItEdits, FieldDecorator InDecorator, DecoratorFixUp InFixUp );

    // The unit-field description of a reflected number (unit, ranges, log, default), for
    // decorators that edit numbers.
    MeWidgets::NumberSpec NumberSpecFor( const Reflection::FieldInfo& InField );

    // Registers the built-in custom drawers (EntityHandle).
    void RegisterDefaultDrawers();

    // The script inspector's reference rows (installed into ScriptInspectorRows by the editor
    // app): the same entity and asset rows, on the GUID/path representations script fields store.
    bool EntityGuidRow( const char* InLabel, uint64_t& InOutGuid );
    bool AssetPathRow( const char* InLabel, std::string& InOutPath, const char* InExtensions );
}

#endif
