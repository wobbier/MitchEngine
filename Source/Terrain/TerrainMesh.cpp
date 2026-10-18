#include "PCH.h"
#include "TerrainMesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainData
{
    bool BuildPatch( const Heightfield& InField, TileCoord InTile, uint32_t InPatchX, uint32_t InPatchZ, PatchMesh& OutMesh, std::string& OutError )
    {
        OutError.clear();
        const Settings& settings = InField.GetSettings();
        const uint32_t q = settings.PatchQuads, side = q + 1;
        if( !settings.Contains( InTile ) || InPatchX >= settings.TileQuads / q || InPatchZ >= settings.TileQuads / q )
        { OutError = "Patch is outside the terrain tile"; return false; }
        PatchMesh mesh;
        mesh.SampleOrigin = { static_cast<int64_t>( InTile.X ) * settings.TileQuads + InPatchX * q, static_cast<int64_t>( InTile.Z ) * settings.TileQuads + InPatchZ * q };
        mesh.Revision = InField.GetRevision();
        mesh.Vertices.reserve( static_cast<size_t>( side ) * side );
        for( uint32_t z = 0; z <= q; ++z )
            for( uint32_t x = 0; x <= q; ++x )
            {
                const SampleCoord sample{ mesh.SampleOrigin.X + x, mesh.SampleOrigin.Z + z };
                float height;
                Vector3 normal;
                if( InField.GetSample( sample, height ) != QueryStatus::Ready || InField.GetNormal( sample, normal ) != QueryStatus::Ready )
                { OutError = "Patch height samples or normal halo are not resident"; return false; }
                PatchVertex vertex;
                vertex.Position = Vector3( x * settings.SampleSpacing, height, z * settings.SampleSpacing );
                vertex.Normal = normal; vertex.U = static_cast<float>( x ) / q; vertex.V = static_cast<float>( z ) / q;
                mesh.Bounds.Encapsulate( vertex.Position );
                mesh.Vertices.push_back( vertex );
            }
        const auto heightAt = [&]( uint32_t x, uint32_t z ) { return static_cast<double>( mesh.Vertices[static_cast<size_t>( z ) * side + x].Position.y ); };
        for( uint32_t stride = 1; stride <= q; stride *= 2 )
        {
            PatchLod lod;
            lod.Stride = stride;
            lod.Indices.reserve( static_cast<size_t>( q / stride ) * ( q / stride ) * 6 );
            for( uint32_t z = 0; z < q; z += stride )
                for( uint32_t x = 0; x < q; x += stride )
                {
                    const uint32_t a = z * side + x, b = a + stride * side;
                    lod.Indices.insert( lod.Indices.end(), { a, b, a + stride, a + stride, b, b + stride } );
                }
            double maxError = 0.0;
            for( uint32_t z = 0; z <= q; ++z )
                for( uint32_t x = 0; x <= q; ++x )
                {
                    const uint32_t cx = std::min( x / stride * stride, q - stride ), cz = std::min( z / stride * stride, q - stride );
                    const double u = static_cast<double>( x - cx ) / stride, v = static_cast<double>( z - cz ) / stride;
                    const double a = heightAt( cx, cz ), b = heightAt( cx + stride, cz ), c = heightAt( cx, cz + stride ), d = heightAt( cx + stride, cz + stride );
                    const double reconstructed = u + v <= 1.0 ? a + ( b - a ) * u + ( c - a ) * v : d + ( c - d ) * ( 1.0 - u ) + ( b - d ) * ( 1.0 - v );
                    maxError = std::max( maxError, std::abs( heightAt( x, z ) - reconstructed ) );
                }
            // Both triangulations use the same nested diagonals. Their difference is linear
            // inside every fine triangle, so its maximum occurs at a fine-grid vertex.
            lod.MaxError = static_cast<float>( maxError );
            if( lod.MaxError < maxError ) lod.MaxError = std::nextafter( lod.MaxError, std::numeric_limits<float>::infinity() );
            mesh.Lods.push_back( std::move( lod ) );
        }
        OutMesh = std::move( mesh );
        return true;
    }
}
