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
// The MitchTree editor (the open document, else the most recent one):
//   tree-open <path.mtree> / tree-close <path.mtree>
//   tree-assert-open <path.mtree> | <0|1>    whether it has a MitchTree window (any way it was opened)
//   tree-edit <field> | <json>                one undoable edit of the document's recipe
//   tree-preset <Preset> / tree-seed          replace with a preset / a new seed (undoable)
//   tree-undo / tree-redo / tree-save
//   tree-page <page> [| level]                show a side panel page (its title; Branches by level from 1)
//   tree-level <add|remove|toggle> [| level]  branch level edits (undoable; the deepest level by default)
//   tree-assert-levels <levels> [| <on>]      branch level count (and how many are switched on)
//   tree-preview <wind|lod|wireframe|ground|yaw|pitch|distance|frame|scene|sun|sun-height> | <value>
//   tree-wait-preview                         retries until generated and shown for a few frames
//   tree-assert-stats <min> | <max>           full-detail triangles in range, each level smaller
//   tree-assert-dirty <0|1>
//   tree-assert-lod <level>                   the preview drew that level last frame
//   tree-screenshot <path.png>                the preview's image (retries until written)
namespace TreeAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry );
}
#endif
