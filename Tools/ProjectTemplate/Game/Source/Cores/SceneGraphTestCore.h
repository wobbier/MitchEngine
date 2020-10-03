#pragma once
#include "ECS/Core.h"
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"

class SceneGraphTestObject
	: public Component<SceneGraphTestObject>
{
public:
	SceneGraphTestObject();
	virtual void Init() override;

private:
	void OnSerialize(json& outJson) override;


	void OnDeserialize(const json& inJson) override;

};

ME_REGISTER_COMPONENT(SceneGraphTestObject);

class SceneGraphTestCore
	: public Core<SceneGraphTestCore>
{
public:
	static constexpr const char* kPrefabName = "Assets/Scene Cube.prefab";
	static constexpr const char* kPrefabName2 = "Assets/Scene Cube 2.prefab";
	static constexpr const char* kPrefabName3 = "Assets/Scene Cube 3.prefab";
	static constexpr const char* kPrefabName4 = "Assets/Scene Cube 4.prefab";

	SceneGraphTestCore();
	virtual void OnEntityAdded(Entity& NewEntity) override;


	virtual void OnEntityRemoved(Entity& InEntity) override;

#if ME_EDITOR
	virtual void OnEditorInspect() override;
#endif

	virtual void Update(float dt) override;


	void OnEntityDestroyed(Entity& InEntity) override;

private:
	virtual void Init() override;

	virtual void OnStart() override;

	virtual void OnStop() override;

};

ME_REGISTER_CORE(SceneGraphTestCore);