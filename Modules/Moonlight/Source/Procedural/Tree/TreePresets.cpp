#include "TreePresets.h"

namespace MitchTree
{
    namespace
    {
        BranchLevel Level( int InCount, float InStart, float InDown, float InDownTip, float InLength, float InRadius, float InCurve, float InSegment, float InFlexibility )
        {
            BranchLevel level;
            level.Count = InCount;
            level.Start = InStart;
            level.DownAngle = InDown;
            level.DownAngleTip = InDownTip;
            level.Length = InLength;
            level.Radius = InRadius;
            level.Curve = InCurve;
            level.SegmentLength = InSegment;
            level.Flexibility = InFlexibility;
            return level;
        }


        TreeRecipe Oak()
        {
            TreeRecipe r;
            r.Name = "Oak";
            r.Seed = 7;
            r.Trunk.Height = 11.f; r.Trunk.Radius = 0.42f; r.Trunk.Taper = 0.75f; r.Trunk.TaperPower = 1.6f;
            r.Trunk.Flare = 0.8f; r.Trunk.FlareHeight = 0.07f; r.Trunk.FlareLobes = 6; r.Trunk.LobeDepth = 0.45f;
            r.Trunk.Lean = 5.f; r.Trunk.Bend = 8.f; r.Trunk.Gnarl = 0.15f; r.Trunk.Splits = 2; r.Trunk.SplitHeight = 0.42f; r.Trunk.SplitAngle = 28.f;
            r.Crown = CrownShape::Hemispherical;
            BranchLevel limbs = Level( 22, 0.32f, 62.f, -30.f, 0.55f, 0.62f, 18.f, 0.3f, 0.8f );
            limbs.DownAngleVariance = 12.f; limbs.Gravity = -0.04f; limbs.Gnarl = 0.35f; limbs.ForkChance = 0.06f; limbs.ForkAngle = 35.f; limbs.Collar = 0.35f;
            BranchLevel branches = Level( 10, 0.2f, 52.f, -15.f, 0.45f, 0.6f, -10.f, 0.2f, 1.2f );
            branches.Gnarl = 0.4f; branches.LengthAtTip = 0.45f;
            BranchLevel twigs = Level( 6, 0.2f, 45.f, 0.f, 0.42f, 0.6f, 0.f, 0.12f, 1.6f );
            twigs.Gnarl = 0.3f; twigs.LengthAtTip = 0.55f;
            r.Branches = { limbs, branches, twigs };
            r.Leaves.Kind = LeafKind::Broadleaf; r.Leaves.Size = 0.42f; r.Leaves.PerStem = 5; r.Leaves.Start = 0.3f;
            r.Leaves.DownAngle = 45.f; r.Leaves.UpBias = 0.5f; r.Leaves.Curl = 20.f; r.Leaves.Roundness = 0.65f;
            r.Bark.TileWidth = 0.9f; r.Bark.TileHeight = 1.6f;
            r.BarkTextures.Style = BarkStyle::Furrowed; r.BarkTextures.Color = Vector3( 0.4f, 0.33f, 0.26f ); r.BarkTextures.CrevasseColor = Vector3( 0.1f, 0.08f, 0.06f );
            r.BarkTextures.Scale = 7.f; r.BarkTextures.Stretch = 2.5f; r.BarkTextures.Depth = 1.3f; r.BarkTextures.Moss = 0.25f; r.BarkTextures.Seed = 7;
            r.LeafTextures.Shape = LeafShape::Lobed; r.LeafTextures.Leaves = 12; r.LeafTextures.Aspect = 0.55f; r.LeafTextures.Lobes = 7; r.LeafTextures.LobeDepth = 0.35f;
            r.LeafTextures.Color = Vector3( 0.14f, 0.27f, 0.06f ); r.LeafTextures.TipColor = Vector3( 0.26f, 0.36f, 0.08f ); r.LeafTextures.Seed = 7;
            return r;
        }


