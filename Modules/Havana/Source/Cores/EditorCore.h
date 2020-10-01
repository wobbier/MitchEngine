#pragma once
#include <ECS/Core.h>

#include <Events/EventReceiver.h>

class Transform;
class Havana;
class Camera;

#if ME_EDITOR

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
	virtual void Update(float dt) final;

	virtual bool OnEvent(const BaseEvent& evt);

	void Update(float dt, Transform* rootTransform);

	virtual void OnEntityAdded(Entity& NewEntity) final;

	Transform* RootTransform = nullptr;

	Havana* GetEditor() const;

	Transform* GetEditorCameraTransform() const;

private:
	bool m_isTryingToSaveNewScene = false;
	float m_flyingSpeed = 5.f;

	float m_lookSensitivity = .15f;
	float m_speedModifier = 100.f;
	float LastX = 0.f;
	float LastY = 0.f;
	bool FirstUpdate = true;
	bool PreviousMouseDown = false;
	bool IsFocusingTransform = false;
	float startTime = 0.0f;
	float totalTime = 0.0f;
	float TravelDistance = 0.f;
	float m_focusSpeed = 10.f;

	Transform* EditorCameraTransform = nullptr;
	Camera* EditorCamera = nullptr;
	Havana* m_editor = nullptr;

	virtual void OnEditorInspect() final;
};

#endif