#pragma once
#include "Math/Vector3.h"
#include <array>
#include <cstdint>
#include <string>

// Project-wide settings stored in Assets/Config/ProjectSettings.json (timing lives in Engine.cfg):
// the names of the 32 entity layers, which layers collide with which, and gravity.
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

    // Physics layer collision matrix (symmetric). A mask is the set of layers a layer collides with.
    uint32_t GetCollisionMask( int InLayer ) const;
    bool DoLayersCollide( int InLayerA, int InLayerB ) const;
    void SetLayersCollide( int InLayerA, int InLayerB, bool InCollide );

    Vector3 Gravity = Vector3( 0.f, -9.81f, 0.f );

private:
    ProjectSettings();

    std::array<std::string, kLayerCount> m_layerNames;
    std::array<uint32_t, kLayerCount> m_collisionMasks;
};
