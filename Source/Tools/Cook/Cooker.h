#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// The dependency cooker (SHIP-2, Engine/Docs/Cooking.md): turns a project's scenes into a cooked
// tree that runs without sources. Dependencies come from three places:
//   - a static walk of the scenes and the prefabs, scenes and shader graphs they name;
//   - recorded runs (--record-deps; the cooker records each scene through the game itself when
//     it has a game executable), which catch string paths in scripts and engine code;
//   - the project's config files, its Build.AlwaysInclude and the engine's shader stages.
// Every dependency ships in its runtime form: textures as .dds, models as .assbin, shader stages
// as the target platform's binaries, everything else as is. Sources, .meta files, build artifacts
// and editor-only content stay behind; AssetRegistry.json (Resource/CookedAssets.h) carries what
// the runtime still needs from them.
namespace Cook
{
    enum class Platform
    {
        Linux,
        Windows,
        MacOS
    };

    Platform HostPlatform();
    // "linux", "windows", "macos".
    std::string PlatformName( Platform InPlatform );
    bool ParsePlatform( const std::string& InName, Platform& OutPlatform );

    // The shader binaries a platform's renderer loads (Vulkan on Linux, D3D11 on Windows, Metal on
    // macOS): shaderc's --platform and -p, and the "<stage>.<Binary>.bin" name.
    struct ShaderProfile
    {
        std::string CompilerPlatform;
        std::string Profile;
        std::string Binary;
    };
    ShaderProfile GetShaderProfile( Platform InPlatform );

    // How an asset ships, by its path.
    enum class AssetKind
    {
        Texture,        // as <path>.dds
        Model,          // as <path>.assbin (and <path>.vertexcache when there is one)
        ShaderStage,    // as <path>.<profile binary>.bin
        Data,           // as is
        Stripped        // never: metas, build inputs, editor-only content, dev caches
    };
    AssetKind Classify( const std::string& InLogicalPath );

    // The files InAsset ships as on InPlatform (empty when stripped; a model's optional vertex
    // cache is added by the cooker when it exists).
    std::vector<std::string> ShippedFiles( const std::string& InAsset, Platform InPlatform );

    // The asset a recorded read belongs to: "X.png.dds" -> "X.png", "X.frag.spirv.bin" ->
    // "X.frag", "X.fbx.assbin" -> "X.fbx", "X.png.meta" -> "X.png". Empty when the read is an
    // asset of its own.
    std::string OwningAsset( const std::string& InRead );

    // The overlay spelling of an existing file a scene names ("Engine/Assets/X" -> "Assets/X");
    // empty when it doesn't resolve to a file.
    std::string CanonicalAsset( const std::string& InPath );

    // Adds the assets a JSON asset (scene, prefab, shader graph manifest) names to InOutAssets,
    // recursing into the JSON assets among them (and JSON embedded in strings, like script fields).
    void CollectStaticDependencies( const std::string& InAsset, std::set<std::string>& InOutAssets );

    struct Request
    {
        // Scenes to cook (logical paths). Empty: the project's Build.Scenes and StartupScene.
        std::vector<std::string> Scenes;
        // Recorded runs (--record-deps files) to ship as well.
        std::vector<std::string> DependencyFiles;
        // The cooked tree. Replaced each cook; only an earlier cook or an empty directory is cleared.
        std::string OutputDirectory = ".tmp/Cook";
        // Also packs the tree into this .mpak.
        std::string PakFile;
        Platform TargetPlatform = HostPlatform();
        // Records each scene through this game executable first (--record-deps); empty: no recording.
        std::string GameExecutable;
        int RecordFrames = 240;
    };

    struct Report
    {
        bool Success = false;
        size_t FileCount = 0;
        uint64_t ByteCount = 0;
        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
    };

    // Tools builds: cooks missing or stale twins in place, then writes the tree.
    Report Run( const Request& InRequest );

    // Tools builds: cooks every texture, model and shader stage under InRoots in place for
    // InPlatform (the asset browser's Build Assets). Returns how many failed.
    int CookInPlace( const std::vector<std::string>& InRoots, Platform InPlatform );
}
