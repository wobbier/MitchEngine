#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <string>

// Editor automation (".edscript") commands for shader graphs, all prefixed "sg-":
//   sg-new <Target> | <path>          create a graph file and open it
//   sg-open <path> / sg-close <path>
//   sg-add <alias> | <NodeType> [| <settings json>]
//   sg-add-property <alias> | <Type> | <Name>   (a blackboard property and a node reading it)
//   sg-link <alias>:<Pin> | <alias>:<Pin>       ("out" is the output node)
//   sg-unlink <alias>:<Pin>
//   sg-set <alias> | <Setting> | <json>     sg-default <alias> | <Pin> | <json number or array>
//   sg-target <Target>                      sg-delete <alias>
//   sg-compile, sg-save, sg-undo, sg-redo, sg-frame
//   sg-assert-compiled, sg-assert-error <alias>, sg-assert-nodes <n>, sg-assert-links <n>
//   sg-assert-code <file> | <text>, sg-assert-no-code <file> | <text>
//   sg-assert-dirty <true|false>, sg-assert-compiles <n> (compile count since the last mark),
//   sg-mark-compiles
namespace ShaderGraphAutomation
{
    // False with OutError set on failure.
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError );
    // Clears aliases (a new script run).
    void Reset();
}
#endif
