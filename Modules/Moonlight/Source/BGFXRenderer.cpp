#include <BGFXRenderer.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <ImGui/ImGuiRenderer.h>
#include "optick.h"
#include "imgui.h"
#include "Utils/BGFXUtils.h"
#include "bx/timer.h"
#include "Window/IWindow.h"
#include "Engine/Engine.h"
#include "Widgets/AssetBrowser.h"

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
//#include <bgfx/embedded_shader.h>
//
//// embedded shaders
//#include "Graphics/cubes_frag.h"
//#include "Graphics/cubes_vert.h"

static bool s_showStats = false;

namespace
{



		//static const bgfx::EmbeddedShader s_embeddedShaders[] =
		//{
		//	BGFX_EMBEDDED_SHADER(cubes_vert),
		//	BGFX_EMBEDDED_SHADER(cubes_frag),

		//	BGFX_EMBEDDED_SHADER_END()
		//};

	struct PosColorVertex
	{
		float m_x;
		float m_y;
		float m_z;
		uint32_t m_abgr;

		static void init()
		{
			ms_layout
				.begin()
				.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
				.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
				.end();
		};

		static bgfx::VertexLayout ms_layout;
	};

	bgfx::VertexLayout PosColorVertex::ms_layout;

	static PosColorVertex s_cubeVertices[] =
	{
		{-1.0f,  1.0f,  1.0f, 0xff000000 },
		{ 1.0f,  1.0f,  1.0f, 0xff0000ff },
		{-1.0f, -1.0f,  1.0f, 0xff00ff00 },
		{ 1.0f, -1.0f,  1.0f, 0xff00ffff },
		{-1.0f,  1.0f, -1.0f, 0xffff0000 },
		{ 1.0f,  1.0f, -1.0f, 0xffff00ff },
		{-1.0f, -1.0f, -1.0f, 0xffffff00 },
		{ 1.0f, -1.0f, -1.0f, 0xffffffff },
	};

	static const uint16_t s_cubeTriList[] =
	{
		0, 1, 2, // 0
		1, 3, 2,
		4, 6, 5, // 2
		5, 6, 7,
		0, 2, 4, // 4
		4, 2, 6,
		1, 5, 3, // 6
		5, 7, 3,
		0, 4, 1, // 8
		4, 5, 1,
		2, 3, 6, // 10
		6, 3, 7,
	};

	/*static const uint64_t s_ptState[]
	{
		UINT64_C(0),
		BGFX_STATE_PT_TRISTRIP,
		BGFX_STATE_PT_LINES,
		BGFX_STATE_PT_LINESTRIP,
		BGFX_STATE_PT_POINTS,
	};*/
}


void BGFXRenderer::Create(const RendererCreationSettings& settings)
{
	PreviousSize = settings.InitialSize;
	// Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
	// Most graphics APIs must be used on the same thread that created the window.
	bgfx::renderFrame();
	// Initialize bgfx using the native window handle and window resolution.
	bgfx::Init init;
	init.platformData.nwh = settings.WindowPtr;
	init.resolution.width = static_cast<uint32_t>(PreviousSize.x);
	init.resolution.height = static_cast<uint32_t>(PreviousSize.y);
	init.resolution.reset = BGFX_RESET_VSYNC;

	CurrentSize = settings.InitialSize;

	if (!bgfx::init(init))
	{
		return;
	}

	EditorCameraBuffer = new Moonlight::FrameBuffer(init.resolution.width, init.resolution.height);

	if(true)
	{
		// Set view 0 clear state.
		bgfx::setViewClear(kClearView
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, 0x303030ff
			, 1.0f
			, 0
		);
		//bgfx::setViewClear(1
		//	, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
		//	, 0x303030ff
		//	, 1.0f
		//	, 0
		//);
		// Create vertex stream declaration.
		PosColorVertex::init();

		// Create static vertex buffer.
		m_vbh = bgfx::createVertexBuffer(
			// Static data can be passed with bgfx::makeRef
			bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices))
			, PosColorVertex::ms_layout
		);

		// Create static index buffer for triangle list rendering.
		m_ibh = bgfx::createIndexBuffer(
			// Static data can be passed with bgfx::makeRef
			bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList))
		);

		CubeProgram = Moonlight::LoadProgram("cubes.vert", "cubes.frag");

		//bgfx::RendererType::Enum type = bgfx::getRendererType();
		//bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(s_embeddedShaders, type, "cubes_vert");
		//bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(s_embeddedShaders, type, "cubes_frag");
		//// Create program from shaders.
		//CubeProgram = bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);

		m_timeOffset = bx::getHPCounter();
	}

	ImGuiRender = new ImGuiRenderer();
	ImGuiRender->Create();
}

