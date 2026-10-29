# Localization

String tables per language, looked up by key, behind RmlUi documents, C# (`Loc`) and engine-generated text, with a pseudo-locale and a missing-string report (PLAY-6).

> Verified against engine PLAY-6, 2026-10-29.

## Overview

- **Tables.** A language's table merges these, later keys winning:
  1. `Engine/Assets/Localization/<code>.json` (the stock menus' strings)
  2. the project's `Assets/Localization/<code>.json`
  3. `Assets/UI/Strings/<code>.json` (UI-4's location, still read)

  Each is a flat `{ "key": "text" }` object; `"language.name"` names the language in pickers.
- **Keys are the source text,** usually the English, so documents need no ids. RmlUi asks `RmlSystemInterface::TranslateString` for every text node, and a text node whose trimmed text is a key renders the translation. The surrounding whitespace is kept.
- **The language** is the player setting `Language` (`Docs/Player-Settings.md`). Setting it reloads the documents. `""` shows text as written.
- **The pseudo-locale `qps`** shows every string the English table knows, accented, bracketed and padded about 30% longer: `[Ɓáçķ ~~]`. Text that stays plain under it never went through a table, and the padding shows layouts that won't fit longer languages.
- **Missing strings.** While a language is active, prose looked up with no translation is recorded (`Localization::GetMissing`): the editor lists it, and flows assert it. Data isn't prose:
  - numbers and data bindings (`{{ score }}`);
  - markup;
  - anything marked with `MarkData`: key names, values and language names the engine generates.
- **Generated text** (`RmlSettings`: the binding prompts, part labels, On/Off, Default) goes through `Localization::Get`.

## Key Files

| Path | Role |
|------|------|
| `Source/Engine/Localization.h` / `.cpp` | Tables, the language, `Get` / `Translate`, the pseudo-locale, missing strings, saving a project table |
| `Source/UI/Rml/RmlInterfaces.cpp` | `TranslateString` → `Localization::Translate` |
| `Source/Cores/UI/RmlUiCore.cpp` | `SetLanguage`: sets the language and reloads the documents |
| `Engine/Assets/Localization/en.json` | The stock menus' English |
| `Templates/*/Assets/Localization/en.json` | The template actions' names (a shared template file) |
| `Modules/ScriptCore/Source/Core/Loc.cs` | C# `Loc.Get`, `Loc.Language`, `Loc.Languages` |
| `Modules/Havana/Source/Widgets/LocalizationWidget.cpp` | The editor's Localization window |
| `Tests/Source/LocalizationTests.cpp` | Merging, translation, missing strings, the pseudo-locale |

## The editor

**Window > Localization** shows every key of the English table beside one language's text:
- **Editing:** empty cells are untranslated and highlighted.
- **Save** writes the project's `Assets/Localization/<code>.json` with only what differs from the engine's table.
- **Preview**, **Pseudo** and **Off** switch the player's language.
- **Missing** lists what was shown untranslated since the language was set. **Add** puts it in the table.

## Proof

- `LocalizationTests.cpp`.
- `LocalizationFlows.edscript`: the stock options menu under `qps` reports nothing untranslated on any tab, with captures. The `loc-assert-missing` and `loc-clear-missing` commands support it.
- `UINavFlows.edscript`: Spanish, through the UI-4 path.

## Caveats & Fragility

- **Keys are exact text.** An edited English string orphans its translations. A key-id scheme (`menu.back`) works too, but the documents must then use the ids.
- **Data bindings aren't translated:** `{{ value }}` text skips the hook. Translate values before handing them to a model (C# `Loc.Get`).
- **Fallback chains** are UI-4's: every face under `Assets/Fonts/Fallback` covers missing glyphs. No CJK font ships with the engine; add one there for CJK languages.
- **The missing report needs the text shown.** A hidden document that never loaded reports nothing.

## Related Docs

- `Docs/UI-RmlUi-and-ImGui.md`: documents and the stock menus
- `Docs/Player-Settings.md`: the `Language` setting
