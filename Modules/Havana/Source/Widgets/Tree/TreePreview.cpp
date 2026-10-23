#include "TreePreview.h"

#if USING( ME_EDITOR )
#include "Camera/CameraData.h"
#include "CLog.h"
#include "Components/Camera.h"
#include "Device/FrameBuffer.h"
#include "Engine/Engine.h"
#include "Graphics/MeshData.h"
#include "Graphics/MeshLod.h"
#include "Materials/FoliageMaterial.h"
#include "Materials/StandardMaterial.h"
#include "Primitives/Primitives.h"
#include "Procedural/Tree/TreeTextures.h"
#include "RenderViews.h"
#include "Renderer.h"
#include "Utils/ImGuiUtils.h"
#include <bx/math.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace
{
    // Render groups in use by previews (bit g = group g; group 0 is the world).
    uint8_t s_usedGroups = 1;


    uint8_t AcquireGroup()
    {
        for( uint8_t group = 1; group < Moonlight::kMaxRenderGroups; ++group )
        {
            if( !( s_usedGroups & ( 1u << group ) ) )
            {
                s_usedGroups = static_cast<uint8_t>( s_usedGroups | ( 1u << group ) );
                return group;
            }
        }
        return 0;
    }


    void ReleaseGroup( uint8_t InGroup )
    {
        if( InGroup != 0 ) s_usedGroups = static_cast<uint8_t>( s_usedGroups & ~( 1u << InGroup ) );
    }


    constexpr float kFov = 40.f;
}


TreePreview::TreePreview()
    : m_group( AcquireGroup() )
{
    if( m_group == 0 )
    {
        CLog::Log( CLog::LogType::Warning, "MitchTree: too many previews open; this one stays blank" );
    }
}


TreePreview::~TreePreview()
{
    ReleaseCommands();
    for( Part& part : m_meshes ) delete part.Mesh;
    m_meshes.clear();
    if( m_camera != UINT32_MAX )
    {
        GetEngine().GetRenderer().GetCameraCache().Pop( m_camera );
    }
    delete m_buffer;
    if( bgfx::isValid( m_captureTexture ) && Moonlight::IsGpuAlive() ) bgfx::destroy( m_captureTexture );
    ReleaseGroup( m_group );
}


void TreePreview::SetMeshes( std::vector<Moonlight::MeshData*> InMeshes, bool InFrame )
{
    // Nothing may draw the old meshes once they're gone (bgfx frees their buffers when it's done).
    ReleaseCommands();
    for( Part& part : m_meshes ) delete part.Mesh;
    m_meshes.clear();
    bool first = true;
    for( Moonlight::MeshData* mesh : InMeshes )
    {
        Part part;
        part.Mesh = mesh;
        if( auto foliage = std::dynamic_pointer_cast<FoliageMaterial>( mesh->MeshMaterial ) )
        {
            part.Material = std::static_pointer_cast<FoliageMaterial>( foliage->CreateInstance() );
            part.Material->Init();
        }
        if( first )
        {
            m_boundsMin = mesh->Bounds.Min;
            m_boundsMax = mesh->Bounds.Max;
            first = false;
        }
        m_meshes.push_back( std::move( part ) );
    }
    if( InFrame ) Frame();
}


void TreePreview::Frame()
{
    const Vector3 size = m_boundsMax - m_boundsMin;
    const float height = std::max( size.y, 0.5f );
    const float radius = std::max( { size.x, size.y, size.z } ) * 0.5f;
    Focus = Vector3( ( m_boundsMin.x + m_boundsMax.x ) * 0.5f, m_boundsMin.y + height * 0.45f, ( m_boundsMin.z + m_boundsMax.z ) * 0.5f );
    Distance = std::max( radius / std::tan( glm::radians( kFov * 0.5f ) ) * 1.15f, 1.f );
}


void TreePreview::Hide()
{
    m_shown = false;
    if( m_camera != UINT32_MAX )
    {
        if( Moonlight::CameraData* camera = GetEngine().GetRenderer().GetCameraCache().Get( m_camera ) ) camera->ShouldRender = false;
    }
}


