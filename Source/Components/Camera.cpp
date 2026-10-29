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
#include "Utils/ImGuiUtils.h"
#endif

ME_REFLECT_ENUM( Moonlight::ProjectionType, { { "Perspective", Moonlight::ProjectionType::Perspective }, { "Orthographic", Moonlight::ProjectionType::Orthographic } } )
ME_REFLECT_ENUM( Moonlight::UpscaleFilter, { { "Bilinear", Moonlight::UpscaleFilter::Bilinear }, { "Nearest", Moonlight::UpscaleFilter::Nearest }, { "Pixel", Moonlight::UpscaleFilter::Pixel }, { "Sharp", Moonlight::UpscaleFilter::Sharp } } )
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
    ME_FIELD( MoonSize ).Display( "Moon Size" ).Range( 0.1f, 100.f );
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
    ME_FIELD( Viewport ).Display( "Viewport Rect" ).Speed( 0.01f ).Tooltip( "Normalized x, y (from the top left), width and height of the screen this camera draws to, for split screen" );
    ME_FIELD( ClearType ).Display( "Background" ).CustomInspector();
    ME_FIELD( ClearColor ).Color().CustomInspector();
    ME_FIELD( ProceduralSky ).Display( "Procedural Sky" ).CustomInspector();
    ME_FIELD( MatchMainCamera ).Display( "Match Main Camera" ).Tooltip( "Render at the main camera's size, field of view, far plane and background (for portal and screen-space views)" );
    ME_FIELD( Zoom ).Hidden();
    ME_FIELD( TargetTexture ).Category( "Render Texture" ).Display( "Target Texture" ).Asset( "RenderTexture" ).Tooltip( "Render into this render texture, at its size, instead of the screen. Materials show the image by using the same asset as a texture" );
    ME_FIELD( UpdateInterval ).Category( "Render Texture" ).Display( "Update Interval" ).Range( 1.f, 60.f ).Tooltip( "Render every Nth frame; the texture keeps its last image in between" );
    ME_FIELD( SunShadows ).Category( "Render Passes" ).Display( "Sun Shadows" ).Tooltip( "Draw the sun's shadow cascades for this camera. Spot and point light shadows are shared by every camera and stay" );
    ME_FIELD( AmbientOcclusion ).Category( "Render Passes" ).Display( "Ambient Occlusion" ).Tooltip( "Allow screen-space ambient occlusion when the post settings turn it on" );
    ME_FIELD( VolumetricFog ).Category( "Render Passes" ).Display( "Volumetric Fog" ).Tooltip( "Allow volumetric fog when the post settings turn it on; off falls back to the analytic height fog" );
    ME_FIELD( RenderScale ).Category( "Render Scale" ).Display( "Render Scale" ).Range( 0.1f, 1.f ).Tooltip( "Render the scene at this fraction of the output size, then upscale it (0.5 = a quarter of the pixels)" );
    ME_FIELD( Upscale ).Category( "Render Scale" ).Display( "Upscale Filter" ).Tooltip( "Bilinear: smooth. Nearest: blocky. Pixel: whole N x N texel blocks for pixel art (the scale snaps to 1/N). Sharp: bicubic plus contrast-adaptive sharpening" );
    ME_FIELD( UpscaleSharpness ).Category( "Render Scale" ).Display( "Sharpness" ).Range( 0.f, 1.f ).Tooltip( "Sharp filter only" );
    ME_FIELD( DynamicResolution ).Category( "Render Scale" ).Display( "Dynamic Resolution" ).Tooltip( "Lower the scale while the GPU frame takes longer than the target, and raise it back when there's headroom" );
    ME_FIELD( DynamicMinScale ).Category( "Render Scale" ).Display( "Minimum Scale" ).Range( 0.1f, 1.f );
    ME_FIELD( DynamicTargetMs ).Category( "Render Scale" ).Display( "Target GPU ms" ).Range( 1.f, 100.f );
    ME_FIELD( PixelPerfect ).Category( "Pixel Perfect" ).Display( "Pixel Perfect" ).Tooltip( "Render pixel art at the reference resolution times the largest whole factor that fits, as exact blocks (overrides Render Scale and the upscale filter)" );
    ME_FIELD( ReferenceResolution ).Category( "Pixel Perfect" ).Display( "Reference Resolution" ).Speed( 1.f ).Tooltip( "The art's native resolution: the least the camera shows" );
    ME_FIELD( PixelsPerUnit ).Category( "Pixel Perfect" ).Display( "Pixels Per Unit" ).Range( 0.f, 512.f ).Tooltip( "Orthographic only: texels per world unit (sets the view size). 0 keeps Orthographic Size" );
    ME_FIELD( PixelSnap ).Category( "Pixel Perfect" ).Display( "Pixel Snap" ).Tooltip( "Orthographic only: move the view in whole texels so a panning camera doesn't shimmer" );
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
    // A render texture camera never takes over the screen by itself.
    if( !CurrentCamera && !HasTargetTexture() )
    {
        CurrentCamera = this;
    }
}

