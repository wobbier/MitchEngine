#include "TreeSkeleton.h"
#include "Math/Hash.h"
#include "Math/Noise.h"
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace MitchTree::Detail
{
    namespace
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kMinRadius = 0.003f;
        const glm::vec3 kUp( 0.f, 1.f, 0.f );

        float Radians( float InDegrees )
        {
            return InDegrees * ( kPi / 180.f );
        }


        // Rodrigues' rotation of InVector about the unit InAxis.
        glm::vec3 Rotate( const glm::vec3& InVector, const glm::vec3& InAxis, float InAngle )
        {
            const float c = std::cos( InAngle ), s = std::sin( InAngle );
            return InVector * c + glm::cross( InAxis, InVector ) * s + InAxis * glm::dot( InAxis, InVector ) * ( 1.f - c );
        }


        glm::vec3 SafeNormalize( const glm::vec3& InVector, const glm::vec3& InFallback )
        {
            const float length = glm::length( InVector );
            return length > 1e-6f ? InVector / length : InFallback;
        }


        // A unit vector perpendicular to InDirection, preferring InHint's projection.
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


        glm::vec3 NoiseVector( const glm::vec3& InPosition, uint32_t InSeed )
        {
            return glm::vec3(
                Noise::Value3( InPosition.x, InPosition.y, InPosition.z, InSeed ),
                Noise::Value3( InPosition.x + 31.7f, InPosition.y - 11.3f, InPosition.z + 5.1f, InSeed + 1u ),
                Noise::Value3( InPosition.x - 17.9f, InPosition.y + 23.5f, InPosition.z - 9.7f, InSeed + 2u ) );
        }


        // Turns InDirection toward (positive) or away from (negative) world up by InAngle radians.
        glm::vec3 TurnUp( const glm::vec3& InDirection, float InAngle, const glm::vec3& InFallbackAxis )
        {
            glm::vec3 upward = kUp - InDirection * glm::dot( kUp, InDirection );
            if( glm::length( upward ) < 1e-4f )
            {
                // Pointing straight up or down: bend in the stem's own plane instead.
                upward = InFallbackAxis;
            }
            upward = glm::normalize( upward );
            return glm::normalize( InDirection * std::cos( InAngle ) + upward * std::sin( InAngle ) );
        }


        // How one stem grows; filled per stem from the recipe.
        struct Growth
        {
            glm::vec3 Origin{ 0.f };
            glm::vec3 Direction{ 0.f, 1.f, 0.f };
            float Length = 1.f;
            float Radius = 0.1f;
            float Taper = 0.8f;
            float TaperPower = 1.f;
            float SegmentLength = 0.25f;
            float CurvePerMetre = 0.f;  // radians toward up (+) / down (-)
            glm::vec3 BendDirection{ 0.f };  // (trunk) horizontal bend direction
            float BendPerMetre = 0.f;
            float Gravity = 0.f;
            float Phototropism = 0.f;
            float Gnarl = 0.f;
            float GnarlScale = 1.f;
            float ForkChance = 0.f;
            float ForkAngle = 0.f;
            float Collar = 0.f;
            float Emergence = 0.f;      // where it leaves the parent's surface
            float Flexibility = 0.f;
            float BaseSway = 0.f;
            float SwayReference = 1.f;  // tree height
            // Trunk flare.
            bool Trunk = false;
            float Flare = 0.f;
            float FlareZone = 1.f;      // metres above the ground over which the flare fades
        };

        struct PendingFork
        {
            Growth Grow;
            Stem Template;
        };


        // Distances at which nodes sit: finer at the trunk's flare and around a branch's collar.
        std::vector<float> NodeDistances( const Growth& InGrow, float InSink )
        {
            std::vector<float> distances{ 0.f };
            const float length = InGrow.Length;
            float s = 0.f;
            const float segment = std::max( InGrow.SegmentLength, length / static_cast<float>( MaxNodesPerStem - 4 ) );
            if( !InGrow.Trunk && InGrow.Emergence > 0.f && InGrow.Emergence < length * 0.6f )
            {
                distances.push_back( InGrow.Emergence );
                const float collarEnd = InGrow.Emergence + std::max( InGrow.Radius * 2.f, segment * 0.3f );
                if( collarEnd < length * 0.8f ) distances.push_back( collarEnd );
                s = distances.back();
            }
            while( s < length - 1e-4f )
            {
                float step = segment;
                if( InGrow.Trunk && s < InSink + InGrow.FlareZone * 1.5f ) step = segment * 0.35f;
                s = std::min( length, s + step );
                // Avoid a sliver segment at the tip.
                if( length - s < step * 0.3f ) s = length;
                distances.push_back( s );
            }
            return distances;
        }


        float RadiusAt( const Growth& InGrow, float InDistance, float InHeightAboveGround, float& OutFlare )
        {
            const float t = InGrow.Length > 0.f ? std::clamp( InDistance / InGrow.Length, 0.f, 1.f ) : 0.f;
            float radius = InGrow.Radius * ( 1.f - InGrow.Taper * std::pow( t, InGrow.TaperPower ) );
            OutFlare = 0.f;
            if( InGrow.Trunk && InGrow.Flare > 0.f )
            {
                OutFlare = std::exp( -std::max( InHeightAboveGround, 0.f ) / std::max( InGrow.FlareZone, 0.01f ) * 2.5f );
                radius *= 1.f + InGrow.Flare * OutFlare;
            }
            if( !InGrow.Trunk && InGrow.Collar > 0.f )
            {
                const float beyond = std::max( 0.f, InDistance - InGrow.Emergence );
                radius *= 1.f + InGrow.Collar * std::exp( -beyond / std::max( InGrow.Radius * 1.5f, 1e-3f ) );
            }
            return std::max( radius, kMinRadius );
        }


        // Rotation-minimizing frames by double reflection (Wang et al. 2008).
        void ComputeFrames( Stem& InStem, const glm::vec3& InFirstNormal )
        {
            std::vector<StemNode>& nodes = InStem.Nodes;
            if( nodes.empty() ) return;
            nodes[0].Normal = Perpendicular( nodes[0].Direction, InFirstNormal );
            nodes[0].Binormal = glm::cross( nodes[0].Direction, nodes[0].Normal );
            for( size_t i = 0; i + 1 < nodes.size(); ++i )
            {
                const glm::vec3 v1 = nodes[i + 1].Position - nodes[i].Position;
                const float c1 = glm::dot( v1, v1 );
                glm::vec3 normal = nodes[i].Normal;
                if( c1 > 1e-12f )
                {
                    const glm::vec3 rL = nodes[i].Normal - ( 2.f / c1 ) * glm::dot( v1, nodes[i].Normal ) * v1;
                    const glm::vec3 tL = nodes[i].Direction - ( 2.f / c1 ) * glm::dot( v1, nodes[i].Direction ) * v1;
                    const glm::vec3 v2 = nodes[i + 1].Direction - tL;
                    const float c2 = glm::dot( v2, v2 );
                    normal = c2 > 1e-12f ? rL - ( 2.f / c2 ) * glm::dot( v2, rL ) * v2 : rL;
                }
                nodes[i + 1].Normal = Perpendicular( nodes[i + 1].Direction, normal );
                nodes[i + 1].Binormal = glm::cross( nodes[i + 1].Direction, nodes[i + 1].Normal );
            }
        }


        // Grows one stem's nodes; forks are queued as same-level siblings.
        void Grow( Stem& InStem, const Growth& InGrow, float InSink, Pcg32& InRng, std::vector<PendingFork>& OutForks )
        {
            const std::vector<float> distances = NodeDistances( InGrow, InSink );
            const uint32_t noiseSeed = static_cast<uint32_t>( InStem.Id ) ^ 0x5bd1e995u;
            glm::vec3 position = InGrow.Origin;
            glm::vec3 direction = glm::normalize( InGrow.Direction );
            const glm::vec3 fallbackAxis = Perpendicular( direction, glm::vec3( 0.f, -1.f, 0.f ) );
            float radiusScale = 1.f;
            InStem.Nodes.clear();
            InStem.Nodes.reserve( distances.size() );
            for( size_t i = 0; i < distances.size(); ++i )
            {
                const float s = distances[i];
                StemNode node;
                node.Position = position;
                node.Direction = direction;
                node.Distance = s;
                node.Radius = RadiusAt( InGrow, s, position.y, node.Flare ) * radiusScale;
                const float along = InGrow.Length > 0.f ? s / InGrow.Length : 0.f;
                node.Sway = InGrow.BaseSway + InGrow.Flexibility * std::pow( along, 1.5f ) * std::sqrt( InGrow.Length / InGrow.SwayReference );
                InStem.Nodes.push_back( node );
                if( i + 1 == distances.size() ) break;

                const float ds = distances[i + 1] - s;
                glm::vec3 next = direction;
                if( InGrow.CurvePerMetre != 0.f ) next = TurnUp( next, InGrow.CurvePerMetre * ds, fallbackAxis );
                if( InGrow.Gravity != 0.f ) next = TurnUp( next, InGrow.Gravity * ds, fallbackAxis );
                if( InGrow.BendPerMetre != 0.f )
                {
                    next = glm::normalize( next * std::cos( InGrow.BendPerMetre * ds ) + InGrow.BendDirection * std::sin( InGrow.BendPerMetre * ds ) );
                }
                if( InGrow.Phototropism != 0.f )
                {
                    const glm::vec3 outward( position.x, 0.f, position.z );
                    if( glm::length( outward ) > 1e-3f ) next = glm::normalize( next + glm::normalize( outward ) * InGrow.Phototropism * ds );
                }
                if( InGrow.Gnarl != 0.f )
                {
                    next = glm::normalize( next + NoiseVector( position * InGrow.GnarlScale, noiseSeed ) * InGrow.Gnarl * std::min( 1.f, ds * 2.f ) );
                }

                // Forks split the remaining length in two, away from the base and the tip.
                if( InGrow.ForkChance > 0.f && InStem.ForkDepth < 2 && along > 0.2f && along < 0.75f && InRng.Chance( InGrow.ForkChance ) )
                {
                    const glm::vec3 axis = Rotate( Perpendicular( next, fallbackAxis ), next, InRng.Unit() * 2.f * kPi );
                    const float half = Radians( InGrow.ForkAngle ) * 0.5f;
                    PendingFork fork;
                    fork.Template = InStem;
                    fork.Template.Nodes.clear();
                    fork.Template.Id = Hash::Of( InStem.Id, i, 0xF0F0u );
                    fork.Template.ForkDepth = InStem.ForkDepth + 1;
                    fork.Template.CollapseTip = InStem.CollapseTip;
                    fork.Grow = InGrow;
                    fork.Grow.Origin = position;
                    fork.Grow.Direction = Rotate( next, axis, -half );
                    fork.Grow.Length = ( InGrow.Length - s ) * InRng.Range( 0.8f, 1.f );
                    fork.Grow.Radius = node.Radius * 0.75f;
                    fork.Grow.Emergence = 0.f;
                    fork.Grow.Collar = 0.f;
                    fork.Grow.BaseSway = node.Sway;
                    fork.Grow.ForkChance = InGrow.ForkChance * 0.5f;
                    fork.Template.Length = fork.Grow.Length;
                    fork.Template.Nominal = InStem.Nominal * ( fork.Grow.Length / std::max( InGrow.Length, 1e-3f ) );
                    OutForks.push_back( std::move( fork ) );
                    next = Rotate( next, axis, half );
                    radiusScale *= 0.75f;
                }

                // Midpoint step keeps tight curves smooth.
                position += SafeNormalize( direction + next, next ) * ds;
                direction = next;
            }
            InStem.Length = InGrow.Length;
            if( InStem.CollapseTip && !InStem.Nodes.empty() ) InStem.Nodes.back().Radius = 0.f;
        }


        // Child positions along a parent (0..1), with sibling groups (pairs or whorls) sharing a position.
        struct ChildSlot { float T; float Azimuth; };

        std::vector<ChildSlot> ChildSlots( const BranchLevel& InLevel, int InCount, float InStart, float InEnd, Pcg32& InRng )
        {
            std::vector<ChildSlot> slots;
            if( InCount <= 0 || InEnd <= InStart ) return slots;
            const int group = InLevel.Layout == Arrangement::Opposite ? 2 : InLevel.Layout == Arrangement::Whorled ? std::max( 2, InLevel.WhorlSize ) : 1;
            const int positions = std::max( 1, ( InCount + group - 1 ) / group );
            float azimuth = InRng.Unit() * 2.f * kPi;
            for( int p = 0; p < positions; ++p )
            {
                const float t = InStart + ( InEnd - InStart ) * ( static_cast<float>( p ) + 0.5f + 0.35f * InRng.Signed() ) / static_cast<float>( positions );
                for( int g = 0; g < group && static_cast<int>( slots.size() ) < InCount; ++g )
                {
                    slots.push_back( { std::clamp( t, 0.f, 1.f ), azimuth + 2.f * kPi * static_cast<float>( g ) / static_cast<float>( group ) } );
                }
                switch( InLevel.Layout )
                {
                case Arrangement::Spiral: azimuth += Radians( InLevel.Rotation ) * ( 1.f + 0.06f * InRng.Signed() ); break;
                case Arrangement::Alternate: azimuth += kPi * ( 1.f + 0.06f * InRng.Signed() ); break;
                case Arrangement::Opposite: azimuth += 0.5f * kPi * ( 1.f + 0.06f * InRng.Signed() ); break;
                case Arrangement::Whorled: azimuth += kPi / static_cast<float>( group ) + 0.3f * InRng.Signed(); break;
                }
            }
            return slots;
        }
    }


    StemNode SampleStem( const Stem& InStem, float InT )
    {
        const std::vector<StemNode>& nodes = InStem.Nodes;
        if( nodes.empty() ) return StemNode();
        const float s = std::clamp( InT, 0.f, 1.f ) * InStem.Length;
        size_t i = 0;
        while( i + 2 < nodes.size() && nodes[i + 1].Distance < s ) ++i;
        if( nodes.size() == 1 ) return nodes[0];
        const StemNode& a = nodes[i];
        const StemNode& b = nodes[i + 1];
        const float span = b.Distance - a.Distance;
        const float f = span > 1e-6f ? std::clamp( ( s - a.Distance ) / span, 0.f, 1.f ) : 0.f;
        StemNode out;
        out.Position = a.Position + ( b.Position - a.Position ) * f;
        out.Direction = SafeNormalize( a.Direction + ( b.Direction - a.Direction ) * f, a.Direction );
        out.Normal = Perpendicular( out.Direction, a.Normal + ( b.Normal - a.Normal ) * f );
        out.Binormal = glm::cross( out.Direction, out.Normal );
        // A collapsed tip would read as zero radius just before the end: use the last real ring.
        const float radiusB = ( i + 2 == nodes.size() && b.Radius == 0.f ) ? a.Radius * ( 1.f - f ) + kMinRadius * f : b.Radius;
        out.Radius = a.Radius + ( radiusB - a.Radius ) * f;
        out.Distance = s;
        out.Sway = a.Sway + ( b.Sway - a.Sway ) * f;
        out.Flare = a.Flare + ( b.Flare - a.Flare ) * f;
        return out;
    }


    Skeleton BuildSkeleton( const TreeRecipe& InRecipe )
    {
        Skeleton skeleton;
        const TrunkSettings& trunk = InRecipe.Trunk;
        const uint64_t treeSeed = Hash::Of( 0x4D69746368547265ULL, InRecipe.Seed );
        Pcg32 trunkRng( Hash::Of( treeSeed, 0 ) );
        const float height = trunk.Height * ( 1.f + trunk.HeightVariance * trunkRng.Signed() );
        skeleton.Height = height;
        std::vector<PendingFork> forks;

        // Trunk, from below the ground.
        {
            Stem stem;
            stem.Id = Hash::Of( treeSeed, 1 );
            stem.Level = 0;
            stem.Nominal = height;
            const float leanAzimuth = trunkRng.Unit() * 2.f * kPi;
            const glm::vec3 leanAxis( std::cos( leanAzimuth ), 0.f, std::sin( leanAzimuth ) );
            const float lean = Radians( trunk.Lean * trunkRng.Range( 0.5f, 1.f ) );
            Growth grow;
            grow.Trunk = true;
            grow.Origin = glm::vec3( 0.f, -trunk.SinkDepth, 0.f );
            grow.Direction = glm::normalize( kUp * std::cos( lean ) + leanAxis * std::sin( lean ) );
            const float aboveSplit = trunk.Splits > 0 ? height * trunk.SplitHeight : height;
            grow.Length = aboveSplit + trunk.SinkDepth;
            grow.Radius = trunk.Radius;
            grow.Taper = trunk.Splits > 0 ? trunk.Taper * trunk.SplitHeight : trunk.Taper;
            grow.TaperPower = trunk.TaperPower;
            grow.SegmentLength = trunk.SegmentLength;
            const float bendAzimuth = trunkRng.Unit() * 2.f * kPi;
            grow.BendDirection = glm::vec3( std::cos( bendAzimuth ), 0.f, std::sin( bendAzimuth ) );
            grow.BendPerMetre = Radians( trunk.Bend ) / std::max( height, 0.1f );
            grow.Gnarl = trunk.Gnarl;
            grow.GnarlScale = trunk.GnarlScale;
            grow.Flare = trunk.Flare;
            grow.FlareZone = trunk.FlareHeight * height;
            grow.SwayReference = std::max( height, 0.1f );
            stem.CollapseTip = trunk.Splits == 0;
            Grow( stem, grow, trunk.SinkDepth, trunkRng, forks );
            ComputeFrames( stem, glm::vec3( 1.f, 0.f, 0.f ) );
            stem.TrunkOffset = -trunk.SinkDepth;
            skeleton.Stems.push_back( std::move( stem ) );

            // Leaders above the split share the trunk's top.
            if( trunk.Splits > 0 )
            {
                const Stem& base = skeleton.Stems[0];
                const StemNode top = base.Nodes.back();
                const float leaderAzimuth = trunkRng.Unit() * 2.f * kPi;
                for( int j = 0; j < trunk.Splits; ++j )
                {
                    Stem leader;
                    leader.Id = Hash::Of( treeSeed, 2, j );
                    leader.Level = 0;
                    leader.Parent = 0;
                    leader.ParentT = 1.f;
                    leader.Nominal = height;
                    leader.TrunkOffset = aboveSplit;
                    Pcg32 rng( leader.Id );
                    const float azimuth = leaderAzimuth + 2.f * kPi * static_cast<float>( j ) / static_cast<float>( trunk.Splits ) + 0.3f * rng.Signed();
                    const glm::vec3 radial = std::cos( azimuth ) * top.Normal + std::sin( azimuth ) * top.Binormal;
                    const float angle = Radians( trunk.Splits == 1 ? trunk.SplitAngle * 0.3f : trunk.SplitAngle ) * rng.Range( 0.8f, 1.2f );
                    Growth lead;
                    lead.Trunk = true;
                    lead.Origin = top.Position - top.Direction * top.Radius * 0.5f;
                    lead.Direction = glm::normalize( top.Direction * std::cos( angle ) + radial * std::sin( angle ) );
                    lead.Length = ( height - aboveSplit ) * rng.Range( 0.85f, 1.f );
                    lead.Radius = top.Radius / std::sqrt( static_cast<float>( trunk.Splits ) ) * 1.1f;
                    lead.Taper = trunk.Taper;
                    lead.TaperPower = trunk.TaperPower;
                    lead.SegmentLength = trunk.SegmentLength;
                    lead.Gnarl = trunk.Gnarl * 1.5f;
                    lead.GnarlScale = trunk.GnarlScale;
                    lead.BendDirection = Perpendicular( lead.Direction, radial );
                    lead.BendPerMetre = -Radians( trunk.SplitAngle ) * 0.5f / std::max( lead.Length, 0.1f );
                    lead.SwayReference = std::max( height, 0.1f );
                    Grow( leader, lead, 0.f, rng, forks );
                    ComputeFrames( leader, top.Normal );
                    skeleton.Stems.push_back( std::move( leader ) );
                }
            }
        }

        // Branch levels, breadth first so each level's stems (and their forks) exist before the next.
        size_t levelBegin = 0, levelEnd = skeleton.Stems.size();
        for( int level = 1; level <= static_cast<int>( InRecipe.Branches.size() ); ++level )
        {
            const BranchLevel& settings = InRecipe.Branches[level - 1];
            if( !settings.Enabled || settings.Count <= 0 ) break;
            for( size_t parentIndex = levelBegin; parentIndex < levelEnd; ++parentIndex )
            {
                if( skeleton.Stems.size() >= MaxStems ) break;
                // Copies: pushing children reallocates the stem list.
                const uint64_t parentId = skeleton.Stems[parentIndex].Id;
                const float parentLength = skeleton.Stems[parentIndex].Length;
                const float parentNominal = std::max( skeleton.Stems[parentIndex].Nominal, 1e-3f );
                const float parentPhase = skeleton.Stems[parentIndex].Phase;
                const float parentOffset = skeleton.Stems[parentIndex].TrunkOffset;
                Pcg32 rng( Hash::Of( parentId, level, 0xC4C4u ) );

                float tStart = settings.Start, tEnd = settings.End;
                float expected = static_cast<float>( settings.Count );
                if( level == 1 )
                {
                    // On the trunk, Start/End are fractions of the tree height from the ground.
                    tStart = std::clamp( ( settings.Start * height - parentOffset ) / std::max( parentLength, 1e-3f ), 0.f, 1.f );
                    tEnd = std::clamp( ( settings.End * height - parentOffset ) / std::max( parentLength, 1e-3f ), 0.f, 1.f );
                    expected *= ( tEnd - tStart ) * parentLength / std::max( ( settings.End - settings.Start ) * height, 1e-3f );
                }
                else
                {
                    expected *= parentLength / parentNominal;
                }
                const int count = std::max( 0, static_cast<int>( std::lround( expected * ( 1.f + settings.CountVariance * rng.Signed() ) ) ) );
                const std::vector<ChildSlot> slots = ChildSlots( settings, count, tStart, tEnd, rng );
                for( size_t c = 0; c < slots.size() && skeleton.Stems.size() < MaxStems; ++c )
                {
                    const Stem& parent = skeleton.Stems[parentIndex];
                    const float t = slots[c].T;
                    const StemNode at = SampleStem( parent, t );
                    Stem child;
                    child.Id = Hash::Of( parentId, c, level );
                    child.Level = level;
                    child.Parent = static_cast<int>( parentIndex );
                    child.ParentT = t;
                    Pcg32 childRng( child.Id );

                    const glm::vec3 radial = std::cos( slots[c].Azimuth ) * at.Normal + std::sin( slots[c].Azimuth ) * at.Binormal;
                    const float down = std::clamp( Radians( settings.DownAngle + settings.DownAngleTip * t + settings.DownAngleVariance * childRng.Signed() ), 0.f, kPi );
                    const glm::vec3 direction = SafeNormalize( at.Direction * std::cos( down ) + radial * std::sin( down ), radial );

                    float length = 0.f;
                    if( level == 1 )
                    {
                        const float fromGround = parentOffset + t * parentLength;
                        const float ratio = ( 1.f - fromGround / height ) / std::max( 1.f - settings.Start, 1e-3f );
                        child.Nominal = settings.Length * height;
                        length = child.Nominal * ShapeRatio( InRecipe.Crown, ratio );
                    }
                    else
                    {
                        child.Nominal = settings.Length * parentNominal;
                        length = settings.Length * parentLength * ( 1.f + ( settings.LengthAtTip - 1.f ) * t );
                    }
                    length *= 1.f + settings.LengthVariance * childRng.Signed();
                    if( length < 0.04f ) continue;

                    Growth grow;
                    grow.Origin = at.Position;
                    grow.Direction = direction;
                    grow.Length = length;
                    grow.Radius = std::clamp( settings.Radius * at.Radius * std::sqrt( length / std::max( child.Nominal, 1e-3f ) ), kMinRadius, std::max( kMinRadius, at.Radius * 0.92f ) );
                    grow.Taper = settings.Taper;
                    grow.TaperPower = 1.f;
                    grow.SegmentLength = settings.SegmentLength;
                    grow.CurvePerMetre = Radians( settings.Curve + settings.CurveVariance * childRng.Signed() ) / length;
                    grow.Gravity = settings.Gravity;
                    grow.Phototropism = settings.Phototropism;
                    grow.Gnarl = settings.Gnarl;
                    grow.GnarlScale = 2.f / std::max( length, 0.2f ) + 0.5f;
                    grow.ForkChance = settings.ForkChance;
                    grow.ForkAngle = settings.ForkAngle;
                    grow.Collar = settings.Collar;
                    grow.Emergence = at.Radius / std::max( std::sin( down ), 0.25f );
                    grow.Flexibility = settings.Flexibility;
                    grow.BaseSway = at.Sway;
                    grow.SwayReference = std::max( height, 0.1f );
                    child.Phase = level == 1 ? Hash::ToUnit( Hash::Of( child.Id, 0x9A5Eu ) ) : std::fmod( parentPhase + 1.f + 0.08f * childRng.Signed(), 1.f );
                    Grow( child, grow, 0.f, childRng, forks );
                    // The seam faces the ground.
                    ComputeFrames( child, glm::vec3( 0.f, -1.f, 0.f ) );
                    skeleton.Stems.push_back( std::move( child ) );

                    while( !forks.empty() && skeleton.Stems.size() < MaxStems )
                    {
                        PendingFork fork = std::move( forks.back() );
                        forks.pop_back();
                        Pcg32 forkRng( fork.Template.Id );
                        Grow( fork.Template, fork.Grow, 0.f, forkRng, forks );
                        ComputeFrames( fork.Template, glm::vec3( 0.f, -1.f, 0.f ) );
                        skeleton.Stems.push_back( std::move( fork.Template ) );
                    }
                    forks.clear();
                }
            }
            levelBegin = levelEnd;
            levelEnd = skeleton.Stems.size();
            if( levelEnd == levelBegin ) break;
            skeleton.DeepestLevel = level;
        }

        for( const Stem& stem : skeleton.Stems )
        {
            for( const StemNode& node : stem.Nodes ) skeleton.MaxSway = std::max( skeleton.MaxSway, node.Sway );
        }
        return skeleton;
    }
}
