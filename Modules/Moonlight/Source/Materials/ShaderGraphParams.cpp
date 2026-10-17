#include "ShaderGraphParams.h"
#include "Materials/ShaderGraphAsset.h"
#include "Graphics/Texture.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include <algorithm>
#include <filesystem>

#if USING( ME_TOOLS )
#include "Events/EditorEvents.h"
#include "Utils/HavanaUtils.h"
#include <imgui.h>
#endif

static_assert( Moonlight::kShaderGraphMaxParameters == Moonlight::ShaderGraphAsset::kMaxParameters, "ShaderGraphBinding and ShaderGraphAsset disagree on the parameter block size" );

namespace
{
    json VectorToJson( const Vector4& InValue )
    {
        return json::array( { InValue.x, InValue.y, InValue.z, InValue.w } );
    }


    Vector4 VectorFromJson( const json& InJson )
    {
        Vector4 result;
        if( InJson.is_number() )
        {
            result.x = InJson.get<float>();
        }
        else if( InJson.is_array() )
        {
            float* components[4] = { &result.x, &result.y, &result.z, &result.w };
            for( size_t i = 0; i < InJson.size() && i < 4; ++i )
            {
                *components[i] = InJson[i].is_number() ? InJson[i].get<float>() : 0.f;
            }
        }
        return result;
    }

#if USING( ME_TOOLS )
    // Texture pickers answer on a later frame, after the inspector that asked may have moved: results
    // are keyed by widget ID instead of capturing the params object.
    std::map<ImGuiID, std::string>& PickedTextures()
    {
        static std::map<ImGuiID, std::string> results;
        return results;
    }
#endif
}

namespace Moonlight
{
    Vector4 ShaderGraphParams::Get( const std::string& InName ) const
    {
        auto found = Values.find( InName );
        if( found != Values.end() )
        {
            return found->second;
        }
        const ShaderGraphAsset::Parameter* parameter = Asset ? Asset->FindParameter( InName ) : nullptr;
        return parameter ? parameter->Default : Vector4();
    }


    void ShaderGraphParams::Set( const std::string& InName, const Vector4& InValue )
    {
        Values[InName] = InValue;
    }


    void ShaderGraphParams::Reset( const std::string& InName )
    {
        Values.erase( InName );
    }


    void ShaderGraphParams::SetTexture( const std::string& InName, const std::string& InPath )
    {
        m_textures.erase( InName );
        if( InPath.empty() )
        {
            TexturePaths.erase( InName );
            return;
        }
        TexturePaths[InName] = InPath;
        m_textures[InName] = ResourceCache::GetInstance().GetAsync<Texture>( Path( InPath ) );
    }


    void ShaderGraphParams::Fill( ShaderGraphBinding& OutBinding ) const
    {
        if( Asset )
        {
            Asset->PackParameters( Values, OutBinding.Block );
        }
        else
        {
            for( Vector4& value : OutBinding.Block )
            {
                value = Vector4();
            }
        }
        OutBinding.Textures = m_textures;
    }


    void ShaderGraphParams::ToJson( json& OutJson ) const
    {
        OutJson = json::object();
        json& parameters = OutJson["Params"];
        parameters = json::object();
        for( const auto& entry : Values )
        {
            parameters[entry.first] = VectorToJson( entry.second );
        }
        json& textures = OutJson["Textures"];
        textures = json::object();
        for( const auto& entry : TexturePaths )
        {
            textures[entry.first] = entry.second;
        }
    }


