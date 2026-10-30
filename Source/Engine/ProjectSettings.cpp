#include "PCH.h"
#include "ProjectSettings.h"
#include "File.h"
#include "JSON.h"
#include "Path.h"
#include <algorithm>

namespace
{
    constexpr const char* kSettingsPath = "Assets/Config/ProjectSettings.json";
}


ProjectSettings& ProjectSettings::Get()
{
    static ProjectSettings instance;
    return instance;
}


ProjectSettings::ProjectSettings()
{
    m_layerNames[0] = "Default";
    m_collisionMasks.fill( 0xFFFFFFFFu );
    NavAreaNames[0] = "Walkable";
    NavAreaNames[1] = "Not Walkable";
    NavAreaNames[2] = "Jump";
    NavAreaCosts.fill( 1.f );
    NavAreaCosts[2] = 2.f;
    NavAgentTypes.push_back( { "Humanoid", 0.5f, 2.f, 0.5f, 45.f } );
}


void ProjectSettings::Load()
{
    LoadFrom( kSettingsPath );
}


namespace
{
    ProjectSettings::BuildProfile ProfileFromJson( const json& InEntry )
    {
        ProjectSettings::BuildProfile profile;
        profile.Name = InEntry.value( "Name", profile.Name );
        profile.Configuration = InEntry.value( "Configuration", profile.Configuration );
        profile.OutputDirectory = InEntry.value( "Output", profile.OutputDirectory );
        profile.Pak = InEntry.value( "Pak", profile.Pak );
        profile.Archive = InEntry.value( "Archive", profile.Archive );
        if( InEntry.contains( "Scenes" ) && InEntry["Scenes"].is_array() )
        {
            for( const json& scene : InEntry["Scenes"] )
            {
                if( scene.is_string() )
                {
                    profile.Scenes.push_back( scene.get<std::string>() );
                }
            }
        }
        return profile;
    }


    json ProfileToJson( const ProjectSettings::BuildProfile& InProfile )
    {
        return { { "Name", InProfile.Name }, { "Configuration", InProfile.Configuration }, { "Output", InProfile.OutputDirectory },
            { "Scenes", InProfile.Scenes }, { "Pak", InProfile.Pak }, { "Archive", InProfile.Archive } };
    }
}


void ProjectSettings::LoadFrom( const std::string& InPath )
{
    const Path path( InPath );
    if( !path.Exists )
    {
        return;   // defaults (a project without settings is fine)
    }
    File file{ path };
    const std::string& contents = file.Read();
    if( contents.empty() )
    {
        return;
    }
    Deserialize( contents );
}