        TreeRecipe Maple()
        {
            TreeRecipe r;
            r.Name = "Maple";
            r.Seed = 21;
            r.Trunk.Height = 13.f; r.Trunk.Radius = 0.3f; r.Trunk.Taper = 0.85f; r.Trunk.TaperPower = 1.3f;
            r.Trunk.Flare = 0.55f; r.Trunk.FlareLobes = 5; r.Trunk.Lean = 3.f; r.Trunk.Bend = 5.f; r.Trunk.Gnarl = 0.08f;
            r.Crown = CrownShape::TendFlame;
            BranchLevel limbs = Level( 30, 0.3f, 50.f, -25.f, 0.45f, 0.55f, 15.f, 0.3f, 0.9f );
            limbs.Gnarl = 0.2f; limbs.ForkChance = 0.04f;
            BranchLevel branches = Level( 12, 0.2f, 48.f, -10.f, 0.45f, 0.6f, -5.f, 0.2f, 1.3f );
            branches.LengthAtTip = 0.45f; branches.Gnarl = 0.25f;
            BranchLevel twigs = Level( 6, 0.25f, 42.f, 0.f, 0.4f, 0.6f, 0.f, 0.12f, 1.6f );
            r.Branches = { limbs, branches, twigs };
            r.Leaves.Kind = LeafKind::Broadleaf; r.Leaves.Size = 0.36f; r.Leaves.PerStem = 6; r.Leaves.Roundness = 0.7f; r.Leaves.UpBias = 0.45f;
            r.BarkTextures.Style = BarkStyle::Furrowed; r.BarkTextures.Color = Vector3( 0.42f, 0.38f, 0.34f ); r.BarkTextures.CrevasseColor = Vector3( 0.14f, 0.12f, 0.1f );
            r.BarkTextures.Scale = 9.f; r.BarkTextures.Stretch = 4.f; r.BarkTextures.Depth = 0.8f; r.BarkTextures.Seed = 21;
            r.LeafTextures.Shape = LeafShape::Palmate; r.LeafTextures.Leaves = 9; r.LeafTextures.Aspect = 0.9f; r.LeafTextures.LobeDepth = 0.45f; r.LeafTextures.Serration = 0.15f;
            r.LeafTextures.Color = Vector3( 0.18f, 0.34f, 0.07f ); r.LeafTextures.TipColor = Vector3( 0.4f, 0.42f, 0.1f ); r.LeafTextures.Seed = 21;
            return r;
        }


        TreeRecipe Birch()
        {
            TreeRecipe r;
            r.Name = "Birch";
            r.Seed = 3;
            r.Trunk.Height = 15.f; r.Trunk.Radius = 0.17f; r.Trunk.Taper = 0.92f; r.Trunk.TaperPower = 1.1f;
            r.Trunk.Flare = 0.3f; r.Trunk.FlareLobes = 3; r.Trunk.LobeDepth = 0.2f; r.Trunk.Lean = 6.f; r.Trunk.Bend = 5.f; r.Trunk.Gnarl = 0.05f;
            r.Crown = CrownShape::Flame;
            BranchLevel limbs = Level( 40, 0.35f, 42.f, -12.f, 0.3f, 0.45f, -25.f, 0.25f, 1.3f );
            limbs.Gravity = -0.15f; limbs.Gnarl = 0.15f;
            BranchLevel twigs = Level( 14, 0.15f, 40.f, 0.f, 0.5f, 0.55f, -20.f, 0.12f, 1.8f );
            twigs.Gravity = -0.45f; twigs.LengthAtTip = 0.6f;
            r.Branches = { limbs, twigs };
            r.Leaves.Kind = LeafKind::Broadleaf; r.Leaves.Size = 0.26f; r.Leaves.PerStem = 9; r.Leaves.DownAngle = 60.f;
            r.Leaves.UpBias = 0.2f; r.Leaves.Roundness = 0.5f; r.Leaves.Curl = 10.f;
            r.Bark.TileWidth = 0.6f; r.Bark.TileHeight = 1.2f;
            r.BarkTextures.Style = BarkStyle::Birch; r.BarkTextures.CrevasseColor = Vector3( 0.08f, 0.07f, 0.07f ); r.BarkTextures.Scale = 4.f; r.BarkTextures.Stretch = 2.f; r.BarkTextures.Depth = 0.6f; r.BarkTextures.Seed = 3;
            r.LeafTextures.Shape = LeafShape::Serrated; r.LeafTextures.Leaves = 11; r.LeafTextures.Aspect = 0.6f; r.LeafTextures.Serration = 0.15f;
            r.LeafTextures.Color = Vector3( 0.2f, 0.36f, 0.08f ); r.LeafTextures.TipColor = Vector3( 0.38f, 0.48f, 0.12f ); r.LeafTextures.Seed = 3;
            r.Wind.Leaves = 1.4f;
            return r;
        }