void TreePreview::EnsureCamera( uint16_t InWidth, uint16_t InHeight )
{
    BGFXRenderer& renderer = GetEngine().GetRenderer();
    if( m_camera == UINT32_MAX )
    {
        Moonlight::CameraData data;
        m_buffer = new Moonlight::FrameBuffer( InWidth, InHeight );
        data.Buffer = m_buffer;
        m_camera = renderer.GetCameraCache().Push( data );
    }
    Moonlight::CameraData& camera = *renderer.GetCameraCache().Get( m_camera );

    // The scene's sky and look (sharing its environment probe), without its fog or lens effects.
    // The studio's backdrop colour is its ambient light; its sun is the group light below (a
    // second procedural sky would fight the scene's over the one sky probe).
    const Moonlight::CameraData* scene = Camera::CurrentCamera ? renderer.GetCameraCache().Get( Camera::CurrentCamera->GetCameraId() ) : nullptr;
    const bool sceneLighting = SceneLighting && scene && scene != &camera;
    if( sceneLighting )
    {
        camera.ClearType = scene->ClearType;
        camera.ClearColor = scene->ClearColor;
        camera.Skybox = scene->Skybox;
        camera.ProceduralSky = scene->ProceduralSky;
        camera.Post = scene->Post;
    }
    else
    {
        camera.ClearType = Moonlight::ClearColorType::Color;
        camera.ClearColor = Vector3( 0.56f, 0.64f, 0.74f );
        camera.Skybox = nullptr;
        camera.Post = Moonlight::PostProcessSettings();
    }
    camera.Post.Fog = false;
    camera.Post.VolumetricFog = false;
    camera.Post.DepthOfField = false;
    camera.Post.Graphs.clear();

    const float yaw = glm::radians( Yaw );
    const float pitch = glm::radians( std::clamp( Pitch, -85.f, 85.f ) );
    const glm::vec3 toCamera( std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), -std::cos( yaw ) * std::cos( pitch ) );
    const glm::vec3 focus = Focus.InternalVector;
    const glm::vec3 eye = focus + toCamera * Distance;
    camera.Position = Vector3( eye.x, eye.y, eye.z );
    camera.Front = Vector3( -toCamera.x, -toCamera.y, -toCamera.z );
    camera.Up = Vector3( 0.f, 1.f, 0.f );
    camera.OutputSize = Vector2( InWidth, InHeight );
    camera.FOV = kFov;
    camera.Near = std::max( 0.02f, Distance * 0.005f );
    camera.Far = std::max( 200.f, Distance * 6.f );
    camera.Projection = Moonlight::ProjectionType::Perspective;
    camera.IsMain = false;
    camera.IsEditorView = false;
    camera.ShouldCull = false;
    camera.ShouldRender = true;
    camera.RenderGroup = m_group;
    camera.Shading = Wireframe ? Moonlight::ViewShadingMode::Wireframe : Moonlight::ViewShadingMode::Lit;
    camera.ViewOptions = Moonlight::ViewRenderOptions();
    camera.ViewOptions.Particles = false;
    camera.ViewOptions.Decals = false;
    camera.ViewOptions.DebugDraw = false;
    camera.ViewOptions.GameUI = false;
    camera.View = glm::lookAtLH( eye, focus, glm::vec3( 0.f, 1.f, 0.f ) );
    bx::mtxProj( &camera.ProjectionMatrix.GetInternalMatrix()[0][0], camera.FOV, float( InWidth ) / float( std::max<uint16_t>( InHeight, 1 ) ), camera.Near, camera.Far, bgfx::getCaps()->homogeneousDepth );
    camera.ViewFrustum.Update( camera.ProjectionMatrix, camera.View, camera.FOV, camera.OutputSize, camera.Near, camera.Far );

    // The sun (a procedural scene sky moves it to its own). Its shadows reach the whole tree.
    const float azimuth = glm::radians( SunAzimuth ), elevation = glm::radians( std::clamp( SunElevation, 2.f, 90.f ) );
    Moonlight::LightCommand sun;
    sun.Type = Moonlight::LightType::Directional;
    sun.Direction = Vector3( -std::sin( azimuth ) * std::cos( elevation ), -std::sin( elevation ), std::cos( azimuth ) * std::cos( elevation ) );
    sun.Color = Vector3( 3.6f, 3.4f, 3.1f );
    sun.CastShadows = true;
    sun.ShadowDistance = std::max( 30.f, Distance * 3.f );
    camera.GroupLights = { sun };
}


void TreePreview::ReleaseCommands()
{
    if( !Moonlight::IsGpuAlive() ) return;
    auto& cache = GetEngine().GetRenderer().GetMeshCache();
    const bool valid = m_meshGeneration == GetEngine().GetRenderer().GetMeshCacheGeneration();
    for( Part& part : m_meshes )
    {
        if( part.Command != UINT32_MAX && valid ) cache.Pop( part.Command );
        part.Command = UINT32_MAX;
    }
    if( m_groundCommand != UINT32_MAX && valid ) cache.Pop( m_groundCommand );
    m_groundCommand = UINT32_MAX;
}


