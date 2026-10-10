#include "PCH.h"
#include "ProjectSettings.h"
#include "File.h"
#include "JSON.h"
#include "Path.h"

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
    const Path path( kSettingsPath );
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
    const json root = json::parse( contents, nullptr, false );
    if( root.is_discarded() || !root.is_object() )
    {
        return;
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
}


void ProjectSettings::Save() const
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
    File file{ Path( kSettingsPath ) };
    file.Write( root.dump( 4 ) );
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
