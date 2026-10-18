#include "PCH.h"
#include "Heightfield.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace TerrainData
{
    namespace
    {
        bool PowerOfTwo( uint32_t InValue ) { return InValue && !( InValue & ( InValue - 1 ) ); }

        Vector3 SurfaceNormal( double InDx, double InDz )
        {
            const double length = std::hypot( std::hypot( InDx, InDz ), 1.0 );
            return Vector3( static_cast<float>( -InDx / length ), static_cast<float>( 1.0 / length ), static_cast<float>( -InDz / length ) );
        }
    }

    bool TileCoord::operator<( const TileCoord& InOther ) const
    {
        return std::tie( X, Z ) < std::tie( InOther.X, InOther.Z );
    }

    bool SampleRect::IsEmpty() const { return Min.X > Max.X || Min.Z > Max.Z; }

    void SampleRect::Include( SampleCoord InSample )
    {
        if( IsEmpty() ) { Min = Max = InSample; return; }
        Min.X = std::min( Min.X, InSample.X ); Min.Z = std::min( Min.Z, InSample.Z );
        Max.X = std::max( Max.X, InSample.X ); Max.Z = std::max( Max.Z, InSample.Z );
    }

    int64_t FloorDivide( int64_t InValue, int64_t InDivisor )
    {
        if( InDivisor <= 0 ) throw std::invalid_argument( "Terrain divisor must be positive" );
        const int64_t quotient = InValue / InDivisor;
        return quotient - ( InValue % InDivisor < 0 ? 1 : 0 );
    }

    bool Settings::Validate( std::string& OutError ) const
    {
        OutError.clear();
        // Bounds individual allocations, not dataset extent. Larger tiles need an explicit format/tool decision.
        if( !PowerOfTwo( TileQuads ) || TileQuads > 4096 || !PowerOfTwo( PatchQuads ) || PatchQuads > 1024 || PatchQuads > TileQuads )
            OutError = "Terrain tile/patch quads must be powers of two (tile <= 4096, patch <= 1024, patch <= tile)";
        else if( !std::isfinite( SampleSpacing ) || SampleSpacing <= 0.f || static_cast<double>( SampleSpacing ) * TileQuads > std::numeric_limits<float>::max() )
            OutError = "Terrain sample spacing must be finite, positive, and representable across a tile";
        else if( !std::isfinite( HeightBase ) || !std::isfinite( HeightRange ) || HeightRange <= 0.f || std::abs( static_cast<double>( HeightBase ) + HeightRange ) > std::numeric_limits<float>::max() )
            OutError = "Terrain height encoding needs a finite base and positive, representable range";
        else if( !TilesX || !TilesZ || static_cast<int64_t>( MinTile.X ) + TilesX - 1 > INT32_MAX || static_cast<int64_t>( MinTile.Z ) + TilesZ - 1 > INT32_MAX )
            OutError = "Terrain tile extent is empty or exceeds signed tile coordinates";
        return OutError.empty();
    }

    bool Settings::Contains( TileCoord InTile ) const
    {
        return InTile.X >= MinTile.X && InTile.Z >= MinTile.Z
            && static_cast<int64_t>( InTile.X ) < static_cast<int64_t>( MinTile.X ) + TilesX
            && static_cast<int64_t>( InTile.Z ) < static_cast<int64_t>( MinTile.Z ) + TilesZ;
    }

    SampleCoord Settings::MinSample() const { return { static_cast<int64_t>( MinTile.X ) * TileQuads, static_cast<int64_t>( MinTile.Z ) * TileQuads }; }
    SampleCoord Settings::MaxSample() const { return { ( static_cast<int64_t>( MinTile.X ) + TilesX ) * TileQuads, ( static_cast<int64_t>( MinTile.Z ) + TilesZ ) * TileQuads }; }

    bool Settings::Contains( SampleCoord InSample ) const
    {
        const auto min = MinSample(), max = MaxSample();
        return InSample.X >= min.X && InSample.X <= max.X && InSample.Z >= min.Z && InSample.Z <= max.Z;
    }

    size_t Settings::SamplesPerTile() const { return static_cast<size_t>( TileQuads + 1 ) * ( TileQuads + 1 ); }

    TileCoord Settings::Owner( SampleCoord InSample ) const
    {
        if( !Contains( InSample ) ) throw std::out_of_range( "Terrain sample is outside the dataset" );
        return {
            static_cast<int32_t>( std::min( FloorDivide( InSample.X, TileQuads ), static_cast<int64_t>( MinTile.X ) + TilesX - 1 ) ),
            static_cast<int32_t>( std::min( FloorDivide( InSample.Z, TileQuads ), static_cast<int64_t>( MinTile.Z ) + TilesZ - 1 ) )
        };
    }

    Heightfield::Heightfield( const Settings& InSettings ) : m_settings( InSettings )
    {
        std::string error;
        if( !m_settings.Validate( error ) ) throw std::invalid_argument( error );
    }

    const Tile* Heightfield::FindTile( TileCoord InTile ) const
    {
        auto it = m_tiles.find( InTile );
        return it == m_tiles.end() ? nullptr : &it->second;
    }

    size_t Heightfield::Index( TileCoord InTile, SampleCoord InSample ) const
    {
        const int64_t x = InSample.X - static_cast<int64_t>( InTile.X ) * m_settings.TileQuads;
        const int64_t z = InSample.Z - static_cast<int64_t>( InTile.Z ) * m_settings.TileQuads;
        return static_cast<size_t>( z ) * ( m_settings.TileQuads + 1 ) + static_cast<size_t>( x );
    }

    std::vector<TileCoord> Heightfield::SharingTiles( SampleCoord InSample ) const
    {
        std::vector<TileCoord> result;
        if( !m_settings.Contains( InSample ) ) return result;
        const int64_t tx = FloorDivide( InSample.X, m_settings.TileQuads ), tz = FloorDivide( InSample.Z, m_settings.TileQuads );
        for( int dx = 0; dx <= ( InSample.X % m_settings.TileQuads == 0 ? 1 : 0 ); ++dx )
            for( int dz = 0; dz <= ( InSample.Z % m_settings.TileQuads == 0 ? 1 : 0 ); ++dz )
            {
                const int64_t x = tx - dx, z = tz - dz;
                if( x < INT32_MIN || x > INT32_MAX || z < INT32_MIN || z > INT32_MAX ) continue;
                const TileCoord coord{ static_cast<int32_t>( x ), static_cast<int32_t>( z ) };
                if( m_settings.Contains( coord ) ) result.push_back( coord );
            }
        return result;
    }

    bool Heightfield::LoadTile( TileCoord InTile, std::vector<float> InHeights, std::string& OutError )
    {
        OutError.clear();
        if( !m_settings.Contains( InTile ) ) OutError = "Tile is outside the terrain extent";
        else if( InHeights.size() != m_settings.SamplesPerTile() ) OutError = "Tile has the wrong height sample count";
        else if( !std::all_of( InHeights.begin(), InHeights.end(), []( float h ) { return std::isfinite( h ); } ) ) OutError = "Tile contains a non-finite height";
        if( !OutError.empty() ) return false;

        const int64_t q = m_settings.TileQuads;
        const SampleCoord origin{ static_cast<int64_t>( InTile.X ) * q, static_cast<int64_t>( InTile.Z ) * q };
        // Only the perimeter is shared. Validate before replacing a previously resident page.
        for( int64_t z = 0; z <= q; ++z )
            for( int64_t x = 0; x <= q; x += ( z == 0 || z == q ? 1 : q ) )
            {
                const SampleCoord sample{ origin.X + x, origin.Z + z };
                for( TileCoord other : SharingTiles( sample ) )
                    if( other != InTile )
                        if( const Tile* tile = FindTile( other ); tile && tile->Heights[Index( other, sample )] != InHeights[Index( InTile, sample )] )
                        {
                            OutError = "Tile shared edge/corner disagrees with a resident neighbor";
                            return false;
                        }
            }
        ++m_revision;
        Tile tile;
        tile.Heights = std::move( InHeights );
        m_tiles.insert_or_assign( InTile, std::move( tile ) );
        m_pending.erase( InTile );
        Invalidate( { { origin.X - 1, origin.Z - 1 }, { origin.X + q + 1, origin.Z + q + 1 } } );
        return true;
    }

    bool Heightfield::CreateFlatTile( TileCoord InTile, float InHeight, std::string& OutError )
    {
        return LoadTile( InTile, std::vector<float>( m_settings.SamplesPerTile(), InHeight ), OutError );
    }

    void Heightfield::Invalidate( SampleRect InGlobalRect )
    {
        if( InGlobalRect.IsEmpty() ) return;
        const int64_t q = m_settings.TileQuads;
        const int64_t x0 = std::max( static_cast<int64_t>( m_settings.MinTile.X ), FloorDivide( InGlobalRect.Min.X, q ) - ( InGlobalRect.Min.X % q == 0 ? 1 : 0 ) );
        const int64_t z0 = std::max( static_cast<int64_t>( m_settings.MinTile.Z ), FloorDivide( InGlobalRect.Min.Z, q ) - ( InGlobalRect.Min.Z % q == 0 ? 1 : 0 ) );
        const int64_t x1 = std::min( static_cast<int64_t>( m_settings.MinTile.X ) + m_settings.TilesX - 1, FloorDivide( InGlobalRect.Max.X, q ) );
        const int64_t z1 = std::min( static_cast<int64_t>( m_settings.MinTile.Z ) + m_settings.TilesZ - 1, FloorDivide( InGlobalRect.Max.Z, q ) );
        for( int64_t x = x0; x <= x1; ++x ) for( int64_t z = z0; z <= z1; ++z )
        {
            const TileCoord coord{ static_cast<int32_t>( x ), static_cast<int32_t>( z ) };
            auto entry = m_tiles.find( coord );
            if( entry == m_tiles.end() ) continue;
            Tile& tile = entry->second;
            const SampleCoord origin{ static_cast<int64_t>( coord.X ) * q, static_cast<int64_t>( coord.Z ) * q };
            const SampleCoord min{ std::max( int64_t( 0 ), InGlobalRect.Min.X - origin.X ), std::max( int64_t( 0 ), InGlobalRect.Min.Z - origin.Z ) };
            const SampleCoord max{ std::min( q, InGlobalRect.Max.X - origin.X ), std::min( q, InGlobalRect.Max.Z - origin.Z ) };
            if( min.X > max.X || min.Z > max.Z ) continue;
            tile.DirtySamples.Include( min ); tile.DirtySamples.Include( max );
            tile.Revision = m_revision;
        }
    }

    void Heightfield::UnloadTile( TileCoord InTile )
    {
        m_pending.erase( InTile );
        if( !m_tiles.erase( InTile ) ) return;
        ++m_revision;
        const int64_t q = m_settings.TileQuads;
        const SampleCoord origin{ static_cast<int64_t>( InTile.X ) * q, static_cast<int64_t>( InTile.Z ) * q };
        Invalidate( { { origin.X - 1, origin.Z - 1 }, { origin.X + q + 1, origin.Z + q + 1 } } );
    }

    bool Heightfield::SetPending( TileCoord InTile, bool InPending )
    {
        if( !m_settings.Contains( InTile ) || FindTile( InTile ) ) return false;
        if( InPending ) m_pending.insert( InTile ); else m_pending.erase( InTile );
        return true;
    }

    void Heightfield::ClearDirty()
    {
        for( auto& [coord, tile] : m_tiles ) tile.DirtySamples = {};
    }

    QueryStatus Heightfield::GetSample( SampleCoord InSample, float& OutHeight ) const
    {
        if( !m_settings.Contains( InSample ) ) return QueryStatus::OutsideBounds;
        const TileCoord owner = m_settings.Owner( InSample );
        const Tile* tile = FindTile( owner );
        if( !tile ) return m_pending.contains( owner ) ? QueryStatus::Pending : QueryStatus::Unavailable;
        OutHeight = tile->Heights[Index( owner, InSample )];
        return QueryStatus::Ready;
    }

    QueryStatus Heightfield::GetNormal( SampleCoord InSample, Vector3& OutNormal ) const
    {
        if( !m_settings.Contains( InSample ) ) return QueryStatus::OutsideBounds;
        const auto min = m_settings.MinSample(), max = m_settings.MaxSample();
        const int64_t x0 = std::max( min.X, InSample.X - 1 ), x1 = std::min( max.X, InSample.X + 1 );
        const int64_t z0 = std::max( min.Z, InSample.Z - 1 ), z1 = std::min( max.Z, InSample.Z + 1 );
        const SampleCoord samples[] = { InSample, { x0, InSample.Z }, { x1, InSample.Z }, { InSample.X, z0 }, { InSample.X, z1 } };
        float heights[5]{};
        for( size_t i = 0; i < 5; ++i )
            if( const auto status = GetSample( samples[i], heights[i] ); status != QueryStatus::Ready ) return status;
        OutNormal = SurfaceNormal( ( static_cast<double>( heights[2] ) - heights[1] ) / ( ( x1 - x0 ) * static_cast<double>( m_settings.SampleSpacing ) ),
            ( static_cast<double>( heights[4] ) - heights[3] ) / ( ( z1 - z0 ) * static_cast<double>( m_settings.SampleSpacing ) ) );
        return QueryStatus::Ready;
    }

    bool Heightfield::SetSample( SampleCoord InSample, float InHeight, std::string& OutError )
    {
        OutError.clear();
        if( !m_settings.Contains( InSample ) || !std::isfinite( InHeight ) ) { OutError = "Sample is outside the terrain or its height is not finite"; return false; }
        const auto sharing = SharingTiles( InSample );
        for( TileCoord coord : sharing )
            if( !FindTile( coord ) ) { OutError = "Load all shared source pages before editing a sample"; return false; }
        if( m_tiles.at( sharing.front() ).Heights[Index( sharing.front(), InSample )] == InHeight ) return true;
        for( TileCoord coord : sharing ) m_tiles.at( coord ).Heights[Index( coord, InSample )] = InHeight;
        ++m_revision;
        Invalidate( { { InSample.X - 1, InSample.Z - 1 }, { InSample.X + 1, InSample.Z + 1 } } );
        return true;
    }

    HeightQuery Heightfield::Query( double InX, double InZ ) const
    {
        HeightQuery result;
        if( !std::isfinite( InX ) || !std::isfinite( InZ ) ) { result.Status = QueryStatus::InvalidInput; return result; }
        const double x = InX / m_settings.SampleSpacing, z = InZ / m_settings.SampleSpacing;
        const auto min = m_settings.MinSample(), max = m_settings.MaxSample();
        if( x < min.X || x > max.X || z < min.Z || z > max.Z ) { result.Status = QueryStatus::OutsideBounds; return result; }
        const int64_t ix = std::min( static_cast<int64_t>( std::floor( x ) ), max.X - 1 );
        const int64_t iz = std::min( static_cast<int64_t>( std::floor( z ) ), max.Z - 1 );
        const double u = x - ix, v = z - iz;
        const SampleCoord samples[] = { { ix, iz }, { ix + 1, iz }, { ix, iz + 1 }, { ix + 1, iz + 1 } };
        float h[4]{};
        for( size_t i = 0; i < 4; ++i )
            if( const auto status = GetSample( samples[i], h[i] ); status != QueryStatus::Ready ) { result.Status = status; return result; }
        double dx, dz, height;
        if( u + v <= 1.0 )
        {
            dx = static_cast<double>( h[1] ) - h[0]; dz = static_cast<double>( h[2] ) - h[0];
            height = h[0] + dx * u + dz * v;
        }
        else
        {
            dx = static_cast<double>( h[3] ) - h[2]; dz = static_cast<double>( h[3] ) - h[1];
            height = h[3] - dx * ( 1.0 - u ) - dz * ( 1.0 - v );
        }
        result.Status = QueryStatus::Ready;
        result.Height = static_cast<float>( height );
        result.Normal = SurfaceNormal( dx / m_settings.SampleSpacing, dz / m_settings.SampleSpacing );
        return result;
    }
}