        TreeRecipe Pine()
        {
            TreeRecipe r;
            r.Name = "Pine";
            r.Seed = 11;
            r.Trunk.Height = 18.f; r.Trunk.Radius = 0.3f; r.Trunk.Taper = 0.9f; r.Trunk.TaperPower = 1.2f;
            r.Trunk.Flare = 0.4f; r.Trunk.FlareLobes = 4; r.Trunk.Lean = 4.f; r.Trunk.Bend = 8.f; r.Trunk.Gnarl = 0.08f;
            r.Crown = CrownShape::TendFlame;
            BranchLevel limbs = Level( 25, 0.6f, 80.f, -40.f, 0.3f, 0.4f, 25.f, 0.3f, 0.8f );
            limbs.Layout = Arrangement::Whorled; limbs.WhorlSize = 4; limbs.Gnarl = 0.3f;
            BranchLevel branches = Level( 10, 0.2f, 60.f, 0.f, 0.45f, 0.5f, 10.f, 0.15f, 1.3f );
            branches.Gnarl = 0.2f;
            r.Branches = { limbs, branches };
            r.Leaves.Kind = LeafKind::Needles; r.Leaves.Size = 0.34f; r.Leaves.PerStem = 8; r.Leaves.Crossed = true;
            r.Leaves.DownAngle = 30.f; r.Leaves.UpBias = 0.6f; r.Leaves.Roundness = 0.5f; r.Leaves.Curl = 8.f;
            r.Bark.TileWidth = 0.8f; r.Bark.TileHeight = 1.4f;
            r.BarkTextures.Style = BarkStyle::Plated; r.BarkTextures.Color = Vector3( 0.55f, 0.33f, 0.22f ); r.BarkTextures.CrevasseColor = Vector3( 0.12f, 0.08f, 0.06f );
            r.BarkTextures.Scale = 5.f; r.BarkTextures.Stretch = 1.6f; r.BarkTextures.Depth = 1.5f; r.BarkTextures.Seed = 11;
            r.LeafTextures.Shape = LeafShape::Needles; r.LeafTextures.Leaves = 3; r.LeafTextures.NeedleLength = 0.22f;
            r.LeafTextures.Color = Vector3( 0.1f, 0.22f, 0.08f ); r.LeafTextures.TipColor = Vector3( 0.18f, 0.3f, 0.1f ); r.LeafTextures.Seed = 11;
            r.Foliage.Translucency = 0.35f;
            r.Wind.Leaves = 0.5f;
            return r;
        }


        TreeRecipe Spruce()
        {
            TreeRecipe r;
            r.Name = "Spruce";
            r.Seed = 5;
            r.Trunk.Height = 16.f; r.Trunk.Radius = 0.25f; r.Trunk.Taper = 0.96f; r.Trunk.TaperPower = 1.f;
            r.Trunk.Flare = 0.4f; r.Trunk.FlareLobes = 4; r.Trunk.Lean = 2.f; r.Trunk.Bend = 3.f; r.Trunk.Gnarl = 0.04f;
            r.Crown = CrownShape::Conical;
            BranchLevel limbs = Level( 70, 0.1f, 95.f, -45.f, 0.32f, 0.35f, 15.f, 0.25f, 1.f );
            limbs.Layout = Arrangement::Whorled; limbs.WhorlSize = 5; limbs.LengthVariance = 0.1f; limbs.Gravity = -0.25f; limbs.Gnarl = 0.1f;
            BranchLevel branches = Level( 18, 0.1f, 70.f, 0.f, 0.35f, 0.5f, 0.f, 0.1f, 1.5f );
            branches.Layout = Arrangement::Alternate; branches.Gravity = -0.3f; branches.LengthAtTip = 0.4f;
            r.Branches = { limbs, branches };
            r.Leaves.Kind = LeafKind::Needles; r.Leaves.Size = 0.3f; r.Leaves.PerStem = 6; r.Leaves.Crossed = true;
            r.Leaves.DownAngle = 20.f; r.Leaves.UpBias = 0.3f; r.Leaves.Roundness = 0.35f; r.Leaves.Curl = 5.f;
            r.BarkTextures.Style = BarkStyle::Plated; r.BarkTextures.Color = Vector3( 0.38f, 0.28f, 0.22f ); r.BarkTextures.Scale = 10.f; r.BarkTextures.Stretch = 1.4f; r.BarkTextures.Seed = 5;
            r.LeafTextures.Shape = LeafShape::Needles; r.LeafTextures.Leaves = 8; r.LeafTextures.NeedleLength = 0.09f;
            r.LeafTextures.Color = Vector3( 0.07f, 0.17f, 0.09f ); r.LeafTextures.TipColor = Vector3( 0.12f, 0.24f, 0.12f ); r.LeafTextures.Seed = 5;
            r.Foliage.Translucency = 0.25f;
            r.Wind.Leaves = 0.4f;
            return r;
        }


