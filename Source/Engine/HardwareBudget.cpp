#include "PCH.h"
#include "HardwareBudget.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Cores/AudioCore.h"
#include "Engine/ProjectSettings.h"
#include "File.h"
#include "Graphics/Texture.h"
#include "Path.h"
#include "Profiling/FrameStats.h"
#include "Renderer.h"
#include "Resource/ResourceCache.h"
#include "Utils/PlatformUtils.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>

namespace
{
    constexpr double kMiB = 1024.0 * 1024.0;
    constexpr double kNearFraction = 0.9;
    // A line is logged once it has been over for a second, and again only after it has dropped
    // clearly back under (so a value hovering at the limit doesn't flood the log).
    constexpr double kWarnAfterSeconds = 1.0;
    constexpr double kRearmFraction = 0.95;
    constexpr double kMemorySampleSeconds = 0.25;
    constexpr double kGpuSmoothing = 0.1;

    struct BudgetInfo
    {
        const char* Name;
        const char* Tooltip;
        HardwareBudget::Unit Units;
    };
    // In HardwareBudget::Budget order.
    constexpr BudgetInfo kBudgets[] = {
        { "CPU frame", "Main-thread frame time, smoothed. Measured on this machine (the editor's own work included in the editor).", HardwareBudget::Unit::Milliseconds },
        { "GPU frame", "GPU frame time, smoothed. Measured on this machine's GPU.", HardwareBudget::Unit::Milliseconds },
        { "Draw calls", "Mesh draw calls for the game's cameras and their shadows this frame (the editor's views aren't counted).", HardwareBudget::Unit::Count },
        { "Triangles", "Mesh triangles drawn for the game's cameras and their shadows this frame.", HardwareBudget::Unit::Count },
        { "Lights", "Lights in the scene this frame.", HardwareBudget::Unit::Count },
        { "Resolution", "Output size of the game's main camera (the scene view when no game camera rendered).", HardwareBudget::Unit::Pixels },
        { "Memory", "Process resident memory (in the editor that includes the editor).", HardwareBudget::Unit::Bytes },
        { "GPU memory", "Textures + render targets + meshes: what the engine uploaded (driver overhead not included).", HardwareBudget::Unit::Bytes },
        { "Textures", "Texture memory (bgfx's estimate; editor icons and thumbnails count in the editor).", HardwareBudget::Unit::Bytes },
        { "Meshes", "Vertex and index buffers of loaded meshes.", HardwareBudget::Unit::Bytes },
        { "Render targets", "Render target memory: camera targets, shadow atlases, post-processing (bgfx's estimate).", HardwareBudget::Unit::Bytes },
        { "Audio", "FMOD memory: loaded sounds, stream buffers and DSP.", HardwareBudget::Unit::Bytes },
    };
    static_assert( std::size( kBudgets ) == static_cast<size_t>( HardwareBudget::Budget::Count ), "One BudgetInfo per Budget" );
    constexpr const char* kUnifiedMemoryTooltip = "Process memory plus GPU memory: a unified-memory target shares one pool.";

#if USING( ME_IMGUI )
    // Fixed light-on-dark colours: overlays have to read over any scene.
    constexpr ImU32 kOverlayText = IM_COL32( 230, 230, 230, 255 );
    constexpr ImU32 kOverlayNear = IM_COL32( 255, 196, 64, 255 );
    constexpr ImU32 kOverlayOver = IM_COL32( 255, 96, 96, 255 );
#endif


    double NowSeconds()
    {
        return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    }


    std::string GroupThousands( uint64_t InValue )
    {
        std::string digits = std::to_string( InValue );
        for( int i = static_cast<int>( digits.size() ) - 3; i > 0; i -= 3 )
        {
            digits.insert( static_cast<size_t>( i ), "," );
        }
        return digits;
    }


    const char* GetUnitName( HardwareBudget::Unit InUnit )
    {
        switch( InUnit )
        {
        case HardwareBudget::Unit::Milliseconds:
            return "ms";
        case HardwareBudget::Unit::Bytes:
            return "bytes";
        case HardwareBudget::Unit::Pixels:
            return "pixels";
        case HardwareBudget::Unit::Count:
        default:
            return "count";
        }
    }
}


float HardwareProfile::GetCpuBudgetMs() const
{
    return CpuMs > 0.f ? CpuMs : ( TargetFps > 0.f ? 1000.f / TargetFps : 0.f );
}