void Camera::OnPropertyChanged( const std::string& InFieldName )
{
    Near = std::max( Near, 0.01f );
    Far = std::max( Far, Near + 0.01f );
    UpdateInterval = std::max( UpdateInterval, 1 );
    ProceduralSky.Clamp();
}


bool Camera::HasTargetTexture() const
{
    return !TargetTexture.GetLocalPathString().empty();
}


bool Camera::RendersToTexture() const
{
    return HasTargetTexture() && !IsMain();
}


Moonlight::Texture* Camera::ResolveTargetTexture()
{
    const std::string& path = TargetTexture.GetLocalPathString();
    if( path != m_loadedTarget )
    {
        m_loadedTarget = path;
        m_target = nullptr;
        if( !path.empty() && TargetTexture.Exists && Moonlight::RenderTextureSettings::IsRenderTexturePath( path ) )
        {
            // The same cached texture the materials showing it hold.
            m_target = ResourceCache::GetInstance().GetAsync<Moonlight::Texture>( TargetTexture );
        }
    }
    // A fixed file reloads a texture that failed to load, so check the target rather than the state.
    if( !m_target || m_target->IsLoading() || !m_target->IsRenderTarget() || !bgfx::isValid( m_target->GetRenderTarget() ) )
    {
        return nullptr;
    }
    return m_target.get();
}


void Camera::ResetSkyClock()
{
    m_skyStartTime = m_skyTime = ProceduralSky.Time;
    m_moonStartPhase = m_moonPhase = ProceduralSky.MoonPhase;
    m_skyAnimationTime = 0.f;
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
    if( ClearType == Moonlight::ClearColorType::Procedural && deltaTime > 0.f )
    {
        m_skyAnimationTime += deltaTime;
        if( ProceduralSky.TimeScale > 0.f )
        {
            const float hours = ProceduralSky.TimeScale * deltaTime;
            m_skyTime = std::fmod( m_skyTime + hours, 24.f );
            // A lunar month is 29.53 days: the moon rises about 50 minutes later each day.
            m_moonPhase = std::fmod( m_moonPhase + hours / ( 24.f * 29.53f ), 1.f );
        }
    }
    Moonlight::ProceduralSkySettings settings = ProceduralSky;
    settings.Time = m_skyTime;
    settings.MoonPhase = m_moonPhase;
    settings.AnimationTime = m_skyAnimationTime;
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
    const Vector2 size = GetViewportSize();
    return ( size.x / size.y );
}


bool Camera::HasPartialViewport() const
{
    return !RendersToTexture() && ( Viewport.x != 0.f || Viewport.y != 0.f || Viewport.z != 1.f || Viewport.w != 1.f );
}


