#pragma once
#include "Engine/Core.h"

class SceneGraph : public Core<SceneGraph>
{
public:
	SceneGraph();
	~SceneGraph();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;

	Entity RootEntity;
};
