#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "JSON.h"
#include "ShaderGraph/Graph.h"

namespace ShaderGraph
{
    struct Diagnostic
    {
        enum class Severity : uint8_t
        {
            Error,
            Warning
        };

        Severity Level = Severity::Error;
        // 0: the graph as a whole.
        uint32_t NodeId = 0;
        std::string Message;
    };

    struct GeneratedFile
    {
        std::string Name;
        std::string Source;
        // "vertex", "fragment" or "varyings".
        std::string Kind;
    };

    // The parameter block (u_sgParams) and sampler layout the runtime binds.
    struct ParameterSlot
    {
        std::string PropertyId;
        std::string Name;
        ValueType Type = ValueType::Float;
        int Slot = 0;
        Vector4 Default;
    };

    struct TextureSlot
    {
        // Empty for textures that aren't properties (Texture Asset nodes, unconnected samplers).
        std::string PropertyId;
        std::string Name;
        ValueType Type = ValueType::Texture2D;
        int Slot = 0;
        std::string Asset;
        std::string DefaultTexture = "white";
    };

    // A constant the preview program reads from u_sgPreview[Slot] instead of inlining.
    struct PreviewConstant
    {
        uint32_t NodeId = 0;
        std::string Key;
        int Slot = 0;
        Vector4 Value;
    };

    // One node output the preview program can display.
    struct PreviewOutput
    {
        uint32_t NodeId = 0;
        std::string Pin;
        ValueType Type = ValueType::Float;
        // The value of u_sgPreviewSelect.x that shows it.
        int Index = 0;
        // Reads position/normal/view inputs: show on a sphere rather than a flat swatch.
        bool Spatial = false;
    };

    struct CompileOptions
    {
        // Project-relative path of the graph (comments, manifest).
        std::string GraphPath;
        // Build the editor preview program instead of the target's files.
        bool Preview = false;
    };

    struct CompileResult
    {
        bool Success = false;
        std::vector<Diagnostic> Diagnostics;
        std::vector<GeneratedFile> Files;
        // Material.json: target, programs, render state, parameter and texture layout.
        json Manifest;
        // Resolved type of every pin, keyed by (node, "in:Pin" / "out:Pin").
        std::map<std::pair<uint32_t, std::string>, ValueType> PinTypes;
        std::vector<ParameterSlot> Parameters;
        std::vector<TextureSlot> Textures;
        // Preview builds only.
        std::vector<PreviewConstant> PreviewConstants;
        std::vector<PreviewOutput> PreviewOutputs;

        bool HasErrors() const;
        const GeneratedFile* FindFile( const std::string& InName ) const;
        // Errors and warnings as text ("error: Add (#4): ...").
        std::string FormatDiagnostics( const Graph& InGraph ) const;
    };

    // Shared by every shader graph program (bgfx uniforms are global by name, so the layout is fixed).
    constexpr int kMaxParameters = 16;
    constexpr int kMaxTextures = 8;
    constexpr int kMaxPreviewConstants = 64;

    CompileResult Compile( const Graph& InGraph, const CompileOptions& InOptions = {} );

    // Writes InResult's files and Material.json into InDirectory (created if missing). Unchanged
    // files are left alone so hot reload only recompiles what changed. Returns false on IO errors.
    bool WriteGeneratedFiles( const CompileResult& InResult, const std::string& InDirectory, std::string* OutError = nullptr );

    // Compiles the graph at InGraphPath (project-relative or absolute) and writes <Name>.sg/.
    CompileResult CompileAndExport( const std::string& InGraphPath, const Graph& InGraph );

    // Maps shaderc output back to nodes: generated variables are named n<id>_<pin>, so any error
    // line that mentions one is attributed to that node.
    std::vector<Diagnostic> MapShaderErrors( const std::string& InCompilerLog );
}
