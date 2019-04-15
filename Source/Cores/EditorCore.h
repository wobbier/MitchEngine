#pragma once
#include "ECS/Core.h"
#include "Events/EventReceiver.h"
#include "TranslationGizmo.h"

class Transform;
class Havana;

class EditorCore
	: public Core<EditorCore>
	, public EventReceiver
{
public:
	EditorCore(Havana* editor);
	~EditorCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual bool OnEvent(const BaseEvent& evt);

	void Update(float dt, Transform* rootTransform);

	virtual void OnEntityAdded(Entity& NewEntity) final;
	void SaveSceneRecursively(json& d, Transform* transform);
	Transform* RootTransform = nullptr;

private:
	Havana* m_editor = nullptr;
	TranslationGizmo* gizmo;
	WeakPtr<Entity> TransformEntity;
#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
	void SaveWorld();
};
