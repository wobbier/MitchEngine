#include "PCH.h"
#include "TerrainEditing.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace TerrainData
{
    bool ValidateAuthoringSettings( const Settings& settings, std::string& error )
    {
        if( !settings.Validate( error ) ) return false;
        if( uint64_t( settings.TilesX ) * settings.TilesZ > MaxAuthoringSamples / settings.SamplesPerTile() )
        { error = "Resident terrain authoring supports up to one million source samples; split the source or use a cooked runtime archive"; return false; }
        return true;
    }


    bool BrushStroke::Begin( Heightfield& field, const BrushSettings& brush, double x, double z, std::string& error )
    {
        if( m_active ) { error = "Finish the current stroke first"; return false; }
        const auto& s = field.GetSettings();
        if( !std::isfinite( x ) || !std::isfinite( z ) || !std::isfinite( brush.Radius ) ||
            !std::isfinite( brush.Strength ) || !std::isfinite( brush.Falloff ) || !std::isfinite( brush.Spacing ) ||
            brush.Radius < s.SampleSpacing * 0.25f || brush.Radius > s.SampleSpacing * 128.f || brush.Strength < 0 ||
            static_cast<unsigned>( brush.Tool ) > static_cast<unsigned>( BrushTool::Erase ) || brush.Layer >= 4 ||
            brush.Falloff < 0 || brush.Falloff > 1 || brush.Spacing < 0.05f || brush.Spacing > 1 )
        { error = "Invalid brush settings (maximum radius: 128 sample intervals)"; return false; }
        if( field.GetTiles().size() != static_cast<size_t>( s.TilesX ) * s.TilesZ )
        { error = "Load every source tile before sculpting"; return false; }
        auto hit = field.Query( x, z );
        if( hit.Status != QueryStatus::Ready ) { error = "Start the stroke on the terrain"; return false; }
        m_beforeSurface = field.GetSurface(); m_paint.reset();
        if( brush.Tool == BrushTool::Paint || brush.Tool == BrushTool::Erase )
            m_paint = m_beforeSurface ? std::make_unique<Surface>( *m_beforeSurface ) : std::make_unique<Surface>();
        m_brush = brush; m_flattenHeight = hit.Height;
        m_x = x; m_z = z; m_remaining = brush.Radius * brush.Spacing;
        m_deltas.clear(); m_active = true;
        if( !Stamp( field, x, z, error ) || ( m_paint && !field.SetSurface( std::make_shared<Surface>( *m_paint ), error ) ) ) { Cancel( field ); return false; }
        return true;
    }

    bool BrushStroke::Move( Heightfield& field, double x, double z, std::string& error )
    {
        if( !m_active || !std::isfinite( x ) || !std::isfinite( z ) ) { error = "Invalid stroke position"; return false; }
        const double dx = x - m_x, dz = z - m_z, length = std::hypot( dx, dz );
        // Bound work for discontinuous input (e.g. changing the camera during capture).
        if( length > m_brush.Radius * 1024.0 ) { error = "Stroke movement is too large"; return false; }
        if( length > 0 )
        {
            double distance = m_remaining;
            while( distance <= length )
            {
                if( !Stamp( field, m_x + dx * distance / length, m_z + dz * distance / length, error ) ) return false;
                distance += m_brush.Radius * m_brush.Spacing;
            }
            m_remaining = distance - length;
        }
        m_x = x; m_z = z;
        return !m_paint || field.SetSurface( std::make_shared<Surface>( *m_paint ), error );
    }

    bool BrushStroke::Stamp( Heightfield& field, double x, double z, std::string& error )
    {
        const auto& s = field.GetSettings();
        const auto min = s.MinSample(), max = s.MaxSample();
        const double cx = x / s.SampleSpacing, cz = z / s.SampleSpacing, radius = m_brush.Radius / s.SampleSpacing;
        if( cx + radius < min.X || cz + radius < min.Z || cx - radius > max.X || cz - radius > max.Z ) return true;
        const int64_t x0 = static_cast<int64_t>( std::max( std::ceil( cx - radius ), double( min.X ) ) );
        const int64_t z0 = static_cast<int64_t>( std::max( std::ceil( cz - radius ), double( min.Z ) ) );
        const int64_t x1 = static_cast<int64_t>( std::min( std::floor( cx + radius ), double( max.X ) ) );
        const int64_t z1 = static_cast<int64_t>( std::min( std::floor( cz + radius ), double( max.Z ) ) );
        // Gather all results before applying: smoothing always reads the same neighborhood.
        std::vector<SampleDelta> changes;
        for( int64_t sz = z0; sz <= z1; ++sz ) for( int64_t sx = x0; sx <= x1; ++sx )
        {
            const double d = std::hypot( sx - cx, sz - cz ) / radius;
            if( d >= 1 ) continue;
            float height = 0;
            if( field.GetSample( { sx, sz }, height ) != QueryStatus::Ready ) { error = "Source sample is unavailable"; return false; }
            const double edge = m_brush.Falloff == 0 ? 1 : std::clamp( ( 1 - d ) / m_brush.Falloff, 0.0, 1.0 );
            const float weight = static_cast<float>( edge * edge * ( 3 - 2 * edge ) );
            if( m_paint )
            {
                const SampleCoord coordinate{ sx, sz };
                m_paint->Set( coordinate, BlendLayer( m_paint->Get( coordinate ), m_brush.Layer, std::min( m_brush.Strength, 1.f ) * weight, m_brush.Tool == BrushTool::Erase ) );
                continue;
            }
            float result = height;
            if( m_brush.Tool == BrushTool::Raise || m_brush.Tool == BrushTool::Lower )
                result += ( m_brush.Tool == BrushTool::Lower ? -1 : 1 ) * m_brush.Strength * weight;
            else
            {
                float target = m_flattenHeight;
                if( m_brush.Tool == BrushTool::Smooth )
                {
                    double total = 0; int count = 0;
                    for( int dz = -1; dz <= 1; ++dz ) for( int dx = -1; dx <= 1; ++dx )
                    {
                        float value;
                        if( field.GetSample( { sx + dx, sz + dz }, value ) == QueryStatus::Ready ) { total += value; ++count; }
                    }
                    target = static_cast<float>( total / count );
                }
                result += ( target - height ) * std::min( m_brush.Strength, 1.f ) * weight;
            }
            result = std::clamp( result, s.HeightBase, s.HeightBase + s.HeightRange );
            if( result != height ) changes.push_back( { { sx, sz }, height, result } );
        }
        if( m_deltas.size() + changes.size() > 1024 * 1024 ) { error = "Stroke exceeds the one-million-sample limit; start another stroke"; return false; }
        for( const auto& change : changes )
        {
            if( !field.SetSample( change.Coordinate, change.After, error ) ) return false;
            auto [it, inserted] = m_deltas.emplace( std::make_pair( change.Coordinate.X, change.Coordinate.Z ), change );
            if( !inserted ) it->second.After = change.After;
        }
        return true;
    }

    std::vector<SampleDelta> BrushStroke::Finish()
    {
        std::vector<SampleDelta> result;
        for( const auto& [key, delta] : m_deltas ) if( delta.Before != delta.After ) result.push_back( delta );
        m_deltas.clear(); m_active = false; m_paint.reset(); m_beforeSurface.reset();
        return result;
    }

    void BrushStroke::Cancel( Heightfield& field )
    {
        std::string error;
        if( m_paint ) field.SetSurface( m_beforeSurface, error );
        m_paint.reset(); m_beforeSurface.reset();
        for( const auto& [key, delta] : m_deltas ) field.SetSample( delta.Coordinate, delta.Before, error );
        m_deltas.clear(); m_active = false;
    }

    bool RaycastHeightfield( const Heightfield& field, const Ray& ray, Vector3& point, float maxDistance )
    {
        if( !std::isfinite( maxDistance ) || maxDistance <= 0 ) return false;
        for( int i = 0; i < 3; ++i ) if( !std::isfinite( ray.Origin[i] ) || !std::isfinite( ray.Direction[i] ) ) return false;
        const auto& s = field.GetSettings();
        const auto min = s.MinSample(), max = s.MaxSample();
        // Clip to the finite X/Z slab. DDA visits each intersected quad once, even at grazing angles.
        double enter = 0, leave = maxDistance;
        for( int axis : { 0, 2 } )
        {
            const double lo = ( axis == 0 ? min.X : min.Z ) * double( s.SampleSpacing );
            const double hi = ( axis == 0 ? max.X : max.Z ) * double( s.SampleSpacing );
            const double o = ray.Origin[axis], d = ray.Direction[axis];
            if( std::abs( d ) < 1e-12 ) { if( o < lo || o > hi ) return false; continue; }
            double a = ( lo - o ) / d, b = ( hi - o ) / d;
            if( a > b ) std::swap( a, b );
            enter = std::max( enter, a ); leave = std::min( leave, b );
            if( enter > leave ) return false;
        }
        const double spacing = s.SampleSpacing;
        auto cellAt = [&]( int axis, double t ) {
            const double value = ( ray.Origin[axis] + ray.Direction[axis] * t ) / spacing;
            int64_t cell = static_cast<int64_t>( std::floor( value ) );
            if( ray.Direction[axis] < 0 && std::abs( value - std::round( value ) ) < 1e-8 ) --cell;
            return std::clamp( cell, axis == 0 ? min.X : min.Z, ( axis == 0 ? max.X : max.Z ) - 1 );
        };
        int64_t x = cellAt( 0, enter ), z = cellAt( 2, enter );
        const int stepX = ray.Direction.x > 0 ? 1 : -1, stepZ = ray.Direction.z > 0 ? 1 : -1;
        const double infinity = std::numeric_limits<double>::infinity();
        const double dtX = std::abs( ray.Direction.x ) < 1e-12 ? infinity : spacing / std::abs( ray.Direction.x );
        const double dtZ = std::abs( ray.Direction.z ) < 1e-12 ? infinity : spacing / std::abs( ray.Direction.z );
        double nextX = std::isinf( dtX ) ? infinity : ( ( x + ( stepX > 0 ? 1 : 0 ) ) * spacing - ray.Origin.x ) / ray.Direction.x;
        double nextZ = std::isinf( dtZ ) ? infinity : ( ( z + ( stepZ > 0 ? 1 : 0 ) ) * spacing - ray.Origin.z ) / ray.Direction.z;
        while( x >= min.X && x < max.X && z >= min.Z && z < max.Z && enter <= leave )
        {
            float heights[4];
            const SampleCoord coords[] = { { x, z }, { x + 1, z }, { x, z + 1 }, { x + 1, z + 1 } };
            bool ready = true;
            for( int i = 0; i < 4; ++i ) ready &= field.GetSample( coords[i], heights[i] ) == QueryStatus::Ready;
            if( ready )
            {
                const double end = std::min( { nextX, nextZ, leave } );
                double nearest = infinity;
                const int indices[2][3] = { { 0, 2, 1 }, { 1, 2, 3 } };
                // Double precision barycentric triangle test in cell-relative coordinates.
                for( const auto& tri : indices )
                {
                    glm::dvec3 v[3];
                    for( int i = 0; i < 3; ++i ) v[i] = { ( coords[tri[i]].X - x ) * spacing, heights[tri[i]], ( coords[tri[i]].Z - z ) * spacing };
                    const glm::dvec3 origin( ray.Origin.x - x * spacing, ray.Origin.y, ray.Origin.z - z * spacing );
                    const glm::dvec3 dir( ray.Direction.x, ray.Direction.y, ray.Direction.z );
                    const auto e1 = v[1] - v[0], e2 = v[2] - v[0], p = glm::cross( dir, e2 );
                    const double det = glm::dot( e1, p );
                    if( std::abs( det ) < 1e-12 ) continue;
                    const auto delta = origin - v[0], q = glm::cross( delta, e1 );
                    const double u = glm::dot( delta, p ) / det, w = glm::dot( dir, q ) / det, t = glm::dot( e2, q ) / det;
                    if( u >= -1e-7 && w >= -1e-7 && u + w <= 1 + 1e-7 && t >= enter - 1e-6 && t <= end + 1e-6 && t >= 0 ) nearest = std::min( nearest, t );
                }
                if( std::isfinite( nearest ) ) { point = ray.GetPoint( static_cast<float>( nearest ) ); return true; }
            }
            const double next = std::min( nextX, nextZ );
            if( !std::isfinite( next ) || next > leave ) break;
            const bool crossX = nextX <= nextZ, crossZ = nextZ <= nextX;
            if( crossX ) { x += stepX; nextX += dtX; }
            if( crossZ ) { z += stepZ; nextZ += dtZ; }
            enter = next;
        }
        return false;
    }
}
