#pragma once
// RmlUi's own profiling zones (RMLUI_ZoneScoped..., RmlUi/Core/Profiling.h) are written for Tracy.
// RmlUi.sharpmake.cs builds RmlUi with RMLUI_TRACY_PROFILING and this folder ahead on the include
// path, so they land in the engine's profiler instead (Optick, or the Linux Chrome trace shim):
// layout, styles, data bindings and geometry show as zones under Rml::Context::Update / Render.
// Drop this when the engine moves to Tracy (ME-74): RmlUi then uses the real Tracy.hpp.
#include <optick.h>

#define ZoneScoped                         OPTICK_EVENT( __FUNCTION__ )
#define ZoneScopedN( name )                OPTICK_EVENT( name )
#define ZoneScopedC( color )               OPTICK_EVENT( __FUNCTION__ )
#define ZoneScopedNC( name, color )        OPTICK_EVENT( name )
#define ZoneNamed( varname, active )       OPTICK_EVENT( __FUNCTION__ )
#define ZoneNamedN( varname, name, active ) OPTICK_EVENT( name )
#define ZoneNamedC( varname, color, active ) OPTICK_EVENT( __FUNCTION__ )
#define ZoneNamedNC( varname, name, color, active ) OPTICK_EVENT( name )

// Zone text/renames, plots and frame marks have no equivalent here.
#define ZoneText( txt, size )   ( (void)0 )
#define ZoneName( txt, size )   ( (void)0 )
#define TracyPlot( name, val )  ( (void)0 )
#define FrameMark               ( (void)0 )
#define FrameMarkNamed( name )  ( (void)0 )
#define FrameMarkStart( name )  ( (void)0 )
#define FrameMarkEnd( name )    ( (void)0 )
