#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>

// Editor automation (`tree-*` edscript commands) for MitchTree assets:
//   tree-new <Preset> | <path.mtree>          write a preset recipe
//   tree-place <path.mtree> | <Name> | x | z  undoable placement as a Model, like a browser drop
//   tree-wait <Name>                          retries until the model has loaded and expanded
//   tree-assert <Name> | <parts>              a generated tree with that many parts, drawn instanced
//   tree-set <path.mtree> | <field> | <json>  edits a recipe field, saves and regenerates instances
//   tree-mark <Name> / tree-assert-regenerated <Name>   the instance now uses a fresh generation
//   tree-foliage <path.mtree>                 adds the tree to the selected terrain's foliage palette
namespace TreeAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry );
}
#endif
