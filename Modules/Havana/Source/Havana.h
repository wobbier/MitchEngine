#pragma once
#include "Renderer.h"
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
#include <Keyboard.h>
#include "Engine/Input.h"

class Havana
	: public EventReceiver
{
public:
	struct ParentDescriptor
	{
		class Transform* Parent;
	};

	Havana(class Engine* GameEngine, class EditorApp* app, Moonlight::Renderer* renderer);

	void InitUI();

	void NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	void DrawOpenFilePopup();

	void DrawMainMenuBar(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);
	void DrawLog();

	void UpdateWorld(class World* world, class Transform* root, const std::vector<Entity>& ents);
	void UpdateWorldRecursive(class Transform* root);

	void DrawEntityRightClickMenu(class Transform* transform);

	void Render(Moonlight::CameraData& EditorCamera);

	void SetWindowTitle(const std::string& title)
	{
		WindowTitle = title;
	}

	Input& GetInput();
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	void EditTransform(const float* cameraView, float* cameraProjection, float* matrix);
	void AddComponentPopup();

	void DrawAddComponentList(const EntityHandle& entity);

	void DrawAddCoreList();

	void ClearSelection();

	void DrawCommandPanel();

	void DrawResourceMonitor();

	EntityHandle SelectedEntity;
	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Moonlight::Renderer* Renderer = nullptr;
	Vector2 RenderSize;
	Vector2 GameRenderSize;
	Vector2 WorldViewRenderSize;
	Vector2 WorldViewRenderLocation;
	Vector2 GameViewRenderLocation;

	void BrowseDirectory(const Path& path);

	const Vector2& GetGameOutputSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;
	SharedPtr<Moonlight::Texture> ViewTexture;
private:
	void HandleAssetDragAndDrop(Transform* root);

	class Engine* m_engine = nullptr;
	class EditorApp* m_app = nullptr;
	class Transform* m_rootTransform = nullptr;
	std::string WindowTitle;
	CommandManager EditorCommands;
	Input m_input;

	bool m_isGameFocused = false;
	bool m_isWorldViewFocused = false;
	bool OpenScene = false;
	bool AllowGameInput = false;
	ImVec2 MainMenuSize;
	Path CurrentDirectory;
	json AssetDirectory;
	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
	AssetBrowser m_assetBrowser;
	float prevMatrixTranslation[3];
	float prevMatrixRotation[3];
	float prevMatrixScale[3];
	ImVec2 previousMousePos;
	ParentDescriptor DragParentDescriptor;

	DirectX::Keyboard::KeyboardStateTracker tracker;
	DirectX::Mouse::ButtonStateTracker mouseTracker;
	//Path ConfigFilePath;
	Path EngineConfigFilePath;
};