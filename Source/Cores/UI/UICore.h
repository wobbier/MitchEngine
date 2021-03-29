#pragma once
#include <Components/UI/BasicUIView.h>

#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64

#include <UI/OverlayManager.h>
#include <bgfx/bgfx.h>

class BGFXRenderer;
class GPUContext;
class GPUDriverBGFX;

namespace ultralight
{ 
	class FileLogger;
	class FontLoader;
	class FileSystemBasic;
	class GPUContextD3D11;
	//class GPUDriverD3D11;
	class Renderer;
}

class IWindow;
class OverlayManager;
class UIWindow;

class UICore final
	: public Core<UICore>
	, public ultralight::OverlayManager
{
public:
	UICore(IWindow* window, BGFXRenderer* renderer);
	~UICore();

	virtual void Init() final;
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	virtual void OnEntityRemoved(Entity& InEntity) final;

	virtual void OnStop() override;

	void InitUIView(BasicUIView& view);

	void OnResize(const Vector2& NewSize);

	void Render();

	OverlayManager* GetOverlayManager();

	ultralight::RefPtr<ultralight::Renderer> m_uiRenderer;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
	bgfx::TextureHandle m_uiTexture;
	Vector2 UISize;
private:
	std::vector<ultralight::RefPtr<ultralight::Overlay>> m_overlays;

	BGFXRenderer* m_renderer;

	void CopyBitmapToTexture(ultralight::RefPtr<ultralight::Bitmap> bitmap);

	UniquePtr<ultralight::FileSystemBasic> m_fs;
	UniquePtr<GPUDriverBGFX> m_driver = nullptr;
	UniquePtr<GPUContext> m_context = nullptr;
	UniquePtr<ultralight::FontLoader> m_fontLoader = nullptr;
	UniquePtr<ultralight::FileLogger> m_logger = nullptr;
#if ME_PLATFORM_UWP || ME_PLATFORM_WIN64
	ultralight::RefPtr<UIWindow> m_window = nullptr;
#endif
};

#endif
