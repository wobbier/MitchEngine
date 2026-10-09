#include "PCH.h"
#include "Colliders2D.h"
#include "Colliders.h"
#include "Math/Bounds.h"
#include "Physics/PhysicsHash.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( CapsuleDirection2D, { { "Vertical", CapsuleDirection2D::Vertical }, { "Horizontal", CapsuleDirection2D::Horizontal } } )

#define ME_COLLIDER2D_FIELDS()                                                                                            \
    ME_FIELD( Offset );                                                                                                   \
    ME_FIELD( IsTrigger ).Tooltip( "Report overlaps without colliding" );                                                  \
    ME_FIELD( Friction ).Range( 0.f, 2.f ).Category( "Material" );                                                         \
    ME_FIELD( Restitution ).Range( 0.f, 1.f ).Category( "Material" ).Tooltip( "Bounciness" );                             \
    ME_FIELD( Density ).Range( 0.f, 100000.f ).Category( "Material" ).Tooltip( "Used when the Rigidbody2D's Mass is 0" );

ME_REFLECT_BEGIN( BoxCollider2D )
    ME_FIELD( Size );
    ME_FIELD( EdgeRadius ).Range( 0.f, 10.f );
    ME_COLLIDER2D_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( CircleCollider2D )
    ME_FIELD( Radius ).Range( 0.001f, 1000.f );
    ME_COLLIDER2D_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( CapsuleCollider2D )
    ME_FIELD( Radius ).Range( 0.001f, 1000.f );
    ME_FIELD( Height ).Range( 0.001f, 1000.f ).Tooltip( "End to end, including the caps" );
    ME_FIELD( Direction );
    ME_COLLIDER2D_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( PolygonCollider2D )
    ME_FIELD( Points ).Tooltip( "Local XY outline; collides as its convex hull (at most 8 vertices)" );
    ME_FIELD( EdgeRadius ).Range( 0.f, 10.f );
    ME_COLLIDER2D_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( EdgeCollider2D )
    ME_FIELD( Points ).Tooltip( "Local XY polyline" );
    ME_FIELD( Loop );
    ME_COLLIDER2D_FIELDS()
ME_REFLECT_END()

using namespace PhysicsHash;

namespace
{
    // The XY rectangle of the entity's mesh bounds.
    bool GetMeshRect( Entity& InEntity, Vector2& OutCenter, Vector2& OutSize )
    {
        AABB bounds;
        if( !ColliderUtils::GetMeshBounds( InEntity, bounds ) )
        {
            return false;
        }
        const Vector3 center = bounds.GetCenter();
        const Vector3 size = bounds.GetSize();
        OutCenter = Vector2( center.x, center.y );
        OutSize = Vector2( size.x, size.y );
        return true;
    }

    float Cross( const Vector2& a, const Vector2& b, const Vector2& c )
    {
        return ( b.x - a.x ) * ( c.y - a.y ) - ( b.y - a.y ) * ( c.x - a.x );
    }
}


uint64_t Collider2DSettings::SettingsHash() const
{
    uint64_t h = 0xc0112dULL;
    for( float value : { Offset.x, Offset.y, Friction, Restitution, Density } )
    {
        h = Mix( h, Bits( value ) );
    }
    return Mix( h, IsTrigger ? 1u : 0u );
}


BoxCollider2D::BoxCollider2D()
    : Component( "BoxCollider2D" )
{
}


void BoxCollider2D::Init()
{
    Vector2 center;
    Vector2 size;
    if( !m_isConfigured && Parent && GetMeshRect( *Parent.Get(), center, size ) )
    {
        Size = size;
        Offset = center;
    }
    m_isConfigured = true;
}


void BoxCollider2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


CircleCollider2D::CircleCollider2D()
    : Component( "CircleCollider2D" )
{
}


void CircleCollider2D::Init()
{
    Vector2 center;
    Vector2 size;
    if( !m_isConfigured && Parent && GetMeshRect( *Parent.Get(), center, size ) )
    {
        Radius = std::max( size.x, size.y ) * 0.5f;
        Offset = center;
    }
    m_isConfigured = true;
}


void CircleCollider2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


CapsuleCollider2D::CapsuleCollider2D()
    : Component( "CapsuleCollider2D" )
{
}


void CapsuleCollider2D::Init()
{
    Vector2 center;
    Vector2 size;
    if( !m_isConfigured && Parent && GetMeshRect( *Parent.Get(), center, size ) )
    {
        Direction = size.y >= size.x ? CapsuleDirection2D::Vertical : CapsuleDirection2D::Horizontal;
        Radius = std::min( size.x, size.y ) * 0.5f;
        Height = std::max( size.x, size.y );
        Offset = center;
    }
    m_isConfigured = true;
}


void CapsuleCollider2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


PolygonCollider2D::PolygonCollider2D()
    : Component( "PolygonCollider2D" )
{
}


void PolygonCollider2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


EdgeCollider2D::EdgeCollider2D()
    : Component( "EdgeCollider2D" )
{
}


void EdgeCollider2D::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


std::vector<Vector2> Collider2DUtils::ConvexHull( const std::vector<Vector2>& InPoints, size_t InMaxVertices )
{
    // Andrew's monotone chain.
    std::vector<Vector2> points = InPoints;
    std::sort( points.begin(), points.end(), []( const Vector2& a, const Vector2& b ) { return a.x < b.x || ( a.x == b.x && a.y < b.y ); } );
    points.erase( std::unique( points.begin(), points.end(), []( const Vector2& a, const Vector2& b ) { return std::abs( a.x - b.x ) < 1e-6f && std::abs( a.y - b.y ) < 1e-6f; } ), points.end() );
    if( points.size() < 3 )
    {
        return points;
    }
    std::vector<Vector2> hull( points.size() * 2 );
    size_t k = 0;
    for( const Vector2& p : points )
    {
        while( k >= 2 && Cross( hull[k - 2], hull[k - 1], p ) <= 0.f )
        {
            --k;
        }
        hull[k++] = p;
    }
    for( size_t i = points.size() - 1, lower = k + 1; i-- > 0; )
    {
        const Vector2& p = points[i];
        while( k >= lower && Cross( hull[k - 2], hull[k - 1], p ) <= 0.f )
        {
            --k;
        }
        hull[k++] = p;
    }
    hull.resize( k - 1 );

    // Simplify: drop the vertex whose removal loses the least area until it fits.
    while( hull.size() > InMaxVertices && hull.size() > 3 )
    {
        size_t best = 0;
        float bestArea = FLT_MAX;
        for( size_t i = 0; i < hull.size(); ++i )
        {
            const Vector2& prev = hull[( i + hull.size() - 1 ) % hull.size()];
            const Vector2& next = hull[( i + 1 ) % hull.size()];
            const float area = std::abs( Cross( prev, hull[i], next ) );
            if( area < bestArea )
            {
                bestArea = area;
                best = i;
            }
        }
        hull.erase( hull.begin() + static_cast<std::ptrdiff_t>( best ) );
    }
    return hull;
}
