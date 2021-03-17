#pragma once
#include <HavanaWidget.h>
#include <Camera/CameraData.h>

class EditorApp;

class SceneViewWidget
	: public HavanaWidget
{
public:
	SceneViewWidget(const std::string& inTitle, EditorApp* inApp);

	void Init() override;
	void Destroy() override;
	void SetData(Moonlight::CameraData& data);

	void Update() override;
	void Render() override;

	Moonlight::CameraData* MainCamera = nullptr;
	EditorApp* App = nullptr;
	ImGuiWindowFlags window_flags = 0;
};