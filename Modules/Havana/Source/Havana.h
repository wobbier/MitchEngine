#pragma once
#include <functional>
#include "ECS/Entity.h"
#include "ECS/EntityHandle.h"
#include "Math/Vector2.h"
#include "imgui.h"
#include "Pointers.h"
#include "Widgets/AssetBrowser.h"
#include "Events/Event.h"
#include "Events/EventReceiver.h"
#include "RenderCommands.h"
#include "Commands/CommandManager.h"
#include "Engine/Input.h"
#include "Camera/CameraData.h"
#include "BGFXRenderer.h"
#include <HavanaWidget.h>

class LogWidget;
class ComponentInfo;
class ResourceMonitorWidget;
class MainMenuWidget;
class SceneViewWidget;

class FolderTest
{
public:
	std::map<std::string, FolderTest> Folders;
	std::map<std::string, ComponentInfo*> Reg;
};

class Havana
	: public EventReceiver
{
public:
	struct ParentDescriptor
	{
		class Transform* Parent;
	};

	Havana(class Engine* GameEngine, class EditorApp* app);

	void InitUI();

	void NewFrame();

	void UpdateWorld(class World* world, class Transform* root, const std::vector<Entity>& ents);
	void UpdateWorldRecursive(class Transform* root);

	void DrawEntityRightClickMenu(class Transform* transform);

	void Render(Moonlight::CameraData& EditorCamera);

	void RenderProfilerBar();

	void RenderMainView(Moonlight::CameraData& EditorCamera);

	void SetViewportMode(ViewportMode mode);

	void SetWindowTitle(const std::string& title);

	Input& GetInput();
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	void EditTransform(const float* cameraView, float* cameraProjection, float* matrix);
	void AddComponentPopup();

	void DrawAddComponentList(const EntityHandle& entity);

	void DrawAddCoreList();

	void ClearSelection();

	void DrawCommandPanel();

	void SetGameCallbacks(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	void DrawResourceMonitor();
	void DoComponentRecursive(const FolderTest& currentFolder, const EntityHandle& entity);
	EntityHandle SelectedEntity;
	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Vector2 RenderSize;
	Vector2 GameRenderSize;
	Vector2 WorldViewRenderSize;
	Vector2 WorldViewRenderLocation;
	Vector2 GameViewRenderLocation;

	void BrowseDirectory(const Path& path);

	const Vector2& GetGameOutputSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;
	SharedPtr<Moonlight::Texture> ViewTexture;

	ViewportMode GetViewportMode() const { return m_viewportMode; }
	ImVec2 DockPos;
	ImVec2 DockSize;
	bool MaximizeOnPlay = false;
	bool m_isGameFocused = false;
	bool m_isWorldViewFocused = false;

private:
	void HandleAssetDragAndDrop(Transform* root);

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	class Transform* m_rootTransform = nullptr;
	CommandManager EditorCommands;

	BGFXRenderer* Renderer = nullptr;

	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
	AssetBrowser m_assetBrowser;
	float prevMatrixTranslation[3];
	float prevMatrixRotation[3];
	float prevMatrixScale[3];
	ImVec2 previousMousePos;
	ParentDescriptor DragParentDescriptor;
	ViewportMode m_viewportMode;
	//Path ConfigFilePath;
	Path EngineConfigFilePath;

	SharedPtr<LogWidget> LogPanel;
	SharedPtr<ResourceMonitorWidget> ResourceMonitor;
	SharedPtr<MainMenuWidget> MainMenu;
	SharedPtr<SceneViewWidget> MainSceneView;

	std::vector<SharedPtr<HavanaWidget>> RegisteredWidgets;
};