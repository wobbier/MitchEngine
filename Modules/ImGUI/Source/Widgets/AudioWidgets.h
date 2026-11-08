#pragma once
#include "ScrubField.h"

// Audio widgets (UX-9), built on Graph like the light, camera, post and physics widgets: each is a
// whole inspector row (a caption in the label column, the widget in the value column), returns
// true when it changed a value and is made of real ImGui items, so the inspector turns a gesture
// into one undo step and automation finds the parts by caption: "AudioSource/Level/Value",
// "AudioSource/Keyboard/+7", "AudioSource/Falloff/Max Distance", "AudioSource/Falloff/Linear",
// "AudioSource/Spatial/3D".
namespace MeWidgets
{
    // The sound math the widgets show. Volumes are linear amplitudes (FMOD Channel::setVolume),
    // pitch a playback-rate ratio (Channel::setPitch), and the rolloffs FMOD's, with the min / max
    // distances clamped as AudioCore passes them (Cores/AudioCore.cpp). Pure: no ImGui, unit tested.
    namespace AudioMath
    {
        // Amplitude <-> decibels: 20 log10(gain). Silence (gain 0) is -infinity dB.
        double GainToDb( double InGain );
        double DbToGain( double InDb );

        // Playback rate <-> semitones: 2^(n / 12), so 12 semitones is an octave (twice as fast).
        double SemitonesToRatio( double InSemitones );
        double RatioToSemitones( double InRatio );
        // The nearest whole semitone and the cents (hundredths of a semitone, -50..50) left over.
        void SplitCents( double InSemitones, int& OutWhole, int& OutCents );
        // Whether semitone InSemitone (0 = the clip's own pitch, drawn as a C) is a black key.
        bool IsBlackKey( int InSemitone );

        // The level meter's scale: the IEC 60268-18 peak meter law (wide steps near 0 dB, the
        // quiet end compressed), from -70 dB (the left edge, read as silence) to +6 dB. 0..1.
        constexpr double kMeterFloorDb = -70.0;
        constexpr double kMeterTopDb = 6.0;
        double MeterFromDb( double InDb );
        double MeterToDb( double InT );

        // AudioRolloff, in order: 0 Inverse, 1 Linear, 2 LinearSquare.
        constexpr int kRolloffCount = 3;
        const char* RolloffName( int InMode );
        // The distance gain (0..1) at InDistance, as FMOD attenuates a 3D voice (rolloff scale 1,
        // AudioCore's set3DSettings): full inside the min distance, then
        //   Inverse       min / d, held at min / max past the max distance;
        //   Linear        (max - d) / (max - min), silent at max;
        //   LinearSquare  ((max - d) / (max - min))^2, silent at max.
        // The distances are clamped like AudioCore's set3DMinMaxDistance call: min >= 0.001,
        // max >= min + 0.001.
        double RolloffGain( int InMode, double InDistance, double InMin, double InMax );
        // Where the gain first falls to InGain (0..1): infinity when it never does (Inverse past
        // its max distance holds at min / max).
        double RolloffDistanceFor( int InMode, double InGain, double InMin, double InMax );
    }

    // Volume on a level meter: -inf .. +6 dB on the IEC meter scale with ticks at 0, -6, -12, -24
    // and -48 dB. Drag along the strip (half decibels, Shift for tenths; it snaps to the ticks
    // and ticks as it passes them; the far left is silence). Double-click the value's marker
    // ("Value") for 0 dB. Past the field's maximum (gain 1 = 0 dB) the strip is hatched: headroom
    // the field doesn't allow. InLiveGain >= 0 draws the playing voice's live level (FMOD
    // audibility) as a bar along the strip's bottom; < 0 (stopped, edit mode) shows only the set
    // level. Reports the set level ("-6.0 dB").
    bool LevelMeter( const char* InCaption, double& InOutGain, const NumberSpec& InSpec, double InLiveGain );

    // Pitch on a keyboard one octave each way (-12 .. +12 semitones; 0, the clip's own pitch, is
    // drawn as a C with a dot). Click a key ("+7", "-12", "0") for ratio 2^(n / 12); drag across
    // the keys in whole semitones (Shift for cents), ticking at each one. The marker on the rail
    // above the keys sits at the exact pitch; double-click it ("Value") for 1 (0 st). A pitch past
    // an octave pins the marker to that end. Reports "+7 st, +0 cents, ×1.498".
    bool PitchKeyboard( const char* InCaption, double& InOutRatio, const NumberSpec& InSpec );

    // Gain (dB) against distance (log axis fitting the max distance) for the active rolloff, the
    // other two faint behind it, with draggable "Min Distance" and "Max Distance" handles (two
    // significant digits; min stays at or under max). The names under the plot switch
    // InOutRolloff. Reports where the gain reaches -6 dB: "Inverse: -6 dB at 2 m". At
    // InSpatialBlend 0 the voice is 2D and the graph says it doesn't fade.
    bool RolloffGraph( const char* InCaption, int& InOutRolloff, double& InOutMin, double& InOutMax, const NumberSpec& InMinSpec, const NumberSpec& InMaxSpec, double InSpatialBlend );

    // Spatial blend between labelled ends: "2D" (0) .. "3D" (1). Drag along the track
    // (hundredths) with detents at both ends and the centre; click an end to snap to it.
    // Reports "2D", "3D" or "50% 3D".
    bool SpatialBlendSlider( const char* InCaption, double& InOutBlend, const NumberSpec& InSpec );
}
