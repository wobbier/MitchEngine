#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"
#include <imgui.h>
#include <string>

#if USING( ME_EDITOR )

// Per-user editor preferences (EditorConfig): general, scene view, keyboard shortcuts.
class PreferencesWidget
	: public HavanaWidget
{
public:
	PreferencesWidget();

	void Init() override;
	void Destroy() override {}
	void Update() override {}
	void Render() override;

	// Applies persisted preferences that live outside EditorConfig (shortcuts, code editor).
	static void ApplyStartupPreferences();

private:
	void DrawGeneral();
	void DrawSceneView();
	void DrawShortcuts();
	void SaveShortcuts();

	std::string m_capturingAction;
	char m_shortcutFilter[64] = {};
};

// Project-wide settings: timing (Engine.cfg) and layer names (ProjectSettings.json).
class ProjectSettingsWidget
	: public HavanaWidget
{
public:
	ProjectSettingsWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;
};

#endif