float HardwareProfile::GetGpuBudgetMs() const
{
    return GpuMs > 0.f ? GpuMs : ( TargetFps > 0.f ? 1000.f / TargetFps : 0.f );
}


void HardwareProfile::Deserialize( const json& InJson )
{
    const auto readString = [&]( const char* InKey, std::string& OutValue ) {
        auto found = InJson.find( InKey );
        if( found != InJson.end() && found->is_string() ) OutValue = found->get<std::string>();
    };
    const auto readFloat = [&]( const char* InKey, float& OutValue ) {
        auto found = InJson.find( InKey );
        if( found != InJson.end() && found->is_number() ) OutValue = std::max( found->get<float>(), 0.f );
    };
    const auto readInt = []( const json& InFrom, const char* InKey, int& OutValue ) {
        auto found = InFrom.find( InKey );
        if( found != InFrom.end() && found->is_number() ) OutValue = static_cast<int>( std::clamp( found->get<double>(), 0.0, 2147483647.0 ) );
    };
    const auto readBool = []( const json& InFrom, const char* InKey, bool& OutValue ) {
        auto found = InFrom.find( InKey );
        if( found != InFrom.end() && found->is_boolean() ) OutValue = found->get<bool>();
    };

    readString( "Name", Name );
    readString( "Description", Description );
    readString( "ShaderModel", ShaderModel );
    readFloat( "TargetFps", TargetFps );
    readFloat( "CpuMs", CpuMs );
    readFloat( "GpuMs", GpuMs );
    if( auto resolution = InJson.find( "Resolution" ); resolution != InJson.end() && resolution->is_array() && resolution->size() == 2 && ( *resolution )[0].is_number() && ( *resolution )[1].is_number() )
    {
        Width = std::max( ( *resolution )[0].get<int>(), 0 );
        Height = std::max( ( *resolution )[1].get<int>(), 0 );
    }
    readBool( InJson, "UnifiedMemory", UnifiedMemory );
    readInt( InJson, "RamMiB", RamMiB );
    readInt( InJson, "VramMiB", VramMiB );
    readInt( InJson, "TextureMiB", TextureMiB );
    readInt( InJson, "MeshMiB", MeshMiB );
    readInt( InJson, "RenderTargetMiB", RenderTargetMiB );
    readInt( InJson, "AudioMiB", AudioMiB );
    readInt( InJson, "MaxDrawCalls", MaxDrawCalls );
    readInt( InJson, "MaxTriangles", MaxTriangles );
    readInt( InJson, "MaxLights", MaxLights );
    if( auto caps = InJson.find( "Caps" ); caps != InJson.end() && caps->is_object() )
    {
        readInt( *caps, "MaxTextureSize", Caps.MaxTextureSize );
        readInt( *caps, "MaxShadowResolution", Caps.MaxShadowResolution );
        readBool( *caps, "Shadows", Caps.Shadows );
        readBool( *caps, "AmbientOcclusion", Caps.AmbientOcclusion );
        readBool( *caps, "Bloom", Caps.Bloom );
        readBool( *caps, "DepthOfField", Caps.DepthOfField );
        readBool( *caps, "VolumetricFog", Caps.VolumetricFog );
        readInt( *caps, "MaxVolumetricSteps", Caps.MaxVolumetricSteps );
        readBool( *caps, "TemporalAA", Caps.TemporalAA );
        readBool( *caps, "EnvironmentLighting", Caps.EnvironmentLighting );
        readBool( *caps, "Instancing", Caps.Instancing );
    }
}


HardwareProfile::FeatureCaps HardwareProfile::Combine( const FeatureCaps& InA, const FeatureCaps& InB )
{
    const auto stricter = []( int InLeft, int InRight ) {
        return InLeft <= 0 ? std::max( InRight, 0 ) : InRight <= 0 ? InLeft : std::min( InLeft, InRight );
    };
    FeatureCaps caps;
    caps.MaxTextureSize = stricter( InA.MaxTextureSize, InB.MaxTextureSize );
    caps.MaxShadowResolution = stricter( InA.MaxShadowResolution, InB.MaxShadowResolution );
    caps.Shadows = InA.Shadows && InB.Shadows;
    caps.AmbientOcclusion = InA.AmbientOcclusion && InB.AmbientOcclusion;
    caps.Bloom = InA.Bloom && InB.Bloom;
    caps.DepthOfField = InA.DepthOfField && InB.DepthOfField;
    caps.VolumetricFog = InA.VolumetricFog && InB.VolumetricFog;
    caps.MaxVolumetricSteps = stricter( InA.MaxVolumetricSteps, InB.MaxVolumetricSteps );
    caps.TemporalAA = InA.TemporalAA && InB.TemporalAA;
    caps.EnvironmentLighting = InA.EnvironmentLighting && InB.EnvironmentLighting;
    caps.Instancing = InA.Instancing && InB.Instancing;
    return caps;
}


