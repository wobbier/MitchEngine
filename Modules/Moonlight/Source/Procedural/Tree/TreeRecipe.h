#pragma once
#include "JSON.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Reflection/Reflection.h"
#include <cstdint>
#include <string>
#include <vector>

// MitchTree: procedural trees from a recipe (a `.mtree` asset). The recipe is plain data; Generate()
// (TreeGenerator.h) turns it into geometry deterministically, so the asset is just these parameters.
namespace MitchTree
{
    constexpr int FormatVersion = 1;
    constexpr int MaxBranchLevels = 4;

    // Weber & Penn crown envelopes: first-level branch length against height up the crown.
    enum class CrownShape { Conical, Spherical, Hemispherical, Cylindrical, TaperedCylindrical, Flame, InverseConical, TendFlame };
    // How siblings are arranged around their parent.
    enum class Arrangement { Spiral, Alternate, Opposite, Whorled };
    enum class LeafKind { None, Broadleaf, Needles, Frond };

    struct TrunkSettings
    {
        ME_REFLECTABLE( TrunkSettings )
    public:
        float Height = 12.f;            // metres above the ground
        float HeightVariance = 0.1f;    // fraction, per seed
        float Radius = 0.28f;           // metres, above the root flare
        float Taper = 0.85f;            // fraction of the radius lost by the tip
        float TaperPower = 1.2f;        // above 1 keeps the trunk thick for longer
        float Flare = 0.7f;             // extra radius at the ground (fraction)
        float FlareHeight = 0.06f;      // fraction of the height over which the flare fades
        int FlareLobes = 5;             // root buttresses
        float LobeDepth = 0.35f;
        float Lean = 4.f;               // degrees, in a seeded direction
        float Bend = 6.f;               // degrees of curvature over the height
        float Gnarl = 0.06f;            // noise wiggle of the direction
        float GnarlScale = 0.5f;        // wiggles per metre
        int Splits = 0;                 // leaders the trunk forks into at SplitHeight (0 = none)
        float SplitHeight = 0.55f;      // fraction of the height
        float SplitAngle = 22.f;        // degrees between the trunk and each leader
        float SinkDepth = 0.4f;         // metres below the ground, so slopes never show the base
        float SegmentLength = 0.35f;    // metres between rings
    };

    struct BranchLevel
    {
        ME_REFLECTABLE( BranchLevel )
    public:
        bool Enabled = true;
        int Count = 30;                 // children on a full-length parent
        float CountVariance = 0.15f;
        float Start = 0.3f;             // along the parent: 0 base, 1 tip
        float End = 0.97f;
        Arrangement Layout = Arrangement::Spiral;
        float Rotation = 137.5f;        // degrees between consecutive siblings (Spiral)
        int WhorlSize = 4;              // siblings per whorl (Whorled)
        float DownAngle = 55.f;         // degrees away from the parent's axis
        float DownAngleVariance = 10.f;
        float DownAngleTip = -20.f;     // change of the down angle from the parent's base to its tip
        float Length = 0.45f;           // level 1: fraction of the trunk height; deeper: of the parent
        float LengthVariance = 0.2f;
        float LengthAtTip = 0.4f;       // (levels 2+) length multiplier toward the parent's tip
        float Radius = 0.55f;           // fraction of the parent's radius where it attaches
        float Taper = 0.8f;
        float Curve = -10.f;            // degrees of bend over the length: positive up, negative down
        float CurveVariance = 12.f;
        float Gravity = 0.f;            // per metre: positive turns up (gravitropism), negative droops
        float Phototropism = 0.f;       // per metre: turns outward, away from the trunk
        float Gnarl = 0.12f;
        float ForkChance = 0.f;         // per segment
        float ForkAngle = 30.f;
        float SegmentLength = 0.25f;
        float Flexibility = 1.f;        // wind sway
        float Collar = 0.3f;            // extra radius where it leaves the parent
    };

