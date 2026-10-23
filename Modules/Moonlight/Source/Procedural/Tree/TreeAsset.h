#pragma once
#include "Resource/MetaFile.h"
#include "Resource/MetaRegistry.h"
#include "TreeRecipe.h"
#include <string>

namespace MitchTree
{
    // `.mtree` files: the recipe's JSON.
    bool LoadRecipe( const std::string& InFullPath, TreeRecipe& OutRecipe, std::string& OutError );
    // Writes beside the destination, then replaces it, so readers see the old or new file whole.
    bool SaveRecipe( const std::string& InFullPath, const TreeRecipe& InRecipe, std::string& OutError );
}

// A `.mtree` recipe is its own runtime asset: ModelResource generates the tree when it loads (no
// cook), so it works anywhere a model path does (Model, prefabs, terrain foliage).
struct TreeMetadata final : public MetaBase
{
    explicit TreeMetadata( const Path& InPath ) : MetaBase( InPath ) {}
    std::string GetExtension2() const override { return "mtree"; }
    void OnSerialize( json& ) override {}
    void OnDeserialize( const json& ) override {}
    const Reflection::TypeInfo* GetEditableType() override;
    void* GetEditableData() override;
    void SaveEditableData() override;

private:
    MitchTree::TreeRecipe m_recipe;
    bool m_loaded = false;
};

ME_REGISTER_METADATA( "mtree", TreeMetadata )
