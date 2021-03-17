#pragma once
#include <HavanaWidget.h>
#include <ECS/Entity.h>
#include <ECS/EntityHandle.h>

class World;
class Transform;
class BaseCore;
class EditorApp;

class SceneHierarchyWidget
	: public HavanaWidget
{
public:
	SceneHierarchyWidget();

	void Init() override;
	void Destroy() override;

	void SetData(Transform* inRoot, std::vector<Entity>& inEntities);

	void Update() override;
	void Render() override;

	void DrawAddCoreList();
	void UpdateWorldRecursive(Transform* root);

	Transform* m_rootTransform = nullptr;
	EditorApp* App = nullptr;
	std::vector<Entity>* Entities = nullptr;
};