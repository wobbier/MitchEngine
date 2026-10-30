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
struct SDL_Window;
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
	// Nothing to show (no camera).
	void ClearData() { MainCamera = nullptr; }

	void Update() override;
	void Render() override;

	void SetGizmoOperation(GizmoOperation op);
	void ToggleGizmoSpace();
	void TogglePivotMode();
	void ToggleSnapping();
	void SetShadingMode(Moonlight::ViewShadingMode mode);
	void ResetViewOptions();
	// Automation: a view option by id (DebugDraw, Fog, ...) for this session, not saved; false
	// for an unknown id.
	bool SetViewOption(const std::string& InId, bool InValue);
	// Automation (ED-8): the selection onto the surface at a fraction of the view, as Shift-drag does.
	bool PlaceSelectionOnSurface(const Vector2& InViewportFraction);
	// Automation (ED-8): measures between two points of the view (fractions) as the Measure tool
	// does, and returns the distance in metres.
	float Measure(const Vector2& InFrom, const Vector2& InTo);
	const Vector3& GetMeasureFrom() const { return m_measureFrom; }
	const Vector3& GetMeasureTo() const { return m_measureTo; }

	// A left click (press + release without dragging) landed on the viewport this frame.
	bool ConsumeClick();
	// Clicks the viewport at a point given as a fraction of its size (automation).
	void RequestClick(const Vector2& InViewportFraction);
	// Called after camera navigation consumes this frame's mouse movement.
	void WrapNavigationCursor();
	// The OS window showing the viewport image and the image's rectangle in that window's
	// coordinates; false when the image wasn't shown this frame (closed, hidden tab, collapsed).
	bool GetImageWindowRect(SDL_Window*& OutWindow, Vector2& OutMin, Vector2& OutSize) const;
	// Draws over the viewport image (the UI designer's selection outline): the foreground list
	// of the image's OS window and the image's screen rectangle; null when it wasn't shown.
	ImDrawList* GetImageDrawList(Vector2& OutMin, Vector2& OutSize) const;
	// The display presets ("Freeform", "1080p", "Steam Deck", ...): false for an unknown name.
	bool SetDisplayPreset(const std::string& InName);
	// Where the game's image is drawn (ImGui's absolute coordinates) and its drawn size.
	Vector2 GetViewportScreenMin() const { return m_viewportScreenMin; }
	Vector2 GetViewportScreenSize() const { return m_viewportScreenSize; }
	std::vector<std::string> GetDisplayPresetNames() const;

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
	void DrawShadingMenu();
	void DrawViewMenu();
	void DrawSceneToolbar();
	void DrawGameToolbar();
	void DrawManipulator();
	// ED-8, the Measure tool: LMB drag between surface points; true while it owns the mouse.
	bool DrawMeasure();
	Vector3 SurfacePoint(const Vector2& InPixel) const;
	void DrawViewCube();
	void DrawStatsOverlay();
	// Per view: "Scene.ShowStats" or "GameView.ShowStats".
	const char* GetStatsPreference() const;
	void DrawIcons();
	void HandleMouse();
	void HandleAssetDrop();
	void BeginDrag();
	void EndDrag();
	Matrix4 ComputeProxyMatrix();
	Matrix4 GetProjection() const;
	void SavePreferences() const;

	GizmoOperation m_operation = GizmoOperation::Translate;
	Moonlight::ViewShadingMode m_shadingMode = Moonlight::ViewShadingMode::Lit;
	Moonlight::ViewRenderOptions m_viewOptions;
	ImGuizmo::MODE m_mode = ImGuizmo::LOCAL;
	PivotMode m_pivotMode = PivotMode::Pivot;
	bool m_snapEnabled = false;
	bool m_measureMode = false;
	bool m_measuring = false;
	bool m_hasMeasure = false;
	Vector3 m_measureFrom;
	Vector3 m_measureTo;
	float m_snapTranslate = 0.5f;
	float m_snapRotate = 15.f;
	float m_snapScale = 0.1f;

	Vector2 m_viewportScreenMin;
	Vector2 m_viewportScreenSize;
	Vector2 m_viewportWindowMin;
	bool m_imageShown = false;
	ImGuiID m_viewportId = 0;

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
	bool m_hasRequestedClick = false;
	Vector2 m_requestedClick;

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
