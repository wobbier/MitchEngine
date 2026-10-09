#pragma once
#include <HavanaWidget.h>
#include <Camera/CameraData.h>
#include "Events/EventReceiver.h"
#include <ImGuizmo.h>
#include <Pointers.h>
#include "Dementia.h"
#include "JSON.h"
#include "ECS/EntityHandle.h"
#include "Math/Matrix4.h"
#include <string>
#include <vector>

#if USING( ME_EDITOR )

class EditorApp;
#include "Components/Transform.h"

// A camera viewport inside the editor. The "World View" instance (scene tools enabled) adds the
// transform gizmo (translate/rotate/scale/universal, local/world, pivot/center, snapping,
// multi-object), the view cube, marquee selection, component icons, click picking and asset
// drop placement. The "Game View" instance shows the game camera with aspect presets.
class SceneViewWidget
	: public HavanaWidget
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
	enum class GizmoOperation : uint8_t { Translate, Rotate, Scale, Universal };
	enum class PivotMode : uint8_t { Pivot, Center };

	SceneViewWidget(const std::string& inTitle, bool inSceneToolsEnabled = false);

	void Init() override;
	void Destroy() override;
	void SetData(Moonlight::CameraData& data);

	void Update() override;
	void Render() override;

	void SetGizmoOperation(GizmoOperation op);
	void ToggleGizmoSpace();
	void TogglePivotMode();
	void ToggleSnapping();

	// A left click (press + release without dragging) landed on the viewport this frame.
	bool ConsumeClick();

	Moonlight::CameraData* MainCamera = nullptr;
	EditorApp* App = nullptr;
	ImGuiWindowFlags WindowFlags = 0;
	bool IsFocused = false;
	// The mouse is over the rendered viewport image (not the toolbar or another window).
	bool IsViewportHovered = false;
	bool EnableSceneTools = false;
	bool MaximizeOnPlay = false;
	bool MaximizeOnPlayPreference = false;
	bool IsUsingGuizmo = false;
	bool IsPlatformWindow = false;

	Vector2 SceneViewRenderSize;
	Vector2 SceneViewRenderLocation;
	// Mouse position in viewport pixels (valid while hovered).
	Vector2 ViewportMousePosition;

	DisplayParams CurrentDisplayParams;

private:
	void DrawSceneToolbar();
	void DrawGameToolbar();
	void DrawManipulator();
	void DrawViewCube();
	void DrawIcons();
	void HandleMouse();
	void HandleAssetDrop();
	void BeginDrag();
	void EndDrag();
	Matrix4 ComputeProxyMatrix();
	Matrix4 GetProjection() const;
	void SavePreferences() const;

	GizmoOperation m_operation = GizmoOperation::Translate;
	ImGuizmo::MODE m_mode = ImGuizmo::LOCAL;
	PivotMode m_pivotMode = PivotMode::Pivot;
	bool m_snapEnabled = false;
	float m_snapTranslate = 0.5f;
	float m_snapRotate = 15.f;
	float m_snapScale = 0.1f;

	Vector2 m_viewportScreenMin;
	Vector2 m_viewportScreenSize;

	// Gizmo drag
	bool m_dragging = false;
	Matrix4 m_proxy;
	Matrix4 m_proxyStart;
	struct DraggedEntity
	{
		uint64_t GUID = 0;
		Matrix4 StartWorld;
		json Before;
	};
	std::vector<DraggedEntity> m_dragged;

	// Click / marquee
	bool m_pressed = false;
	bool m_marquee = false;
	Vector2 m_pressPosition;
	bool m_clicked = false;

	// Icons hit this frame (screen position, entity) for click selection.
	struct IconHit
	{
		Vector2 Position;
		EntityHandle Entity;
	};
	std::vector<IconHit> m_icons;

	std::vector<DisplayParams> DisplayOptions;
};

#endif