void BGFXRenderer::Destroy()
{
	bgfx::shutdown();
}

void BGFXRenderer::BeginFrame(Vector2& mousePosition, uint8_t mouseButton, int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId)
{
	ImGuiRender->NewFrame(mousePosition, mouseButton, scroll, outputSize, inputChar, viewId);
}

void BGFXRenderer::Render(Moonlight::CameraData& EditorCamera)
{
	OPTICK_EVENT("Renderer::Render", Optick::Category::Rendering);
	ImGui::Begin("Debug");
	ImGui::Checkbox("Show Frame Stats", &s_showStats);
	ImGui::End();

	ImGuiRender->EndFrame();

	// Use debug font to print information about this example.
	bgfx::dbgTextClear();
	// Enable stats or debug text.
	bgfx::setDebug(s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);
	// Advance to next frame. Process submitted rendering primitives.

	if (CurrentSize != PreviousSize)
	{
		PreviousSize = CurrentSize;
		bgfx::reset((uint32_t)CurrentSize.x, (uint32_t)CurrentSize.y, BGFX_RESET_VSYNC);
		bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
	}
#if ME_EDITOR
	bgfx::ViewId id = 1;

	EditorCamera.Buffer = EditorCameraBuffer;
	RenderCameraView(EditorCamera, id);
	++id;
	for (auto& camData : Cameras)
	{
		RenderCameraView(camData, id);
		++id;
	}
#else
	bgfx::ViewId id = kClearView + 1;
	for (auto& camData : Cameras)
	{
		if (!camData.IsMain)
		{
			RenderCameraView(camData, id);
		}
		++id;
	}

	for (auto& camData : Cameras)
	{
		if (camData.IsMain)
		{
			RenderCameraView(camData, kClearView);
			break;
		}
	}
#endif
	{

		{
			OPTICK_EVENT("Renderer::Frame", Optick::Category::Rendering);
			bgfx::frame();
		}
	}
}