HardwareBudget& HardwareBudget::Get()
{
    static HardwareBudget instance;
    return instance;
}


void HardwareBudget::Init( BGFXRenderer& InRenderer )
{
    m_renderer = &InRenderer;
    const ProjectSettings& settings = ProjectSettings::Get();
    m_activeName = CommandLine::Has( "--hardware-profile" ) ? CommandLine::GetString( "--hardware-profile" ) : settings.HardwareProfileName;
    // --no-profile-enforcement wins: reference captures and regression runs pass it
    // (Tools/ScreenshotRegression.py), so a local ProjectSettings edit can't cap what they render.
    const bool enforcementOff = CommandLine::Has( "--no-profile-enforcement" );
    m_enforced = !enforcementOff && ( CommandLine::Has( "--enforce-profile" ) || settings.EnforceHardwareProfile );
    ReloadProfiles();
    // Always one line, so a run's log says which caps it rendered under.
    if( m_activeName.empty() )
    {
        CLog::Log( CLog::LogType::Info, "Hardware profile: none" );
        return;
    }
    if( GetActiveProfile() )
    {
        const bool asked = CommandLine::Has( "--enforce-profile" ) || settings.EnforceHardwareProfile;
        CLog::Log( CLog::LogType::Info, "Hardware profile: " + m_activeName
            + ( m_enforced ? " (enforced)" : enforcementOff && asked ? " (not enforced: --no-profile-enforcement)" : "" ) );
    }
    else
    {
        BRUH( "Hardware profile '" + m_activeName + "' isn't defined in " + kProjectProfilesPath + " or " + kEngineProfilesPath );
    }
}


void HardwareBudget::ReloadProfiles()
{
    m_profiles.clear();
    m_qualityPresets.clear();
    const Path engineFile( kEngineProfilesPath );
    const Path projectFile( kProjectProfilesPath );
    LoadProfilesFile( engineFile );
    // Without a project file (or running from the engine itself) both name the same file.
    if( projectFile.FullPath != engineFile.FullPath )
    {
        LoadProfilesFile( projectFile );
    }
    RebuildLines();
    ApplyCaps();
}


void HardwareBudget::LoadProfilesFile( const Path& InPath )
{
    if( !InPath.Exists )
    {
        return;
    }
    File file{ InPath };
    if( !ParseProfiles( json::parse( file.Read(), nullptr, false ), InPath.GetLocalPathString() ) )
    {
        YIKES( "Hardware profiles: can't read " + InPath.GetLocalPathString() + " (expected { \"Profiles\": [ ... ] })" );
    }
}


bool HardwareBudget::AddProfiles( const json& InRoot, const std::string& InSource )
{
    if( !ParseProfiles( InRoot, InSource ) )
    {
        return false;
    }
    RebuildLines();
    ApplyCaps();
    return true;
}


bool HardwareBudget::ParseProfiles( const json& InRoot, const std::string& InSource )
{
    const json* list = InRoot.is_array() ? &InRoot : nullptr;
    if( InRoot.is_object() && InRoot.contains( "Profiles" ) && InRoot["Profiles"].is_array() )
    {
        list = &InRoot["Profiles"];
    }
    if( InRoot.is_object() )
    {
        if( auto presets = InRoot.find( "QualityPresets" ); presets != InRoot.end() && presets->is_array() )
        {
            ParseQualityPresets( *presets, InSource );
            // A document of presets alone is still a profiles document.
            static const json kNoProfiles = json::array();
            list = list ? list : &kNoProfiles;
        }
    }
    if( !list )
    {
        return false;
    }
    for( const json& entry : *list )
    {
        const std::string name = entry.is_object() ? entry.value( "Name", std::string() ) : std::string();
        if( name.empty() )
        {
            continue;
        }
        HardwareProfile profile;
        if( auto base = entry.find( "Extends" ); base != entry.end() && base->is_string() )
        {
            auto found = std::find_if( m_profiles.begin(), m_profiles.end(), [&]( const HardwareProfile& InProfile ) { return InProfile.Name == base->get<std::string>(); } );
            if( found != m_profiles.end() )
            {
                profile = *found;
            }
            else
            {
                BRUH( "Hardware profile '" + name + "' (" + InSource + ") extends '" + base->get<std::string>() + "', which isn't defined before it" );
            }
        }
        profile.Deserialize( entry );
        auto existing = std::find_if( m_profiles.begin(), m_profiles.end(), [&]( const HardwareProfile& InProfile ) { return InProfile.Name == name; } );
        if( existing != m_profiles.end() )
        {
            *existing = std::move( profile );
        }
        else
        {
            m_profiles.push_back( std::move( profile ) );
        }
    }
    return true;
}


