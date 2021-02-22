#pragma once
#include <Components/UI/BasicUIView.h>

#include <UI/OverlayManager.h>

namespace Moonlight { class Renderer; }

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
	UICore(IWindow* window, Moonlight::Renderer* renderer);
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

	//ultralight::RefPtr<ultralight::Renderer> m_uiRenderer;
private:
	std::vector<ultralight::RefPtr<ultralight::Overlay>> m_overlays;

	Moonlight::Renderer* m_renderer;

	UniquePtr<ultralight::FileSystemBasic> m_fs;
	//UniquePtr<ultralight::GPUDriverD3D11> m_driver = nullptr;
	UniquePtr<ultralight::GPUContextD3D11> m_context = nullptr;
	UniquePtr<ultralight::FontLoader> m_fontLoader = nullptr;
	UniquePtr<ultralight::FileLogger> m_logger = nullptr;
	ultralight::RefPtr<UIWindow> m_window = nullptr;
};
