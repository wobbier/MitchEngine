#pragma once
#include "Renderer.h"

class Havana
{
public:
	Havana(Moonlight::Renderer* renderer);

	void InitUI();

	void NewFrame();
	void UpdateWorld(class World* world, class Transform* root);
	void UpdateWorldRecursive(class Transform* root);

	void Render();
	class Transform* SelectedTransform = nullptr;
	Moonlight::Renderer* Renderer;
};