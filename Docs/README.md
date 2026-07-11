# MitchEngine Documentation

Technical documentation for MitchEngine — a C++20, ECS-based game engine built on BGFX, SDL2, Bullet3D, FMOD, and .NET 8 scripting, with a visual editor called Havana. The engine lives in this repository and is consumed as a git submodule (`Engine/`) by game projects such as Drumsmith and MitchGame.

## Doc Map

| Doc | What it covers |
|-----|----------------|
| [Architecture.md](Architecture.md) | Module layout, `Engine` singleton lifecycle, the verified frame loop, engine-owned vs scene-loaded cores, feature flags |
| [ECS.md](ECS.md) | `EntityID` packing, component storage, `ComponentFilter`, `World` caches and `Simulate()`, registration macros, add-a-component / add-a-core recipes |
| [Jobs-and-Events.md](Jobs-and-Events.md) | `SimpleJobSystem`, `Burst` chunking, main-thread work stealing, `EventManager` — **the threading rulebook** |
| [Rendering-Pipeline.md](Rendering-Pipeline.md) | `RenderCore` parallel command build → `BGFXRenderer` submit, view-ID map (editor vs game), instancing, transparency, lighting reality |
| [Materials-and-Shaders.md](Materials-and-Shaders.md) | Material hierarchy, instance batch keys, offline `shaderc` cooking via the MetaFile system |
| [Resources-and-Assets.md](Resources-and-Assets.md) | `ResourceCache` load flow, `.meta` sidecars, export/cooking, eviction, resource type inventory |
| [Cores-and-Components-Reference.md](Cores-and-Components-Reference.md) | Catalog of every core (incl. Physics/Bullet and Audio/FMOD deep-dives) and every component; orphaned-component list |
| [Serialization-and-Scenes.md](Serialization-and-Scenes.md) | `.lvl` JSON format, prefabs, registry-name instantiation, save/load flow, versioning caveats |
| [Scripting-DotNet.md](Scripting-DotNet.md) | hostfxr bootstrap, `ScriptEngine`, the function-pointer API table, C# side, platform status, add-a-binding recipe |
| [UI-Ultralight-and-ImGui.md](UI-Ultralight-and-ImGui.md) | Ultralight HTML UI integration, GPU driver, view composite — **deprecation status: do not extend** |
| [Editor-Havana.md](Editor-Havana.md) | `EditorApp`/`Havana`, widgets, command/undo system, entity picking, play-in-editor lifecycle |
| [Platform-Window-Input-Config.md](Platform-Window-Input-Config.md) | `IWindow`/`SDLWindow`, dual input contexts, `EngineConfig` persistence — the OS boundary |
| [Build-System.md](Build-System.md) | Sharpmake project graph, target matrix, ThirdParty-existence feature defines, flake.nix, tools |
| [State-of-the-Engine.md](State-of-the-Engine.md) | **Opinions live here**: maturity scorecard, platform matrix, half-finished inventory, improvement themes, priorities |

**Suggested reading order:** start with [Architecture.md](Architecture.md), then [ECS.md](ECS.md) and [Jobs-and-Events.md](Jobs-and-Events.md) — nearly everything else builds on those three. [State-of-the-Engine.md](State-of-the-Engine.md) is the executive summary of where things stand.

## Conventions

These rules apply to every doc in this directory:

1. **Facts vs opinions.** Docs 2–14 are factual: what the code does today, including its limits and sharp edges (each doc's *Caveats & Fragility* section). All opinionated assessment — what should change, priorities, maturity ratings — lives **only** in `State-of-the-Engine.md`.
2. **File references** are backticked repo-relative paths from the engine root, e.g. `Source/Engine/Engine.cpp`. Files living in the *game* repo (which embeds this engine at `Engine/`) are referenced with a `../` prefix, e.g. `../flake.nix`. A checked reference must contain at least one `/` — bare backticked filenames (`imgui.h`) are just names, not checked references. This makes stale references scriptable to detect:
   ```bash
   # run from the engine root
   grep -rhoE '`[A-Za-z0-9_. -]+(/[A-Za-z0-9_. -]+)+\.(h|cpp|cs|md|json|nix|lvl)`' Docs/ | tr -d '`' | sort -u \
     | while read -r p; do [ -e "$p" ] || echo "MISSING: $p"; done
   ```
3. **Cite symbols, not line numbers.** Behavioral claims name a function, class, or constant (`kClearView`, `World::Simulate`) so they survive edits and stay greppable.
4. **Diagrams** are Mermaid, at most ~2 per doc, restricted to GitHub-supported types (`flowchart`, `sequenceDiagram`, `classDiagram`, `stateDiagram-v2`, `quadrantChart`), no `%%{init}%%` directives. After editing a diagram, validate that every block still renders:
   ```bash
   # run from the engine root; a parse error prints FAIL for that block
   tmp=$(mktemp -d)
   awk -v dir="$tmp" '/^```mermaid/{n++; f=sprintf("%s/d%02d.mmd", dir, n); next} /^```$/{f=""} f{print > f}' Docs/*.md
   for f in "$tmp"/d*.mmd; do
     nix run nixpkgs#mermaid-cli -- -i "$f" -o "${f%.mmd}.svg" --quiet >/dev/null 2>&1 || echo "FAIL: $f"
   done
   ```
   Known label traps (both hit during validation): `stateDiagram-v2` transition labels cannot contain `::` (write `World Start`, not `World::Start`), and `sequenceDiagram` treats every `;` as a statement separator — including the one terminating an HTML entity like `&lt;` — so keep entities and semicolons out of message text.
5. **Verification stamp.** Each doc opens with `> Verified against engine commit <hash>, <date>.` If you change engine behavior a doc describes, **update the doc and its stamp in the same change** — source wins over docs on conflict, but a known-stale doc is worse than none.
6. **Template** (docs 2–14): Summary → Overview → Key Files → How It Works → How to Extend (where applicable) → Caveats & Fragility → Related Docs.
