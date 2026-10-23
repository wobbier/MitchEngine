#include "TreeBuild.h"
#include "Math/Hash.h"
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <algorithm>
#include <cmath>

namespace MitchTree::Detail
{
    namespace
    {
        constexpr float kPi = 3.14159265f;
        constexpr size_t kMaxCards = 60000;
        const glm::vec3 kUp( 0.f, 1.f, 0.f );

        float Radians( float InDegrees )
        {
            return InDegrees * ( kPi / 180.f );
        }


        glm::vec3 ToGlm( const Vector3& InVector )
        {
            return glm::vec3( InVector.x, InVector.y, InVector.z );
        }


        Vector3 ToVector( const glm::vec3& InVector )
        {
            return Vector3( InVector.x, InVector.y, InVector.z );
        }


        glm::vec3 SafeNormalize( const glm::vec3& InVector, const glm::vec3& InFallback )
        {
            const float length = glm::length( InVector );
            return length > 1e-6f ? InVector / length : InFallback;
        }


        glm::vec3 Perpendicular( const glm::vec3& InDirection, const glm::vec3& InHint )
        {
            glm::vec3 projected = InHint - InDirection * glm::dot( InHint, InDirection );
            if( glm::length( projected ) < 1e-3f )
            {
                const glm::vec3 other = std::fabs( InDirection.x ) < 0.9f ? glm::vec3( 1.f, 0.f, 0.f ) : glm::vec3( 0.f, 0.f, 1.f );
                projected = other - InDirection * glm::dot( other, InDirection );
            }
            return glm::normalize( projected );
        }


        // A leaf picture's texture frame: its stalk pivot and tip axis, and the picture's extent along
        // and across that axis (texture units).
        struct RegionFrame
        {
            glm::vec2 Pivot{ 0.5f, 1.f };
            glm::vec2 Axis{ 0.f, -1.f };
            glm::vec2 Across{ 1.f, 0.f };
            float AlongMin = 0.f, AlongMax = 1.f, AcrossMin = -0.5f, AcrossMax = 0.5f;
            float Weight = 1.f;

            glm::vec2 UV( float InAlong, float InAcross ) const
            {
                return Pivot + Axis * InAlong + Across * InAcross;
            }
        };


        RegionFrame MakeRegion( const LeafRegion& InRegion )
        {
            RegionFrame frame;
            frame.Pivot = glm::vec2( InRegion.Pivot.x, InRegion.Pivot.y );
            const float angle = Radians( InRegion.Angle );
            frame.Axis = glm::vec2( std::sin( angle ), -std::cos( angle ) );
            frame.Across = glm::vec2( std::cos( angle ), std::sin( angle ) );
            frame.Weight = InRegion.Weight;
            const glm::vec2 corners[4] = { { InRegion.Rect.x, InRegion.Rect.y }, { InRegion.Rect.z, InRegion.Rect.y },
                { InRegion.Rect.x, InRegion.Rect.w }, { InRegion.Rect.z, InRegion.Rect.w } };
            frame.AlongMin = frame.AcrossMin = 1e9f;
            frame.AlongMax = frame.AcrossMax = -1e9f;
            for( const glm::vec2& corner : corners )
            {
                const glm::vec2 offset = corner - frame.Pivot;
                frame.AlongMin = std::min( frame.AlongMin, glm::dot( offset, frame.Axis ) );
                frame.AlongMax = std::max( frame.AlongMax, glm::dot( offset, frame.Axis ) );
                frame.AcrossMin = std::min( frame.AcrossMin, glm::dot( offset, frame.Across ) );
                frame.AcrossMax = std::max( frame.AcrossMax, glm::dot( offset, frame.Across ) );
            }
            frame.AlongMax = std::max( frame.AlongMax, 1e-3f );
            return frame;
        }


        std::vector<RegionFrame> MakeRegions( const LeafSettings& InLeaves )
        {
            std::vector<RegionFrame> regions;
            for( const LeafRegion& region : InLeaves.Regions )
            {
                if( region.Weight > 0.f ) regions.push_back( MakeRegion( region ) );
            }
            if( regions.empty() ) regions.push_back( MakeRegion( LeafRegion() ) );
            return regions;
        }


