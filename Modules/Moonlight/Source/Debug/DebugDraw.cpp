#include "DebugDraw.h"
#include <algorithm>
#include <cmath>
#include <mutex>

namespace DebugDraw
{
    namespace
    {
        struct Segment
        {
            LineVertex A;
            LineVertex B;
            float Remaining;
            uint8_t Flags;
        };

        std::mutex s_mutex;
        std::vector<Segment> s_segments;

        uint32_t ToABGR( const Vector4& InColor )
        {
            auto channel = []( float value ) { return static_cast<uint32_t>( std::clamp( value, 0.f, 1.f ) * 255.f + 0.5f ); };
            return channel( InColor.x ) | ( channel( InColor.y ) << 8 ) | ( channel( InColor.z ) << 16 ) | ( channel( InColor.w ) << 24 );
        }

        LineVertex MakeVertex( const Vector3& InPosition, uint32_t InColor )
        {
            return { InPosition.x, InPosition.y, InPosition.z, InColor };
        }

        // Batches a shape's segments under one lock.
        class Builder
        {
        public:
            Builder( const Vector4& InColor, float InDuration, uint8_t InFlags )
                : m_color( ToABGR( InColor ) ), m_duration( InDuration ), m_flags( InFlags )
            {
            }

            ~Builder()
            {
                std::lock_guard<std::mutex> lock( s_mutex );
                s_segments.insert( s_segments.end(), m_segments.begin(), m_segments.end() );
            }

            void Add( const Vector3& InA, const Vector3& InB )
            {
                m_segments.push_back( { MakeVertex( InA, m_color ), MakeVertex( InB, m_color ), m_duration, m_flags } );
            }

        private:
            uint32_t m_color;
            float m_duration;
            uint8_t m_flags;
            std::vector<Segment> m_segments;
        };

        // Two unit vectors perpendicular to InNormal (and each other).
        void Basis( const Vector3& InNormal, Vector3& OutU, Vector3& OutV )
        {
            Vector3 n = InNormal.LengthSquared() > 0.f ? InNormal.Normalized() : Vector3::Up;
            const Vector3 helper = std::fabs( n.y ) < 0.99f ? Vector3::Up : Vector3( 1.f, 0.f, 0.f );
            OutU = n.Cross( helper ).Normalized();
            OutV = n.Cross( OutU ).Normalized();
        }

        void AddCircle( Builder& InBuilder, const Vector3& InCenter, const Vector3& InU, const Vector3& InV, float InRadius, int InSegments, float InStart = 0.f, float InSweep = 6.2831853f )
        {
            InSegments = std::max( InSegments, 3 );
            Vector3 previous = InCenter + ( InU * std::cos( InStart ) + InV * std::sin( InStart ) ) * InRadius;
            for( int i = 1; i <= InSegments; ++i )
            {
                const float angle = InStart + InSweep * ( static_cast<float>( i ) / InSegments );
                const Vector3 point = InCenter + ( InU * std::cos( angle ) + InV * std::sin( angle ) ) * InRadius;
                InBuilder.Add( previous, point );
                previous = point;
            }
        }

        void AddBoxCorners( Builder& InBuilder, const Vector3 InCorners[8] )
        {
            // Corner i: bit0 = x, bit1 = y, bit2 = z.
            static const int kEdges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for( const auto& edge : kEdges )
            {
                InBuilder.Add( InCorners[edge[0]], InCorners[edge[1]] );
            }
        }

        Vector3 TransformPoint( const Matrix4& InMatrix, const Vector3& InPoint )
        {
            const glm::vec4 result = InMatrix.GetInternalMatrix() * glm::vec4( InPoint.InternalVector, 1.f );
            return Vector3( result.x / result.w, result.y / result.w, result.z / result.w );
        }
    }


    void Line( const Vector3& InFrom, const Vector3& InTo, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        Builder builder( InColor, InDuration, InFlags );
        builder.Add( InFrom, InTo );
    }


    void Ray( const Vector3& InOrigin, const Vector3& InDirection, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        Line( InOrigin, InOrigin + InDirection, InColor, InDuration, InFlags );
    }


