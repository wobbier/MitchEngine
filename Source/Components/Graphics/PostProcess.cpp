#include "PCH.h"
#include "PostProcess.h"

ME_REFLECT_ENUM( Moonlight::Tonemapper, { { "None", Moonlight::Tonemapper::None }, { "ACES", Moonlight::Tonemapper::ACES }, { "AgX", Moonlight::Tonemapper::AgX }, { "Reinhard", Moonlight::Tonemapper::Reinhard } } )

ME_REFLECT_BEGIN( PostProcess )
    ME_FIELD( ExposureCompensation ).Category( "Exposure" ).Range( -8.f, 8.f ).Tooltip( "Stops added to the exposure (or to auto exposure)" );
    ME_FIELD( AutoExposure ).Category( "Exposure" ).Tooltip( "Adapt to the scene's average brightness like an eye" );
    ME_FIELD( AutoExposureMinEV ).Category( "Exposure" ).Range( -10.f, 20.f );
    ME_FIELD( AutoExposureMaxEV ).Category( "Exposure" ).Range( -10.f, 20.f );
    ME_FIELD( AutoExposureSpeed ).Category( "Exposure" ).Range( 0.1f, 10.f );
    ME_FIELD( Tonemapper ).Category( "Exposure" );
    ME_FIELD( Bloom ).Category( "Bloom" );
    ME_FIELD( BloomIntensity ).Category( "Bloom" ).Range( 0.f, 1.f );
    ME_FIELD( BloomThreshold ).Category( "Bloom" ).Range( 0.f, 10.f ).Tooltip( "HDR brightness where bloom starts (soft knee)" );
    ME_FIELD( BloomRadius ).Category( "Bloom" ).Range( 0.25f, 3.f );
    ME_FIELD( AmbientOcclusion ).Category( "Ambient Occlusion" );
    ME_FIELD( AORadius ).Category( "Ambient Occlusion" ).Range( 0.05f, 5.f );
    ME_FIELD( AOIntensity ).Category( "Ambient Occlusion" ).Range( 0.f, 4.f );
    ME_FIELD( Saturation ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( Contrast ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( Temperature ).Category( "Color Grading" ).Range( -1.f, 1.f );
    ME_FIELD( Tint ).Category( "Color Grading" ).Range( -1.f, 1.f );
    ME_FIELD( Gamma ).Category( "Color Grading" ).Range( 0.2f, 3.f );
    ME_FIELD( Lift ).Category( "Color Grading" ).Range( -0.5f, 0.5f );
    ME_FIELD( Gain ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( VignetteIntensity ).Category( "Vignette" ).Range( 0.f, 1.f );
    ME_FIELD( VignetteSmoothness ).Category( "Vignette" ).Range( 0.01f, 2.f );
    ME_FIELD( FXAA ).Category( "Anti-aliasing" );
ME_REFLECT_END()


PostProcess::PostProcess()
    : Component( "PostProcess" )
{
}


Moonlight::PostProcessSettings PostProcess::ToSettings() const
{
    Moonlight::PostProcessSettings settings;
    settings.ExposureCompensation = ExposureCompensation;
    settings.AutoExposure = AutoExposure;
    settings.AutoExposureMinEV = AutoExposureMinEV;
    settings.AutoExposureMaxEV = AutoExposureMaxEV;
    settings.AutoExposureSpeed = AutoExposureSpeed;
    settings.Operator = Tonemapper;
    settings.Bloom = Bloom;
    settings.BloomIntensity = BloomIntensity;
    settings.BloomThreshold = BloomThreshold;
    settings.BloomRadius = BloomRadius;
    settings.AmbientOcclusion = AmbientOcclusion;
    settings.AORadius = AORadius;
    settings.AOIntensity = AOIntensity;
    settings.Saturation = Saturation;
    settings.Contrast = Contrast;
    settings.Temperature = Temperature;
    settings.Tint = Tint;
    settings.Gamma = Gamma;
    settings.Lift = Lift;
    settings.Gain = Gain;
    settings.VignetteIntensity = VignetteIntensity;
    settings.VignetteSmoothness = VignetteSmoothness;
    settings.FXAA = FXAA;
    return settings;
}