        TreeRecipe Palm()
        {
            TreeRecipe r;
            r.Name = "Palm";
            r.Seed = 9;
            r.Trunk.Height = 9.f; r.Trunk.Radius = 0.2f; r.Trunk.Taper = 0.3f; r.Trunk.TaperPower = 1.f;
            r.Trunk.Flare = 0.45f; r.Trunk.FlareHeight = 0.04f; r.Trunk.FlareLobes = 0; r.Trunk.Lean = 12.f; r.Trunk.Bend = 18.f; r.Trunk.Gnarl = 0.03f;
            r.Crown = CrownShape::Cylindrical;
            BranchLevel fronds = Level( 16, 0.97f, 50.f, 0.f, 0.45f, 0.12f, -70.f, 0.25f, 1.5f );
            fronds.End = 1.f; fronds.DownAngleVariance = 25.f; fronds.CurveVariance = 15.f; fronds.Gravity = -0.2f;
            fronds.Taper = 0.9f; fronds.Collar = 0.5f; fronds.Gnarl = 0.05f;
            r.Branches = { fronds };
            r.Leaves.Kind = LeafKind::Frond; r.Leaves.FrondWidth = 1.3f; r.Leaves.FrondFold = 30.f; r.Leaves.Roundness = 0.2f;
            r.Bark.TileWidth = 0.7f; r.Bark.TileHeight = 0.5f;
            r.BarkTextures.Style = BarkStyle::Ringed; r.BarkTextures.Color = Vector3( 0.5f, 0.42f, 0.33f ); r.BarkTextures.Scale = 6.f; r.BarkTextures.Seed = 9;
            r.LeafTextures.Shape = LeafShape::Frond; r.LeafTextures.Columns = 1; r.LeafTextures.Rows = 1;
            r.LeafTextures.Color = Vector3( 0.2f, 0.33f, 0.08f ); r.LeafTextures.TipColor = Vector3( 0.35f, 0.42f, 0.12f ); r.LeafTextures.TwigColor = Vector3( 0.4f, 0.38f, 0.2f ); r.LeafTextures.Seed = 9;
            r.Foliage.Translucency = 0.5f;
            r.Wind.Branches = 1.4f;
            return r;
        }


