#include "PCH.h"
#include "TerrainMesh.h"

namespace TerrainData
{
    bool BuildStitchedIndices( uint32_t q, uint32_t stride, uint8_t edges, std::vector<uint32_t>& out, std::string& error )
    {
        if( !q || q > 1024 || ( q & ( q - 1 ) ) || !stride || ( stride & ( stride - 1 ) ) || stride > q ||
            ( edges & ~15 ) || ( edges && stride == q ) )
        { error = "Invalid terrain topology resolution, stride or stitch mask"; return false; }
        std::vector<uint32_t> indices;
        indices.reserve( static_cast<size_t>( q / stride ) * ( q / stride ) * 6 );
        const auto vertex = [&]( uint32_t x, uint32_t z ) {
            if( x == 0 && ( edges & MinX ) && ( z / stride ) % 2 ) z += stride;
            if( x == q && ( edges & MaxX ) && ( z / stride ) % 2 ) z -= stride;
            if( z == 0 && ( edges & MinZ ) && ( x / stride ) % 2 ) x -= stride;
            if( z == q && ( edges & MaxZ ) && ( x / stride ) % 2 ) x += stride;
            return z * ( q + 1 ) + x;
        };
        const auto triangle = [&]( uint32_t a, uint32_t b, uint32_t c ) {
            const int64_t ax = a % ( q + 1 ), az = a / ( q + 1 ), bx = b % ( q + 1 ), bz = b / ( q + 1 ), cx = c % ( q + 1 ), cz = c / ( q + 1 );
            const int64_t area = ( bx - ax ) * ( cz - az ) - ( bz - az ) * ( cx - ax );
            if( area < 0 ) indices.insert( indices.end(), { a, b, c } );
        };
        for( uint32_t z = 0; z < q; z += stride ) for( uint32_t x = 0; x < q; x += stride )
        {
            const uint32_t a = vertex( x, z ), b = vertex( x, z + stride ), c = vertex( x + stride, z ), d = vertex( x + stride, z + stride );
            triangle( a, b, c ); triangle( c, b, d );
        }
        out = std::move( indices ); error.clear(); return true;
    }
}
