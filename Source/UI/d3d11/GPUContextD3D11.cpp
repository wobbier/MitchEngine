#include "PCH.h"
#include "GPUContextD3D11.h"
#include <cassert>
#include "Engine/Engine.h"
#include "Renderer.h"

namespace ultralight {

GPUContextD3D11::GPUContextD3D11() {}

GPUContextD3D11::~GPUContextD3D11() {
  if (device_)
    immediate_context_->ClearState();

#if defined(_DEBUG)
  ID3D11Debug* debug;
  HRESULT result = device_.Get()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void **>(&debug));

  if (SUCCEEDED(result))
  {
    debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
    debug->Release();
  }
#endif
}

// Inherited from GPUContext
void GPUContextD3D11::BeginDrawing() {}
void GPUContextD3D11::EndDrawing() {}
void GPUContextD3D11::PresentFrame() {}

void GPUContextD3D11::Resize(int width, int height) {
	if (width == 0 || height == 0)
	{
		return;
	}
  set_screen_size(width, height);

  swap_chain_ = GetEngine().GetRenderer().GetDevice().GetSwapChain();
  immediate_context_ = GetEngine().GetRenderer().GetDevice().GetD3DDeviceContext();
  device_ = GetEngine().GetRenderer().GetDevice().GetD3DDevice();
  if (GetEngine().GetRenderer().GameViewRTT)
  {
	  back_buffer_view_ = GetEngine().GetRenderer().GameViewRTT->UIRenderTargetView;
  }

  //immediate_context_->OMSetRenderTargets(1, back_buffer_view_.GetAddressOf(), nullptr);
}

ID3D11Device* GPUContextD3D11::device() { assert(device_.Get()); return device_.Get(); }
ID3D11DeviceContext* GPUContextD3D11::immediate_context() { assert(immediate_context_.Get()); return immediate_context_.Get(); }
IDXGISwapChain* GPUContextD3D11::swap_chain() { assert(swap_chain_.Get()); return swap_chain_.Get(); }
ID3D11RenderTargetView* GPUContextD3D11::render_target_view() { return back_buffer_view_.Get(); }

void GPUContextD3D11::EnableBlend() {
  immediate_context_->OMSetBlendState(blend_state_.Get(), NULL, 0xffffffff);
}

void GPUContextD3D11::DisableBlend() {
  immediate_context_->OMSetBlendState(disabled_blend_state_.Get(), NULL, 0xffffffff);
}

void GPUContextD3D11::EnableScissor() {
  immediate_context_->RSSetState(scissored_rasterizer_state_.Get());
}

void GPUContextD3D11::DisableScissor() {
  immediate_context_->RSSetState(rasterizer_state_.Get());
}

// Scale is calculated from monitor DPI, see Application::SetScale
void GPUContextD3D11::set_scale(double scale) { scale_ = scale; }
double GPUContextD3D11::scale() const { return scale_; }

// This is in units, not actual pixels.
void GPUContextD3D11::set_screen_size(uint32_t width, uint32_t height) { width_ = width; height_ = height; }
void GPUContextD3D11::screen_size(uint32_t& width, uint32_t& height) { width = width_; height = height_; }

