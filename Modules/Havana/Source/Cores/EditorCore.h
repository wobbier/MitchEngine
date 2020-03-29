#pragma once
#include "ECS/Core.h"
#include "Events/EventReceiver.h"
#include "TranslationGizmo.h"
#include <string>
#include "Math/Vector3.h"

class Transform;
class Havana;

class EditorCore
	: public Core<EditorCore>
	, public EventReceiver
{
public:
	EditorCore() = default;
	EditorCore(Havana* editor);
	~EditorCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual bool OnEvent(const BaseEvent& evt);

	void Update(float dt, Transform* rootTransform);

	virtual void OnEntityAdded(Entity& NewEntity) final;

	Transform* RootTransform = nullptr;

	Havana* GetEditor();

	Transform* GetEditorCameraTransform() const;

private:
	bool TryingToSaveNewScene = false;
	float FlyingSpeed = 5.f;

	float LookSensitivity = .15f;
	float SpeedModifier = 100.f;
	float LastX = 0.f;
	float LastY = 0.f;
	bool FirstUpdate = true;
	bool InputEnabled = false;
	bool PreviousMouseDown = false;
	bool IsFocusingTransform = false;
	float startTime = 0.0f;
	float totalTime = 0.0f;
	float TravelDistance = 0.f;
	float FocusSpeed = 10.f;

	Transform* EditorCameraTransform = nullptr;
	Camera* EditorCamera = nullptr;

	Havana* m_editor = nullptr;
	TranslationGizmo* gizmo;
	class AudioSource* testAudio;
#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
};