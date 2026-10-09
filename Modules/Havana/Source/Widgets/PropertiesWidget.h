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

#if USING( ME_EDITOR )

// Inspector for the selection. Reflected components get generated UI (with multi-entity editing of
// the components every selected entity shares); custom OnEditorInspect code still runs for extra
// UI. Every edit made inside a component's section is captured as an undoable before/after
// snapshot, so custom inspector code gets undo for free.
class PropertiesWidget
	: public HavanaWidget
{
public:
	PropertiesWidget();

	void Init() override;
	void Destroy() override;

	void Update() override;
	void Render() override;

private:
	struct Instance
	{
		Entity* Owner = nullptr;
		BaseComponent* Component = nullptr;
		std::string Key;
	};

	void DrawEntityHeader(Entity& entity);
	void DrawComponentGroup(const std::string& typeName, std::vector<Instance>& instances);
	void DrawComponentContextMenu(const std::string& typeName, std::vector<Instance>& instances);
	void CommitEdits(const std::string& typeName, std::vector<Instance>& instances, const std::string& undoName);
	void AddComponentPopup(Entity& entity);
	void SortComponents(std::vector<BaseComponent*>& components) const;

	// Pre-edit snapshots of the inspected components, keyed by "guid:type".
	std::unordered_map<std::string, json> m_snapshots;
	std::unordered_map<std::string, bool> m_pendingEdits;
	uint64_t m_selectionVersion = 0;
	char m_nameBuffer[256] = {};
	uint64_t m_nameEntityGUID = 0;
	bool m_nameEditing = false;
	char m_addComponentFilter[128] = {};
};

#endif
