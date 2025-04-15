#include "Material.h"
#include "optick.h"
#include "Texture.h"
#include "Resource/ResourceCache.h"
#include "bgfx/defines.h"
#include "Utils/HavanaUtils.h"
#include "Utils/ImGuiUtils.h"
#include "Events/EditorEvents.h"
#include "Types/AssetDescriptor.h"

namespace Moonlight
{
    Material::Material( const std::string& MaterialTypeName, const std::string& ShaderPath )
        : Resource( Path( ShaderPath ) )
        , Textures( TextureType::Count, nullptr )
        , DiffuseColor( 1.f, 1.f, 1.f )
        , Tiling( 1.f, 1.f )
        , TypeName( MaterialTypeName )
    {
        LoadShader( ShaderPath );
    }

    Material::Material( const Path& InFilePath, WrapMode mode /*= WrapMode::Wrap */ )
        : Resource( InFilePath )
    {

    }

    Material::~Material()
    {
    }

    void Material::LoadShader( const std::string& inShaderName )
    {
        ShaderName = inShaderName;
        if( inShaderName.length() > 0 )
        {
            MeshShader = ShaderCommand( inShaderName );
        }
    }

    const bool Material::IsTransparent() const
    {
        if( RenderMode == RenderingMode::Transparent )
        {
            return true;
        }
        else if( Textures[TextureType::Opacity] )
        {
            return true;
        }
        return false;
    }

    void Material::SetRenderMode( RenderingMode newMode )
    {
        RenderMode = newMode;
    }

    void Material::OnSerialize( json& OutJson )
    {
        OutJson["Type"] = TypeName;
        OutJson["FaceMode"] = FaceMode;
        OutJson["BlendMode"] = AlphaBlendMode;
        OutJson["RenderMode"] = RenderMode;
        OutJson["DiffuseColor"] = { DiffuseColor.x, DiffuseColor.y, DiffuseColor.z };
        OutJson["Tiling"] = { Tiling.x, Tiling.y };
        for( unsigned int type = 0; type < TextureType::Count; ++type )
        {
            auto texture = Textures[type];
            if( texture )
            {
                json& savedTexture = OutJson["Textures"][Texture::ToString( static_cast<TextureType>( type ) )];
                savedTexture["Path"] = texture->GetPath().GetLocalPath();
                savedTexture["RenderMode"] = GetRenderingModeString( RenderMode );
            }
        }
    }

    void Material::OnDeserialize( const json& InJson )
    {
        if( InJson.contains( "Type" ) )
        {
            TypeName = InJson["Type"];
        }
        if( InJson.contains( "DiffuseColor" ) )
        {
            DiffuseColor = Vector3( (float)InJson["DiffuseColor"][0], (float)InJson["DiffuseColor"][1], (float)InJson["DiffuseColor"][2] );
        }
        if( InJson.contains( "Tiling" ) )
        {
            Tiling = Vector2( (float)InJson["Tiling"][0], (float)InJson["Tiling"][1] );
        }
        if( InJson.contains( "FaceMode" ) )
        {
            FaceMode = (RenderFaceMode)InJson["FaceMode"];
        }
        if( InJson.contains( "BlendMode" ) )
        {
            AlphaBlendMode = (BlendMode)InJson["BlendMode"];
        }
        if( InJson.contains( "RenderMode" ) )
        {
            RenderMode = (RenderingMode)InJson["RenderMode"];
        }
        if( InJson.contains( "Textures" ) )
        {
            for( unsigned int type = 0; type < TextureType::Count; ++type )
            {
                if( InJson["Textures"].contains( Texture::ToString( static_cast<TextureType>( type ) ) ) )
                {
                    Path texturePath = Path( InJson["Textures"][Texture::ToString( static_cast<TextureType>( type ) )]["Path"] );
                    SetTexture( static_cast<TextureType>( type ), ResourceCache::GetInstance().Get<Moonlight::Texture>( texturePath ) );
                }
            }
        }
    }

    void Material::CopyValues( Material* mat )
    {
        DiffuseColor = mat->DiffuseColor;
        Textures = mat->Textures;
        RenderMode = mat->RenderMode;
        Tiling = mat->Tiling;
        MeshShader = mat->MeshShader;
        AlphaBlendMode = mat->AlphaBlendMode;
    }

    void Material::SetTexture( const TextureType& textureType, std::shared_ptr<Moonlight::Texture> loadedTexture )
    {
        Textures[textureType] = loadedTexture;
    }

