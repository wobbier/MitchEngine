# Modding

> Status: MOD-1 (manifest, discovery, resolver, VFS layers) and MOD-3 (net-required mods in the handshake), 2026-10-30. Per-mod C# assemblies are MOD-2, the in-game manager MOD-6.

## Overview

A mod is a folder with a `mod.json`. Its files mount over the game's as a VFS layer, so a mod's `Assets/Textures/Sprites/Crate.png` replaces the game's for every logical path that names it. Nothing is copied or patched.

**Mods are trusted.** They run with the same rights as the game, and MOD-2's assemblies get full .NET. Only install mods you trust. MOD-7 is an optional load-time scanner for Workshop builds.

## Key Files

| File | What |
|---|---|
| `Source/Mods/ModManifest.h` | `mod.json`, `SemVer`, `VersionRange` |
| `Source/Mods/ModResolver.h` | `ModSettings` (the player's order and disabled list) and `ModResolver::Resolve` |
| `Source/Mods/Mods.h` | `ModManager`: discovery, resolving, mounting (`Apply`) |
| `Source/Engine/EngineVersion.h` | `kEngineVersion`, which a mod's `"engine"` range is checked against |
| `Tests/Source/ModResolverTests.cpp` | Ranges, manifests, ordering, cycles, dependencies, VFS layering |
| `Tests/Mods/BlueCrates/` | A texture-override test mod. RunChecks' "Mods" step captures Physics2D with it (`Tests/Mods/Reference/`) |

## mod.json

```json
{ "id": "com.example.tinted-crates", "name": "Tinted Crates", "version": "1.2.0",
  "engine": ">=0.1 <1", "dependencies": { "com.example.core": "^1.0" },
  "loadAfter": [ "com.example.textures" ], "loadBefore": [], "assemblies": [ "TintedCrates.dll" ],
  "netRequired": false }
```

- **`id`:** required, with no spaces or slashes.
- **Versions:** `1`, `1.2` or `1.2.3`; pre-release and build suffixes are ignored.
- **Ranges:** space-separated comparators that must all hold:
  - `*` matches anything;
  - `>=1.2`, `>1.2`, `<2`, `<=2.1` and `=1.2.3` compare;
  - `1.2` means `>=1.2.0 <1.3.0`;
  - `^1.2.3` keeps the same major (for `0.x`, the same minor);
  - `~1.2` keeps the same minor.

## How It Works

- **Discovery:** every folder with a `mod.json` directly under:
  - `<project>/Mods`;
  - `user://Mods`;
  - any `--mod-dir <folder>`, searched first.

  Folders are taken in name order, and the first of two mods with one id wins.
- **The player's choices:** `user://Config/Mods.json`, `{ "Order": [ ids ], "Disabled": [ ids ] }`. A mod the file doesn't name is enabled and goes after the named ones.
- **Resolving** (`ModResolver::Resolve`):
  - **What's dropped:** disabled mods, mods whose `engine` range doesn't hold `kEngineVersion`, and mods with a dependency that's missing, disabled, dropped or outside its range. A drop cascades to whatever depended on the dropped mod.
  - **Ordering:** dependencies and `loadAfter` targets come first, and `loadBefore` targets later. Ties follow the player's order. Mods caught in a cycle, or depending on one, are dropped.
  - **Reporting:** every drop is a `Mods:` warning naming the mod and the reason.
- **Mounting:** each loaded mod's folder mounts read-only at the VFS root with priority 200 + its place in the order. That's above the game (100) and a pak (150), so later mods win over earlier ones. Writes still go to the game and `user://`.
- **When:** game builds apply mods in `Engine::Init` once `user://` is known; `--no-mods` skips them. The editor applies them only with `--mods`, so it edits the game's own files.

## Caveats & Fragility

- **Only folder mods so far.** Zip and `.mpak` mods (the design's other forms) aren't discovered yet.
- **`assemblies` is only read** (MOD-2 loads them).
- **`netRequired` (MOD-3):** the loaded mods marked `netRequired` go into the session handshake as `id@version` (`NetVersion::Mods`; `ModSet` hashes them). A client whose set differs is turned away with the differences, e.g. `version mismatch: mods (the server needs com.example.weapons@2.0.0; you also have com.example.hats@1.0.0)`. Cosmetic mods leave the flag off and can differ between players.
- **Cooked side files:** an override texture's `.dds` and `.meta` are written next to it in the mod folder, the first time it's cooked.
- **`kEngineVersion` is set by hand** (0.1.0) until ME-267 stamps builds.

## Related Docs

- [VFS.md](VFS.md): mounts and priorities.
