#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "JSON.h"
#include "ShaderGraph/Graph.h"
#include "ShaderGraph/NodeDef.h"

namespace ShaderGraph
{
    // How a builtin is computed in one stage of a target's template.
    struct BuiltinSource
    {
        // Declares sg_<Name> (may use other builtins listed in Depends, and the template's locals).
        std::string Code;
        std::vector<Builtin> Depends;
    };

    // One generated file: a template with {{TOKENS}} replaced.
    struct TemplateFile
    {
        enum class Kind : uint8_t
        {
            Vertex,
            Fragment,
            Varyings
        };

        // File name inside <Graph>.sg/ ("Forward.vert").
        std::string Output;
        // Template under Engine/Assets/Shaders/ShaderGraph/Templates/.
        std::string Template;
        Kind FileKind = Kind::Fragment;
        // The output-node pins this file's body computes (empty: every pin of its stage).
        std::vector<std::string> Pins;
        // #defines written into {{DEFINES}} (in addition to the target's).
        std::vector<std::string> Defines;
        // Extra {{TOKEN}} replacements.
        std::map<std::string, std::string> Tokens;
    };

    // Facts about the graph the target's file list depends on.
    struct GraphFacts
    {
        std::function<bool( const std::string& )> IsOutputConnected;
    };

    class TargetDef
    {
    public:
        virtual ~TargetDef() = default;

        Target Id = Target::Lit;
        std::string DisplayName;
        std::string Description;
        bool HasSceneColor = false;
        bool HasSceneDepth = false;

        // The output node's inputs for these settings.
        virtual void BuildOutputPins( const TargetSettings& InSettings, std::vector<PinDef>& OutPins ) const = 0;
        // Null when InBuiltin isn't available in that stage of this target.
        virtual const BuiltinSource* GetBuiltin( Builtin InBuiltin, Stage InStage ) const = 0;
        // The generated files, and the manifest's "Programs" ({name: [vertex, fragment]} file stems).
        virtual void GetFiles( const TargetSettings& InSettings, const GraphFacts& InFacts, std::vector<TemplateFile>& OutFiles, json& OutPrograms ) const = 0;
        // #defines for every file of the target.
        virtual void GetDefines( const TargetSettings& InSettings, std::vector<std::string>& OutDefines ) const;
    };

    // Null for targets without a template yet.
    const TargetDef* GetTargetDef( Target InTarget );

    // Builtins in the editor's preview shader (Preview.frag.sgt): a flat swatch or an analytic
    // sphere, so every builtin exists there (scene and particle inputs are stand-ins).
    const BuiltinSource* GetPreviewBuiltin( Builtin InBuiltin );

    // Engine/Assets/Shaders/ShaderGraph/Templates
    std::string GetTemplateDirectory();
}
