#pragma once
#include "ECS/Core.h"

#include "Ultralight/Renderer.h"
#include "Ultralight/RefPtr.h"
#include "UI/OverlayManager.h"
#include "UI/UIWindow.h"
#include "UI/d3d11/GPUDriverD3D11.h"

#include "Components/UI/BasicUIView.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Path.h"
#include "UI/FileSystemBasic.h"

namespace Moonlight { class Renderer; }

class UICore final
	: public Core<UICore>
	, public ultralight::OverlayManager
{
public:
	UICore(IWindow* window);
	~UICore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;

	void InitUIView(BasicUIView& view);

	virtual void OnEntityRemoved(Entity& InEntity) final;

	virtual void OnStop() override;
	void Render();

	OverlayManager* GetOverlayManager();
	void OnResize(const Vector2& NewSize);
	//private:

	bool IsInitialized = false;

	Moonlight::Renderer* m_renderer;
	ultralight::Ref<ultralight::Renderer> m_uiRenderer;
	std::unique_ptr<ultralight::FileSystemBasic> m_fs;
	std::unique_ptr<ultralight::GPUDriverD3D11> m_driver = nullptr;
	std::unique_ptr<ultralight::GPUContextD3D11> m_context = nullptr;
	ultralight::RefPtr<UIWindow> m_window = nullptr;
	std::vector<ultralight::RefPtr<ultralight::Overlay>> m_overlays;
};
