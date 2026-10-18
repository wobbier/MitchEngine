#pragma once
#include "Terrain/ResidentTerrain.h"
#include <span>

namespace TerrainData
{
    struct LodView
    {
        Vector3 Position, Forward{ 0.f, 0.f, 1.f }, Up{ 0.f, 1.f, 0.f };
        // abs(projection diagonal) * viewport dimension / 2; actual projection, not guessed FOV.
        double PixelsX = 1, PixelsY = 1;
        float Near = 0.1f;
        bool Orthographic = false, ForceFine = false;
    };

    struct LodSettings
    {
        float PixelError = 2.f;
        float Hysteresis = 0.2f; // Coarsen below (1 - Hysteresis) * PixelError; refine above PixelError.
        int ForcedLevel = -1;   // Uniform inspection override; bypasses the error threshold.
    };

    struct PatchLodChoice
    {
        uint8_t Level = 0, Edges = 0;
        double PixelError = 0;
    };

    // Conservative pixels per metre of vertical error. Near/behind-camera and oblique views
    // retain full detail. Includes the perspective depth change from a vertical displacement.
    double VerticalPixelScale( const AABB& InWorldBounds, const LodView& InView );
    // All supplied views request common topology, also used by shadows and picking. Neighbor
    // refinement enforces 2:1 strides and the additional error introduced by stitching.
    bool SelectTerrainLods( const ResidentTerrain& InTerrain, const Vector3& InTranslation, std::span<const LodView> InViews,
        const LodSettings& InSettings, std::span<const PatchLodChoice> InPrevious, std::vector<PatchLodChoice>& OutChoices, std::string& OutError );
}
