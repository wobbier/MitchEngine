#pragma once
#include <string>
#include <vector>

// bgfx shaderc, run as a child process. Thread safe: hot reload, the shader graph previews and the
// unit tests call it from worker threads.
namespace Moonlight::ShaderCompiler
{
    enum class Stage
    {
        Vertex,
        Fragment
    };

    // A shaderc --platform / -p pair.
    struct Profile
    {
        std::string Platform;
        std::string Name;
    };

    struct Request
    {
        std::string SourcePath;
        std::string OutputPath;
        // Empty: <source dir>/<source base name>.var, the engine's convention.
        std::string VaryingDefPath;
        Stage ShaderStage = Stage::Fragment;
        // Empty: the running renderer's profile (GetRunningProfile).
        Profile TargetProfile;
        // Engine/Assets/Shaders is always searched.
        std::vector<std::string> IncludeDirs;
        // Writes <output>.d for include tracking.
        bool WriteDepends = false;
    };

    struct Result
    {
        bool Success = false;
        // shaderc's combined stdout/stderr.
        std::string Log;
    };

    Result Compile( const Request& InRequest );

    // The profile matching Moonlight::GetPlatformString() on this OS.
    Profile GetRunningProfile();

    // Profiles shaderc can produce on this OS (the dx11 profiles need Windows' D3D compiler).
    std::vector<Profile> GetPortableProfiles();

    // The vertex/fragment stage for a .vert / .frag path.
    Stage StageFromPath( const std::string& InPath );
}
