#pragma once
#include "Math/Vector3.h"
#include <array>
#include <vector>
#include <cstdint>
#include <string>

// Project-wide settings stored in Assets/Config/ProjectSettings.json (timing lives in Engine.cfg):
// the names of the 32 entity layers, which layers collide with which, gravity, the audio bus
// volumes, the navigation areas and the hardware profile the project budgets for.
class ProjectSettings
{
public:
    static constexpr int kLayerCount = 32;

    static ProjectSettings& Get();

    // Local instances are allowed (tests round-trip them); the game reads the singleton.
    ProjectSettings();

    void Load();
    void Save() const;
    // The project file lives at Assets/Config/ProjectSettings.json; tests point these elsewhere.
    void LoadFrom( const std::string& InPath );
    void SaveTo( const std::string& InPath ) const;
    // The settings as the file's JSON, and back (ED-13: Project Settings undo snapshots them).
    std::string Serialize() const;
    bool Deserialize( const std::string& InJson );
    // Where Save writes ("" = the project's file). Editor flows point it at a scratch file.
    void SetSavePath( const std::string& InPath ) { m_savePath = InPath; }
    // Bumped by every save, so the editor can notice a change and record it for undo.
    uint32_t GetSaveCount() const { return m_saveCount; }

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
    // Shared across streamed terrain assets in a world, including pending/retiring work.
    int TerrainCpuBudgetMiB = 256, TerrainGpuBudgetMiB = 128, TerrainCollisionBudgetMiB = 512;
    int TerrainMaxLoads = 2;
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
    // The hardware profile the project is held to ("" = none) and whether its feature caps apply
    // (see HardwareBudget). --hardware-profile / --enforce-profile / --no-profile-enforcement override them for a run.
    std::string HardwareProfileName;
    bool EnforceHardwareProfile = false;
    // Product identity: names the user:// pref dir of shipped builds, so changing Org or
    // Name orphans players' saved state.
    std::string ProductOrg = "MitchEngine";
    std::string ProductName = "MitchGame";
    std::string ProductVersion = "0.1.0";
    // Window/taskbar icon, resolved through the VFS ("" = the engine default).
    std::string ProductIcon;
    // The scene a game build boots when no --scene overrides it ("" = none; the editor
    // never auto-boots it).
    std::string StartupScene;
    // PLAY-5: documents shown in turn over the startup scene when a game build boots (never in the
    // editor or unattended runs), and the document async scene loads show (LoadingScreen).
    struct SplashScreen
    {
        std::string Document;
        float Seconds = 2.f;
    };
    std::vector<SplashScreen> Splash;
    // The project's look preset (LookPresets, LOOK-S0): its texture import defaults and cap layer
    // apply at startup; Project Settings > Look applies its camera, post and materials to a scene.
    // Empty = none.
    std::string Look;
    static constexpr const char* kDefaultLoadingDocument = "Assets/UI/Loading/Loading.rml";
    std::string LoadingDocument = kDefaultLoadingDocument;
    // Build settings, consumed by the SHIP-2 cooker: the scenes a shipped build includes,
    // assets always packed even when nothing references them, and path prefixes to exclude.
    std::vector<std::string> BuildScenes;
    std::vector<std::string> BuildAlwaysInclude;
    std::vector<std::string> BuildExclude;
    // Build Game profiles (SHIP-3, Engine/Docs/Building.md): each makes one shippable build of
    // the project for the machine building it.
    struct BuildProfile
    {
        std::string Name = "Default";
        // "Retail" ships; "Release" keeps the tools code (automation, cooking); "Debug".
        std::string Configuration = "Retail";
        // Where the build goes ("" = Builds/<Name>). Replaced each build.
        std::string OutputDirectory;
        // The scenes it ships (empty = Build.Scenes and the StartupScene).
        std::vector<std::string> Scenes;
        // The content as one Game.mpak (true) or as a loose cooked tree.
        bool Pak = true;
        // Also packs the output into one archive next to it (.tar.gz; .zip on Windows).
        bool Archive = false;
    };
    std::vector<BuildProfile> BuildProfiles;
    // The profile named InName ("" = the first); null when there is none.
    const BuildProfile* FindBuildProfile( const std::string& InName ) const;
    // One profile as a JSON file (the shape of a Build.Profiles entry): the editor hands the
    // profile it shows to a Havana --build-game run this way, saved or not.
    static bool ParseBuildProfile( const std::string& InJson, BuildProfile& OutProfile );
    static bool ReadBuildProfile( const std::string& InPath, BuildProfile& OutProfile );
    static bool WriteBuildProfile( const std::string& InPath, const BuildProfile& InProfile );

private:
    std::array<std::string, kLayerCount> m_layerNames;
    std::array<uint32_t, kLayerCount> m_collisionMasks;
    mutable uint32_t m_saveCount = 0;
    std::string m_savePath;
};
