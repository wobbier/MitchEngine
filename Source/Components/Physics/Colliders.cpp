#include "PCH.h"
#include "Colliders.h"
#include "Components/Graphics/Mesh.h"
#include "Graphics/MeshData.h"
#include "Physics/PhysicsHash.h"
#include <algorithm>
#include <cstring>

ME_REFLECT_ENUM( CapsuleAxis, { { "X", CapsuleAxis::X }, { "Y", CapsuleAxis::Y }, { "Z", CapsuleAxis::Z } } )

#define ME_COLLIDER_FIELDS()                                                                                              \
    ME_FIELD( Center );                                                                                                   \
    ME_FIELD( IsTrigger ).Tooltip( "Report overlaps without colliding" );                                                  \
    ME_FIELD( Friction ).Range( 0.f, 2.f ).Category( "Material" );                                                         \
    ME_FIELD( Restitution ).Range( 0.f, 1.f ).Category( "Material" ).Tooltip( "Bounciness" );                             \
    ME_FIELD( Density ).Range( 0.f, 100000.f ).Category( "Material" ).Tooltip( "Used when the Rigidbody's Mass is 0" );

ME_REFLECT_BEGIN( BoxCollider )
    ME_FIELD( Size );
    ME_COLLIDER_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( SphereCollider )
    ME_FIELD( Radius ).Range( 0.001f, 1000.f );
    ME_COLLIDER_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( CapsuleCollider )
    ME_FIELD( Radius ).Range( 0.001f, 1000.f );
    ME_FIELD( Height ).Range( 0.001f, 1000.f ).Tooltip( "End to end, including the caps" );
    ME_FIELD( Direction );
    ME_COLLIDER_FIELDS()
ME_REFLECT_END()

ME_REFLECT_BEGIN( MeshCollider )
    ME_FIELD( Convex ).Tooltip( "Convex hull (works on any body); off uses the exact triangles (static / kinematic only)" );
    ME_COLLIDER_FIELDS()
ME_REFLECT_END()

using namespace PhysicsHash;


uint64_t ColliderSettings::SettingsHash() const
{
    uint64_t h = 0xc011de5ULL;
    for( float value : { Center.x, Center.y, Center.z, Friction, Restitution, Density } )
    {
        h = Mix( h, Bits( value ) );
    }
    return Mix( h, IsTrigger ? 1u : 0u );
}


bool ColliderUtils::GetMeshBounds( Entity& InEntity, AABB& OutBounds )
{
    Mesh* mesh = InEntity.TryGetComponent<Mesh>();
    if( !mesh || !mesh->MeshReferece || !mesh->MeshReferece->Bounds.IsValid() )
    {
        return false;
    }
    OutBounds = mesh->MeshReferece->Bounds;
    return true;
}


BoxCollider::BoxCollider()
    : Component( "BoxCollider" )
{
}


void BoxCollider::Init()
{
    AABB bounds;
    if( !m_isConfigured && Parent && ColliderUtils::GetMeshBounds( *Parent.Get(), bounds ) )
    {
        Size = bounds.GetSize();
        Center = bounds.GetCenter();
    }
    m_isConfigured = true;
}


void BoxCollider::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


SphereCollider::SphereCollider()
    : Component( "SphereCollider" )
{
}


void SphereCollider::Init()
{
    AABB bounds;
    if( !m_isConfigured && Parent && ColliderUtils::GetMeshBounds( *Parent.Get(), bounds ) )
    {
        const Vector3 extents = bounds.GetExtents();
        Radius = std::max( { extents.x, extents.y, extents.z } );
        Center = bounds.GetCenter();
    }
    m_isConfigured = true;
}


void SphereCollider::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


CapsuleCollider::CapsuleCollider()
    : Component( "CapsuleCollider" )
{
}


void CapsuleCollider::Init()
{
    AABB bounds;
    if( !m_isConfigured && Parent && ColliderUtils::GetMeshBounds( *Parent.Get(), bounds ) )
    {
        // Along the longest axis.
        const Vector3 size = bounds.GetSize();
        int axis = size.y >= size.x && size.y >= size.z ? 1 : ( size.x >= size.z ? 0 : 2 );
        Direction = static_cast<CapsuleAxis>( axis );
        const float length = axis == 0 ? size.x : ( axis == 1 ? size.y : size.z );
        const float a = axis == 0 ? size.y : size.x;
        const float b = axis == 2 ? size.y : size.z;
        Radius = std::max( a, b ) * 0.5f;
        Height = std::max( length, Radius * 2.f );
        Center = bounds.GetCenter();
    }
    m_isConfigured = true;
}


void CapsuleCollider::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}


MeshCollider::MeshCollider()
    : Component( "MeshCollider" )
{
}


void MeshCollider::OnDeserialize( const json& InJson )
{
    Reflection::FromJson( StaticType(), this, InJson );
    m_isConfigured = true;
}
