#include "PCH.h"
#include "Camera.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Texture.h"
#include "Math/Frustrum.h"
#include "Resource/ResourceCache.h"
#include "Utils/HavanaUtils.h"
#include <Graphics/Material.h>
#include <cmath>

#if USING( ME_EDITOR )
#include "Events/EditorEvents.h"
#include "Events/HavanaEvents.h"
#include "Types/AssetDescriptor.h"
#endif

ME_REFLECT_ENUM( Moonlight::ProjectionType, { { "Perspective", Moonlight::ProjectionType::Perspective }, { "Orthographic", Moonlight::ProjectionType::Orthographic } } )
ME_REFLECT_ENUM( Moonlight::ClearColorType, { { "Color", Moonlight::ClearColorType::Color }, { "Skybox", Moonlight::ClearColorType::Skybox }, { "Procedural", Moonlight::ClearColorType::Procedural } } )

ME_REFLECT_BEGIN( Moonlight::ProceduralSkySettings )
    ME_FIELD( Time ).Display( "Time of Day" ).Range( 0.f, 24.f );
    ME_FIELD( TimeScale ).Display( "Time Scale" ).Range( 0.f, 1.f );
    ME_FIELD( Latitude ).Range( -90.f, 90.f );
    ME_FIELD( Turbidity ).Range( 1.9f, 10.f );
    ME_FIELD( Month ).Range( 0.f, 11.f );
ME_REFLECT_END()

ME_REFLECT_BEGIN( Camera )
    ME_FIELD( Projection );
    ME_FIELD_NAMED( m_FOV, "FieldOfView" ).Display( "Field of View" ).Range( 1.f, 179.f );
    ME_FIELD( OrthographicSize ).Display( "Orthographic Size (m)" ).Range( 0.01f, 1000.f ).Tooltip( "Half the view height in metres" );
    ME_FIELD( Near ).Display( "Near Clip (m)" ).Speed( 0.01f ).Tooltip( "Distance to the near clipping plane in metres" );
    ME_FIELD( Far ).Display( "Far Clip (m)" ).Speed( 1.f ).Tooltip( "Distance to the far clipping plane in metres" );
    ME_FIELD( ClearType ).Display( "Background" ).CustomInspector();
    ME_FIELD( ClearColor ).Color().CustomInspector();
    ME_FIELD( ProceduralSky ).Display( "Procedural Sky" ).CustomInspector();
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
    if( EditorCamera == this )
    {
        EditorCamera = nullptr;
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

void Camera::OnPropertyChanged( const std::string& InFieldName )
{
    Near = std::max( Near, 0.01f );
    Far = std::max( Far, Near + 0.01f );
    ProceduralSky.Clamp();
}

Moonlight::ProceduralSkySettings Camera::GetSkySettings( float deltaTime )
{
    if( m_skyStartTime != ProceduralSky.Time )
    {
        m_skyStartTime = m_skyTime = ProceduralSky.Time;
    }
    if( ClearType == Moonlight::ClearColorType::Procedural && ProceduralSky.TimeScale > 0.f && deltaTime > 0.f )
    {
        m_skyTime = std::fmod( m_skyTime + ProceduralSky.TimeScale * deltaTime, 24.f );
    }
    Moonlight::ProceduralSkySettings settings = ProceduralSky;
    settings.Time = m_skyTime;
    return settings;
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
    OnPropertyChanged( "" );
    m_skyStartTime = m_skyTime = ProceduralSky.Time;
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

namespace
{
    void CameraTooltip( const char* text )
    {
        if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort ) )
        {
            ImGui::SetTooltip( "%s", text );
        }
    }
}