void HardwareBudget::ParseQualityPresets( const json& InList, const std::string& InSource )
{
    for( const json& entry : InList )
    {
        const std::string name = entry.is_object() ? entry.value( "Name", std::string() ) : std::string();
        if( name.empty() )
        {
            continue;
        }
        // Presets share the profiles' "Caps" reader.
        HardwareProfile reader;
        if( auto base = entry.find( "Extends" ); base != entry.end() && base->is_string() )
        {
            if( const QualityPreset* found = FindQualityPreset( base->get<std::string>() ) )
            {
                reader.Caps = found->Caps;
            }
            else
            {
                BRUH( "Quality preset '" + name + "' (" + InSource + ") extends '" + base->get<std::string>() + "', which isn't defined before it" );
            }
        }
        reader.Deserialize( entry );
        QualityPreset preset{ name, reader.Description, reader.Caps };
        auto existing = std::find_if( m_qualityPresets.begin(), m_qualityPresets.end(), [&]( const QualityPreset& InPreset ) { return InPreset.Name == name; } );
        if( existing != m_qualityPresets.end() )
        {
            *existing = std::move( preset );
        }
        else
        {
            m_qualityPresets.push_back( std::move( preset ) );
        }
    }
}


const QualityPreset* HardwareBudget::FindQualityPreset( const std::string& InName ) const
{
    auto found = std::find_if( m_qualityPresets.begin(), m_qualityPresets.end(), [&]( const QualityPreset& InPreset ) { return InPreset.Name == InName; } );
    return found != m_qualityPresets.end() ? &*found : nullptr;
}


bool HardwareBudget::SetQualityPreset( const std::string& InName )
{
    const bool known = InName.empty() || FindQualityPreset( InName ) != nullptr;
    if( InName != m_qualityName )
    {
        m_qualityName = InName;
        ApplyCaps();
        CLog::Log( CLog::LogType::Info, "Quality preset: " + ( InName.empty() ? std::string( "none" ) : InName ) + ( known ? "" : " (not defined)" ) );
    }
    return known;
}


HardwareProfile::FeatureCaps HardwareBudget::GetEffectiveCaps() const
{
    const HardwareProfile* profile = m_enforced ? GetActiveProfile() : nullptr;
    const QualityPreset* quality = m_qualityName.empty() ? nullptr : FindQualityPreset( m_qualityName );
    const HardwareProfile::FeatureCaps none;
    return HardwareProfile::Combine( profile ? profile->Caps : none, quality ? quality->Caps : none );
}


const HardwareProfile* HardwareBudget::GetActiveProfile() const
{
    if( m_activeName.empty() )
    {
        return nullptr;
    }
    auto found = std::find_if( m_profiles.begin(), m_profiles.end(), [&]( const HardwareProfile& InProfile ) { return InProfile.Name == m_activeName; } );
    return found != m_profiles.end() ? &*found : nullptr;
}


void HardwareBudget::SetActiveProfile( const std::string& InName )
{
    if( InName == m_activeName )
    {
        return;
    }
    m_activeName = InName;
    RebuildLines();
    ApplyCaps();
}


void HardwareBudget::SetEnforced( bool InEnforced )
{
    if( InEnforced == m_enforced )
    {
        return;
    }
    m_enforced = InEnforced;
    ApplyCaps();
}