void TreePreview::SubmitCommands()
{
    BGFXRenderer& renderer = GetEngine().GetRenderer();
    auto& cache = renderer.GetMeshCache();
    // A scene change empties the mesh cache: push again.
    if( m_meshGeneration != renderer.GetMeshCacheGeneration() )
    {
        for( Part& part : m_meshes ) part.Command = UINT32_MAX;
        m_groundCommand = UINT32_MAX;
        m_meshGeneration = renderer.GetMeshCacheGeneration();
    }

    // One level for the whole tree, chosen like the game does (or forced).
    const AABB bounds( m_boundsMin, m_boundsMax );
    const Moonlight::CameraData* camera = renderer.GetCameraCache().Get( m_camera );
    uint8_t lod = 0;
    if( !m_meshes.empty() && m_meshes.front().Mesh->GetLodCount() > 0 )
    {
        const Moonlight::MeshData& mesh = *m_meshes.front().Mesh;
        if( ForcedLod >= 0 )
        {
            lod = static_cast<uint8_t>( std::min<int>( ForcedLod, mesh.GetLodCount() ) );
        }
        else if( camera )
        {
            const float radius = bounds.GetExtents().Length();
            const float distance = ( bounds.GetCenter() - camera->Position ).Length();
            lod = Moonlight::SelectLod( Moonlight::RelativeScreenHeight( radius, distance, camera->FOV, false, 1.f ), mesh.GetLodHeights(), mesh.GetLodCount() );
        }
    }
    m_drawnLod = lod;

    for( Part& part : m_meshes )
    {
        SharedPtr<Moonlight::Material> material = part.Mesh->MeshMaterial;
        if( part.Material )
        {
            // The recipe's settings, with the wind switched off on request.
            const auto source = std::static_pointer_cast<FoliageMaterial>( part.Mesh->MeshMaterial );
            part.Material->Foliage = source->Foliage;
            if( !Wind )
            {
                part.Material->Foliage.TrunkLean = part.Material->Foliage.BranchSway = part.Material->Foliage.LeafFlutter = 0.f;
            }
            material = part.Material;
        }
        Moonlight::MeshCommand command;
        Moonlight::FillMeshCommand( command, part.Mesh, material, lod );
        command.Transform = glm::mat4( 1.f );
        command.Visible = true;
        command.CastShadows = true;
        command.WorldBounds = part.Mesh->Bounds;
        command.RenderGroup = m_group;
        if( part.Command == UINT32_MAX ) part.Command = cache.Push( command );
        else cache.Update( part.Command, command );
    }

    // A ground disc under the crown, to catch the shadow.
    if( Moonlight::MeshData* plane = Moonlight::Primitives::Get( Moonlight::MeshType::Plane ) )
    {
        if( !m_groundMaterial )
        {
            auto ground = MakeShared<StandardMaterial>();
            ground->Init();
            ground->DiffuseColor = Vector3( 0.46f, 0.47f, 0.4f );
            ground->Roughness = 0.95f;
            m_groundMaterial = ground;
        }
        const Vector3 size = m_boundsMax - m_boundsMin;
        const float extent = std::max( { size.x, size.z, size.y * 0.6f, 2.f } ) * 0.9f;
        Moonlight::MeshCommand command;
        Moonlight::FillMeshCommand( command, plane, m_groundMaterial );
        command.Transform = glm::scale( glm::mat4( 1.f ), glm::vec3( extent, 1.f, extent ) );
        command.Visible = ShowGround;
        command.CastShadows = false;
        command.WorldBounds = plane->Bounds.Transformed( Matrix4( command.Transform ) );
        command.RenderGroup = m_group;
        if( m_groundCommand == UINT32_MAX ) m_groundCommand = cache.Push( command );
        else cache.Update( m_groundCommand, command );
    }
}


