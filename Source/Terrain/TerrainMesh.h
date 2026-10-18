#pragma once

#include "Terrain/Heightfield.h"
#include "Math/Bounds.h"

namespace TerrainData
{
    struct PatchVertex
    {
        Vector3 Position; // Patch-local; SampleOrigin locates the patch on the terrain lattice.
        Vector3 Normal;
        float U = 0.f;
        float V = 0.f;
    };

    struct PatchLod
    {
        uint32_t Stride = 1;
        std::vector<uint32_t> Indices; // Over the full-resolution vertex array.
        float MaxError = 0.f;          // Conservative vertical error against the fine surface.
    };

    struct PatchMesh
    {
        SampleCoord SampleOrigin;
        uint64_t Revision = 0;
        AABB Bounds;
        std::vector<PatchVertex> Vertices;
        std::vector<PatchLod> Lods; // Includes the full-resolution level, then power-of-two strides.
    };

    // Reads an immutable heightfield snapshot, including neighbor samples for vertex normals.
    // Failure leaves OutMesh unchanged. No GPU allocation or neighbor LOD stitching yet.
    bool BuildPatch( const Heightfield& InField, TileCoord InTile, uint32_t InPatchX, uint32_t InPatchZ, PatchMesh& OutMesh, std::string& OutError );
}