        const RegionFrame& PickRegion( const std::vector<RegionFrame>& InRegions, Pcg32& InRng )
        {
            float total = 0.f;
            for( const RegionFrame& region : InRegions ) total += region.Weight;
            float pick = InRng.Unit() * total;
            for( const RegionFrame& region : InRegions )
            {
                pick -= region.Weight;
                if( pick <= 0.f ) return region;
            }
            return InRegions.back();
        }


        struct CardPose
        {
            glm::vec3 Attach{ 0.f };
            glm::vec3 Direction{ 0.f, 1.f, 0.f };   // stalk to tip
            glm::vec3 Normal{ 0.f, 0.f, 1.f };      // blade front
            glm::vec3 Side{ 1.f, 0.f, 0.f };        // across the blade
            float Size = 0.3f;
            float Curl = 0.f;                       // radians over the length
            uint8_t Phase = 0;
            uint8_t Sway = 0;
        };


        // A curled card: three rows along the leaf, two columns across, mapped onto its picture.
        void AddCard( const CardPose& InPose, const RegionFrame& InRegion, float InWidth, Pcg32& InRng, TreePart& OutPart, std::vector<LeafCard>& OutCards, BuildContext& InContext )
        {
            std::vector<TreeVertex>& vertices = OutPart.Vertices;
            const uint32_t first = static_cast<uint32_t>( vertices.size() );
            const float scale = InPose.Size / InRegion.AlongMax;
            const float kappa = std::fabs( InPose.Curl ) > 1e-4f ? InPose.Curl / InPose.Size : 0.f;
            const float halfWidth = std::max( std::fabs( InRegion.AcrossMin ), std::fabs( InRegion.AcrossMax ) ) * scale * InWidth;
            for( int row = 0; row < 3; ++row )
            {
                const float alongTexture = InRegion.AlongMin + ( InRegion.AlongMax - InRegion.AlongMin ) * static_cast<float>( row ) * 0.5f;
                const float x = alongTexture * scale;
                glm::vec3 offset = InPose.Direction * x, normal = InPose.Normal, tangent = InPose.Direction;
                if( kappa != 0.f )
                {
                    // A circular arc bending the tip away from the front face.
                    offset = InPose.Direction * ( std::sin( kappa * x ) / kappa ) - InPose.Normal * ( ( 1.f - std::cos( kappa * x ) ) / kappa );
                    normal = InPose.Normal * std::cos( kappa * x ) + InPose.Direction * std::sin( kappa * x );
                    tangent = InPose.Direction * std::cos( kappa * x ) - InPose.Normal * std::sin( kappa * x );
                }
                for( int column = 0; column < 2; ++column )
                {
                    const float acrossTexture = column == 0 ? InRegion.AcrossMin : InRegion.AcrossMax;
                    const float y = acrossTexture * scale * InWidth;
                    TreeVertex vertex;
                    vertex.Position = ToVector( InPose.Attach + offset + InPose.Side * y );
                    // A slight fold along the midrib.
                    vertex.Normal = ToVector( SafeNormalize( normal + InPose.Side * ( y >= 0.f ? 0.25f : -0.25f ), normal ) );
                    const glm::vec2 uv = InRegion.UV( alongTexture, acrossTexture );
                    vertex.UV = Vector2( uv.x, uv.y );
                    const glm::vec3 dPdu = tangent * scale * InRegion.Axis.x + InPose.Side * scale * InWidth * InRegion.Across.x;
                    const glm::vec3 dPdv = tangent * scale * InRegion.Axis.y + InPose.Side * scale * InWidth * InRegion.Across.y;
                    vertex.Tangent = ToVector( SafeNormalize( dPdu, InPose.Side ) );
                    vertex.Bitangent = ToVector( SafeNormalize( dPdv, tangent ) );
                    const float flutter = std::max( std::clamp( x / InPose.Size, 0.f, 1.f ), halfWidth > 0.f ? std::fabs( y ) / halfWidth * 0.4f : 0.f );
                    vertex.Wind = { { ToByte( flutter ), InPose.Phase, InPose.Sway, 255 } };
                    vertices.push_back( vertex );
                }
            }
            std::vector<uint32_t>& indices = OutPart.Lods[0];
            for( int row = 0; row < 2; ++row )
            {
                const uint32_t a = first + static_cast<uint32_t>( row ) * 2;
                AddTriangle( vertices, indices, a, a + 1, a + 2, InPose.Normal );
                AddTriangle( vertices, indices, a + 1, a + 3, a + 2, InPose.Normal );
            }
            LeafCard card;
            card.Corners[0] = first; card.Corners[1] = first + 1; card.Corners[2] = first + 4; card.Corners[3] = first + 5;
            card.Center = InPose.Attach + InPose.Direction * ( InPose.Size * 0.5f );
            card.Normal = InPose.Normal;
            card.Keep = InRng.Unit();
            OutCards.push_back( card );

            OcclusionSample sample;
            sample.Position = card.Center;
            // Leaf pictures cover roughly half their card.
            sample.Area = InPose.Size * halfWidth * 2.f * 0.5f;
            sample.FirstVertex = first;
            sample.VertexCount = 6;
            sample.Leaf = true;
            InContext.Samples.push_back( sample );
        }