bool ProjectSettings::Deserialize( const std::string& InJson )
{
    const json root = json::parse( InJson, nullptr, false );
    if( root.is_discarded() || !root.is_object() )
    {
        return false;
    }
    if( root.contains( "Layers" ) && root["Layers"].is_array() )
    {
        const json& layers = root["Layers"];
        for( size_t i = 0; i < layers.size() && i < static_cast<size_t>( kLayerCount ); ++i )
        {
            if( layers[i].is_string() )
            {
                m_layerNames[i] = layers[i].get<std::string>();
            }
        }
    }
    if( root.contains( "LayerCollision" ) && root["LayerCollision"].is_array() )
    {
        const json& masks = root["LayerCollision"];
        for( size_t i = 0; i < masks.size() && i < static_cast<size_t>( kLayerCount ); ++i )
        {
            if( masks[i].is_number_unsigned() || masks[i].is_number_integer() )
            {
                m_collisionMasks[i] = masks[i].get<uint32_t>();
            }
        }
    }
    if( root.contains( "Gravity" ) && root["Gravity"].is_array() && root["Gravity"].size() == 3 )
    {
        Gravity = Vector3( root["Gravity"][0].get<float>(), root["Gravity"][1].get<float>(), root["Gravity"][2].get<float>() );
    }
    if( root.contains( "InputActions" ) && root["InputActions"].is_string() )
    {
        InputActions = root["InputActions"].get<std::string>();
    }
    if( auto terrain = root.find( "TerrainStreaming" ); terrain != root.end() && terrain->is_object() )
    {
        const auto read = [&]( const char* name, int& value, int maximum ) {
            auto found = terrain->find( name );
            if( found != terrain->end() && found->is_number_integer() ) value = static_cast<int>( std::clamp( found->get<double>(), 1.0, double( maximum ) ) );
        };
        read( "CpuBudgetMiB", TerrainCpuBudgetMiB, 4096 ); read( "GpuBudgetMiB", TerrainGpuBudgetMiB, 4096 );
        read( "CollisionBudgetMiB", TerrainCollisionBudgetMiB, 4096 ); read( "MaxLoads", TerrainMaxLoads, 64 );
    }
    if( root.contains( "NavAreas" ) && root["NavAreas"].is_array() )
    {
        const json& areas = root["NavAreas"];
        for( size_t i = 0; i < areas.size() && i < static_cast<size_t>( kNavAreaCount ); ++i )
        {
            if( areas[i].is_object() )
            {
                NavAreaNames[i] = areas[i].value( "Name", NavAreaNames[i] );
                NavAreaCosts[i] = areas[i].value( "Cost", NavAreaCosts[i] );
            }
        }
    }
    if( root.contains( "NavAgentTypes" ) && root["NavAgentTypes"].is_array() )
    {
        NavAgentTypes.clear();
        for( const json& type : root["NavAgentTypes"] )
        {
            if( type.is_object() && NavAgentTypes.size() < static_cast<size_t>( kMaxNavAgentTypes ) )
            {
                NavAgentType agent;
                agent.Name = type.value( "Name", std::string( "Agent" ) );
                agent.Radius = type.value( "Radius", agent.Radius );
                agent.Height = type.value( "Height", agent.Height );
                agent.MaxClimb = type.value( "MaxClimb", agent.MaxClimb );
                agent.MaxSlope = type.value( "MaxSlope", agent.MaxSlope );
                NavAgentTypes.push_back( agent );
            }
        }
    }
    if( auto profile = root.find( "HardwareProfile" ); profile != root.end() && profile->is_object() )
    {
        HardwareProfileName = profile->value( "Name", HardwareProfileName );
        EnforceHardwareProfile = profile->value( "Enforce", EnforceHardwareProfile );
    }
    if( root.contains( "AudioBusVolumes" ) && root["AudioBusVolumes"].is_array() )
    {
        const json& volumes = root["AudioBusVolumes"];
        for( size_t i = 0; i < volumes.size() && i < BusVolumes.size(); ++i )
        {
            if( volumes[i].is_number() )
            {
                BusVolumes[i] = volumes[i].get<float>();
            }
        }
    }
    if( root.contains( "Product" ) && root["Product"].is_object() )
    {
        const json& product = root["Product"];
        if( product.contains( "Org" ) && product["Org"].is_string() )
        {
            ProductOrg = product["Org"].get<std::string>();
        }
        if( product.contains( "Name" ) && product["Name"].is_string() )
        {
            ProductName = product["Name"].get<std::string>();
        }
        if( product.contains( "Version" ) && product["Version"].is_string() )
        {
            ProductVersion = product["Version"].get<std::string>();
        }
        if( product.contains( "Icon" ) && product["Icon"].is_string() )
        {
            ProductIcon = product["Icon"].get<std::string>();
        }
        if( product.contains( "StartupScene" ) && product["StartupScene"].is_string() )
        {
            StartupScene = product["StartupScene"].get<std::string>();
        }
        if( product.contains( "LoadingDocument" ) && product["LoadingDocument"].is_string() )
        {
            LoadingDocument = product["LoadingDocument"].get<std::string>();
        }
        if( product.contains( "Look" ) && product["Look"].is_string() )
        {
            Look = product["Look"].get<std::string>();
        }
        if( product.contains( "Splash" ) && product["Splash"].is_array() )
        {
            Splash.clear();
            for( const json& entry : product["Splash"] )
            {
                if( entry.is_object() && entry.contains( "Document" ) && entry["Document"].is_string() )
                {
                    SplashScreen splash;
                    splash.Document = entry["Document"].get<std::string>();
                    splash.Seconds = entry.contains( "Seconds" ) && entry["Seconds"].is_number() ? std::max( entry["Seconds"].get<float>(), 0.f ) : splash.Seconds;
                    Splash.push_back( std::move( splash ) );
                }
            }
        }
    }
    if( root.contains( "Build" ) && root["Build"].is_object() )
    {
        const json& build = root["Build"];
        auto readList = [&build]( const char* InKey, std::vector<std::string>& OutList )
        {
            if( build.contains( InKey ) && build[InKey].is_array() )
            {
                OutList.clear();
                for( const json& entry : build[InKey] )
                {
                    if( entry.is_string() )
                    {
                        OutList.push_back( entry.get<std::string>() );
                    }
                }
            }
        };
        readList( "Scenes", BuildScenes );
        readList( "AlwaysInclude", BuildAlwaysInclude );
        readList( "Exclude", BuildExclude );
        if( build.contains( "Profiles" ) && build["Profiles"].is_array() )
        {
            BuildProfiles.clear();
            for( const json& entry : build["Profiles"] )
            {
                if( entry.is_object() )
                {
                    BuildProfiles.push_back( ProfileFromJson( entry ) );
                }
            }
        }
    }
    return true;
}


