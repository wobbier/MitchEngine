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

ME_REFLECT_ENUM( Moonlight::ProjectionType, { { "Perspective", Moonlight::ProjectionType::Perspective }, { "Orthographic", Moonlight::ProjectionType::Orthographic } } )
ME_REFLECT_ENUM( Moonlight::ClearColorType, { { "Color", Moonlight::ClearColorType::Color }, { "Skybox", Moonlight::ClearColorType::Skybox }, { "Procedural", Moonlight::ClearColorType::Procedural } } )

ME_REFLECT_BEGIN( Camera )
    ME_FIELD( Projection );
    ME_FIELD_NAMED( m_FOV, "FieldOfView" ).Display( "Field of View" ).Range( 1.f, 179.f );
    ME_FIELD( OrthographicSize ).Range( 0.01f, 1000.f );
    ME_FIELD( Near ).Speed( 0.01f );
    ME_FIELD( Far ).Speed( 1.f );
    ME_FIELD( ClearType );
    ME_FIELD( ClearColor ).Color();
    ME_FIELD( Zoom ).Hidden();
ME_REFLECT_END()


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
        const float halfHeight = std::max( OrthographicSize, 0.001f );
        const float halfWidth = OutputSize.y > 0.f ? halfHeight * OutputSize.x / OutputSize.y : halfHeight;
        bx::mtxOrtho( &outMatrix.GetInternalMatrix()[0][0], -halfWidth, halfWidth, -halfHeight, halfHeight, Near, Far, 0.f, bgfx::getCaps()->homogeneousDepth );
    }

    if( isOblique )
    {
        return CalculateObliqueMatrix( outMatrix.GetInternalMatrix(), ObliqueMatData );
    }
    return outMatrix;
}

void Camera::OnDeserialize( const json& inJson )
{
    Reflection::FromJson( StaticType(), this, inJson );
    if( inJson.contains( "Skybox" ) && inJson["Skybox"].is_string() )
    {
        Skybox = new Moonlight::SkyBox( inJson["Skybox"].get<std::string>() );
    }
    if( inJson.value( "IsCurrent", false ) )
    {
        SetCurrent();
    }
}


void Camera::OnSerialize( json& outJson )
{
    Reflection::ToJson( StaticType(), this, outJson );
    outJson["IsCurrent"] = IsCurrent();
    if( Skybox && Skybox->SkyMaterial && Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse ) )
    {
        outJson["Skybox"] = Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse )->GetPath().GetLocalPath();
    }
}


#if USING( ME_EDITOR )

void Camera::OnEditorInspect()
{
    // Projection, FOV, clipping and clear settings are drawn from reflection.
    ImGui::BeginDisabled( IsCurrent() );
    if( ImGui::Button( IsCurrent() ? "Current Camera" : "Set Current" ) )
    {
        SetCurrent();
    }
    ImGui::EndDisabled();

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
                AssetDescriptor* _p = AssetDescriptor::GetDragged();
                if( !_p ) return;
                AssetDescriptor& payload_n = *_p;

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
    else if( ClearType == Moonlight::ClearColorType::Procedural )
    {
        GetEngine().GetRenderer().GetSky()->DrawImGui();
    }
}

#endif