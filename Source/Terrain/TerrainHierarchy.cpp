#include "PCH.h"
#include "TerrainHierarchy.h"
#include "TerrainStreamArchive.h"
#include "TerrainSurface.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace TerrainData
{
    namespace
    {
        struct Rect { int64_t X, Z, Size; };
        struct Contact { size_t A, B; uint8_t Edge; };


        std::vector<Contact> Contacts( std::span<const Rect> rectangles )
        {
            struct Edge { int64_t Begin, End; size_t Index; };
            struct Line { std::vector<Edge> Low, High; };
            std::map<int64_t, Line> vertical, horizontal;
            for( size_t i = 0; i < rectangles.size(); ++i )
            {
                const auto& r = rectangles[i];
                vertical[r.X].Low.push_back( { r.Z, r.Z + r.Size, i } ); vertical[r.X + r.Size].High.push_back( { r.Z, r.Z + r.Size, i } );
                horizontal[r.Z].Low.push_back( { r.X, r.X + r.Size, i } ); horizontal[r.Z + r.Size].High.push_back( { r.X, r.X + r.Size, i } );
            }
            std::vector<Contact> result;
            const auto visit = [&]( auto& lines, uint8_t edge ) {
                for( auto& [coordinate, line] : lines )
                {
                    const auto less = []( const Edge& a, const Edge& b ) { return a.Begin < b.Begin; };
                    std::sort( line.Low.begin(), line.Low.end(), less ); std::sort( line.High.begin(), line.High.end(), less );
                    size_t a = 0, b = 0;
                    while( a < line.High.size() && b < line.Low.size() )
                    {
                        const auto& x = line.High[a]; const auto& y = line.Low[b];
                        if( std::max( x.Begin, y.Begin ) < std::min( x.End, y.End ) ) result.push_back( { x.Index, y.Index, edge } );
                        if( x.End <= y.End ) ++a; else ++b;
                    }
                }
            };
            visit( vertical, 1 ); visit( horizontal, 3 ); return result;
        }


        std::vector<Rect> Rectangles( const std::vector<CoverageNode>& nodes, const std::set<uint32_t>& cut )
        {
            std::vector<Rect> rectangles; rectangles.reserve( cut.size() );
            for( auto i : cut ) { const auto& n = nodes[i]; rectangles.push_back( { n.Coordinate.X, n.Coordinate.Z, int64_t( 1 ) << n.Level } ); }
            return rectangles;
        }


        bool Descendant( const std::vector<CoverageNode>& nodes, uint32_t node, uint32_t ancestor )
        {
            while( nodes[node].Level < nodes[ancestor].Level && nodes[node].Parent >= 0 ) node = uint32_t( nodes[node].Parent );
            return node == ancestor;
        }


        void Balance( const std::vector<CoverageNode>& nodes, std::set<uint32_t>& cut, bool refine, const std::function<bool( uint32_t )>& ready = {} )
        {
            for( ;; )
            {
                const auto links = Contacts( Rectangles( nodes, cut ) ); const std::vector<uint32_t> ids( cut.begin(), cut.end() );
                bool changed = false;
                for( const auto& link : links )
                {
                    auto a = ids[link.A], b = ids[link.B]; if( nodes[a].Level < nodes[b].Level ) std::swap( a, b );
                    if( nodes[a].Level <= nodes[b].Level + 1 ) continue;
                    if( refine ) { cut.erase( a ); for( auto child : nodes[a].Children ) cut.insert( uint32_t( child ) ); }
                    else
                    {
                        uint32_t parent = b;
                        do { parent = uint32_t( nodes[parent].Parent ); }
                        while( parent != uint32_t( nodes[b].Root ) && ( nodes[parent].Level + 1 < nodes[a].Level || !ready( parent ) ) );
                        std::erase_if( cut, [&]( uint32_t item ) { return Descendant( nodes, item, parent ); } ); cut.insert( parent );
                    }
                    changed = true; break;
                }
                if( !changed ) break;
            }
        }
    }


    bool BuildCoverageHierarchy( const Settings& s, std::vector<CoverageNode>& out, std::vector<uint32_t>& rootsOut, std::string& error )
    {
        try
        {
            if( !s.Validate( error ) ) throw std::runtime_error( error );
            if( uint64_t( s.TilesX ) * s.TilesZ > 65536 ) throw std::runtime_error( "Terrain hierarchy exceeds the tile limit" );
            std::vector<CoverageNode> nodes; std::map<std::tuple<int32_t, int32_t, uint8_t>, uint32_t> indices;
            for( uint8_t level = 0; level <= 16; ++level )
            {
                const uint32_t side = 1u << level;
                for( uint32_t x = 0; x + side <= s.TilesX; x += side ) for( uint32_t z = 0; z + side <= s.TilesZ; z += side )
                {
                    CoverageNode node; node.Coordinate = { int32_t( int64_t( s.MinTile.X ) + x ), int32_t( int64_t( s.MinTile.Z ) + z ) }; node.Level = level;
                    if( level ) for( int child = 0; child < 4; ++child )
                    {
                        const auto dx = ( child / 2 ) * ( side / 2 ), dz = ( child % 2 ) * ( side / 2 );
                        const uint32_t i = indices.at( { int32_t( int64_t( node.Coordinate.X ) + dx ), int32_t( int64_t( node.Coordinate.Z ) + dz ), uint8_t( level - 1 ) } );
                        node.Children[child] = int32_t( i ); nodes[i].Parent = int32_t( nodes.size() );
                    }
                    indices[{ node.Coordinate.X, node.Coordinate.Z, level }] = uint32_t( nodes.size() ); nodes.push_back( node );
                }
            }
            std::set<uint32_t> roots;
            for( uint32_t i = 0; i < nodes.size(); ++i ) if( nodes[i].Parent < 0 ) roots.insert( i );
            Balance( nodes, roots, true );
            const auto assign = [&]( const auto& self, uint32_t i, int32_t root ) -> void {
                nodes[i].Root = root; if( nodes[i].Level ) for( auto child : nodes[i].Children ) self( self, uint32_t( child ), root );
            };
            for( auto root : roots ) assign( assign, root, int32_t( root ) );
            out = std::move( nodes ); rootsOut.assign( roots.begin(), roots.end() ); error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool BuildHierarchyPage( const Heightfield& field, const CoverageNode& node, uint8_t base, std::shared_ptr<const StreamCoverageTile>& out, std::string& error )
    {
        try
        {
            const auto& s = field.GetSettings();
            if( base >= std::bit_width( s.PatchQuads ) - 1 ) throw std::runtime_error( "Hierarchy patches need at least two quads" );
            const uint32_t q = s.PatchQuads >> base, count = s.TileQuads / s.PatchQuads;
            if( !q || node.Level > 16 || base + node.Level > 30 ) throw std::runtime_error( "Invalid hierarchical patch grid" );
            const uint32_t span = 1u << node.Level, extent = s.PatchQuads * span, stride = 1u << ( base + node.Level );
            auto page = std::make_shared<StreamCoverageTile>(); page->Coordinate = node.Coordinate;
            for( uint32_t pz = 0; pz < count; ++pz ) for( uint32_t px = 0; px < count; ++px )
            {
                auto patch = std::make_shared<ResidentPatch>(); patch->BaseLod = base + node.Level; patch->SpanLevel = node.Level;
                auto& mesh = patch->Mesh; mesh.SampleOrigin = { int64_t( node.Coordinate.X ) * s.TileQuads + px * extent, int64_t( node.Coordinate.Z ) * s.TileQuads + pz * extent };
                patch->Offset = { float( mesh.SampleOrigin.X * double( s.SampleSpacing ) ), 0, float( mesh.SampleOrigin.Z * double( s.SampleSpacing ) ) };
                for( uint32_t z = 0; z <= q; ++z ) for( uint32_t x = 0; x <= q; ++x )
                {
                    PatchVertex v; const SampleCoord sample{ mesh.SampleOrigin.X + x * stride, mesh.SampleOrigin.Z + z * stride };
                    float height;
                    if( field.GetSample( sample, height ) != QueryStatus::Ready || field.GetNormal( sample, v.Normal ) != QueryStatus::Ready ) throw std::runtime_error( "Hierarchy source samples or normal halo are missing" );
                    v.Position = { x * stride * s.SampleSpacing, height, z * stride * s.SampleSpacing }; v.U = float( x ) / q; v.V = float( z ) / q; mesh.Vertices.push_back( v );
                }
                if( !BuildPatchLods( mesh, q, error ) ) throw std::runtime_error( error );
                for( auto& lod : mesh.Lods ) lod.MaxError = 0;
                for( uint32_t z = 0; z <= extent; ++z ) for( uint32_t x = 0; x <= extent; ++x )
                {
                    float height; if( field.GetSample( { mesh.SampleOrigin.X + x, mesh.SampleOrigin.Z + z }, height ) != QueryStatus::Ready ) throw std::runtime_error( "Hierarchy canonical heights are missing" );
                    mesh.Bounds.Encapsulate( { x * s.SampleSpacing, height, z * s.SampleSpacing } );
                    for( auto& lod : mesh.Lods )
                    {
                        const uint32_t step = stride * lod.Stride, gx = std::min( x / step, extent / step - 1 ) * lod.Stride, gz = std::min( z / step, extent / step - 1 ) * lod.Stride;
                        const double u = double( x - gx * stride ) / step, v = double( z - gz * stride ) / step;
                        const auto h = [&]( uint32_t dx, uint32_t dz ) { return double( mesh.Vertices[size_t( gz + dz ) * ( q + 1 ) + gx + dx].Position.y ); };
                        const double predicted = u + v <= 1 ? h( 0, 0 ) + u * ( h( lod.Stride, 0 ) - h( 0, 0 ) ) + v * ( h( 0, lod.Stride ) - h( 0, 0 ) ) :
                            h( lod.Stride, lod.Stride ) + ( 1 - u ) * ( h( 0, lod.Stride ) - h( lod.Stride, lod.Stride ) ) + ( 1 - v ) * ( h( lod.Stride, 0 ) - h( lod.Stride, lod.Stride ) );
                        lod.MaxError = std::max( lod.MaxError, std::abs( height - predicted ) == 0 ? 0.f : std::nextafter( float( std::abs( height - predicted ) ), std::numeric_limits<float>::infinity() ) );
                    }
                }
                if( field.GetSurface() ) for( uint32_t z = 0; z <= s.PatchQuads; ++z ) for( uint32_t x = 0; x <= s.PatchQuads; ++x )
                    patch->Weights.push_back( field.GetSurface()->Get( { mesh.SampleOrigin.X + x * span, mesh.SampleOrigin.Z + z * span } ) );
                page->Patches.push_back( patch );
            }
            out = page; error.clear(); return true;
        }
        catch( const std::exception& e ) { error = e.what(); return false; }
    }


    bool SelectCoverageRequests( const StreamArchive& archive, std::span<const CoverageView> views, size_t maximum, std::vector<uint32_t>& out, std::string& error )
    {
        if( !archive.Hierarchical || maximum < archive.Roots.size() || maximum > 65536 ) { error = "Invalid hierarchy selection limit"; return false; }
        for( const auto& v : views ) if( !std::isfinite( v.Position.x ) || !std::isfinite( v.Position.z ) || !std::isfinite( v.FineRadius ) || !std::isfinite( v.CoverageRadius ) || v.FineRadius < 0 || v.CoverageRadius < 0 ) { error = "Invalid hierarchy view"; return false; }
        struct Candidate { double Priority; uint32_t Node; bool operator<( const Candidate& b ) const { return Priority != b.Priority ? Priority > b.Priority : Node > b.Node; } };
        std::priority_queue<Candidate> queue; std::set<uint32_t> cut( archive.Roots.begin(), archive.Roots.end() );
        const auto consider = [&]( uint32_t i ) {
            const auto& n = archive.Nodes[i]; if( !n.Level ) return;
            const double size = ( uint64_t( 1 ) << n.Level ) * archive.Config.TileQuads * double( archive.Config.SampleSpacing );
            const double x = double( n.Coordinate.X ) * archive.Config.TileQuads * archive.Config.SampleSpacing, z = double( n.Coordinate.Z ) * archive.Config.TileQuads * archive.Config.SampleSpacing;
            double priority = std::numeric_limits<double>::infinity();
            for( const auto& view : views )
            {
                const double distance = std::max( { x - view.Position.x, view.Position.x - ( x + size ), z - view.Position.z, view.Position.z - ( z + size ), 0.0 } );
                if( distance <= view.CoverageRadius && distance <= std::max( double( view.FineRadius ), size * 1.5 ) ) priority = std::min( priority, distance / size );
            }
            if( std::isfinite( priority ) ) queue.push( { priority, i } );
        };
        for( auto i : cut ) consider( i );
        const size_t reserve = std::max( archive.Roots.size(), maximum * 3 / 4 );
        while( !queue.empty() && cut.size() + 3 <= reserve )
        {
            const auto item = queue.top(); queue.pop(); if( !cut.erase( item.Node ) ) continue;
            for( auto child : archive.Nodes[item.Node].Children ) { cut.insert( uint32_t( child ) ); consider( uint32_t( child ) ); }
        }
        Balance( archive.Nodes, cut, false, []( uint32_t ) { return true; } );
        std::set<uint32_t> requests = cut;
        for( auto i : cut ) while( i != uint32_t( archive.Nodes[i].Root ) ) { i = uint32_t( archive.Nodes[i].Parent ); requests.insert( i ); }
        out.assign( requests.begin(), requests.end() ); error.clear(); return true;
    }


    std::vector<uint32_t> ReadyCoverageCut( const StreamArchive& archive, const std::function<bool( uint32_t )>& wanted, const std::function<bool( uint32_t )>& ready )
    {
        std::set<uint32_t> cut;
        const auto visit = [&]( const auto& self, uint32_t i ) -> void {
            const auto& n = archive.Nodes[i];
            const bool children = n.Level && std::all_of( n.Children.begin(), n.Children.end(), [&]( int32_t c ) { return wanted( uint32_t( c ) ) && ready( uint32_t( c ) ); } );
            if( children ) for( auto child : n.Children ) self( self, uint32_t( child ) ); else cut.insert( i );
        };
        for( auto root : archive.Roots ) visit( visit, root );
        Balance( archive.Nodes, cut, false, ready ); return { cut.begin(), cut.end() };
    }


    void BuildHierarchyNeighbors( ResidentTerrain& terrain )
    {
        std::vector<Rect> rectangles; const auto q = terrain.Heights->GetSettings().PatchQuads;
        for( const auto& p : terrain.Patches ) rectangles.push_back( { p->Mesh.SampleOrigin.X, p->Mesh.SampleOrigin.Z, int64_t( q ) << p->SpanLevel } );
        std::vector<std::vector<ResidentTerrain::Neighbor>> neighbors( rectangles.size() );
        for( const auto& c : Contacts( rectangles ) ) { neighbors[c.A].push_back( { int32_t( c.B ), c.Edge } ); neighbors[c.B].push_back( { int32_t( c.A ), uint8_t( c.Edge ^ 1 ) } ); }
        terrain.NeighborOffsets.clear(); terrain.NeighborLinks.clear(); terrain.PatchNeighbors.assign( rectangles.size(), { -1, -1, -1, -1 } );
        for( size_t i = 0; i < neighbors.size(); ++i )
        {
            terrain.NeighborOffsets.push_back( uint32_t( terrain.NeighborLinks.size() ) );
            for( const auto& n : neighbors[i] ) { terrain.NeighborLinks.push_back( n ); terrain.PatchNeighbors[i][n.Edge] = n.Index; }
        }
        terrain.NeighborOffsets.push_back( uint32_t( terrain.NeighborLinks.size() ) );
    }
}
