#pragma once
#include "Renderer.h"
#include <functional>
#include "ECS/Entity.h"
#include "Math/Vector2.h"
#include "imgui.h"
#include "Graphics/Texture.h"
#include "Pointers.h"
#include "Widgets/AssetBrowser.h"
#include "Events/Event.h"
#include "Events/EventReceiver.h"
#include "RenderCommands.h"

class Havana
	: public EventReceiver
{
public:
	Havana(class Engine* GameEngine, Moonlight::Renderer* renderer);

	void InitUI();

	void NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	void DrawOpenFilePopup();

	void DrawMainMenuBar(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);
	void DrawLog();

	void UpdateWorld(class World* world, class Transform* root, const std::vector<Entity> & ents);
	void UpdateWorldRecursive(class Transform* root);

	void Render(const Moonlight::CameraData& EditorCamera);

	void SetWindowTitle(const std::string& title)
	{
		WindowTitle = title;
	}
	std::string WindowTitle;
	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	static void Text(const std::string& Name, const Vector3& Vector);
	static void Text(const std::string& Name, const Vector2& Vector);
	static void EditableVector3(const std::string& Name, Vector3& Vector);

	void EditTransform(const float* cameraView, float* cameraProjection, float* matrix);
	void AddComponentPopup();

	class Entity* SelectedEntity = nullptr;
	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Moonlight::Renderer* Renderer;
	Vector2 RenderSize;
	Vector2 GameRenderSize;
	Vector2 WorldViewRenderSize;
	Vector2 WorldViewRenderLocation;

	void BrowseDirectory(const FilePath& path);

	const Vector2& GetGameOutputSize() const;

	virtual bool OnEvent(const BaseEvent& evt) override;
private:
	class Engine* m_engine;
	bool m_isGameFocused = false;
	bool m_isWorldViewFocused = false;
	bool OpenScene = false;
	ImVec2 MainMenuSize;
	FilePath CurrentDirectory;
	json AssetDirectory;
	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
	AssetBrowser m_assetBrowser;
};