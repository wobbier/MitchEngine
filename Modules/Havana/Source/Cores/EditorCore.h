#pragma once
#include <ECS/Core.h>

#include <Events/EventReceiver.h>
#include "Pointers.h"
#include "Math/Vector3.h"
#include "Editor/EditorCameraController.h"

#include "Components/Transform.h"
class Havana;
class Camera;

#if USING( ME_EDITOR )

// Editor-only core: owns the scene view camera (navigation via EditorCameraController), queues the
// scene overlays (grid, selection, gizmos) and saves scenes.
class EditorCore
	: public Core<EditorCore>
	, public EventReceiver
{
public:
	EditorCore() = delete;
	EditorCore(Havana* editor);
	~EditorCore();

	virtual void Init() final;

	virtual void Update(const UpdateContext& inUpdateContext) final;

	virtual bool OnEvent(const BaseEvent& evt);

	void Update(const UpdateContext& inUpdateContext, Transform* rootTransform);

	Transform* RootTransform = nullptr;

	Havana* GetEditor() const;

	SharedPtr<Transform> GetEditorCameraTransform() const;
	EditorCameraController& GetCameraController() { return m_camera; }

	// Frames the selection (or the whole scene when nothing is selected).
	void FrameSelection();

private:
	void RegisterViewActions();
	void SaveCameraState();

	bool FirstUpdate = true;

	SharedPtr<Transform> EditorCameraTransform = nullptr;
	Camera* EditorCamera = nullptr;
	Havana* m_editor = nullptr;
	EditorCameraController m_camera;
	float m_saveTimer = 0.f;

	virtual void OnEditorInspect() final;
};

#endif
