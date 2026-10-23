#pragma once
#include "TreeRecipe.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bimg { struct ImageContainer; }

// Procedural bark and leaf textures for MitchTree. A recipe's generated textures are virtual
// sub-assets ("<tree>.mtree#bark-color"): the texture loader bakes them from the recipe (with
// mip chains) instead of reading a file, so they always match the recipe and need no files.
namespace MitchTree
{
    struct Image
    {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Pixels;    // RGBA8, rows top to bottom
    };

    struct BarkImages
    {
        Image Color;    // sRGB
        Image Normal;   // tangent space, +Y along increasing v
        Image Orm;      // R occlusion, G roughness, B metallic
    };

    struct LeafImages
    {
        Image Color;    // sRGB, alpha = coverage
        Image Normal;
    };

    BarkImages GenerateBark( const BarkTexture& InSettings );
    LeafImages GenerateLeaves( const LeafTexture& InSettings );
    // The leaf pictures a generated atlas contains (known without rasterizing it).
    std::vector<LeafRegion> GeneratedLeafRegions( const LeafTexture& InSettings );

    // Virtual texture channels of a recipe: bark-color, bark-normal, bark-orm, leaf-color, leaf-normal.
    const std::vector<std::string>& TextureChannels();
    std::string ChannelPath( const std::string& InRecipePath, const std::string& InChannel );
    // A mipmapped RGBA8 image for one channel of the recipe at InRecipePath (used by Texture loads of
    // "<recipe>#<channel>" paths). Alpha-tested leaf mips keep their coverage.
    bimg::ImageContainer* BakeChannel( const std::string& InRecipePath, const std::string& InChannel, std::string& OutError );
    // The same from a recipe in memory (the MitchTree editor previews unsaved texture settings).
    bimg::ImageContainer* BakeChannel( const TreeRecipe& InRecipe, const std::string& InChannel, std::string& OutError );

    // Writes an image as PNG (exporting textures for editing elsewhere).
    bool WritePng( const Image& InImage, const std::string& InFullPath, std::string& OutError );
}