void Camera::OnEditorInspect()
{
    ImGui::SeparatorText( "Camera Output" );
    if( this != EditorCamera )
    {
        const float width = HavanaUtils::Label( "Render Camera" );
        if( IsCurrent() )
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored( ImGui::GetStyleColorVec4( ImGuiCol_ButtonHovered ), "Active Camera" );
            CameraTooltip( "This camera supplies the Game View." );
        }
        else
        {
            ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.f );
            if( ImGui::Button( "Make Current", ImVec2( width, 0.f ) ) )
            {
                SetCurrent();
            }
            ImGui::PopStyleVar();
            CameraTooltip( "Use this camera for the Game View." );
        }
    }

    HavanaUtils::Label( "Background" );
    const char* backgrounds[] = { "Solid Color", "Skybox Texture", "Procedural Sky" };
    int background = static_cast<int>( ClearType );
    if( ImGui::Combo( "##Background", &background, backgrounds, 3 ) )
    {
        ClearType = static_cast<Moonlight::ClearColorType>( background );
    }

    if( ClearType == Moonlight::ClearColorType::Color )
    {
        HavanaUtils::Label( "Clear Color" );
        ImGui::ColorEdit3( "##ClearColor", &ClearColor.x, ImGuiColorEditFlags_NoInputs );
    }
    else if( ClearType == Moonlight::ClearColorType::Skybox )
    {
        const Moonlight::Texture* texture = Skybox && Skybox->SkyMaterial
            ? Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse ) : nullptr;
        const float width = HavanaUtils::Label( "Skybox Texture" );
        const float clearWidth = texture ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.f;
        const std::string name = texture ? texture->GetPath().GetFileNameString() : "Select Texture...";
        auto assignTexture = [this]( Path path ) {
            if( !Skybox ) Skybox = new Moonlight::SkyBox( path.FullPath );
            Skybox->SkyMaterial->SetTexture( Moonlight::TextureType::Diffuse, ResourceCache::GetInstance().Get<Moonlight::Texture>( path ) );
        };
        if( ImGui::Button( ( name + "##SkyboxTexture" ).c_str(), ImVec2( std::max( width - clearWidth, 1.f ), 0.f ) ) )
        {
            RequestAssetSelectionEvent evt( assignTexture, AssetType::Texture );
            evt.Fire();
        }
        CameraTooltip( texture ? std::string( texture->GetPath().GetLocalPath() ).c_str() : "Select a skybox texture or drop one here." );
        if( ImGui::BeginDragDropTarget() )
        {
            if( ImGui::AcceptDragDropPayload( AssetDescriptor::kDragAndDropPayload ) )
            {
                AssetDescriptor* asset = AssetDescriptor::GetDragged();
                if( asset && asset->Type == AssetType::Texture ) assignTexture( asset->FullPath );
            }
            ImGui::EndDragDropTarget();
        }
        if( texture )
        {
            ImGui::SameLine();
            if( ImGui::Button( "X##ClearSkybox", ImVec2( ImGui::GetFrameHeight(), 0.f ) ) )
            {
                Skybox->SkyMaterial->SetTexture( Moonlight::TextureType::Diffuse, nullptr );
            }
            CameraTooltip( "Clear skybox texture" );
        }
    }
    else if( ClearType == Moonlight::ClearColorType::Procedural )
    {
        ImGui::SeparatorText( "Procedural Sky" );
        HavanaUtils::Label( "Time of Day" );
        bool changed = ImGui::SliderFloat( "##SkyTime", &ProceduralSky.Time, 0.f, 24.f, "%.2f h", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "24-hour local solar time. Changing this restarts the sky clock." );
        HavanaUtils::Label( "Time Scale" );
        changed |= ImGui::DragFloat( "##SkyTimeScale", &ProceduralSky.TimeScale, 0.01f, 0.f, 1.f, "%.2f h/s", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Sky hours per real second. Set to zero to hold the time of day." );
        HavanaUtils::Label( "Latitude" );
        changed |= ImGui::DragFloat( "##SkyLatitude", &ProceduralSky.Latitude, 0.1f, -90.f, 90.f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Sun position: -90 degrees at the South Pole, 0 at the equator, 90 at the North Pole." );
        HavanaUtils::Label( "Month" );
        const char* months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        changed |= ImGui::Combo( "##SkyMonth", &ProceduralSky.Month, months, 12 );
        CameraTooltip( "Season used to calculate the sun's orbit." );
        HavanaUtils::Label( "Turbidity" );
        changed |= ImGui::DragFloat( "##SkyTurbidity", &ProceduralSky.Turbidity, 0.01f, 1.9f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Atmospheric haze. Lower values give a clear sky; higher values scatter more sunlight." );
        if( changed ) OnPropertyChanged( "ProceduralSky" );
    }
}

#endif
