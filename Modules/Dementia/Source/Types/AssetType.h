#pragma once
#include <string>

enum class AssetType : unsigned int
{
    Unknown = 0,
    Audio,
    Level,
    Model,
    Prefab,
    Shader,
    ShaderGraph,
    Material,
    Texture,
    UI,
    Code,
    CS,
    Count
};

namespace {
    std::string AssetTypeToString( AssetType InType )
    {
        switch( InType )
        {
        case AssetType::Unknown:
            return "Unknown";
        case AssetType::Texture:
            return "Texture";
        case AssetType::Audio:
            return "Audio";
        case AssetType::Model:
            return "Model";
        case AssetType::Level:
            return "Level";
        case AssetType::Shader:
            return "Shader";
        case AssetType::ShaderGraph:
            return "ShaderGraph";
        case AssetType::Prefab:
            return "Prefab";
        case AssetType::UI:
            return "UI";
        case AssetType::Code:
            return "Code";
        case AssetType::CS:
            return "Script";
        case AssetType::Material:
            return "Material";
        case AssetType::Count:
        default:
            return "";
        }
    }
}