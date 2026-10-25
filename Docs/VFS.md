# Virtual Filesystem (VFS)

Logical paths in, bytes or physical locations out. The VFS owns the question "where does
`Assets/Foo.png` actually live?" that `Path` used to answer with hardcoded roots.

Code: `Modules/Dementia/Source/VFS/` (`VFS.h`, `Mount.h`, `DirectoryMount.cpp`).
Tests: `Tests/Source/VfsTests.cpp`.

## Mounts

A mount is `{ prefix, source, priority, writable, display prefix }`:

- **Prefix** — the logical namespace it serves: `""` for the overlay roots, `user://` for
  per-user state. Scheme-shaped prefixes (`name://`) match exactly; `""` serves every
  scheme-less path.
- **Source** — an `IMountSource`: `DirectoryMount` (a directory on disk) or `PakMount`
  (one `.mpak`, read-only; VFS-3). Memory sources follow behind the same interface.
- **Priority** — higher wins. Mounts sharing a prefix overlay each other: resolution walks
  them highest-first and the first hit answers. A miss lands on the highest-priority mount
  that yields a physical location (paks serve bytes but host no files), which is where a
  write to that logical path would go.
- **Display prefix** — what `Path::GetLocalPath` shows (`"Engine/"` on the engine root), so
  local paths keep round-tripping through scene serialization unchanged.

The defaults reproduce the historical two-root resolver and are installed lazily on first use:

| Prefix | Source | Priority | Notes |
|---|---|---|---|
| `""` | `<cwd>` (the game root) | 100 | Writable; where misses land |
| `""` | `<cwd>/Engine` | 0 | The engine fallback; displays as `Engine/...` |
| `user://` | `<cwd>/.tmp/User` | 0 | Dev default; see below |

The mount table is an immutable snapshot swapped under a lock; `Resolve` copies the
snapshot pointer and walks it lock-free, so a remount never races loader threads mid-resolve.

## `user://`

The per-user writable root — player settings, save games, and the shipped build's
`Engine.cfg` user state (`EngineConfig::DefaultUserStatePath()` is `user://Engine.cfg`
outside the editor; the editor keeps `.tmp/Engine.user.cfg`).

Physical location:

- **Editor / tools / unattended runs** — `<cwd>/.tmp/User`, repo-local and gitignored, so
  dev machines and automation stay hermetic.
- **Shipped (attended) game runs** — `SDL_GetPrefPath( ProductOrg, ProductName )`
  (e.g. `~/.local/share/<Org>/<Name>/`), mounted in `Engine::Init`. The identity comes from
  `ProjectSettings` (`Assets/Config/ProjectSettings.json`, `Product` block); changing it
  orphans players' saved state. Unattended runs never call `SDL_GetPrefPath` (it creates the
  directory) and never read the player's `user://Engine.cfg`, keeping captures deterministic.

Writes through `File::Write` / `VFS::WriteText` create missing parent directories.

## Resolution (what `Path` does now)

`Path( logical )` normalizes slashes, then:

1. **Absolute paths** (leading `/`, or a drive `:` on Windows) bypass the mounts entirely.
2. **Scheme paths** (`user://...`) resolve through that scheme's mounts. An unknown scheme
   falls back to the `""` roots, like any relative path always has.
3. **Relative paths** walk the `""` mounts by priority: the game root, then the engine root.
4. **Legacy rewrite**: a missing `Engine/Assets/...` retries as `Assets/...`, so writes
   against engine content land in the project, not the engine.

`Raw = true` consults only the highest-priority matching mount (no fallback, no rewrite) —
it means "this literal location, relative to the game root".

Two deliberate changes from the legacy resolver, both only reachable with inputs that don't
occur in content: absolute paths that don't exist no longer fall back into the engine tree
(they stay as given), and `<junk>/Assets/...` relative paths no longer find the engine copy.
`VfsTests.cpp` holds a verbatim copy of the legacy algorithm and golden-tests the VFS against
it for every file in both `Assets` trees, in both spellings.

## Reads and writes

`File::Read` / `File::Write` go through `VFS::ReadText` / `VFS::WriteText`, and every runtime
loader reads through the same seam (VFS-2): textures, models (and their vertex caches),
shader binaries, terrain assets/heightmaps/sources, navmeshes, sequences, trees, shader
graphs, ImGui fonts, Ultralight's file system, and FMOD (system-level file callbacks pulling
chunks through `VfsFileStream`). `VfsFileStream` is the chunked-read surface for consumers
that stream instead of loading whole files; it reads a window of a physical file through
`IMountSource::GetStreamWindow` — the whole file on directory mounts, the entry's slice of
the archive on pak mounts — so positions are always entry-relative and FMOD streams straight
out of a pak. The deliberate exceptions — the terrain stream archive and texture cache
(random access over huge files) and editor/diagnostic I/O — are allowlisted with reasons in
`Engine/Tools/LintRuntimeIO.py`, which RunChecks and CI run to ban new raw `ifstream`/`fopen`
reads in runtime code.

`ResourceCache` keys on logical paths (`ResourceCache::CacheKey`: the local spelling trimmed
to the last `Assets`), so a logical request and any mount's resolution of it share one cache
entry — an override replaces the resource instead of duplicating it, and the FileWatcher's
physical change paths map back through the same normalization.

`VFS::Enumerate( dir, recursive, visitor )` merges entries across mounts: higher priority
wins duplicates, and the visit is sorted.

## Recording reads (`--record-deps`)

`VFS::SetRecordingReads( true )` makes every successful read through a mount (`ReadText`,
`ReadBytes`, `VfsFileStream::Open`) note the logical path it served, in the overlay spelling:
engine content read as `Engine/Assets/X` is noted as `Assets/X`, the path a cooked tree or pak
serves it under. Misses, existence probes, `user://` state and absolute paths outside the mounts
aren't noted. Any run takes `--record-deps <file>`: recording starts in `Engine::Init` and the
run writes `{ "Version": 1, "Reads": [...] }` when it exits (`Source/Tools/Cook/
CookDependencies.h`). The cooker ships what a recorded run read, which catches the assets
scripts and engine code load by string paths no scene names. Models read their `.assbin` cook
through the VFS too (`ReadFileFromMemory`), so a cook served by a pak loads.

## Paks (`.mpak`, VFS-3)

One file carrying a logical tree: a header, raw entry blobs, then the entry table (paths are
forward-slash, case-preserved). `Mpak::Write` streams entries from a provider;
`Mpak::WriteFromVfs( pak, roots )` packs what the VFS serves under each logical root — so the
game-over-engine overlay bakes in at pack time — and the `pack-assets out.mpak | root,root`
edscript command drives it from a flow or a headless editor run. `PakMount::Open` parses the
table; every read opens its own window on the archive, so loader threads never share stream
state. `VFS::MountPak( prefix, pak, priority )` mounts one, and the game mounts `--pak
<file>` at priority 150 before project settings load, so a working directory with no
`Assets/` still serves every logical path (`VfsTests.cpp` round-trips bytes, stats,
enumeration and windowed streams; a packaged Lighting run reproduces the showcase reference).
Dependency-aware packing is the cooker's job (SHIP-2); until then callers name their roots,
and engine defaults like `Assets/Textures/DefaultAlpha.png` need naming explicitly.

## Roadmap

- **VFS-3 leftovers** — retail reads only paks, mods and `user://`; the terrain stream
  archive and texture cache adopt `GetStreamWindow` (they still random-access physical
  files, allowlisted in the lint); per-entry compression flags.
- **Mods** mount at priority 200 + load order (MOD-1).