void TreePreview::HandleInput( bool InHovered )
{
    ImGuiIO& io = ImGui::GetIO();
    const bool pan = ImGui::IsMouseDragging( ImGuiMouseButton_Middle ) || ( io.KeyShift && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) );
    if( ImGui::IsItemActive() )
    {
        if( pan )
        {
            // Along the view plane, scaled so the point under the cursor follows it.
            const float yaw = glm::radians( Yaw );
            const glm::vec3 right( std::cos( yaw ), 0.f, std::sin( yaw ) );
            const float metresPerPixel = 2.f * Distance * std::tan( glm::radians( kFov * 0.5f ) ) / std::max( ImGui::GetItemRectSize().y, 1.f );
            const glm::vec3 move = right * ( -io.MouseDelta.x * metresPerPixel ) + glm::vec3( 0.f, 1.f, 0.f ) * ( io.MouseDelta.y * metresPerPixel );
            Focus = Vector3( Focus.x + move.x, Focus.y + move.y, Focus.z + move.z );
        }
        else if( ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            Yaw -= io.MouseDelta.x * 0.4f;
            Pitch = std::clamp( Pitch + io.MouseDelta.y * 0.3f, -85.f, 85.f );
        }
    }
    if( InHovered && io.MouseWheel != 0.f )
    {
        Distance = std::clamp( Distance * std::pow( 0.88f, io.MouseWheel ), 0.5f, 2000.f );
    }
    if( InHovered && ImGui::IsKeyPressed( ImGuiKey_F, false ) )
    {
        Frame();
    }
}


void TreePreview::Draw( const ImVec2& InSize )
{
    const uint16_t width = static_cast<uint16_t>( std::clamp( InSize.x, 16.f, 4096.f ) );
    const uint16_t height = static_cast<uint16_t>( std::clamp( InSize.y, 16.f, 4096.f ) );
    if( m_group == 0 )
    {
        ImGui::Dummy( InSize );
        return;
    }
    EnsureCamera( width, height );
    SubmitCommands();
    if( m_shown ) ++m_renderedFrames;
    m_shown = true;

    // The image (last frame's until the camera has rendered once at this size).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if( m_buffer && bgfx::isValid( m_buffer->Buffer ) && m_buffer->Width > 0 && m_buffer->Height > 0 )
    {
        ImGui::Image( bgfx::getTexture( m_buffer->Buffer ), InSize, ImVec2( 0.f, 0.f ), ImVec2( std::min( 1.f, InSize.x / m_buffer->Width ), std::min( 1.f, InSize.y / m_buffer->Height ) ) );
    }
    else
    {
        ImGui::Dummy( InSize );
    }
    ImGui::SetCursorScreenPos( origin );
    ImGui::InvisibleButton( "##TreePreviewInput", InSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle );
    HandleInput( ImGui::IsItemHovered() );
    UpdateCapture();
}


void TreePreview::RequestCapture( const std::string& InFullPath )
{
    m_capturePath = InFullPath;
    m_captureError.clear();
    m_captureFrame = 0;
}


void TreePreview::UpdateCapture()
{
    if( m_capturePath.empty() || !m_buffer || !bgfx::isValid( m_buffer->Texture ) ) return;
    // Not while the camera's targets are about to be recreated at a new size.
    const Moonlight::CameraData* camera = GetEngine().GetRenderer().GetCameraCache().Get( m_camera );
    if( !camera || m_renderedFrames < 2 || static_cast<uint32_t>( camera->OutputSize.x ) != m_buffer->Width || static_cast<uint32_t>( camera->OutputSize.y ) != m_buffer->Height ) return;
    const uint32_t frame = GetEngine().m_frameRenderSettings.m_currentFrame;
    if( m_captureFrame == 0 )
    {
        // The camera must have rendered at its current size before its image is copied.
        if( m_buffer->Width != m_captureWidth || m_buffer->Height != m_captureHeight || !bgfx::isValid( m_captureTexture ) )
        {
            if( bgfx::isValid( m_captureTexture ) ) bgfx::destroy( m_captureTexture );
            m_captureWidth = static_cast<uint16_t>( m_buffer->Width );
            m_captureHeight = static_cast<uint16_t>( m_buffer->Height );
            m_captureTexture = bgfx::createTexture2D( m_captureWidth, m_captureHeight, false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK );
        }
        m_captureData.assign( static_cast<size_t>( m_captureWidth ) * m_captureHeight * 4, 0 );
        bgfx::TextureRegion destination, source;
        destination.init( m_captureTexture );
        source.init( m_buffer->Texture );
        bgfx::blit( Moonlight::RenderView::EditorCapture, destination, source );
        m_captureFrame = bgfx::read( destination, m_captureData.data() );
        return;
    }
    if( frame < m_captureFrame ) return;
    MitchTree::Image image;
    image.Width = m_captureWidth;
    image.Height = m_captureHeight;
    image.Pixels = m_captureData;
    for( size_t i = 0; i + 3 < image.Pixels.size(); i += 4 )
    {
        std::swap( image.Pixels[i], image.Pixels[i + 2] );   // BGRA to RGBA
        image.Pixels[i + 3] = 255;
    }
    std::string error;
    if( !MitchTree::WritePng( image, m_capturePath, error ) ) m_captureError = error;
    m_capturePath.clear();
    m_captureFrame = 0;
}
#endif
