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
    // Failure leaves OutMesh unchanged. Regular levels only; stitching uses the helper below.
    bool BuildPatch( const Heightfield& InField, TileCoord InTile, uint32_t InPatchX, uint32_t InPatchZ, PatchMesh& OutMesh, std::string& OutError );
    // Builds regular index levels and errors over an already populated square vertex grid.
    bool BuildPatchLods( PatchMesh& InMesh, uint32_t InQuads, std::string& OutError );
    // Bits identify edges adjoining a neighbor with twice this patch's stride.
    enum StitchEdge : uint8_t { MinX = 1, MaxX = 2, MinZ = 4, MaxZ = 8 };
    // Clockwise edge contraction preserves the next coarser grid's diagonals, including corners.
    bool BuildStitchedIndices( uint32_t InQuads, uint32_t InStride, uint8_t InEdges, std::vector<uint32_t>& OutIndices, std::string& OutError );
}
