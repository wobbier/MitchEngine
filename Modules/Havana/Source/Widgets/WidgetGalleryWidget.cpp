#include "WidgetGalleryWidget.h"

#if USING( ME_EDITOR )
#include <Widgets/ScrubField.h>
#include <Widgets/Juice.h>
#include <Widgets/Color.h>
#include <Widgets/LightWidgets.h>
#include <Widgets/CameraWidgets.h>
#include <Widgets/PostWidgets.h>
#include <Widgets/PhysicsWidgets.h>
#include <Widgets/AudioWidgets.h>
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

    // Light widgets (UX-5) with fixed samples.
    struct LightSamples
    {
        float Color[3] = { 1.f, 1.f, 1.f };
        double Intensity = 1.0;
        double Inner = 20.0;
        double Outer = 30.0;
        double Range = 10.0;

        LightSamples()
        {
            const std::array<float, 3> daylight = MeWidgets::ColorTemperature::ToSrgb( 5600.0 );
            std::copy( daylight.begin(), daylight.end(), Color );
        }
    };

    LightSamples& Lights()
    {
        static LightSamples samples;
        return samples;
    }

    // Camera widgets (UX-6) with fixed samples: a full-frame 50 mm lens at f/2.8 focused at 8 m,
    // rendering at 75% of 1920 x 1080.
    struct CameraSamples
    {
        double Fov = MeWidgets::Lens::VerticalFov( 50.0, MeWidgets::Lens::kFullFrameHeight );
        double RenderScale = 0.75;
        double Aperture = 2.8;
        double Focus = 8.0;
    };

    CameraSamples& Cameras()
    {
        static CameraSamples samples;
        return samples;
    }

    // Post widgets (UX-7) with fixed samples: the Fog showcase's camera, warmed a little and graded.
    struct PostSamples
    {
        double Exposure = -0.2;
        int Tonemapper = 1;
        double Temperature = 0.3;
        double Tint = -0.1;
        double Lift = 0.02;
        double Gamma = 1.1;
        double Gain = 0.95;
        double FogDensity = 0.035;
        float FogColor[3] = { 0.16f, 0.2f, 0.28f };
    };

    PostSamples& Posts()
    {
        static PostSamples samples;
        return samples;
    }

    // Physics widgets (UX-8) with fixed samples: a 70 kg body on a rubbery collider, damped a
    // little, with its Y rotation locked.
    struct PhysicsSamples
    {
        double Mass = 70.0;
        double Restitution = 0.6;
        double Friction = 0.6;
        double LinearDamping = 0.5;
        bool LockPosition[3] = { false, false, false };
        bool LockRotation[3] = { false, true, false };
    };

    PhysicsSamples& Physics()
    {
        static PhysicsSamples samples;
        return samples;
    }

    // Audio widgets (UX-9) with fixed samples: a source at -6 dB with a fixed live level, a fifth
    // up, mostly 3D, fading inversely from 2 m to 40 m.
    struct AudioSamples
    {
        double Volume = 0.5011872336;
        double Live = 0.18;
        double Pitch = 1.4983070769;
        int Rolloff = 0;
        double MinDistance = 2.0;
        double MaxDistance = 40.0;
        double SpatialBlend = 0.75;
    };

    AudioSamples& Audio()
    {
        static AudioSamples samples;
        return samples;
    }

    const MeWidgets::Reference kSampleRungs[] = {
        { "Candle", 0.1, "A candle flame" },
        { "Bulb", 1.0, "A household bulb" },
        { "Floodlight", 8.0, "A work light" },
    };

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
	Lights() = LightSamples();
	Cameras() = CameraSamples();
	Posts() = PostSamples();
	Physics() = PhysicsSamples();
	Audio() = AudioSamples();
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
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 30.f ), ImGuiTheme::Em( 46.f ) ), ImGuiCond_FirstUseEver );
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

	ImGui::SeparatorText( "Light widgets" );
	LightSamples& light = Lights();
	MeWidgets::KelvinBar( "Temperature", light.Color );
	MeWidgets::NumberSpec intensity = MakeSpec( Unit::Multiplier, 0.0, 100.0, true, 1.0 );
	intensity.HasSoftRange = true;
	intensity.SoftMin = 0.0;
	intensity.SoftMax = 10.0;
	MeWidgets::ReferenceLadder( "Reference", light.Intensity, intensity, kSampleRungs, IM_ARRAYSIZE( kSampleRungs ), "Looks-like references, not photometric" );
	MeWidgets::ConeWedge( "Cone", light.Inner, light.Outer, light.Color );
	MeWidgets::FalloffGraph( "Falloff", light.Range, MakeSpec( Unit::Meters, 0.01, 1000.0, true, 10.0 ), light.Color );

	ImGui::SeparatorText( "Camera widgets" );
	CameraSamples& camera = Cameras();
	const double sensor = MeWidgets::Lens::kFullFrameHeight;
	const char* sensorNote = "full frame";
	MeWidgets::FocalLengthRow( "Focal Length", camera.Fov, sensor, sensorNote );
	MeWidgets::LensWedge( "Lens", camera.Fov, sensor, sensorNote );
	MeWidgets::RenderScaleBar( "Resolution", camera.RenderScale, 1920, 1080, nullptr, "a sample 1920 x 1080 output" );
	MeWidgets::ApertureDial( "Iris", camera.Aperture );
	MeWidgets::FocusBar( "Focus Band", camera.Focus, MeWidgets::Lens::FocalLength( camera.Fov, sensor ), camera.Aperture, sensor, "the lens above" );

	ImGui::SeparatorText( "Post widgets" );
	PostSamples& post = Posts();
	MeWidgets::NumberSpec exposure = MakeSpec( Unit::EV, -8.0, 8.0, false, 0.0 );
	MeWidgets::ExposureLadder( "Lit Like", post.Exposure, exposure );
	MeWidgets::TonemapCurve( "Tonemap Curve", post.Tonemapper, post.Exposure, false );
	MeWidgets::WhiteBalancePad( "White Balance", post.Temperature, post.Tint );
	MeWidgets::LiftGammaGainCurve( "Lift Gamma Gain", post.Lift, post.Gamma, post.Gain, 1.0 );
	MeWidgets::NumberSpec density = MakeSpec( Unit::PerMeter, 0.0, 0.5, true, 0.02 );
	MeWidgets::FogVisibilityBar( "Fog Visibility", post.FogDensity, density, 0.0, 1.0, post.FogColor, 0.15 );

	ImGui::SeparatorText( "Physics widgets" );
	PhysicsSamples& physics = Physics();
	MeWidgets::NumberSpec mass = MakeSpec( Unit::Kilograms, 0.0, 100000.0, true, 1.0 );
	MeWidgets::MassLadder( "Weighs Like", physics.Mass, mass, 1.0, true );
	MeWidgets::BouncePreview( "Bounce", physics.Restitution );
	MeWidgets::SlideAngleRamp( "Slide Angle", physics.Friction, MakeSpec( Unit::None, 0.0, 2.0, false, 0.6 ) );
	// Damped every 1/240 s: the editor's 60 Hz fixed step in four substeps.
	MeWidgets::DampingGraph( "Linear Decay", physics.LinearDamping, MakeSpec( Unit::None, 0.0, 100.0, true, 0.0 ), 1.0 / 240.0, "speed" );
	const char* const axisNames[] = { "X", "Y", "Z" };
	const int axes[] = { 0, 1, 2 };
	MeWidgets::AxisLocks( "Lock Position", physics.LockPosition, axisNames, axes, 3, false );
	MeWidgets::AxisLocks( "Lock Rotation", physics.LockRotation, axisNames, axes, 3, true );

	ImGui::SeparatorText( "Audio widgets" );
	AudioSamples& audio = Audio();
	// Its own ID scope: the light widgets above have a "Falloff" too.
	ImGui::PushID( "Audio" );
	MeWidgets::LevelMeter( "Level", audio.Volume, MakeSpec( Unit::LinearGain, 0.0, 1.0, false, 1.0 ), audio.Live );
	MeWidgets::PitchKeyboard( "Keyboard", audio.Pitch, MakeSpec( Unit::PitchRatio, 0.1, 3.0, false, 1.0 ) );
	MeWidgets::SpatialBlendSlider( "Spatial", audio.SpatialBlend, MakeSpec( Unit::None, 0.0, 1.0, false, 0.0 ) );
	MeWidgets::RolloffGraph( "Falloff", audio.Rolloff, audio.MinDistance, audio.MaxDistance, MakeSpec( Unit::Meters, 0.0, 1000.0, true, 1.0 ),
		MakeSpec( Unit::Meters, 0.0, 10000.0, true, 50.0 ), audio.SpatialBlend );
	ImGui::PopID();
	ImGui::End();
}

#endif
