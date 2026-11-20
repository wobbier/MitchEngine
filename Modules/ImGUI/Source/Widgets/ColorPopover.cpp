#include "ColorPopover.h"
#include "Color.h"
#include "Graph.h"
#include "ItemLog.h"
#include "Juice.h"
#include "ScrubField.h"
#include "Units.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace MeWidgets
{
    // ---- Pure colour math -----------------------------------------------------------------

    namespace ColorMath
    {
        namespace
        {
            std::string_view Trim( std::string_view InText )
            {
                while( !InText.empty() && std::isspace( static_cast<unsigned char>( InText.front() ) ) ) InText.remove_prefix( 1 );
                while( !InText.empty() && std::isspace( static_cast<unsigned char>( InText.back() ) ) ) InText.remove_suffix( 1 );
                return InText;
            }

            int HexDigit( char InChar )
            {
                if( InChar >= '0' && InChar <= '9' ) return InChar - '0';
                if( InChar >= 'a' && InChar <= 'f' ) return InChar - 'a' + 10;
                if( InChar >= 'A' && InChar <= 'F' ) return InChar - 'A' + 10;
                return -1;
            }

            bool StartsWithNoCase( std::string_view InText, std::string_view InPrefix )
            {
                return InText.size() >= InPrefix.size() && std::equal( InPrefix.begin(), InPrefix.end(), InText.begin(), []( char a, char b ) {
                    return std::tolower( static_cast<unsigned char>( a ) ) == std::tolower( static_cast<unsigned char>( b ) );
                } );
            }

            float Byte( float InChannel )
            {
                return std::round( std::clamp( std::isfinite( InChannel ) ? InChannel : 0.f, 0.f, 1.f ) * 255.f );
            }

            bool Fail( std::string* OutError, const std::string& InMessage )
            {
                if( OutError ) *OutError = InMessage;
                return false;
            }

            constexpr const char* kForms = "use #RRGGBB, RRGGBB, #RGB or rgb(255, 136, 0)";

            bool ParseHexDigits( std::string_view InDigits, Rgb& OutRgb, float* OutAlpha, std::string* OutError )
            {
                const size_t count = InDigits.size();
                if( count != 3 && count != 4 && count != 6 && count != 8 )
                {
                    return Fail( OutError, std::string( "a hex colour has 3, 4, 6 or 8 digits: " ) + kForms );
                }
                int values[8] = {};
                for( size_t i = 0; i < count; ++i )
                {
                    values[i] = HexDigit( InDigits[i] );
                    if( values[i] < 0 )
                    {
                        return Fail( OutError, std::string( "'" ) + InDigits[i] + "' isn't a hex digit: " + kForms );
                    }
                }
                const bool shortForm = count <= 4;
                const int channels = shortForm ? static_cast<int>( count ) : static_cast<int>( count / 2 );
                float out[4] = { 0.f, 0.f, 0.f, 1.f };
                for( int c = 0; c < channels; ++c )
                {
                    const int byte = shortForm ? values[c] * 17 : values[c * 2] * 16 + values[c * 2 + 1];
                    out[c] = static_cast<float>( byte ) / 255.f;
                }
                OutRgb = { out[0], out[1], out[2] };
                if( channels == 4 && OutAlpha )
                {
                    *OutAlpha = out[3];
                }
                return true;
            }

            bool ParseFunction( std::string_view InText, Rgb& OutRgb, float* OutAlpha, std::string* OutError )
            {
                const size_t open = InText.find( '(' );
                const size_t close = InText.rfind( ')' );
                if( open == std::string_view::npos || close == std::string_view::npos || close < open || !Trim( InText.substr( close + 1 ) ).empty() )
                {
                    return Fail( OutError, std::string( "unbalanced parentheses: " ) + kForms );
                }
                std::string inside( InText.substr( open + 1, close - open - 1 ) );
                std::replace( inside.begin(), inside.end(), ',', ' ' );
                std::replace( inside.begin(), inside.end(), '/', ' ' );
                struct Part
                {
                    double Value;
                    bool Percent;
                    bool Decimal;
                };
                std::vector<Part> parts;
                const char* cursor = inside.c_str();
                while( true )
                {
                    while( *cursor && std::isspace( static_cast<unsigned char>( *cursor ) ) ) ++cursor;
                    if( !*cursor ) break;
                    char* end = nullptr;
                    const double value = std::strtod( cursor, &end );
                    if( end == cursor || !std::isfinite( value ) )
                    {
                        return Fail( OutError, "'" + std::string( cursor ) + "' isn't a number: " + kForms );
                    }
                    const bool decimal = std::string_view( cursor, static_cast<size_t>( end - cursor ) ).find_first_of( ".eE" ) != std::string_view::npos;
                    cursor = end;
                    const bool percent = *cursor == '%';
                    if( percent ) ++cursor;
                    parts.push_back( { value, percent, decimal } );
                }
                if( parts.size() != 3 && parts.size() != 4 )
                {
                    return Fail( OutError, "rgb() takes three channels (and an optional alpha): " + std::string( kForms ) );
                }
                // CSS reads channels as 0..255. Written as decimals that all stay within 1 they
                // are 0..1, the way the engine stores them.
                bool unit = true;
                for( size_t i = 0; i < 3; ++i )
                {
                    unit = unit && !parts[i].Percent && parts[i].Decimal && parts[i].Value <= 1.0;
                }
                float channels[3];
                for( size_t i = 0; i < 3; ++i )
                {
                    const double value = parts[i].Percent ? parts[i].Value / 100.0 : unit ? parts[i].Value : parts[i].Value / 255.0;
                    if( value < 0.0 || value > 1.0 + 1e-9 )
                    {
                        return Fail( OutError, "channels go from 0 to 255 (or 0% to 100%)" );
                    }
                    channels[i] = static_cast<float>( std::clamp( value, 0.0, 1.0 ) );
                }
                OutRgb = { channels[0], channels[1], channels[2] };
                if( parts.size() == 4 )
                {
                    const double alpha = parts[3].Percent ? parts[3].Value / 100.0 : parts[3].Value;
                    if( alpha < 0.0 || alpha > 1.0 + 1e-9 )
                    {
                        return Fail( OutError, "alpha goes from 0 to 1 (or 0% to 100%)" );
                    }
                    if( OutAlpha ) *OutAlpha = static_cast<float>( std::clamp( alpha, 0.0, 1.0 ) );
                }
                return true;
            }
        }

        bool ParseColor( std::string_view InText, Rgb& OutRgb, float* OutAlpha, std::string* OutError )
        {
            const std::string_view text = Trim( InText );
            if( text.empty() )
            {
                return Fail( OutError, std::string( "type a colour: " ) + kForms );
            }
            if( StartsWithNoCase( text, "rgb" ) )
            {
                return ParseFunction( text, OutRgb, OutAlpha, OutError );
            }
            std::string_view digits = text;
            if( digits.front() == '#' )
            {
                digits.remove_prefix( 1 );
            }
            else if( StartsWithNoCase( digits, "0x" ) )
            {
                digits.remove_prefix( 2 );
            }
            return ParseHexDigits( Trim( digits ), OutRgb, OutAlpha, OutError );
        }

        std::string FormatHex( const Rgb& InRgb )
        {
            char text[16];
            std::snprintf( text, sizeof( text ), "#%02X%02X%02X", static_cast<int>( Byte( InRgb[0] ) ), static_cast<int>( Byte( InRgb[1] ) ), static_cast<int>( Byte( InRgb[2] ) ) );
            return text;
        }

        std::string FormatHex( const Rgb& InRgb, float InAlpha )
        {
            const int alpha = static_cast<int>( Byte( InAlpha ) );
            if( alpha >= 255 )
            {
                return FormatHex( InRgb );
            }
            char text[8];
            std::snprintf( text, sizeof( text ), "%02X", alpha );
            return FormatHex( InRgb ) + text;
        }

        HdrSplit SplitHdr( const Rgb& InColor )
        {
            HdrSplit split;
            split.Base = InColor;
            const float peak = std::max( { InColor[0], InColor[1], InColor[2] } );
            if( std::isfinite( peak ) && peak > 1.f )
            {
                split.Ev = std::log2( static_cast<double>( peak ) );
                for( float& channel : split.Base ) channel /= peak;
            }
            return split;
        }

        Rgb Scaled( const Rgb& InBase, double InEv )
        {
            const float multiplier = static_cast<float>( EvToMultiplier( InEv ) );
            return { InBase[0] * multiplier, InBase[1] * multiplier, InBase[2] * multiplier };
        }

        Rgb WithIntensity( const Rgb& InColor, double InEv )
        {
            const float peak = std::max( { InColor[0], InColor[1], InColor[2] } );
            if( !std::isfinite( peak ) || peak <= 0.f )
            {
                return InColor;
            }
            if( !( InEv > 0.0 ) )
            {
                return peak > 1.f ? Rgb { InColor[0] / peak, InColor[1] / peak, InColor[2] / peak } : InColor;
            }
            const Rgb hue = { InColor[0] / peak, InColor[1] / peak, InColor[2] / peak };
            return Scaled( hue, InEv );
        }

        double EvToMultiplier( double InEv )
        {
            return std::exp2( InEv );
        }

        double MultiplierToEv( double InMultiplier )
        {
            return InMultiplier > 0.0 ? std::log2( InMultiplier ) : std::numeric_limits<double>::quiet_NaN();
        }

        std::string FormatIntensity( double InEv )
        {
            const double tenth = std::round( InEv * 10.0 ) / 10.0;
            if( std::abs( tenth ) < 0.05 )
            {
                return "0 EV";
            }
            return ( tenth > 0.0 ? "+" : "" ) + FormatNumber( tenth, -1 ) + " EV";
        }

        std::string FormatMultiplier( double InEv )
        {
            const double multiplier = EvToMultiplier( InEv );
            const double power = std::pow( 10.0, std::floor( std::log10( std::max( multiplier, 1e-9 ) ) ) - 2.0 );
            return "\xC3\x97" + FormatNumber( std::round( multiplier / power ) * power, -1 );
        }

        bool ParseIntensity( std::string_view InText, double& OutEv, std::string* OutError )
        {
            NumberSpec spec;
            spec.FieldUnit = Unit::EV;
            spec.AlternateUnit = Unit::Multiplier;
            spec.AlternateToStored = &MultiplierToEv;
            // "x4" and the multiplication sign in front read as "4x".
            std::string text( Trim( InText ) );
            for( const char* prefix : { "\xC3\x97", "x", "X" } )
            {
                const size_t length = std::char_traits<char>::length( prefix );
                if( text.size() > length && text.compare( 0, length, prefix ) == 0 && std::isdigit( static_cast<unsigned char>( text[length] ) ) )
                {
                    text = text.substr( length ) + "x";
                    break;
                }
            }
            double ev = 0.0;
            if( !ParseTyped( spec, text, ev, nullptr ) || !std::isfinite( ev ) )
            {
                return Fail( OutError, "type stops (\"+2 EV\") or a multiplier (\"4x\")" );
            }
            OutEv = std::max( ev, 0.0 );
            return true;
        }

        std::array<float, 3> ToOklab( const Rgb& InSrgb )
        {
            auto linear = []( float InC ) {
                const float c = std::clamp( std::isfinite( InC ) ? InC : 0.f, 0.f, 1.f );
                return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f );
            };
            const float r = linear( InSrgb[0] ), g = linear( InSrgb[1] ), b = linear( InSrgb[2] );
            const float l = std::cbrt( 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b );
            const float m = std::cbrt( 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b );
            const float s = std::cbrt( 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b );
            return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
        }

        float PerceptualDistance( const Rgb& InA, const Rgb& InB )
        {
            const auto a = ToOklab( InA );
            const auto b = ToOklab( InB );
            return std::sqrt( ( a[0] - b[0] ) * ( a[0] - b[0] ) + ( a[1] - b[1] ) * ( a[1] - b[1] ) + ( a[2] - b[2] ) * ( a[2] - b[2] ) );
        }

        std::vector<PaletteEntry> BuildPalette( const std::vector<Rgb>& InColors, size_t InMax, float InMinDistance )
        {
            struct Cluster
            {
                PaletteEntry Entry;
                std::array<float, 3> Lab;
                size_t First;
            };
            std::vector<Cluster> clusters;
            for( size_t i = 0; i < InColors.size(); ++i )
            {
                Rgb color = InColors[i];
                bool finite = true;
                for( float& channel : color )
                {
                    finite = finite && std::isfinite( channel );
                    channel = std::clamp( channel, 0.f, 1.f );
                }
                if( !finite )
                {
                    continue;
                }
                const auto lab = ToOklab( color );
                Cluster* nearest = nullptr;
                float best = InMinDistance;
                for( Cluster& cluster : clusters )
                {
                    const float d = std::sqrt( ( lab[0] - cluster.Lab[0] ) * ( lab[0] - cluster.Lab[0] ) + ( lab[1] - cluster.Lab[1] ) * ( lab[1] - cluster.Lab[1] )
                        + ( lab[2] - cluster.Lab[2] ) * ( lab[2] - cluster.Lab[2] ) );
                    if( d < best )
                    {
                        best = d;
                        nearest = &cluster;
                    }
                }
                if( nearest )
                {
                    ++nearest->Entry.Uses;
                }
                else
                {
                    clusters.push_back( { { color, 1 }, lab, i } );
                }
            }
            std::stable_sort( clusters.begin(), clusters.end(), []( const Cluster& a, const Cluster& b ) { return a.Entry.Uses > b.Entry.Uses; } );
            std::vector<PaletteEntry> palette;
            for( size_t i = 0; i < clusters.size() && palette.size() < InMax; ++i )
            {
                palette.push_back( clusters[i].Entry );
            }
            return palette;
        }

        bool EstimateKelvin( const Rgb& InSrgb, double& OutKelvin, float InTolerance )
        {
            return ColorTemperature::FromSrgbInRange( InSrgb, kKelvinMin, kKelvinMax, OutKelvin, InTolerance );
        }

        std::string Describe( const float* InColor, int InComponents, bool InHdr )
        {
            const Rgb color = { InColor[0], InColor[1], InColor[2] };
            const HdrSplit split = InHdr ? SplitHdr( color ) : HdrSplit { color, 0.0 };
            std::string text = InComponents >= 4 ? FormatHex( split.Base, InColor[3] ) : FormatHex( split.Base );
            if( InHdr )
            {
                text += " " + FormatIntensity( split.Ev );
            }
            return text;
        }

        bool ParseSwatchText( std::string_view InText, Rgb& OutRgb, float* OutAlpha, double* OutEv, std::string* OutError )
        {
            std::string_view text = Trim( InText );
            if( ParseColor( text, OutRgb, OutAlpha, OutError ) )
            {
                return true;
            }
            size_t split = std::string_view::npos;
            if( StartsWithNoCase( text, "rgb" ) )
            {
                const size_t close = text.find( ')' );
                split = close == std::string_view::npos ? close : close + 1;
            }
            else
            {
                split = text.find_first_of( " \t" );
            }
            if( split == std::string_view::npos || split >= text.size() )
            {
                return false;
            }
            Rgb rgb;
            float alpha = OutAlpha ? *OutAlpha : 1.f;
            double ev = 0.0;
            if( !ParseColor( text.substr( 0, split ), rgb, &alpha, OutError ) || !ParseIntensity( text.substr( split ), ev, OutError ) )
            {
                return false;
            }
            OutRgb = rgb;
            if( OutAlpha ) *OutAlpha = alpha;
            if( OutEv ) *OutEv = ev;
            return true;
        }
    }

    // ---- The swatch and its popover --------------------------------------------------------

    namespace
    {
        using ColorMath::Rgb;

        ColorPopoverHooks& Hooks()
        {
            static ColorPopoverHooks hooks;
            return hooks;
        }

        struct PopoverState
        {
            int LastFrame = -1;
            // What the popover edits: a displayable base and the stops above it (HDR), kept
            // while it is open so a picker drag on an HDR colour doesn't renormalise under the
            // pointer. Re-read whenever the field changes from elsewhere.
            Rgb Base { 0.f, 0.f, 0.f };
            double Ev = 0.0;
            bool HasBase = false;
            std::string Hex;
            bool HexError = false;
            std::string HexMessage;
            std::vector<ColorMath::PaletteEntry> Palette;
            float Original[4] = { 0.f, 0.f, 0.f, 1.f };
            float Height = 0.f;
        };

        std::unordered_map<ImGuiID, PopoverState>& States()
        {
            static std::unordered_map<ImGuiID, PopoverState> states;
            return states;
        }

        float Thin()
        {
            return std::max( 1.f, std::round( ImGui::GetFontSize() / 14.f ) );
        }

        ImU32 U32( const ImVec4& InColor )
        {
            return ImGui::GetColorU32( InColor );
        }

        ImU32 Display( const Rgb& InRgb, float InAlpha = 1.f )
        {
            return ImGui::GetColorU32( ImVec4( std::clamp( InRgb[0], 0.f, 1.f ), std::clamp( InRgb[1], 0.f, 1.f ), std::clamp( InRgb[2], 0.f, 1.f ), std::clamp( InAlpha, 0.f, 1.f ) ) );
        }

        bool Near( const Rgb& InA, const float* InB )
        {
            for( int i = 0; i < 3; ++i )
            {
                if( std::abs( InA[i] - InB[i] ) > 1e-5f * std::max( 1.f, std::abs( InB[i] ) ) )
                {
                    return false;
                }
            }
            return true;
        }

        // A colour chip: opaque on the left, the alpha over a checkerboard on the right when it
        // isn't opaque, outlined so dark and light colours both read on either theme.
        void Chip( ImDrawList* InDrawList, ImVec2 InMin, ImVec2 InMax, const Rgb& InRgb, float InAlpha, float InRounding )
        {
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            if( InAlpha < 1.f )
            {
                const float middle = std::round( ( InMin.x + InMax.x ) * 0.5f );
                InDrawList->AddRectFilled( InMin, ImVec2( middle, InMax.y ), Display( InRgb ), InRounding, ImDrawFlags_RoundCornersLeft );
                const float step = std::max( std::round( ( InMax.y - InMin.y ) * 0.5f ), 2.f );
                ImGui::RenderColorRectWithAlphaCheckerboard( InDrawList, ImVec2( middle, InMin.y ), InMax, Display( InRgb, InAlpha ), step, ImVec2( 0.f, 0.f ), InRounding, ImDrawFlags_RoundCornersRight );
            }
            else
            {
                InDrawList->AddRectFilled( InMin, InMax, Display( InRgb ), InRounding );
            }
            InDrawList->AddRect( InMin, InMax, U32( ImGuiTheme::WithAlpha( palette.Separator, 0.9f ) ), InRounding, 0, Thin() );
        }

        std::string HexOf( const PopoverState& InState, const float* InColor, int InComponents )
        {
            return InComponents >= 4 ? ColorMath::FormatHex( InState.Base, InColor[3] ) : ColorMath::FormatHex( InState.Base );
        }

        // A new hue for an HDR field that is above white: at full brightness, so the kept stops stay the shown ones.
        Rgb HueForIntensity( const Rgb& InColor, bool InHdr, double InEv )
        {
            const float peak = std::max( { InColor[0], InColor[1], InColor[2] } );
            if( !InHdr || !( InEv > 0.0 ) || !std::isfinite( peak ) || peak <= 0.f )
            {
                return InColor;
            }
            return { InColor[0] / peak, InColor[1] / peak, InColor[2] / peak };
        }

        // Writes the popover's base (and intensity) back to the field.
        void WriteBack( const PopoverState& InState, float* InOutColor, bool InHdr )
        {
            const Rgb value = InHdr ? ColorMath::Scaled( InState.Base, InState.Ev ) : InState.Base;
            std::copy( value.begin(), value.end(), InOutColor );
        }

        // Reads the field into the popover's base unless it already holds it.
        void Sync( PopoverState& InState, const float* InColor, bool InHdr )
        {
            const Rgb shown = InHdr ? ColorMath::Scaled( InState.Base, InState.Ev ) : InState.Base;
            if( InState.HasBase && Near( shown, InColor ) )
            {
                return;
            }
            const Rgb color = { InColor[0], InColor[1], InColor[2] };
            const ColorMath::HdrSplit split = InHdr ? ColorMath::SplitHdr( color ) : ColorMath::HdrSplit { color, 0.0 };
            InState.Base = split.Base;
            InState.Ev = split.Ev;
            InState.HasBase = true;
            // The field changed from elsewhere: a refused hex no longer describes anything.
            InState.HexError = false;
        }

        const NumberSpec& IntensitySpec()
        {
            static const NumberSpec spec = [] {
                NumberSpec s;
                s.FieldUnit = Unit::EV;
                s.HasRange = true;
                s.Min = 0.0;
                s.Max = 20.0;
                s.HasSoftRange = true;
                s.SoftMin = 0.0;
                s.SoftMax = 10.0;
                s.HasDefault = true;
                s.Default = 0.0;
                s.Decimals = 1;
                s.AlternateUnit = Unit::Multiplier;
                s.AlternateToStored = &ColorMath::MultiplierToEv;
                return s;
            }();
            return spec;
        }

        // Text with a soft halo, readable on any colour under it.
        void HaloText( ImDrawList* InDrawList, ImVec2 InPos, ImU32 InColor, ImU32 InHalo, const char* InText )
        {
            const float o = Thin();
            InDrawList->AddText( ImVec2( InPos.x - o, InPos.y ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x + o, InPos.y ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y - o ), InHalo, InText );
            InDrawList->AddText( ImVec2( InPos.x, InPos.y + o ), InHalo, InText );
            InDrawList->AddText( InPos, InColor, InText );
        }

        // The popover's temperature bar: blackbody colours over kKelvinMin..kKelvinMax (log).
        bool KelvinStrip( PopoverState& InState, float InWidth )
        {
            using namespace ColorTemperature;
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            const float height = ImGui::GetFrameHeight();
            Graph::Frame frame;
            frame.XLabels = false;
            frame.Background = false;
            frame.Inset = 0.f;
            const Graph::Axis axis { ColorMath::kKelvinMin, ColorMath::kKelvinMax, true, Unit::Kelvin };
            const bool visible = Graph::Begin( "Kelvin", ImVec2( InWidth, height ), axis, Graph::Axis(), frame );
            const Graph::Mapping map = Graph::Current();
            const ImGuiID canvasId = Graph::CanvasId();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float rounding = ImGui::GetStyle().FrameRounding;
            if( visible )
            {
                constexpr int kSegments = 48;
                for( int i = 0; i < kSegments; ++i )
                {
                    const double k0 = axis.FromNormalized( static_cast<double>( i ) / kSegments );
                    const double k1 = axis.FromNormalized( static_cast<double>( i + 1 ) / kSegments );
                    const ImU32 c0 = Display( ToSrgb( k0 ) );
                    const ImU32 c1 = Display( ToSrgb( k1 ) );
                    drawList->AddRectFilledMultiColor( ImVec2( std::floor( map.XToScreen( k0 ) ), map.Min.y ), ImVec2( std::ceil( map.XToScreen( k1 ) ), map.Max.y ), c0, c1, c1, c0 );
                }
                drawList->AddRect( map.Min, map.Max, U32( palette.Separator ), rounding, 0, Thin() );
            }
            const Graph::CanvasInput input = Graph::End();
            bool changed = false;
            double kelvin = 0.0;
            bool onCurve = ColorMath::EstimateKelvin( InState.Base, kelvin );
            if( input.Pressed || ( input.Held && GImGui->IO.MouseDelta.x != 0.f ) )
            {
                const double next = std::clamp( std::round( input.X / 10.0 ) * 10.0, ColorMath::kKelvinMin, ColorMath::kKelvinMax );
                if( !onCurve || next != kelvin || input.Pressed )
                {
                    InState.Base = ToSrgb( next );
                    kelvin = next;
                    onCurve = true;
                    changed = true;
                    ImGui::MarkItemEdited( canvasId );
                }
            }
            if( input.Hovered && !input.Held )
            {
                ImGui::SetTooltip( "Click or drag for the colour of a blackbody at that temperature\n(%s..%s, log scale): candle flame at the left, blue sky at the right.\nThe marker shows the colour's temperature when it has one.",
                    FormatValue( Unit::Kelvin, ColorMath::kKelvinMin ).c_str(), FormatValue( Unit::Kelvin, ColorMath::kKelvinMax ).c_str() );
            }
            const double shown = std::round( kelvin / 10.0 ) * 10.0;
            if( visible && onCurve )
            {
                const float x = std::round( map.XToScreen( kelvin ) );
                const ImU32 dark = U32( ImGuiTheme::Gray( 0.06f ) );
                const ImU32 light = U32( ImGuiTheme::Gray( 1.f, 0.85f ) );
                drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, map.Max.y ), light, Thin() * 4.f );
                drawList->AddLine( ImVec2( x, map.Min.y ), ImVec2( x, map.Max.y ), dark, Thin() * 2.f );
                const std::string reading = FormatValue( Unit::Kelvin, shown );
                const ImVec2 size = ImGui::CalcTextSize( reading.c_str() );
                const float gap = ImGuiTheme::Em( 0.3f );
                float left = x + gap;
                if( left + size.x > map.Max.x - gap )
                {
                    left = x - gap - size.x;
                }
                left = std::max( left, map.Min.x + gap );
                drawList->PushClipRect( map.Min, map.Max, true );
                HaloText( drawList, ImVec2( left, map.Min.y + ( map.Max.y - map.Min.y - size.y ) * 0.5f ), dark, light, reading.c_str() );
                drawList->PopClipRect();
            }
            ItemLog::SetItemValue( canvasId, onCurve ? FormatValue( Unit::Kelvin, shown ) : std::string( "off curve" ) );
            return changed;
        }

        // A small pipette, drawn in the text colour.
        void PipetteGlyph( ImDrawList* InDrawList, ImVec2 InCenter, float InSize, ImU32 InColor )
        {
            const float h = InSize * 0.5f;
            const ImVec2 tip( InCenter.x - h, InCenter.y + h );
            const ImVec2 neck( InCenter.x + h * 0.25f, InCenter.y - h * 0.25f );
            InDrawList->AddLine( tip, neck, InColor, Thin() * 1.5f );
            InDrawList->AddCircleFilled( ImVec2( InCenter.x + h * 0.5f, InCenter.y - h * 0.5f ), h * 0.42f, InColor );
            InDrawList->AddLine( ImVec2( InCenter.x - h * 0.05f, InCenter.y - h * 0.65f ), ImVec2( InCenter.x + h * 0.65f, InCenter.y + h * 0.05f ), InColor, Thin() * 2.f );
        }

        bool DrawPopoverBody( ImGuiID InSwatchId, PopoverState& InState, float* InOutColor, int InComponents, bool InHdr, const char* InCaption )
        {
            ImGuiContext& g = *GImGui;
            const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float width = std::max( ImGui::GetContentRegionAvail().x, ImGuiTheme::Em( 4.f ) );
            const float rounding = ImGui::GetStyle().FrameRounding;
            const float gap = ImGuiTheme::InnerGap();
            ColorPopoverHooks& hooks = Hooks();
            bool changed = false;

            // Header: the field's name, then what it was when the popover opened (click to go
            // back) beside what it is now.
            {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const float rowHeight = ImGui::GetFrameHeight();
                const ImRect header( start, ImVec2( start.x + width, start.y + rowHeight ) );
                ImGui::ItemSize( header );
                ImGui::ItemAdd( header, ImGui::GetID( "##header" ) );
                ItemLog::MarkSection( "Color" );
                const char* caption = InCaption && *InCaption ? InCaption : "Color";
                drawList->AddText( ImVec2( start.x, start.y + ( rowHeight - ImGui::GetFontSize() ) * 0.5f ), U32( palette.Text ), caption );
                const float chip = std::round( rowHeight * 1.6f );
                const float chipTop = start.y + std::round( rowHeight * 0.15f );
                const float chipBottom = start.y + rowHeight - std::round( rowHeight * 0.15f );
                const ImVec2 nowMin( header.Max.x - chip, chipTop );
                const ImVec2 wasMin( nowMin.x - chip, chipTop );
                const Rgb was = { InState.Original[0], InState.Original[1], InState.Original[2] };
                const Rgb wasBase = InHdr ? ColorMath::SplitHdr( was ).Base : was;
                const float alpha = InComponents >= 4 ? InOutColor[3] : 1.f;
                const float wasAlpha = InComponents >= 4 ? InState.Original[3] : 1.f;
                ImGui::SetCursorScreenPos( wasMin );
                const bool restore = ImGui::InvisibleButton( "Original", ImVec2( chip, chipBottom - chipTop ) );
                const bool wasHovered = ImGui::IsItemHovered();
                ItemLog::SetItemValue( ImGui::GetItemID(), ColorMath::Describe( InState.Original, InComponents, InHdr ) );
                if( wasHovered )
                {
                    ImGui::SetTooltip( "When the popover opened: %s\nClick to go back to it", ColorMath::Describe( InState.Original, InComponents, InHdr ).c_str() );
                }
                drawList->AddRectFilled( wasMin, ImVec2( nowMin.x + chip, chipBottom ), U32( palette.Separator ), rounding );
                Chip( drawList, wasMin, ImVec2( nowMin.x, chipBottom ), wasBase, wasAlpha, rounding );
                Chip( drawList, nowMin, ImVec2( nowMin.x + chip, chipBottom ), InState.Base, alpha, rounding );
                if( wasHovered )
                {
                    drawList->AddRect( wasMin, ImVec2( nowMin.x, chipBottom ), U32( palette.Text ), rounding, 0, Thin() * 1.5f );
                }
                if( restore )
                {
                    std::copy( InState.Original, InState.Original + InComponents, InOutColor );
                    InState.HasBase = false;
                    Sync( InState, InOutColor, InHdr );
                    ImGui::MarkItemEdited( ImGui::GetItemID() );
                    Juice::Play( Juice::Cue::Reset );
                    changed = true;
                }
                ImGui::SetCursorScreenPos( ImVec2( start.x, header.Max.y + g.Style.ItemSpacing.y ) );
            }

            // The picker, on the displayable base (HDR intensity is its own row).
            {
                float pick[4] = { InState.Base[0], InState.Base[1], InState.Base[2], InComponents >= 4 ? InOutColor[3] : 1.f };
                ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSidePreview
                    | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoTooltip;
                flags |= InComponents >= 4 ? ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha;
                ImGui::SetNextItemWidth( width );
                if( ImGui::ColorPicker4( "##picker", pick, flags ) )
                {
                    for( int i = 0; i < 3; ++i )
                    {
                        InState.Base[i] = std::clamp( pick[i], 0.f, 1.f );
                    }
                    if( InComponents >= 4 )
                    {
                        InOutColor[3] = std::clamp( pick[3], 0.f, 1.f );
                    }
                    WriteBack( InState, InOutColor, InHdr );
                    changed = true;
                }
            }

            // Hex: typed, applied on Enter (or when the field loses focus).
            {
                HavanaUtils::Label( "Hex" );
                const ImGuiID hexId = ImGui::GetID( "##hex" );
                if( g.ActiveId != hexId && !InState.HexError )
                {
                    InState.Hex = HexOf( InState, InOutColor, InComponents );
                }
                const float shake = Juice::ShakeOffset( hexId );
                if( shake != 0.f )
                {
                    ImGui::SetCursorScreenPos( ImVec2( ImGui::GetCursorScreenPos().x + shake, ImGui::GetCursorScreenPos().y ) );
                }
                if( InState.HexError )
                {
                    ImGui::PushStyleColor( ImGuiCol_Border, ImGuiTheme::ErrorText() );
                    ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, Thin() );
                }
                const bool entered = ImGui::InputText( "##hex", &InState.Hex, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
                if( InState.HexError )
                {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                if( ImGui::IsItemActivated() )
                {
                    InState.HexError = false;
                }
                if( entered || ImGui::IsItemDeactivatedAfterEdit() )
                {
                    Rgb rgb;
                    float alpha = InComponents >= 4 ? InOutColor[3] : 1.f;
                    std::string error;
                    if( ColorMath::ParseColor( InState.Hex, rgb, &alpha, &error ) )
                    {
                        InState.Base = HueForIntensity( rgb, InHdr, InState.Ev );
                        if( InComponents >= 4 )
                        {
                            InOutColor[3] = alpha;
                        }
                        WriteBack( InState, InOutColor, InHdr );
                        InState.HexError = false;
                        InState.Hex = HexOf( InState, InOutColor, InComponents );
                        ImGui::MarkItemEdited( hexId );
                        Juice::Play( Juice::Cue::Commit );
                        changed = true;
                    }
                    else
                    {
                        InState.HexError = true;
                        InState.HexMessage = "Can't read '" + InState.Hex + "': " + error;
                        Juice::Shake( hexId ); // Shake plays the Refuse cue.
                    }
                }
                ItemLog::SetItemValue( hexId, InState.Hex );
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) && !ImGui::IsItemActive() )
                {
                    if( InState.HexError )
                    {
                        ImGui::SetTooltip( "%s", InState.HexMessage.c_str() );
                    }
                    else
                    {
                        ImGui::SetTooltip( "Type #RRGGBB, RRGGBB, #RGB%s or rgb(255, 136, 0); Enter applies", InComponents >= 4 ? ", #RRGGBBAA" : "" );
                    }
                }
            }

            // Intensity (HDR fields): the stops the brightest channel goes above 1.
            if( InHdr )
            {
                double ev = InState.Ev;
                if( UnitRow( "Intensity", ev, IntensitySpec() ) )
                {
                    // The base at full brightness, so the shown stops are the colour's own.
                    const float peak = std::max( { InState.Base[0], InState.Base[1], InState.Base[2] } );
                    if( peak > 0.f && ev > 0.0 )
                    {
                        for( float& channel : InState.Base ) channel /= peak;
                    }
                    InState.Ev = std::max( ev, 0.0 );
                    WriteBack( InState, InOutColor, InHdr );
                    changed = true;
                }
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) && !ImGui::IsItemActive() )
                {
                    ImGui::SetTooltip( "How far the brightest channel goes above 1, in stops: %s is %s.\nType \"+2 EV\" or a multiplier (\"4x\"); drag to scrub.",
                        ColorMath::FormatIntensity( InState.Ev ).c_str(), ColorMath::FormatMultiplier( InState.Ev ).c_str() );
                }
            }

            // Temperature.
            ImGui::Dummy( ImVec2( 0.f, std::round( gap * 0.5f ) ) );
            if( KelvinStrip( InState, width ) )
            {
                WriteBack( InState, InOutColor, InHdr );
                changed = true;
            }

            // The scene's palette.
            if( hooks.SceneColors )
            {
                ImGui::Dummy( ImVec2( 0.f, std::round( gap * 0.5f ) ) );
                if( InState.Palette.empty() )
                {
                    ImGui::TextDisabled( "No colours in the scene yet" );
                }
                else
                {
                    ImGui::TextDisabled( "In this scene" );
                    constexpr int kPerRow = 6;
                    const float cell = std::floor( ( width - gap * ( kPerRow - 1 ) ) / kPerRow );
                    const float cellHeight = std::round( ImGui::GetFrameHeight() * 0.9f );
                    for( size_t i = 0; i < InState.Palette.size(); ++i )
                    {
                        const ColorMath::PaletteEntry& entry = InState.Palette[i];
                        if( i % kPerRow != 0 )
                        {
                            ImGui::SameLine( 0.f, gap );
                        }
                        char label[24];
                        std::snprintf( label, sizeof( label ), "Palette %d", static_cast<int>( i + 1 ) );
                        const ImVec2 min = ImGui::GetCursorScreenPos();
                        const bool clicked = ImGui::InvisibleButton( label, ImVec2( cell, cellHeight ) );
                        const ImGuiID chipId = ImGui::GetItemID();
                        const bool hovered = ImGui::IsItemHovered();
                        const ImVec2 max( min.x + cell, min.y + cellHeight );
                        const std::string hex = ColorMath::FormatHex( entry.Color );
                        ItemLog::SetItemValue( chipId, hex );
                        const bool current = ColorMath::PerceptualDistance( entry.Color, InState.Base ) < 0.01f;
                        Chip( drawList, min, max, entry.Color, 1.f, rounding );
                        const float flash = Juice::FlashAmount( chipId );
                        if( current || hovered || flash > 0.f )
                        {
                            const float o = Thin();
                            const ImVec4 ring = current ? palette.AccentText : ImGuiTheme::WithAlpha( palette.Text, hovered ? 1.f : flash );
                            drawList->AddRect( ImVec2( min.x - o, min.y - o ), ImVec2( max.x + o, max.y + o ), U32( ring ), rounding + o, 0, o * 2.f );
                        }
                        if( hovered )
                        {
                            ImGui::SetTooltip( "%s, used by %d colour field%s in this scene", hex.c_str(), entry.Uses, entry.Uses == 1 ? "" : "s" );
                        }
                        if( clicked )
                        {
                            InState.Base = HueForIntensity( entry.Color, InHdr, InState.Ev );
                            WriteBack( InState, InOutColor, InHdr );
                            ImGui::MarkItemEdited( chipId );
                            Juice::Flash( chipId );
                            Juice::Play( Juice::Cue::Commit );
                            changed = true;
                        }
                    }
                }
            }

            // The eyedropper.
            if( hooks.ArmEyedropper )
            {
                ImGui::Dummy( ImVec2( 0.f, std::round( gap * 0.5f ) ) );
                const bool armed = hooks.IsEyedropperArmed && hooks.IsEyedropperArmed( InSwatchId );
                if( armed )
                {
                    ImGui::PushStyleColor( ImGuiCol_Button, palette.Accent );
                }
                const ImVec2 min = ImGui::GetCursorScreenPos();
                // Room on the left for the pipette, then the name.
                const bool pressed = ImGui::Button( "     Eyedropper", ImVec2( width, 0.f ) );
                ItemLog::SetLastItemLabel( "Eyedropper" );
                ItemLog::SetLastItemValue( armed ? "armed" : "off" );
                if( armed )
                {
                    ImGui::PopStyleColor();
                }
                const float textWidth = ImGui::CalcTextSize( "     Eyedropper" ).x;
                const float glyph = std::round( ImGui::GetFontSize() * 0.8f );
                const ImVec2 center( std::round( min.x + ( width - textWidth ) * 0.5f + glyph * 0.6f ), std::round( min.y + ImGui::GetFrameHeight() * 0.5f ) );
                PipetteGlyph( drawList, center, glyph, U32( palette.Text ) );
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
                {
                    ImGui::SetTooltip( "Then click in the World View to take the colour under the pointer.\nIt is the colour on screen: after lighting, exposure and tonemapping,\nnot the surface's own colour. Esc cancels." );
                }
                if( pressed )
                {
                    hooks.ArmEyedropper( InSwatchId );
                    ImGui::CloseCurrentPopup();
                }
            }
            // Any other gesture that changed the colour leaves the Hex row following it again.
            if( changed )
            {
                InState.HexError = false;
            }
            return changed;
        }
    }

    void SetColorPopoverHooks( ColorPopoverHooks InHooks )
    {
        Hooks() = std::move( InHooks );
    }

    bool ColorSwatchPopover( const char* InId, float* InOutColor, int InComponents, bool InHdr, const char* InCaption )
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const int components = std::clamp( InComponents, 3, 4 );
        const ImGuiID id = window->GetID( InId );
        const bool mixed = ( g.CurrentItemFlags & ImGuiItemFlags_MixedValue ) != 0;
        const ImGuiTheme::Palette& palette = ImGuiTheme::ActivePalette();
        ColorPopoverHooks& hooks = Hooks();
        bool changed = false;

        // A colour the eyedropper sampled for this swatch (it lands some frames after the click).
        Rgb picked;
        if( hooks.TakeEyedropped && hooks.TakeEyedropped( id, picked ) )
        {
            const Rgb current = { InOutColor[0], InOutColor[1], InOutColor[2] };
            const double ev = InHdr ? ColorMath::SplitHdr( current ).Ev : 0.0;
            // A screen sample is rarely at full brightness: keep the stops the swatch shows.
            const Rgb value = InHdr ? ColorMath::WithIntensity( picked, ev ) : picked;
            std::copy( value.begin(), value.end(), InOutColor );
            Juice::Flash( id );
            Juice::Play( Juice::Cue::Commit );
            changed = true;
        }

        // The swatch.
        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 size( std::max( ImGui::CalcItemWidth(), ImGuiTheme::Em( 3.f ) ), ImGui::GetFrameHeight() );
        const ImRect bb( pos, ImVec2( pos.x + size.x, pos.y + size.y ) );
        ImGui::ItemSize( size, g.Style.FramePadding.y );
        const bool visible = ImGui::ItemAdd( bb, id );
        bool hovered = false;
        bool held = false;
        const bool pressed = visible && ImGui::ButtonBehavior( bb, id, &hovered, &held );
        if( changed )
        {
            ImGui::MarkItemEdited( id );
        }
        // The menu opens on release, as context menus do: the press closes an open popover first.
        const bool openMenu = ( pressed && g.IO.KeyCtrl ) || ( hovered && ImGui::IsMouseReleased( ImGuiMouseButton_Right ) );
        const bool openPopover = pressed && !g.IO.KeyCtrl;
        const std::string described = ColorMath::Describe( InOutColor, components, InHdr );
        ItemLog::SetItemValue( id, mixed ? std::string( "Mixed" ) : described );

        const Rgb color = { InOutColor[0], InOutColor[1], InOutColor[2] };
        const ColorMath::HdrSplit split = InHdr ? ColorMath::SplitHdr( color ) : ColorMath::HdrSplit { color, 0.0 };
        const float alpha = components >= 4 ? InOutColor[3] : 1.f;
        const bool armed = hooks.IsEyedropperArmed && hooks.IsEyedropperArmed( id );
        if( visible )
        {
            ImDrawList* drawList = window->DrawList;
            const float rounding = g.Style.FrameRounding;
            const ImGuiCol background = held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg;
            ImGui::RenderFrame( bb.Min, bb.Max, ImGui::GetColorU32( background ), true, rounding );
            const float inset = std::max( std::round( g.Style.FramePadding.y * 0.6f ), Thin() * 2.f );
            const float chipWidth = std::min( std::round( ImGui::GetFrameHeight() * 2.f ), std::round( size.x * 0.35f ) );
            const ImVec2 chipMin( bb.Min.x + inset, bb.Min.y + inset );
            const ImVec2 chipMax( bb.Min.x + inset + chipWidth, bb.Max.y - inset );
            Chip( drawList, chipMin, chipMax, split.Base, alpha, std::max( rounding - inset * 0.5f, 0.f ) );
            // HDR: a bright edge on the chip says the colour goes past white.
            if( InHdr && split.Ev > 0.05 )
            {
                const float edge = std::max( Thin() * 2.f, std::round( chipWidth * 0.18f ) );
                drawList->AddRectFilledMultiColor( ImVec2( chipMax.x - edge, chipMin.y + Thin() ), ImVec2( chipMax.x - Thin(), chipMax.y - Thin() ),
                    Display( split.Base, 0.f ), IM_COL32_WHITE, IM_COL32_WHITE, Display( split.Base, 0.f ) );
            }
            const float textY = bb.Min.y + ( size.y - ImGui::GetFontSize() ) * 0.5f;
            const float textX = chipMax.x + ImGuiTheme::InnerGap() + inset;
            const std::string hex = mixed ? std::string( "Mixed" ) : components >= 4 ? ColorMath::FormatHex( split.Base, alpha ) : ColorMath::FormatHex( split.Base );
            const ImVec4 textColor = mixed ? palette.Warning : palette.Text;
            ImGui::PushClipRect( bb.Min, bb.Max, true );
            drawList->AddText( ImVec2( textX, textY ), U32( textColor ), hex.c_str() );
            if( InHdr && !mixed )
            {
                const std::string tag = ColorMath::FormatIntensity( split.Ev );
                const float tagWidth = ImGui::CalcTextSize( tag.c_str() ).x;
                const float tagX = bb.Max.x - g.Style.FramePadding.x - tagWidth;
                if( tagX > textX + ImGui::CalcTextSize( hex.c_str() ).x + ImGuiTheme::InnerGap() )
                {
                    drawList->AddText( ImVec2( tagX, textY ), U32( split.Ev > 0.05 ? palette.AccentText : palette.Muted ), tag.c_str() );
                }
            }
            ImGui::PopClipRect();
            const float flash = Juice::FlashAmount( id );
            if( armed || flash > 0.f )
            {
                const ImVec4 ring = armed ? palette.AccentText : ImGuiTheme::WithAlpha( palette.Text, flash );
                drawList->AddRect( bb.Min, bb.Max, U32( ring ), rounding, 0, Thin() * 2.f );
            }
        }
        ImGui::PushID( InId );
        if( hovered && !held && !ImGui::IsPopupOpen( ImGui::GetID( "##colorpopover" ), 0 ) && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
        {
            const int r = static_cast<int>( std::round( std::clamp( split.Base[0], 0.f, 1.f ) * 255.f ) );
            const int gr = static_cast<int>( std::round( std::clamp( split.Base[1], 0.f, 1.f ) * 255.f ) );
            const int b = static_cast<int>( std::round( std::clamp( split.Base[2], 0.f, 1.f ) * 255.f ) );
            std::string intensity;
            if( InHdr )
            {
                intensity = "\n" + ColorMath::FormatIntensity( split.Ev ) + " (" + ColorMath::FormatMultiplier( split.Ev ) + ") above white";
            }
            ImGui::SetTooltip( "%s%s  rgb(%d, %d, %d)%s\nClick to edit; right-click to copy or paste", mixed ? "Selected entities differ. This one: " : "",
                ( components >= 4 ? ColorMath::FormatHex( split.Base, alpha ) : ColorMath::FormatHex( split.Base ) ).c_str(), r, gr, b, intensity.c_str() );
        }

        if( openMenu )
        {
            ImGui::OpenPopup( "##colormenu" );
        }
        if( ImGui::BeginPopup( "##colormenu" ) )
        {
            if( ImGui::MenuItem( "Copy", described.c_str() ) )
            {
                ImGui::SetClipboardText( described.c_str() );
            }
            const char* clipboard = ImGui::GetClipboardText();
            Rgb pasted;
            float pastedAlpha = alpha;
            double pastedEv = InHdr ? split.Ev : 0.0;
            const bool parsed = clipboard && ColorMath::ParseSwatchText( clipboard, pasted, &pastedAlpha, &pastedEv, nullptr );
            std::string preview;
            if( parsed )
            {
                float shown[4] = { pasted[0], pasted[1], pasted[2], pastedAlpha };
                if( InHdr )
                {
                    const Rgb value = ColorMath::WithIntensity( pasted, pastedEv );
                    std::copy( value.begin(), value.end(), shown );
                }
                preview = ColorMath::Describe( shown, components, InHdr );
            }
            if( ImGui::MenuItem( "Paste", parsed ? preview.c_str() : nullptr, false, parsed ) )
            {
                const Rgb value = InHdr ? ColorMath::WithIntensity( pasted, pastedEv ) : pasted;
                std::copy( value.begin(), value.end(), InOutColor );
                if( components >= 4 )
                {
                    InOutColor[3] = pastedAlpha;
                }
                Juice::Flash( id );
                Juice::Play( Juice::Cue::Commit );
                changed = true;
            }
            if( !parsed )
            {
                ImGui::SetItemTooltip( "The clipboard holds no colour (#RRGGBB, RRGGBB or rgb(255, 136, 0))" );
            }
            ImGui::EndPopup();
        }

        PopoverState& state = States()[id];
        if( openPopover )
        {
            ImGui::OpenPopup( "##colorpopover" );
            // Below the swatch, or above it when there's no room underneath.
            const ImGuiViewport* viewport = window->Viewport ? window->Viewport : ImGui::GetMainViewport();
            const float expected = state.Height > 0.f ? state.Height : ImGuiTheme::Em( 24.f );
            ImVec2 at( bb.Min.x, bb.Max.y + g.Style.ItemSpacing.y );
            if( at.y + expected > viewport->WorkPos.y + viewport->WorkSize.y && bb.Min.y - expected > viewport->WorkPos.y )
            {
                at.y = bb.Min.y - expected - g.Style.ItemSpacing.y;
            }
            at.x = std::clamp( at.x, viewport->WorkPos.x, std::max( viewport->WorkPos.x, viewport->WorkPos.x + viewport->WorkSize.x - ImGuiTheme::Em( 15.f ) ) );
            ImGui::SetNextWindowPos( at, ImGuiCond_Always );
        }
        ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 15.f ), 0.f ), ImGuiCond_Always );
        if( ImGui::BeginPopup( "##colorpopover" ) )
        {
            if( state.LastFrame != g.FrameCount - 1 )
            {
                // Just opened: remember where it started and gather the scene's colours once.
                std::copy( InOutColor, InOutColor + components, state.Original );
                state.HasBase = false;
                state.HexError = false;
                state.Palette.clear();
                if( hooks.SceneColors )
                {
                    state.Palette = ColorMath::BuildPalette( hooks.SceneColors() );
                }
            }
            state.LastFrame = g.FrameCount;
            Sync( state, InOutColor, InHdr );
            if( DrawPopoverBody( id, state, InOutColor, components, InHdr, InCaption ) )
            {
                changed = true;
            }
            state.Height = ImGui::GetWindowHeight();
            ItemLog::EndSection();
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }
}
