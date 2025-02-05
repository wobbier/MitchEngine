#pragma once
#include <HavanaWidget.h>
#include <Camera/CameraData.h>
#include "Events/EventReceiver.h"
#include <ImGuizmo.h>
#include <Pointers.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

class EditorApp;
class Transform;
class SceneViewWidget
	: public HavanaWidget
	, public EventReceiver
{
	enum class DisplayType : uint8_t
	{
		FreeForm = 0,
		Ratio,
		Fixed
	};

	struct DisplayParams
	{
		std::string Name;
		Vector2 Extents;
		DisplayType Type = DisplayType::FreeForm;
	};

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
	bool IsUsingGuizmo = false;
	bool IsPlatformWindow = false;

	Vector2 SceneViewRenderSize;
	Vector2 SceneViewRenderLocation;

	WeakPtr<Transform> SelectedTransform;

	DisplayParams CurrentDisplayParams;
private:
	void DrawGuizmo();

	ImGuizmo::OPERATION CurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE CurrentGizmoMode = ImGuizmo::LOCAL;

	Vector2 GizmoRenderLocation;

	std::vector<DisplayParams> DisplayOptions;
};

#endif