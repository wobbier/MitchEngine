#pragma once
#include <Pointers.h>
#include <string>
#include <vector>
#include <Path.h>
#include "ProjectCache.h"
#include <Math/Vector2.h>

class Input;
class SDLWindow;
class ImGuiRenderer;
namespace Moonlight { class Texture; }

class MitchHub
{
public:
	MitchHub(Input* input, SDLWindow* window, ImGuiRenderer* renderer);

	void Draw();
	Path ShowOpenFilePrompt();

	SharedPtr<Moonlight::Texture>& GetActiveBackgroundTexture();
	SharedPtr<Moonlight::Texture>& GetActiveTitleTexture();

	ImGuiRenderer* GetRenderer() const;

private:
	Input* m_input = nullptr;
	SDLWindow* m_window = nullptr;
	ImGuiRenderer* m_renderer = nullptr;
	SharedPtr<Moonlight::Texture> logo;
	SharedPtr<Moonlight::Texture> closeIcon;
	SharedPtr<Moonlight::Texture> minimizeIcon;
	SharedPtr<Moonlight::Texture> vsIcon;
	
	std::size_t SelectedProjectIndex = 0;
	ProjectCache Cache;
	Vector2 TitleBarDragPosition = { 0.f, 0.f };
	Vector2 TitleBarDragSize = { 0.f, 0.f };
	const float SystemButtonSize = 30.f;
};