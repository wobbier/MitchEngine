#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class BGFXRenderer;
class Path;
struct ImDrawList;

// A target machine to budget for ("Xbox 360", "2004 PC", "Steam Deck"): a frame-time target,
// memory budgets, per-frame draw limits and the GPU features it has. Zero means no limit.
// Profiles are data: the engine's Engine/Assets/Config/HardwareProfiles.json, then the project's
// Assets/Config/HardwareProfiles.json (a profile with an existing name replaces it, and "Extends"
// starts a profile from another one's values).
struct HardwareProfile
{
    std::string Name;
    std::string Description;
    std::string ShaderModel;            // informational

    // Frame time. CpuMs / GpuMs default to the frame target (1000 / TargetFps).
    float TargetFps = 0.f;
    float CpuMs = 0.f;
    float GpuMs = 0.f;
    // Render resolution: the game main camera's render size (output x render scale). Enforced, it caps
    // every camera's internal pixel count (LOOK-S1).
    int Width = 0;
    int Height = 0;
    // Memory, MiB. A unified-memory target has one pool: process memory plus GPU memory count
    // against RamMiB, and VramMiB is ignored.
    bool UnifiedMemory = false;
    int RamMiB = 0;
    int VramMiB = 0;
    int TextureMiB = 0;
    int MeshMiB = 0;
    int RenderTargetMiB = 0;
    int AudioMiB = 0;
    // Per frame: mesh draws and triangles for the game's cameras and their shadows, and lights.
    int MaxDrawCalls = 0;
    int MaxTriangles = 0;
    int MaxLights = 0;

    // GPU features. Enforcing the profile turns off what the target lacks and caps the sizes, so
    // the game shows the target's look; otherwise they're only reported.
    struct FeatureCaps
    {
        int MaxTextureSize = 0;         // texels on a texture's longest side
        int MaxShadowResolution = 0;    // texels per cascade, spot light or point light face
        bool Shadows = true;
        bool AmbientOcclusion = true;
        bool Bloom = true;
        bool DepthOfField = true;
        bool VolumetricFog = true;
        int MaxVolumetricSteps = 0;     // raymarch samples per pixel the target affords (0 = no cap)
        bool TemporalAA = true;
        bool EnvironmentLighting = true;
        bool Instancing = true;
        float MaxRenderScale = 0.f;     // cameras' render scale ceiling (0 = no cap)
    };
    FeatureCaps Caps;

    // The stricter of two cap sets: a feature stays on only if both have it, and a size takes the
    // smaller nonzero limit (0 = no limit).
    static FeatureCaps Combine( const FeatureCaps& InA, const FeatureCaps& InB );

    float GetCpuBudgetMs() const;
    float GetGpuBudgetMs() const;
    // Reads the keys InJson has over the current values.
    void Deserialize( const json& InJson );
};


// A player-facing graphics quality level ("Low" .. "Ultra"): feature caps the player picks in
// the game's settings (PlayerSettings "Graphics.Quality"). Presets are data, in the profiles
// files' "QualityPresets" list (engine first, then the project's; same Name replaces, "Extends"
// starts from an earlier preset). The caps reuse the profile enforcement: under an enforced
// profile the stricter of the two applies.
struct QualityPreset
{
    std::string Name;
    std::string Description;
    HardwareProfile::FeatureCaps Caps;
};


// NEXT-11: what one loaded asset costs. Bytes estimate what it holds in memory: a texture's
// compiled image at the size it uploaded (a render texture: its colour target), a model's vertex
// and index buffers, a sound's file. Other resources (materials, shaders, data) count 0.
struct AssetCost
{
    std::string Path;
    std::string Kind;           // "Texture", "Render Texture", "Model", "Sound" or "Other"
    uint64_t Bytes = 0;
    std::string Detail;         // "2048x2048, 12 mips", "84,120 vertices, 120,000 triangles"
    long References = 0;        // holders besides the resource cache
};


// Holds the game to the active HardwareProfile. Every frame, once the frame's statistics are in,
// it measures the frame and memory against the profile's budgets and logs a warning when one stays
// over for a second. Enforcing the profile applies its feature caps to the renderer and its
// texture size cap to texture uploads (loaded textures reload). Main thread only.
class HardwareBudget
{
public:
    enum class Budget : uint8_t
    {
        Cpu,
        Gpu,
        DrawCalls,
        Triangles,
        Lights,
        Resolution,
        Memory,
        GpuMemory,
        Textures,
        Meshes,
        RenderTargets,
        Audio,
        Count
    };

    enum class Unit : uint8_t
    {
        Milliseconds,
        Bytes,
        Count,
        Pixels
    };

    enum class Status : uint8_t
    {
        Ok,
        Near,       // 90% of the limit or more
        Over
    };

    struct Line
    {
        Budget Id = Budget::Count;
        const char* Name = "";
        const char* Tooltip = "";
        Unit Units = Unit::Count;
        double Used = 0.0;
        double Limit = 0.0;
        double Peak = 0.0;          // highest Used since the profile was chosen or ResetPeaks
        Status State = Status::Ok;
        uint32_t Width = 0;         // Pixels lines: the measured and the target size
        uint32_t Height = 0;
        uint32_t LimitWidth = 0;
        uint32_t LimitHeight = 0;
    };

    static HardwareBudget& Get();

