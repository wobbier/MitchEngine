#pragma once
#include <bgfx/bgfx.h>
#include <Math/Vector2.h>
#include "Camera/CameraData.h"
#include <queue>

class ImGuiRenderer;

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
	const bgfx::ViewId kClearView = 0;

public:
	BGFXRenderer() = default;

	void Create(const RendererCreationSettings& settings);
	void Destroy();

	void BeginFrame(Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId);
	void Render(Moonlight::CameraData& MainCamera);

	void WindowResized(const Vector2& newSize);

	void UpdateCamera(unsigned int Id, Moonlight::CameraData& NewCommand);
	Moonlight::CameraData& GetCamera(unsigned int Id);

	unsigned int PushCamera(Moonlight::CameraData& command);
	void PopCamera(unsigned int Id);

private:
	Vector2 PreviousSize;
	Vector2 CurrentSize;

	ImGuiRenderer* ImGuiRender = nullptr;

	std::vector<Moonlight::CameraData> Cameras;
	std::queue<unsigned int> FreeCameraCommandIndicies;


	bgfx::VertexBufferHandle m_vbh;
	bgfx::IndexBufferHandle m_ibh[5];
	bgfx::ProgramHandle CubeProgram;
	int64_t m_timeOffset;
	int32_t m_pt;

	bool m_r;
	bool m_g;
	bool m_b;
	bool m_a;
};