        // A frond: a ribbon along the stem, folded down either side of its spine; levels skip rows.
        void AddFrond( const Stem& InStem, const LeafSettings& InLeaves, float InSizeScale, uint8_t InPhase, float InMaxSway, TreePart& OutPart, BuildContext& InContext )
        {
            std::vector<TreeVertex>& vertices = OutPart.Vertices;
            const uint32_t first = static_cast<uint32_t>( vertices.size() );
            const float fold = Radians( InLeaves.FrondFold );
            const size_t rows = InStem.Nodes.size();
            std::vector<glm::vec3> leftNormals, rightNormals;
            for( size_t i = 0; i < rows; ++i )
            {
                const StemNode& node = InStem.Nodes[i];
                const float t = InStem.Length > 0.f ? node.Distance / InStem.Length : 0.f;
                const float width = InLeaves.FrondWidth * InSizeScale * std::pow( std::max( 0.f, std::sin( kPi * std::min( 1.f, t * 0.95f + 0.05f ) ) ), 0.6f );
                const glm::vec3 up = Perpendicular( node.Direction, kUp );
                const glm::vec3 side = glm::normalize( glm::cross( node.Direction, up ) );
                const float half = width * 0.5f;
                const glm::vec3 positions[3] = {
                    node.Position - side * half * std::cos( fold ) - up * half * std::sin( fold ),
                    node.Position + up * 0.005f,
                    node.Position + side * half * std::cos( fold ) - up * half * std::sin( fold ) };
                const glm::vec3 normals[3] = { glm::normalize( up * std::cos( fold ) - side * std::sin( fold ) ), up,
                    glm::normalize( up * std::cos( fold ) + side * std::sin( fold ) ) };
                leftNormals.push_back( normals[0] );
                rightNormals.push_back( normals[2] );
                for( int column = 0; column < 3; ++column )
                {
                    TreeVertex vertex;
                    vertex.Position = ToVector( positions[column] );
                    vertex.Normal = ToVector( normals[column] );
                    vertex.UV = Vector2( 0.5f * static_cast<float>( column ), 1.f - t );
                    vertex.Tangent = ToVector( side );
                    vertex.Bitangent = ToVector( -node.Direction );
                    const float flutter = std::clamp( t * ( column == 1 ? 0.6f : 1.f ), 0.f, 1.f );
                    vertex.Wind = { { ToByte( flutter ), InPhase, ToByte( node.Sway / InMaxSway ), 255 } };
                    vertices.push_back( vertex );
                }
                OcclusionSample sample;
                sample.Position = node.Position;
                const float segment = i + 1 < rows ? InStem.Nodes[i + 1].Distance - node.Distance : 0.f;
                sample.Area = width * segment * 0.6f;
                sample.FirstVertex = first + static_cast<uint32_t>( i ) * 3;
                sample.VertexCount = 3;
                sample.Leaf = true;
                InContext.Samples.push_back( sample );
            }
            for( int lod = 0; lod <= InContext.LodLevels; ++lod )
            {
                const size_t step = static_cast<size_t>( 1 ) << lod;
                std::vector<size_t> kept;
                for( size_t i = 0; i < rows; i += step ) kept.push_back( i );
                if( kept.back() != rows - 1 ) kept.push_back( rows - 1 );
                std::vector<uint32_t>& indices = OutPart.Lods[static_cast<size_t>( lod )];
                for( size_t k = 0; k + 1 < kept.size(); ++k )
                {
                    const uint32_t a = first + static_cast<uint32_t>( kept[k] ) * 3, b = first + static_cast<uint32_t>( kept[k + 1] ) * 3;
                    AddTriangle( vertices, indices, a, a + 1, b, leftNormals[kept[k]] );
                    AddTriangle( vertices, indices, a + 1, b + 1, b, leftNormals[kept[k]] );
                    AddTriangle( vertices, indices, a + 1, a + 2, b + 1, rightNormals[kept[k]] );
                    AddTriangle( vertices, indices, a + 2, b + 2, b + 1, rightNormals[kept[k]] );
                }
            }
        }
    }


