#include "PCH.h"
#include "Camera.h"
#include "Graphics/SkyBox.h"
#include "Graphics/Texture.h"
#include "Math/Frustrum.h"
#include "Resource/ResourceCache.h"
#include "Utils/HavanaUtils.h"
#include "ImGuiTheme.h"
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
    ME_FIELD( Turbidity ).Range( 1.f, 10.f );
    ME_FIELD( Month ).Range( 0.f, 11.f );
    ME_FIELD( MoonPhase ).Display( "Moon Phase" ).Range( 0.f, 1.f );
    ME_FIELD( MoonOrbitInclination ).Display( "Moon Orbit Inclination" ).Range( 0.f, 10.f ).Tooltip( "Degrees from the ecliptic. Set to 0 with Moon Phase 0 for a solar eclipse; relative Sun/Moon Size controls partial, total or annular coverage." );
    ME_FIELD( MoonAscendingNode ).Display( "Moon Ascending Node" ).Range( 0.f, 360.f ).Tooltip( "Degrees along the ecliptic where the tilted lunar orbit crosses it; eclipses occur near a node." );
    ME_FIELD( SunSize ).Display( "Sun Size" ).Range( 0.1f, 10.f );
    ME_FIELD( MoonSize ).Display( "Moon Size" ).Range( 0.1f, 10.f );
    ME_FIELD( MoonBrightness ).Display( "Moon Brightness" ).Range( 0.f, 10.f );
    ME_FIELD( StarBrightness ).Display( "Star Brightness" ).Range( 0.f, 10.f );
    ME_FIELD( CloudCoverage ).Display( "Cloud Coverage" ).Range( 0.f, 1.f );
    ME_FIELD( CloudDensity ).Display( "Cloud Density" ).Range( 0.f, 4.f );
    ME_FIELD( CloudAltitude ).Display( "Cloud Altitude (m)" ).Range( 100.f, 10000.f );
    ME_FIELD( CloudThickness ).Display( "Cloud Thickness (m)" ).Range( 100.f, 8000.f );
    ME_FIELD( WindSpeed ).Display( "Wind Speed (m/s)" ).Range( 0.f, 100.f );
    ME_FIELD( WindDirection ).Display( "Wind Direction" ).Range( 0.f, 360.f );
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
    if( m_moonStartPhase != ProceduralSky.MoonPhase )
    {
        m_moonStartPhase = m_moonPhase = ProceduralSky.MoonPhase;
    }
    if( ClearType == Moonlight::ClearColorType::Procedural && ProceduralSky.TimeScale > 0.f && deltaTime > 0.f )
    {
        const float hours = ProceduralSky.TimeScale * deltaTime;
        m_skyTime = std::fmod( m_skyTime + hours, 24.f );
        // A lunar month is 29.53 days: the moon rises about 50 minutes later each day.
        m_moonPhase = std::fmod( m_moonPhase + hours / ( 24.f * 29.53f ), 1.f );
    }
    Moonlight::ProceduralSkySettings settings = ProceduralSky;
    settings.Time = m_skyTime;
    settings.MoonPhase = m_moonPhase;
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
    m_moonStartPhase = m_moonPhase = ProceduralSky.MoonPhase;
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

    const char* MoonPhaseFormat( float phase )
    {
        if( phase < 0.03f || phase >= 0.97f ) return "%.2f  New Moon";
        if( phase < 0.22f ) return "%.2f  Waxing Crescent";
        if( phase < 0.28f ) return "%.2f  First Quarter";
        if( phase < 0.47f ) return "%.2f  Waxing Gibbous";
        if( phase < 0.53f ) return "%.2f  Full Moon";
        if( phase < 0.72f ) return "%.2f  Waning Gibbous";
        if( phase < 0.78f ) return "%.2f  Last Quarter";
        return "%.2f  Waning Crescent";
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
            ImGui::TextColored( ImGuiTheme::AccentText(), "Active Camera" );
            CameraTooltip( "This camera supplies the Game View." );
        }
        else
        {
            if( ImGui::Button( "Make Current", ImVec2( width, 0.f ) ) )
            {
                SetCurrent();
            }
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
        ImGui::ColorEdit3( "##ClearColor", &ClearColor.x, ImGuiColorEditFlags_Float );
    }
    else if( ClearType == Moonlight::ClearColorType::Skybox )
    {
        const Moonlight::Texture* texture = Skybox && Skybox->SkyMaterial
            ? Skybox->SkyMaterial->GetTexture( Moonlight::TextureType::Diffuse ) : nullptr;
        const float width = HavanaUtils::Label( "Skybox Texture" );
        const float clearWidth = texture ? ImGuiTheme::IconButtonsWidth( 1 ) : 0.f;
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
            ImGui::SameLine( 0.f, ImGuiTheme::InnerGap() );
            if( ImGui::Button( "x##ClearSkybox", ImGuiTheme::IconButtonSize() ) )
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
        changed |= ImGui::DragFloat( "##SkyTurbidity", &ProceduralSky.Turbidity, 0.01f, 1.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Haze (aerosols) in the air. 1 is perfectly clean air, 2 a clear day, 4 and up hazy." );

        ImGui::SeparatorText( "Sun, Moon & Stars" );
        HavanaUtils::Label( "Moon Phase" );
        changed |= ImGui::SliderFloat( "##SkyMoonPhase", &ProceduralSky.MoonPhase, 0.f, 1.f, MoonPhaseFormat( ProceduralSky.MoonPhase ), ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "0 new moon, 0.25 first quarter, 0.5 full moon, 0.75 last quarter. The moon's position in the sky follows from it, and it advances with the time scale." );
        HavanaUtils::Label( "Orbit Inclination" );
        changed |= ImGui::DragFloat( "##SkyMoonOrbitInclination", &ProceduralSky.MoonOrbitInclination, 0.01f, 0.f, 10.f, "%.3f deg", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Moon orbit tilt. Set to 0 with Moon Phase 0 for a solar eclipse. A moon larger than the sun gives totality; a smaller moon leaves an annular ring." );
        HavanaUtils::Label( "Ascending Node" );
        changed |= ImGui::DragFloat( "##SkyMoonAscendingNode", &ProceduralSky.MoonAscendingNode, 0.1f, 0.f, 360.f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Where the lunar orbit crosses the ecliptic, in degrees. With a tilted orbit, eclipses occur near these crossings." );
        HavanaUtils::Label( "Sun Size" );
        changed |= ImGui::DragFloat( "##SkySunSize", &ProceduralSky.SunSize, 0.01f, 0.1f, 10.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Angular size of the sun's disc; 1 is the real sun." );
        HavanaUtils::Label( "Moon Size" );
        changed |= ImGui::DragFloat( "##SkyMoonSize", &ProceduralSky.MoonSize, 0.01f, 0.1f, 10.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Angular size of the moon's disc; 1 is the real moon." );
        HavanaUtils::Label( "Moon Brightness" );
        changed |= ImGui::DragFloat( "##SkyMoonBrightness", &ProceduralSky.MoonBrightness, 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Moon disc brightness and scene moonlight, including the daytime moon. Cloud and atmospheric scattering stay dimmer than the lifted scene lighting." );
        HavanaUtils::Label( "Star Brightness" );
        changed |= ImGui::DragFloat( "##SkyStarBrightness", &ProceduralSky.StarBrightness, 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Stars and the Milky Way. They fade as the sky brightens." );

        ImGui::SeparatorText( "Clouds" );
        HavanaUtils::Label( "Coverage" );
        changed |= ImGui::SliderFloat( "##SkyCloudCoverage", &ProceduralSky.CloudCoverage, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "0 clear sky, 0.3 scattered clouds, 0.6 broken cloud, 1 overcast." );
        HavanaUtils::Label( "Density" );
        changed |= ImGui::DragFloat( "##SkyCloudDensity", &ProceduralSky.CloudDensity, 0.01f, 0.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "How thick the clouds are: low values give wispy, translucent clouds." );
        HavanaUtils::Label( "Altitude" );
        changed |= ImGui::DragFloat( "##SkyCloudAltitude", &ProceduralSky.CloudAltitude, 10.f, 100.f, 10000.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Height of the cloud base above the ground (world Y = 0)." );
        HavanaUtils::Label( "Thickness" );
        changed |= ImGui::DragFloat( "##SkyCloudThickness", &ProceduralSky.CloudThickness, 10.f, 100.f, 8000.f, "%.0f m", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Depth of the cloud layer: the tallest clouds reach its top." );
        HavanaUtils::Label( "Wind Speed" );
        changed |= ImGui::DragFloat( "##SkyWindSpeed", &ProceduralSky.WindSpeed, 0.1f, 0.f, 100.f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "How fast the clouds drift." );
        HavanaUtils::Label( "Wind Direction" );
        changed |= ImGui::SliderFloat( "##SkyWindDirection", &ProceduralSky.WindDirection, 0.f, 360.f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp );
        CameraTooltip( "Where the wind blows towards, clockwise from north (+Z): 90 is east (+X)." );
        if( changed ) OnPropertyChanged( "ProceduralSky" );
    }
}

#endif
