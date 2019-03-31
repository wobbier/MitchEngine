#pragma once
#include "Renderer.h"

class Havana
{
public:
	Havana(Moonlight::Renderer* renderer);

	void InitUI();

	void NewFrame();

	void Render();

	Moonlight::Renderer* Renderer;
};