#include "PCH.h"
#include "Camera.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Texture.h"
#include "Math/Frustrum.h"
#include "Resource/ResourceCache.h"
#include "Utils/HavanaUtils.h"
#include <Graphics/Material.h>

#if USING( ME_EDITOR )
#include <Renderer.h>
#include <Graphics/DynamicSky.h>
#include <Engine/Engine.h>
#include "Events/EditorEvents.h"
#include "Events/HavanaEvents.h"
#include "Types/AssetDescriptor.h"
#endif

Camera* Camera::CurrentCamera = nullptr;
Camera* Camera::EditorCamera = nullptr;

Camera::Camera()
    : Component( "Camera" )
    , OutputSize( 1280.f, 720.f )
{
    //CameraFrustum = new Frustum();
}

Camera::~Camera()
{
    if( CurrentCamera == this )
    {
        CurrentCamera = nullptr;
    }
    //delete CameraFrustum;
}

void Camera::Init()
{
    if( !CurrentCamera )
    {
        CurrentCamera = this;
    }
}

Matrix4 Camera::GetViewMatrix()
{
    return Matrix4();//glm::lookAt(Position.GetInternalVec(), Position.GetInternalVec() + Front.GetInternalVec(), Up.GetInternalVec()));
}

bool Camera::IsCurrent()
{
    return Camera::CurrentCamera == this;
}

void Camera::SetCurrent()
{
    Camera::CurrentCamera = this;
}

float Camera::GetFOV()
{
    return m_FOV;
}

float Camera::GetAspectRatio() const
{
    return ( OutputSize.x / OutputSize.y );
}

const int Camera::GetCameraId() const
{
    return m_id;
}

const bool Camera::IsMain() const
{
    return ( Camera::CurrentCamera == this );
}

void Camera::SetObliqueMatrixData( const glm::vec4& inVec )
{
    ObliqueMatData = inVec;
    isOblique = true;
}

void Camera::ClearObliqueMatrixData()
{
    isOblique = false;
}

Matrix4 CalculateObliqueMatrix( const Matrix4& projection, const glm::vec4& clipPlane )
{
    Matrix4 returnMat = projection;
    {
        glm::vec4 vCamera = {
        ( Mathf::Sign( clipPlane.x ) + returnMat.GetInternalMatrix()[2][0] ) / returnMat.GetInternalMatrix()[0][0],
        ( Mathf::Sign( clipPlane.y ) + returnMat.GetInternalMatrix()[2][1] ) / returnMat.GetInternalMatrix()[1][1],
        1.f,
        ( 1.f + returnMat.GetInternalMatrix()[2][2] ) / returnMat.GetInternalMatrix()[3][2]
        };

        float m = 2.f / glm::dot( clipPlane, vCamera );
        returnMat.GetInternalMatrix()[0][2] = clipPlane.x * m;
        returnMat.GetInternalMatrix()[1][2] = clipPlane.y * m;
        returnMat.GetInternalMatrix()[2][2] = clipPlane.z * m;
        returnMat.GetInternalMatrix()[3][2] = clipPlane.w * m;
    }
    /*{
        glm::vec4 vCamera = {
        (Mathf::Sign(clipPlane.x) - returnMat.GetInternalMatrix()[0][2]) / returnMat.GetInternalMatrix()[0][0],
        (Mathf::Sign(clipPlane.y) - returnMat.GetInternalMatrix()[1][2]) / returnMat.GetInternalMatrix()[1][1],
        1.f,
        (1.f - returnMat.GetInternalMatrix()[2][2]) / returnMat.GetInternalMatrix()[2][3]
        };

        float m = 1.f / glm::dot(clipPlane, vCamera);
        returnMat.GetInternalMatrix()[2][0] = m * clipPlane.x;
        returnMat.GetInternalMatrix()[2][1] = m * clipPlane.y;
        returnMat.GetInternalMatrix()[2][2] = m * clipPlane.z;
        returnMat.GetInternalMatrix()[2][3] = m * clipPlane.w;
    }*/
    //{
    //	glm::vec4 q = projection.Inverse().GetInternalMatrix() * glm::vec4(Mathf::Sign(clipPlane.x), Mathf::Sign(clipPlane.y), 1.f, 1.f);
    //	glm::vec4 c = clipPlane * (2.f / glm::dot(clipPlane, q));
    //	returnMat.GetInternalMatrix()[2][0] = c.x - returnMat.GetInternalMatrix()[3][0];
    //	returnMat.GetInternalMatrix()[2][1] = c.y - returnMat.GetInternalMatrix()[3][1];
    //	returnMat.GetInternalMatrix()[2][2] = c.z - returnMat.GetInternalMatrix()[3][2];
    //	returnMat.GetInternalMatrix()[2][3] = c.w - returnMat.GetInternalMatrix()[3][3];
    //}
    return returnMat;
}

