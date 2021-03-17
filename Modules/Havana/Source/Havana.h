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
#include <HavanaWidget.h>
#include <Utils/CommonUtils.h>
#include <BGFXRenderer.h>

class LogWidget;
class ComponentInfo;
class ResourceMonitorWidget;
class MainMenuWidget;
class SceneViewWidget;
class SceneHierarchyWidget;

class Havana
	: public EventReceiver
{
public:
	Havana(class Engine* GameEngine, class EditorApp* app);

	void InitUI();

	void NewFrame();

	void UpdateWorld(class Transform* root, std::vector<Entity>& ents);
	void UpdateWorldRecursive(class Transform* root);

	void DrawEntityRightClickMenu(class Transform* transform);

	void Render(Moonlight::CameraData& EditorCamera);

	void SetViewportMode(ViewportMode mode);

	void SetWindowTitle(const std::string& title);

	Input& GetInput();
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	void AddComponentPopup(EntityHandle inSelectedEntity);

	void ClearSelection();

	void DrawCommandPanel();

	void SetGameCallbacks(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	EntityHandle SelectedEntity;
	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Vector2 RenderSize;
	Vector2 GameRenderSize;
	Vector2 WorldViewRenderSize;
	Vector2 WorldViewRenderLocation;
	Vector2 GameViewRenderLocation;

	const Vector2& GetGameOutputSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;
	SharedPtr<Moonlight::Texture> ViewTexture;

	ViewportMode GetViewportMode() const { return m_viewportMode; }
	ImVec2 DockPos;
	ImVec2 DockSize;
	bool MaximizeOnPlay = false;
	bool m_isGameFocused = false;
	bool m_isWorldViewFocused = false;

	void HandleAssetDragAndDrop(Transform* root);
	ParentDescriptor DragParentDescriptor;
private:

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	CommandManager EditorCommands;

	class BGFXRenderer* Renderer = nullptr;

	AssetBrowser m_assetBrowser;
	ViewportMode m_viewportMode;
	//Path ConfigFilePath;
	Path EngineConfigFilePath;

	SharedPtr<LogWidget> LogPanel;
	SharedPtr<ResourceMonitorWidget> ResourceMonitor;
	SharedPtr<MainMenuWidget> MainMenu;
	SharedPtr<SceneViewWidget> MainSceneView;
	SharedPtr<SceneHierarchyWidget> SceneHierarchy;

	std::vector<SharedPtr<HavanaWidget>> RegisteredWidgets;
};