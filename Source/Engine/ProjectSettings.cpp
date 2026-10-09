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
}


void ProjectSettings::Load()
{
    File file{ Path( kSettingsPath ) };
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
}


void ProjectSettings::Save() const
{
    json root;
    root["Layers"] = json::array();
    for( const std::string& name : m_layerNames )
    {
        root["Layers"].push_back( name );
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
