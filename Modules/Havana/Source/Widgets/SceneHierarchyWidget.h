#pragma once
#include <HavanaWidget.h>
#include <ECS/Entity.h>
#include <ECS/EntityHandle.h>
#include <Events/EventReceiver.h>
#include <Utils/CommonUtils.h>
#include <string>
#include <vector>

class World;
class Transform;
class BaseCore;
class EditorApp;

#if USING( ME_EDITOR )

// Scene tree: multi-select (Ctrl toggles, Shift selects a range), search ("t:Light" filters by
// component), inline rename (F2 / double-click), drag to reparent or reorder (keeps world
// transforms), active toggles, prefab instance colouring. All edits are undoable.
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
	// Create menu entries for common objects (primitives, camera, light...).
	void DrawCreateTemplates(Transform* parent);

	// Starts inline rename of the active selection (Edit.Rename).
	void BeginRename();

	Transform* RootTransform = nullptr;
	EditorApp* App = nullptr;
	std::vector<Entity>* Entities = nullptr;

	ParentDescriptor DragParentDescriptor;

private:
	enum class DropZone { Into, Above, Below };

	void DrawNode(Transform* node);
	void DrawRowContents(Entity& entity, Transform* transform, bool isOpenable, bool& outOpen);
	void HandleRowClick(Entity& entity);
	void HandleDragDrop(Entity& entity, Transform* transform);
	void HandleAssetDrop(Transform* parent);
	void DrawEntityContextMenu(Entity& entity, Transform* transform);
	void DrawTransformlessEntities(World& world);
	void DrawCores(World& world);
	bool PassesFilter(Entity& entity) const;
	bool SubtreePassesFilter(Transform* node) const;

	char m_filter[128] = {};
	EntityHandle m_renaming;
	char m_renameBuffer[256] = {};
	bool m_focusRename = false;
	EntityHandle m_rangeAnchor;
	// Rows in draw order this frame / last frame (for Shift-click ranges).
	std::vector<EntityHandle> m_visibleRows;
	std::vector<EntityHandle> m_lastVisibleRows;
	EntityHandle m_scrollTo;
};

#endif
