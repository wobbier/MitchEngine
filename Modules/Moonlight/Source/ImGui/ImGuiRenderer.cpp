#include <ImGui/ImGuiRenderer.h>

#include <bgfx/embedded_shader.h>
#include <bx/math.h>
#include <imgui.h>
#include <ImGui/Resources/vs_ocornut_imgui.bin.h>
#include <ImGui/Resources/fs_ocornut_imgui.bin.h>
#include <ImGui/Resources/vs_imgui_image.bin.h>
#include <ImGui/Resources/fs_imgui_image.bin.h>
#include <ImGui/Resources/roboto_regular.ttf.h>
#include <ImGui/Resources/robotomono_regular.ttf.h>
#include <ImGui/Resources/icons_kenney.ttf.h>
#include <ImGui/Resources/icons_font_awesome.ttf.h>
#include "optick.h"

#define IMGUI_MBUT_LEFT   0x01
#define IMGUI_MBUT_RIGHT  0x02
#define IMGUI_MBUT_MIDDLE 0x04

inline bool checkAvailTransientBuffers(uint32_t _numVertices, const bgfx::VertexLayout& _layout, uint32_t _numIndices)
{
	return _numVertices == bgfx::getAvailTransientVertexBuffer(_numVertices, _layout)
		&& (0 == _numIndices || _numIndices == bgfx::getAvailTransientIndexBuffer(_numIndices));
}

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(vs_imgui_image),
	BGFX_EMBEDDED_SHADER(fs_imgui_image),

	BGFX_EMBEDDED_SHADER_END()
};

void ImGuiRenderer::Create()
{
	ViewId = 255;
	LastScroll = 0;
	//Last = bx::getHPCounter();

	IMGUI_CHECKVERSION();
	Context = ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();

	io.DisplaySize = ImVec2(1280, 720);
	io.DeltaTime = 1.f / 60.f;
	io.IniFilename = nullptr;

	auto renderType = bgfx::getRendererType();

	Program = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, renderType, "vs_ocornut_imgui")
		, bgfx::createEmbeddedShader(s_embeddedShaders, renderType, "fs_ocornut_imgui")
		, true);

	ImageLODEnabled = bgfx::createUniform("u_imageLodEnabled", bgfx::UniformType::Vec4);
	ImageProgram = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, renderType, "vs_imgui_image")
		, bgfx::createEmbeddedShader(s_embeddedShaders, renderType, "fs_imgui_image")
		, true);

	Layout.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true);

	sTexture = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

	uint8_t* data;
	int32_t width;
	int32_t height;
	io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);

	mTexture = bgfx::createTexture2D(
		(uint16_t)width
		, (uint16_t)height
		, false
		, 1
		, bgfx::TextureFormat::BGRA8
		, 0
		, bgfx::copy(data, width * height * 4)
	);
}

void ImGuiRenderer::NewFrame(Vector2& mousePosition, uint8_t mouseButton,  int32_t scroll, Vector2 outputSize, int inputChar, bgfx::ViewId viewId)
{
	ViewId = viewId;

	ImGuiIO& io = ImGui::GetIO();
	if (inputChar >= 0)
	{
		io.AddInputCharacter(inputChar);
	}
	io.DisplaySize = ImVec2(outputSize.x, outputSize.y);

	io.MousePos = ImVec2(mousePosition.x, mousePosition.y);
	io.MouseDown[0] = (mouseButton & IMGUI_MBUT_LEFT) != 0;
	io.MouseDown[1] = (mouseButton & IMGUI_MBUT_RIGHT) != 0;
	io.MouseDown[2] = (mouseButton & IMGUI_MBUT_MIDDLE) != 0;
	io.MouseWheel = (float)(scroll - LastScroll);
	io.DeltaTime = 1.f / 60.f;
	LastScroll = scroll;

	bgfx::setViewClear(viewId
		, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
		, 0x303030ff
		, 1.0f
		, 0
	);

	ImGui::NewFrame();
}

