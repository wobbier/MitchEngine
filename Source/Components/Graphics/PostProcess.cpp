#include "PCH.h"
#include "PostProcess.h"
#include "Graphics/ColorLut.h"
#include "Graphics/Texture.h"
#include "Materials/ShaderGraphAsset.h"
#include "Resource/ResourceCache.h"

ME_REFLECT_ENUM( Moonlight::Tonemapper, { { "None", Moonlight::Tonemapper::None }, { "ACES", Moonlight::Tonemapper::ACES }, { "AgX", Moonlight::Tonemapper::AgX }, { "Reinhard", Moonlight::Tonemapper::Reinhard } } )

ME_REFLECT_BEGIN( PostProcess )
    ME_FIELD( FXAA ).Category( "Anti-aliasing" ).Tooltip( "Smooths edges in the final image after tonemapping; can be combined with TAA" );
    ME_FIELD( TemporalAA ).Category( "Anti-aliasing" ).Tooltip( "Jittered frames resolved against depth-reprojected history; stabilizes edges and specular detail, but moving objects may ghost" );
    ME_FIELD( TemporalSharpness ).Category( "Anti-aliasing" ).Range( 0.f, 1.f ).Tooltip( "Counters the temporal resolve's softening; used when TAA is enabled" );
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
    ME_FIELD( AORadius ).Display( "AO Radius (m)" ).Category( "Ambient Occlusion" ).Range( 0.05f, 5.f );
    ME_FIELD( AOIntensity ).Category( "Ambient Occlusion" ).Range( 0.f, 4.f );
    ME_FIELD( DepthOfField ).Category( "Depth of Field" ).Tooltip( "Thin-lens circular bokeh before bloom and tonemapping; perspective cameras only" );
    ME_FIELD( DoFFocusDistance ).Display( "Focus Distance (m)" ).Category( "Depth of Field" ).Range( 0.1f, 1000.f ).Drag( 0.001f ).Tooltip( "World-space metres along this camera's forward axis to the sharp focal plane. Drag for 1 mm steps; Alt slows, Shift speeds up, Ctrl-click to type a value. Match the depth of the face, rather than the model's origin" );
    ME_FIELD( DoFAperture ).Display( "Aperture (f-stop)" ).Category( "Depth of Field" ).Range( 0.7f, 32.f ).Tooltip( "Smaller f-numbers give stronger blur; does not change exposure" );
    ME_FIELD( DoFSensorHeight ).Display( "Sensor Height (mm)" ).Category( "Depth of Field" ).Range( 1.f, 70.f ).Tooltip( "Focal length is inferred from this filmback height and the camera's vertical field of view" );
    ME_FIELD( DoFMaxBlurRadius ).Display( "Max Blur Radius (1080p px)" ).Category( "Depth of Field" ).Range( 0.f, 32.f ).Tooltip( "Scales with camera resolution, capped at 64 pixels; zero bypasses the effect" );
    ME_FIELD( DoFDebugCoC ).Display( "Visualize Circle of Confusion" ).Category( "Depth of Field" ).Tooltip( "Red = foreground, green = background, black = in focus (requires Depth of Field)" );
    ME_FIELD( DoFSplitDiopter ).Display( "Split Diopter" ).Category( "Depth of Field" ).Tooltip( "Use a near focus plane on the left and an independent far focus plane on the right" );
    ME_FIELD( DoFSplitFocusDistance ).Display( "Right Focus Distance (m)" ).Category( "Depth of Field" ).Range( 0.1f, 1000.f ).Drag( 0.001f ).Tooltip( "Camera-forward focus depth for the right side of the split" );
    ME_FIELD( DoFSplitPosition ).Display( "Split Position" ).Category( "Depth of Field" ).Range( 0.f, 1.f );
    ME_FIELD( DoFSplitFeather ).Display( "Split Feather" ).Category( "Depth of Field" ).Range( 0.f, 0.25f ).Tooltip( "Full transition width as a fraction of the image; zero makes a hard split" );
    ME_FIELD( Outline ).Category( "Outline" ).Tooltip( "Ink the opaque scene's silhouettes and creases, detected from the depth buffer; fog covers the lines" );
    ME_FIELD( OutlineColor ).Category( "Outline" ).Color();
    ME_FIELD( OutlineThickness ).Category( "Outline" ).Range( 0.5f, 4.f ).Tooltip( "Edge sample offset in pixels" );
    ME_FIELD( OutlineDepthThreshold ).Display( "Depth Threshold" ).Category( "Outline" ).Range( 0.01f, 1.f ).Speed( 0.005f ).Tooltip( "Metres of depth gap (per metre of distance) that reads as a silhouette" );
    ME_FIELD( OutlineNormalThreshold ).Display( "Crease Threshold" ).Category( "Outline" ).Range( 0.01f, 2.f ).Speed( 0.01f ).Tooltip( "How sharp a fold must be to ink: lower catches gentler creases" );
    ME_FIELD( Fog ).Category( "Fog" ).Tooltip( "Exponential height fog over the opaque scene" );
    ME_FIELD( FogColor ).Category( "Fog" ).HDR().Tooltip( "Ambient light scattered by the fog" );
    ME_FIELD( FogDensity ).Category( "Fog" ).Range( 0.f, 0.5f ).Speed( 0.001f ).Tooltip( "Extinction per metre at the base height" );
    ME_FIELD( FogHeight ).Display( "Fog Height (m)" ).Category( "Fog" ).Tooltip( "World height where the density is FogDensity" );
    ME_FIELD( FogHeightFalloff ).Category( "Fog" ).Range( 0.f, 2.f ).Speed( 0.005f ).Tooltip( "How fast the fog thins with height (per metre)" );
    ME_FIELD( FogStartDistance ).Display( "Fog Start Distance (m)" ).Category( "Fog" ).Range( 0.f, 500.f );
    ME_FIELD( FogMaxOpacity ).Category( "Fog" ).Range( 0.f, 1.f );
    ME_FIELD( FogSunIntensity ).Category( "Fog" ).Range( 0.f, 10.f ).Tooltip( "Sunlight (and, when volumetric, point / spot light) scattered by the fog" );
    ME_FIELD( FogAnisotropy ).Category( "Fog" ).Range( 0.f, 0.95f ).Tooltip( "0 scatters evenly; towards 1 the fog glows around the sun" );
    ME_FIELD( VolumetricFog ).Category( "Fog" ).Tooltip( "Raymarch the fog's lighting: shafts through the sun's shadows, point and spot lights" );
    ME_FIELD( VolumetricDistance ).Display( "Volumetric Distance (m)" ).Category( "Fog" ).Range( 5.f, 300.f ).Tooltip( "Metres raymarched (fog beyond is analytic)" );
    ME_FIELD( VolumetricSteps ).Category( "Fog" ).Range( 4.f, 256.f ).Tooltip( "Raymarch samples per pixel (hardware profiles may cap it)" );
    ME_FIELD( VolumetricTemporal ).Display( "Volumetric Temporal" ).Category( "Fog" ).Tooltip( "Accumulate the raymarch over frames against a reprojected history (independent of Temporal AA): smooth shafts for the same step count; a fast cut or a moving light trails for a few frames" );
    ME_FIELD( FogLocalAnisotropy ).Display( "Local Light Anisotropy" ).Category( "Fog" ).Range( 0.f, 0.95f ).Tooltip( "Henyey-Greenstein g for point, spot and area lights in volumetric fog: 0 scatters evenly; towards 1 beams brighten when seen from in front of the fixture" );
    ME_FIELD( Saturation ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( Contrast ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( Temperature ).Category( "Color Grading" ).Range( -1.f, 1.f );
    ME_FIELD( Tint ).Category( "Color Grading" ).Range( -1.f, 1.f );
    ME_FIELD( Gamma ).Category( "Color Grading" ).Range( 0.2f, 3.f );
    ME_FIELD( Lift ).Category( "Color Grading" ).Range( -0.5f, 0.5f );
    ME_FIELD( Gain ).Category( "Color Grading" ).Range( 0.f, 2.f );
    ME_FIELD( Lut ).Display( "LUT" ).Category( "Color Grading" ).Tooltip( "Display-referred lookup table: a .cube file (Resolve/Photoshop export), or an image strip of N slices of NxN with Format None and no mips. Stock grades: Engine/Assets/Textures/LUTs" );
    ME_FIELD( LutContribution ).Display( "LUT Contribution" ).Category( "Color Grading" ).Range( 0.f, 1.f ).Tooltip( "Blend between the graded and ungraded image" );
    ME_FIELD( VignetteIntensity ).Category( "Vignette" ).Range( 0.f, 1.f );
    ME_FIELD( VignetteSmoothness ).Category( "Vignette" ).Range( 0.01f, 2.f );
    ME_FIELD( EraVertexSnap ).Category( "Era" ).Display( "Vertex Snap" ).Range( 0.f, 2160.f ).Tooltip( "PS1 wobble: vertices snap to a grid this many virtual pixels tall (0 = off; try 120)" );
    ME_FIELD( EraAffineTextures ).Category( "Era" ).Display( "Affine Textures" ).Tooltip( "PS1 warping: textures interpolate in screen space, without perspective correction" );
    ME_FIELD( EraThreePointFilter ).Category( "Era" ).Display( "Three-Point Filter" ).Tooltip( "N64 texture filtering: three texels per sample instead of four" );
    ME_FIELD( EraKuwahara ).Category( "Era" ).Display( "Painterly (Kuwahara)" ).Range( 0.f, 8.f ).Tooltip( "Painted strokes: a Kuwahara filter of this radius in pixels (0 = off)" );
    ME_FIELD( EraColorBits ).Category( "Era" ).Display( "Color Bits" ).Range( 0.f, 8.f ).Tooltip( "Colour depth per channel (5 = 15-bit; 0 = full)" );
    ME_FIELD( EraDither ).Category( "Era" ).Display( "Dither" ).Range( 0.f, 1.f ).Tooltip( "Ordered (Bayer) dithering when reducing colours" );
    ME_FIELD( EraPalette ).Category( "Era" ).Display( "Palette" ).Asset( "Texture" ).Tooltip( "Quantize to these colours: an image one row tall (Format None, no mips). Stock: Engine/Assets/Textures/Palettes" );
    ME_FIELD( Graphs ).Category( "Shader Graphs" ).Asset( "ShaderGraph" ).Tooltip( "Post Process shader graphs, applied in order (each before or after tonemapping, per the graph)" );
    ME_FIELD( GraphParams ).Category( "Shader Graphs" ).Display( "Graph Properties" ).Tooltip( "This volume's values for each graph's exposed properties" );
ME_REFLECT_END()


PostProcess::PostProcess()
    : Component( "PostProcess" )
{
}


void PostProcess::ResolveGraphs() const
{
    // Loaded once per path (Material.json is small; its programs load in the background).
    if( m_graphs.size() != Graphs.size() )
    {
        m_graphs.resize( Graphs.size() );
    }
    for( size_t i = 0; i < Graphs.size(); ++i )
    {
        const std::string& path = Graphs[i].GetLocalPathString();
        if( m_graphs[i].first != path )
        {
            m_graphs[i].first = path;
            m_graphs[i].second = path.empty() ? nullptr : ResourceCache::GetInstance().Get<Moonlight::ShaderGraphAsset>( Moonlight::ShaderGraphAsset::ManifestPathFor( path ) );
        }
    }
    GraphParams.Sync( m_graphs );
}


#if USING( ME_EDITOR )
void PostProcess::OnEditorInspect()
{
    ResolveGraphs();
}
#endif


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
    settings.DepthOfField = DepthOfField;
    settings.DoFFocusDistance = DoFFocusDistance;
    settings.DoFAperture = DoFAperture;
    settings.DoFSensorHeight = DoFSensorHeight;
    settings.DoFMaxBlurRadius = DoFMaxBlurRadius;
    settings.DoFDebugCoC = DoFDebugCoC;
    settings.DoFSplitDiopter = DoFSplitDiopter;
    settings.DoFSplitFocusDistance = DoFSplitFocusDistance;
    settings.DoFSplitPosition = DoFSplitPosition;
    settings.DoFSplitFeather = DoFSplitFeather;
    settings.Outline = Outline;
    settings.OutlineColor[0] = OutlineColor.x;
    settings.OutlineColor[1] = OutlineColor.y;
    settings.OutlineColor[2] = OutlineColor.z;
    settings.OutlineThickness = OutlineThickness;
    settings.OutlineDepthThreshold = OutlineDepthThreshold;
    settings.OutlineNormalThreshold = OutlineNormalThreshold;
    settings.Fog = Fog;
    settings.FogColor[0] = FogColor.x;
    settings.FogColor[1] = FogColor.y;
    settings.FogColor[2] = FogColor.z;
    settings.FogDensity = FogDensity;
    settings.FogHeight = FogHeight;
    settings.FogHeightFalloff = FogHeightFalloff;
    settings.FogStartDistance = FogStartDistance;
    settings.FogMaxOpacity = FogMaxOpacity;
    settings.FogSunIntensity = FogSunIntensity;
    settings.FogAnisotropy = FogAnisotropy;
    settings.VolumetricFog = VolumetricFog;
    settings.VolumetricDistance = VolumetricDistance;
    settings.VolumetricSteps = VolumetricSteps;
    settings.VolumetricTemporal = VolumetricTemporal;
    settings.FogLocalAnisotropy = FogLocalAnisotropy;
    settings.Saturation = Saturation;
    settings.Contrast = Contrast;
    settings.Temperature = Temperature;
    settings.Tint = Tint;
    settings.Gamma = Gamma;
    settings.Lift = Lift;
    settings.Gain = Gain;
    const std::string& lut = Lut.GetLocalPathString();
    if( lut != m_loadedLut )
    {
        m_loadedLut = lut;
        m_lut = nullptr;
        m_lutStrip = nullptr;
        if( !lut.empty() && Lut.Exists )
        {
            if( lut.ends_with( ".cube" ) ) m_lut = ResourceCache::GetInstance().GetAsync<Moonlight::ColorLut>( Lut );
            else m_lutStrip = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( Lut );
        }
    }
    settings.Lut = m_lut;
    settings.LutStrip = m_lutStrip;
    settings.LutContribution = std::clamp( LutContribution, 0.f, 1.f );
    settings.VignetteIntensity = VignetteIntensity;
    settings.VignetteSmoothness = VignetteSmoothness;
    settings.EraVertexSnap = std::max( EraVertexSnap, 0.f );
    settings.EraAffineTextures = EraAffineTextures;
    settings.EraThreePointFilter = EraThreePointFilter;
    settings.EraKuwahara = std::clamp( EraKuwahara, 0, 8 );
    settings.EraColorBits = std::clamp( EraColorBits, 0, 8 );
    settings.EraDither = std::clamp( EraDither, 0.f, 1.f );
    const std::string& palette = EraPalette.GetLocalPathString();
    if( palette != m_loadedEraPalette )
    {
        m_loadedEraPalette = palette;
        m_eraPalette = !palette.empty() && EraPalette.Exists ? ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( EraPalette ) : nullptr;
    }
    settings.EraPalette = m_eraPalette && !m_eraPalette->IsLoading() && !m_eraPalette->HasLoadFailed() ? m_eraPalette : nullptr;
    settings.FXAA = FXAA;
    settings.TemporalAA = TemporalAA;
    settings.TemporalSharpness = TemporalSharpness;
    ResolveGraphs();
    for( const auto& graph : m_graphs )
    {
        if( graph.second && graph.second->IsValid() && graph.second->GetTarget() == "PostProcess" )
        {
            settings.Graphs.push_back( graph.second );
            settings.GraphBindings.emplace_back();
            if( const Moonlight::ShaderGraphParams* params = GraphParams.Find( graph.first ) )
            {
                params->Fill( settings.GraphBindings.back() );
            }
            else
            {
                graph.second->PackParameters( {}, settings.GraphBindings.back().Block );
            }
        }
    }
    return settings;
}
