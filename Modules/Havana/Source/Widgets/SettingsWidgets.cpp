#include "SettingsWidgets.h"
#include "Editor/EditorActions.h"
#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Engine/ProjectSettings.h"
#include "Cores/AudioCore.h"
#include "Cores/EditorCore.h"
#include "EditorApp.h"
#include <Utils/EditorConfig.h>
#include <Utils/PlatformUtils.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <unordered_map>

#if USING( ME_EDITOR )

namespace
{
	bool ContainsInsensitive(const std::string& text, const char* needle)
	{
		if (!needle || !*needle)
		{
			return true;
		}
		std::string a = text, b = needle;
		std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a.find(b) != std::string::npos;
	}

	// The chord being pressed this frame (non-modifier key + current modifiers), or 0.
	ImGuiKeyChord CaptureChord()
	{
		const ImGuiIO& io = ImGui::GetIO();
		for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
		{
			const ImGuiKey imguiKey = static_cast<ImGuiKey>(key);
			const bool isModifier = (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper) || key == ImGuiKey_ReservedForModCtrl || key == ImGuiKey_ReservedForModShift || key == ImGuiKey_ReservedForModAlt || key == ImGuiKey_ReservedForModSuper;
			if (isModifier || ImGui::IsMouseKey(imguiKey) || ImGui::IsAliasKey(imguiKey))
			{
				continue;
			}
			if (ImGui::IsKeyPressed(imguiKey, false))
			{
				ImGuiKeyChord chord = imguiKey;
				// Store Ctrl/Cmd as the portable "shortcut" modifier.
				if (io.KeyCtrl && !io.ConfigMacOSXBehaviors) chord |= ImGuiMod_Shortcut;
				if (io.KeySuper && io.ConfigMacOSXBehaviors) chord |= ImGuiMod_Shortcut;
				if (io.KeyCtrl && io.ConfigMacOSXBehaviors) chord |= ImGuiMod_Ctrl;
				if (io.KeySuper && !io.ConfigMacOSXBehaviors) chord |= ImGuiMod_Super;
				if (io.KeyShift) chord |= ImGuiMod_Shift;
				if (io.KeyAlt) chord |= ImGuiMod_Alt;
				return chord;
			}
		}
		return 0;
	}
}


PreferencesWidget::PreferencesWidget()
	: HavanaWidget("Preferences")
{
	IsOpen = false;
}


void PreferencesWidget::Init()
{
}


void PreferencesWidget::ApplyStartupPreferences()
{
	EditorConfig& config = EditorConfig::GetInstance();
	PlatformUtils::SetCodeEditorCommand(config.GetPreference<std::string>("CodeEditorCommand", ""));

	std::unordered_map<std::string, ImGuiKeyChord> overrides;
	const json shortcuts = config.GetPreference<json>("Shortcuts", json::object());
	if (shortcuts.is_object())
	{
		for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
		{
			if (it.value().is_number_integer())
			{
				overrides[it.key()] = it.value().get<int>();
			}
		}
	}
	EditorActions::Get().SetShortcutOverrides(overrides);
}


void PreferencesWidget::SaveShortcuts()
{
	json shortcuts = json::object();
	for (const auto& [id, chord] : EditorActions::Get().GetShortcutOverrides())
	{
		shortcuts[id] = chord;
	}
	EditorConfig::GetInstance().SetPreference("Shortcuts", shortcuts);
	EditorConfig::GetInstance().Save();
}


