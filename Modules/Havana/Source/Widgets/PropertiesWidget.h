#pragma once
#include <HavanaWidget.h>
#include "Events/EventReceiver.h"
#include <ECS/EntityHandle.h>
#include <JSON.h>
#include <string>
#include <unordered_map>
#include <vector>

class BaseCore;
class BaseComponent;
class Entity;
class World;

#if USING( ME_EDITOR )

// Inspector for the selection. Reflected components get generated UI (with multi-entity editing of
// the components every selected entity shares); custom OnEditorInspect code still runs for extra
// UI. Every edit made inside a component's section is captured as an undoable before/after
// snapshot, so custom inspector code gets undo for free.
class PropertiesWidget
	: public HavanaWidget
	, public EventReceiver
{
public:
	PropertiesWidget();

	void Init() override;
	void Destroy() override;

	void Update() override;
	void Render() override;

	static PropertiesWidget* Get();
	// Editor automation (ED-7): inspector-lock 1|0, inspector-search <text>,
	// inspector-assert-shows <Type>[.<Field>] | 1|0, inspector-assert-entity <name>.
	bool ExecuteAutomation(const std::string& InCommand, const std::string& InArgs, std::string& OutError);

private:
	struct Instance
	{
		Entity* Owner = nullptr;
		BaseComponent* Component = nullptr;
		std::string Key;
	};

	void DrawToolbar(bool InHasEntity);
	// Lock (ED-7): keep inspecting the current selection while others are selected.
	void SetLocked(bool InLocked);
	void DrawEntityHeader(Entity& entity);
	void DrawRenderCameraSettings(World& world);
	// InFilter: the field search, when it matched fields rather than the component's name.
	void DrawComponentGroup(const std::string& typeName, std::vector<Instance>& instances, const std::string& InFilter);
	void DrawComponentContextMenu(const std::string& typeName, std::vector<Instance>& instances);
	void CommitEdits(const std::string& typeName, std::vector<Instance>& instances, const std::string& undoName);
	void AddComponentPopup(Entity& entity);
	void SortComponents(std::vector<BaseComponent*>& components) const;

	// Pre-edit snapshots of the inspected components, keyed by "guid:type".
	std::unordered_map<std::string, json> m_snapshots;
	std::unordered_map<std::string, bool> m_pendingEdits;
	uint64_t m_selectionVersion = 0;
	uint64_t m_renderCameraGUID = 0; // 0 follows the main camera.
	char m_nameBuffer[256] = {};
	uint64_t m_nameEntityGUID = 0;
	bool m_nameEditing = false;
	char m_addComponentFilter[128] = {};
	char m_fieldFilter[128] = {};
	bool m_locked = false;
	std::vector<EntityHandle> m_lockedEntities;	// the inspected one first
	// What the last frame drew: "Type" and "Type.Field".
	std::vector<std::string> m_drawn;
	double m_prefabCheckTime = 0.0;
	uint64_t m_prefabCheckRoot = 0;
	std::string m_prefabBaseAsset;
	std::string m_prefabBase;
	bool m_prefabHasOverrides = false;
};

#endif
