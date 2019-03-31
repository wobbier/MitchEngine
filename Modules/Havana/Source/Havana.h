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

	static void Text(const std::string& Name, const Vector3& Vector);

	class Transform* SelectedTransform = nullptr;
	Moonlight::Renderer* Renderer;
};