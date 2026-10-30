#!/usr/bin/env python3
"""Bans raw read-side file I/O (std::ifstream, std::fstream, fopen) in runtime code.

Runtime reads go through the VFS (Engine/Docs/VFS.md), so overlays, user:// and later pak
mounts apply everywhere. Writers (std::ofstream) and the allowlisted files below stay raw.
Run from the project root (RunChecks does): exits 1 listing any new offender.
"""
import os
import re
import sys

SCAN_DIRS = [
    "Engine/Source",
    "Engine/Modules/Moonlight/Source",
    "Engine/Modules/Dementia/Source",
]

# file -> why raw I/O is allowed there. Keep reasons honest; new entries need one.
ALLOWED = {
    "Engine/Modules/Dementia/Source/VFS/VFS.h": "the VFS itself (VfsFileStream backend)",
    "Engine/Modules/Dementia/Source/VFS/VFS.cpp": "the VFS itself (physical backend)",
    "Engine/Modules/Dementia/Source/VFS/DirectoryMount.cpp": "the VFS itself (physical backend)",
    "Engine/Modules/Dementia/Source/VFS/Pak.cpp": "the VFS itself (.mpak writer/reader)",
    "Engine/Source/Terrain/TerrainStreamArchive.cpp": "random-access streaming archive; moves behind the VFS stream source with VFS-3",
    "Engine/Modules/Moonlight/Source/Graphics/TerrainTextureCache.cpp": "chunked fingerprints over huge sources + derived cache; VFS-3 stream source",
    "Engine/Source/Terrain/TerrainRecovery.cpp": "editor recovery journal (derived state, write/read-back)",
    "Engine/Source/Terrain/TerrainFileOperations.cpp": "editor terrain document I/O",
    "Engine/Modules/Moonlight/Source/Graphics/ShaderDependencies.cpp": "compiler depfiles (derived build artifacts, editor hot reload)",
    "Engine/Modules/Dementia/Source/Utils/PlatformUtils.cpp": "platform glue, not game content",
    "Engine/Modules/Dementia/Source/Resource/AssetDatabase.cpp": "editor/tools asset database caches",
    "Engine/Modules/Dementia/Source/Core/StackTrace.cpp": "crash diagnostics",
    "Engine/Source/Tools/Build/BuildGame.cpp": "Build Game reads build-machine files (the solution makefile, the build's own runtimeconfig), not game content",
    "Engine/Source/Tools/Projects/ProjectLibrary.cpp": "the Project Hub reads other projects, templates and the per-user recent list, outside the open project's mounts",
    "Engine/Source/Mods/Mods.cpp": "MOD-1 reads each mod folder's mod.json to decide what to mount, before the mod's mount exists",
    "Engine/Modules/Dementia/Source/Core/CrashHandler.cpp": "crash diagnostics: the crash directory's acknowledgement marker, outside any mount",
}

TOKEN = re.compile(r"std::ifstream|std::fstream|\bfopen\s*\(")


def main():
    offenders = []
    for scan in SCAN_DIRS:
        for root, _, files in os.walk(scan):
            for name in files:
                if not name.endswith((".cpp", ".h", ".hpp")):
                    continue
                path = os.path.join(root, name).replace(os.sep, "/")
                if path in ALLOWED:
                    continue
                with open(path, encoding="utf-8", errors="replace") as handle:
                    for number, line in enumerate(handle, 1):
                        stripped = line.lstrip()
                        if stripped.startswith("//"):
                            continue
                        if TOKEN.search(line):
                            offenders.append(f"{path}:{number}: {stripped.rstrip()}")
    if offenders:
        print("Raw read-side file I/O in runtime code (route it through the VFS, or allowlist it")
        print("with a reason in Engine/Tools/LintRuntimeIO.py):")
        for offender in offenders:
            print("  " + offender)
        return 1
    print(f"Runtime I/O lint: clean ({len(ALLOWED)} allowlisted files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
