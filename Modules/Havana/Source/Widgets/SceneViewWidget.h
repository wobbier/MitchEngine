#pragma once
#include <HavanaWidget.h>
#include <Camera/CameraData.h>
#include "Events/EventReceiver.h"
#include <ImGuizmo.h>

class EditorApp;
class Transform;

class SceneViewWidget
	: public HavanaWidget
	, public EventReceiver
{
public:
	SceneViewWidget(const std::string& inTitle, bool inSceneToolsEnabled = false);

	void Init() override;
	void Destroy() override;
	void SetData(Moonlight::CameraData& data);

	bool OnEvent(const BaseEvent& evt) override;

	void Update() override;
	void Render() override;

	Moonlight::CameraData* MainCamera = nullptr;
	EditorApp* App = nullptr;
	ImGuiWindowFlags WindowFlags = 0;
	bool IsFocused = false;
	bool EnableSceneTools = false;
	bool MaximizeOnPlay = false;

	Vector2 SceneViewRenderSize;
	Vector2 SceneViewRenderLocation;

	Transform* SelectedTransform = nullptr;

private:
	void DrawGuizmo();

	ImGuizmo::OPERATION CurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE CurrentGizmoMode = ImGuizmo::LOCAL;
};