Matrix4 Camera::GetProjectionMatrix() const
{
    // #TODO: output size in the game view of the editor is messed up cause it's not reading the output size of the editor window widget.
    Matrix4 outMatrix;
    if( Projection == Moonlight::ProjectionType::Perspective )
    {
        bx::mtxProj( &outMatrix.GetInternalMatrix()[0][0], m_FOV, float( OutputSize.x ) / float( OutputSize.y ), std::max( Near, 0.01f ), Far, bgfx::getCaps()->homogeneousDepth );
    }
    else
    {
        bx::mtxOrtho( &outMatrix.GetInternalMatrix()[0][0], -( OutputSize.x / OrthographicSize ), ( OutputSize.x / OrthographicSize ), -( OutputSize.y / OrthographicSize ), ( OutputSize.y / OrthographicSize ), Near, Far, 0.f, bgfx::getCaps()->homogeneousDepth );
    }

    if( isOblique )
    {
        return CalculateObliqueMatrix( outMatrix.GetInternalMatrix(), ObliqueMatData );
    }
    return outMatrix;
}

void Camera::OnDeserialize( const json& inJson )
{
    if( inJson.contains( "Skybox" ) )
    {
        Skybox = new Moonlight::SkyBox( inJson["Skybox"] );
    }

    if( inJson.contains( "Zoom" ) )
    {
        Zoom = inJson["Zoom"];
    }

    if( inJson.contains( "IsCurrent" ) )
    {
        if( inJson["IsCurrent"] )
        {
            SetCurrent();
        }
    }

    if( inJson.contains( "Near" ) )
    {
        Near = inJson["Near"];
    }

    if( inJson.contains( "Far" ) )
    {
        Far = inJson["Far"];
    }

    if( inJson.contains( "ClearType" ) )
    {
        if( inJson["ClearType"] == "Color" )
        {
            ClearType = Moonlight::ClearColorType::Color;
        }
        else if( inJson["ClearType"] == "Skybox" )
        {
            ClearType = Moonlight::ClearColorType::Skybox;
        }
        else if( inJson["ClearType"] == "Procedural" )
        {
            ClearType = Moonlight::ClearColorType::Procedural;
        }
    }

    if( inJson.contains( "ClearColor" ) )
    {
        ClearColor = Vector3( (float)inJson["ClearColor"][0], (float)inJson["ClearColor"][1], (float)inJson["ClearColor"][2] );
    }
}

void Camera::OnSerialize( json& outJson )
{
    outJson["Zoom"] = Zoom;
    outJson["IsCurrent"] = IsCurrent();
    outJson["Near"] = Near;
    outJson["Far"] = Far;
    if( ClearType == Moonlight::ClearColorType::Color )
    {
        outJson["ClearType"] = "Color";
    }
    else if( ClearType == Moonlight::ClearColorType::Skybox )
    {
        outJson["ClearType"] = "Skybox";
    }
    else if( ClearType == Moonlight::ClearColorType::Procedural )
    {
        outJson["ClearType"] = "Procedural";
    }

    if( Skybox && Skybox->SkyMaterial )
    {
        outJson["Skybox"] = Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse )->GetPath().GetLocalPath();
    }

    outJson["ClearColor"] = { ClearColor.x, ClearColor.y, ClearColor.z };
}

#if USING( ME_EDITOR )

