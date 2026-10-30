#!/usr/bin/env python3
"""Generates both halves of the script ABI, both directions.

From Source/Scripting/ScriptAPI.def (the engine API C# calls):
  Source/Scripting/Generated/ScriptEngineAPI.generated.h      C++ struct ScriptEngineAPI
  Modules/ScriptCore/Source/Generated/EngineAPIBindings.g.cs   C# struct EngineAPIBindings
From Source/Scripting/ScriptBridge.def (the managed entry points C++ calls; ME-299):
  Source/Scripting/Generated/ScriptBridgeAPI.generated.h      C++ struct ScriptBridgeAPI
  Modules/ScriptCore/Source/Generated/ScriptBridgeAPI.g.cs     C# struct + ScriptBridge.GetBridge

  Engine/Tools/GenerateScriptAPI.py           # write both files
  Engine/Tools/GenerateScriptAPI.py --check   # exit 1 if either is out of date
"""
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ENGINE, "Source/Scripting/ScriptAPI.def")
CPP_OUT = os.path.join(ENGINE, "Source/Scripting/Generated/ScriptEngineAPI.generated.h")
CS_OUT = os.path.join(ENGINE, "Modules/ScriptCore/Source/Generated/EngineAPIBindings.g.cs")
BRIDGE_MANIFEST = os.path.join(ENGINE, "Source/Scripting/ScriptBridge.def")
BRIDGE_CPP_OUT = os.path.join(ENGINE, "Source/Scripting/Generated/ScriptBridgeAPI.generated.h")
BRIDGE_CS_OUT = os.path.join(ENGINE, "Modules/ScriptCore/Source/Generated/ScriptBridgeAPI.g.cs")

# manifest type -> (C++ type, C# type)
TYPES = {
    "void": ("void", "void"),
    "bool": ("bool", "byte"),
    "int": ("int", "int"),
    "float": ("float", "float"),
    "double": ("double", "double"),
    "cstr": ("const uint8_t*", "byte*"),
    "Entity": ("EntityID", "Entity"),
    "Entity*": ("EntityID*", "Entity*"),
    "Vector2*": ("Vector2*", "Vector2*"),
    "Vector3*": ("Vector3*", "Vector3*"),
    "const Vector3*": ("const Vector3*", "Vector3*"),
    "Vector4*": ("Vector4*", "Vector4*"),
    "float*": ("float*", "float*"),
    "double*": ("double*", "double*"),
    "int*": ("int*", "int*"),
    "bool*": ("uint8_t*", "byte*"),
    "byte*": ("uint8_t*", "byte*"),
    "RaycastHit*": ("ScriptRaycastHit*", "RaycastHit*"),
    "ulong*": ("uint64_t*", "ulong*"),
    "EngineAPI*": ("const ScriptEngineAPI*", "EngineAPIBindings*"),
}

# Structs never come back by value: MSVC x64 returns a type with a constructor (EntityID) through a
# hidden pointer while .NET expects it in a register. Results that aren't scalars go through out
# pointers (ME-298).
RETURN_TYPES = {"void", "bool", "int", "float", "double"}

# The structs that cross, by size (and the raycast hit's field offsets): static_asserts in C++, and
# constants C# checks when the API is bound, so a layout change on either side fails loudly.
LAYOUT = [
    # (C++ type, C# type, size, crosses by value: then it must also be trivially copyable)
    ("EntityID", "Entity", 8, True),
    ("Vector2", "Vector2", 8, False),
    ("Vector3", "Vector3", 12, False),
    ("Vector4", "Vector4", 16, False),
    ("ScriptRaycastHit", "RaycastHit", 36, False),
]

LINE = re.compile(r"^(?P<ret>[\w*]+)\s+(?P<name>\w+)\s*\((?P<args>.*)\)\s*$")


