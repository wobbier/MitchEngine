#include "PCH.h"
#include "PhysicsDebugDraw.h"
#include "Debug/DebugDraw.h"
#include "Physics/Box3DUtils.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    struct DebugShape
    {
        b3ShapeType Type = b3_sphereShape;
        b3Sphere Sphere = {};
        b3Capsule Capsule = {};
        std::vector<b3Vec3> Lines;  // pairs, body space
    };

    constexpr size_t kMaxMeshLines = 40000;

    Vector4 ToColor( b3HexColor InColor, float InAlpha = 1.f )
    {
        const uint32_t c = static_cast<uint32_t>( InColor );
        return Vector4( ( ( c >> 16 ) & 0xFF ) / 255.f, ( ( c >> 8 ) & 0xFF ) / 255.f, ( c & 0xFF ) / 255.f, InAlpha );
    }

    void DrawShape( void* InUserShape, b3WorldTransform InTransform, b3HexColor InColor, void* )
    {
        const DebugShape* shape = static_cast<const DebugShape*>( InUserShape );
        const Vector4 color = ToColor( InColor );
        using Box3DUtils::FromB3;
        switch( shape->Type )
        {
        case b3_sphereShape:
            DebugDraw::Sphere( FromB3( b3TransformPoint( InTransform, shape->Sphere.center ) ), shape->Sphere.radius, color, 0.f, DebugDraw::None, 16 );
            break;
        case b3_capsuleShape:
            DebugDraw::Capsule( FromB3( b3TransformPoint( InTransform, shape->Capsule.center1 ) ), FromB3( b3TransformPoint( InTransform, shape->Capsule.center2 ) ), shape->Capsule.radius, color );
            break;
        default:
            for( size_t i = 0; i + 1 < shape->Lines.size(); i += 2 )
            {
                DebugDraw::Line( FromB3( b3TransformPoint( InTransform, shape->Lines[i] ) ), FromB3( b3TransformPoint( InTransform, shape->Lines[i + 1] ) ), color );
            }
            break;
        }
    }

    void DrawSegment( b3Pos InA, b3Pos InB, b3HexColor InColor, void* )
    {
        DebugDraw::Line( Box3DUtils::FromB3( InA ), Box3DUtils::FromB3( InB ), ToColor( InColor ) );
    }

    void DrawTransform( b3WorldTransform InTransform, void* )
    {
        const Vector3 origin = Box3DUtils::FromB3( InTransform.p );
        const b3Vec3 axes[3] = { { 0.25f, 0.f, 0.f }, { 0.f, 0.25f, 0.f }, { 0.f, 0.f, 0.25f } };
        const Vector4 colors[3] = { DebugDraw::Red, DebugDraw::Green, DebugDraw::Blue };
        for( int i = 0; i < 3; ++i )
        {
            DebugDraw::Line( origin, origin + Box3DUtils::FromB3( b3RotateVector( InTransform.q, axes[i] ) ), colors[i] );
        }
    }

    void DrawPoint( b3Pos InPoint, float InSize, b3HexColor InColor, void* )
    {
        DebugDraw::Sphere( Box3DUtils::FromB3( InPoint ), std::max( InSize * 0.005f, 0.02f ), ToColor( InColor ), 0.f, DebugDraw::NoDepthTest, 6 );
    }

    void DrawSphere( b3Pos InCenter, float InRadius, b3HexColor InColor, float InAlpha, void* )
    {
        DebugDraw::Sphere( Box3DUtils::FromB3( InCenter ), InRadius, ToColor( InColor, std::max( InAlpha, 0.5f ) ), 0.f, DebugDraw::None, 16 );
    }

    void DrawCapsule( b3Pos InA, b3Pos InB, float InRadius, b3HexColor InColor, float InAlpha, void* )
    {
        DebugDraw::Capsule( Box3DUtils::FromB3( InA ), Box3DUtils::FromB3( InB ), InRadius, ToColor( InColor, std::max( InAlpha, 0.5f ) ) );
    }

    void DrawBounds( b3AABB InBounds, b3HexColor InColor, void* )
    {
        DebugDraw::Box( AABB( Box3DUtils::FromB3( InBounds.lowerBound ), Box3DUtils::FromB3( InBounds.upperBound ) ), ToColor( InColor ) );
    }

    void DrawBox( b3Vec3 InExtents, b3WorldTransform InTransform, b3HexColor InColor, void* )
    {
        const b3Vec3 e = InExtents;
        b3Vec3 corners[8];
        for( int i = 0; i < 8; ++i )
        {
            corners[i] = b3TransformPoint( InTransform, b3Vec3{ ( i & 1 ) ? e.x : -e.x, ( i & 2 ) ? e.y : -e.y, ( i & 4 ) ? e.z : -e.z } );
        }
        const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        const Vector4 color = ToColor( InColor );
        for( const auto& edge : edges )
        {
            DebugDraw::Line( Box3DUtils::FromB3( corners[edge[0]] ), Box3DUtils::FromB3( corners[edge[1]] ), color );
        }
    }

    void DrawString( b3Pos, const char*, b3HexColor, void* )
    {
    }
}