    // Loads the profiles and applies the project's choice, or --hardware-profile "Name" and
    // --enforce-profile for this run.
    void Init( BGFXRenderer& InRenderer );
    // Re-reads both profile files (the active profile keeps its name).
    void ReloadProfiles();
    // Adds the profiles of a profiles document ({ "Profiles": [ ... ], "QualityPresets": [ ... ] }),
    // replacing same-named ones; "Extends" starts from one added before. Returns false if it isn't one.
    bool AddProfiles( const json& InRoot, const std::string& InSource );

    const std::vector<HardwareProfile>& GetProfiles() const { return m_profiles; }
    // Null when no profile is chosen or the chosen name isn't defined.
    const HardwareProfile* GetActiveProfile() const;
    const std::string& GetActiveName() const { return m_activeName; }
    // Picks a profile ("" = none) and re-applies the caps. Saving ProjectSettings is the caller's.
    void SetActiveProfile( const std::string& InName );
    bool IsEnforced() const { return m_enforced; }
    void SetEnforced( bool InEnforced );

    // Player quality presets, in file order (lowest first by convention).
    const std::vector<QualityPreset>& GetQualityPresets() const { return m_qualityPresets; }
    const QualityPreset* FindQualityPreset( const std::string& InName ) const;
    // Picks a preset ("" = none: no player caps) and re-applies the caps. False when InName
    // isn't defined (the choice is kept, so a preset added by a later reload takes effect).
    bool SetQualityPreset( const std::string& InName );
    // The project look's cap layer (LookPresets: its Caps, plus the Lite pipeline), combined like
    // the quality preset: the stricter cap wins. Default caps = none.
    void SetLookCaps( const HardwareProfile::FeatureCaps& InCaps );
    // The player's graphics settings (PlayerSettings Graphics.RenderScale / DynamicResolution /
    // Upscale; ME-275): passed to the renderer with the caps. InUpscale -1 keeps each camera's.
    void SetPlayerGraphics( float InRenderScale, bool InDynamicResolution, int InUpscale );
    const std::string& GetQualityPresetName() const { return m_qualityName; }
    // What ApplyCaps hands the renderer: the enforced profile's caps and the quality preset's.
    HardwareProfile::FeatureCaps GetEffectiveCaps() const;

    // Once a frame, after rendering.
    void Update();
    // The active profile's budgets that have a limit (empty without a profile).
    const std::vector<Line>& GetLines() const { return m_lines; }
    int GetOverCount() const;
    void ResetPeaks();

    // The profile, whether it's enforced, and every line's used / peak / limit / status.
    json ToJson() const;

    // NEXT-11: how many times faster this machine is than the target. The measured CPU and GPU
    // frame times are multiplied by it before they meet the budget, so a fast dev machine reads
    // like the target (1 = as measured; --host-speed <factor>, the editor's Hardware tab).
    void SetHostSpeedFactor( float InFactor );
    float GetHostSpeedFactor() const { return m_hostSpeedFactor; }

    // NEXT-11: every loaded asset's estimated cost, most expensive first.
    static std::vector<AssetCost> CollectAssetCosts();
    // { "Total": bytes, "Kinds": { "Texture": { "Bytes", "Count" }, ... }, "Assets": [ the first
    // InTop costs ] }
    static json AssetCostsToJson( const std::vector<AssetCost>& InCosts, size_t InTop = SIZE_MAX );
    // "1.5 MiB", "820 KiB", "312 B".
    static std::string FormatBytes( uint64_t InBytes );

    // "12.4 ms", "245.0 MiB", "1,520" (pixel counts read "0.92 MP").
    static std::string FormatValue( const Line& InLine, double InValue );
    // The line's used value and limit; resolutions read "1280x720".
    static std::string FormatUsed( const Line& InLine );
    static std::string FormatLimit( const Line& InLine );
    static const char* GetStatusName( Status InStatus );

#if USING( ME_IMGUI )
    // A light-on-dark box at (InX, InY), or with its right edge at InX: the profile's name and its
    // lines as "used / limit", coloured by status. InProblemsOnly keeps the lines that are near or
    // over. Returns its height.
    float DrawOverlay( ImDrawList* InDrawList, float InX, float InY, bool InProblemsOnly, bool InAlignRight = false ) const;
#endif

    static constexpr const char* kEngineProfilesPath = "Engine/Assets/Config/HardwareProfiles.json";
    static constexpr const char* kProjectProfilesPath = "Assets/Config/HardwareProfiles.json";

private:
    HardwareBudget() = default;

    void LoadProfilesFile( const Path& InPath );
    bool ParseProfiles( const json& InRoot, const std::string& InSource );
    void ParseQualityPresets( const json& InList, const std::string& InSource );
    void ApplyCaps();
    void ReloadTextures();
    void RebuildLines();
    void SampleMemory();

    BGFXRenderer* m_renderer = nullptr;
    std::vector<HardwareProfile> m_profiles;
    std::string m_activeName;
    bool m_enforced = false;
    float m_hostSpeedFactor = 1.f;
    std::vector<QualityPreset> m_qualityPresets;
    std::string m_qualityName;
    HardwareProfile::FeatureCaps m_lookCaps;
    float m_playerRenderScale = 1.f;
    bool m_playerDynamicResolution = false;
    int m_playerUpscale = -1;

    std::vector<Line> m_lines;
    // Per line: when it went over (seconds, < 0 while under) and whether that was logged.
    struct LineWatch
    {
        double OverSince = -1.0;
        bool Warned = false;
    };
    std::vector<LineWatch> m_watches;
    double m_smoothedGpu = 0.0;
    // Process and audio memory are sampled a few times a second.
    double m_nextMemorySample = 0.0;
    uint64_t m_processBytes = 0;
    int64_t m_audioBytes = 0;
};