bool ProjectSettings::ParseBuildProfile( const std::string& InJson, BuildProfile& OutProfile )
{
    const json entry = json::parse( InJson, nullptr, false );
    if( entry.is_discarded() || !entry.is_object() )
    {
        return false;
    }
    OutProfile = ProfileFromJson( entry );
    return true;
}


bool ProjectSettings::ReadBuildProfile( const std::string& InPath, BuildProfile& OutProfile )
{
    const Path path( InPath, true );
    if( !path.Exists )
    {
        return false;
    }
    File file{ path };
    return ParseBuildProfile( file.Read(), OutProfile );
}


bool ProjectSettings::WriteBuildProfile( const std::string& InPath, const BuildProfile& InProfile )
{
    File file{ Path( InPath, true ) };
    file.Write( ProfileToJson( InProfile ).dump( 4 ) );
    return Path( InPath, true ).Exists;
}


const ProjectSettings::BuildProfile* ProjectSettings::FindBuildProfile( const std::string& InName ) const
{
    for( const BuildProfile& profile : BuildProfiles )
    {
        if( InName.empty() || profile.Name == InName )
        {
            return &profile;
        }
    }
    return nullptr;
}


void ProjectSettings::Save() const
{
    SaveTo( m_savePath.empty() ? std::string( kSettingsPath ) : m_savePath );
}


void ProjectSettings::SaveTo( const std::string& InPath ) const
{
    File file{ Path( InPath, true ) };
    file.Write( Serialize() );
    ++m_saveCount;
}