void BGFXRenderer::RenderCameraView(Moonlight::CameraData& camera, bgfx::ViewId id)
{
	if (true)
	{
		if (CurrentSize.IsZero())
		{
			return;
		}
		if (!camera.Buffer)
		{
			return;
		}

		float time = (float)((bx::getHPCounter() - m_timeOffset) / double(bx::getHPFrequency()));

		const bx::Vec3 eye = { camera.Position.x, camera.Position.y, camera.Position.z };
		const bx::Vec3 at = { camera.Position.x + camera.Front.x, camera.Position.y + camera.Front.y, camera.Position.z + camera.Front.z };

		// Set view and projection matrix for view 0.
		{
			float view[16];
			bx::mtxLookAt(view, eye, at);

			float proj[16];
			bx::mtxProj(proj, camera.FOV, float(camera.OutputSize.x) / float(camera.OutputSize.y), camera.Near, camera.Far, bgfx::getCaps()->homogeneousDepth);
			bgfx::setViewTransform(id, view, proj);
			if (id > 0)
			{
				bgfx::setViewFrameBuffer(id, camera.Buffer->Buffer);
			}

			// Set view 0 default viewport.
			bgfx::setViewRect(id, 0, 0, uint16_t(camera.OutputSize.x), uint16_t(camera.OutputSize.y));
		}

		//bgfx::touch(0);
		uint32_t color = (uint32_t)(camera.ClearColor.x * 255.f) << 24 | (uint32_t)(camera.ClearColor.y * 255.f) << 16 | (uint32_t)(camera.ClearColor.z * 255.f) << 8 | 255;
		bgfx::setViewClear(id
			, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
			, color, 1.0f, 0
		);

		bgfx::touch(id);
		uint64_t state = 0
			| BGFX_STATE_WRITE_RGB
			| BGFX_STATE_WRITE_A
			| BGFX_STATE_WRITE_Z
			| BGFX_STATE_DEPTH_TEST_LESS
			| BGFX_STATE_CULL_CW
			| BGFX_STATE_MSAA
			| UINT64_C(0) // tris
			;

		// Submit 11x11 cubes.
		for (uint32_t yy = 0; yy < 11; ++yy)
		{
			for (uint32_t xx = 0; xx < 11; ++xx)
			{
				float mtx[16];
				bx::mtxRotateXY(mtx, time + xx * 0.21f, time + yy * 0.37f);
				mtx[12] = -15.0f + float(xx) * 3.0f;
				mtx[13] = -15.0f + float(yy) * 3.0f;
				mtx[14] = 0.0f;

				// Set model matrix for rendering.
				bgfx::setTransform(mtx);

				// Set vertex and index buffer.
				bgfx::setVertexBuffer(0, m_vbh);
				bgfx::setIndexBuffer(m_ibh);

				// Set render states.
				bgfx::setState(state);

				// Submit primitive for rendering to view 0.
				bgfx::submit(id, CubeProgram);
			}
		}
	}
}

void BGFXRenderer::WindowResized(const Vector2& newSize)
{
	CurrentSize = newSize;
}

void BGFXRenderer::UpdateCamera(unsigned int Id, Moonlight::CameraData& NewCommand)
{
	if (Id >= Cameras.size())
	{
		return;
	}

	Cameras[Id] = NewCommand;
}

Moonlight::CameraData& BGFXRenderer::GetCamera(unsigned int Id)
{
	if (Id >= Cameras.size())
	{
		return Cameras[0];
	}

	return Cameras[Id];
}

unsigned int BGFXRenderer::PushCamera(Moonlight::CameraData& command)
{
	unsigned int index = 0;
	if (!FreeCameraCommandIndicies.empty())
	{
		index = FreeCameraCommandIndicies.front();
		FreeCameraCommandIndicies.pop();
		Cameras[index] = std::move(command);
	}
	else
	{
		Cameras.push_back(std::move(command));
		index = static_cast<unsigned int>(Cameras.size() - 1);
	}

	Moonlight::CameraData& data = Cameras[index];

	delete data.Buffer;
	data.Buffer = new Moonlight::FrameBuffer(static_cast<uint32_t>(data.OutputSize.x), static_cast<uint32_t>(data.OutputSize.y));

	//data.FrameBuffer = bgfx::createFrameBuffer(GetEngine().GetWindow()->GetWindowPtr(), data.OutputSize.X(), data.OutputSize.Y());

	//delete data.Buffer;
	//if (data.IsMain)
	//{
	//	GameViewRTT = data.Buffer = m_device->CreateFrameBuffer(static_cast<UINT>(m_device->GetOutputSize().X()), static_cast<UINT>(m_device->GetOutputSize().Y()), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
	//}
	//else
	//{
	//	data.Buffer = m_device->CreateFrameBuffer(static_cast<UINT>(m_device->GetOutputSize().X()), static_cast<UINT>(m_device->GetOutputSize().Y()), 1, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT);
	//}

	return index;
}

void BGFXRenderer::PopCamera(unsigned int Id)
{
	if (Id > Cameras.size())
	{
		return;
	}

// 	if (Cameras[Id].Buffer == GameViewRTT)
// 	{
// 		//GameViewRTT = nullptr;
// 	}
	//delete Cameras[Id].Buffer;

	FreeCameraCommandIndicies.push(Id);
	Cameras[Id] = Moonlight::CameraData();
}