#pragma once
#include <box3d/box3d.h>

// Box3D debug drawing routed to DebugDraw: shapes become line lists built once per shape
// (createDebugShape) and transformed each draw.
namespace PhysicsDebugDraw
{
    void* CreateDebugShape( const b3DebugShape* InShape, void* InContext );
    void DestroyDebugShape( void* InUserShape, void* InContext );
    b3DebugDraw Make();
}
