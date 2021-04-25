#pragma once
#include <bgfx/bgfx.h>
#include <Math/Vector2.h>
#include "Camera/CameraData.h"
#include <queue>
#include "Device/FrameBuffer.h"
#include <RenderCommands.h>
#include "Graphics/Texture.h"

class ImGuiRenderer;
namespace Moonlight { class DynamicSky; }

struct RendererCreationSettings
{
	void* WindowPtr = nullptr;
	Vector2 InitialSize = Vector2(1280.f, 720.f);
};

enum class ViewportMode : uint8_t
{
	Game = 0,
	Editor,
	Count
};

class BGFXRenderer
{
	static constexpr bgfx::ViewId kClearView = 0;
public:
	BGFXRenderer() = default;

	void Create(const RendererCreationSettings& settings);
	void Destroy();

	void BeginFrame(const Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId);
	void Render(Moonlight::CameraData& EditorCamera);

	void RenderCameraView(Moonlight::CameraData& camera, bgfx::ViewId id);

	void RenderSingleMesh(bgfx::ViewId id, const Moonlight::MeshCommand& mesh, uint64_t state);

	void WindowResized(const Vector2& newSize);

	// Cameras
	unsigned int PushCamera(const Moonlight::CameraData& command);
	void UpdateCamera(unsigned int Id, Moonlight::CameraData& NewCommand);
	void PopCamera(unsigned int Id);
	Moonlight::CameraData& GetCamera(unsigned int Id);

	// Meshes
	unsigned int PushMesh(Moonlight::MeshCommand command);
	void UpdateMesh(unsigned int Id, Moonlight::MeshCommand& NewCommand);
	void PopMesh(unsigned int Id);
	Moonlight::MeshCommand& GetMesh(unsigned int Id);
	void UpdateMeshMatrix(unsigned int Id, glm::mat4& matrix);
	void ClearMeshes();
	SharedPtr<Moonlight::DynamicSky> GetSky();

private:
	Vector2 PreviousSize;
	Vector2 CurrentSize;

	ImGuiRenderer* ImGuiRender = nullptr;

	std::vector<Moonlight::CameraData> Cameras;
	std::queue<unsigned int> FreeCameraCommandIndicies;

	std::vector<Moonlight::MeshCommand> Meshes;
	std::queue<unsigned int> FreeMeshCommandIndicies;

	Moonlight::FrameBuffer* EditorCameraBuffer = nullptr;

	bgfx::VertexBufferHandle m_vbh;
	bgfx::IndexBufferHandle m_ibh;
	bgfx::ProgramHandle UIProgram;
	bgfx::UniformHandle s_texDiffuse;
	bgfx::UniformHandle s_texAlpha;
	bgfx::UniformHandle s_texUI;
	int32_t m_pt;
	int64_t m_timeOffset;
	Moonlight::CameraData DummyCameraData;
	SharedPtr<Moonlight::Texture> m_defaultOpacityTexture;
	SharedPtr<Moonlight::DynamicSky> m_dynamicSky;
};
