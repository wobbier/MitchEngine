#pragma once
#include <ECS/Core.h>

#include <Events/EventReceiver.h>
#include "Pointers.h"

class Transform;
class Havana;
class Camera;

#if USING( ME_EDITOR )

class EditorCore
	: public Core<EditorCore>
	, public EventReceiver
{
public:
	EditorCore() = delete;
	EditorCore(Havana* editor);
	~EditorCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(const UpdateContext& inUpdateContext) final;

	virtual bool OnEvent(const BaseEvent& evt);

	void Update(const UpdateContext& inUpdateContext, Transform* rootTransform);

	virtual void OnEntityAdded(Entity& NewEntity) final;

	Transform* RootTransform = nullptr;

	Havana* GetEditor() const;

	SharedPtr<Transform> GetEditorCameraTransform() const;

private:
	float m_flyingSpeed = 25.f;

	float m_lookSensitivity = 40.f;
	float m_speedModifier = 2.f;
	float LastX = 0.f;
	float LastY = 0.f;
	bool FirstUpdate = true;
	bool PreviousMouseDown = false;
	bool IsFocusingTransform = false;
	float startTime = 0.0f;
	float totalTime = 0.0f;
	float TravelDistance = 0.f;
	float m_focusSpeed = 10.f;

	SharedPtr<Transform> EditorCameraTransform = nullptr;
	Camera* EditorCamera = nullptr;
	Havana* m_editor = nullptr;

	virtual void OnEditorInspect() final;
};

#endif