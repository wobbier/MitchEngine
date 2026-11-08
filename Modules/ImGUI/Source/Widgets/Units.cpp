#include "Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace MeWidgets
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        // Below this a linear gain shows as silence (-100 dB).
        constexpr double kSilentGain = 1e-5;

        // A word typed after a number, and how many display units one of it is.
        struct Alias
        {
            const char* Text;
            double Scale;
        };

        struct UnitDef
        {
            UnitInfo Info;
            std::vector<Alias> Aliases;     // the display symbol first
        };

        const std::vector<UnitDef>& Defs()
        {
            static const std::vector<UnitDef> defs = [] {
                std::vector<UnitDef> table( static_cast<size_t>( Unit::Count ) );
                auto set = [&]( Unit unit, UnitInfo info, std::vector<Alias> aliases ) {
                    table[static_cast<size_t>( unit )] = UnitDef{ info, std::move( aliases ) };
                };
                set( Unit::None, { "None", "", "a number" }, {} );
                set( Unit::Meters, { "Meters", "m", "a length" },
                    { { "m", 1.0 }, { "meters", 1.0 }, { "meter", 1.0 }, { "metres", 1.0 }, { "metre", 1.0 }, { "mm", 0.001 },
                      { "cm", 0.01 }, { "km", 1000.0 }, { "in", 0.0254 }, { "\"", 0.0254 }, { "ft", 0.3048 }, { "'", 0.3048 },
                      { "yd", 0.9144 }, { "mi", 1609.344 } } );
                set( Unit::Millimeters, { "Millimeters", "mm", "a length" },
                    { { "mm", 1.0 }, { "cm", 10.0 }, { "m", 1000.0 }, { "in", 25.4 }, { "\"", 25.4 } } );
                const std::vector<Alias> angles = { { "\xC2\xB0", 1.0 }, { "deg", 1.0 }, { "degrees", 1.0 }, { "degree", 1.0 },
                    { "rad", 180.0 / kPi }, { "radians", 180.0 / kPi }, { "turns", 360.0 }, { "turn", 360.0 }, { "rev", 360.0 } };
                set( Unit::Degrees, { "Degrees", "\xC2\xB0", "an angle" }, angles );
                set( Unit::Radians, { "Radians", "\xC2\xB0", "an angle" }, angles );
                set( Unit::Kelvin, { "Kelvin", "K", "a colour temperature" }, { { "K", 1.0 }, { "kelvin", 1.0 }, { "k", 1000.0 } } );
                set( Unit::Decibels, { "Decibels", "dB", "a level" }, { { "dB", 1.0 } } );
                set( Unit::LinearGain, { "Gain", "dB", "a level" }, { { "dB", 1.0 } } );
                set( Unit::Kilograms, { "Kilograms", "kg", "a mass" },
                    { { "kg", 1.0 }, { "g", 0.001 }, { "t", 1000.0 }, { "tonnes", 1000.0 }, { "lb", 0.45359237 }, { "lbs", 0.45359237 } } );
                set( Unit::Newtons, { "Newtons", "N", "a force" }, { { "N", 1.0 }, { "kN", 1000.0 } } );
                set( Unit::EV, { "EV", "EV", "an exposure" }, { { "EV", 1.0 }, { "stops", 1.0 }, { "stop", 1.0 } } );
                set( Unit::Seconds, { "Seconds", "s", "a duration" },
                    { { "s", 1.0 }, { "sec", 1.0 }, { "ms", 0.001 }, { "min", 60.0 }, { "h", 3600.0 } } );
                set( Unit::Milliseconds, { "Milliseconds", "ms", "a duration" }, { { "ms", 1.0 }, { "s", 1000.0 } } );
                set( Unit::Hours, { "Hours", "h", "a time of day" }, { { "h", 1.0 }, { "min", 1.0 / 60.0 }, { "s", 1.0 / 3600.0 } } );
                set( Unit::Hertz, { "Hertz", "Hz", "a frequency" }, { { "Hz", 1.0 }, { "kHz", 1000.0 } } );
                set( Unit::Percent, { "Percent", "%", "a fraction" }, { { "%", 1.0 } } );
                set( Unit::Multiplier, { "Multiplier", "\xC3\x97", "a multiplier" }, { { "\xC3\x97", 1.0 }, { "x", 1.0 } } );
                set( Unit::MetersPerSecond, { "Meters per second", "m/s", "a speed" },
                    { { "m/s", 1.0 }, { "km/h", 1.0 / 3.6 }, { "kph", 1.0 / 3.6 }, { "mph", 0.44704 } } );
                set( Unit::MetersPerSecondSq, { "Meters per second squared", "m/s\xC2\xB2", "an acceleration" },
                    { { "m/s\xC2\xB2", 1.0 }, { "m/s2", 1.0 }, { "g", 9.80665 } } );
                set( Unit::DegreesPerSecond, { "Degrees per second", "\xC2\xB0/s", "an angular speed" },
                    { { "\xC2\xB0/s", 1.0 }, { "deg/s", 1.0 }, { "rpm", 6.0 } } );
                set( Unit::Pixels, { "Pixels", "px", "a pixel size" }, { { "px", 1.0 } } );
                set( Unit::FStop, { "F-stop", "f/", "an aperture" }, {} );
                set( Unit::PitchRatio, { "Semitones", "st", "a pitch" }, { { "st", 1.0 }, { "semitones", 1.0 }, { "oct", 12.0 } } );
                set( Unit::Frames, { "Frames", "frames", "a frame count" }, { { "frames", 1.0 }, { "f", 1.0 } } );
                set( Unit::Texels, { "Texels", "texels", "a texel count" }, { { "texels", 1.0 } } );
                set( Unit::PerMeter, { "Per meter", "/m", "a density" }, { { "/m", 1.0 } } );
                return table;
            }();
            return defs;
        }

        const UnitDef& Def( Unit InUnit )
        {
            const size_t index = static_cast<size_t>( InUnit );
            return Defs()[index < Defs().size() ? index : 0];
        }

        bool StartsWithNoCase( std::string_view InText, std::string_view InPrefix )
        {
            return InText.size() >= InPrefix.size() && std::equal( InPrefix.begin(), InPrefix.end(), InText.begin(), []( char a, char b ) {
                return std::tolower( static_cast<unsigned char>( a ) ) == std::tolower( static_cast<unsigned char>( b ) );
            } );
        }

        bool IsWordChar( char InChar )
        {
            return std::isalpha( static_cast<unsigned char>( InChar ) ) || ( static_cast<unsigned char>( InChar ) & 0x80 );
        }

        // Recursive descent over + - * / ( ) with unit suffixes. Every quantity is in the field's
        // display unit.
        class Parser
        {
        public:
            Parser( Unit InUnit, std::string_view InText ) : m_unit( InUnit ), m_text( InText ) {}

            bool Run( double& OutValue, std::string& OutError )
            {
                SkipSpace();
                if( m_pos == m_text.size() )
                {
                    OutError = "type a number";
                    return false;
                }
                if( !Expression( OutValue ) )
                {
                    OutError = m_error;
                    return false;
                }
                SkipSpace();
                if( m_pos != m_text.size() )
                {
                    OutError = m_error.empty() ? "didn't understand '" + std::string( m_text.substr( m_pos ) ) + "'" : m_error;
                    return false;
                }
                if( !std::isfinite( OutValue ) && !m_allowInfinite )
                {
                    OutError = "that isn't a finite number";
                    return false;
                }
                return true;
            }

        private:
            void SkipSpace()
            {
                while( m_pos < m_text.size() && std::isspace( static_cast<unsigned char>( m_text[m_pos] ) ) ) ++m_pos;
            }

            bool Expression( double& OutValue )
            {
                if( !Term( OutValue ) ) return false;
                while( true )
                {
                    SkipSpace();
                    if( m_pos >= m_text.size() || ( m_text[m_pos] != '+' && m_text[m_pos] != '-' ) ) return true;
                    const char op = m_text[m_pos++];
                    double right = 0.0;
                    if( !Term( right ) ) return false;
                    OutValue = op == '+' ? OutValue + right : OutValue - right;
                }
            }

            bool Term( double& OutValue )
            {
                if( !Factor( OutValue ) ) return false;
                while( true )
                {
                    SkipSpace();
                    // "m/s" after a number is a unit, handled in Factor; here '/' divides.
                    if( m_pos >= m_text.size() || ( m_text[m_pos] != '*' && m_text[m_pos] != '/' ) ) return true;
                    const char op = m_text[m_pos++];
                    double right = 0.0;
                    m_lastScale = 1.0;
                    if( !Factor( right ) ) return false;
                    if( op == '/' )
                    {
                        if( right == 0.0 )
                        {
                            m_error = "can't divide by zero";
                            return false;
                        }
                        // A unit on the divisor names the result's unit: "1/4 turn" is a quarter
                        // turn, not one degree over four turns.
                        const double scale = m_lastScale;
                        OutValue = OutValue / ( right / scale ) * scale;
                    }
                    else
                    {
                        OutValue *= right;
                    }
                }
            }

            bool Factor( double& OutValue )
            {
                SkipSpace();
                if( m_pos >= m_text.size() )
                {
                    m_error = "the expression ends early";
                    return false;
                }
                const char c = m_text[m_pos];
                if( c == '-' || c == '+' )
                {
                    ++m_pos;
                    if( !Factor( OutValue ) ) return false;
                    if( c == '-' ) OutValue = -OutValue;
                    return true;
                }
                if( c == '(' )
                {
                    ++m_pos;
                    if( !Expression( OutValue ) ) return false;
                    SkipSpace();
                    if( m_pos >= m_text.size() || m_text[m_pos] != ')' )
                    {
                        m_error = "a '(' is never closed";
                        return false;
                    }
                    ++m_pos;
                    // A bracketed quantity is already in display units; only a unit after it scales.
                    m_lastScale = 1.0;
                    return Suffix( OutValue );
                }
                // f/2.8 for apertures.
                if( m_unit == Unit::FStop && StartsWithNoCase( m_text.substr( m_pos ), "f/" ) )
                {
                    m_pos += 2;
                }
                if( StartsWithNoCase( m_text.substr( m_pos ), "inf" ) )
                {
                    m_pos += 3;
                    if( StartsWithNoCase( m_text.substr( m_pos ), "inity" ) ) m_pos += 5;
                    OutValue = HUGE_VAL;
                    m_allowInfinite = m_unit == Unit::LinearGain || m_unit == Unit::Decibels;
                    return Suffix( OutValue );
                }
                const char* start = m_text.data() + m_pos;
                std::string number( start, std::min<size_t>( m_text.size() - m_pos, 64 ) );
                char* end = nullptr;
                OutValue = std::strtod( number.c_str(), &end );
                if( end == number.c_str() || std::isnan( OutValue ) || std::isinf( OutValue ) )
                {
                    m_error = "expected a number at '" + std::string( m_text.substr( m_pos ) ) + "'";
                    return false;
                }
                m_pos += static_cast<size_t>( end - number.c_str() );
                return Suffix( OutValue );
            }

            // An optional unit after a quantity, scaled into the field's display unit.
            bool Suffix( double& InOutValue )
            {
                SkipSpace();
                if( m_pos >= m_text.size() ) return true;
                const std::string_view rest = m_text.substr( m_pos );
                const auto& aliases = Def( m_unit ).Aliases;
                // Exact case first ("k" is kilo-kelvin, "K" is kelvin), then any case.
                const Alias* best = nullptr;
                for( int pass = 0; pass < 2 && !best; ++pass )
                {
                    for( const Alias& alias : aliases )
                    {
                        const size_t length = std::strlen( alias.Text );
                        const bool matches = pass == 0 ? rest.substr( 0, length ) == alias.Text : StartsWithNoCase( rest, alias.Text );
                        if( !matches ) continue;
                        // A word unit must end at a word boundary ("m" doesn't swallow "min").
                        if( IsWordChar( alias.Text[length - 1] ) && length < rest.size() && IsWordChar( rest[length] ) ) continue;
                        if( !best || length > std::strlen( best->Text ) ) best = &alias;
                    }
                }
                if( best )
                {
                    m_pos += std::strlen( best->Text );
                    InOutValue *= best->Scale;
                    m_lastScale = best->Scale;
                    return true;
                }
                if( IsWordChar( rest[0] ) || rest[0] == '%' )
                {
                    size_t length = 0;
                    while( length < rest.size() && ( IsWordChar( rest[length] ) || rest[length] == '%' || rest[length] == '/' ) ) ++length;
                    m_error = "'" + std::string( rest.substr( 0, length ) ) + "' isn't " + Def( m_unit ).Info.Quantity;
                    return false;
                }
                return true;
            }

            Unit m_unit;
            std::string_view m_text;
            size_t m_pos = 0;
            double m_lastScale = 1.0;   // the unit scale the last Suffix applied
            std::string m_error;
            bool m_allowInfinite = false;
        };
    }

    const UnitInfo& GetUnitInfo( Unit InUnit )
    {
        return Def( InUnit ).Info;
    }

    double ToDisplay( Unit InUnit, double InStored )
    {
        switch( InUnit )
        {
        case Unit::Radians: return InStored * 180.0 / kPi;
        case Unit::LinearGain: return InStored <= kSilentGain ? -HUGE_VAL : 20.0 * std::log10( InStored );
        case Unit::Percent: return InStored * 100.0;
        case Unit::PitchRatio: return InStored <= 0.0 ? -HUGE_VAL : 12.0 * std::log2( InStored );
        default: return InStored;
        }
    }

    double FromDisplay( Unit InUnit, double InDisplay )
    {
        switch( InUnit )
        {
        case Unit::Radians: return InDisplay * kPi / 180.0;
        case Unit::LinearGain: return std::isinf( InDisplay ) && InDisplay < 0.0 ? 0.0 : std::pow( 10.0, InDisplay / 20.0 );
        case Unit::Percent: return InDisplay / 100.0;
        case Unit::PitchRatio: return std::pow( 2.0, InDisplay / 12.0 );
        default: return InDisplay;
        }
    }

    namespace
    {
        // "-0", "-0.0" -> "0", "0.0": a value that rounds to zero has no sign.
        std::string WithoutNegativeZero( std::string InText )
        {
            if( !InText.empty() && InText[0] == '-' && InText.find_first_not_of( "-0." ) == std::string::npos )
            {
                InText.erase( 0, 1 );
            }
            return InText;
        }

        bool IsZeroText( const std::string& InText )
        {
            return InText.find_first_not_of( "-+0." ) == std::string::npos;
        }
    }

    std::string FormatNumber( double InValue, int InDecimals )
    {
        if( std::isnan( InValue ) ) return "nan";
        if( std::isinf( InValue ) ) return InValue < 0.0 ? "-inf" : "inf";
        int decimals = InDecimals;
        if( decimals < 0 )
        {
            const double magnitude = std::abs( InValue );
            decimals = magnitude < 1e-12 ? 0 : std::clamp( 3 - static_cast<int>( std::floor( std::log10( magnitude ) ) ), 0, 5 );
        }
        char buffer[64];
        if( std::abs( InValue ) >= 1e15 )
        {
            // Too long for plain digits: scientific, which typed entry reads back.
            std::snprintf( buffer, sizeof( buffer ), "%.4g", InValue );
            return buffer;
        }
        std::snprintf( buffer, sizeof( buffer ), "%.*f", decimals, InValue );
        std::string text = buffer;
        if( InDecimals < 0 && text.find( '.' ) != std::string::npos )
        {
            while( text.back() == '0' ) text.pop_back();
            if( text.back() == '.' ) text.pop_back();
        }
        return WithoutNegativeZero( text );
    }

    namespace
    {
        // Angles and percentages read best with at most one decimal.
        int CappedDecimals( double InShown, int InDecimals, int InMax )
        {
            if( InDecimals >= 0 ) return InDecimals;
            const double magnitude = std::abs( InShown );
            const int natural = magnitude < 1e-12 ? 0 : std::clamp( 3 - static_cast<int>( std::floor( std::log10( magnitude ) ) ), 0, 5 );
            return natural > InMax ? InMax : -1;
        }
    }

    std::string FormatValue( Unit InUnit, double InStored, int InDecimals )
    {
        const double shown = ToDisplay( InUnit, InStored );
        switch( InUnit )
        {
        case Unit::None: return FormatNumber( shown, InDecimals );
        case Unit::Degrees:
        case Unit::Radians:
        case Unit::Percent:
        case Unit::DegreesPerSecond:
        {
            // A capped decimal count still trims trailing zeros ("20°", "21.3°").
            const int decimals = CappedDecimals( shown, InDecimals, 1 );
            std::string number = FormatNumber( shown, decimals );
            if( InDecimals < 0 && number.find( '.' ) != std::string::npos )
            {
                while( number.back() == '0' ) number.pop_back();
                if( number.back() == '.' ) number.pop_back();
            }
            return WithoutNegativeZero( number ) + GetUnitInfo( InUnit ).Symbol;
        }
        case Unit::Multiplier:
            return FormatNumber( shown, InDecimals ) + GetUnitInfo( InUnit ).Symbol;
        case Unit::FStop: return "f/" + FormatNumber( shown, InDecimals < 0 ? ( shown < 10.0 ? 1 : 0 ) : InDecimals );
        case Unit::LinearGain:
        case Unit::Decibels:
            if( std::isinf( shown ) && shown < 0.0 ) return "-inf dB";
            return FormatNumber( shown, InDecimals < 0 ? 1 : InDecimals ) + " dB";
        case Unit::EV:
        case Unit::PitchRatio:
        {
            std::string number = FormatNumber( shown, InDecimals < 0 ? ( InUnit == Unit::EV ? 1 : -1 ) : InDecimals );
            if( shown > 0.0 && !IsZeroText( number ) ) number = "+" + number;
            return number + " " + GetUnitInfo( InUnit ).Symbol;
        }
        case Unit::Kelvin: return FormatNumber( shown, InDecimals < 0 ? 0 : InDecimals ) + " K";
        default: return FormatNumber( shown, InDecimals ) + " " + GetUnitInfo( InUnit ).Symbol;
        }
    }

    bool ParseValue( Unit InUnit, std::string_view InText, double& OutStored, std::string* OutError )
    {
        Parser parser( InUnit, InText );
        double shown = 0.0;
        std::string error;
        if( !parser.Run( shown, error ) )
        {
            if( OutError ) *OutError = error;
            return false;
        }
        OutStored = FromDisplay( InUnit, shown );
        if( !std::isfinite( OutStored ) )
        {
            if( OutError ) *OutError = "that's out of range";
            return false;
        }
        return true;
    }

    namespace LogMap
    {
        namespace
        {
            // A range touching zero is shifted so its low end sits a thousandth of the span above
            // zero: log space then still spreads the small values out.
            double Offset( double InMin, double InMax )
            {
                return InMin > 0.0 ? 0.0 : ( InMax - InMin ) * 1e-3 - InMin;
            }
        }

        double ToNormalized( double InValue, double InMin, double InMax )
        {
            if( !( InMax > InMin ) ) return 0.0;
            const double offset = Offset( InMin, InMax );
            const double low = InMin + offset;
            const double high = InMax + offset;
            const double value = std::clamp( InValue, InMin, InMax ) + offset;
            return std::clamp( std::log( value / low ) / std::log( high / low ), 0.0, 1.0 );
        }

        double FromNormalized( double InT, double InMin, double InMax )
        {
            if( !( InMax > InMin ) ) return InMin;
            const double offset = Offset( InMin, InMax );
            const double low = InMin + offset;
            const double high = InMax + offset;
            const double t = std::clamp( InT, 0.0, 1.0 );
            return std::clamp( low * std::pow( high / low, t ) - offset, InMin, InMax );
        }
    }

    double NiceStep( double InSpan )
    {
        const double target = std::abs( InSpan ) / 10.0;
        if( !( target > 0.0 ) || !std::isfinite( target ) ) return 1.0;
        const double power = std::pow( 10.0, std::floor( std::log10( target ) ) );
        const double scaled = target / power;
        return ( scaled < 1.5 ? 1.0 : scaled < 3.5 ? 2.0 : scaled < 7.5 ? 5.0 : 10.0 ) * power;
    }
}
