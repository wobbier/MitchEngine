#pragma once
#include "Renderer.h"
#include <functional>

#if ME_EDITOR

class Havana
{
public:
	Havana(class Engine* GameEngine, Moonlight::Renderer* renderer);

	void InitUI();

	static void ShowExampleMenuFile();

	void NewFrame(std::function<void()> StartGameFunc, std::function<void()> PauseGameFunc, std::function<void()> StopGameFunc);
	void UpdateWorld(class World* world, class Transform* root);
	void UpdateWorldRecursive(class Transform* root);

	void Render();

	const bool IsGameFocused() const;

	static void Text(const std::string& Name, const Vector3& Vector);
	static void EditableVector3(const std::string& Name, Vector3& Vector);

	class Transform* SelectedTransform = nullptr;
	class BaseCore* SelectedCore = nullptr;
	Moonlight::Renderer* Renderer;
	Vector2 RenderSize;

private:
	class Engine* m_engine;
	bool m_isGameFocused = false;

};

#endif