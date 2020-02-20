#pragma once
#include "ECS/Core.h"

class Transform;

class SceneGraph
	: public Core<SceneGraph>
{
public:
	SceneGraph();
	~SceneGraph();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	void UpdateRecursively(Transform* transform);
	Transform* RootTransform = nullptr;

	void OnEntityRemoved(Entity& InEntity) override;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif

};