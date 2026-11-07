#include "WidgetGalleryWidget.h"

#if USING( ME_EDITOR )
#include <Widgets/ScrubField.h>
#include <Widgets/Juice.h>
#include <Utils/HavanaUtils.h>
#include <ImGuiTheme.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace
{
    using Reflection::Unit;

    struct Sample
    {
        const char* Label;
        double Start;
        MeWidgets::NumberSpec Spec;
        double Value;
    };

    MeWidgets::NumberSpec MakeSpec( Unit InUnit, double InMin, double InMax, bool InLog = false, double InDefault = 0.0 )
    {
        MeWidgets::NumberSpec spec;
        spec.FieldUnit = InUnit;
        spec.HasRange = InMax > InMin;
        spec.Min = InMin;
        spec.Max = InMax;
        spec.Log = InLog;
        spec.HasDefault = true;
        spec.Default = InDefault;
        return spec;
    }

    std::vector<Sample>& Samples()
    {
        static std::vector<Sample> samples = [] {
            std::vector<Sample> list;
            auto add = [&]( const char* InLabel, double InStart, MeWidgets::NumberSpec InSpec ) {
                list.push_back( { InLabel, InStart, InSpec, InStart } );
            };
            MeWidgets::NumberSpec distance = MakeSpec( Unit::Meters, 0.01, 1000.0, true, 10.0 );
            distance.HasSoftRange = true;
            distance.SoftMin = 0.5;
            distance.SoftMax = 50.0;
            add( "Distance", 10.0, distance );
            add( "Angle", 35.0, MakeSpec( Unit::Degrees, 0.0, 89.0, false, 30.0 ) );
            add( "Volume", 0.5, MakeSpec( Unit::LinearGain, 0.0, 1.0, false, 1.0 ) );
            add( "Opacity", 0.75, MakeSpec( Unit::Percent, 0.0, 1.0, false, 1.0 ) );
            add( "Exposure", 1.5, MakeSpec( Unit::EV, -6.0, 6.0, false, 0.0 ) );
            add( "Temperature", 5600.0, MakeSpec( Unit::Kelvin, 1000.0, 12000.0, false, 6500.0 ) );
            add( "Aperture", 2.8, MakeSpec( Unit::FStop, 1.0, 22.0, true, 5.6 ) );
            add( "Duration", 0.25, MakeSpec( Unit::Seconds, 0.0, 10.0, false, 1.0 ) );
            add( "Mass", 70.0, MakeSpec( Unit::Kilograms, 0.001, 50000.0, true, 1.0 ) );
            MeWidgets::NumberSpec count = MakeSpec( Unit::None, 0.0, 64.0, false, 8.0 );
            count.Integer = true;
            add( "Count", 12.0, count );
            add( "Plain", 0.1, MakeSpec( Unit::None, 0.0, 0.0, false, 0.0 ) );
            return list;
        }();
        return samples;
    }

    const char* LevelName( MeWidgets::Juice::Level InLevel )
    {
        switch( InLevel )
        {
        case MeWidgets::Juice::Level::Off: return "Off";
        case MeWidgets::Juice::Level::Subtle: return "Subtle";
        default: return "Full";
        }
    }
}


WidgetGalleryWidget::WidgetGalleryWidget()
	: HavanaWidget( "Widget Gallery" )
{
	IsOpen = false;
}


void WidgetGalleryWidget::ResetSamples()
{
	for( Sample& sample : Samples() )
	{
		sample.Value = sample.Start;
	}
	MeWidgets::Juice::Reset();
}


void WidgetGalleryWidget::Poke()
{
	m_poke = true;
}


void WidgetGalleryWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 30.f ), ImGuiTheme::Em( 28.f ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
	{
		ImGui::End();
		return;
	}

	ImGui::TextDisabled( "Juice: %s%s (Edit > Preferences > Appearance)", LevelName( MeWidgets::Juice::GetLevel() ),
		MeWidgets::Juice::GetReduceMotion() ? ", reduce motion" : "" );
	HavanaUtils::SkipLabel();
	if( ImGui::Button( "Poke" ) )
	{
		m_poke = true;
	}
	ImGui::SetItemTooltip( "Fire every kind of feedback at once" );
	ImGui::SameLine();
	if( ImGui::Button( "Reset" ) )
	{
		ResetSamples();
	}

	ImGui::SeparatorText( "Unit fields" );
	const bool poke = m_poke;
	m_poke = false;
	for( size_t i = 0; i < Samples().size(); ++i )
	{
		Sample& sample = Samples()[i];
		MeWidgets::UnitRow( sample.Label, sample.Value, sample.Spec );
		if( poke )
		{
			const ImGuiID id = ImGui::GetItemID();
			MeWidgets::Juice::Flash( id );
			if( i == 0 )
			{
				MeWidgets::Juice::Tick( id, ImVec2( ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f, ImGui::GetItemRectMin().y ) );
			}
			if( i == 1 )
			{
				MeWidgets::Juice::Shake( id );
			}
			if( i == 2 )
			{
				MeWidgets::Juice::Burst( ImGui::GetItemRectMax(), ImGui::GetColorU32( ImGuiTheme::AccentText() ), 14 );
			}
		}
	}
	ImGui::End();
}

#endif
