#include "TreeRecipe.h"
#include <algorithm>
#include <cmath>

ME_REFLECT_ENUM( MitchTree::CrownShape, {
    { "Conical", MitchTree::CrownShape::Conical }, { "Spherical", MitchTree::CrownShape::Spherical },
    { "Hemispherical", MitchTree::CrownShape::Hemispherical }, { "Cylindrical", MitchTree::CrownShape::Cylindrical },
    { "TaperedCylindrical", MitchTree::CrownShape::TaperedCylindrical }, { "Flame", MitchTree::CrownShape::Flame },
    { "InverseConical", MitchTree::CrownShape::InverseConical }, { "TendFlame", MitchTree::CrownShape::TendFlame } } )
ME_REFLECT_ENUM( MitchTree::Arrangement, {
    { "Spiral", MitchTree::Arrangement::Spiral }, { "Alternate", MitchTree::Arrangement::Alternate },
    { "Opposite", MitchTree::Arrangement::Opposite }, { "Whorled", MitchTree::Arrangement::Whorled } } )
ME_REFLECT_ENUM( MitchTree::LeafKind, {
    { "None", MitchTree::LeafKind::None }, { "Broadleaf", MitchTree::LeafKind::Broadleaf },
    { "Needles", MitchTree::LeafKind::Needles }, { "Frond", MitchTree::LeafKind::Frond } } )

ME_REFLECT_ENUM( MitchTree::BarkStyle, {
    { "Furrowed", MitchTree::BarkStyle::Furrowed }, { "Plated", MitchTree::BarkStyle::Plated }, { "Smooth", MitchTree::BarkStyle::Smooth },
    { "Birch", MitchTree::BarkStyle::Birch }, { "Ringed", MitchTree::BarkStyle::Ringed }, { "Weathered", MitchTree::BarkStyle::Weathered } } )
ME_REFLECT_ENUM( MitchTree::LeafShape, {
    { "Oval", MitchTree::LeafShape::Oval }, { "Lobed", MitchTree::LeafShape::Lobed }, { "Palmate", MitchTree::LeafShape::Palmate },
    { "Serrated", MitchTree::LeafShape::Serrated }, { "Round", MitchTree::LeafShape::Round }, { "Needles", MitchTree::LeafShape::Needles },
    { "Frond", MitchTree::LeafShape::Frond } } )

