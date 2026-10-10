#pragma once
#include "Math/Vector3.h"
#include <array>
#include <vector>
#include <cstdint>
#include <string>

// Project-wide settings stored in Assets/Config/ProjectSettings.json (timing lives in Engine.cfg):
// the names of the 32 entity layers, which layers collide with which, gravity, the audio bus
// volumes and the navigation areas.
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
    // Per AudioBus (Master, Music, SFX, UI, Voice), 0..1.
    static constexpr int kAudioBusCount = 5;
    std::array<float, kAudioBusCount> BusVolumes = { 1.f, 1.f, 1.f, 1.f, 1.f };
    // The game's action map (.inputactions), loaded into the game Input at startup.
    std::string InputActions = "Assets/Config/Input.inputactions";
    // Navigation areas (NavAreas): names and traversal cost multipliers. Area 0 is "Walkable" and
    // area 1 "Not Walkable" (never on the navmesh); area 2 is the default for NavMeshLinks.
    static constexpr int kNavAreaCount = 16;
    std::array<std::string, kNavAreaCount> NavAreaNames;
    std::array<float, kNavAreaCount> NavAreaCosts;
    // Display label: the name, or "Area N" when unnamed.
    std::string GetNavAreaLabel( int InArea ) const;
    // Navigation agent types: the body sizes navmeshes are baked for. A surface bakes for one (or
    // for its own custom size) and agents walk the surfaces of theirs. Surfaces and agents store the
    // type 1-based (0 = custom / any).
    struct NavAgentType
    {
        std::string Name;
        float Radius = 0.5f;
        float Height = 2.f;
        float MaxClimb = 0.5f;      // step height
        float MaxSlope = 45.f;      // degrees
    };
    static constexpr int kMaxNavAgentTypes = 16;
    std::vector<NavAgentType> NavAgentTypes;
    // Type N (1-based), or null.
    const NavAgentType* GetNavAgentType( int InType ) const;

private:
    ProjectSettings();

    std::array<std::string, kLayerCount> m_layerNames;
    std::array<uint32_t, kLayerCount> m_collisionMasks;
};