    void Arrow( const Vector3& InFrom, const Vector3& InTo, const Vector4& InColor, float InHeadSize, float InDuration, uint8_t InFlags )
    {
        Builder builder( InColor, InDuration, InFlags );
        builder.Add( InFrom, InTo );
        const Vector3 delta = InTo - InFrom;
        const float length = delta.Length();
        if( length <= 0.f )
        {
            return;
        }
        const float head = InHeadSize > 0.f ? InHeadSize : length * 0.15f;
        const Vector3 direction = delta / length;
        Vector3 u, v;
        Basis( direction, u, v );
        const Vector3 base = InTo - direction * head;
        for( const Vector3& side : { u, u * -1.f, v, v * -1.f } )
        {
            builder.Add( InTo, base + side * ( head * 0.4f ) );
        }
    }


    void Box( const AABB& InBounds, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        Box( Matrix4(), InBounds, InColor, InDuration, InFlags );
    }


    void Box( const Matrix4& InTransform, const AABB& InLocalBounds, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        if( !InLocalBounds.IsValid() )
        {
            return;
        }
        Vector3 corners[8];
        for( int i = 0; i < 8; ++i )
        {
            const Vector3 local( ( i & 1 ) ? InLocalBounds.Max.x : InLocalBounds.Min.x, ( i & 2 ) ? InLocalBounds.Max.y : InLocalBounds.Min.y, ( i & 4 ) ? InLocalBounds.Max.z : InLocalBounds.Min.z );
            corners[i] = TransformPoint( InTransform, local );
        }
        Builder builder( InColor, InDuration, InFlags );
        AddBoxCorners( builder, corners );
    }


    void Circle( const Vector3& InCenter, const Vector3& InNormal, float InRadius, const Vector4& InColor, float InDuration, uint8_t InFlags, int InSegments )
    {
        Vector3 u, v;
        Basis( InNormal, u, v );
        Builder builder( InColor, InDuration, InFlags );
        AddCircle( builder, InCenter, u, v, InRadius, InSegments );
    }


    void Sphere( const Vector3& InCenter, float InRadius, const Vector4& InColor, float InDuration, uint8_t InFlags, int InSegments )
    {
        Builder builder( InColor, InDuration, InFlags );
        const Vector3 x( 1.f, 0.f, 0.f ), y( 0.f, 1.f, 0.f ), z( 0.f, 0.f, 1.f );
        AddCircle( builder, InCenter, x, y, InRadius, InSegments );
        AddCircle( builder, InCenter, y, z, InRadius, InSegments );
        AddCircle( builder, InCenter, z, x, InRadius, InSegments );
    }


    void Capsule( const Vector3& InA, const Vector3& InB, float InRadius, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        const Vector3 axis = InB - InA;
        Vector3 u, v;
        Basis( axis, u, v );
        const Vector3 direction = axis.LengthSquared() > 0.f ? axis.Normalized() : Vector3::Up;
        Builder builder( InColor, InDuration, InFlags );
        AddCircle( builder, InA, u, v, InRadius, 24 );
        AddCircle( builder, InB, u, v, InRadius, 24 );
        for( const Vector3& side : { u, u * -1.f, v, v * -1.f } )
        {
            builder.Add( InA + side * InRadius, InB + side * InRadius );
        }
        // Hemispheres: half circles in the two planes containing the axis.
        const float pi = 3.14159265f;
        AddCircle( builder, InB, u, direction, InRadius, 12, 0.f, pi );
        AddCircle( builder, InB, v, direction, InRadius, 12, 0.f, pi );
        AddCircle( builder, InA, u, direction * -1.f, InRadius, 12, 0.f, pi );
        AddCircle( builder, InA, v, direction * -1.f, InRadius, 12, 0.f, pi );
    }


