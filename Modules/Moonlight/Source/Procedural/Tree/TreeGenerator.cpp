#include "TreeGenerator.h"
#include "TreeBuild.h"
#include "TreeTextures.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace MitchTree
{
    namespace Detail
    {
        namespace
        {
            constexpr int kGridCells = 32;
            constexpr int kRaySteps = 16;
        }


        void BakeOcclusion( BuildContext& InContext, std::vector<TreeVertex>& InOutBark, std::vector<TreeVertex>* InOutLeaves )
        {
            if( InContext.Samples.empty() || InContext.Recipe.Occlusion <= 0.f ) return;
            glm::vec3 low( 1e9f ), high( -1e9f );
            for( const OcclusionSample& sample : InContext.Samples )
            {
                low = glm::min( low, sample.Position );
                high = glm::max( high, sample.Position );
            }
            const glm::vec3 extent = glm::max( high - low, glm::vec3( 0.01f ) );
            const float cell = std::max( { extent.x, extent.y, extent.z } ) / static_cast<float>( kGridCells );
            const int nx = static_cast<int>( extent.x / cell ) + 2, ny = static_cast<int>( extent.y / cell ) + 2, nz = static_cast<int>( extent.z / cell ) + 2;
            std::vector<float> density( static_cast<size_t>( nx ) * ny * nz, 0.f );
            auto index = [&]( const glm::vec3& InPosition, int& OutIndex ) {
                const glm::vec3 local = ( InPosition - low ) / cell;
                const int x = static_cast<int>( std::floor( local.x ) ), y = static_cast<int>( std::floor( local.y ) ), z = static_cast<int>( std::floor( local.z ) );
                if( x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz ) return false;
                OutIndex = ( z * ny + y ) * nx + x;
                return true;
            };
            // Optical depth per cell crossed: blocking area over the cell's face (randomly oriented cards
            // block about half their area).
            for( const OcclusionSample& sample : InContext.Samples )
            {
                int i = 0;
                if( sample.Area > 0.f && index( sample.Position, i ) ) density[static_cast<size_t>( i )] += 0.5f * sample.Area / ( cell * cell );
            }
            // Straight up, then a ring of seven at 35 degrees.
            glm::vec3 rays[8] = { glm::vec3( 0.f, 1.f, 0.f ) };
            for( int k = 1; k < 8; ++k )
            {
                const float azimuth = 6.2831853f * static_cast<float>( k - 1 ) / 7.f;
                rays[k] = glm::vec3( std::cos( azimuth ) * 0.819f, 0.574f, std::sin( azimuth ) * 0.819f );
            }
            const float strength = InContext.Recipe.Occlusion;
            for( const OcclusionSample& sample : InContext.Samples )
            {
                float visible = 0.f;
                for( const glm::vec3& ray : rays )
                {
                    float depth = 0.f;
                    for( int step = 1; step <= kRaySteps; ++step )
                    {
                        int i = 0;
                        if( !index( sample.Position + ray * ( cell * static_cast<float>( step ) ), i ) ) break;
                        depth += density[static_cast<size_t>( i )];
                    }
                    visible += std::exp( -depth );
                }
                const float occlusion = std::max( 0.1f, 1.f - strength * ( 1.f - visible / 8.f ) );
                std::vector<TreeVertex>* target = sample.Leaf ? InOutLeaves : &InOutBark;
                if( !target ) continue;
                for( uint32_t v = sample.FirstVertex; v < sample.FirstVertex + sample.VertexCount && v < target->size(); ++v )
                {
                    ( *target )[v].Wind[3] = ToByte( occlusion );
                }
            }
        }
    }


    const TreePart* TreeMesh::FindPart( const std::string& InName ) const
    {
        for( const TreePart& part : Parts )
        {
            if( part.Name == InName ) return &part;
        }
        return nullptr;
    }


    TreeMesh Generate( const TreeRecipe& InRecipe )
    {
        const auto start = std::chrono::steady_clock::now();
        TreeRecipe recipe = InRecipe;
        recipe.Sanitize();
        // A generated atlas decides where its leaf pictures are.
        if( recipe.LeafTextures.Generate && recipe.Leaves.Kind != LeafKind::Frond ) recipe.Leaves.Regions = GeneratedLeafRegions( recipe.LeafTextures );
        TreeMesh mesh;
        const Detail::Skeleton skeleton = Detail::BuildSkeleton( recipe );
        Detail::BuildContext context{ recipe, skeleton };
        context.LodLevels = std::clamp( recipe.Lod.Levels, 0, 4 );

        TreePart bark;
        Detail::BuildBark( context, bark );
        TreePart leaves;
        std::vector<Detail::LeafCard> cards;
        Detail::BuildLeaves( context, leaves, cards );
        Detail::BakeOcclusion( context, bark.Vertices, &leaves.Vertices );
        Detail::BuildLeafLods( context, leaves, cards );

        mesh.Parts.push_back( std::move( bark ) );
        if( !leaves.Vertices.empty() ) mesh.Parts.push_back( std::move( leaves ) );

        // Every part keeps the same level count: an empty level gets one degenerate triangle.
        for( TreePart& part : mesh.Parts )
        {
            for( std::vector<uint32_t>& lod : part.Lods )
            {
                if( lod.empty() && !part.Vertices.empty() ) lod = { 0u, 0u, 0u };
            }
        }

        glm::vec3 low( 1e9f ), high( -1e9f );
        for( const TreePart& part : mesh.Parts )
        {
            for( const TreeVertex& vertex : part.Vertices )
            {
                low = glm::min( low, glm::vec3( vertex.Position.x, vertex.Position.y, vertex.Position.z ) );
                high = glm::max( high, glm::vec3( vertex.Position.x, vertex.Position.y, vertex.Position.z ) );
            }
        }
        if( low.x > high.x ) low = high = glm::vec3( 0.f );
        mesh.BoundsMin = Vector3( low.x, low.y, low.z );
        mesh.BoundsMax = Vector3( high.x, high.y, high.z );
        mesh.Height = high.y;
        mesh.MaxSway = skeleton.MaxSway;
        for( int level = 1; level <= context.LodLevels; ++level )
        {
            mesh.LodScreenHeights.push_back( recipe.Lod.Transition / static_cast<float>( 1 << ( level - 1 ) ) );
        }

        mesh.Statistics.Stems = static_cast<uint32_t>( skeleton.Stems.size() );
        mesh.Statistics.Leaves = static_cast<uint32_t>( recipe.Leaves.Kind == LeafKind::Frond ? 0 : cards.size() );
        mesh.Statistics.Triangles.assign( static_cast<size_t>( 1 + context.LodLevels ), 0u );
        for( const TreePart& part : mesh.Parts )
        {
            for( size_t lod = 0; lod < part.Lods.size(); ++lod )
            {
                mesh.Statistics.Triangles[lod] += static_cast<uint32_t>( part.Lods[lod].size() / 3 );
            }
        }
        mesh.Statistics.Milliseconds = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
        return mesh;
    }
}
