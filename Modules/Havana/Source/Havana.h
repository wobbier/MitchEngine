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
class AssetBrowserWidget;
class HistoryWidget;
class ProfilerWidget;
class PreferencesWidget;
class ProjectSettingsWidget;

#if USING( ME_EDITOR )

class Havana
	: public EventReceiver
{
public:
	Havana(class Engine* GameEngine, class EditorApp* app);
	~Havana();

	void InitUI();
	void NewFrame();

	void UpdateWorld(class Transform* root, std::vector<Entity>& ents);
	void Render(Moonlight::CameraData& EditorCamera);

	void SetWindowTitle(const std::string& title);

	Input& GetInput();
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	SceneHierarchyWidget* GetHierarchy() const { return SceneHierarchy.get(); }
	SceneViewWidget* GetSceneView() const { return MainSceneView.get(); }
	AssetBrowserWidget* GetAssetBrowser() const { return AssetBrowser.get(); }
	// Called by EditorApp when play mode starts/stops (game view focus, maximize on play).
	void SetPlayMode(bool playing);
	// Opens (and focuses) a registered widget by name.
	void ShowWidget(const std::string& name);
	// True while the hierarchy or the world view has keyboard focus (scene-editing shortcuts).
	bool IsSceneContextFocused() const;

	const Vector2& GetGameOutputSize() const;
	Vector2 GetWorldEditorRenderSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;

	void Save();

	ImVec2 DockPos;
	ImVec2 DockSize;
private:

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	class BGFXRenderer* Renderer = nullptr;

	Path EngineConfigFilePath;

	SharedPtr<LogWidget> LogPanel;
	SharedPtr<ResourceMonitorWidget> ResourceMonitor;
	SharedPtr<MainMenuWidget> MainMenu;
	SharedPtr<SceneViewWidget> MainSceneView;
	SharedPtr<SceneViewWidget> GameSceneView;
	SharedPtr<SceneHierarchyWidget> SceneHierarchy;
	SharedPtr<PropertiesWidget> PropertiesView;
	SharedPtr<AssetPreviewWidget> AssetPreview;
	SharedPtr<AssetBrowserWidget> AssetBrowser;
	SharedPtr<HistoryWidget> History;
	SharedPtr<ProfilerWidget> Profiler;
	SharedPtr<PreferencesWidget> Preferences;
	SharedPtr<ProjectSettingsWidget> ProjectSettingsView;

    std::vector<SharedPtr<HavanaWidget>> RegisteredWidgets;
    std::vector<SharedPtr<HavanaWidget>> CustomRegisteredWidgets;
};

#endif