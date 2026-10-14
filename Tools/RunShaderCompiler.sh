#!/usr/bin/env bash
set -euo pipefail
engine_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shader_compiler="$engine_dir/Tools/linux/shaderc"
rebuilt="$engine_dir/ThirdParty/bgfx/.build/linux64_gcc/bin/shadercRelease"
if [[ -x "$rebuilt" ]]; then shader_compiler="$rebuilt"; fi
# Vendored tool binaries can retain the path of a garbage-collected Nix loader.
if [[ -n "${NIX_CC:-}" && -f "$NIX_CC/nix-support/dynamic-linker" ]]; then
    exec "$(cat "$NIX_CC/nix-support/dynamic-linker")" \
        --library-path "$(dirname "$(g++ -print-file-name=libstdc++.so.6)"):${LD_LIBRARY_PATH:-}" \
        "$shader_compiler" "$@"
fi
exec "$shader_compiler" "$@"