ME_REFLECT_BEGIN( MitchTree::TrunkSettings )
    ME_FIELD( Height ).Range( 0.5f, 80.f ).Tooltip( "Metres above the ground." );
    ME_FIELD( HeightVariance ).Range( 0.f, 0.5f ).Tooltip( "Random height change per seed, as a fraction." );
    ME_FIELD( Radius ).Range( 0.01f, 3.f ).Tooltip( "Trunk radius in metres above the root flare." );
    ME_FIELD( Taper ).Range( 0.f, 1.f ).Tooltip( "Fraction of the radius lost by the top." );
    ME_FIELD( TaperPower ).Range( 0.3f, 4.f ).Tooltip( "Above 1 keeps the trunk thick for longer before it narrows." );
    ME_FIELD( Flare ).Range( 0.f, 3.f ).Category( "Roots" ).Tooltip( "Extra radius at the ground, as a fraction of the trunk radius." );
    ME_FIELD( FlareHeight ).Range( 0.f, 0.3f ).Category( "Roots" ).Tooltip( "Fraction of the height over which the flare fades." );
    ME_FIELD( FlareLobes ).Range( 0.f, 12.f ).Category( "Roots" ).Tooltip( "Root buttresses around the base." );
    ME_FIELD( LobeDepth ).Range( 0.f, 1.f ).Category( "Roots" );
    ME_FIELD( Lean ).Range( 0.f, 45.f ).Category( "Shape" ).Tooltip( "Degrees the trunk leans, in a seeded direction." );
    ME_FIELD( Bend ).Range( 0.f, 60.f ).Category( "Shape" ).Tooltip( "Degrees of curvature over the height." );
    ME_FIELD( Gnarl ).Range( 0.f, 1.f ).Category( "Shape" ).Tooltip( "Noise wiggle of the trunk's direction." );
    ME_FIELD( GnarlScale ).Range( 0.05f, 4.f ).Category( "Shape" ).Tooltip( "Wiggles per metre." );
    ME_FIELD( Splits ).Range( 0.f, 4.f ).Category( "Shape" ).Tooltip( "Leaders the trunk forks into (0 = a single trunk)." );
    ME_FIELD( SplitHeight ).Range( 0.1f, 0.95f ).Category( "Shape" );
    ME_FIELD( SplitAngle ).Range( 0.f, 80.f ).Category( "Shape" );
    ME_FIELD( SinkDepth ).Range( 0.f, 3.f ).Category( "Detail" ).Tooltip( "Metres buried below the ground so slopes never show the base." );
    ME_FIELD( SegmentLength ).Range( 0.05f, 2.f ).Category( "Detail" ).Tooltip( "Metres between rings." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::BranchLevel )
    ME_FIELD( Enabled );
    ME_FIELD( Count ).Range( 0.f, 200.f ).Tooltip( "Children on a full-length parent." );
    ME_FIELD( CountVariance ).Range( 0.f, 1.f );
    ME_FIELD( Start ).Range( 0.f, 1.f ).Tooltip( "Where children begin along the parent (0 base, 1 tip)." );
    ME_FIELD( End ).Range( 0.f, 1.f );
    ME_FIELD( Layout ).Category( "Arrangement" );
    ME_FIELD( Rotation ).Range( 0.f, 360.f ).Category( "Arrangement" ).Tooltip( "Degrees between consecutive siblings (137.5 is the golden angle)." );
    ME_FIELD( WhorlSize ).Range( 2.f, 12.f ).Category( "Arrangement" );
    ME_FIELD( DownAngle ).Range( 0.f, 180.f ).Category( "Angle" ).Tooltip( "Degrees away from the parent's axis." );
    ME_FIELD( DownAngleVariance ).Range( 0.f, 90.f ).Category( "Angle" );
    ME_FIELD( DownAngleTip ).Range( -120.f, 120.f ).Category( "Angle" ).Tooltip( "Change of the down angle from the parent's base to its tip." );
    ME_FIELD( Length ).Range( 0.01f, 2.f ).Category( "Size" ).Tooltip( "Level 1: fraction of the trunk height. Deeper levels: fraction of the parent." );
    ME_FIELD( LengthVariance ).Range( 0.f, 1.f ).Category( "Size" );
    ME_FIELD( LengthAtTip ).Range( 0.f, 2.f ).Category( "Size" ).Tooltip( "(Levels 2+) length multiplier toward the parent's tip." );
    ME_FIELD( Radius ).Range( 0.05f, 1.f ).Category( "Size" ).Tooltip( "Fraction of the parent's radius where it attaches." );
    ME_FIELD( Taper ).Range( 0.f, 1.f ).Category( "Size" );
    ME_FIELD( Curve ).Range( -180.f, 180.f ).Category( "Shape" ).Tooltip( "Degrees of bend over the length: positive turns up, negative down." );
    ME_FIELD( CurveVariance ).Range( 0.f, 90.f ).Category( "Shape" );
    ME_FIELD( Gravity ).Range( -2.f, 2.f ).Category( "Shape" ).Tooltip( "Per metre: positive turns up, negative droops." );
    ME_FIELD( Phototropism ).Range( 0.f, 2.f ).Category( "Shape" ).Tooltip( "Per metre: turns outward, toward the light." );
    ME_FIELD( Gnarl ).Range( 0.f, 2.f ).Category( "Shape" );
    ME_FIELD( ForkChance ).Range( 0.f, 0.5f ).Category( "Shape" ).Tooltip( "Chance per segment that the branch forks in two." );
    ME_FIELD( ForkAngle ).Range( 0.f, 90.f ).Category( "Shape" );
    ME_FIELD( SegmentLength ).Range( 0.03f, 2.f ).Category( "Detail" );
    ME_FIELD( Flexibility ).Range( 0.f, 4.f ).Category( "Detail" ).Tooltip( "How much wind sways this level." );
    ME_FIELD( Collar ).Range( 0.f, 1.f ).Category( "Detail" ).Tooltip( "Extra radius where the branch leaves its parent." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::LeafRegion )
    ME_FIELD( Rect ).Tooltip( "u0, v0, u1, v1 of the leaf picture in the texture." );
    ME_FIELD( Pivot ).Tooltip( "Texture position where the stalk attaches." );
    ME_FIELD( Angle ).Range( -180.f, 180.f ).Tooltip( "Direction of the tip in the image (0 = up)." );
    ME_FIELD( Weight ).Range( 0.f, 10.f );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::LeafSettings )
    ME_FIELD( Kind );
    ME_FIELD( Size ).Range( 0.01f, 5.f ).Tooltip( "Metres from stalk to tip. A card may picture a whole cluster." );
    ME_FIELD( SizeVariance ).Range( 0.f, 1.f );
    ME_FIELD( Width ).Range( 0.2f, 3.f ).Tooltip( "Width scale over the texture's own aspect." );
    ME_FIELD( PerStem ).Range( 0.f, 100.f ).Tooltip( "Leaves on each outermost stem." );
    ME_FIELD( ParentCoverage ).Range( 0.f, 2.f ).Tooltip( "Leaves also along the stems one level in (conifer limbs, the inner crown), at this density relative to the outermost stems." );
    ME_FIELD( Start ).Range( 0.f, 1.f );
    ME_FIELD( DownAngle ).Range( 0.f, 180.f ).Category( "Orientation" );
    ME_FIELD( DownAngleVariance ).Range( 0.f, 90.f ).Category( "Orientation" );
    ME_FIELD( UpBias ).Range( 0.f, 1.f ).Category( "Orientation" ).Tooltip( "Turns leaf faces toward the sky." );
    ME_FIELD( Curl ).Range( -90.f, 90.f ).Category( "Orientation" );
    ME_FIELD( Roundness ).Range( 0.f, 1.f ).Category( "Lighting" ).Tooltip( "Bends leaf normals toward the crown's outline for soft, volumetric lighting." );
    ME_FIELD( Crossed ).Category( "Orientation" ).Tooltip( "Adds a second card at 90 degrees (needle sprays)." );
    ME_FIELD( FrondWidth ).Range( 0.05f, 5.f ).Category( "Frond" );
    ME_FIELD( FrondFold ).Range( 0.f, 80.f ).Category( "Frond" );
    ME_FIELD( Regions ).Category( "Texture" ).Tooltip( "Leaf pictures in the leaf texture. Empty uses the whole texture." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::BarkMaterial )
    ME_FIELD( BaseColorMap ).Asset( "Texture" );
    ME_FIELD( NormalMap ).Asset( "Texture" );
    ME_FIELD( OrmMap ).Asset( "Texture" ).Tooltip( "Packed occlusion, roughness and metallic." );
    ME_FIELD( Tint ).Color();
    ME_FIELD( Roughness ).Range( 0.f, 1.f );
    ME_FIELD( NormalStrength ).Range( 0.f, 4.f );
    ME_FIELD( TileWidth ).Range( 0.05f, 10.f ).Tooltip( "Metres around the trunk per texture repeat." );
    ME_FIELD( TileHeight ).Range( 0.05f, 20.f ).Tooltip( "Metres along a stem per texture repeat." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::LeafMaterial )
    ME_FIELD( BaseColorMap ).Asset( "Texture" );
    ME_FIELD( NormalMap ).Asset( "Texture" );
    ME_FIELD( Tint ).Color();
    ME_FIELD( Roughness ).Range( 0.f, 1.f );
    ME_FIELD( Translucency ).Range( 0.f, 2.f ).Tooltip( "Light through the leaves when they are back-lit." );
    ME_FIELD( AlphaCutoff ).Range( 0.f, 1.f );
    ME_FIELD( Variation ).Range( 0.f, 1.f ).Tooltip( "Per-branch tint variation." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::BarkTexture )
    ME_FIELD( Generate ).Tooltip( "Generate the bark maps from these settings (off: use the Bark material's maps)." );
    ME_FIELD( Style );
    ME_FIELD( Color ).Color();
    ME_FIELD( CrevasseColor ).Color();
    ME_FIELD( Scale ).Range( 1.f, 24.f ).Tooltip( "Ridges or plates around one texture repeat." );
    ME_FIELD( Stretch ).Range( 0.5f, 12.f ).Tooltip( "Vertical elongation of the pattern." );
    ME_FIELD( Depth ).Range( 0.f, 4.f ).Tooltip( "Relief strength (normal map and occlusion)." );
    ME_FIELD( Roughness ).Range( 0.f, 1.f );
    ME_FIELD( Moss ).Range( 0.f, 1.f );
    ME_FIELD( MossColor ).Color();
    ME_FIELD( Resolution ).Range( 64.f, 2048.f );
    ME_FIELD( Seed );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::LeafTexture )
    ME_FIELD( Generate ).Tooltip( "Generate the leaf atlas from these settings (off: use the Foliage material's maps and the leaf regions)." );
    ME_FIELD( Shape );
    ME_FIELD( Leaves ).Range( 1.f, 24.f ).Tooltip( "Leaves on each twig picture (1 = a single leaf; more than six make a spray with side twigs). Needles: shoots in each spray." );
    ME_FIELD( Aspect ).Range( 0.05f, 1.5f ).Tooltip( "Leaf width over length." );
    ME_FIELD( Lobes ).Range( 2.f, 15.f );
    ME_FIELD( LobeDepth ).Range( 0.f, 0.9f );
    ME_FIELD( Serration ).Range( 0.f, 0.5f );
    ME_FIELD( NeedleLength ).Range( 0.03f, 0.5f ).Tooltip( "(Needles) needle length as a fraction of a picture." );
    ME_FIELD( Color ).Color();
    ME_FIELD( TipColor ).Color();
    ME_FIELD( Variation ).Range( 0.f, 1.f );
    ME_FIELD( Veins ).Range( 0.f, 1.f );
    ME_FIELD( TwigColor ).Color();
    ME_FIELD( Columns ).Range( 1.f, 4.f );
    ME_FIELD( Rows ).Range( 1.f, 4.f );
    ME_FIELD( Resolution ).Range( 128.f, 2048.f );
    ME_FIELD( Seed );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::WindSettings )
    ME_FIELD( Trunk ).Range( 0.f, 4.f ).Tooltip( "Sway of the whole tree." );
    ME_FIELD( Branches ).Range( 0.f, 4.f );
    ME_FIELD( Leaves ).Range( 0.f, 4.f ).Tooltip( "Leaf flutter." );
    ME_FIELD( Frequency ).Range( 0.1f, 4.f );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::LodSettings )
    ME_FIELD( Levels ).Range( 0.f, 4.f ).Tooltip( "Simplified levels after full detail." );
    ME_FIELD( Transition ).Range( 0.02f, 2.f ).Tooltip( "Screen height below which level 1 draws; each further level halves it." );
    ME_FIELD( LeafKeep ).Range( 0.1f, 1.f ).Tooltip( "Fraction of leaf cards each level keeps (scaled up to cover the same area)." );
