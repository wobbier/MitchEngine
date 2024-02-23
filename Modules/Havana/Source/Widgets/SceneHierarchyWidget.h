#pragma once
#include <HavanaWidget.h>
#include <ECS/Entity.h>
#include <ECS/EntityHandle.h>
#include <Events/EventReceiver.h>
#include <Utils/CommonUtils.h>

class World;
class Transform;
class BaseCore;
class EditorApp;

#if USING( ME_EDITOR )

class SceneHierarchyWidget
	: public HavanaWidget
	, public EventReceiver
{
public:
	SceneHierarchyWidget();

	void Init() override;
	void Destroy() override;

	bool OnEvent(const BaseEvent& evt) override;
	void SetData(Transform* inRoot, std::vector<Entity>& inEntities);

	void Update() override;
	void Render() override;

	void DrawAddCoreList();
	void UpdateWorldRecursive(Transform* root);

	void DrawEntityRightClickMenu(Transform* transform);
	void ClearSelection();

	void HandleAssetDragAndDrop(Transform* root);

	Transform* RootTransform = nullptr;
	EditorApp* App = nullptr;
	std::vector<Entity>* Entities = nullptr;

	EntityHandle SelectedEntity;
	WeakPtr<Transform> SelectedTransform;
	class BaseCore* SelectedCore = nullptr;

	ParentDescriptor DragParentDescriptor;
};

#endif