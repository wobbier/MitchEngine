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
class LocalizationWidget;
class ProfilerWidget;
class PreferencesWidget;
class ProjectSettingsWidget;
class BuildGameWidget;
class OpenProjectWidget;
class ShaderGraphWidget;
class TreeWidget;
class TerrainWidget;
class SequenceWidget;

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
	// Personal editor layouts. Ctrl+1 through Ctrl+9 select a workspace; the outgoing
	// workspace is saved automatically so its dock tree and open panels are restored.
	void SwitchWorkspace( int InWorkspace );
	int GetActiveWorkspace() const { return ActiveWorkspace; }
	// True while the hierarchy or the world view has keyboard focus (scene-editing shortcuts).
	bool IsSceneContextFocused() const;

	const Vector2& GetGameOutputSize() const;
	Vector2 GetWorldEditorRenderSize() const;

	void Save();
	// Release editor-owned previews while the renderer is still alive.
	void Destroy();

	ImVec2 DockPos;
	ImVec2 DockSize;
private:

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	class BGFXRenderer* Renderer = nullptr;

	Path EngineConfigFilePath;
	Path DefaultLayoutFilePath;
	int ActiveWorkspace = 1;

	Path GetWorkspaceLayoutPath( int InWorkspace ) const;
	void SaveWorkspaceLayout( int InWorkspace );
	void RestoreWorkspacePanelVisibility( int InWorkspace );
	void StoreWorkspacePanelVisibility( int InWorkspace );

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
	SharedPtr<LocalizationWidget> LocalizationTables;
	SharedPtr<ProfilerWidget> Profiler;
	SharedPtr<PreferencesWidget> Preferences;
	SharedPtr<ProjectSettingsWidget> ProjectSettingsView;
	SharedPtr<BuildGameWidget> BuildGameView;
	SharedPtr<OpenProjectWidget> OpenProjectView;
	// Open shader graph documents (not a single window, so not in RegisteredWidgets).
	SharedPtr<ShaderGraphWidget> ShaderGraphs;
	// Open MitchTree documents (likewise).
	SharedPtr<TreeWidget> Trees;
	SharedPtr<TerrainWidget> TerrainPanel;
    SharedPtr<SequenceWidget> Sequencer;

    std::vector<SharedPtr<HavanaWidget>> RegisteredWidgets;
    std::vector<SharedPtr<HavanaWidget>> CustomRegisteredWidgets;

    // Holds the cursor in the Game View while the game owns input (UpdateGameCursor).
    void UpdateGameCursor();
    bool m_imguiMouseBlocked = false;
};

#endif