ME_REFLECT_END()

ME_REFLECT_BEGIN( MitchTree::TreeRecipe )
    ME_FIELD( Name );
    ME_FIELD( Seed );
    ME_FIELD( Trunk );
    ME_FIELD( Crown ).Tooltip( "Envelope of the first branch level's lengths up the crown." );
    ME_FIELD( Branches );
    ME_FIELD( Leaves );
    ME_FIELD( Occlusion ).Range( 0.f, 1.f ).Tooltip( "Strength of the baked crown self-shadowing." );
    ME_FIELD( Bark );
    ME_FIELD( Foliage );
    ME_FIELD( BarkTextures ).Tooltip( "Generated bark maps." );
    ME_FIELD( LeafTextures ).Tooltip( "Generated leaf atlas." );
    ME_FIELD( Wind );
    ME_FIELD( Lod );
ME_REFLECT_END()

namespace MitchTree
{
    namespace
    {
        float Clamp( float InValue, float InMin, float InMax )
        {
            return std::isfinite( InValue ) ? std::clamp( InValue, InMin, InMax ) : InMin;
        }


        int Clamp( int InValue, int InMin, int InMax )
        {
            return std::clamp( InValue, InMin, InMax );
        }


        // Clamps every float/int field to its reflected range.
        void ClampFields( const Reflection::TypeInfo& InType, void* InInstance )
        {
            InType.ForEachField( InInstance, [&]( const Reflection::FieldInfo& field, void* owner ) {
                void* value = field.Get( owner );
                const Reflection::PropertyType type = field.GetType();
                if( type == Reflection::PropertyType::Float && field.HasRange )
                {
                    float& number = *static_cast<float*>( value );
                    number = Clamp( number, field.Min, field.Max );
                }
                else if( type == Reflection::PropertyType::Int && field.HasRange && field.Ops->IntBytes == sizeof( int ) )
                {
                    int& number = *static_cast<int*>( value );
                    number = Clamp( number, static_cast<int>( field.Min ), static_cast<int>( field.Max ) );
                }
            } );
        }
    }


