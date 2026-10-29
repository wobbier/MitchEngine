#include "PCH.h"
#include "LookPresets.h"
#include "CLog.h"
#include "Components/Camera.h"
#include "Components/Graphics/Mesh.h"
#include "Components/Graphics/PostProcess.h"
#include "ECS/Entity.h"
#include "ECS/EntityHandle.h"
#include "Engine/World.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "VFS/VFS.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Equal as a component would store them: numbers compare as floats (0.4f is not 0.4).
    bool SameValue( const json& InA, const json& InB )
    {
        if( InA.is_number() && InB.is_number() )
        {
            return std::abs( InA.get<float>() - InB.get<float>() ) <= 1e-6f * std::max( 1.f, std::abs( InA.get<float>() ) );
        }
        if( InA.is_array() && InB.is_array() && InA.size() == InB.size() )
        {
            for( size_t i = 0; i < InA.size(); ++i )
            {
                if( !SameValue( InA[i], InB[i] ) )
                {
                    return false;
                }
            }
            return true;
        }
        return InA == InB;
    }


    // Merges InPatch's keys onto the component's serialized form and loads it back.
    template<typename T>
    bool MergeComponent( T& InComponent, const json& InPatch )
    {
        if( !InPatch.is_object() || InPatch.empty() )
        {
            return false;
        }
        json state;
        InComponent.Serialize( state );
        bool differs = false;
        for( const auto& [key, value] : InPatch.items() )
        {
            differs |= !state.contains( key ) || !SameValue( state[key], value );
            state[key] = value;
        }
        if( !differs )
        {
            return false;
        }
        InComponent.Deserialize( state );
        return true;
    }
}


LookPresets& LookPresets::Get()
{
    static LookPresets instance;
    return instance;
}


void LookPresets::Load()
{
    m_presets.clear();
    for( const char* path : { "Engine/Assets/Config/LookPresets.json", "Assets/Config/LookPresets.json" } )
    {
        std::string text;
        if( VFS::Get().ReadText( Path( path ), text ) )
        {
            const json root = json::parse( text, nullptr, false );
            if( !AddPresets( root ) )
            {
                YIKES( std::string( "Look presets: " ) + path + " isn't a { \"Presets\": [...] } file" );
            }
        }
    }
}


bool LookPresets::AddPresets( const json& InRoot )
{
    if( !InRoot.is_object() || !InRoot.contains( "Presets" ) || !InRoot["Presets"].is_array() )
    {
        return false;
    }
    for( const json& entry : InRoot["Presets"] )
    {
        if( !entry.is_object() || !entry.contains( "Name" ) )
        {
            continue;
        }
        Preset preset;
        preset.Name = entry.value( "Name", std::string() );
        preset.Description = entry.value( "Description", std::string() );
        preset.Camera = entry.value( "Camera", json::object() );
        preset.Post = entry.value( "Post", json::object() );
        preset.Material = entry.value( "Material", json() );
        preset.Import = entry.value( "Import", json::object() );
        preset.Lite = entry.value( "Lite", false );
        if( entry.contains( "Caps" ) )
        {
            HardwareProfile profile;
            profile.Deserialize( json{ { "Name", preset.Name }, { "Caps", entry["Caps"] } } );
            preset.Caps = profile.Caps;
        }
        auto existing = std::find_if( m_presets.begin(), m_presets.end(), [&]( const Preset& InPreset ) { return InPreset.Name == preset.Name; } );
        if( existing != m_presets.end() )
        {
            *existing = std::move( preset );
        }
        else
        {
            m_presets.push_back( std::move( preset ) );
        }
    }
    return true;
}


const LookPresets::Preset* LookPresets::Find( const std::string& InName ) const
{
    auto found = std::find_if( m_presets.begin(), m_presets.end(), [&]( const Preset& InPreset ) { return InPreset.Name == InName; } );
    return found != m_presets.end() ? &*found : nullptr;
}


HardwareProfile::FeatureCaps LookPresets::LiteCaps()
{
    HardwareProfile::FeatureCaps caps;
    caps.Shadows = false;
    caps.AmbientOcclusion = false;
    caps.EnvironmentLighting = false;
    caps.VolumetricFog = false;
    caps.DepthOfField = false;
    caps.TemporalAA = false;
    return caps;
}


HardwareProfile::FeatureCaps LookPresets::CapsOf( const Preset& InPreset )
{
    return InPreset.Lite ? HardwareProfile::Combine( InPreset.Caps, LiteCaps() ) : InPreset.Caps;
}


bool LookPresets::ApplyToEntity( const Preset& InPreset, Entity& InEntity, bool InMaterials )
{
    bool changed = false;
    if( Camera* camera = InEntity.TryGetComponent<Camera>() )
    {
        changed |= MergeComponent( *camera, InPreset.Camera );
        if( !InPreset.Post.empty() )
        {
            PostProcess* post = InEntity.TryGetComponent<PostProcess>();
            if( !post )
            {
                post = &InEntity.AddComponent<PostProcess>();
                changed = true;
            }
            changed |= MergeComponent( *post, InPreset.Post );
        }
    }
    if( InMaterials && InPreset.Material.is_object() )
    {
        if( Mesh* mesh = InEntity.TryGetComponent<Mesh>() )
        {
            if( mesh->MeshMaterial && mesh->MeshMaterial->GetTypeName() == "StandardMaterial" )
            {
                // A graph material keeps the mesh's colour as its Base Color (unless the look sets one).
                json material = InPreset.Material;
                json current;
                mesh->Serialize( current );
                const json diffuse = current.contains( "Material" ) ? current["Material"].value( "DiffuseColor", json() ) : json();
                if( material.value( "Type", std::string() ) == "ShaderGraphMaterial" && diffuse.is_array() && diffuse.size() >= 3 )
                {
                    json& params = material["Params"];
                    if( !params.is_object() )
                    {
                        params = json::object();
                    }
                    if( !params.contains( "Base Color" ) )
                    {
                        params["Base Color"] = { diffuse[0], diffuse[1], diffuse[2], 1.0 };
                    }
                }
                changed |= MergeComponent( *mesh, json{ { "Material", material } } );
            }
        }
    }
    return changed;
}


int LookPresets::ApplyToWorld( const Preset& InPreset, World& InWorld, bool InMaterials )
{
    // Collected first: adding a PostProcess inside the iteration would be deferred.
    std::vector<EntityHandle> entities;
    InWorld.ForEachEntity( [&entities]( Entity& InEntity ) { entities.push_back( InEntity.GetHandle() ); } );
    int changed = 0;
    for( EntityHandle& handle : entities )
    {
        if( Entity* entity = handle.Get() )
        {
            changed += ApplyToEntity( InPreset, *entity, InMaterials ) ? 1 : 0;
        }
    }
    return changed;
}


void LookPresets::ApplyProjectDefaults( const Preset* InPreset )
{
    const json import = InPreset ? InPreset->Import : json::object();
    TextureResourceMetadata::s_defaultUncompressed = import.value( "Format", std::string( "Auto" ) ) == "None";
    TextureResourceMetadata::s_defaultMips = import.value( "MIPs", true );
    HardwareBudget::Get().SetLookCaps( InPreset ? CapsOf( *InPreset ) : HardwareProfile::FeatureCaps() );
}
