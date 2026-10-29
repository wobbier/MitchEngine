# Player Settings

The player's own choices (graphics quality, volumes, rebinds, language, accessibility, and whatever the game adds) live in one versioned JSON document, `user://Settings.json`. The engine reads it at boot, applies each value to the system it drives, and writes it back when something changes. C# reaches it through `Settings`.

> Verified against engine commit f4cd0707 + PLAY-2, 2026-10-29.

## Overview

- **One store, dotted keys.** `PlayerSettings` holds the document. Every value is addressed by a dotted key such as `Audio.Music` or `Graphics.Quality`. The same keys work in C#, in edscripts and in the file.
- **Typed known keys plus `Game.*`.** The engine's keys have a type and, for numbers, a range. A write of the wrong type is rejected, and an out-of-range number clamps. Anything under `Game.` is the game's own value and accepts any JSON.
- **Applied as they change.** Setting a key pushes it to its system at once: the quality preset to `HardwareBudget`, volumes to `AudioCore`, the language and text scale to `RmlUiCore`, and rebinds to the game input's `InputActionSystem`.
- **Saved when they settle.** A change marks the document dirty, and the next frame half a second after the last change writes it (a dragged slider writes once). `Settings.Save()` writes immediately, and shutdown flushes anything unsaved. Each write goes to a temporary file that is then renamed over the old one, so a crash mid-write leaves the previous settings intact.
- **Hermetic automation.** Captures and editor scripts never read or write the player's file: settings stay in memory unless the run is given `--user-dir <folder>`.

## Key Files

| Path | Role |
|------|------|
| `Source/Engine/PlayerSettings.h` / `.cpp` | The store: defaults, `Sanitize` (versions, types, ranges), get/set by key, apply, atomic save, rebinds |
| `Source/Engine/HardwareBudget.h` / `.cpp` | `QualityPreset`, `SetQualityPreset`, `GetEffectiveCaps` (preset caps combined with an enforced profile) |
| `Engine/Assets/Config/HardwareProfiles.json` | The built-in presets `Low`, `Medium`, `High`, `Ultra` under `QualityPresets` |
| `Source/Cores/AudioCore.h` / `.cpp` | `SetPlayerVolume`: the player's volume scales the project's bus mix |
| `Source/Scripting/Bindings/Systems/Settings.bindings.cpp` | The `Settings_*` ABI slots |
| `Modules/ScriptCore/Source/Core/Settings.cs` | C# `Settings` and the `AudioBus` enum |
| `Source/Engine/Engine.cpp` | `--user-dir`; `PlayerSettings::Init` after `InitGame`; per-frame `Update`; the flush at shutdown |
| `Modules/Havana/Source/Editor/EditorAutomation.cpp` | `settings-set`, `settings-assert`, `settings-save`, `settings-reload`, `settings-reset`, `assert-action` |
| `Tests/Source/PlayerSettingsTests.cpp` | Sanitizing, validation, save and fresh-load round trip, quality presets |

## How It Works

### The keys

| Key | Type | Drives |
|-----|------|--------|
| `Graphics.Quality` | string, `""` = no player caps | `HardwareBudget::SetQualityPreset` |
| `Graphics.RenderScale` | number 0.5..1, `1` | `HardwareBudget::SetPlayerGraphics`: a scale on every game camera's own render scale, under the quality preset's and an enforced profile's caps; editor views and pixel-perfect cameras keep theirs (ME-275) |
| `Graphics.DynamicResolution` | bool, `false` | `HardwareBudget::SetPlayerGraphics`: dynamic resolution on every game camera |
| `Graphics.Upscale` | string, `""` = each camera's | `HardwareBudget::SetPlayerGraphics`: `Bilinear`, `Nearest`, `Pixel` or `Sharp` replaces each camera's filter |
| `Audio.Master`, `Audio.Music`, `Audio.SFX`, `Audio.UI`, `Audio.Voice` | number 0..1 | `AudioCore::SetPlayerVolume` |
| `Language` | string, `""` = no table | `RmlUiCore::SetLanguage` (only when it changes: a switch reloads every document) |
| `Accessibility.TextScale` | number 0.5..3 | `RmlUiCore::SetTextScale` |
| `Accessibility.ReduceMotion` | bool | Nothing in the engine yet: for games to read (PLAY-7 wires it) |
| `Input.Bindings` | object, `Context/Action/Index[/Part]` → control path | `InputActionSystem::LoadOverrides` |
| `Game.<anything>` | any JSON, nested by its dots | Nothing: the game reads it |

Top-level `Version` (currently 1) is the document's schema version.

### Loading

`PlayerSettings::Init` runs at the end of `Engine::Init`, after `InitGame` has loaded the project's action map. In order:

1. **Pick a path.** `user://Settings.json`. Unattended runs (`AutomationRunner::IsUnattendedRun`) keep settings in memory only, unless `--user-dir` was given.
2. **Read and sanitize.** `Sanitize` reads the stored document over `Defaults()`:
   - A document older than this build runs through `kMigrations` (none yet).
   - A newer document is read for the keys this build knows, with a warning.
   - Unknown keys outside `Game` are dropped.
   - A wrong-typed value keeps its default, and numbers clamp.
   - Rebinds whose control path doesn't parse are dropped.
   - A file that isn't JSON reads as the defaults and logs an error.