void HardwareBudget::ApplyCaps()
{
    const HardwareProfile::FeatureCaps caps = GetEffectiveCaps();
    if( m_renderer )
    {
        BGFXRenderer::FeatureCapSettings& out = m_renderer->FeatureCaps;
        out.Shadows = caps.Shadows;
        out.MaxShadowResolution = static_cast<uint16_t>( std::clamp( caps.MaxShadowResolution, 0, 16384 ) );
        out.EnvironmentLighting = caps.EnvironmentLighting;
        out.AmbientOcclusion = caps.AmbientOcclusion;
        out.Bloom = caps.Bloom;
        out.DepthOfField = caps.DepthOfField;
        out.VolumetricFog = caps.VolumetricFog;
        out.MaxVolumetricSteps = std::max( caps.MaxVolumetricSteps, 0 );
        out.TemporalAA = caps.TemporalAA;
        out.Instancing = caps.Instancing;
    }
    const uint32_t textureCap = static_cast<uint32_t>( caps.MaxTextureSize );
    if( textureCap != Moonlight::Texture::GetMaxUploadSize() )
    {
        Moonlight::Texture::SetMaxUploadSize( textureCap );
        ReloadTextures();
    }
}


void HardwareBudget::ReloadTextures()
{
    // File textures with a compiled image; render targets and generated textures have none.
    std::vector<SharedPtr<Moonlight::Texture>> textures;
    for( const auto& entry : ResourceCache::GetInstance().GetResouceStack() )
    {
        SharedPtr<Moonlight::Texture> texture = std::dynamic_pointer_cast<Moonlight::Texture>( entry.second );
        if( texture && !texture->IsLoading() && !texture->HasLoadFailed() && Path( texture->GetPath().FullPath + ".dds" ).Exists )
        {
            textures.push_back( std::move( texture ) );
        }
    }
    for( const SharedPtr<Moonlight::Texture>& texture : textures )
    {
        texture->Reload();
    }
    if( !textures.empty() )
    {
        CLog::Log( CLog::LogType::Info, "Hardware profile: reloaded " + std::to_string( textures.size() ) + " textures for the texture size cap" );
    }
}


void HardwareBudget::RebuildLines()
{
    m_lines.clear();
    const HardwareProfile* profile = GetActiveProfile();
    if( profile )
    {
        const auto add = [&]( Budget InId, double InLimit ) -> Line* {
            if( InLimit <= 0.0 )
            {
                return nullptr;
            }
            const BudgetInfo& info = kBudgets[static_cast<size_t>( InId )];
            Line line;
            line.Id = InId;
            line.Name = info.Name;
            line.Tooltip = info.Tooltip;
            line.Units = info.Units;
            line.Limit = InLimit;
            m_lines.push_back( line );
            return &m_lines.back();
        };
        add( Budget::Cpu, profile->GetCpuBudgetMs() );
        add( Budget::Gpu, profile->GetGpuBudgetMs() );
        add( Budget::DrawCalls, profile->MaxDrawCalls );
        add( Budget::Triangles, profile->MaxTriangles );
        add( Budget::Lights, profile->MaxLights );
        if( profile->Width > 0 && profile->Height > 0 )
        {
            Line* resolution = add( Budget::Resolution, double( profile->Width ) * double( profile->Height ) );
            resolution->LimitWidth = static_cast<uint32_t>( profile->Width );
            resolution->LimitHeight = static_cast<uint32_t>( profile->Height );
        }
        if( Line* memory = add( Budget::Memory, profile->RamMiB * kMiB ); memory && profile->UnifiedMemory )
        {
            memory->Name = "Memory (shared)";
            memory->Tooltip = kUnifiedMemoryTooltip;
        }
        if( !profile->UnifiedMemory )
        {
            add( Budget::GpuMemory, profile->VramMiB * kMiB );
        }
        add( Budget::Textures, profile->TextureMiB * kMiB );
        add( Budget::Meshes, profile->MeshMiB * kMiB );
        add( Budget::RenderTargets, profile->RenderTargetMiB * kMiB );
        add( Budget::Audio, profile->AudioMiB * kMiB );
    }
    m_watches.assign( m_lines.size(), LineWatch() );
    m_smoothedGpu = 0.0;
    m_nextMemorySample = 0.0;
}


void HardwareBudget::SampleMemory()
{
    m_processBytes = PlatformUtils::GetProcessMemoryBytes();
    m_audioBytes = AudioCore::GetMemoryUsage();
}