    json TreeRecipe::ToJson() const
    {
        json out;
        Reflection::ToJson( StaticType(), this, out );
        out["Format"] = "MitchTree";
        out["Version"] = FormatVersion;
        return out;
    }


    bool TreeRecipe::FromJson( const json& InJson, std::string& OutError )
    {
        if( !InJson.is_object() || InJson.value( "Format", std::string() ) != "MitchTree" )
        {
            OutError = "Not a MitchTree recipe (missing \"Format\": \"MitchTree\").";
            return false;
        }
        const int version = InJson.value( "Version", 0 );
        if( version < 1 || version > FormatVersion )
        {
            OutError = "Unsupported MitchTree recipe version " + std::to_string( version ) + ".";
            return false;
        }
        TreeRecipe loaded;
        loaded.Branches.clear();
        Reflection::FromJson( StaticType(), &loaded, InJson );
        loaded.Sanitize();
        *this = std::move( loaded );
        return true;
    }


    void TreeRecipe::Sanitize()
    {
        ClampFields( TrunkSettings::StaticType(), &Trunk );
        if( Branches.size() > static_cast<size_t>( MaxBranchLevels ) )
        {
            Branches.resize( MaxBranchLevels );
        }
        for( BranchLevel& level : Branches )
        {
            ClampFields( BranchLevel::StaticType(), &level );
            if( level.End < level.Start )
            {
                std::swap( level.Start, level.End );
            }
        }
        ClampFields( LeafSettings::StaticType(), &Leaves );
        for( LeafRegion& region : Leaves.Regions )
        {
            ClampFields( LeafRegion::StaticType(), &region );
        }
        ClampFields( BarkMaterial::StaticType(), &Bark );
        ClampFields( LeafMaterial::StaticType(), &Foliage );
        ClampFields( BarkTexture::StaticType(), &BarkTextures );
        ClampFields( LeafTexture::StaticType(), &LeafTextures );
        ClampFields( WindSettings::StaticType(), &Wind );
        ClampFields( LodSettings::StaticType(), &Lod );
        Occlusion = Clamp( Occlusion, 0.f, 1.f );
    }


    float ShapeRatio( CrownShape InShape, float InRatio )
    {
        // InRatio is 1 at the bottom of the crown and 0 at the top (Weber & Penn, 1995).
        const float r = Clamp( InRatio, 0.f, 1.f );
        constexpr float pi = 3.14159265f;
        switch( InShape )
        {
        case CrownShape::Conical: return 0.2f + 0.8f * r;
        case CrownShape::Spherical: return 0.2f + 0.8f * std::sin( pi * r );
        case CrownShape::Hemispherical: return 0.2f + 0.8f * std::sin( 0.5f * pi * r );
        case CrownShape::Cylindrical: return 1.f;
        case CrownShape::TaperedCylindrical: return 0.5f + 0.5f * r;
        case CrownShape::Flame: return r <= 0.7f ? r / 0.7f : ( 1.f - r ) / 0.3f;
        case CrownShape::InverseConical: return 1.f - 0.8f * r;
        case CrownShape::TendFlame: return r <= 0.7f ? 0.5f + 0.5f * r / 0.7f : 0.5f + 0.5f * ( 1.f - r ) / 0.3f;
        }
        return 1.f;
    }
}
