#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/ShaderCommand.h"
#include "Device/IDevice.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"
#include "Ultralight/Renderer.h"
#include "Ultralight/RefPtr.h"
#include "UI2/OverlayManager.h"
#include "UI/UIWindow.h"
#include "UI2/FileSystemWin.h"
#include "UI2/FontLoaderWin.h"
#include "UI2/d3d11/GPUDriverD3D11.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "File.h"
#include "Path.h"

class UIView
	: public Component<UIView>
{
	friend class UICore;
public:
	UIView();
	virtual void Serialize(json& outJson) override;


	virtual void Deserialize(const json& inJson) override;


	virtual void OnEditorInspect() override;


	virtual void Init() override;
	size_t Index;
private:
	bool IsInitialized = false;
	File SourceFile;
	Path FilePath;
};
ME_REGISTER_COMPONENT(UIView)

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

	void InitUIView(UIView& view);

	virtual void OnEntityRemoved(Entity& InEntity) final;

	Cubemap* SkyboxMap = nullptr;
	Moonlight::ShaderCommand* SkyboxShader = nullptr;

	virtual void OnStop() override;
	void Render();

	OverlayManager* GetOverlayManager();
	void OnResize(const Vector2& NewSize);
	//private:

	bool IsInitialized = false;

	Moonlight::Renderer* m_renderer;
	ultralight::Ref<ultralight::Renderer> m_uiRenderer;
	std::unique_ptr<ultralight::FileSystemWin> m_fs;
	std::unique_ptr<ultralight::FontLoaderWin> m_fontLoader;
	std::unique_ptr<ultralight::GPUDriverD3D11> m_driver = nullptr;
	std::unique_ptr<ultralight::GPUContextD3D11> m_context = nullptr;
	ultralight::RefPtr<UIWindow> m_window = nullptr;
	std::vector<ultralight::RefPtr<ultralight::Overlay>> m_overlays;
};