void HardwareBudget::Update()
{
    const HardwareProfile* profile = GetActiveProfile();
    if( !profile || m_lines.empty() )
    {
        return;
    }
    const double now = NowSeconds();
    if( now >= m_nextMemorySample )
    {
        SampleMemory();
        m_nextMemorySample = now + kMemorySampleSeconds;
    }

    const FrameStats& stats = FrameStats::Get();
    const FrameStats::RenderStats& render = stats.GetRenderStats();
    m_smoothedGpu = m_smoothedGpu <= 0.0 ? render.GpuMilliseconds : m_smoothedGpu + ( render.GpuMilliseconds - m_smoothedGpu ) * kGpuSmoothing;
    // GPU memory is what the engine uploaded, not the driver's number: drivers on shared-memory
    // devices (integrated GPUs, lavapipe) report the whole heap's use, other processes included.
    const double gpuMemory = double( render.TextureMemoryUsed + render.RenderTargetMemoryUsed + render.MeshMemoryUsed );

    for( size_t i = 0; i < m_lines.size(); ++i )
    {
        Line& line = m_lines[i];
        switch( line.Id )
        {
        case Budget::Cpu:
            line.Used = stats.GetSmoothedFrameMilliseconds();
            break;
        case Budget::Gpu:
            line.Used = m_smoothedGpu;
            break;
        case Budget::DrawCalls:
            line.Used = render.SceneDrawCalls;
            break;
        case Budget::Triangles:
            line.Used = double( render.SceneTriangles );
            break;
        case Budget::Lights:
            line.Used = render.Lights;
            break;
        case Budget::Resolution:
            line.Width = render.SceneWidth;
            line.Height = render.SceneHeight;
            line.Used = double( line.Width ) * double( line.Height );
            break;
        case Budget::Memory:
            line.Used = double( m_processBytes ) + ( profile->UnifiedMemory ? gpuMemory : 0.0 );
            break;
        case Budget::GpuMemory:
            line.Used = gpuMemory;
            break;
        case Budget::Textures:
            line.Used = double( render.TextureMemoryUsed );
            break;
        case Budget::Meshes:
            line.Used = double( render.MeshMemoryUsed );
            break;
        case Budget::RenderTargets:
            line.Used = double( render.RenderTargetMemoryUsed );
            break;
        case Budget::Audio:
            line.Used = double( m_audioBytes );
            break;
        default:
            break;
        }
        line.Peak = std::max( line.Peak, line.Used );
        line.State = line.Used > line.Limit ? Status::Over : ( line.Used >= line.Limit * kNearFraction ? Status::Near : Status::Ok );

        LineWatch& watch = m_watches[i];
        if( line.State == Status::Over )
        {
            if( watch.OverSince < 0.0 )
            {
                watch.OverSince = now;
            }
            if( !watch.Warned && now - watch.OverSince >= kWarnAfterSeconds )
            {
                watch.Warned = true;
                BRUH( "[Budget] " + profile->Name + ": " + line.Name + " " + FormatUsed( line ) + " is over " + FormatLimit( line ) );
            }
        }
        else
        {
            watch.OverSince = -1.0;
            if( line.Used < line.Limit * kRearmFraction )
            {
                watch.Warned = false;
            }
        }
    }
}


int HardwareBudget::GetOverCount() const
{
    return static_cast<int>( std::count_if( m_lines.begin(), m_lines.end(), []( const Line& InLine ) { return InLine.State == Status::Over; } ) );
}


void HardwareBudget::ResetPeaks()
{
    for( Line& line : m_lines )
    {
        line.Peak = line.Used;
    }
}


json HardwareBudget::ToJson() const
{
    json out;
    out["Profile"] = m_activeName;
    out["Defined"] = GetActiveProfile() != nullptr;
    out["Enforced"] = m_enforced;
    out["Quality"] = m_qualityName;
    out["OverCount"] = GetOverCount();
    int peakOver = 0;
    json lines = json::array();
    for( const Line& line : m_lines )
    {
        json entry = { { "Name", line.Name }, { "Unit", GetUnitName( line.Units ) }, { "Used", line.Used }, { "Peak", line.Peak },
            { "Limit", line.Limit }, { "Status", GetStatusName( line.State ) }, { "PeakOver", line.Peak > line.Limit } };
        if( line.Units == Unit::Pixels )
        {
            entry["Size"] = { line.Width, line.Height };
            entry["LimitSize"] = { line.LimitWidth, line.LimitHeight };
        }
        peakOver += line.Peak > line.Limit ? 1 : 0;
        lines.push_back( std::move( entry ) );
    }
    out["PeakOverCount"] = peakOver;
    out["Lines"] = std::move( lines );
    return out;
}