bool GPUContextD3D11::Initialize(int screen_width, int screen_height, double screen_scale, bool fullscreen, bool enable_vsync, bool sRGB, int samples) {
  samples_ = samples;
#if ENABLE_MSAA
  samples_ = 4;
#endif
  enable_vsync_ = enable_vsync;
  set_screen_size(screen_width, screen_height);
  set_scale(screen_scale);

  HRESULT hr = S_OK;

  swap_chain_ = GetEngine().GetRenderer().GetDevice().GetSwapChain();
  immediate_context_ = GetEngine().GetRenderer().GetDevice().GetD3DDeviceContext();
  device_ = GetEngine().GetRenderer().GetDevice().GetD3DDevice();
  if (back_buffer_view_)
  {
	back_buffer_view_ = GetEngine().GetRenderer().GameViewRTT->UIRenderTargetView;
	immediate_context_->OMSetRenderTargets(1, back_buffer_view_.GetAddressOf(), nullptr);
  }

  // Create Enabled Blend State

  D3D11_RENDER_TARGET_BLEND_DESC rt_blend_desc;
  ZeroMemory(&rt_blend_desc, sizeof(rt_blend_desc));
  rt_blend_desc.BlendEnable = true;
  rt_blend_desc.SrcBlend = D3D11_BLEND_ONE;
  rt_blend_desc.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  rt_blend_desc.BlendOp = D3D11_BLEND_OP_ADD;
  rt_blend_desc.SrcBlendAlpha = D3D11_BLEND_INV_DEST_ALPHA;
  rt_blend_desc.DestBlendAlpha = D3D11_BLEND_ONE;
  rt_blend_desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt_blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  D3D11_BLEND_DESC blend_desc;
  ZeroMemory(&blend_desc, sizeof(blend_desc));
  blend_desc.AlphaToCoverageEnable = false;
  blend_desc.IndependentBlendEnable = false;
  blend_desc.RenderTarget[0] = rt_blend_desc;

  device()->CreateBlendState(&blend_desc, blend_state_.GetAddressOf());

  // Create Disabled Blend State

  ZeroMemory(&rt_blend_desc, sizeof(rt_blend_desc));
  rt_blend_desc.BlendEnable = false;
  rt_blend_desc.SrcBlend = D3D11_BLEND_ONE;
  rt_blend_desc.DestBlend = D3D11_BLEND_ZERO;
  rt_blend_desc.BlendOp = D3D11_BLEND_OP_ADD;
  rt_blend_desc.SrcBlendAlpha = D3D11_BLEND_ONE;
  rt_blend_desc.DestBlendAlpha = D3D11_BLEND_ZERO;
  rt_blend_desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt_blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  ZeroMemory(&blend_desc, sizeof(blend_desc));
  blend_desc.AlphaToCoverageEnable = false;
  blend_desc.IndependentBlendEnable = false;
  blend_desc.RenderTarget[0] = rt_blend_desc;

  device()->CreateBlendState(&blend_desc, disabled_blend_state_.GetAddressOf());

  EnableBlend();

  D3D11_RASTERIZER_DESC rasterizer_desc;
  ZeroMemory(&rasterizer_desc, sizeof(rasterizer_desc));
  rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  rasterizer_desc.CullMode = D3D11_CULL_NONE;
  rasterizer_desc.FrontCounterClockwise = false;
  rasterizer_desc.DepthBias = 0;
  rasterizer_desc.SlopeScaledDepthBias = 0.0f;
  rasterizer_desc.DepthBiasClamp = 0.0f;
  rasterizer_desc.DepthClipEnable = false;
  rasterizer_desc.ScissorEnable = false;
#if ENABLE_MSAA
  rasterizer_desc.MultisampleEnable = true;
  rasterizer_desc.AntialiasedLineEnable = true;
#else
  rasterizer_desc.MultisampleEnable = false;
  rasterizer_desc.AntialiasedLineEnable = false;
#endif

  device()->CreateRasterizerState(&rasterizer_desc, rasterizer_state_.GetAddressOf());

  D3D11_RASTERIZER_DESC scissor_rasterizer_desc;
  ZeroMemory(&scissor_rasterizer_desc, sizeof(scissor_rasterizer_desc));
  scissor_rasterizer_desc.FillMode = D3D11_FILL_SOLID;
  scissor_rasterizer_desc.CullMode = D3D11_CULL_NONE;
  scissor_rasterizer_desc.FrontCounterClockwise = false;
  scissor_rasterizer_desc.DepthBias = 0;
  scissor_rasterizer_desc.SlopeScaledDepthBias = 0.0f;
  scissor_rasterizer_desc.DepthBiasClamp = 0.0f;
  scissor_rasterizer_desc.DepthClipEnable = false;
  scissor_rasterizer_desc.ScissorEnable = true;
#if ENABLE_MSAA
  scissor_rasterizer_desc.MultisampleEnable = true;
  scissor_rasterizer_desc.AntialiasedLineEnable = true;
#else
  scissor_rasterizer_desc.MultisampleEnable = false;
  scissor_rasterizer_desc.AntialiasedLineEnable = false;
#endif

  device()->CreateRasterizerState(&scissor_rasterizer_desc, scissored_rasterizer_state_.GetAddressOf());

  DisableScissor();

  //// Setup the viewport
  //D3D11_VIEWPORT vp;
  //ZeroMemory(&vp, sizeof(vp));
  //vp.Width = (float)width * (float)scale();
  //vp.Height = (float)height * (float)scale();
  //vp.MinDepth = 0.0f;
  //vp.MaxDepth = 1.0f;
  //vp.TopLeftX = 0;
  //vp.TopLeftY = 0;
  //immediate_context_->RSSetViewports(1, &vp);

  //back_buffer_width_ = width;
  //back_buffer_height_ = height;

  // Initialize backbuffer with white so we don't get flash of black while loading views.
  float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  if (immediate_context_ && render_target_view())
  {
	immediate_context_->ClearRenderTargetView(render_target_view(), color);
  }


  return true;
}

UINT GPUContextD3D11::back_buffer_width() { return back_buffer_width_; }
UINT GPUContextD3D11::back_buffer_height() { return back_buffer_height_; }

}  // namespace ultralight
