#pragma once
#include <HavanaWidget.h>
#include <Camera/CameraData.h>

class EditorApp;

class SceneViewWidget
	: public HavanaWidget
{
public:
	SceneViewWidget(const std::string& inTitle, bool inSceneToolsEnabled = false);

	void Init() override;
	void Destroy() override;
	void SetData(Moonlight::CameraData& data);

	void Update() override;
	void Render() override;

	void DrawGuizmo();

	Moonlight::CameraData* MainCamera = nullptr;
	EditorApp* App = nullptr;
	ImGuiWindowFlags WindowFlags = 0;
	bool IsFocused = false;
	bool EnableSceneTools = false;
	bool MaximizeOnPlay = false;

	Vector2 SceneViewRenderSize;
	Vector2 SceneViewRenderLocation;
};