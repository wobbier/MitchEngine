#pragma once
#include "Renderer.h"
#include <functional>
#include "ECS/Entity.h"
#include "Math/Vector2.h"

#if ME_EDITOR

class Havana
{
public:
	Havana(class Engine* GameEngine, Moonlight::Renderer* renderer);

	void InitUI();

	void NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);

	void DrawOpenFilePopup();

	void DrawMainMenuBar();

	void UpdateWorld(class World* world, class Transform* root, const std::vector<Entity> & ents);
	void UpdateWorldRecursive(class Transform* root);

	void Render();

	const bool IsGameFocused() const;
	const bool IsWorldViewFocused() const;

	static void Text(const std::string& Name, const Vector3& Vector);
	static void Text(const std::string& Name, const Vector2& Vector);
	static void EditableVector3(const std::string& Name, Vector3& Vector);

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
private:
	class Engine* m_engine;
	bool m_isGameFocused = false;
	bool m_isWorldViewFocused = false;
	bool OpenScene = false;

	FilePath CurrentDirectory;
	json AssetDirectory;
};

#endif