void PreferencesWidget::Render()
{
	if (!IsOpen)
	{
		m_capturingAction.clear();
		EditorActions::Get().ShortcutsSuspended = false;
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(560.f, 520.f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}
	if (ImGui::BeginTabBar("##PreferenceTabs"))
	{
		if (ImGui::BeginTabItem("General"))
		{
			DrawGeneral();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Scene View"))
		{
			DrawSceneView();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Shortcuts"))
		{
			DrawShortcuts();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}


void PreferencesWidget::DrawGeneral()
{
	EditorConfig& config = EditorConfig::GetInstance();
	float autosave = config.GetPreference<float>("Autosave.IntervalSeconds", 120.f);
	if (ImGui::DragFloat("Autosave interval (s)", &autosave, 5.f, 0.f, 3600.f, autosave <= 0.f ? "Off" : "%.0f"))
	{
		config.SetPreference("Autosave.IntervalSeconds", std::max(autosave, 0.f));
	}
	ImGui::SetItemTooltip("Unsaved scenes are snapshotted to .tmp/Autosave for crash recovery. 0 disables.");

	std::string codeEditor = config.GetPreference<std::string>("CodeEditorCommand", "");
	if (ImGui::InputTextWithHint("Code editor", "code -g \"{file}:{line}\"", &codeEditor))
	{
		config.SetPreference("CodeEditorCommand", codeEditor);
		PlatformUtils::SetCodeEditorCommand(codeEditor);
	}
	ImGui::SetItemTooltip("Command used to open file:line references from the console. {file} and {line} are substituted.");

	if (ImGui::Button("Save Preferences"))
	{
		config.Save();
	}
}


void PreferencesWidget::DrawSceneView()
{
	EditorConfig& config = EditorConfig::GetInstance();
	auto toggle = [&config](const char* label, const char* key, bool fallback) {
		bool value = config.GetPreference<bool>(key, fallback);
		if (ImGui::Checkbox(label, &value))
		{
			config.SetPreference(key, value);
		}
	};
	toggle("Show grid", "Scene.ShowGrid", true);
	toggle("Show component gizmos and icons", "Scene.ShowGizmos", true);
	toggle("Show selection bounds", "Scene.ShowSelection", true);
	toggle("Show stats overlay", "Scene.ShowStats", true);

	ImGui::SeparatorText("Snapping");
	float translate = config.GetPreference<float>("Snap.Translate", 0.5f);
	float rotate = config.GetPreference<float>("Snap.Rotate", 15.f);
	float scale = config.GetPreference<float>("Snap.Scale", 0.1f);
	if (ImGui::DragFloat("Move increment", &translate, 0.05f, 0.001f, 1000.f)) config.SetPreference("Snap.Translate", translate);
	if (ImGui::DragFloat("Rotate increment (deg)", &rotate, 1.f, 0.1f, 180.f)) config.SetPreference("Snap.Rotate", rotate);
	if (ImGui::DragFloat("Scale increment", &scale, 0.01f, 0.001f, 10.f)) config.SetPreference("Snap.Scale", scale);
	ImGui::TextDisabled("Snap values apply the next time the scene view loads its settings.");

	ImGui::SeparatorText("Camera");
	EditorApp* app = static_cast<EditorApp*>(GetEngine().GetGame());
	if (app && app->EditorSceneManager)
	{
		EditorCameraController& camera = app->EditorSceneManager->GetCameraController();
		float speed = camera.GetFlySpeed();
		if (ImGui::DragFloat("Fly speed", &speed, 0.1f, 0.1f, 500.f))
		{
			camera.SetFlySpeed(speed);
		}
		ImGui::DragFloat("Look sensitivity", &camera.LookSensitivity, 0.01f, 0.01f, 2.f);
		ImGui::DragFloat("Focus smoothing", &camera.FocusSmoothing, 0.1f, 1.f, 60.f);
	}
}


void PreferencesWidget::DrawShortcuts()
{
	EditorActions& actions = EditorActions::Get();
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputTextWithHint("##ShortcutFilter", "Filter actions", m_shortcutFilter, sizeof(m_shortcutFilter));

	if (!m_capturingAction.empty())
	{
		ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "Press a key combination (Esc cancels, Backspace clears)...");
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			m_capturingAction.clear();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
		{
			actions.SetShortcut(m_capturingAction, 0);
			SaveShortcuts();
			m_capturingAction.clear();
		}
		else if (const ImGuiKeyChord chord = CaptureChord())
		{
			if (const EditorAction* conflict = actions.FindConflict(m_capturingAction, chord))
			{
				// Steal the chord: the other action loses its binding.
				actions.SetShortcut(conflict->Id, 0);
			}
			actions.SetShortcut(m_capturingAction, chord);
			SaveShortcuts();
			m_capturingAction.clear();
		}
	}

	actions.ShortcutsSuspended = !m_capturingAction.empty();
	if (!ImGui::BeginTable("##Shortcuts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
	{
		return;
	}
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 150.f);
	ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, 50.f);
	ImGui::TableHeadersRow();

	std::vector<const EditorAction*> sorted;
	for (const EditorAction& action : actions.GetAll())
	{
		if (ContainsInsensitive(action.Category + " " + action.DisplayName, m_shortcutFilter))
		{
			sorted.push_back(&action);
		}
	}
	std::sort(sorted.begin(), sorted.end(), [](const EditorAction* a, const EditorAction* b) {
		return a->Category != b->Category ? a->Category < b->Category : a->DisplayName < b->DisplayName;
	});

	for (const EditorAction* action : sorted)
	{
		ImGui::PushID(action->Id.c_str());
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s: %s", action->Category.c_str(), action->DisplayName.c_str());
		ImGui::TableSetColumnIndex(1);
		const bool capturing = m_capturingAction == action->Id;
		std::string label = capturing ? "..." : EditorActions::ShortcutToString(action->Shortcut);
		if (label.empty())
		{
			label = "-";
		}
		if (ImGui::Button(label.c_str(), ImVec2(-1.f, 0.f)))
		{
			m_capturingAction = action->Id;
		}
		ImGui::TableSetColumnIndex(2);
		ImGui::BeginDisabled(action->Shortcut == action->DefaultShortcut);
		if (ImGui::SmallButton("Reset"))
		{
			actions.ResetShortcut(action->Id);
			SaveShortcuts();
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	}
	ImGui::EndTable();
}


ProjectSettingsWidget::ProjectSettingsWidget()
	: HavanaWidget("Project Settings")
{
	IsOpen = false;
}


void ProjectSettingsWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(560.f, 560.f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}

	Engine& engine = GetEngine();
	if (!ImGui::BeginTabBar("##ProjectSettingsTabs"))
	{
		ImGui::End();
		return;
	}
	if (ImGui::BeginTabItem("Time"))
	{
		float fixedHz = 1.f / std::max(engine.GetFixedTimeStep(), 0.0001f);
		if (ImGui::DragFloat("Fixed update rate (Hz)", &fixedHz, 1.f, 10.f, 1000.f, "%.0f"))
		{
			engine.SetFixedTimeStep(1.f / std::clamp(fixedHz, 10.f, 1000.f));
		}
		float maxFps = engine.GetMaxFrameRate();
		if (ImGui::DragFloat("Frame rate cap", &maxFps, 1.f, 0.f, 1000.f, maxFps <= 0.f ? "Uncapped" : "%.0f fps"))
		{
			engine.SetMaxFrameRate(std::max(maxFps, 0.f));
		}
		if (ImGui::Button("Save to Engine.cfg"))
		{
			EngineConfig& config = engine.GetConfig();
			config.Root["FixedTimeStep"] = engine.GetFixedTimeStep();
			config.Root["MaxFrameRate"] = engine.GetMaxFrameRate();
			config.Save();
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Layers"))
	{
		ProjectSettings& settings = ProjectSettings::Get();
		bool changed = false;
		for (int i = 0; i < ProjectSettings::kLayerCount; ++i)
		{
			ImGui::PushID(i);
			std::string name = settings.GetLayerName(i);
			ImGui::Text("%2d", i);
			ImGui::SameLine(40.f);
			ImGui::SetNextItemWidth(-1.f);
			if (ImGui::InputTextWithHint("##LayerName", "(unnamed)", &name))
			{
				settings.SetLayerName(i, name);
				changed = true;
			}
			ImGui::PopID();
		}
		if (changed)
		{
			settings.Save();
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Physics"))
	{
		ProjectSettings& settings = ProjectSettings::Get();
		ImGui::DragFloat3("Gravity", &settings.Gravity.x, 0.05f, -100.f, 100.f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			settings.Save();
		}

		// Layer collision matrix over the named layers (plus Default), triangular like Unity's.
		std::vector<int> layers;
		for (int i = 0; i < ProjectSettings::kLayerCount; ++i)
		{
			if (i == 0 || !settings.GetLayerName(i).empty())
			{
				layers.push_back(i);
			}
		}
		ImGui::TextDisabled("Layer collision (name more layers above to add them)");
		const int count = static_cast<int>(layers.size());
		const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoSavedSettings;
		if (count > 0 && ImGui::BeginTable("##LayerCollision", count + 1, flags))
		{
			ImGui::TableSetupColumn("##Rows", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
			for (int column = count - 1; column >= 0; --column)
			{
				ImGui::TableSetupColumn(settings.GetLayerLabel(layers[column]).c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
			}
			ImGui::TableAngledHeadersRow();
			bool changed = false;
			for (int row = 0; row < count; ++row)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(settings.GetLayerLabel(layers[row]).c_str());
				// Columns run from the last layer down to the row's own layer.
				for (int column = count - 1; column >= row; --column)
				{
					ImGui::TableSetColumnIndex(count - column);
					ImGui::PushID(row * ProjectSettings::kLayerCount + column);
					bool collide = settings.DoLayersCollide(layers[row], layers[column]);
					if (ImGui::Checkbox("##Collide", &collide))
					{
						settings.SetLayersCollide(layers[row], layers[column], collide);
						changed = true;
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s / %s", settings.GetLayerLabel(layers[row]).c_str(), settings.GetLayerLabel(layers[column]).c_str());
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
			if (changed)
			{
				settings.Save();
			}
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Input"))
	{
		ProjectSettings& settings = ProjectSettings::Get();
		Input& input = engine.GetInput();
		ImGui::InputText("Action map", &settings.InputActions);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			settings.Save();
		}
		ImGui::SameLine();
		if (ImGui::Button("Load"))
		{
			input.LoadActions(Path(settings.InputActions));
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("A .inputactions asset; edit it from the Assets window (Create > Input Actions)");
		}

		// Connected pads and the live value of every action (the game's input; zero while the
		// editor has focus).
		const int pads = input.GetGamepadCount();
		ImGui::TextDisabled("%d gamepad(s)", pads);
		for (int slot = 0; slot < kMaxGamepads; ++slot)
		{
			if (input.IsGamepadConnected(slot))
			{
				ImGui::BulletText("%d: %s", slot, input.GetGamepadName(slot).c_str());
			}
		}
		InputActionSystem& actions = input.GetActions();
		if (actions.HasMap() && ImGui::BeginTable("##Actions", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Context");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Bindings");
			ImGui::TableHeadersRow();
			for (const InputContext& context : actions.GetMap().Contexts)
			{
				for (const InputAction& action : context.Actions)
				{
					const InputActionState& state = actions.GetAction(action.Name);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(state.IsPressed() ? ImVec4(0.4f, 1.f, 0.5f, 1.f) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", action.Name.c_str());
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s%s", context.Name.c_str(), actions.IsContextEnabled(context.Name) ? "" : " (off)");
					ImGui::TableNextColumn();
					if (state.Type == InputActionType::Vector2)
					{
						ImGui::Text("%.2f, %.2f", state.Value.x, state.Value.y);
					}
					else
					{
						ImGui::Text("%.2f", state.Value.x);
					}
					ImGui::TableNextColumn();
					std::string bindings;
					for (size_t binding = 0; binding < action.Bindings.size(); ++binding)
					{
						bindings += (binding ? ", " : "") + actions.GetBindingDisplay(action.Name, binding);
					}
					ImGui::TextDisabled("%s", bindings.c_str());
				}
			}
			ImGui::EndTable();
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Audio"))
	{
		// Bus volumes apply live and are saved when a slider is released.
		ProjectSettings& settings = ProjectSettings::Get();
		AudioCore* audio = AudioCore::Get();
		if (audio && audio->IsSilent())
		{
			ImGui::TextDisabled("No audio device: mixing silently");
		}
		for (int bus = 0; bus < ProjectSettings::kAudioBusCount; ++bus)
		{
			if (ImGui::SliderFloat(AudioBusName(static_cast<AudioBus>(bus)), &settings.BusVolumes[bus], 0.f, 1.f, "%.2f") && audio)
			{
				audio->SetBusVolume(static_cast<AudioBus>(bus), settings.BusVolumes[bus]);
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				settings.Save();
			}
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Navigation"))
	{
		// Area names show in area pickers; costs multiply the length of paths through an area.
		ProjectSettings& settings = ProjectSettings::Get();
		if (ImGui::BeginTable("NavAreas", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.f);
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 120.f);
			ImGui::TableHeadersRow();
			for (int area = 0; area < ProjectSettings::kNavAreaCount; ++area)
			{
				ImGui::PushID(area);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%d", area);
				ImGui::TableNextColumn();
				// The two built-in areas keep their names (code refers to them).
				const bool builtIn = area == 0 || area == 1;
				ImGui::BeginDisabled(builtIn);
				ImGui::SetNextItemWidth(-1.f);
				ImGui::InputText("##name", &settings.NavAreaNames[area]);
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					settings.Save();
				}
				ImGui::EndDisabled();
				ImGui::TableNextColumn();
				ImGui::BeginDisabled(area == 1);
				ImGui::SetNextItemWidth(-1.f);
				ImGui::DragFloat("##cost", &settings.NavAreaCosts[area], 0.05f, 0.01f, 1000.f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					settings.Save();
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Area 1 (Not Walkable) is removed from the navmesh. Costs apply to new path queries.");

		// Agent types: the body sizes surfaces bake for; agents walk the surfaces of their type.
		ImGui::Separator();
		ImGui::TextUnformatted("Agent Types");
		bool agentsChanged = false;
		int removeType = -1;
		if (ImGui::BeginTable("NavAgentTypes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Radius", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("Slope", ImGuiTableColumnFlags_WidthFixed, 80.f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.f);
			ImGui::TableHeadersRow();
			for (int i = 0; i < static_cast<int>(settings.NavAgentTypes.size()); ++i)
			{
				ProjectSettings::NavAgentType& type = settings.NavAgentTypes[i];
				ImGui::PushID(i);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				ImGui::InputText("##name", &type.Name);
				agentsChanged |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				ImGui::DragFloat("##radius", &type.Radius, 0.01f, 0.05f, 20.f, "%.2f");
				agentsChanged |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				ImGui::DragFloat("##height", &type.Height, 0.01f, 0.1f, 50.f, "%.2f");
				agentsChanged |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				ImGui::DragFloat("##climb", &type.MaxClimb, 0.01f, 0.f, 10.f, "%.2f");
				agentsChanged |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				ImGui::DragFloat("##slope", &type.MaxSlope, 0.5f, 0.f, 89.f, "%.0f");
				agentsChanged |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::TableNextColumn();
				ImGui::BeginDisabled(settings.NavAgentTypes.size() <= 1);
				if (ImGui::SmallButton("x"))
				{
					removeType = i;
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (removeType >= 0)
		{
			settings.NavAgentTypes.erase(settings.NavAgentTypes.begin() + removeType);
			agentsChanged = true;
		}
		ImGui::BeginDisabled(settings.NavAgentTypes.size() >= static_cast<size_t>(ProjectSettings::kMaxNavAgentTypes));
		if (ImGui::Button("Add Agent Type"))
		{
			settings.NavAgentTypes.push_back({ "Agent " + std::to_string(settings.NavAgentTypes.size() + 1) });
			agentsChanged = true;
		}
		ImGui::EndDisabled();
		if (agentsChanged)
		{
			settings.Save();
		}
		ImGui::TextDisabled("Surfaces bake for one type; agents of that type walk them. Re-bake after changing a size.");
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	ImGui::End();
}

#endif
