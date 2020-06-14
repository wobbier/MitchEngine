#include "PCH.h"
//
//#include "OverlayImpl.h"
//#include "UIWindow.h"
//#include "AppCore/Overlay.h"
//
//namespace ultralight
//{
//	static ultralight::IndexType patternCW[] = { 0, 1, 3, 1, 2, 3 };
//	static ultralight::IndexType patternCCW[] = { 0, 3, 1, 1, 3, 2 };
//
//	void OverlayImpl::Draw()
//	{
//		if (is_hidden_)
//			return;
//
//		UpdateGeometry();
//
//		driver_->DrawGeometry(geometry_id_, 6, 0, gpu_state_);
//	}
//
//	void OverlayImpl::Resize(uint32_t width, uint32_t height)
//	{
//		width = width <= 2 ? 2 : width;
//		height = height <= 2 ? 2 : height;
//
//		if (width == width_ && height == height_)
//			return;
//
//		view_->Resize(width, height);
//
//		width_ = width;
//		height_ = height;
//		needs_update_ = true;
//		UpdateGeometry();
//
//		// Update these now since they were invalidated
//		ultralight::RenderTarget target = view_->render_target();
//		gpu_state_.texture_1_id = target.texture_id;
//		gpu_state_.viewport_width = window_->width();
//		gpu_state_.viewport_height = window_->height();
//	}
//
//	void OverlayImpl::UpdateGeometry()
//	{
//		bool initial_creation = false;
//		ultralight::RenderTarget target = view_->render_target();
//
//		if (vertices_.empty()) {
//			vertices_.resize(4);
//			indices_.resize(6);
//
//			auto& config = ultralight::Platform::instance().config();
//			if (config.face_winding == ultralight::kFaceWinding_Clockwise)
//				memcpy(indices_.data(), patternCW, sizeof(ultralight::IndexType) * indices_.size());
//			else
//				memcpy(indices_.data(), patternCCW, sizeof(ultralight::IndexType) * indices_.size());
//
//			memset(&gpu_state_, 0, sizeof(gpu_state_));
//			ultralight::Matrix identity;
//			identity.SetIdentity();
//
//			gpu_state_.viewport_width = window_->width();
//			gpu_state_.viewport_height = window_->height();
//			gpu_state_.transform = identity.GetMatrix4x4();
//			gpu_state_.enable_blend = true;
//			gpu_state_.enable_texturing = true;
//			gpu_state_.shader_type = ultralight::kShaderType_Fill;
//			gpu_state_.render_buffer_id = 0; // default render target view (screen)
//			gpu_state_.texture_1_id = target.texture_id;
//
//			initial_creation = true;
//		}
//
//		if (!needs_update_)
//			return;
//
//		ultralight::Vertex_2f_4ub_2f_2f_28f v;
//		memset(&v, 0, sizeof(v));
//
//		v.data0[0] = 1; // Fill Type: Image
//
//		v.color[0] = 255;
//		v.color[1] = 255;
//		v.color[2] = 255;
//		v.color[3] = 255;
//
//		float left = static_cast<float>(x_);
//		float top = static_cast<float>(y_);
//		float right = static_cast<float>(x_ + width());
//		float bottom = static_cast<float>(y_ + height());
//
//		// TOP LEFT
//		v.pos[0] = v.obj[0] = left;
//		v.pos[1] = v.obj[1] = top;
//		v.tex[0] = target.uv_coords.left;
//		v.tex[1] = target.uv_coords.top;
//
//		vertices_[0] = v;
//
//		// TOP RIGHT
//		v.pos[0] = v.obj[0] = right;
//		v.pos[1] = v.obj[1] = top;
//		v.tex[0] = target.uv_coords.right;
//		v.tex[1] = target.uv_coords.top;
//
//		vertices_[1] = v;
//
//		// BOTTOM RIGHT
//		v.pos[0] = v.obj[0] = right;
//		v.pos[1] = v.obj[1] = bottom;
//		v.tex[0] = target.uv_coords.right;
//		v.tex[1] = target.uv_coords.bottom;
//
//		vertices_[2] = v;
//
//		// BOTTOM LEFT
//		v.pos[0] = v.obj[0] = left;
//		v.pos[1] = v.obj[1] = bottom;
//		v.tex[0] = target.uv_coords.left;
//		v.tex[1] = target.uv_coords.bottom;
//
//		vertices_[3] = v;
//
//		ultralight::VertexBuffer vbuffer;
//		vbuffer.format = ultralight::kVertexBufferFormat_2f_4ub_2f_2f_28f;
//		vbuffer.size = static_cast<uint32_t>(sizeof(ultralight::Vertex_2f_4ub_2f_2f_28f) * vertices_.size());
//		vbuffer.data = (uint8_t*)vertices_.data();
//
//		ultralight::IndexBuffer ibuffer;
//		ibuffer.size = static_cast<uint32_t>(sizeof(ultralight::IndexType) * indices_.size());
//		ibuffer.data = (uint8_t*)indices_.data();
//
//		if (initial_creation) {
//			geometry_id_ = driver_->NextGeometryId();
//			driver_->CreateGeometry(geometry_id_, vbuffer, ibuffer);
//		}
//		else {
//			driver_->UpdateGeometry(geometry_id_, vbuffer, ibuffer);
//		}
//
//		needs_update_ = false;
//	}
//
//	bool OverlayImpl::has_focus() const
//	{
//		UIWindow* win = static_cast<UIWindow*>(&const_cast<ultralight::Window&>(window_.get()));
//		return win->overlay_manager()->IsOverlayFocused((ultralight::Overlay*)this);
//	}
//
//	void OverlayImpl::Focus()
//	{
//		//if (has_focus())
//		{
//			static_cast<UIWindow*>(&window_.get())->overlay_manager()->FocusOverlay((ultralight::Overlay*)this);
//		}
//	}
//
//	void OverlayImpl::Unfocus()
//	{
//		static_cast<UIWindow*>(&window_.get())->overlay_manager()->UnfocusOverlay((ultralight::Overlay*)this);
//	}
//	ultralight::Ref<ultralight::View> DoDebugThing(int width, int height)
//	{
//		Engine& engine = GetEngine();
//		return engine.UI->m_uiRenderer->CreateView(width, height, false, nullptr);
//	}
//
//	OverlayImpl::OverlayImpl(ultralight::Ref<ultralight::Window> window, uint32_t width, uint32_t height, int x, int y)
//		: window_(window)
//		, view_(DoDebugThing(width, height))
//		, width_(width)
//		, height_(height)
//		, x_(x)
//		, y_(y)
//		, needs_update_(true)
//		, driver_((GPUDriverD3D11*)ultralight::Platform::instance().gpu_driver())
//	{
//		static_cast<UIWindow*>(&window_.get())->overlay_manager()->Add(this);
//	}
//
//	OverlayImpl::OverlayImpl(ultralight::Ref<ultralight::Window> window, ultralight::Ref<ultralight::View> view, int x, int y) : window_(window)
//		, view_(view)
//		, width_(view->width())
//		, height_(view->height())
//		, x_(x)
//		, y_(y)
//		, needs_update_(true)
//		, driver_((GPUDriverD3D11*)ultralight::Platform::instance().gpu_driver())
//	{
//		static_cast<UIWindow*>(&window_.get())->overlay_manager()->Add(this);
//	}
//
//
//	bool OverlayImpl::NeedsRepaint()
//	{
//		return needs_update_ || view_->needs_paint();
//	}
//
//	OverlayImpl::~OverlayImpl()
//	{
//		if (GetEngine().UI) {
//			static_cast<UIWindow*>(&window_.get())->overlay_manager()->Remove(this);
//
//			if (vertices_.size())
//				driver_->DestroyGeometry(geometry_id_);
//		}
//	}
//	Ref<Overlay> Overlay::Create(Ref<Window> window, uint32_t width, uint32_t height, int x, int y) {
//		// Clamp each dimension to 2
//		width = width <= 2 ? 2 : width;
//		height = height <= 2 ? 2 : height;
//		std::cout << "CREATE";
//		return AdoptRef(*new OverlayImpl(window, width, height, x, y));
//	}
//
//	Ref<Overlay> Overlay::Create(Ref<Window> window, Ref<View> view, int x, int y) {
//		return AdoptRef(*new OverlayImpl(window, view, x, y));
//	}
//}