    const Moonlight::Texture* Material::GetTexture( const TextureType& type ) const
    {
        if( Textures[type] )
        {
            return Textures[type].get();
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<Moonlight::Texture>>& Material::GetTextures()
    {
        return Textures;
    }

    const std::string& Material::GetTypeName() const
    {
        return TypeName;
    }

    uint64_t Material::GetRenderState( uint64_t state ) const
    {
        uint64_t modifiedState = state;
        // this all should be applied when changed and not polled

        if( IsTransparent() )
        {
            switch( AlphaBlendMode )
            {
            case Moonlight::BlendMode::Alpha:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ALPHA;
                break;
            case Moonlight::BlendMode::Premultiply:
                modifiedState = modifiedState | BGFX_STATE_BLEND_FUNC( BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA );
                break;
            case Moonlight::BlendMode::Additive:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ADD;
                break;
            case Moonlight::BlendMode::Multiply:
                modifiedState = modifiedState | BGFX_STATE_BLEND_MULTIPLY;
                break;
            case Moonlight::BlendMode::Count:
            default:
                modifiedState = modifiedState | BGFX_STATE_BLEND_ALPHA;
                break;
            }
        }

        switch( FaceMode )
        {
        case Moonlight::RenderFaceMode::Front:
            modifiedState = modifiedState | BGFX_STATE_CULL_CCW;
            break;
        case Moonlight::RenderFaceMode::Back:
            modifiedState = modifiedState | BGFX_STATE_CULL_CW;
            break;
        case Moonlight::RenderFaceMode::Both:
            // none?
            break;
        case Moonlight::RenderFaceMode::Count:
        default:
            break;
        }

        return modifiedState;
    }

#if USING( ME_TOOLS )
    void Material::OnEditorInspect()
    {
        bool transparent = IsTransparent();
        //if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen))
            HavanaUtils::Label( "Render Transparent" );
            ImGui::Checkbox( "##Render Transparent", &transparent );
            if( transparent )
            {
                SetRenderMode( Moonlight::RenderingMode::Transparent );

                HavanaUtils::Label( "Blend Type" );
                if( ImGui::BeginCombo( "##Blend Type", GetBlendModeString( AlphaBlendMode ).c_str() ) )
                {
                    for( unsigned int blendMode = (unsigned int)Moonlight::BlendMode::Alpha; blendMode < (unsigned int)Moonlight::BlendMode::Count; ++blendMode )
                    {
                        if( ImGui::Selectable( GetBlendModeString( (Moonlight::BlendMode)blendMode ).c_str(), ( blendMode == (unsigned int)AlphaBlendMode ) ) )
                        {
                            AlphaBlendMode = (Moonlight::BlendMode)blendMode;
                        }
                    }
                    ImGui::EndCombo();
                }

            }
            else
            {
                SetRenderMode( Moonlight::RenderingMode::Opaque );
            }

        HavanaUtils::Label( "Face Type" );
        if( ImGui::BeginCombo( "##Face Type", GetRenderFaceModeString( FaceMode ).c_str() ) )
        {
            for( unsigned int faceMode = (unsigned int)Moonlight::RenderFaceMode::Front; faceMode < (unsigned int)Moonlight::RenderFaceMode::Count; ++faceMode )
            {
                if( ImGui::Selectable( GetRenderFaceModeString( (Moonlight::RenderFaceMode)faceMode ).c_str(), ( faceMode == (unsigned int)FaceMode ) ) )
                {
                    FaceMode = (Moonlight::RenderFaceMode)faceMode;
                }
            }
            ImGui::EndCombo();
        }

        HavanaUtils::EditableVector( "Tiling", Tiling );

        //static std::vector<Path> Textures;
        //Path path = Path("Assets");
        //Path engineAssetPath = Path("Engine/Assets");
        //if (Textures.empty())
        //{
        //	Textures.push_back(Path(""));
        //	for (const auto& entry : std::filesystem::recursive_directory_iterator(path.FullPath))
        //	{
        //		Path filePath(entry.path().string());
        //		if ((filePath.LocalPath.rfind(".png") != std::string::npos || filePath.LocalPath.rfind(".jpg") != std::string::npos || filePath.LocalPath.rfind(".tif") != std::string::npos)
        //			&& filePath.LocalPath.rfind(".meta") == std::string::npos
        //			&& filePath.LocalPath.rfind(".dds") == std::string::npos)
        //		{
        //			Textures.push_back(filePath);
        //		}
        //	}

        //	for (const auto& entry : std::filesystem::recursive_directory_iterator(engineAssetPath.FullPath))
        //	{
        //		Path filePath(entry.path().string());
        //		if ((filePath.LocalPath.rfind(".png") != std::string::npos || filePath.LocalPath.rfind(".jpg") != std::string::npos || filePath.LocalPath.rfind(".tif") != std::string::npos)
        //			&& filePath.LocalPath.rfind(".meta") == std::string::npos
        //			&& filePath.LocalPath.rfind(".dds") == std::string::npos)
        //		{
        //			Textures.push_back(filePath);
        //		}
        //	}
        //}
        HavanaUtils::ColorButton( "Diffuse Color", DiffuseColor );

        {
            int i = 0;
            for( auto texture : GetTextures() )
            {
                float widgetWidth = HavanaUtils::Label( Moonlight::Texture::ToString( static_cast<Moonlight::TextureType>( i ) ) );
                std::string label( "##Texture" + std::to_string( i ) );
                if( texture && bgfx::isValid( texture->TexHandle ) )
                {
                    if( ImGui::ImageButton( texture->TexHandle, ImVec2( 30, 30 ) ) )
                    {
                        PreviewResourceEvent evt;
                        evt.Subject = texture;
                        evt.Fire();
                    }
                    static bool ViewTexture = true;
                    if( ImGui::BeginPopupModal( "ViewTexture", &ViewTexture, ImGuiWindowFlags_MenuBar ) )
                    {
                        if( texture )
                        {
                            // Get the current cursor position (where your window is)
                            ImVec2 pos = ImGui::GetCursorScreenPos();
                            ImVec2 maxPos = ImVec2( pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y );
                            Vector2 RenderSize = Vector2( ImGui::GetWindowSize().x, ImGui::GetWindowSize().y );

                            // Ask ImGui to draw it as an image:
                            // Under OpenGL the ImGUI image type is GLuint
                            // So make sure to use "(void *)tex" but not "&tex"
                            /*ImGui::GetWindowDrawList()->AddImage(
                                (void*)texture->TexHandle,
                                ImVec2(pos.x, pos.y),
                                ImVec2(maxPos));*/
                                //ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));

                        }
                        if( ImGui::Button( "Close" ) )
                        {
                            ViewTexture = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                }

                ImVec2 selectorSize( widgetWidth, 0.f );

                if( texture )
                {
                    selectorSize = ImVec2( widgetWidth - 54.f, 0.f );
                }
                ImGui::PushID( i );
                if( ImGui::Button( ( ( texture ) ? texture->GetPath().GetLocalPath().data() : "Select Asset" ), selectorSize ) )
                {
                    RequestAssetSelectionEvent evt( [this, i]( Path selectedAsset )
                        {
                            SetTexture( static_cast<Moonlight::TextureType>( i ), ResourceCache::GetInstance().Get<Moonlight::Texture>( selectedAsset ) );
                        }, AssetType::Texture );
                    evt.Fire();
                }

                if( ImGui::BeginDragDropTarget() )
                {
                    if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
                    {
                        IM_ASSERT( payload->DataSize == sizeof( AssetDescriptor ) );
                        AssetDescriptor& payload_n = *(AssetDescriptor*)payload->Data;

                        if( payload_n.Type == AssetType::Texture )
                        {
                            SetTexture( static_cast<Moonlight::TextureType>( i ), ResourceCache::GetInstance().Get<Moonlight::Texture>( payload_n.FullPath ) );
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if( texture )
                {
                    ImGui::SameLine();
                    if( ImGui::Button( "X" ) )
                    {
                        SetTexture( static_cast<Moonlight::TextureType>( i ), nullptr );
                    }
                }
                ImGui::PopID();
                //if (ImGui::BeginCombo(label.c_str(), ((texture) ? texture->GetPath().LocalPath.c_str() : "")))
                //{
                //	static ImGuiTextFilter filter;
                //	/*ImGui::Text("Filter usage:\n"
                //		"  \"\"         display all lines\n"
                //		"  \"xxx\"      display lines containing \"xxx\"\n"
                //		"  \"xxx,yyy\"  display lines containing \"xxx\" or \"yyy\"\n"
                //		"  \"-xxx\"     hide lines containing \"xxx\"");*/
                //	filter.Draw("##Filter");
                //	for (size_t n = 0; n < Textures.size(); n++)
                //	{
                //		if (!filter.PassFilter(Textures[n].LocalPath.c_str()))
                //		{
                //			continue;
                //		}

                //		if (ImGui::Selectable(Textures[n].LocalPath.c_str(), false))
                //		{
                //			if (!Textures[n].LocalPath.empty())
                //			{
                //				MeshMaterial->SetTexture(static_cast<Moonlight::TextureType>(i), ResourceCache::GetInstance().Get<Moonlight::Texture>(Textures[n]));
                //			}
                //			else
                //			{
                //				MeshMaterial->SetTexture(static_cast<Moonlight::TextureType>(i), nullptr);
                //			}
                //			Textures.clear();
                //			break;
                //		}
                //	}
                //	ImGui::EndCombo();
                //}
                //if (texture)
                //{
                //	if (i == static_cast<int>(Moonlight::TextureType::Diffuse))
                //	{
                //	}
                //}
                i++;
            }
            //ImGui::TreePop();
        }

    }
#endif
}
