#pragma once
// RmlUi's own profiling zones (RMLUI_ZoneScoped..., RmlUi/Core/Profiling.h) are written for Tracy.
// With ME_RMLUI_ZONES=1 at generate time, RmlUi.sharpmake.cs builds RmlUi with RMLUI_TRACY_PROFILING
// and this folder ahead on the include path, so they land in the engine's profiler instead (Optick,
// or the Linux Chrome trace shim). Opt-in: with it on, RmlUi also builds an element path string for
// every element it updates or renders, to name zones, which no filter here can avoid.
//
// Only zones around real work are kept: layout, style recomputes, geometry building, document and
// style sheet loading, which run when something changed. RmlUi also zones its per-element walks
// (Element::Update for every element, Render/OnRender and a draw per visible element): about 1,100
// zones of ~1 us each per frame in HeroShooter's HUD, too fine to read. Those compile to nothing:
// the choice is made at compile time from the function's signature.
//
// Drop this when the engine moves to Tracy (ME-74): RmlUi then uses the real Tracy.hpp.
#include <optick.h>
#include <string_view>

namespace MERmlZones
{
    // Functions (as they appear in the signature) whose zones are kept.
    constexpr std::string_view kKeptFunctions[] = {
        "ElementDocument::UpdateLayout(",
        "BlockFormattingContext::Format(",
        "FlexFormattingContext::Format(",
        "FormattingContext::FormatIndependent(",
        "ElementStyle::ComputeValues(",
        "ElementStyle::UpdateDefinition(",
        "ElementText::GenerateGeometry(",
        "ElementText::GenerateDecoration(",
        "ElementBackgroundBorder::GenerateGeometry(",
        "ElementEffects::InstanceEffects(",
        "Element::SetInnerRML(",
        "Factory::InstanceDocumentStream(",
        "BaseXMLParser::ReadBody(",
        "ElementDocument::ProcessHeader(",
        "ElementDocument::SetStyleSheetContainer(",
        "StyleSheet::BuildNodeIndex(",
        "StyleSheet::CombineStyleSheet(",
        "GeometryBoxShadow::GenerateTexture(",
        "GeometryBoxShadow::Resolve(",
    };
    // Explicitly named zones that are kept.
    constexpr std::string_view kKeptNames[] = {
        "CompileGeometry",
        "InstanceStream",
        "ConvFilter::Run",
        "BoxShadow::GenerateTexture::Callback",
    };

    constexpr bool KeepFunction( std::string_view InSignature )
    {
        for( std::string_view kept : kKeptFunctions )
        {
            // "Element::SetInnerRML(" must not match inside "ElementText::...": the character
            // before the match has to end a namespace or the return type.
            for( size_t at = InSignature.find( kept ); at != std::string_view::npos; at = InSignature.find( kept, at + 1 ) )
            {
                if( at == 0 || InSignature[at - 1] == ':' || InSignature[at - 1] == ' ' ) return true;
            }
        }
        return false;
    }

    constexpr bool KeepName( std::string_view InName )
    {
        for( std::string_view kept : kKeptNames )
        {
            if( InName == kept ) return true;
        }
        return false;
    }

    template<bool Keep>
    struct Zone
    {
        constexpr explicit Zone( const char* ) {}
    };

    template<>
    struct Zone<true>
    {
#if defined( ME_CHROMETRACE )
        explicit Zone( const char* InName ) : m_scope( InName, "RmlUi" ) {}
        ChromeTrace::Scope m_scope;
#elif USE_OPTICK
        explicit Zone( const char* InName ) : m_event( *::Optick::EventDescription::CreateShared( InName ) ) {}
        ::Optick::Event m_event;
#else
        explicit Zone( const char* ) {}
#endif
    };
}

#if defined( _MSC_VER ) && !defined( __clang__ )
    #define ME_RML_SIGNATURE __FUNCSIG__
#else
    #define ME_RML_SIGNATURE __PRETTY_FUNCTION__
#endif
#define ME_RML_CAT_IMPL( a, b ) a##b
#define ME_RML_CAT( a, b ) ME_RML_CAT_IMPL( a, b )
#define ME_RML_ZONE( keep, name )                                                         \
    static constexpr bool ME_RML_CAT( me_rml_keep_, __LINE__ ) = ( keep );                \
    ::MERmlZones::Zone<ME_RML_CAT( me_rml_keep_, __LINE__ )> ME_RML_CAT( me_rml_zone_, __LINE__ )( name )

#define ZoneScoped                                   ME_RML_ZONE( ::MERmlZones::KeepFunction( ME_RML_SIGNATURE ), __FUNCTION__ )
#define ZoneScopedC( color )                         ZoneScoped
#define ZoneScopedN( name )                          ME_RML_ZONE( ::MERmlZones::KeepName( name ), name )
#define ZoneScopedNC( name, color )                  ZoneScopedN( name )
#define ZoneNamed( varname, active )                 ZoneScoped
#define ZoneNamedN( varname, name, active )          ZoneScopedN( name )
#define ZoneNamedC( varname, color, active )         ZoneScoped
#define ZoneNamedNC( varname, name, color, active )  ZoneScopedN( name )

// Zone text/renames, plots and frame marks have no equivalent here.
#define ZoneText( txt, size )   ( (void)0 )
#define ZoneName( txt, size )   ( (void)0 )
#define TracyPlot( name, val )  ( (void)0 )
#define FrameMark               ( (void)0 )
#define FrameMarkNamed( name )  ( (void)0 )
#define FrameMarkStart( name )  ( (void)0 )
#define FrameMarkEnd( name )    ( (void)0 )