    void ShaderGraphParams::FromJson( const json& InJson )
    {
        Values.clear();
        TexturePaths.clear();
        m_textures.clear();
        if( !InJson.is_object() )
        {
            return;
        }
        if( InJson.contains( "Params" ) && InJson["Params"].is_object() )
        {
            for( auto it = InJson["Params"].begin(); it != InJson["Params"].end(); ++it )
            {
                Values[it.key()] = VectorFromJson( it.value() );
            }
        }
        if( InJson.contains( "Textures" ) && InJson["Textures"].is_object() )
        {
            for( auto it = InJson["Textures"].begin(); it != InJson["Textures"].end(); ++it )
            {
                if( it.value().is_string() )
                {
                    SetTexture( it.key(), it.value().get<std::string>() );
                }
            }
        }
    }


#if USING( ME_TOOLS )
    bool ShaderGraphParams::OnEditorInspect( bool InAfterLabel )
    {
        if( !Asset )
        {
            ImGui::TextDisabled( "No graph" );
            return false;
        }
        if( !Asset->IsValid() )
        {
            ImGui::TextDisabled( "Not compiled yet: open and save the graph." );
            return false;
        }
        bool anyExposed = false;
        for( const ShaderGraphAsset::Parameter& parameter : Asset->GetParameters() )
        {
            anyExposed |= parameter.Exposed;
        }
        for( const ShaderGraphAsset::TextureSlot& texture : Asset->GetTextures() )
        {
            anyExposed |= texture.Exposed;
        }
        if( !anyExposed )
        {
            ImGui::TextDisabled( "No exposed properties" );
            ImGui::SetItemTooltip( "Tick Exposed on a property in the graph's blackboard to set it here." );
            return false;
        }
        if( InAfterLabel )
        {
            ImGui::NewLine();
        }

        bool changed = false;
        const float resetWidth = ImGui::CalcTextSize( "Reset" ).x + ImGui::GetStyle().FramePadding.x * 2.f + ImGui::GetStyle().ItemSpacing.x;
        for( const ShaderGraphAsset::Parameter& parameter : Asset->GetParameters() )
        {
            if( !parameter.Exposed )
            {
                continue;
            }
            ImGui::PushID( parameter.Name.c_str() );
            Vector4 value = Get( parameter.Name );
            const bool overridden = Values.count( parameter.Name ) > 0;
            bool edited = false;
            const float width = HavanaUtils::Label( parameter.Name );
            ImGui::SetNextItemWidth( overridden ? std::max( width - resetWidth, 40.f ) : width );
            if( parameter.Type == "Bool" )
            {
                bool enabled = value.x != 0.f;
                if( ImGui::Checkbox( "##Value", &enabled ) )
                {
                    value.x = enabled ? 1.f : 0.f;
                    edited = true;
                }
            }
            else if( parameter.IsColor && parameter.Components >= 3 )
            {
                const ImGuiColorEditFlags flags = parameter.IsHDR ? ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float : 0;
                edited = parameter.Components == 4 ? ImGui::ColorEdit4( "##Value", &value.x, flags ) : ImGui::ColorEdit3( "##Value", &value.x, flags );
            }
            else if( parameter.Components == 1 && parameter.Min != parameter.Max )
            {
                edited = ImGui::SliderFloat( "##Value", &value.x, parameter.Min, parameter.Max );
            }
            else
            {
                edited = ImGui::DragScalarN( "##Value", ImGuiDataType_Float, &value.x, parameter.Components, 0.01f );
            }
            if( edited )
            {
                Set( parameter.Name, value );
                changed = true;
            }
            if( overridden )
            {
                ImGui::SameLine();
                if( ImGui::SmallButton( "Reset" ) )
                {
                    Reset( parameter.Name );
                    changed = true;
                }
                ImGui::SetItemTooltip( "Back to the graph's default" );
            }
            ImGui::PopID();
        }

        for( const ShaderGraphAsset::TextureSlot& texture : Asset->GetTextures() )
        {
            if( !texture.Exposed )
            {
                continue;
            }
            ImGui::PushID( texture.Name.c_str() );
            auto overridden = TexturePaths.find( texture.Name );
            const bool isOverridden = overridden != TexturePaths.end();
            const std::string current = isOverridden ? overridden->second : ( texture.Asset.empty() ? std::string( "(default)" ) : texture.Asset );
            const float width = HavanaUtils::Label( texture.Name );
            const ImGuiID pickerId = ImGui::GetID( "TexturePicker" );
            if( ImGui::Button( ( current + "##Texture" ).c_str(), ImVec2( isOverridden ? std::max( width - resetWidth, 40.f ) : width, 0.f ) ) )
            {
                RequestAssetSelectionEvent evt( [pickerId]( Path InSelected ) {
                    PickedTextures()[pickerId] = InSelected.GetLocalPathString();
                }, AssetType::Texture );
                evt.Fire();
            }
            auto picked = PickedTextures().find( pickerId );
            if( picked != PickedTextures().end() )
            {
                SetTexture( texture.Name, picked->second );
                PickedTextures().erase( picked );
                changed = true;
            }
            if( isOverridden )
            {
                ImGui::SameLine();
                if( ImGui::SmallButton( "Reset" ) )
                {
                    SetTexture( texture.Name, "" );
                    changed = true;
                }
                ImGui::SetItemTooltip( "Back to the graph's texture" );
            }
            ImGui::PopID();
        }
        return changed;
    }
#endif