std::string ProjectSettings::Serialize() const
{
    json root;
    root["Layers"] = json::array();
    for( const std::string& name : m_layerNames )
    {
        root["Layers"].push_back( name );
    }
    root["LayerCollision"] = json::array();
    for( uint32_t mask : m_collisionMasks )
    {
        root["LayerCollision"].push_back( mask );
    }
    root["Gravity"] = { Gravity.x, Gravity.y, Gravity.z };
    root["AudioBusVolumes"] = BusVolumes;
    root["InputActions"] = InputActions;
    root["TerrainStreaming"] = { { "CpuBudgetMiB", TerrainCpuBudgetMiB }, { "GpuBudgetMiB", TerrainGpuBudgetMiB },
        { "CollisionBudgetMiB", TerrainCollisionBudgetMiB }, { "MaxLoads", TerrainMaxLoads } };
    root["NavAreas"] = json::array();
    for( int i = 0; i < kNavAreaCount; ++i )
    {
        root["NavAreas"].push_back( { { "Name", NavAreaNames[i] }, { "Cost", NavAreaCosts[i] } } );
    }
    root["NavAgentTypes"] = json::array();
    for( const NavAgentType& type : NavAgentTypes )
    {
        root["NavAgentTypes"].push_back( { { "Name", type.Name }, { "Radius", type.Radius }, { "Height", type.Height }, { "MaxClimb", type.MaxClimb }, { "MaxSlope", type.MaxSlope } } );
    }
    root["HardwareProfile"] = { { "Name", HardwareProfileName }, { "Enforce", EnforceHardwareProfile } };
    root["Product"] = { { "Org", ProductOrg }, { "Name", ProductName }, { "Version", ProductVersion },
        { "Icon", ProductIcon }, { "StartupScene", StartupScene } };
    // Written only when set, so projects that never touched them keep their file as is.
    if( LoadingDocument != kDefaultLoadingDocument )
    {
        root["Product"]["LoadingDocument"] = LoadingDocument;
    }
    if( !Look.empty() )
    {
        root["Product"]["Look"] = Look;
    }
    if( !Splash.empty() )
    {
        json& splash = root["Product"]["Splash"] = json::array();
        for( const SplashScreen& entry : Splash )
        {
            splash.push_back( { { "Document", entry.Document }, { "Seconds", entry.Seconds } } );
        }
    }
    root["Build"] = { { "Scenes", BuildScenes }, { "AlwaysInclude", BuildAlwaysInclude }, { "Exclude", BuildExclude } };
    // Written only when there are any, so projects that never made one keep their file as is.
    if( !BuildProfiles.empty() )
    {
        json& profiles = root["Build"]["Profiles"] = json::array();
        for( const BuildProfile& profile : BuildProfiles )
        {
            profiles.push_back( ProfileToJson( profile ) );
        }
    }
    return root.dump( 4 );
}


const std::string& ProjectSettings::GetLayerName( int InLayer ) const
{
    static const std::string kEmpty;
    return InLayer >= 0 && InLayer < kLayerCount ? m_layerNames[InLayer] : kEmpty;
}


void ProjectSettings::SetLayerName( int InLayer, const std::string& InName )
{
    if( InLayer >= 0 && InLayer < kLayerCount )
    {
        m_layerNames[InLayer] = InName;
    }
}


std::string ProjectSettings::GetLayerLabel( int InLayer ) const
{
    const std::string& name = GetLayerName( InLayer );
    return name.empty() ? "Layer " + std::to_string( InLayer ) : std::to_string( InLayer ) + ": " + name;
}


std::string ProjectSettings::GetNavAreaLabel( int InArea ) const
{
    if( InArea < 0 || InArea >= kNavAreaCount )
    {
        return "Area " + std::to_string( InArea );
    }
    return NavAreaNames[InArea].empty() ? "Area " + std::to_string( InArea ) : NavAreaNames[InArea];
}


const ProjectSettings::NavAgentType* ProjectSettings::GetNavAgentType( int InType ) const
{
    return InType >= 1 && InType <= static_cast<int>( NavAgentTypes.size() ) ? &NavAgentTypes[InType - 1] : nullptr;
}


uint32_t ProjectSettings::GetCollisionMask( int InLayer ) const
{
    return InLayer >= 0 && InLayer < kLayerCount ? m_collisionMasks[InLayer] : 0xFFFFFFFFu;
}


bool ProjectSettings::DoLayersCollide( int InLayerA, int InLayerB ) const
{
    return InLayerB >= 0 && InLayerB < kLayerCount && ( GetCollisionMask( InLayerA ) & ( 1u << InLayerB ) ) != 0;
}


void ProjectSettings::SetLayersCollide( int InLayerA, int InLayerB, bool InCollide )
{
    if( InLayerA < 0 || InLayerA >= kLayerCount || InLayerB < 0 || InLayerB >= kLayerCount )
    {
        return;
    }
    const uint32_t bitA = 1u << InLayerA;
    const uint32_t bitB = 1u << InLayerB;
    if( InCollide )
    {
        m_collisionMasks[InLayerA] |= bitB;
        m_collisionMasks[InLayerB] |= bitA;
    }
    else
    {
        m_collisionMasks[InLayerA] &= ~bitB;
        m_collisionMasks[InLayerB] &= ~bitA;
    }
}
