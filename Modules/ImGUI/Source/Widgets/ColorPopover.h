#pragma once
#include <imgui.h>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Colour fields (UX-14): a swatch that opens a popover. The swatch shows the colour (alpha over
// a checkerboard), its hex and, for HDR fields, how many stops above 1 it goes ("+2 EV"). Click
// it for the popover: a picker, a hex field, the intensity in EV (HDR fields), a blackbody
// temperature bar, the colours the open scene already uses, and an eyedropper that takes a colour
// from the World View. Right-click (or Ctrl+click) the swatch to copy or paste its hex.
// Colours are stored as they are shown (sRGB 0..1, above 1 for HDR fields).
namespace MeWidgets
{
    // The pure parts, unit tested without ImGui.
    namespace ColorMath
    {
        using Rgb = std::array<float, 3>;

        // Typed colours: "#FF8800", "FF8800", "#F80", "#FF8800CC" (alpha), "rgb(255, 136, 0)",
        // "rgb(100%, 53%, 0%)", "rgba(255, 136, 0, 0.5)" and, when every channel is written
        // with a decimal point and none is above 1, "rgb(1.0, 0.53, 0.0)". OutAlpha is only
        // written when the text gives one. False, with OutError saying why, for anything else.
        bool ParseColor( std::string_view InText, Rgb& OutRgb, float* OutAlpha = nullptr, std::string* OutError = nullptr );

        // "#FF8800" (channels clamped to 0..1); with an alpha below 1 "#FF8800CC".
        std::string FormatHex( const Rgb& InRgb );
        std::string FormatHex( const Rgb& InRgb, float InAlpha );

        // An HDR colour as a displayable colour and the stops its brightest channel sits above 1:
        // (2, 1, 0) is #FF8000 at +1 EV. Colours that fit in 0..1 are themselves at 0 EV.
        struct HdrSplit
        {
            Rgb Base;
            double Ev = 0.0;
        };
        HdrSplit SplitHdr( const Rgb& InColor );
        // The colour with its brightest channel at 2^InEv (InEv > 0). At 0 EV (or below) a colour
        // above 1 comes down to its base and one inside 0..1 stays as it is. Black stays black.
        Rgb WithIntensity( const Rgb& InColor, double InEv );
        Rgb Scaled( const Rgb& InBase, double InEv );

        double EvToMultiplier( double InEv );
        double MultiplierToEv( double InMultiplier );
        // "+2 EV", "+1.5 EV", "0 EV" (a tenth of a stop).
        std::string FormatIntensity( double InEv );
        // "\xC3\x974" (x4), "\xC3\x972.83".
        std::string FormatMultiplier( double InEv );
        // "+2 EV", "2", "2 stops", "4x", "\xC3\x974" -> stops (0 or more).
        bool ParseIntensity( std::string_view InText, double& OutEv, std::string* OutError = nullptr );

        // OKLab (perceptual: equal distances look about equally different) of an sRGB colour,
        // and the distance between two colours there (0.02 is about the smallest step people see).
        std::array<float, 3> ToOklab( const Rgb& InSrgb );
        float PerceptualDistance( const Rgb& InA, const Rgb& InB );

        // The scene palette: InColors (repeats welcome, clamped to 0..1) merged where they are
        // closer than InMinDistance in OKLab, most used first (ties keep first-seen order), at
        // most InMax.
        struct PaletteEntry
        {
            Rgb Color;
            int Uses = 0;
        };
        std::vector<PaletteEntry> BuildPalette( const std::vector<Rgb>& InColors, size_t InMax = 12, float InMinDistance = 0.03f );

        // The popover's temperature bar spans these (log scale).
        constexpr double kKelvinMin = 1000.0;
        constexpr double kKelvinMax = 20000.0;
        // The blackbody temperature a colour sits at (normalised to a brightest channel of 1),
        // when it is within InTolerance of the curve on every channel.
        bool EstimateKelvin( const Rgb& InSrgb, double& OutKelvin, float InTolerance = 0.03f );

        // What a swatch shows and reports: "#FF8800", "#FF880080" (alpha below 1), and for HDR
        // fields the intensity after it: "#FF8800 +2 EV".
        std::string Describe( const float* InColor, int InComponents, bool InHdr );
        // What the swatch's Paste reads: a colour (ParseColor), optionally followed by an
        // intensity ("#FF8800 +2 EV", "rgb(255, 136, 0) 4x"). OutAlpha and OutEv are only
        // written when the text gives them.
        bool ParseSwatchText( std::string_view InText, Rgb& OutRgb, float* OutAlpha, double* OutEv, std::string* OutError = nullptr );
    }

    // The editor's side of the popover (the scene palette and the World View eyedropper). The
    // swatch works without them: no palette, no eyedropper button.
    struct ColorPopoverHooks
    {
        // The colours the open scene uses (with repeats), asked once each time a popover opens.
        std::function<std::vector<ColorMath::Rgb>()> SceneColors;
        // Arms the eyedropper for the swatch InId: the next click in the World View samples it.
        std::function<void( ImGuiID InId )> ArmEyedropper;
        std::function<bool( ImGuiID InId )> IsEyedropperArmed;
        // A sampled colour for the swatch InId, once (true when one has landed).
        std::function<bool( ImGuiID InId, ColorMath::Rgb& OutRgb )> TakeEyedropped;
    };
    void SetColorPopoverHooks( ColorPopoverHooks InHooks );

    // The swatch at the cursor, CalcItemWidth() wide, for InOutColor's InComponents (3: rgb, 4:
    // rgba) channels. InHdr allows channels above 1 (the popover then edits the intensity).
    // InCaption names the field in the popover's header. Returns true when the colour changed
    // this frame (picker drags, hex entry, temperature, palette, eyedropper, paste). The swatch
    // is one item; the popover's items sit in a section named "Color": "Color/Hex",
    // "Color/Intensity", "Color/Kelvin", "Color/Palette 1", "Color/Eyedropper", "Color/Original".
    bool ColorSwatchPopover( const char* InId, float* InOutColor, int InComponents, bool InHdr, const char* InCaption = nullptr );
}
