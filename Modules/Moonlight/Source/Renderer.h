#pragma once
#include <bgfx/bgfx.h>
#include <Math/Vector2.h>
#include "Camera/CameraData.h"
#include <queue>
#include "Device/FrameBuffer.h"
#include <RenderCommands.h>
#include "Graphics/Texture.h"
#include <Dementia.h>

#if USING( ME_ENABLE_RENDERDOC )
#include <Debug/RenderDocManager.h>
#endif
#include <Debug/DebugDrawer.h>
#include <Utils/CommandCache.h>
#include "Core/ISystem.h"
#include "Math/Vector4.h"

class ImGuiRenderer;

namespace Moonlight {
    class DynamicSky;
    class PickingPass;
}

struct RendererCreationSettings
{
    void* WindowPtr = nullptr;
    Vector2 InitialSize = Vector2( 1280.f, 720.f );
    bool InitAssets = true;
};

enum class ViewportMode : uint8_t
{
    Game = 0,
    Editor,
    Count
};

class BGFXRenderer
    : public ISystem
{
    static constexpr bgfx::ViewId kClearView = 0;
    static constexpr std::size_t kMeshTransparencyTempSize = 50;
public:
    ME_SYSTEM_ID( BGFXRenderer );

	BGFXRenderer()
	: m_ambient(bx::InitNone)
	, m_pt(0)
		, m_timeOffset(0)
	{
	}
    // move me, dummy :D 
    Vector4 m_time;

    void Create( const RendererCreationSettings& settings );
    void Destroy();

#if USING( ME_IMGUI )
    void BeginFrame( const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId );
#endif

    void Render( Moonlight::CameraData& EditorCamera );
    void SetGuizmoDrawCallback( std::function<void( DebugDrawer* )> GuizmoDrawingFunc );
    void RenderCameraView( Moonlight::CameraData& camera, bgfx::ViewId id );

    void RenderSingleMesh( bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state );

    void WindowResized( const Vector2& newSize );

    uint32_t GetResetFlags() const;

    // Caches
    CommandCache<Moonlight::CameraData>& GetCameraCache();
    CommandCache<Moonlight::MeshCommand>& GetMeshCache();
    CommandCache<Moonlight::DebugColliderCommand>& GetDebugDrawCache();

    // Meshes
    void UpdateMeshMatrix( unsigned int Id, const glm::mat4& matrix );
    void ClearMeshes();
    SharedPtr<Moonlight::DynamicSky> GetSky();

    void RecreateFrameBuffer( uint32_t index );


    // Settings
    enum MSAALevel
    {
        None,
        X2,
        X4,
        X8,
        X16
    };
    void SetMSAALevel( MSAALevel level );

#if USING( ME_IMGUI )
    ImGuiRenderer* GetImGuiRenderer() const;
#endif

private:
    Vector2 PreviousSize;
    Vector2 CurrentSize;
    uint32_t m_resetFlags = 0u;

    CommandCache<Moonlight::CameraData> m_cameraCache;
    CommandCache<Moonlight::MeshCommand> m_meshCache;
    CommandCache<Moonlight::DebugColliderCommand> m_debugDrawCache;
    std::vector<size_t> TransparentIndicies;

    Moonlight::FrameBuffer* EditorCameraBuffer = nullptr;
    std::function<void( DebugDrawer* )> m_guizmoCallback;
    bgfx::VertexBufferHandle m_vbh;
    bgfx::IndexBufferHandle m_ibh;
    bgfx::ProgramHandle UIProgram;
    bgfx::UniformHandle s_texDiffuse;
    bgfx::UniformHandle s_texNormal;
    bgfx::UniformHandle s_texAlpha;
    bgfx::UniformHandle s_texUI;
    bgfx::UniformHandle s_ambient;
    bgfx::UniformHandle s_sunDirection;
    bgfx::UniformHandle s_sunDiffuse;
    bgfx::UniformHandle s_time;
    bx::Vec3 m_ambient;
    int32_t m_pt;
    int64_t m_timeOffset;
    Moonlight::CameraData DummyCameraData;
    SharedPtr<Moonlight::Texture> m_defaultOpacityTexture;
    SharedPtr<Moonlight::DynamicSky> m_dynamicSky;
    bool EnableDebugDraw = false;
    UniquePtr<DebugDrawer> m_debugDraw;
    bool NeedsReset = false;

#if USING( ME_ENABLE_RENDERDOC )
    RenderDocManager* RenderDoc;
#endif

#if USING( ME_IMGUI )
    ImGuiRenderer* ImGuiRender = nullptr;
#endif

#if USING( ME_EDITOR )
    SharedPtr<Moonlight::PickingPass> m_pickingPass;
#endif

public:
    void SetDebugDrawEnabled( bool inEnabled );
};