std::string HardwareBudget::FormatValue( const Line& InLine, double InValue )
{
    char text[64];
    switch( InLine.Units )
    {
    case Unit::Milliseconds:
        std::snprintf( text, sizeof( text ), "%.1f ms", InValue );
        break;
    case Unit::Bytes:
        if( InValue >= 1024.0 * kMiB )
        {
            std::snprintf( text, sizeof( text ), "%.2f GiB", InValue / ( 1024.0 * kMiB ) );
        }
        else
        {
            std::snprintf( text, sizeof( text ), "%.1f MiB", InValue / kMiB );
        }
        break;
    case Unit::Pixels:
        std::snprintf( text, sizeof( text ), "%.2f MP", InValue / 1000000.0 );
        break;
    case Unit::Count:
    default:
        return GroupThousands( static_cast<uint64_t>( std::max( InValue, 0.0 ) + 0.5 ) );
    }
    return text;
}


std::string HardwareBudget::FormatUsed( const Line& InLine )
{
    if( InLine.Units == Unit::Pixels )
    {
        return std::to_string( InLine.Width ) + "x" + std::to_string( InLine.Height );
    }
    return FormatValue( InLine, InLine.Used );
}


std::string HardwareBudget::FormatLimit( const Line& InLine )
{
    if( InLine.Units == Unit::Pixels )
    {
        return std::to_string( InLine.LimitWidth ) + "x" + std::to_string( InLine.LimitHeight );
    }
    return FormatValue( InLine, InLine.Limit );
}


const char* HardwareBudget::GetStatusName( Status InStatus )
{
    switch( InStatus )
    {
    case Status::Near:
        return "Near";
    case Status::Over:
        return "Over";
    case Status::Ok:
    default:
        return "Ok";
    }
}


#if USING( ME_IMGUI )
float HardwareBudget::DrawOverlay( ImDrawList* InDrawList, float InX, float InY, bool InProblemsOnly, bool InAlignRight ) const
{
    const HardwareProfile* profile = GetActiveProfile();
    if( !InDrawList || !profile )
    {
        return 0.f;
    }
    std::vector<const Line*> shown;
    std::vector<std::string> values;
    float nameWidth = 0.f;
    float valueWidth = 0.f;
    for( const Line& line : m_lines )
    {
        if( InProblemsOnly && line.State == Status::Ok )
        {
            continue;
        }
        shown.push_back( &line );
        values.push_back( FormatUsed( line ) + " / " + FormatLimit( line ) );
        nameWidth = std::max( nameWidth, ImGui::CalcTextSize( line.Name ).x );
        valueWidth = std::max( valueWidth, ImGui::CalcTextSize( values.back().c_str() ).x );
    }
    const int over = GetOverCount();
    std::string header = profile->Name + ( m_enforced ? " (enforced)" : "" );
    if( InProblemsOnly )
    {
        header += shown.empty() ? ": within budget" : ( over > 0 ? ": " + std::to_string( over ) + " over budget" : ": near budget" );
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 padding = style.FramePadding;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float gap = ImGui::CalcTextSize( "  " ).x;
    const float width = std::max( ImGui::CalcTextSize( header.c_str() ).x, shown.empty() ? 0.f : nameWidth + gap + valueWidth );
    const float height = lineHeight * static_cast<float>( 1 + shown.size() ) + padding.y * 2.f;
    if( InAlignRight )
    {
        InX -= width + padding.x * 2.f;
    }
    InDrawList->AddRectFilled( ImVec2( InX, InY ), ImVec2( InX + width + padding.x * 2.f, InY + height ), IM_COL32( 0, 0, 0, 140 ), style.FrameRounding );
    float y = InY + padding.y;
    InDrawList->AddText( ImVec2( InX + padding.x, y ), over > 0 ? kOverlayOver : kOverlayText, header.c_str() );
    for( size_t i = 0; i < shown.size(); ++i )
    {
        y += lineHeight;
        const ImU32 color = shown[i]->State == Status::Over ? kOverlayOver : ( shown[i]->State == Status::Near ? kOverlayNear : kOverlayText );
        InDrawList->AddText( ImVec2( InX + padding.x, y ), color, shown[i]->Name );
        InDrawList->AddText( ImVec2( InX + padding.x + nameWidth + gap, y ), color, values[i].c_str() );
    }
    return height;
}
#endif