def parse(manifest=MANIFEST):
    MANIFEST = manifest
    sections = []
    names = set()
    with open(manifest, encoding="utf-8") as f:
        for number, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or (line.startswith("#") and not line.startswith("##")):
                continue
            if line.startswith("##"):
                sections.append((line[2:].strip(), []))
                continue
            match = LINE.match(line)
            if not match:
                sys.exit(f"{MANIFEST}:{number}: can't parse '{line}'")
            ret, name, args = match["ret"], match["name"], match["args"].strip()
            if ret not in TYPES:
                sys.exit(f"{MANIFEST}:{number}: unknown return type '{ret}'")
            if ret not in RETURN_TYPES:
                sys.exit(f"{MANIFEST}:{number}: '{ret}' can't be a return type (structs cross by value only as arguments; return through an out pointer, e.g. 'void {name}( ..., {ret}* out )')")
            if name in names:
                sys.exit(f"{MANIFEST}:{number}: '{name}' is declared twice")
            names.add(name)
            params = []
            if args:
                for arg in args.split(","):
                    parts = arg.strip().rsplit(" ", 1)
                    if len(parts) != 2 or parts[0].strip() not in TYPES:
                        sys.exit(f"{MANIFEST}:{number}: bad parameter '{arg.strip()}'")
                    params.append((parts[0].strip(), parts[1]))
            if not sections:
                sections.append(("", []))
            sections[-1][1].append((ret, name, params))
    return sections


def abi_hash(sections):
    """FNV-1a 64 over every slot's signature, in order: both sides compare it when scripting starts."""
    text = "\n".join(f"{ret} {name}({','.join(t for t, _ in params)})" for _, functions in sections for ret, name, params in functions)
    value = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def cpp(sections):
    out = [
        "// Generated by Engine/Tools/GenerateScriptAPI.py from Source/Scripting/ScriptAPI.def - edit the manifest, not this file.",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <type_traits>",
        "",
        '#include "ECS/EntityID.h"',
        '#include "Math/Vector2.h"',
        '#include "Math/Vector3.h"',
        '#include "Math/Vector4.h"',
        "",
        "// A physics query result as scripts see it (matches ScriptCore's RaycastHit).",
        "struct ScriptRaycastHit",
        "{",
        "    EntityID Entity;",
        "    Vector3 Point;",
        "    Vector3 Normal;",
        "    float Distance = 0.f;",
        "};",
        "",
        "// The ABI both sides were generated from (C# checks it: ScriptBridge.GetEngineAPIHash).",
        f"constexpr uint64_t kScriptEngineAPIHash = 0x{abi_hash(sections):016X}ULL;",
        "",
        "// Structs that cross: their layout is part of the ABI (C# checks the same sizes).",
    ]
    for native, _, size, by_value in LAYOUT:
        out.append(f'static_assert( sizeof( {native} ) == {size}, "{native} crosses into C#: its size is part of the script ABI" );')
        if by_value:
            out.append(f'static_assert( std::is_trivially_copyable_v<{native}>, "{native} crosses into C# by value: it must be trivially copyable" );')
    out += [
        'static_assert( offsetof( ScriptRaycastHit, Point ) == 8 && offsetof( ScriptRaycastHit, Normal ) == 20 && offsetof( ScriptRaycastHit, Distance ) == 32, "ScriptRaycastHit must match ScriptCore\'s RaycastHit" );',
        "",
        "struct ScriptEngineAPI",
        "{",
    ]
    first = True
    for title, functions in sections:
        if not first:
            out.append("")
        first = False
        if title:
            out.append(f"    // {title}")
        for ret, name, params in functions:
            args = ", ".join(f"{TYPES[t][0]} {n}" for t, n in params)
            # noexcept is part of the pointer type: every binding must be noexcept, so a C++ exception
            # can't unwind into the managed frames that called it (ME-301; it terminates here instead).
            out.append(f"    {TYPES[ret][0]} ( *{name} )( {args} ) noexcept;" if args else f"    {TYPES[ret][0]} ( *{name} )() noexcept;")
    out += ["};", ""]
    return "\n".join(out)


def cs(sections):
    out = [
        "// Generated by Engine/Tools/GenerateScriptAPI.py from Engine/Source/Scripting/ScriptAPI.def - edit the manifest, not this file.",
        "// Interop plumbing, documented in the manifest: no XML docs here (CS-10).",
        "#pragma warning disable CS1591",
        "using System.Runtime.InteropServices;",
        "",
        "namespace ScriptCore;",
        "",
        "[StructLayout(LayoutKind.Sequential)]",
        "public unsafe struct EngineAPIBindings",
        "{",
        "    // The ABI both sides were generated from (C++: kScriptEngineAPIHash).",
        f"    public const ulong AbiHash = 0x{abi_hash(sections):016X}UL;",
        "",
    ]
    first = True
    for title, functions in sections:
        if not first:
            out.append("")
        first = False
        if title:
            out.append(f"    // {title}")
        for ret, name, params in functions:
            types = [TYPES[t][1] for t, _ in params] + [TYPES[ret][1]]
            out.append(f"    public delegate* unmanaged<{', '.join(types)}> {name};")
    out += ["}", "", "// The sizes C++ static_asserts for the structs that cross (checked when the API is bound).", "public static class ScriptAbiLayout", "{"]
    for _, managed, size, _ in LAYOUT:
        out.append(f"    public const int {managed}Size = {size};")
    out += ["}", ""]
    return "\n".join(out)


