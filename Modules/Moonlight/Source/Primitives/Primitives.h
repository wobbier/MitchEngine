#pragma once
#include "RenderCommands.h"

namespace Moonlight
{
    class MeshData;

    // Shared primitive geometry. Every primitive of a type uses the same GPU buffers, so they batch into
    // one instanced draw. All shapes fit the [-1, 1] box: Plane (XZ quad), Cube, Sphere (radius 1),
    // Cylinder (radius 1, height 2) and Capsule (radius 0.5, height 2), with normals, tangents and UVs.
    namespace Primitives
    {
        // nullptr for MeshType::Model / MeshCount. Built on first use (needs an initialized renderer).
        MeshData* Get( MeshType InType );

        bool IsPrimitive( MeshType InType );

        // Releases the GPU buffers; call before the renderer shuts down.
        void Shutdown();
    }
}
