#include "TreeBuild.h"
#include "Math/Hash.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace MitchTree::Detail
{
    namespace
    {
        constexpr float kPi = 3.14159265f;
        // Ring sides per level (multiples of four so every level of detail can halve them).
        constexpr int kMaxSides[] = { 24, 12, 8, 4, 4 };
        constexpr float kEdgeLength = 0.05f;

        glm::vec3 ToGlm( const Vector3& InVector )
        {
            return glm::vec3( InVector.x, InVector.y, InVector.z );
        }


        Vector3 ToVector( const glm::vec3& InVector )
        {
            return Vector3( InVector.x, InVector.y, InVector.z );
        }


        // The radius that sizes a stem's rings and texture repeats: past the flare or collar.
        float ReferenceRadius( const Stem& InStem )
        {
            return SampleStem( InStem, InStem.Level == 0 ? 0.15f : 0.1f ).Radius;
        }


        int SidesFor( const Stem& InStem )
        {
            const int level = std::min( InStem.Level, 4 );
            const int minimum = InStem.Level == 0 ? 8 : 4;
            const float radius = InStem.Nodes.front().Radius;
            const int sides = static_cast<int>( std::lround( 2.f * kPi * radius / kEdgeLength / 4.f ) ) * 4;
            return std::clamp( sides, minimum, std::max( minimum, kMaxSides[level] ) );
        }


        // Thin and deep stems drop out of coarse levels (their leaves stay, so the crown keeps its shape).
        bool KeptAt( const Stem& InStem, int InLod, int InDeepest, float InReferenceRadius )
        {
            if( InLod == 0 || InStem.Level == 0 ) return true;
            if( InStem.Level > std::max( 1, InDeepest - InLod ) ) return false;
            return InReferenceRadius >= 0.004f * static_cast<float>( 1 << InLod );
        }
    }


    uint8_t ToByte( float InUnit )
    {
        return static_cast<uint8_t>( std::lround( std::clamp( InUnit, 0.f, 1.f ) * 255.f ) );
    }


    void AddTriangle( const std::vector<TreeVertex>& InVertices, std::vector<uint32_t>& OutIndices, uint32_t InA, uint32_t InB, uint32_t InC, const glm::vec3& InFacing )
    {
        const glm::vec3 a = ToGlm( InVertices[InA].Position ), b = ToGlm( InVertices[InB].Position ), c = ToGlm( InVertices[InC].Position );
        const glm::vec3 face = glm::cross( b - a, c - a );
        if( glm::dot( face, face ) < 1e-14f ) return;
        if( glm::dot( face, InFacing ) >= 0.f ) OutIndices.insert( OutIndices.end(), { InA, InB, InC } );
        else OutIndices.insert( OutIndices.end(), { InA, InC, InB } );
    }


    void BuildBark( BuildContext& InContext, TreePart& OutPart )
    {
        OutPart.Name = "Bark";
        OutPart.Lods.assign( static_cast<size_t>( 1 + InContext.LodLevels ), {} );
        const BarkMaterial& bark = InContext.Recipe.Bark;
        const TrunkSettings& trunk = InContext.Recipe.Trunk;
        const float maxSway = InContext.Tree.MaxSway > 0.f ? InContext.Tree.MaxSway : 1.f;
        std::vector<TreeVertex>& vertices = OutPart.Vertices;

        for( const Stem& stem : InContext.Tree.Stems )
        {
            if( stem.Nodes.size() < 2 ) continue;
            const int sides = SidesFor( stem );
            const size_t rings = stem.Nodes.size();
            const uint32_t stride = static_cast<uint32_t>( sides + 1 );
            const float referenceRadius = ReferenceRadius( stem );
            const float uRepeat = std::max( 1.f, std::round( 2.f * kPi * referenceRadius / bark.TileWidth ) );
            Pcg32 rng( Hash::Of( stem.Id, 0xBA4Cu ) );
            const float vOffset = rng.Unit();
            const float lobePhase = rng.Unit() * 2.f * kPi;
            const uint32_t first = static_cast<uint32_t>( vertices.size() );
            const uint8_t phase = ToByte( stem.Phase );

            for( size_t i = 0; i < rings; ++i )
            {
                const StemNode& node = stem.Nodes[i];
                for( int j = 0; j <= sides; ++j )
                {
                    const float theta = 2.f * kPi * static_cast<float>( j ) / static_cast<float>( sides );
                    const glm::vec3 radial = std::cos( theta ) * node.Normal + std::sin( theta ) * node.Binormal;
                    float radius = node.Radius;
                    if( stem.Level == 0 && node.Flare > 0.f && trunk.FlareLobes > 0 )
                    {
                        const float lobe = std::pow( std::max( 0.f, std::cos( static_cast<float>( trunk.FlareLobes ) * theta + lobePhase ) ), 3.f );
                        radius *= 1.f + trunk.LobeDepth * node.Flare * ( lobe - 0.2f ) * 1.5f;
                    }
                    TreeVertex vertex;
                    vertex.Position = ToVector( node.Position + radial * radius );
                    vertex.Normal = ToVector( radial );
                    vertex.UV = Vector2( static_cast<float>( j ) / static_cast<float>( sides ) * uRepeat, node.Distance / bark.TileHeight + vOffset );
                    vertex.Wind = { { 0, phase, ToByte( node.Sway / maxSway ), 255 } };
                    vertices.push_back( vertex );
                }
            }

            // Normals and tangents from the surface itself (covers taper, flare, lobes and collars).
            for( size_t i = 0; i < rings; ++i )
            {
                const size_t below = i > 0 ? i - 1 : 0, above = std::min( i + 1, rings - 1 );
                for( int j = 0; j <= sides; ++j )
                {
                    const int previous = j == 0 ? sides - 1 : j - 1;
                    const int next = j == sides ? 1 : j + 1;
                    auto at = [&]( size_t ring, int side ) { return ToGlm( vertices[first + static_cast<uint32_t>( ring ) * stride + static_cast<uint32_t>( side )].Position ); };
                    const glm::vec3 around = at( i, next ) - at( i, previous );
                    const glm::vec3 along = at( above, j ) - at( below, j );
                    TreeVertex& vertex = vertices[first + static_cast<uint32_t>( i ) * stride + static_cast<uint32_t>( j )];
                    glm::vec3 normal = glm::cross( around, along );
                    glm::vec3 tangent = around;
                    if( glm::dot( normal, normal ) < 1e-14f || glm::dot( tangent, tangent ) < 1e-14f )
                    {
                        // A collapsed tip: continue the ring below it.
                        if( i > 0 )
                        {
                            const TreeVertex& previousRing = vertices[first + static_cast<uint32_t>( i - 1 ) * stride + static_cast<uint32_t>( j )];
                            vertex.Normal = previousRing.Normal;
                            vertex.Tangent = previousRing.Tangent;
                            vertex.Bitangent = previousRing.Bitangent;
                        }
                        else
                        {
                            vertex.Tangent = ToVector( glm::cross( stem.Nodes[i].Direction, ToGlm( vertex.Normal ) ) );
                            vertex.Bitangent = ToVector( stem.Nodes[i].Direction );
                        }
                        continue;
                    }
                    normal = glm::normalize( normal );
                    tangent = glm::normalize( tangent - normal * glm::dot( normal, tangent ) );
                    vertex.Normal = ToVector( normal );
                    vertex.Tangent = ToVector( tangent );
                    vertex.Bitangent = ToVector( glm::cross( normal, tangent ) );
                }
                OcclusionSample sample;
                sample.Position = stem.Nodes[i].Position;
                sample.FirstVertex = first + static_cast<uint32_t>( i ) * stride;
                sample.VertexCount = stride;
                InContext.Samples.push_back( sample );
            }

            for( int lod = 0; lod <= InContext.LodLevels; ++lod )
            {
                if( !KeptAt( stem, lod, InContext.Tree.DeepestLevel, referenceRadius ) ) continue;
                const size_t ringStep = static_cast<size_t>( 1 ) << lod;
                const int sideStep = std::max( 1, std::min( 1 << lod, sides / 4 ) );
                std::vector<size_t> kept;
                for( size_t i = 0; i < rings; i += ringStep ) kept.push_back( i );
                if( kept.back() != rings - 1 ) kept.push_back( rings - 1 );
                std::vector<uint32_t>& indices = OutPart.Lods[static_cast<size_t>( lod )];
                for( size_t k = 0; k + 1 < kept.size(); ++k )
                {
                    const uint32_t ringA = first + static_cast<uint32_t>( kept[k] ) * stride;
                    const uint32_t ringB = first + static_cast<uint32_t>( kept[k + 1] ) * stride;
                    for( int j = 0; j < sides; j += sideStep )
                    {
                        const uint32_t a0 = ringA + static_cast<uint32_t>( j ), a1 = ringA + static_cast<uint32_t>( j + sideStep );
                        const uint32_t b0 = ringB + static_cast<uint32_t>( j ), b1 = ringB + static_cast<uint32_t>( j + sideStep );
                        const glm::vec3 facing = ToGlm( vertices[a0].Normal ) + ToGlm( vertices[a1].Normal ) + ToGlm( vertices[b0].Normal ) + ToGlm( vertices[b1].Normal );
                        AddTriangle( vertices, indices, a0, a1, b0, facing );
                        AddTriangle( vertices, indices, a1, b1, b0, facing );
                    }
                }
            }
        }
    }
}