void* PhysicsDebugDraw::CreateDebugShape( const b3DebugShape* InShape, void* )
{
    DebugShape* shape = new DebugShape();
    shape->Type = InShape->type;
    switch( InShape->type )
    {
    case b3_sphereShape:
        shape->Sphere = *InShape->sphere;
        break;
    case b3_capsuleShape:
        shape->Capsule = *InShape->capsule;
        break;
    case b3_hullShape:
    {
        const b3HullData* hull = InShape->hull;
        const b3HullHalfEdge* edges = b3GetHullEdges( hull );
        const b3Vec3* points = b3GetHullPoints( hull );
        for( int i = 0; i < hull->edgeCount; ++i )
        {
            const b3HullHalfEdge& edge = edges[i];
            if( i < edge.twin )
            {
                shape->Lines.push_back( points[edge.origin] );
                shape->Lines.push_back( points[edges[edge.next].origin] );
            }
        }
        break;
    }
    case b3_meshShape:
    {
        const b3MeshData* data = InShape->mesh->data;
        const b3Vec3 scale = InShape->mesh->scale;
        const b3Vec3* vertices = b3GetMeshVertices( data );
        const b3MeshTriangle* triangles = b3GetMeshTriangles( data );
        auto scaled = [&scale]( b3Vec3 v ) { return b3Vec3{ v.x * scale.x, v.y * scale.y, v.z * scale.z }; };
        for( int i = 0; i < data->triangleCount && shape->Lines.size() < kMaxMeshLines * 2; ++i )
        {
            const b3Vec3 a = scaled( vertices[triangles[i].index1] );
            const b3Vec3 b = scaled( vertices[triangles[i].index2] );
            const b3Vec3 c = scaled( vertices[triangles[i].index3] );
            shape->Lines.insert( shape->Lines.end(), { a, b, b, c, c, a } );
        }
        break;
    }
    default:
        break;
    }
    return shape;
}


void PhysicsDebugDraw::DestroyDebugShape( void* InUserShape, void* )
{
    delete static_cast<DebugShape*>( InUserShape );
}


b3DebugDraw PhysicsDebugDraw::Make()
{
    b3DebugDraw draw = b3DefaultDebugDraw();
    draw.DrawShapeFcn = DrawShape;
    draw.DrawSegmentFcn = DrawSegment;
    draw.DrawTransformFcn = DrawTransform;
    draw.DrawPointFcn = DrawPoint;
    draw.DrawSphereFcn = DrawSphere;
    draw.DrawCapsuleFcn = DrawCapsule;
    draw.DrawBoundsFcn = DrawBounds;
    draw.DrawBoxFcn = DrawBox;
    draw.DrawStringFcn = DrawString;
    draw.drawShapes = true;
    draw.drawJoints = true;
    draw.drawingBounds = b3AABB{ b3Vec3{ -1.0e5f, -1.0e5f, -1.0e5f }, b3Vec3{ 1.0e5f, 1.0e5f, 1.0e5f } };
    return draw;
}
