#include "PCH.h"
#include "FoliageData.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Foliage
{
    namespace
    {
        bool Finite( const Vector3& v ) { return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ); }


        uint64_t Mix( uint64_t x )
        {
            x += 0x9e3779b97f4a7c15ull; x = ( x ^ ( x >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
            x = ( x ^ ( x >> 27 ) ) * 0x94d049bb133111ebull; return x ^ ( x >> 31 );
        }


        float Random( uint64_t seed, unsigned channel ) { return float( Mix( seed + channel ) >> 40 ) / 16777216.f; }


        uint64_t Identity( const json& value )
        {
            if( !value.is_number_integer() || ( !value.is_number_unsigned() && value.get<int64_t>() < 0 ) ) throw std::runtime_error( "Invalid foliage identity" );
            return value.get<uint64_t>();
        }


        int32_t TileCoordinate( const json& value )
        {
            if( !value.is_number_integer() || ( value.is_number_unsigned() ? value.get<uint64_t>() > uint64_t( INT32_MAX ) :
                value.get<int64_t>() < INT32_MIN || value.get<int64_t>() > INT32_MAX ) ) throw std::runtime_error( "Invalid foliage tile coordinate" );
            return value.get<int32_t>();
        }
    }


    bool Data::Validate( std::string& error ) const
    {
        if( Palette.size() > MaxSpecies || Instances.size() > MaxInstances || !NextID ) { error = "Foliage supports 16 species and 5000 instances per component"; return false; }
        for( const auto& species : Palette ) if( species.Name.size() > 256 || species.Model.empty() || species.Model.size() > 2048 ) { error = "Foliage species needs a model path and a short name"; return false; }
        std::set<uint64_t> ids;
        for( const auto& instance : Instances )
            if( !instance.ID || instance.ID >= NextID || !ids.insert( instance.ID ).second || instance.SpeciesIndex >= Palette.size() ||
                !Finite( instance.Position ) || !Finite( instance.Normal ) || std::abs( instance.Normal.LengthSquared() - 1.f ) > 0.002f ||
                !std::isfinite( instance.Scale ) || instance.Scale < 0.01f || instance.Scale > 100 || !std::isfinite( instance.Yaw ) )
            { error = "Invalid foliage instance, identity, species or transform"; return false; }
        error.clear(); return true;
    }


    json Encode( const Data& data )
    {
        json result = { { "Version", 1 }, { "NextID", data.NextID }, { "Palette", json::array() }, { "Instances", json::array() } };
        for( const auto& s : data.Palette ) result["Palette"].push_back( { { "Name", s.Name }, { "Model", s.Model } } );
        for( const auto& i : data.Instances ) result["Instances"].push_back( {
            { "ID", i.ID }, { "Species", i.SpeciesIndex }, { "Tile", { i.Tile.X, i.Tile.Z } },
            { "Position", { i.Position.x, i.Position.y, i.Position.z } }, { "Normal", { i.Normal.x, i.Normal.y, i.Normal.z } },
            { "Scale", i.Scale }, { "Yaw", i.Yaw }, { "AlignNormal", i.AlignNormal } } );
        return result;
    }


    bool Decode( const json& value, Data& out, std::string& error )
    {
        try
        {
            if( Identity( value.at( "Version" ) ) != 1 || !value.at( "Palette" ).is_array() || !value.at( "Instances" ).is_array() ||
                value["Palette"].size() > MaxSpecies || value["Instances"].size() > MaxInstances ) throw std::runtime_error( "Invalid foliage version or count" );
            Data data;
            data.NextID = Identity( value.at( "NextID" ) );
            for( const auto& s : value["Palette"] ) data.Palette.push_back( { s.at( "Name" ).get<std::string>(), s.at( "Model" ).get<std::string>() } );
            for( const auto& v : value["Instances"] )
            {
                Instance i; i.ID = Identity( v.at( "ID" ) ); const auto species = Identity( v.at( "Species" ) );
                if( species >= data.Palette.size() ) throw std::runtime_error( "Invalid foliage species" ); i.SpeciesIndex = uint32_t( species );
                const auto& tile = v.at( "Tile" ); const auto& p = v.at( "Position" ); const auto& n = v.at( "Normal" );
                if( tile.size() != 2 || p.size() != 3 || n.size() != 3 ) throw std::runtime_error( "Invalid foliage coordinate" );
                i.Tile = { TileCoordinate( tile[0] ), TileCoordinate( tile[1] ) }; i.Position = { p[0].get<float>(), p[1].get<float>(), p[2].get<float>() };
                i.Normal = { n[0].get<float>(), n[1].get<float>(), n[2].get<float>() }; i.Scale = v.at( "Scale" ).get<float>(); i.Yaw = v.at( "Yaw" ).get<float>(); i.AlignNormal = v.value( "AlignNormal", false ); data.Instances.push_back( i );
            }
            if( !data.Validate( error ) ) return false;
            out = std::move( data ); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool Stroke::Begin( const TerrainData::Heightfield& field, const Data& data, const Brush& brush, double x, double z, std::string& error )
    {
        Stop();
        if( !data.Validate( error ) ) return false;
        for( float v : { brush.Radius, brush.Density, brush.MinSpacing, brush.MinScale, brush.MaxScale, brush.MaxSlope, brush.HeightOffset } )
            if( !std::isfinite( v ) ) { error = "Foliage brush values must be finite"; return false; }
        if( brush.SpeciesIndex >= data.Palette.size() || brush.Radius < 0.25f || brush.Radius > 128 || brush.Density < 0.005f || brush.Density > 10 ||
            brush.MinSpacing < 0.25f || brush.MinSpacing > 100 || brush.MinScale < 0.01f || brush.MaxScale > 100 || brush.MaxScale < brush.MinScale ||
            brush.MaxSlope < 0 || brush.MaxSlope > 90 || std::abs( brush.HeightOffset ) > 10 || !std::isfinite( x ) || !std::isfinite( z ) )
        { error = "Invalid foliage brush settings"; return false; }
        m_data = data; m_brush = brush; m_seed = Mix( uint64_t( brush.Seed ) ^ data.NextID ); m_added.clear(); m_removed.clear(); m_visited.clear();
        m_x = x; m_z = z; m_remainder = 0; m_active = true;
        if( !Stamp( field, x, z, error ) ) { Stop(); return false; } return true;
    }


    bool Stroke::Move( const TerrainData::Heightfield& field, double x, double z, std::string& error )
    {
        if( !m_active || !std::isfinite( x ) || !std::isfinite( z ) ) { error = "Invalid foliage stroke position"; return false; }
        const double dx = x - m_x, dz = z - m_z, length = std::hypot( dx, dz ), step = std::max( 0.1, m_brush.Radius * 0.25 );
        if( length / step > 10000 ) { error = "Foliage stroke segment is too long"; return false; }
        for( double distance = step - m_remainder; distance <= length; distance += step )
            if( !Stamp( field, m_x + dx * distance / length, m_z + dz * distance / length, error ) ) return false;
        m_remainder = std::fmod( m_remainder + length, step ); m_x = x; m_z = z; return true;
    }


    bool Stroke::Stamp( const TerrainData::Heightfield& field, double x, double z, std::string& error )
    {
        const double r = m_brush.Radius, r2 = r * r;
        if( m_brush.Erase )
        {
            std::erase_if( m_data.Instances, [&]( const auto& i ) {
                if( ( m_brush.EraseAll || i.SpeciesIndex == m_brush.SpeciesIndex ) && std::hypot( i.Position.x - x, i.Position.z - z ) <= r ) { m_removed.push_back( i ); return true; } return false;
            } ); return true;
        }
        const auto& s = field.GetSettings(); const double cell = std::max( double( m_brush.MinSpacing ), std::sqrt( 1.0 / m_brush.Density ) );
        const double loX = std::max( x - r, double( s.MinSample().X ) * s.SampleSpacing ), hiX = std::min( x + r, double( s.MaxSample().X ) * s.SampleSpacing );
        const double loZ = std::max( z - r, double( s.MinSample().Z ) * s.SampleSpacing ), hiZ = std::min( z + r, double( s.MaxSample().Z ) * s.SampleSpacing );
        if( loX > hiX || loZ > hiZ ) return true;
        const auto x0 = int64_t( std::floor( loX / cell ) ), x1 = int64_t( std::floor( hiX / cell ) ), z0 = int64_t( std::floor( loZ / cell ) ), z1 = int64_t( std::floor( hiZ / cell ) );
        if( uint64_t( x1 - x0 + 1 ) * uint64_t( z1 - z0 + 1 ) > 65536 ) { error = "Reduce foliage radius or density"; return false; }
        for( int64_t cz = z0; cz <= z1; ++cz ) for( int64_t cx = x0; cx <= x1; ++cx )
        {
            if( m_data.Instances.size() >= MaxInstances ) return true;
            const uint64_t seed = Mix( uint64_t( cx ) ^ Mix( uint64_t( cz ) ) ^ m_seed );
            const double px = ( cx + 0.1 + 0.8 * Random( seed, 0 ) ) * cell, pz = ( cz + 0.1 + 0.8 * Random( seed, 1 ) ) * cell;
            if( ( px - x ) * ( px - x ) + ( pz - z ) * ( pz - z ) > r2 || !m_visited.emplace( cx, cz ).second ) continue;
            if( m_visited.size() > 200000 ) { error = "Finish this foliage stroke before painting a larger region"; return false; }
            const auto hit = field.Query( px, pz ); if( hit.Status != TerrainData::QueryStatus::Ready ) continue;
            if( hit.Normal.y < std::cos( m_brush.MaxSlope * 3.141592653589793 / 180.0 ) ) continue;
            if( std::any_of( m_data.Instances.begin(), m_data.Instances.end(), [&]( const auto& i ) { return std::hypot( i.Position.x - px, i.Position.z - pz ) < m_brush.MinSpacing; } ) ) continue;
            if( m_data.NextID == std::numeric_limits<uint64_t>::max() ) { error = "Foliage identity limit reached"; return false; }
            Instance i; i.ID = m_data.NextID++; i.SpeciesIndex = m_brush.SpeciesIndex; i.Position = { float( px ), hit.Height + m_brush.HeightOffset, float( pz ) };
            i.Tile = s.Owner( { int64_t( std::floor( px / s.SampleSpacing ) ), int64_t( std::floor( pz / s.SampleSpacing ) ) } );
            i.Normal = hit.Normal; i.Scale = m_brush.MinScale + ( m_brush.MaxScale - m_brush.MinScale ) * Random( seed, 2 ); i.Yaw = Random( seed, 3 ) * 360; i.AlignNormal = m_brush.AlignNormal;
            m_data.Instances.push_back( i ); m_added.push_back( i );
        }
        return true;
    }
}