void Camera::OnEditorInspect()
{
    if( ImGui::Button( "Set Current" ) )
    {
        SetCurrent();
    }
    HavanaUtils::Label( "Projection" );
    if( ImGui::BeginCombo( "##Projection", ( Projection == Moonlight::ProjectionType::Perspective ) ? "Perspective" : "Orthographic" ) )
    {
        if( ImGui::Selectable( "Perspective", ( Projection == Moonlight::ProjectionType::Perspective ) ) )
        {
            Projection = Moonlight::ProjectionType::Perspective;
        }
        if( ImGui::Selectable( "Orthographic", ( Projection == Moonlight::ProjectionType::Orthographic ) ) )
        {
            Projection = Moonlight::ProjectionType::Orthographic;
        }
        ImGui::EndCombo();
    }

    if( Projection == Moonlight::ProjectionType::Perspective )
    {
        HavanaUtils::Label( "Field of View" );
        ImGui::SliderFloat( "##Field of View", &m_FOV, 1.0f, 200.0f );
    }
    else if( Projection == Moonlight::ProjectionType::Orthographic )
    {
        HavanaUtils::Label( "Size" );
        ImGui::SliderFloat( "##Size", &OrthographicSize, 0.1f, 200.0f );
    }

    HavanaUtils::Label( "Near" );
    ImGui::SliderFloat( "##Near", &Near, 0.03f, 200.0f );
    HavanaUtils::Label( "Far" );
    ImGui::SliderFloat( "##Far", &Far, 0.2f, 2000.0f );

    HavanaUtils::Label( "Clear Type" );
    // TODO: Display the proper clear type as text
    if( ImGui::BeginCombo( "##ClearType", ( ClearType == Moonlight::ClearColorType::Color ) ? "Color" : "Skybox" ) )
    {
        if( ImGui::Selectable( "Color", ( ClearType == Moonlight::ClearColorType::Color ) ) )
        {
            ClearType = Moonlight::ClearColorType::Color;
        }
        if( ImGui::Selectable( "Skybox", ( ClearType == Moonlight::ClearColorType::Skybox ) ) )
        {
            ClearType = Moonlight::ClearColorType::Skybox;
        }
        if( ImGui::Selectable( "Procedural", ( ClearType == Moonlight::ClearColorType::Procedural ) ) )
        {
            ClearType = Moonlight::ClearColorType::Procedural;
        }
        ImGui::EndCombo();
    }

    if( ClearType == Moonlight::ClearColorType::Skybox )
    {
        const Moonlight::Texture* skyboxTexture = nullptr;
        if( Skybox && Skybox->SkyMaterial )
        {
            skyboxTexture = Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse );
        }

        float widgetWidth = HavanaUtils::Label( "Skybox Texture" );
        // #TODO: fix this tf?
        //std::string label( "##Texture" );
        //if ( texture && bgfx::isValid( texture->TexHandle ) )
        //{
        //	if ( ImGui::ImageButton( texture->TexHandle, ImVec2( 30, 30 ) ) )
        //	{
        //		PreviewResourceEvent evt;
        //		evt.Subject = texture;
        //		evt.Fire();
        //	}
        //	static bool ViewTexture = true;
        //	if ( ImGui::BeginPopupModal( "ViewTexture", &ViewTexture, ImGuiWindowFlags_MenuBar ) )
        //	{
        //		if ( texture )
        //		{
        //			// Get the current cursor position (where your window is)
        //			ImVec2 pos = ImGui::GetCursorScreenPos();
        //			ImVec2 maxPos = ImVec2( pos.x + ImGui::GetWindowSize().x, pos.y + ImGui::GetWindowSize().y );
        //			Vector2 RenderSize = Vector2( ImGui::GetWindowSize().x, ImGui::GetWindowSize().y );
        //
        //			// Ask ImGui to draw it as an image:
        //			// Under OpenGL the ImGUI image type is GLuint
        //			// So make sure to use "(void *)tex" but not "&tex"
        //			/*ImGui::GetWindowDrawList()->AddImage(
        //				(void*)texture->TexHandle,
        //				ImVec2(pos.x, pos.y),
        //				ImVec2(maxPos));*/
        //				//ImVec2(WorldViewRenderSize.X() / RenderSize.X(), WorldViewRenderSize.Y() / RenderSize.Y()));
        //
        //		}
        //		if ( ImGui::Button( "Close" ) )
        //		{
        //			ViewTexture = false;
        //			ImGui::CloseCurrentPopup();
        //		}
        //		ImGui::EndPopup();
        //	}
        //	ImGui::SameLine();
        //}

        ImVec2 selectorSize( widgetWidth, 0.f );
        if( ImGui::Button( ( ( skyboxTexture ) ? skyboxTexture->GetPath().GetLocalPath().data() : "Select Asset" ), selectorSize ) )
        {
            RequestAssetSelectionEvent evt( [this]( Path selectedAsset )
                {
                    if( !Skybox )
                    {
                        Skybox = new Moonlight::SkyBox( selectedAsset.FullPath );
                    }
                    Skybox->SkyMaterial->SetTexture( Moonlight::TextureType::Diffuse, ResourceCache::GetInstance().Get<Moonlight::Texture>( selectedAsset ) );
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
                    Skybox->SkyMaterial->SetTexture( Moonlight::TextureType::Diffuse, ResourceCache::GetInstance().Get<Moonlight::Texture>( payload_n.FullPath ) );
                }
            }
            ImGui::EndDragDropTarget();
        }
        if( skyboxTexture )
        {
            ImGui::SameLine();
            if( ImGui::Button( "X" ) )
            {
                Skybox->SkyMaterial->SetTexture( Moonlight::TextureType::Diffuse, nullptr );
            }
        }
    }
    else if( ClearType == Moonlight::ClearColorType::Color )
    {
        HavanaUtils::ColorButton( "Clear Color", ClearColor );
    }
    else if( ClearType == Moonlight::ClearColorType::Procedural )
    {
        GetEngine().GetRenderer().GetSky()->DrawImGui();
    }
}

#endif