        TreeRecipe DeadSnag()
        {
            TreeRecipe r;
            r.Name = "DeadSnag";
            r.Seed = 13;
            r.Trunk.Height = 9.f; r.Trunk.Radius = 0.32f; r.Trunk.Taper = 0.7f; r.Trunk.TaperPower = 1.4f;
            r.Trunk.Flare = 0.7f; r.Trunk.FlareLobes = 5; r.Trunk.LobeDepth = 0.5f; r.Trunk.Lean = 8.f; r.Trunk.Bend = 12.f; r.Trunk.Gnarl = 0.25f;
            r.Trunk.Splits = 1; r.Trunk.SplitHeight = 0.6f;
            r.Crown = CrownShape::Spherical;
            BranchLevel limbs = Level( 14, 0.35f, 60.f, -20.f, 0.35f, 0.55f, 30.f, 0.25f, 0.5f );
            limbs.LengthVariance = 0.6f; limbs.Gnarl = 0.6f; limbs.ForkChance = 0.1f; limbs.Taper = 0.6f;
            BranchLevel branches = Level( 6, 0.2f, 50.f, 0.f, 0.4f, 0.6f, 10.f, 0.15f, 0.8f );
            branches.Gnarl = 0.6f; branches.LengthVariance = 0.5f;
            r.Branches = { limbs, branches };
            r.Leaves.Kind = LeafKind::None;
            r.Bark.Roughness = 0.95f;
            r.BarkTextures.Style = BarkStyle::Weathered; r.BarkTextures.Color = Vector3( 0.55f, 0.53f, 0.5f ); r.BarkTextures.CrevasseColor = Vector3( 0.15f, 0.14f, 0.13f );
            r.BarkTextures.Scale = 8.f; r.BarkTextures.Stretch = 6.f; r.BarkTextures.Depth = 1.4f; r.BarkTextures.Moss = 0.3f; r.BarkTextures.Roughness = 0.95f; r.BarkTextures.Seed = 13;
            r.Wind.Branches = 0.4f;
            return r;
        }


        TreeRecipe Bush()
        {
            TreeRecipe r;
            r.Name = "Bush";
            r.Seed = 17;
            r.Trunk.Height = 0.6f; r.Trunk.Radius = 0.05f; r.Trunk.Taper = 0.5f; r.Trunk.Flare = 0.2f; r.Trunk.FlareLobes = 0;
            r.Trunk.Splits = 4; r.Trunk.SplitHeight = 0.15f; r.Trunk.SplitAngle = 35.f; r.Trunk.SinkDepth = 0.1f;
            r.Trunk.SegmentLength = 0.1f; r.Trunk.Gnarl = 0.2f; r.Trunk.GnarlScale = 2.f;
            r.Crown = CrownShape::Spherical;
            BranchLevel stems = Level( 14, 0.1f, 45.f, -10.f, 1.6f, 0.7f, 20.f, 0.12f, 1.2f );
            stems.Gnarl = 0.4f;
            BranchLevel twigs = Level( 8, 0.2f, 50.f, 0.f, 0.4f, 0.6f, 0.f, 0.08f, 1.8f );
            r.Branches = { stems, twigs };
            r.Leaves.Kind = LeafKind::Broadleaf; r.Leaves.Size = 0.22f; r.Leaves.PerStem = 8; r.Leaves.Roundness = 0.7f;
            r.BarkTextures.Style = BarkStyle::Smooth; r.BarkTextures.Color = Vector3( 0.35f, 0.28f, 0.22f ); r.BarkTextures.Seed = 17;
            r.LeafTextures.Shape = LeafShape::Round; r.LeafTextures.Leaves = 10; r.LeafTextures.Aspect = 0.8f;
            r.LeafTextures.Color = Vector3( 0.12f, 0.26f, 0.07f ); r.LeafTextures.TipColor = Vector3( 0.22f, 0.34f, 0.08f ); r.LeafTextures.Seed = 17;
            r.Lod.Transition = 0.3f;
            return r;
        }
    }


    const std::vector<std::string>& PresetNames()
    {
        static const std::vector<std::string> names = { "Oak", "Maple", "Birch", "Pine", "Spruce", "Palm", "DeadSnag", "Bush" };
        return names;
    }


    bool MakePreset( const std::string& InName, TreeRecipe& OutRecipe )
    {
        if( InName == "Oak" ) OutRecipe = Oak();
        else if( InName == "Maple" ) OutRecipe = Maple();
        else if( InName == "Birch" ) OutRecipe = Birch();
        else if( InName == "Pine" ) OutRecipe = Pine();
        else if( InName == "Spruce" ) OutRecipe = Spruce();
        else if( InName == "Palm" ) OutRecipe = Palm();
        else if( InName == "DeadSnag" ) OutRecipe = DeadSnag();
        else if( InName == "Bush" ) OutRecipe = Bush();
        else return false;
        OutRecipe.Sanitize();
        return true;
    }
}
