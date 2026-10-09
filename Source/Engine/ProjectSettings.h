#pragma once
#include <array>
#include <string>

// Project-wide settings stored in Assets/Config/ProjectSettings.json (timing lives in Engine.cfg).
// Currently: the names of the 32 entity layers.
class ProjectSettings
{
public:
    static constexpr int kLayerCount = 32;

    static ProjectSettings& Get();

    void Load();
    void Save() const;

    const std::string& GetLayerName( int InLayer ) const;
    void SetLayerName( int InLayer, const std::string& InName );

    // Display label: the name, or "Layer N" when unnamed.
    std::string GetLayerLabel( int InLayer ) const;

private:
    ProjectSettings();

    std::array<std::string, kLayerCount> m_layerNames;
};
