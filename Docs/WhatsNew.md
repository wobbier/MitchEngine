# What's New

> Shown by Help > What's New, and once by itself whenever this page changes (ED-3). Newest first.

## Late October 2026: the gameplay kit

- Make your first game: two tutorials, Coin Hunt (3D) and Coin Hop (2D), built from the templates; every snippet is tested.
- A C# starter kit: health and damage, an inventory, pickups, doors that open with a key, damage zones, checkpoints, spawners and an interaction prompt. The First-Person template uses it.
- Camera rigs: virtual cameras (fixed, follow, orbit, 2D platformer) that a camera brain blends between, plus camera shake.
- AI: behaviour trees, sight and hearing, and a guard that patrols, chases and searches.
- Dialogue: Yarn-style .dialogue files with choices and variables, a stock dialogue box, localized.
- Splines: curves you draw in the World view, for paths, rails and patrols (C# Spline).
- Sequencer: event markers that call your scripts, camera cuts, audio and animation cues, copy/paste/duplicate keys, mute and lock.
- Ragdolls: build one from a skeleton in one click, knock a character down, and it blends back up.
- Play-mode tests: [GameTest] methods that load a level, play it and check the result (game-tests in the console), with simulated input: hold or tap keys, or replay a recording.
- Accessibility: subtitles and sound captions, speaker names, subtitle size and backdrop, high contrast, toggle instead of hold (sprint, crouch), and reduce motion turns off camera shake.
- Multiplayer: a dedicated server; C# replicated fields, predicted fields and RPCs; lag-compensated hits from C# (the server judges a shot the way the shooter saw it), with the player's aim sent in every command.
- Mods: content and script mods loaded from a folder, with a Mods page in the options menu.
- Play options: Simulate (play without scripts), keep a component's values after Stop, play from the scene camera, and Build & Run.
- Prefabs: double-click one to edit it alone (prefab mode), and variants that keep only their differences from a base prefab and follow its changes.
- World view: Drop to Surface (End), Shift-drag to slide objects over surfaces, V-drag to snap to vertices, a Measure tool, camera bookmarks, and grid plane, size and offset.
- Budgets: the Resource Monitor shows what each loaded asset costs in memory, biggest first, against the target hardware; and a host speed factor so a fast dev machine reads like the target.
- Rhythm games: a beat clock that follows the music's real playback position (C# BeatClock).
- Every public C# API member is documented, with a reference page (Docs/API/ScriptCore.md) and IDE tooltips.
- Editor: inspector search and lock, lock picking in the hierarchy; the command palette finds entities, assets, settings and docs; a status indicator for builds, imports and bakes, with toasts that link to the fix; undo for asset renames, moves and deletes (with a Trash), Project Settings and Prefab Apply.

## October 2026: shipping a game in C#

- Player settings: graphics quality presets, volumes, rebinding, language and accessibility, saved per player (C# Settings).
- A stock options menu every template opens with Escape / Start, built from RmlUi settings actions, gamepad-navigable.
- Save games: named slots with thumbnails and playtime, [Save] script fields, atomic background writes (C# SaveGame).
- Loading screens with real progress for background scene loads, and boot splash screens.
- Localization: string tables, a pseudo-locale that shows what isn't translated, and Window > Localization.
- C#: an event bus, await on the game clock, coroutines, timers and tweens, data assets, and a much wider API (queries, physics casts, 2D, animator, particles, audio, cameras, math, a seeded random generator).
- Render textures: cameras that render onto materials (security cameras, mirrors, screens).
- Editor: named workspaces (Scene, Animation, Shader, UI, Debug), View > Reset Layout, preferences that follow you across projects, and this page and the tour (Help > Take the Tour).

## Earlier this year

- Project Hub, templates (First-Person, Third-Person, Top-Down) and C#-only projects on the Player.
- Build Game: one command to a shippable folder with its own .NET.
- RmlUi game UI with C# view models, hot reload and gamepad navigation.