    void BuildLeaves( BuildContext& InContext, TreePart& OutPart, std::vector<LeafCard>& OutCards )
    {
        const LeafSettings& leaves = InContext.Recipe.Leaves;
        OutPart.Name = "Leaves";
        OutPart.Lods.assign( static_cast<size_t>( 1 + InContext.LodLevels ), {} );
        if( leaves.Kind == LeafKind::None ) return;
        const std::vector<RegionFrame> regions = MakeRegions( leaves );
        const float maxSway = InContext.Tree.MaxSway > 0.f ? InContext.Tree.MaxSway : 1.f;
        const int outermost = InContext.Tree.DeepestLevel;
        // Stems one level in carry leaves at the outermost stems' spacing (scaled by ParentCoverage).
        const bool parents = leaves.ParentCoverage > 0.f && outermost >= 2 && leaves.Kind != LeafKind::Frond;
        float outerLength = 0.f;
        int outerCount = 0;
        if( parents )
        {
            for( const Stem& stem : InContext.Tree.Stems )
            {
                if( stem.Level == outermost ) { outerLength += stem.Length; ++outerCount; }
            }
        }
        const float spacing = outerCount > 0 && leaves.PerStem > 0 ? outerLength / static_cast<float>( outerCount ) / static_cast<float>( leaves.PerStem ) : 0.f;

        for( const Stem& stem : InContext.Tree.Stems )
        {
            const bool outer = stem.Level == outermost;
            const bool parent = parents && spacing > 0.f && stem.Level == outermost - 1;
            if( ( !outer && !parent ) || stem.Nodes.size() < 2 ) continue;
            Pcg32 rng( Hash::Of( stem.Id, outer ? 0x1EAFu : 0x9A7Eu ) );
            const uint8_t phase = ToByte( stem.Phase );
            if( leaves.Kind == LeafKind::Frond )
            {
                AddFrond( stem, leaves, 1.f + leaves.SizeVariance * rng.Signed(), phase, maxSway, OutPart, InContext );
                continue;
            }
            if( leaves.PerStem <= 0 ) continue;
            const float lengthRatio = stem.Nominal > 0.f ? stem.Length / stem.Nominal : 1.f;
            const int count = outer ? std::clamp( static_cast<int>( std::lround( static_cast<float>( leaves.PerStem ) * lengthRatio ) ), 1, leaves.PerStem * 4 )
                : std::clamp( static_cast<int>( std::lround( leaves.ParentCoverage * stem.Length * ( 1.f - leaves.Start ) / spacing ) ), 0, leaves.PerStem * 12 );
            float azimuth = rng.Unit() * 2.f * kPi;
            for( int i = 0; i < count && OutCards.size() < kMaxCards; ++i )
            {
                const bool tip = i == count - 1;
                const float t = tip ? 1.f : leaves.Start + ( 1.f - leaves.Start ) * ( static_cast<float>( i ) + 0.5f + 0.3f * rng.Signed() ) / static_cast<float>( count );
                const StemNode node = SampleStem( stem, std::clamp( t, 0.f, 1.f ) );
                azimuth += Radians( 137.5f ) * ( 1.f + 0.1f * rng.Signed() );
                const glm::vec3 radial = std::cos( azimuth ) * node.Normal + std::sin( azimuth ) * node.Binormal;
                const float down = tip ? Radians( 10.f * rng.Unit() ) : Radians( std::clamp( leaves.DownAngle + leaves.DownAngleVariance * rng.Signed(), 0.f, 180.f ) );
                CardPose pose;
                pose.Direction = SafeNormalize( node.Direction * std::cos( down ) + radial * std::sin( down ), node.Direction );
                pose.Attach = node.Position + radial * node.Radius;
                const glm::vec3 facing = SafeNormalize( radial * ( 1.f - leaves.UpBias ) + kUp * leaves.UpBias, kUp );
                pose.Normal = Perpendicular( pose.Direction, facing );
                // A little roll so neighbouring cards never align.
                const float roll = Radians( 20.f ) * rng.Signed();
                pose.Side = glm::normalize( glm::cross( pose.Direction, pose.Normal ) );
                pose.Normal = glm::normalize( pose.Normal * std::cos( roll ) + pose.Side * std::sin( roll ) );
                pose.Side = glm::normalize( glm::cross( pose.Direction, pose.Normal ) );
                pose.Size = leaves.Size * ( 1.f + leaves.SizeVariance * rng.Signed() );
                pose.Curl = Radians( leaves.Curl * ( 1.f + 0.3f * rng.Signed() ) );
                pose.Phase = static_cast<uint8_t>( std::clamp( static_cast<int>( phase ) + static_cast<int>( rng.Signed() * 6.f ), 0, 255 ) );
                pose.Sway = ToByte( node.Sway / maxSway );
                const RegionFrame& region = PickRegion( regions, rng );
                AddCard( pose, region, leaves.Width, rng, OutPart, OutCards, InContext );
                if( leaves.Crossed )
                {
                    CardPose crossed = pose;
                    crossed.Normal = pose.Side;
                    crossed.Side = glm::normalize( glm::cross( crossed.Direction, crossed.Normal ) );
                    AddCard( crossed, region, leaves.Width, rng, OutPart, OutCards, InContext );
                }
            }
        }

        // Crown-spherical normals: shade the canopy as one soft volume rather than a pile of cards.
        if( leaves.Roundness > 0.f && !OutPart.Vertices.empty() )
        {
            glm::vec3 low( 1e9f ), high( -1e9f );
            for( const TreeVertex& vertex : OutPart.Vertices )
            {
                low = glm::min( low, ToGlm( vertex.Position ) );
                high = glm::max( high, ToGlm( vertex.Position ) );
            }
            const glm::vec3 center = ( low + high ) * 0.5f;
            const glm::vec3 radii = glm::max( ( high - low ) * 0.5f, glm::vec3( 0.1f ) );
            for( TreeVertex& vertex : OutPart.Vertices )
            {
                const glm::vec3 normal = ToGlm( vertex.Normal );
                const glm::vec3 offset = ( ToGlm( vertex.Position ) - center ) / ( radii * radii );
                const glm::vec3 outward = SafeNormalize( offset, normal );
                const glm::vec3 blended = SafeNormalize( normal + ( outward - normal ) * leaves.Roundness, normal );
                glm::vec3 tangent = ToGlm( vertex.Tangent );
                tangent = SafeNormalize( tangent - blended * glm::dot( blended, tangent ), Perpendicular( blended, tangent ) );
                const glm::vec3 bitangent = glm::cross( blended, tangent );
                vertex.Normal = ToVector( blended );
                vertex.Tangent = ToVector( tangent );
                vertex.Bitangent = ToVector( glm::dot( bitangent, ToGlm( vertex.Bitangent ) ) >= 0.f ? bitangent : -bitangent );
            }
        }
    }


    void BuildLeafLods( BuildContext& InContext, TreePart& InOutPart, const std::vector<LeafCard>& InCards )
    {
        const float keepPerLevel = InContext.Recipe.Lod.LeafKeep;
        std::vector<TreeVertex>& vertices = InOutPart.Vertices;
        for( int lod = 1; lod <= InContext.LodLevels; ++lod )
        {
            const float keep = std::pow( keepPerLevel, static_cast<float>( lod ) );
            const float grow = 1.f / std::sqrt( keep );
            std::vector<uint32_t>& indices = InOutPart.Lods[static_cast<size_t>( lod )];
            for( const LeafCard& card : InCards )
            {
                if( card.Keep >= keep ) continue;
                const uint32_t first = static_cast<uint32_t>( vertices.size() );
                for( uint32_t corner : card.Corners )
                {
                    TreeVertex vertex = vertices[corner];
                    vertex.Position = ToVector( card.Center + ( ToGlm( vertex.Position ) - card.Center ) * grow );
                    vertices.push_back( vertex );
                }
                AddTriangle( vertices, indices, first, first + 1, first + 2, card.Normal );
                AddTriangle( vertices, indices, first + 1, first + 3, first + 2, card.Normal );
            }
        }
    }
}