    void ShaderGraphParamsList::Sync( const std::vector<std::pair<std::string, SharedPtr<ShaderGraphAsset>>>& InGraphs )
    {
        m_order.clear();
        for( const auto& graph : InGraphs )
        {
            if( graph.first.empty() )
            {
                continue;
            }
            m_order.push_back( graph.first );
            ByGraph[graph.first].Asset = graph.second;
        }
        for( auto it = ByGraph.begin(); it != ByGraph.end(); )
        {
            if( std::find( m_order.begin(), m_order.end(), it->first ) == m_order.end() )
            {
                it = ByGraph.erase( it );
            }
            else
            {
                ++it;
            }
        }
    }


    const ShaderGraphParams* ShaderGraphParamsList::Find( const std::string& InGraphPath ) const
    {
        auto found = ByGraph.find( InGraphPath );
        return found != ByGraph.end() ? &found->second : nullptr;
    }


    void ShaderGraphParamsList::ToJson( json& OutJson ) const
    {
        OutJson = json::object();
        for( const auto& entry : ByGraph )
        {
            if( !entry.second.Values.empty() || !entry.second.TexturePaths.empty() )
            {
                entry.second.ToJson( OutJson[entry.first] );
            }
        }
    }


    void ShaderGraphParamsList::FromJson( const json& InJson )
    {
        // Keep the loaded assets of graphs still listed; Sync fills in the rest.
        std::map<std::string, SharedPtr<ShaderGraphAsset>> assets;
        for( const auto& entry : ByGraph )
        {
            assets[entry.first] = entry.second.Asset;
        }
        ByGraph.clear();
        if( InJson.is_object() )
        {
            for( auto it = InJson.begin(); it != InJson.end(); ++it )
            {
                ShaderGraphParams& params = ByGraph[it.key()];
                params.FromJson( it.value() );
                params.Asset = assets[it.key()];
            }
        }
        for( const std::string& path : m_order )
        {
            ByGraph[path].Asset = assets[path];
        }
    }


#if USING( ME_TOOLS )
    bool ShaderGraphParamsList::OnEditorInspect()
    {
        if( m_order.empty() )
        {
            ImGui::TextDisabled( "No graphs" );
            return false;
        }
        ImGui::NewLine();
        bool changed = false;
        for( size_t i = 0; i < m_order.size(); ++i )
        {
            const std::string& path = m_order[i];
            ImGui::PushID( static_cast<int>( i ) );
            const std::string name = std::filesystem::path( path ).stem().string();
            const bool open = ImGui::TreeNodeEx( "Graph", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", name.c_str() );
            ImGui::SetItemTooltip( "%s", path.c_str() );
            if( open )
            {
                changed |= ByGraph[path].OnEditorInspect( false );
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        return changed;
    }
#endif
}
