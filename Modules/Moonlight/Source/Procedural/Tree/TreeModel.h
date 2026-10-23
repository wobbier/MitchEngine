#pragma once
#include "Graphics/Texture.h"
#include "Pointers.h"
#include "TreeGenerator.h"
#include <functional>
#include <string>
#include <vector>

namespace Moonlight
{
    class MeshData;
}

// GPU meshes for a generated tree, shared by `.mtree` model loads and the MitchTree editor's preview.
namespace MitchTree
{
    // A part's texture: a generated channel of the recipe ("bark-color", see TextureChannels) when
    // InGenerated, else one of the recipe's authored map paths. Null leaves the slot empty.
    using TextureSource = std::function<SharedPtr<Moonlight::Texture>( const std::string& InChannelOrMap, bool InGenerated, Moonlight::WrapMode InWrap )>;

    // One mesh per part ("Bark", "Leaves"): its levels of detail, the wind stream and a FoliageMaterial
    // filled in from the recipe. Every part shares the whole tree's bounds (padded for wind sway) so
    // bark and leaves switch levels together. The caller owns the meshes (main thread).
    std::vector<Moonlight::MeshData*> BuildMeshes( const TreeMesh& InTree, const TreeRecipe& InRecipe, const TextureSource& InTextures );
}
