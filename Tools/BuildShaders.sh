#!/usr/bin/env bash
# Compiles engine shaders (Engine/Assets/Shaders/**/*.vert|*.frag) for Linux/Vulkan (spirv) and
# OpenGL (glsl 150), writing <name>.<platform>.bin next to each source. If the game repository has
# a compiled copy of the same shader under Assets/Shaders (it shadows the engine copy at runtime),
# that copy is refreshed too. Windows (dx11) and macOS (metal) compile at load time through
# ShaderFileMetadata::Export.
#
#   Engine/Tools/BuildShaders.sh                 # everything
#   Engine/Tools/BuildShaders.sh Post/Tonemap    # sources whose path contains the filter
#   Engine/Tools/BuildShaders.sh --changed       # only sources newer than their spirv binary
set -u
ENGINE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$ENGINE_DIR/.." && pwd)"
SHADERS="$ENGINE_DIR/Assets/Shaders"
SHADERC="$ENGINE_DIR/Tools/linux/shaderc"
FILTER=""
CHANGED_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --changed) CHANGED_ONLY=1 ;;
        *) FILTER="$arg" ;;
    esac
done

failures=0
count=0
while IFS= read -r -d '' source; do
    relative="${source#$SHADERS/}"
    [[ -n "$FILTER" && "$relative" != *"$FILTER"* ]] && continue
    case "$source" in
        *.vert) type=vertex ;;
        *.frag) type=fragment ;;
        *) continue ;;
    esac
    varying="${source%.*}.var"
    if [[ ! -f "$varying" ]]; then
        echo "skip (no .var): $relative"
        continue
    fi
    if [[ $CHANGED_ONLY -eq 1 && -f "$source.spirv.bin" && "$source.spirv.bin" -nt "$source" ]]; then
        continue
    fi
    for profile in spirv 150; do
        if [[ "$profile" == "spirv" ]]; then ext=spirv; else ext=glsl; fi
        output="$source.$ext.bin"
        had_output=0
        [[ -f "$output" ]] && had_output=1
        if ! "$SHADERC" -f "$source" -o "$output" --varyingdef "$varying" --platform linux -p "$profile" --type "$type" -i "$SHADERS" --depends > /tmp/shaderc_$$.log 2>&1; then
            # Some shaders (Ultralight UI) are Vulkan-only; only count GL failures for shaders
            # that have built for GL before, or for new shaders.
            if [[ "$profile" == "150" && $had_output -eq 0 && -f "$source.spirv.bin" && ! "$source" -nt "$source.spirv.bin" && "$relative" == UI* ]]; then
                continue
            fi
            echo "FAILED: $relative ($profile)"
            sed 's/^/    /' /tmp/shaderc_$$.log | head -30
            failures=$((failures + 1))
            continue
        fi
        shadow="$REPO_DIR/Assets/Shaders/$relative.$ext.bin"
        if [[ -f "$shadow" ]]; then
            cp "$output" "$shadow"
            [[ -f "$output.d" ]] && cp "$output.d" "$shadow.d"
        fi
    done
    count=$((count + 1))
done < <(find "$SHADERS" \( -name '*.vert' -o -name '*.frag' \) -print0 | sort -z)
rm -f /tmp/shaderc_$$.log
echo "Compiled $count shader(s), $failures failure(s)."
[[ $failures -eq 0 ]]