def bridge_cpp(sections):
    out = [
        "// Generated by Engine/Tools/GenerateScriptAPI.py from Source/Scripting/ScriptBridge.def - edit the manifest, not this file.",
        "#pragma once",
        "#include <cstdint>",
        "",
        '#include "ScriptEngineAPI.generated.h"',
        "",
        "// The managed entry points, filled in one call by ScriptCore's ScriptBridge.GetBridge (ME-299).",
        f"constexpr uint64_t kScriptBridgeAPIHash = 0x{abi_hash(sections):016X}ULL;",
        "",
        "struct ScriptBridgeAPI",
        "{",
    ]
    first = True
    for title, functions in sections:
        if not first:
            out.append("")
        first = False
        if title:
            out.append(f"    // {title}")
        for ret, name, params in functions:
            args = ", ".join(f"{TYPES[t][0]} {n}" for t, n in params)
            out.append(f"    {TYPES[ret][0]} ( *{name} )( {args} ) = nullptr;" if args else f"    {TYPES[ret][0]} ( *{name} )() = nullptr;")
    out += ["};", ""]
    return "\n".join(out)


def bridge_cs(sections):
    out = [
        "// Generated by Engine/Tools/GenerateScriptAPI.py from Engine/Source/Scripting/ScriptBridge.def - edit the manifest, not this file.",
        "// Interop plumbing, documented in the manifest: no XML docs here (CS-10).",
        "#pragma warning disable CS1591",
        "using System.Runtime.InteropServices;",
        "",
        "namespace ScriptCore;",
        "",
        "// The managed entry points the engine calls, as the C++ ScriptBridgeAPI lays them out (ME-299).",
        "[StructLayout(LayoutKind.Sequential)]",
        "public unsafe struct ScriptBridgeAPI",
        "{",
    ]
    first = True
    for title, functions in sections:
        if not first:
            out.append("")
        first = False
        if title:
            out.append(f"    // {title}")
        for ret, name, params in functions:
            types = [TYPES[t][1] for t, _ in params] + [TYPES[ret][1]]
            out.append(f"    public delegate* unmanaged<{', '.join(types)}> {name};")
    out += [
        "}",
        "",
        "public static partial class ScriptBridge",
        "{",
        "    // The bridge manifest this ScriptCore was generated from (C++: kScriptBridgeAPIHash).",
        f"    public const ulong BridgeHash = 0x{abi_hash(sections):016X}UL;",
        "",
        "    // Fills every entry point. Assigning each method to its typed field is the check: one whose",
        "    // signature drifts from ScriptBridge.def doesn't compile. -1: built from another manifest;",
        "    // -2: the struct sizes disagree.",
        "    [UnmanagedCallersOnly]",
        "    public static unsafe int GetBridge(ScriptBridgeAPI* outBridge, int size, ulong hash)",
        "    {",
        "        if (hash != BridgeHash) return -1;",
        "        if (outBridge == null || size != sizeof(ScriptBridgeAPI)) return -2;",
    ]
    for _, functions in sections:
        for _, name, _ in functions:
            out.append(f"        outBridge->{name} = &{name};")
    out += ["        return 0;", "    }", "}", ""]
    return "\n".join(out)


def main():
    sections = parse()
    bridge = parse(BRIDGE_MANIFEST)
    outputs = {CPP_OUT: cpp(sections), CS_OUT: cs(sections), BRIDGE_CPP_OUT: bridge_cpp(bridge), BRIDGE_CS_OUT: bridge_cs(bridge)}
    if "--check" in sys.argv:
        stale = []
        for path, text in outputs.items():
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    current = f.read()
            except FileNotFoundError:
                current = None
            if current != text:
                stale.append(os.path.relpath(path, ENGINE))
        if stale:
            print("Script API out of date (run Engine/Tools/GenerateScriptAPI.py): " + ", ".join(stale))
            return 1
        print("Script API up to date")
        return 0
    for path, text in outputs.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote " + os.path.relpath(path, ENGINE))
    count = sum(len(functions) for _, functions in sections)
    print(f"{count} functions, {sum(len(functions) for _, functions in bridge)} bridge entry points")
    return 0


if __name__ == "__main__":
    sys.exit(main())
