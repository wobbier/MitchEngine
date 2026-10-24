#include "SettingsWidgets.h"
#include "Editor/EditorActions.h"
#include "Engine/Engine.h"
#include "Engine/HardwareBudget.h"
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
#include <ImGuiTheme.h>
#include <Utils/HavanaUtils.h>
#include <ImGui/ImGuiRenderer.h>
#include <Renderer.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include <File.h>
#include <Path.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <unordered_map>

#if USING( ME_EDITOR )

namespace
{
	ImGuiTheme::Mode AppearanceMode()
	{
		return EditorConfig::GetInstance().GetPreference<std::string>("Appearance.Theme", "Dark") == "Light"
			? ImGuiTheme::Mode::Light : ImGuiTheme::Mode::Dark;
	}

	ImVec4 AppearanceAccent()
	{
		const json value = EditorConfig::GetInstance().GetPreference<json>("Appearance.Accent", json());
		if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() || !value[2].is_number())
			return ImGuiTheme::DefaultAccent();
		return ImGuiTheme::SanitizeAccent(ImVec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), 1.f));
	}

	struct EditorFont { const char* Name; const char* File; };
	// Bundled in Engine/Assets/Fonts (open licenses beside each file). An empty file is the
	// renderer's embedded Roboto, which is also the fallback when a file can't be read.
	const EditorFont kEditorFonts[] = {
		{ "Inter", "Assets/Fonts/Inter-Regular.ttf" },
		{ "Roboto", "" },
		{ "Inconsolata (mono)", "Assets/Fonts/Inconsolata-Regular.ttf" },
		{ "JetBrains Mono (mono)", "Assets/Fonts/JetBrainsMono-Regular.ttf" },
		{ "Hack (mono)", "Assets/Fonts/Hack-Regular.ttf" },
	};
	constexpr float kMaxFontSize = 28.f;

	int AppearanceFont()
	{
		const std::string name = EditorConfig::GetInstance().GetPreference<std::string>("Appearance.Font", kEditorFonts[0].Name);
		for (int i = 0; i < IM_ARRAYSIZE(kEditorFonts); ++i)
			if (name == kEditorFonts[i].Name)
				return i;
		return 0;
	}

	float AppearanceFontSize()
	{
		const float size = EditorConfig::GetInstance().GetPreference<float>("Appearance.FontSize", ImGuiRenderer::kMinFontSize);
		return std::isfinite(size) ? std::clamp(std::round(size), ImGuiRenderer::kMinFontSize, kMaxFontSize) : ImGuiRenderer::kMinFontSize;
	}

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


	ImVec4 BudgetColor(HardwareBudget::Status status)
	{
		switch (status)
		{
		case HardwareBudget::Status::Over:
			return ImGuiTheme::ErrorText();
		case HardwareBudget::Status::Near:
			return ImGuiTheme::WarningText();
		default:
			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}
	}


	// Project Settings > Hardware: the target profile, its budgets live, and which of the target's
	// missing features the project uses.
	void DrawHardwareTab()
	{
		ProjectSettings& settings = ProjectSettings::Get();
		HardwareBudget& budget = HardwareBudget::Get();
		const HardwareProfile* profile = budget.GetActiveProfile();

		HavanaUtils::Label("Target hardware");
		const std::string& active = budget.GetActiveName();
		const std::string preview = active.empty() ? "None" : (profile ? active : active + " (not defined)");
		if (ImGui::BeginCombo("##HardwareProfile", preview.c_str()))
		{
			std::string chosen = active;
			bool picked = false;
			if (ImGui::Selectable("None", active.empty()))
			{
				chosen.clear();
				picked = true;
			}
			for (const HardwareProfile& candidate : budget.GetProfiles())
			{
				if (ImGui::Selectable(candidate.Name.c_str(), candidate.Name == active))
				{
					chosen = candidate.Name;
					picked = true;
				}
				if (!candidate.Description.empty())
				{
					ImGui::SetItemTooltip("%s", candidate.Description.c_str());
				}
			}
			ImGui::EndCombo();
			if (picked && chosen != active)
			{
				budget.SetActiveProfile(chosen);
				settings.HardwareProfileName = chosen;
				settings.Save();
				profile = budget.GetActiveProfile();
			}
		}
		bool enforced = budget.IsEnforced();
		HavanaUtils::Label("Enforce feature caps");
		if (ImGui::Checkbox("##EnforceProfile", &enforced))
		{
			budget.SetEnforced(enforced);
			settings.EnforceHardwareProfile = enforced;
			settings.Save();
		}
		ImGui::SetItemTooltip("Turn off the effects the target lacks and cap texture and shadow sizes, so the game shows the target's look. Off, the caps are only reported. Textures reload when the size cap changes.");
		if (!profile)
		{
			ImGui::TextWrapped(active.empty()
				? "Pick a target to hold the project to its frame time, memory and draw budgets."
				: "This profile isn't defined in either profiles file.");
		}
		else
		{
			if (!profile->Description.empty())
			{
				ImGui::TextWrapped("%s", profile->Description.c_str());
			}
			if (!profile->ShaderModel.empty())
			{
				ImGui::TextDisabled("%s", profile->ShaderModel.c_str());
			}

			ImGui::SeparatorText("Budgets");
			const auto& lines = budget.GetLines();
			if (lines.empty())
			{
				ImGui::TextDisabled("This profile sets no budgets.");
			}
			else if (ImGui::BeginTable("##Budgets", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Budget");
				ImGui::TableSetupColumn("Used");
				ImGui::TableSetupColumn("Peak");
				ImGui::TableSetupColumn("Limit");
				ImGui::TableHeadersRow();
				for (const HardwareBudget::Line& line : lines)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(line.Name);
					ImGui::SetItemTooltip("%s", line.Tooltip);
					ImGui::TableNextColumn();
					const float fraction = line.Limit > 0.0 ? static_cast<float>(line.Used / line.Limit) : 0.f;
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, line.State == HardwareBudget::Status::Ok ? ImGuiTheme::SuccessText() : BudgetColor(line.State));
					ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f), ImVec2(-FLT_MIN, 0.f), HardwareBudget::FormatUsed(line).c_str());
					ImGui::PopStyleColor();
					ImGui::SetItemTooltip("%.0f%% of the budget", fraction * 100.f);
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextColored(BudgetColor(line.Peak > line.Limit ? HardwareBudget::Status::Over : HardwareBudget::Status::Ok), "%s", HardwareBudget::FormatValue(line, line.Peak).c_str());
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(HardwareBudget::FormatLimit(line).c_str());
				}
				ImGui::EndTable();
			}
			if (ImGui::Button("Reset Peaks"))
			{
				budget.ResetPeaks();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Frame times are measured on this machine; in the editor they include the editor.");

			// Each feature the target lacks, and whether the project uses it (live cameras' post
			// settings, the renderer's settings, loaded textures).
			ImGui::SeparatorText("Feature Caps");
			BGFXRenderer& renderer = GetEngine().GetRenderer();
			bool ao = false, bloom = false, dof = false, volumetric = false, taa = false;
			for (const Moonlight::CameraData& camera : renderer.GetCameraCache().Commands)
			{
				if (!camera.Buffer || !camera.ShouldRender)
				{
					continue;
				}
				ao |= camera.Post.AmbientOcclusion;
				bloom |= camera.Post.Bloom;
				dof |= camera.Post.DepthOfField;
				volumetric |= camera.Post.Fog && camera.Post.VolumetricFog;
				taa |= camera.Post.TemporalAA;
			}
			const HardwareProfile::FeatureCaps& caps = profile->Caps;
			const int shadowResolution = std::max({ int(renderer.Shadows.CascadeResolution), int(renderer.Shadows.SpotResolution), int(renderer.Shadows.PointResolution) });
			int largeTextures = 0;
			if (caps.MaxTextureSize > 0 && !enforced)
			{
				for (const auto& entry : ResourceCache::GetInstance().GetResouceStack())
				{
					const Moonlight::Texture* texture = dynamic_cast<const Moonlight::Texture*>(entry.second.get());
					largeTextures += texture && bgfx::isValid(texture->TexHandle) && std::max(texture->mWidth, texture->mHeight) > caps.MaxTextureSize ? 1 : 0;
				}
			}
			if (ImGui::BeginTable("##Caps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Feature");
				ImGui::TableSetupColumn("Target");
				ImGui::TableSetupColumn("Project");
				ImGui::TableHeadersRow();
				// exceeds: the project uses more than the target has.
				const auto row = [&](const char* name, const std::string& target, bool exceeds, const std::string& usage) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(target.c_str());
					ImGui::TableNextColumn();
					if (!exceeds)
					{
						ImGui::TextDisabled("%s", usage.c_str());
					}
					else if (enforced)
					{
						ImGui::TextColored(ImGuiTheme::InfoText(), "%s (capped)", usage.c_str());
					}
					else
					{
						ImGui::TextColored(ImGuiTheme::WarningText(), "%s (over)", usage.c_str());
					}
				};
				const auto feature = [&](const char* name, bool allowed, bool used) {
					row(name, allowed ? "Yes" : "No", used && !allowed, used ? "In use" : "Not used");
				};
				const auto size = [](int texels) { return texels > 0 ? std::to_string(texels) : std::string("Any"); };
				row("Texture size", size(caps.MaxTextureSize), largeTextures > 0, enforced ? std::string("Capped at load") : std::to_string(largeTextures) + " larger");
				feature("Shadows", caps.Shadows, renderer.Shadows.Enabled);
				row("Shadow resolution", size(caps.MaxShadowResolution), caps.MaxShadowResolution > 0 && shadowResolution > caps.MaxShadowResolution, std::to_string(shadowResolution));
				feature("Ambient occlusion", caps.AmbientOcclusion, ao);
				feature("Bloom", caps.Bloom, bloom);
				feature("Depth of field", caps.DepthOfField, dof);
				feature("Volumetric fog", caps.VolumetricFog, volumetric);
				feature("Temporal AA", caps.TemporalAA, taa);
				feature("Environment lighting", caps.EnvironmentLighting, renderer.EnableEnvironmentLighting);
				feature("Instancing", caps.Instancing, true);
				ImGui::EndTable();
			}
		}

		ImGui::SeparatorText("Profiles");
		ImGui::TextWrapped("Built in: %s. The project's %s adds profiles, replaces one with the same name, or starts one from another with \"Extends\".",
			HardwareBudget::kEngineProfilesPath, HardwareBudget::kProjectProfilesPath);
		if (ImGui::Button("Reload Profiles"))
		{
			budget.ReloadProfiles();
		}
		ImGui::SameLine();
		const Path projectFile(HardwareBudget::kProjectProfilesPath, true);
		if (ImGui::Button(projectFile.Exists ? "Edit Project Profiles" : "Create Project Profiles"))
		{
			if (!projectFile.Exists)
			{
				// A project profile to tune, starting from the current target.
				json entry = { { "Name", profile ? profile->Name + " (Project)" : std::string("Project Target") }, { "Description", "This project's budgets." } };
				if (profile)
				{
					entry["Extends"] = profile->Name;
				}
				else
				{
					entry["TargetFps"] = 60;
				}
				json root;
				root["Profiles"] = json::array({ entry });
				File file{ projectFile };
				file.Write(root.dump(4));
				budget.ReloadProfiles();
			}
			PlatformUtils::OpenInCodeEditor(projectFile.FullPath, 1);
		}
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


