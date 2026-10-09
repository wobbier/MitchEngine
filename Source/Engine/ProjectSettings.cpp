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
