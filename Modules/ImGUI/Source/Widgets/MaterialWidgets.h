#pragma once
#include "ScrubField.h"

// Material widgets (UX-12): what a StandardMaterial's numbers look like, drawn with ImGui
// primitives (no GPU). Each is a whole inspector row (a caption in the label column, the widget in
// the value column), returns true when it changed a value and is made of real ImGui items, so the
// inspector turns a gesture into one undo step and automation finds the parts by caption:
// "Mesh/Surface/Puck", "Mesh/Surface", "Mesh/Glow", "Mesh/Glow/Value", "Mesh/Glow/Swatch",
// "Mesh/Relief/Tile".
namespace MeWidgets
{
    // Pure math behind the widgets: no ImGui, unit tested.
    namespace MaterialMath
    {
        // ---- Emissive intensity in stops ---------------------------------------------------
        // The EV field's ends: its bottom (-9 EV) stands for 0 ("Off"), its top is 1024x.
        constexpr double kOffEV = -9.0;
        constexpr double kMaxEV = 10.0;
        // Where a lit intensity at or under 2^kOffEV sits: just above Off, so it stays distinct
        // from a true 0 (it reads "-9.0 EV", and "off" still zeroes it).
        constexpr double kDimEV = kOffEV + 0.01;
        // log2( intensity ); 0 (and below) is kOffEV, a lit intensity under 2^kDimEV is kDimEV.
        double IntensityToEV( double InIntensity );
        // 2^EV; kOffEV and below are 0.
        double EVToIntensity( double InEV );
        // A typed multiplier ("4x") as EV: log2, -inf for 0 (the field clamps it to Off), NaN for
        // a negative one (the field refuses it).
        double MultiplierToEV( double InMultiplier );
        // The EV field: EV with an "Off" end, a soft span of -3..+7 for scrubs, 0 EV (1x) default,
        // typed entry in EV, "off" or a multiplier.
        NumberSpec EmissiveSpec();

        // ---- Metallic x Roughness ---------------------------------------------------------
        // The pad's swatch grid: kSwatchCells x kSwatchCells cells centred on quarters (0, 0.25 ..
        // 1) of metallic (rows, bottom 0) and roughness (columns, left 0).
        constexpr int kSwatchCells = 5;
        double CellValue( int InCell );
        int NearestCell( double InValue );
        // The nearest cell centre (Alt snaps the puck to it).
        double SnapToCell( double InValue );
        // What a surface with these values reads as: "Polished metal", "Satin", "Chalk"...
        const char* LooksLike( double InMetallic, double InRoughness );

        // How a swatch sphere is painted, in linear RGB (0..1+, tonemapped for display):
        // concentric gradient circles from Rim (the unlit edge) to Body (the lit side), then a
        // highlight of Spec at HighlightPeak strength, HighlightRadius (of the sphere radius)
        // wide, LightOffset (of the radius) towards the light.
        struct SphereShade
        {
            double Body[3] = {};
            double Rim[3] = {};
            double Spec[3] = {};
            double HighlightRadius = 0.0;
            double HighlightPeak = 0.0;
            double LightOffset = 0.0;
        };
        // InBaseSrgb is the material's DiffuseColor (sRGB 0..1). Dielectrics show their albedo
        // with a white highlight; metals have no diffuse body: they reflect a dim environment and
        // the light tinted by the base colour. Roughness widens and dims the highlight.
        SphereShade ShadeSphere( const float* InBaseSrgb, double InMetallic, double InRoughness );

        // Linear RGB to what the screen shows, sRGB 0..1: 0..1 as is; brighter colours keep their
        // hue and roll off towards white (a soft Reinhard on the brightest channel's excess).
        void ToDisplay( const double* InLinear, float* OutSrgb );
        // The emissive colour (sRGB) at an intensity, as the screen shows it (tonemapped).
        void EmissiveDisplay( const float* InColorSrgb, double InIntensity, float* OutSrgb );

        // ---- Normal strength --------------------------------------------------------------
        // A tile of round bumps, one per unit cell: height 0..1 at (InU, InV).
        double BumpHeight( double InU, double InV );
        // The tile lit from the upper left with its normals scaled by InStrength (0 flat, 1 as
        // authored, 2 doubled): 0..1 brightness. Flat surfaces get the same brightness everywhere.
        double BumpLight( double InU, double InV, double InStrength );
    }

    // Metallic (y, dielectric at the bottom, metal at the top) by Roughness (x, smooth to rough)
    // over a 5 x 5 grid of shaded spheres in the base colour. Drag the "Puck" (or click a
    // sphere) in hundredths; Alt snaps it to the sphere centres; double-click the puck for the
    // defaults (metallic 0, roughness 0.5). Under the pad: what the surface looks like.
    bool MetalRoughPad( const char* InCaption, double& InOutMetallic, double& InOutRoughness, const float* InBaseSrgb );

    // The emissive output on a strip of stops from Off to +10 EV, painted with the colour at
    // each intensity (tonemapped), and a "Swatch" of the colour at the current intensity. Click
    // or drag along the strip (third stops, the left end is Off); double-click the value's
    // marker for 1x.
    bool EmissiveStrip( const char* InCaption, double& InOutIntensity, const float* InColorSrgb );

    // A tile of bumps in the base colour, lit from the upper left, as deep as InOutStrength
    // (0..2) makes them. Dragging across it scrubs the strength like the field above it.
    bool BumpPreview( const char* InCaption, double& InOutStrength, const NumberSpec& InSpec, const float* InBaseSrgb );
}
