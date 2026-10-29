#pragma once
#include "Engine/HardwareBudget.h"
#include "JSON.h"
#include <string>
#include <vector>

class Entity;
class World;

// Look presets (LOOK-S0): a project's look in one pick, Realistic, Stylized-Lit, Toon,
// Flat/Unlit, Pixel-2D, PS1, N64, Retro-PC. Data in Engine/Assets/Config/LookPresets.json, with a
// project's Assets/Config/LookPresets.json adding or replacing presets by Name. A preset carries:
//   Camera / Post: partial Camera and PostProcess JSON merged onto cameras (render scale, pixel
//                  perfect, pass switches; tonemapper, AA, bloom, outlines, grade...)
//   Material:      the material for meshes still on the stock StandardMaterial (applied on request)
//   Import:        texture import defaults for new images (Format "None", MIPs false: pixel art)
//   Caps / Lite:   a project-wide cap layer over the hardware profile and quality preset; Lite is
//                  the low-end pipeline (no SSAO, shadows or image-based lighting)
// ProjectSettings::Look names the project's preset: its Import and Caps apply at startup, and
// Project Settings > Look (or the look-apply edscript command) applies Camera / Post / Material
// to the open scene.
class LookPresets
{
public:
    struct Preset
    {
        std::string Name;
        std::string Description;
        json Camera = json::object();
        json Post = json::object();
        json Material;      // null = keep materials
        json Import = json::object();
        HardwareProfile::FeatureCaps Caps;
        bool Lite = false;
    };

    static LookPresets& Get();
    void Load();
    // Parses a presets file's JSON over what's loaded (same Name replaces). False if it isn't one.
    bool AddPresets( const json& InRoot );
    const std::vector<Preset>& GetAll() const { return m_presets; }
    const Preset* Find( const std::string& InName ) const;

    // The Lite pipeline's caps.
    static HardwareProfile::FeatureCaps LiteCaps();
    // The preset's whole cap layer (its Caps, with Lite's when set).
    static HardwareProfile::FeatureCaps CapsOf( const Preset& InPreset );

    // One entity: Camera + PostProcess on a camera (PostProcess added when missing), and with
    // InMaterials the preset's Material on a Mesh still on StandardMaterial. True when it changed.
    static bool ApplyToEntity( const Preset& InPreset, Entity& InEntity, bool InMaterials );
    // Every entity of a world; returns how many changed.
    static int ApplyToWorld( const Preset& InPreset, World& InWorld, bool InMaterials );
    // The project-wide parts: texture import defaults and the cap layer (null = none).
    static void ApplyProjectDefaults( const Preset* InPreset );

private:
    std::vector<Preset> m_presets;
};