void ImGuiRenderer::EndFrame()
{
	ImGui::Render();
	Render(ImGui::GetDrawData());

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void ImGuiRenderer::Render(ImDrawData* drawData)
{
	OPTICK_EVENT("ImGuiRenderer::Render", Optick::Category::Rendering);
	const ImGuiIO& io = ImGui::GetIO();
	const float width = io.DisplaySize.x;
	const float height = io.DisplaySize.y;

	bgfx::setViewName(ViewId, "ImGui");
	bgfx::setViewMode(ViewId, bgfx::ViewMode::Sequential);

	{
		const bgfx::Caps* caps = bgfx::getCaps();
		float ortho[16];
		bx::mtxOrtho(ortho, 0.f, width, height, 0.f, 0.f, 1000.f, 0.f, caps->homogeneousDepth);
		bgfx::setViewTransform(ViewId, nullptr, ortho);
		bgfx::setViewRect(ViewId, 0, 0, width, height);
	}

	for (int i = 0; i < drawData->CmdListsCount; ++i)
	{
		OPTICK_EVENT("ImGuiRenderer::CommandList", Optick::Category::Rendering);
		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;

		const ImDrawList* drawList = drawData->CmdLists[i];

		int numVertices = drawList->VtxBuffer.size();
		int numIndices = drawList->IdxBuffer.size();

		if (!checkAvailTransientBuffers(numVertices, Layout, numIndices))
		{
			break;
		}
		bgfx::allocTransientVertexBuffer(&tvb, numVertices, Layout);
		bgfx::allocTransientIndexBuffer(&tib, numIndices, sizeof(ImDrawIdx) == 4);

		ImDrawVert* verts = (ImDrawVert*)tvb.data;
		bx::memCopy(verts, drawList->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert));

		ImDrawVert* indices = (ImDrawVert*)tib.data;
		bx::memCopy(indices, drawList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));

		uint32_t offset = 0;
		for (const ImDrawCmd* cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd; ++cmd)
		{
			OPTICK_EVENT("ImGuiRenderer::Command", Optick::Category::Rendering);
			if (cmd->UserCallback)
			{
				cmd->UserCallback(drawList, cmd);
			}
			else if (cmd->ElemCount != 0)
			{
				uint64_t state = 0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

				bgfx::TextureHandle textureHandle = mTexture;
				bgfx::ProgramHandle programHandle = Program;

				if (cmd->TextureId)
				{
					union
					{
						ImTextureID ptr;
						struct  
						{
							bgfx::TextureHandle handle;
							uint8_t flags;
							uint8_t mip;
						} s;
					} texture = { cmd->TextureId };

					state |= 0 != (0x01 & texture.s.flags) ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA) : BGFX_STATE_NONE;
					textureHandle = texture.s.handle;
					if (texture.s.mip != 0)
					{
						const float lodEnabled[4] = { static_cast<float>(texture.s.mip), 1.f, 0.f, 0.f };
						bgfx::setUniform(ImageLODEnabled, lodEnabled);
						programHandle = ImageProgram;
					}
				}
				else
				{
					state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
				}

				const uint16_t xx = bx::max(cmd->ClipRect.x, 0.f);
				const uint16_t yy = bx::max(cmd->ClipRect.y, 0.f);
				bgfx::setScissor(xx, yy, bx::min(cmd->ClipRect.z, 65535.f) - xx, bx::min(cmd->ClipRect.w, 65535.f) - yy);
				bgfx::setState(state);
				bgfx::setTexture(0, sTexture, textureHandle);
				bgfx::setVertexBuffer(0, &tvb, 0, numVertices);
				bgfx::setIndexBuffer(&tib, offset, cmd->ElemCount);
				bgfx::submit(ViewId, programHandle);
			}
			offset += cmd->ElemCount;
		}
	}
}

