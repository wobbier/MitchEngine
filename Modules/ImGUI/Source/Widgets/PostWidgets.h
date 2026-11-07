#pragma once
#include "ScrubField.h"

// Post-processing and grading widgets (UX-7), built on Graph like the light and camera widgets:
// each is a whole inspector row (a caption in the label column, the widget in the value column),
// returns true when it changed a value and is made of real ImGui items, so the inspector turns a
// gesture into one undo step and automation finds the parts by caption:
// "PostProcess/Lit Like/Overcast", "PostProcess/Tonemap Curve/AgX",
// "PostProcess/White Balance/Puck", "PostProcess/Lift Gamma Gain/Gain",
// "PostProcess/Fog Visibility/Visibility".
namespace MeWidgets
{
    // The engine's image math, replicated from Assets/Shaders/Post (Tonemap.frag, PostCommon.sh)
    // and Assets/Shaders/Fog.sh. Pure: no ImGui, unit tested against the shader formulas.
    namespace PostMath
    {
        // Moonlight::Tonemapper, in order: 0 None (clamp), 1 ACES, 2 AgX, 3 Reinhard.
        constexpr int kTonemapperCount = 4;
        const char* TonemapperName( int InOperator );

        // An operator on linear RGB, as the shader applies it (output linear display, 0..1).
        void Tonemap( int InOperator, const double* InRgb, double* OutRgb );
        // The operator on a grey of linear luminance InLuminance: the Rec. 709 luma of the result.
        double TonemapGrey( int InOperator, double InLuminance );

        // Linear -> sRGB transfer (ShaderLib.sh toGammaAccurate), and back.
        double SrgbEncode( double InLinear );
        double SrgbDecode( double InEncoded );

        // What the screen shows for a grey of scene luminance InLuminance with InExposureEV stops of
        // exposure (manual exposure: a 2^EV multiplier), sRGB-encoded 0..1, before grading.
        double DisplayGrey( int InOperator, double InLuminance, double InExposureEV );

        // Contrast, lift, gain and gamma as Tonemap.frag grades a display-linear channel:
        // c = (c - 0.18) * contrast + 0.18; c = c * gain + lift * (1 - c); c = max(c, 0)^(1 / gamma).
        struct Grade
        {
            double Contrast = 1.0;
            double Lift = 0.0;
            double Gamma = 1.0;
            double Gain = 1.0;
        };
        // The graded channel before the max(c, 0): sign(c) |c|^(1 / gamma), so a crushed black
        // shows how far below zero lift pushed it.
        double GradeSigned( const Grade& InGrade, double InLinear );
        // What the shader outputs (clamped to 0..1).
        double GradeChannel( const Grade& InGrade, double InLinear );
        // The parameter that puts GradeSigned's output at InLinear 0 (lift), 1 (gain) or InAt
        // (gamma) on InTarget (display-linear, signed). False when no value can.
        bool SolveLift( const Grade& InGrade, double InTarget, double& OutLift );
        bool SolveGain( const Grade& InGrade, double InTarget, double& OutGain );
        bool SolveGamma( const Grade& InGrade, double InAt, double InTarget, double& OutGamma );

        // PostCommon.sh whiteBalance: per-channel scale (1 + 0.2 t, 1 - 0.1 tint, 1 - 0.2 t) for
        // temperature t and tint in -1..1.
        void WhiteBalanceScale( double InTemperature, double InTint, double* OutScale );

        // Exponential fog looking level at the base height (Fog.sh fogAnalytic with a horizontal
        // ray): transmittance exp(-density (d - start)) past the start distance, opacity capped
        // at InMaxOpacity. InHeightFactor scales the density for a ray above or below the base
        // height (exp(-falloff (y - height)), capped like fogDensity).
        double FogOpacity( double InDistance, double InDensity, double InStart, double InMaxOpacity = 1.0, double InHeightFactor = 1.0 );
        // The distance where fog reaches 95% opacity: start + ln(20) / density; infinity when the
        // density is zero or the max opacity stays under 95%.
        double FogVisibility( double InDensity, double InStart, double InMaxOpacity = 1.0, double InHeightFactor = 1.0 );
        // The density for a visibility (inverse of FogVisibility): 0 for infinity; false for a
        // visibility inside the start distance, which no density gives.
        bool FogDensityFor( double InVisibility, double InStart, double& OutDensity, double InHeightFactor = 1.0 );
        // exp(min(-falloff (y - height), 8)), Fog.sh fogDensity's factor.
        double FogHeightFactor( double InY, double InFogHeight, double InFalloff );
    }

    // Exposure compensation on a bipolar strip from -6 to +6 EV with a centre detent at 0 and
    // looks-like rungs (Sunlight, Overcast, Indoor, Night street). Click a rung, drag along the
    // strip (third stops, snapping to the rungs and 0) or double-click to return to 0.
    bool ExposureLadder( const char* InCaption, double& InOutEV, const NumberSpec& InSpec );

    // The tonemapper's curve: display value (sRGB 0..1) against scene luminance (log, 0.001..100)
    // with InExposureEV applied; the other operators faint behind it. Their names under the plot
    // switch InOutOperator. Reports what mid grey (0.18) shows: "ACES: 0.18 -> 0.36" at 0 EV.
    bool TonemapCurve( const char* InCaption, int& InOutOperator, double InExposureEV, bool InAutoExposure );

    // Temperature (x, cool..warm) and tint (y, green..magenta) on an XY pad tinted with the white
    // balance's shift of a neutral grey. Drag the "Puck" (or click the pad); it snaps to 0 on
    // each axis near the crosshair; double-click resets both to 0.
    bool WhiteBalancePad( const char* InCaption, double& InOutTemperature, double& InOutTint );

    // The grade's tone curve (output against input, both as seen: sRGB-encoded) with "Lift",
    // "Gamma" and "Gain" handles on it at black, mid grey and white; each drags vertically and
    // double-click resets it (0, 1, 1). Contrast shapes the curve but is edited by its field.
    bool LiftGammaGainCurve( const char* InCaption, double& InOutLift, double& InOutGamma, double& InOutGain, double InContrast );

    // "visibility 86 m": where the fog reaches 95%, over a log distance bar (1 m .. 10 km)
    // shaded with the fog's opacity in InFogSrgb. Drag the "Visibility" handle (or click the bar)
    // to set the density that gives that visibility (two significant digits).
    bool FogVisibilityBar( const char* InCaption, double& InOutDensity, const NumberSpec& InDensitySpec, double InStart, double InMaxOpacity, const float* InFogSrgb, double InFalloff );
}
