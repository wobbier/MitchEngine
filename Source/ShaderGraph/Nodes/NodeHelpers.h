#pragma once
#include "ShaderGraph/NodeDef.h"

// Shorthands for the node tables in Nodes/*.cpp.
namespace ShaderGraph::Helpers
{
    // A fixed-type pin.
    inline PinDef Pin( const char* InName, ValueType InType, const Vector4& InDefault = Vector4() )
    {
        return PinDef( InName, InType, InDefault );
    }

    // A dynamic-vector pin (group 0 unless given): Float until something wider is connected.
    inline PinDef Dyn( const char* InName, float InDefault = 0.f, int InGroup = 0 )
    {
        return PinDef( InName, ValueType::Float, Vector4( InDefault ) ).Group( InGroup );
    }

    inline SettingDef EnumSetting( const char* InName, std::vector<std::string> InOptions, const std::string& InDefault, const char* InTooltip = "" )
    {
        SettingDef setting;
        setting.Name = InName;
        setting.Kind = SettingKind::Enum;
        setting.Options = std::move( InOptions );
        setting.Default = InDefault;
        setting.Tooltip = InTooltip;
        return setting;
    }

    inline SettingDef MakeSetting( const char* InName, SettingKind InKind, json InDefault, const char* InTooltip = "" )
    {
        SettingDef setting;
        setting.Name = InName;
        setting.Kind = InKind;
        setting.Default = std::move( InDefault );
        setting.Tooltip = InTooltip;
        return setting;
    }

    constexpr uint32_t TargetBit( Target InTarget )
    {
        return 1u << static_cast<uint32_t>( InTarget );
    }

    // Surfaces drawn as meshes (Lit/Unlit) and the surface-like targets.
    constexpr uint32_t kMeshTargets = ( 1u << static_cast<uint32_t>( Target::Lit ) ) | ( 1u << static_cast<uint32_t>( Target::Unlit ) );
}