    void Cone( const Vector3& InApex, const Vector3& InDirection, float InLength, float InAngleDegrees, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        const Vector3 direction = InDirection.LengthSquared() > 0.f ? InDirection.Normalized() : Vector3( 0.f, 0.f, 1.f );
        Vector3 u, v;
        Basis( direction, u, v );
        const float radius = std::tan( InAngleDegrees * 3.14159265f / 180.f ) * InLength;
        const Vector3 base = InApex + direction * InLength;
        Builder builder( InColor, InDuration, InFlags );
        AddCircle( builder, base, u, v, radius, 32 );
        for( const Vector3& side : { u, u * -1.f, v, v * -1.f } )
        {
            builder.Add( InApex, base + side * radius );
        }
    }


    void Axes( const Matrix4& InTransform, float InSize, float InDuration, uint8_t InFlags )
    {
        const Vector3 origin = TransformPoint( InTransform, Vector3() );
        Line( origin, TransformPoint( InTransform, Vector3( InSize, 0.f, 0.f ) ), Red, InDuration, InFlags );
        Line( origin, TransformPoint( InTransform, Vector3( 0.f, InSize, 0.f ) ), Green, InDuration, InFlags );
        Line( origin, TransformPoint( InTransform, Vector3( 0.f, 0.f, InSize ) ), Blue, InDuration, InFlags );
    }


    void Frustum( const Matrix4& InViewProjection, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        const Matrix4 inverse( glm::inverse( InViewProjection.GetInternalMatrix() ) );
        Vector3 corners[8];
        for( int i = 0; i < 8; ++i )
        {
            // NDC cube; depth 0..1 covers both depth conventions closely enough for a debug view.
            const Vector3 ndc( ( i & 1 ) ? 1.f : -1.f, ( i & 2 ) ? 1.f : -1.f, ( i & 4 ) ? 1.f : 0.f );
            corners[i] = TransformPoint( inverse, ndc );
        }
        Builder builder( InColor, InDuration, InFlags );
        AddBoxCorners( builder, corners );
    }


    void Grid( const Vector3& InCenter, const Vector3& InAxisA, const Vector3& InAxisB, int InHalfCount, float InSpacing, const Vector4& InColor, float InDuration, uint8_t InFlags )
    {
        const Vector3 a = InAxisA.Normalized();
        const Vector3 b = InAxisB.Normalized();
        const float extent = InHalfCount * InSpacing;
        Builder builder( InColor, InDuration, InFlags );
        for( int i = -InHalfCount; i <= InHalfCount; ++i )
        {
            const float offset = i * InSpacing;
            builder.Add( InCenter + a * offset - b * extent, InCenter + a * offset + b * extent );
            builder.Add( InCenter + b * offset - a * extent, InCenter + b * offset + a * extent );
        }
    }


    void CollectFrame( FrameLines& OutLines )
    {
        OutLines.Depth.clear();
        OutLines.Overlay.clear();
        OutLines.EditorDepth.clear();
        OutLines.EditorOverlay.clear();

        std::lock_guard<std::mutex> lock( s_mutex );
        for( const Segment& segment : s_segments )
        {
            const bool editor = ( segment.Flags & EditorOnly ) != 0;
            const bool overlay = ( segment.Flags & NoDepthTest ) != 0;
            std::vector<LineVertex>& target = editor ? ( overlay ? OutLines.EditorOverlay : OutLines.EditorDepth ) : ( overlay ? OutLines.Overlay : OutLines.Depth );
            target.push_back( segment.A );
            target.push_back( segment.B );
        }
    }


    void EndFrame( float InDeltaSeconds )
    {
        std::lock_guard<std::mutex> lock( s_mutex );
        for( Segment& segment : s_segments )
        {
            segment.Remaining -= InDeltaSeconds;
        }
        s_segments.erase( std::remove_if( s_segments.begin(), s_segments.end(), []( const Segment& segment ) { return segment.Remaining <= 0.f; } ), s_segments.end() );
    }


    void Clear()
    {
        std::lock_guard<std::mutex> lock( s_mutex );
        s_segments.clear();
    }


    size_t GetLineCount()
    {
        std::lock_guard<std::mutex> lock( s_mutex );
        return s_segments.size();
    }
}
