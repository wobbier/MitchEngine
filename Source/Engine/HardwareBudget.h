#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <cstdint>
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
    // Render resolution: the output of the game's main camera.
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
        bool TemporalAA = true;
        bool EnvironmentLighting = true;
        bool Instancing = true;
    };
    FeatureCaps Caps;

    float GetCpuBudgetMs() const;
    float GetGpuBudgetMs() const;
    // Reads the keys InJson has over the current values.
    void Deserialize( const json& InJson );
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
    // Adds the profiles of a profiles document ({ "Profiles": [ ... ] }), replacing same-named
    // ones; "Extends" starts from a profile added before. Returns false if it isn't one.
    bool AddProfiles( const json& InRoot, const std::string& InSource );

    const std::vector<HardwareProfile>& GetProfiles() const { return m_profiles; }
    // Null when no profile is chosen or the chosen name isn't defined.
    const HardwareProfile* GetActiveProfile() const;
    const std::string& GetActiveName() const { return m_activeName; }
    // Picks a profile ("" = none) and re-applies the caps. Saving ProjectSettings is the caller's.
    void SetActiveProfile( const std::string& InName );
    bool IsEnforced() const { return m_enforced; }
    void SetEnforced( bool InEnforced );

    // Once a frame, after rendering.
    void Update();
    // The active profile's budgets that have a limit (empty without a profile).
    const std::vector<Line>& GetLines() const { return m_lines; }
    int GetOverCount() const;
    void ResetPeaks();

    // The profile, whether it's enforced, and every line's used / peak / limit / status.
    json ToJson() const;

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
    void ApplyCaps();
    void ReloadTextures();
    void RebuildLines();
    void SampleMemory();

    BGFXRenderer* m_renderer = nullptr;
    std::vector<HardwareProfile> m_profiles;
    std::string m_activeName;
    bool m_enforced = false;

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
