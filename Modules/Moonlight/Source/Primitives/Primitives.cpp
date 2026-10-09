#include "Primitives.h"
#include "Primitives/Cube.h"
#include "Primitives/Plane.h"
#include "Graphics/MeshData.h"
#include <array>
#include <cmath>
#include <memory>

namespace Moonlight
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        // Builds rings of vertices around the Y axis. Each ring: y offset, radius scale for the unit
        // direction, and the polar angle used for the normal; tangent along increasing u, bitangent
        // towards decreasing v (image up), matching the cube's convention.
        class LatheMesh
            : public MeshData
        {
        public:
            struct Ring
            {
                float Theta = 0.f;      // polar angle of the normal (0 = up)
                float YOffset = 0.f;    // added to the position's y
                float Radius = 1.f;
                float V = 0.f;
            };

            LatheMesh( const std::vector<Ring>& InRings, int InSegments )
            {
                for( const Ring& ring : InRings )
                {
                    const float sinTheta = std::sin( ring.Theta );
                    const float cosTheta = std::cos( ring.Theta );
                    for( int segment = 0; segment <= InSegments; ++segment )
                    {
                        const float u = static_cast<float>( segment ) / InSegments;
                        const float phi = u * 2.f * kPi;
                        const float sinPhi = std::sin( phi );
                        const float cosPhi = std::cos( phi );

                        PosNormTexTanBiVertex vertex;
                        vertex.Normal = Vector3( sinTheta * cosPhi, cosTheta, sinTheta * sinPhi );
                        vertex.Position = vertex.Normal * ring.Radius + Vector3( 0.f, ring.YOffset, 0.f );
                        vertex.TextureCoord = Vector2( u, ring.V );
                        vertex.Tangent = Vector3( -sinPhi, 0.f, cosPhi );
                        vertex.BiTangent = Vector3( -cosTheta * cosPhi, sinTheta, -cosTheta * sinPhi );
                        Vertices.push_back( vertex );
                    }
                }

                const uint32_t stride = static_cast<uint32_t>( InSegments + 1 );
                for( uint32_t ring = 0; ring + 1 < InRings.size(); ++ring )
                {
                    for( uint32_t segment = 0; segment < static_cast<uint32_t>( InSegments ); ++segment )
                    {
                        const uint32_t a = ring * stride + segment;
                        const uint32_t b = a + 1;
                        const uint32_t c = a + stride;
                        const uint32_t d = c + 1;
                        AddTriangle( a, c, b );
                        AddTriangle( b, c, d );
                    }
                }
            }

            // Uploads the geometry once all rings and caps are added.
            void Finish()
            {
                InitMesh();
            }

            // Adds a triangle wound so it faces along its vertices' normals (the engine's front face is
            // cross(v1 - v0, v2 - v0) pointing out). Degenerate pole triangles are dropped.
            void AddTriangle( uint32_t InA, uint32_t InB, uint32_t InC )
            {
                const Vector3& a = Vertices[InA].Position;
                const Vector3& b = Vertices[InB].Position;
                const Vector3& c = Vertices[InC].Position;
                const Vector3 faceNormal = ( b - a ).Cross( c - a );
                if( faceNormal.LengthSquared() < 1e-12f )
                {
                    return;
                }
                const Vector3 normal = Vertices[InA].Normal + Vertices[InB].Normal + Vertices[InC].Normal;
                if( faceNormal.Dot( normal ) >= 0.f )
                {
                    Indices.insert( Indices.end(), { InA, InB, InC } );
                }
                else
                {
                    Indices.insert( Indices.end(), { InA, InC, InB } );
                }
            }

            // A flat disc cap at height InY facing InUp (+1 or -1).
            void AddCap( float InY, float InUp, float InRadius, int InSegments )
            {
                const uint32_t center = static_cast<uint32_t>( Vertices.size() );
                PosNormTexTanBiVertex middle;
                middle.Position = Vector3( 0.f, InY, 0.f );
                middle.Normal = Vector3( 0.f, InUp, 0.f );
                middle.TextureCoord = Vector2( 0.5f, 0.5f );
                middle.Tangent = Vector3( 1.f, 0.f, 0.f );
                middle.BiTangent = Vector3( 0.f, 0.f, InUp );
                Vertices.push_back( middle );
                for( int segment = 0; segment <= InSegments; ++segment )
                {
                    const float phi = static_cast<float>( segment ) / InSegments * 2.f * kPi;
                    PosNormTexTanBiVertex vertex = middle;
                    vertex.Position = Vector3( std::cos( phi ) * InRadius, InY, std::sin( phi ) * InRadius );
                    vertex.TextureCoord = Vector2( 0.5f + 0.5f * std::cos( phi ), 0.5f - 0.5f * std::sin( phi ) * InUp );
                    Vertices.push_back( vertex );
                }
                for( int segment = 0; segment < InSegments; ++segment )
                {
                    AddTriangle( center, center + 1 + segment, center + 2 + segment );
                }
            }
        };


        MeshData* BuildSphere()
        {
            constexpr int kRings = 24;
            std::vector<LatheMesh::Ring> rings;
            for( int ring = 0; ring <= kRings; ++ring )
            {
                const float v = static_cast<float>( ring ) / kRings;
                rings.push_back( { v * kPi, 0.f, 1.f, v } );
            }
            LatheMesh* mesh = new LatheMesh( rings, 48 );
            mesh->Finish();
            return mesh;
        }


        MeshData* BuildCylinder()
        {
            // Side: a lathe with two horizontal-normal rings; caps added separately.
            std::vector<LatheMesh::Ring> rings = { { kPi * 0.5f, 1.f, 1.f, 0.f }, { kPi * 0.5f, -1.f, 1.f, 1.f } };
            LatheMesh* mesh = new LatheMesh( rings, 48 );
            mesh->AddCap( 1.f, 1.f, 1.f, 48 );
            mesh->AddCap( -1.f, -1.f, 1.f, 48 );
            mesh->Finish();
            return mesh;
        }


        MeshData* BuildCapsule()
        {
            // Two hemispheres of radius 0.5 joined by a cylinder 1 tall; total height 2.
            constexpr int kHemisphereRings = 12;
            constexpr float kRadius = 0.5f;
            constexpr float kHalfHeight = 0.5f;
            std::vector<LatheMesh::Ring> rings;
            auto vOf = []( float y ) { return 0.5f - 0.5f * y; };
            for( int ring = 0; ring <= kHemisphereRings; ++ring )
            {
                const float theta = static_cast<float>( ring ) / kHemisphereRings * kPi * 0.5f;
                rings.push_back( { theta, kHalfHeight, kRadius, vOf( kHalfHeight + std::cos( theta ) * kRadius ) } );
            }
            for( int ring = 0; ring <= kHemisphereRings; ++ring )
            {
                const float theta = kPi * 0.5f + static_cast<float>( ring ) / kHemisphereRings * kPi * 0.5f;
                rings.push_back( { theta, -kHalfHeight, kRadius, vOf( -kHalfHeight + std::cos( theta ) * kRadius ) } );
            }
            LatheMesh* mesh = new LatheMesh( rings, 48 );
            mesh->Finish();
            return mesh;
        }


        std::array<std::unique_ptr<MeshData>, MeshType::MeshCount>& Cache()
        {
            static std::array<std::unique_ptr<MeshData>, MeshType::MeshCount> cache;
            return cache;
        }
    }


    namespace Primitives
    {
        MeshData* Get( MeshType InType )
        {
            if( !IsPrimitive( InType ) )
            {
                return nullptr;
            }
            std::unique_ptr<MeshData>& slot = Cache()[InType];
            if( !slot )
            {
                switch( InType )
                {
                case MeshType::Plane:
                    slot.reset( new PlaneMesh() );
                    break;
                case MeshType::Cube:
                    slot.reset( new CubeMesh() );
                    break;
                case MeshType::Sphere:
                    slot.reset( BuildSphere() );
                    break;
                case MeshType::Cylinder:
                    slot.reset( BuildCylinder() );
                    break;
                case MeshType::Capsule:
                    slot.reset( BuildCapsule() );
                    break;
                default:
                    break;
                }
            }
            return slot.get();
        }


        bool IsPrimitive( MeshType InType )
        {
            return InType != MeshType::Model && InType < MeshType::MeshCount;
        }


        void Shutdown()
        {
            for( std::unique_ptr<MeshData>& slot : Cache() )
            {
                slot.reset();
            }
        }
    }
}
