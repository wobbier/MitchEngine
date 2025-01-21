#pragma once
//
//#include "Engine/Engine.h"
//#include <AppCore/Overlay.h>
//#include <AppCore/Window.h>
//#include <Ultralight/Renderer.h>
//#include <Ultralight/Buffer.h>
//#include <Ultralight/platform/Platform.h>
//#include <Ultralight/platform/Config.h>
//#include <Ultralight/platform/GPUDriver.h>
//#include "UI/RefCountedImpl.h"
//#include "UI/OverlayManager.h"
//#include <vector>
//#include "Cores/UI/UICore.h"
//#include "Ultralight/RefPtr.h"
//#include <stdint.h>
//
//namespace ultralight
//{
//
//	class OverlayImpl
//		: public ultralight::Overlay
//		, public ultralight::RefCountedImpl<OverlayImpl> {
//	public:
//		virtual void Draw() override;
//
//		virtual void Resize(uint32_t width, uint32_t height) override;
//
//		void UpdateGeometry();
//
//		virtual ultralight::Ref<ultralight::View> view() override { return view_; }
//
//		virtual uint32_t width() const override { return width_; }
//
//		virtual uint32_t height() const override { return height_; }
//
//		virtual int x() const override { return x_; }
//
//		virtual int y() const override { return y_; }
//
//		virtual bool is_hidden() const override { return is_hidden_; }
//
//		virtual void Hide() override {
//			is_hidden_ = true;
//		}
//
//		virtual void Show() override {
//			is_hidden_ = false;
//		}
//
//		virtual bool has_focus() const override;
//
//		virtual void Focus() override;
//
//		virtual void Unfocus() override;
//
//		virtual void MoveTo(int x, int y) override {
//			x_ = x;
//			y_ = y;
//			needs_update_ = true;
//		}
//
//		REF_COUNTED_IMPL(OverlayImpl);
//		OverlayImpl(ultralight::Ref<ultralight::Window> window, uint32_t width, uint32_t height, int x, int y);
//
//		OverlayImpl(ultralight::Ref<ultralight::Window> window, ultralight::Ref<ultralight::View> view, int x, int y);
//
//		bool NeedsRepaint() override;
//
//	protected:
//
//		~OverlayImpl();
//
//		ultralight::Ref<ultralight::Window> window_;
//		uint32_t width_;
//		uint32_t height_;
//		int x_;
//		int y_;
//		bool is_hidden_ = false;
//		ultralight::Ref<ultralight::View> view_;
//		GPUDriverD3D11* driver_;
//		std::vector<ultralight::Vertex_2f_4ub_2f_2f_28f> vertices_;
//		std::vector<ultralight::IndexType> indices_;
//		bool needs_update_;
//		uint32_t geometry_id_;
//		ultralight::GPUState gpu_state_;
//
//		friend class Overlay;
//
//		DISALLOW_COPY_AND_ASSIGN(OverlayImpl);
//	};
//}