    // A leaf picture in the leaf texture: where it sits, where its stalk attaches and which way its
    // tip points (0 degrees = up the image).
    struct LeafRegion
    {
        ME_REFLECTABLE( LeafRegion )
    public:
        Vector4 Rect = Vector4( 0.f, 0.f, 1.f, 1.f );   // u0, v0, u1, v1
        Vector2 Pivot = Vector2( 0.5f, 1.f );
        float Angle = 0.f;
        float Weight = 1.f;
    };

    struct LeafSettings
    {
        ME_REFLECTABLE( LeafSettings )
    public:
        LeafKind Kind = LeafKind::Broadleaf;
        float Size = 0.35f;             // metres from stalk to tip (a card may picture a cluster)
        float SizeVariance = 0.25f;
        float Width = 1.f;              // width scale over the texture's own aspect
        int PerStem = 12;               // on each outermost stem
        float Start = 0.2f;             // along the stem
        float DownAngle = 50.f;
        float DownAngleVariance = 15.f;
        float UpBias = 0.4f;            // turns faces toward the sky
        float Curl = 15.f;              // degrees of bend along the length
        float Roundness = 0.6f;         // crown-spherical normals (soft, volumetric lighting)
        bool Crossed = false;           // a second card at 90 degrees (needle sprays)
        float FrondWidth = 0.9f;        // (Frond) metres at the widest point
        float FrondFold = 25.f;         // (Frond) degrees each half folds down from the spine
        std::vector<LeafRegion> Regions;
    };

    struct BarkMaterial
    {
        ME_REFLECTABLE( BarkMaterial )
    public:
        std::string BaseColorMap;
        std::string NormalMap;
        std::string OrmMap;
        Vector3 Tint = Vector3( 1.f, 1.f, 1.f );
        float Roughness = 0.9f;
        float NormalStrength = 1.f;
        float TileWidth = 0.8f;         // metres around the trunk per texture repeat
        float TileHeight = 1.6f;        // metres along a stem per texture repeat
    };

    struct LeafMaterial
    {
        ME_REFLECTABLE( LeafMaterial )
    public:
        std::string BaseColorMap;
        std::string NormalMap;
        Vector3 Tint = Vector3( 1.f, 1.f, 1.f );
        float Roughness = 0.65f;
        float Translucency = 0.6f;      // light through the leaves when back-lit
        float AlphaCutoff = 0.45f;
        float Variation = 0.15f;        // per-branch tint variation
    };

    struct WindSettings
    {
        ME_REFLECTABLE( WindSettings )
    public:
        float Trunk = 1.f;              // sway of the whole tree
        float Branches = 1.f;
        float Leaves = 1.f;             // flutter
        float Frequency = 1.f;
    };

    struct LodSettings
    {
        ME_REFLECTABLE( LodSettings )
    public:
        int Levels = 3;                 // simplified levels after full detail (0..4)
        float Transition = 0.4f;        // screen height for level 1; each further level halves it
        float LeafKeep = 0.5f;          // fraction of leaf cards each level keeps (scaled up to cover)
    };

    struct TreeRecipe
    {
        ME_REFLECTABLE( TreeRecipe )
    public:
        std::string Name = "Tree";
        uint32_t Seed = 1;
        TrunkSettings Trunk;
        CrownShape Crown = CrownShape::Spherical;
        std::vector<BranchLevel> Branches;
        LeafSettings Leaves;
        float Occlusion = 0.75f;        // strength of the baked crown self-shadowing
        BarkMaterial Bark;
        LeafMaterial Foliage;
        WindSettings Wind;
        LodSettings Lod;

        // `.mtree` JSON: { "Format": "MitchTree", "Version": n, ...fields }.
        json ToJson() const;
        bool FromJson( const json& InJson, std::string& OutError );
        // Clamps every value into its supported range (after loading or editing).
        void Sanitize();
    };

    float ShapeRatio( CrownShape InShape, float InRatio );
}