void PreferencesWidget::ApplyAppearancePreferences()
{
	ImGuiTheme::Apply(AppearanceMode(), AppearanceAccent(), AppearanceFontSize());
	if (ImGuiRenderer* imgui = GetEngine().GetRenderer().GetImGuiRenderer())
	{
		const EditorFont& font = kEditorFonts[AppearanceFont()];
		imgui->SetFont(*font.File ? Path(font.File).FullPath : std::string(), AppearanceFontSize());
	}
}


void PreferencesWidget::ApplyPendingAppearance()
{
	if (m_appearancePending)
	{
		ApplyAppearancePreferences();
		m_appearancePending = false;
	}
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
	ImGui::SetNextWindowSize(ImVec2(ImGuiTheme::Em(31.f), ImGuiTheme::Em(29.f)), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}
	if (ImGui::BeginTabBar("##PreferenceTabs"))
	{
		if (ImGui::BeginTabItem("Appearance"))
		{
			DrawAppearance();
			ImGui::EndTabItem();
		}
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


void PreferencesWidget::DrawAppearance()
{
	EditorConfig& config = EditorConfig::GetInstance();
	ImGuiTheme::Mode mode = AppearanceMode();
	ImVec4 accent = AppearanceAccent();
	bool changed = false;

	int theme = mode == ImGuiTheme::Mode::Light ? 1 : 0;
	HavanaUtils::Label("Theme");
	if (ImGui::Combo("##Theme", &theme, "Dark\0Light\0"))
	{
		mode = theme == 1 ? ImGuiTheme::Mode::Light : ImGuiTheme::Mode::Dark;
		changed = true;
	}

	ImGui::SeparatorText("Font");
	int font = AppearanceFont();
	HavanaUtils::Label("Typeface");
	if (ImGui::BeginCombo("##Typeface", kEditorFonts[font].Name))
	{
		for (int i = 0; i < IM_ARRAYSIZE(kEditorFonts); ++i)
		{
			if (ImGui::Selectable(kEditorFonts[i].Name, i == font))
			{
				font = i;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	int fontSize = static_cast<int>(AppearanceFontSize());
	HavanaUtils::Label("Size");
	if (ImGui::SliderInt("##Size", &fontSize, static_cast<int>(ImGuiRenderer::kMinFontSize), static_cast<int>(kMaxFontSize), "%d px", ImGuiSliderFlags_AlwaysClamp))
		changed = true;

	ImGui::SeparatorText("Accent");
	struct Preset { const char* Name; ImVec4 Color; };
	const Preset presets[] = {
		{ "Blue (default)", ImGuiTheme::DefaultAccent() },
		{ "Pink", ImVec4(0.88f, 0.30f, 0.55f, 1.f) },
		{ "Lavender", ImVec4(0.58f, 0.43f, 0.85f, 1.f) },
		{ "Mint", ImVec4(0.20f, 0.66f, 0.51f, 1.f) },
		{ "Amber", ImVec4(0.88f, 0.57f, 0.19f, 1.f) }
	};
	HavanaUtils::Label("Presets");
	for (int i = 0; i < IM_ARRAYSIZE(presets); ++i)
	{
		if (i) ImGui::SameLine();
		const Preset& preset = presets[i];
		const bool selected = std::abs(accent.x - preset.Color.x) < 0.001f && std::abs(accent.y - preset.Color.y) < 0.001f && std::abs(accent.z - preset.Color.z) < 0.001f;
		if (ImGui::ColorButton(preset.Name, preset.Color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImGuiTheme::IconButtonSize()))
		{
			accent = preset.Color;
			changed = true;
		}
		ImGui::SetItemTooltip("%s%s", preset.Name, selected ? " (selected)" : "");
		if (selected)
		{
			const float ring = std::max(1.f, std::round(ImGuiTheme::InnerGap() / 3.f));
			const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRect(ImVec2(min.x - ring, min.y - ring), ImVec2(max.x + ring, max.y + ring), ImGui::GetColorU32(ImGuiCol_Text));
		}
	}
	HavanaUtils::Label("Custom color");
	changed |= ImGui::ColorEdit3("##CustomColor", &accent.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoDragDrop);

	const ImGuiTheme::Palette palette = ImGuiTheme::MakePalette(mode, accent);
	const ImVec4 shades[] = { palette.Accent, palette.Hovered, palette.Pressed };
	const char* labels[] = { "Accent", "Hover", "Pressed" };
	HavanaUtils::Label("States");
	for (int i = 0; i < 3; ++i)
	{
		if (i) ImGui::SameLine();
		ImGui::ColorButton(labels[i], shades[i], ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGuiTheme::Em(1.5f), ImGui::GetFrameHeight()));
		ImGui::SameLine(0.f, ImGuiTheme::InnerGap());
		ImGui::TextDisabled("%s", labels[i]);
	}

	ImGui::SeparatorText("Preview");
	HavanaUtils::Label("Controls");
	ImGui::Button("Button");
	ImGui::SameLine();
	ImGui::BeginDisabled();
	ImGui::Button("Disabled");
	ImGui::EndDisabled();
	ImGui::SameLine();
	static bool previewChecked = true;
	ImGui::Checkbox("Enabled", &previewChecked);
	static float previewValue = 0.5f;
	HavanaUtils::Label("Value");
	ImGui::SliderFloat("##Value", &previewValue, 0.f, 1.f, "%.2f");

	ImGui::Separator();
	HavanaUtils::SkipLabel();
	if (ImGui::Button("Restore defaults"))
	{
		mode = ImGuiTheme::Mode::Dark;
		accent = ImGuiTheme::DefaultAccent();
		font = 0;
		fontSize = static_cast<int>(ImGuiRenderer::kMinFontSize);
		changed = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Changes save automatically.");
	if (changed)
	{
		config.SetPreference("Appearance.Theme", mode == ImGuiTheme::Mode::Light ? "Light" : "Dark");
		config.SetPreference("Appearance.Accent", json::array({accent.x, accent.y, accent.z}));
		config.SetPreference("Appearance.Font", std::string(kEditorFonts[font].Name));
		config.SetPreference("Appearance.FontSize", static_cast<float>(fontSize));
		m_appearancePending = true;
		m_appearanceUnsaved = true;
	}
	// Dragging the color picker or size slider changes the preference every frame; write once on release.
	if (m_appearanceUnsaved && !ImGui::IsAnyItemActive())
	{
		config.Save();
		m_appearanceUnsaved = false;
	}
}


void PreferencesWidget::DrawGeneral()
{
	EditorConfig& config = EditorConfig::GetInstance();
	float autosave = config.GetPreference<float>("Autosave.IntervalSeconds", 120.f);
	HavanaUtils::Label("Autosave interval (s)");
	if (ImGui::DragFloat("##Autosave", &autosave, 5.f, 0.f, 3600.f, autosave <= 0.f ? "Off" : "%.0f"))
	{
		config.SetPreference("Autosave.IntervalSeconds", std::max(autosave, 0.f));
	}
	ImGui::SetItemTooltip("Unsaved scenes are snapshotted to .tmp/Autosave for crash recovery. 0 disables.");

	std::string codeEditor = config.GetPreference<std::string>("CodeEditorCommand", "");
	HavanaUtils::Label("Code editor");
	if (ImGui::InputTextWithHint("##CodeEditor", "code -g \"{file}:{line}\"", &codeEditor))
	{
		config.SetPreference("CodeEditorCommand", codeEditor);
		PlatformUtils::SetCodeEditorCommand(codeEditor);
	}
	ImGui::SetItemTooltip("Command used to open file:line references from the console. {file} and {line} are substituted.");

	bool showPlanned = config.GetPreference<bool>("Editor.ShowPlannedTools", false);
	HavanaUtils::Label("Show planned tools");
	if (ImGui::Checkbox("##ShowPlanned", &showPlanned))
	{
		config.SetPreference("Editor.ShowPlannedTools", showPlanned);
	}
	ImGui::SetItemTooltip("Also show dimmed buttons for tools that aren't built yet (Terrain, MitchTree), with what they will do.");

	HavanaUtils::SkipLabel();
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
		HavanaUtils::Label(label);
		if (ImGui::Checkbox((std::string("##") + key).c_str(), &value))
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
	HavanaUtils::Label("Move increment (m)");
	if (ImGui::DragFloat("##SnapTranslate", &translate, 0.05f, 0.001f, 1000.f)) config.SetPreference("Snap.Translate", translate);
	HavanaUtils::Label("Rotate increment (deg)");
	if (ImGui::DragFloat("##SnapRotate", &rotate, 1.f, 0.1f, 180.f)) config.SetPreference("Snap.Rotate", rotate);
	HavanaUtils::Label("Scale increment");
	if (ImGui::DragFloat("##SnapScale", &scale, 0.01f, 0.001f, 10.f)) config.SetPreference("Snap.Scale", scale);
	ImGui::TextDisabled("Snap values apply the next time the scene view loads its settings.");

	ImGui::SeparatorText("Camera");
	EditorApp* app = static_cast<EditorApp*>(GetEngine().GetGame());
	if (app && app->EditorSceneManager)
	{
		EditorCameraController& camera = app->EditorSceneManager->GetCameraController();
		float speed = camera.GetFlySpeed();
		HavanaUtils::Label("Fly speed (m/s)");
		if (ImGui::DragFloat("##FlySpeed", &speed, 0.1f, 0.1f, 500.f))
		{
			camera.SetFlySpeed(speed);
		}
		HavanaUtils::Label("Look sensitivity");
		ImGui::DragFloat("##LookSensitivity", &camera.LookSensitivity, 0.01f, 0.01f, 2.f);
		HavanaUtils::Label("Focus smoothing");
		ImGui::DragFloat("##FocusSmoothing", &camera.FocusSmoothing, 0.1f, 1.f, 60.f);
	}
}


void PreferencesWidget::DrawShortcuts()
{
	EditorActions& actions = EditorActions::Get();
	ImGui::SetNextItemWidth(-1.f);
	ImGui::InputTextWithHint("##ShortcutFilter", "Filter actions", m_shortcutFilter, sizeof(m_shortcutFilter));

	if (!m_capturingAction.empty())
	{
		ImGui::TextColored(ImGuiTheme::AccentText(), "Press a key combination (Esc cancels, Backspace clears)...");
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
	ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(8.f));
	ImGui::TableSetupColumn("##Reset", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::ButtonWidth("Reset"));
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
		ImGui::AlignTextToFramePadding();
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
		if (ImGui::Button("Reset"))
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
	ImGui::SetNextWindowSize(ImVec2(ImGuiTheme::Em(31.f), ImGuiTheme::Em(31.f)), ImGuiCond_FirstUseEver);
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
		HavanaUtils::Label("Fixed update rate (Hz)");
		if (ImGui::DragFloat("##FixedRate", &fixedHz, 1.f, 10.f, 1000.f, "%.0f"))
		{
			engine.SetFixedTimeStep(1.f / std::clamp(fixedHz, 10.f, 1000.f));
		}
		float maxFps = engine.GetMaxFrameRate();
		HavanaUtils::Label("Frame rate cap");
		if (ImGui::DragFloat("##FrameRateCap", &maxFps, 1.f, 0.f, 1000.f, maxFps <= 0.f ? "Uncapped" : "%.0f fps"))
		{
			engine.SetMaxFrameRate(std::max(maxFps, 0.f));
		}
		HavanaUtils::SkipLabel();
		if (ImGui::Button("Save to Engine.cfg"))
		{
			engine.GetConfig().SaveProjectValues({ { "FixedTimeStep", engine.GetFixedTimeStep() }, { "MaxFrameRate", engine.GetMaxFrameRate() } });
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Hardware"))
	{
		DrawHardwareTab();
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Layers"))
	{
		ProjectSettings& settings = ProjectSettings::Get();
		bool changed = false;
		if (ImGui::BeginTable("##Layers", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(1.5f));
			ImGui::TableSetupColumn("Name");
			ImGui::TableHeadersRow();
			for (int i = 0; i < ProjectSettings::kLayerCount; ++i)
			{
				ImGui::PushID(i);
				std::string name = settings.GetLayerName(i);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%d", i);
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(-1.f);
				if (ImGui::InputTextWithHint("##LayerName", "(unnamed)", &name))
				{
					settings.SetLayerName(i, name);
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
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
		HavanaUtils::Label("Gravity");
		HavanaUtils::DragAxes("##Gravity", &settings.Gravity.x, 3, 0.05f, "%.2f");
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
		const float mapWidth = HavanaUtils::Label("Action map");
		ImGui::SetNextItemWidth(std::max(mapWidth - ImGuiTheme::ButtonWidth("Load") - ImGui::GetStyle().ItemSpacing.x, ImGuiTheme::Em(3.f)));
		ImGui::InputText("##ActionMap", &settings.InputActions);
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
					ImGui::TextColored(state.IsPressed() ? ImGuiTheme::SuccessText() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", action.Name.c_str());
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
			ImGui::PushID(bus);
			HavanaUtils::Label(AudioBusName(static_cast<AudioBus>(bus)));
			const bool moved = ImGui::SliderFloat("##Volume", &settings.BusVolumes[bus], 0.f, 1.f, "%.2f");
			ImGui::PopID();
			if (moved && audio)
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

	if (ImGui::BeginTabItem("Terrain"))
	{
		ProjectSettings& settings = ProjectSettings::Get();
		ImGui::TextWrapped("Shared streaming limits across terrain assets in each world. Terrain components retain their individual ceilings. Pending work and retiring consumers remain charged.");
		const auto budget = [&](const char* label, int& value, int maximum) {
			ImGui::SliderInt(label, &value, 1, maximum);
			if (ImGui::IsItemDeactivatedAfterEdit()) settings.Save();
		};
		budget("CPU MiB", settings.TerrainCpuBudgetMiB, 4096);
		budget("GPU MiB", settings.TerrainGpuBudgetMiB, 4096);
		budget("Collision MiB", settings.TerrainCollisionBudgetMiB, 4096);
		budget("Concurrent loads", settings.TerrainMaxLoads, 64);
		ImGui::TextWrapped("Required collision takes priority. Reducing a limit waits for pinned data to retire; existing coarse coverage must fit before detail can stream.");
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Navigation"))
	{
		// Area names show in area pickers; costs multiply the length of paths through an area.
		ProjectSettings& settings = ProjectSettings::Get();
		if (ImGui::BeginTable("NavAreas", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(1.5f));
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(6.5f));
			ImGui::TableHeadersRow();
			for (int area = 0; area < ProjectSettings::kNavAreaCount; ++area)
			{
				ImGui::PushID(area);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
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
		ImGui::SeparatorText("Agent Types");
		bool agentsChanged = false;
		int removeType = -1;
		if (ImGui::BeginTable("NavAgentTypes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Name");
			const float numberWidth = ImGuiTheme::Em(4.5f);
			ImGui::TableSetupColumn("Radius", ImGuiTableColumnFlags_WidthFixed, numberWidth);
			ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, numberWidth);
			ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, numberWidth);
			ImGui::TableSetupColumn("Slope", ImGuiTableColumnFlags_WidthFixed, numberWidth);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
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
				if (ImGui::Button("x", ImGuiTheme::IconButtonSize()))
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