Vector2 Camera::GetViewportSize() const
{
    if( RendersToTexture() )
    {
        return Vector2( std::max( 1.f, OutputSize.x ), std::max( 1.f, OutputSize.y ) );
    }
    const float width = std::clamp( Viewport.z, 0.01f, 1.f );
    const float height = std::clamp( Viewport.w, 0.01f, 1.f );
    return Vector2( std::max( 1.f, std::round( OutputSize.x * width ) ), std::max( 1.f, std::round( OutputSize.y * height ) ) );
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

void Camera::SetClipPlane( const Vector3& InPoint, const Vector3& InNormal )
{
    m_hasClipPlane = true;
    m_clipPoint = InPoint;
    m_clipNormal = InNormal;
}


void Camera::ClearClipPlane()
{
    m_hasClipPlane = false;
    isOblique = false;
}


// Lengyel's oblique near plane: the near plane becomes clipPlane (view space, visible side
// positive), the far plane tilts to keep depth in range.
Matrix4 CalculateObliqueMatrix( const Matrix4& projection, const glm::vec4& clipPlane )
{
    glm::mat4 m = projection.GetInternalMatrix();
    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    // The view-space corner of the frustum opposite the plane, on the far plane.
    // (A zero component still picks a side: a centre row would tilt the far plane through the view.)
    auto side = []( float value ) { return value < 0.f ? -1.f : 1.f; };
    const glm::vec4 corner = glm::inverse( m ) * glm::vec4( side( clipPlane.x ), side( clipPlane.y ), 1.f, 1.f );
    const glm::vec4 c = clipPlane * ( ( homogeneousDepth ? 2.f : 1.f ) / glm::dot( clipPlane, corner ) );
    // Replace the projection's third row (glm is column-major: m[column][row]).
    for( int column = 0; column < 4; ++column )
    {
        m[column][2] = c[column] - ( homogeneousDepth ? m[column][3] : 0.f );
    }
    return Matrix4( m );
}

void Camera::GetPixelPerfectSize( uint32_t& OutFactor, uint32_t& OutWidth, uint32_t& OutHeight ) const
{
    const Vector2 size = GetViewportSize();
    Moonlight::ComputePixelPerfect( static_cast<uint32_t>( size.x ), static_cast<uint32_t>( size.y ),
        static_cast<uint32_t>( std::max( ReferenceResolution.x, 1.f ) ), static_cast<uint32_t>( std::max( ReferenceResolution.y, 1.f ) ), OutFactor, OutWidth, OutHeight );
}


Matrix4 Camera::GetProjectionMatrix() const
{
    // #TODO: output size in the game view of the editor is messed up cause it's not reading the output size of the editor window widget.
    Matrix4 outMatrix;
    const Vector2 size = GetViewportSize();
    if( Projection == Moonlight::ProjectionType::Perspective )
    {
        bx::mtxProj( &outMatrix.GetInternalMatrix()[0][0], m_FOV, float( size.x ) / float( size.y ), std::max( Near, 0.01f ), Far, bgfx::getCaps()->homogeneousDepth );
    }
    else
    {
        float halfHeight = std::max( OrthographicSize, 0.001f );
        float halfWidth = size.y > 0.f ? halfHeight * size.x / size.y : halfHeight;
        if( PixelPerfect && PixelsPerUnit > 0.f )
        {
            // Exactly PixelsPerUnit texels per unit over the internal image (square texels).
            uint32_t factor = 1;
            uint32_t width = 1;
            uint32_t height = 1;
            GetPixelPerfectSize( factor, width, height );
            halfWidth = 0.5f * static_cast<float>( width ) / PixelsPerUnit;
            halfHeight = 0.5f * static_cast<float>( height ) / PixelsPerUnit;
        }
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
    ResetSkyClock();
    if( inJson.contains( "Skybox" ) && inJson["Skybox"].is_string() && !Moonlight::RenderTextureSettings::IsRenderTexturePath( inJson["Skybox"].get<std::string>() ) )
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


void Camera::DrawTargetPreview()
{
    const Moonlight::Texture* target = ResolveTargetTexture();
    if( !target )
    {
        const char* reason = !TargetTexture.Exists ? "The target texture doesn't exist."
            : !Moonlight::RenderTextureSettings::IsRenderTexturePath( TargetTexture.GetLocalPathString() ) ? "The target must be a .rendertexture asset."
            : m_target && m_target->IsLoading() ? "Loading the target texture..."
            : "The target texture couldn't be loaded (see the log).";
        ImGui::TextWrapped( "%s", reason );
        return;
    }
    // Live: the texture as materials see it this frame.
    const float width = std::max( 1.f, std::min( ImGui::GetContentRegionAvail().x, ImGuiTheme::Em( 21.f ) ) );
    const float height = width * target->mHeight / std::max( 1.f, static_cast<float>( target->mWidth ) );
    ImGui::Image( target->TexHandle, ImVec2( width, height ) );
    if( target->GetRenderCount() == 0 )
    {
        ImGui::TextDisabled( "%d x %d, not rendered yet", target->mWidth, target->mHeight );
    }
    else
    {
        ImGui::TextDisabled( "%d x %d, every %d frame%s", target->mWidth, target->mHeight, UpdateInterval, UpdateInterval == 1 ? "" : "s" );
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
            if( HasTargetTexture() )
            {
                ImGui::TextWrapped( "The main camera renders to the screen, so its Target Texture is ignored. Make another camera current to render this one into the texture." );
            }
        }
        else if( HasTargetTexture() )
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored( ImGuiTheme::AccentText(), "Render Texture" );
            CameraTooltip( "This camera renders into its Target Texture." );
            DrawTargetPreview();
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
            evt.ImagesOnly = true;
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
        CameraTooltip( "Sky hours per game second during Play. Stops in Edit mode and while paused; zero holds the time of day." );
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
        changed |= ImGui::DragFloat( "##SkyMoonSize", &ProceduralSky.MoonSize, 0.01f, 0.1f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
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
