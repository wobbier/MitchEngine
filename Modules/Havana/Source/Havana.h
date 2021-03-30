#pragma once
#include <functional>
#include "ECS/Entity.h"
#include "ECS/EntityHandle.h"
#include "Math/Vector2.h"
#include "imgui.h"
#include "Pointers.h"
#include "Widgets/AssetBrowser.h"
#include "Events/EventReceiver.h"
#include "Engine/Input.h"
#include "Camera/CameraData.h"
#include <HavanaWidget.h>
#include <Utils/CommonUtils.h>

class LogWidget;
class ComponentInfo;
class ResourceMonitorWidget;
class MainMenuWidget;
class SceneViewWidget;
class SceneHierarchyWidget;
class PropertiesWidget;
class AssetPreviewWidget;

class Havana
	: public EventReceiver
{
public:
	Havana(class Engine* GameEngine, class EditorApp* app);

	void InitUI();
	void NewFrame();

	void UpdateWorld(class Transform* root, std::vector<Entity>& ents);
	void Render(Moonlight::CameraData& EditorCamera);

	void SetWindowTitle(const std::string& title);

	Input& GetInput();
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	void SetGameCallbacks(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	const Vector2& GetGameOutputSize() const;
	Vector2 GetWorldEditorRenderSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;

	ImVec2 DockPos;
	ImVec2 DockSize;
private:

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	class BGFXRenderer* Renderer = nullptr;

	AssetBrowser m_assetBrowser;
	Path EngineConfigFilePath;

	SharedPtr<LogWidget> LogPanel;
	SharedPtr<ResourceMonitorWidget> ResourceMonitor;
	SharedPtr<MainMenuWidget> MainMenu;
	SharedPtr<SceneViewWidget> MainSceneView;
	SharedPtr<SceneViewWidget> GameSceneView;
	SharedPtr<SceneHierarchyWidget> SceneHierarchy;
	SharedPtr<PropertiesWidget> PropertiesView;
	SharedPtr<AssetPreviewWidget> AssetPreview;

	std::vector<SharedPtr<HavanaWidget>> RegisteredWidgets;
};