3. **Apply.** Every key is pushed to its system.

A missing file reads as the defaults. The log says which: `Settings: user://Settings.json (defaults)`.

### Where user:// is

`user://` is the per-user root (see `Docs/VFS.md`): the OS pref dir in shipped builds and `.tmp/User` in the editor and dev runs. `--user-dir <folder>` re-points it for one run, in any build. It also opts an unattended run into loading and saving, which is how a pair of editor flows proves settings survive a restart.

### Quality presets

Presets are data in the profiles files (`QualityPresets`, engine file first, then the project's `Assets/Config/HardwareProfiles.json`):

- **Shape.** Each preset has a `Name`, a `Description` and `Caps` in the same shape as a hardware profile's.
- **Overrides.** A preset with an existing name replaces it, and `"Extends"` starts from an earlier preset.
- **Effective caps.** `GetEffectiveCaps` combines the enforced profile's caps (when a profile is enforced) with the preset's using `HardwareProfile::Combine`: a feature stays on only if both allow it, and a size cap takes the smaller nonzero limit. `ApplyCaps` hands the result to the renderer and the texture uploads.
- **Built-in levels:**

| Preset | Caps |
|--------|------|
| Low | 1024 textures and shadows; no AO, DoF, volumetric fog or TAA |
| Medium | 2048 textures and shadows; volumetric fog at most 16 steps |
| High | Shadows up to 4096; volumetric fog at most 64 steps |
| Ultra | None |

### Rebinds

- **Two ways in.** `PlayerSettings::Rebind` sets an override on the game input and keeps it in the document. Rebinds made directly on `InputActionSystem` (an interactive rebind, for instance) are picked up by `CaptureBindings` or by the next `Save`, which always reads the live overrides.
- **Survival.** Overrides survive map reloads (`SetMap`), so a game that loads another map in `OnInitialize` keeps the player's rebinds.
- **Player 0 only.** Only the game's main `Input` (player 0) is persisted. Local multiplayer players' maps are not.

## In documents

The stock options menu (`Engine/Assets/UI/Options/Options.rml`) and any other RmlUi document change settings with inline actions (`setting:`, `setting-step:`, `setting-toggle:`, `rebind:`, `reset-settings`) and show them with `data-setting-*` attributes; see `Docs/UI-RmlUi-and-ImGui.md`. `GetRevision()` counts changes, so documents only refresh when something changed.

## C#

```csharp
Settings.Quality = "Low";                        // one of Settings.QualityPresets, or ""
Settings.SetVolume(AudioBus.Music, 0.5f);
Settings.Language = "es";
Settings.TextScale = 1.25f;
Settings.ReduceMotion = true;
Settings.Rebind("Jump", 0, "Keyboard/J");        // binding 0 of Jump; part: "Up", "Negative"...
string prompt = Settings.GetBindingDisplay("Jump");
Settings.SetFloat("MouseSensitivity", 1.5f);     // stored as Game.MouseSensitivity
float sensitivity = Settings.GetFloat("MouseSensitivity", 1f);
Settings.Save();                                 // optional: changes save on their own
```

`GetJson` / `SetJson` reach any key as JSON text. A rejected write returns false and logs why.

## How to Extend

- **A new engine setting:**
  1. Add it to `PlayerSettings::Defaults()`; its type there is its type.
  2. If it's a number with limits, add a `RangeOf` case.
  3. Push it to its system in `ApplyKey`, and add it to `Apply` if it has one.
  4. Add a typed C# property in `Settings.cs`, and a row to the keys table above.
- **A schema change** (renaming or moving a key):
  1. Bump `kVersion`.
  2. Append a migration to `kMigrations`. The entry at index N - 1 turns a version N document into a version N + 1 one.
  3. Add a `Sanitize` test with an old document.
- **A project's own quality levels.** Add `QualityPresets` to the project's `Assets/Config/HardwareProfiles.json`. A preset with the same name replaces the engine's.

## Caveats & Fragility

- **Ranges are fixed.** `Sanitize` clamps to the ranges in `RangeOf`. Projects can't widen them.
- **`Language` isn't checked.** A code with no string table logs `[UI] No string table` and shows the raw keys.
- **An undefined preset caps nothing.** `Graphics.Quality` naming a preset that doesn't exist logs a warning. The name is kept, so a preset a later profiles reload adds takes effect then.
- **The editor shares the player's file.** In the editor, `.tmp/User/Settings.json` is the player's file too, so a quality level picked during Play stays applied after Stop.
- **The stock menu rebinds player 0 only.** Its Controls list is the game input's map; local multiplayer players have none.
- **Window settings live elsewhere.** The window mode, resolution and display stay in `EngineConfig`'s user state (`Docs/Platform-Window-Input-Config.md`).

## Related Docs

- `Docs/Hardware-Profiles.md`: profiles, budgets and the caps that presets reuse
- `Docs/Input.md`: actions, overrides and interactive rebinding
- `Docs/Audio.md`: buses and the project mix
- `Docs/UI-RmlUi-and-ImGui.md`: string tables and text scale
- `Docs/VFS.md`: `user://`
