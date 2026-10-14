#!/usr/bin/env bash
set -euo pipefail

texture_compiler="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/linux/texturec"

# Vendored binaries may reference a Nix loader that has been garbage-collected.
# Run with the current dev shell's loader and C++ runtime when available.
if [[ -n "${NIX_CC:-}" && -f "$NIX_CC/nix-support/dynamic-linker" ]]; then
    exec "$(cat "$NIX_CC/nix-support/dynamic-linker")" \
        --library-path "$(dirname "$(g++ -print-file-name=libstdc++.so.6)"):${LD_LIBRARY_PATH:-}" \
        "$texture_compiler" "$@"
fi

exec "$texture_compiler" "$@"
