#!/usr/bin/env bash
# Packages a Linux build folder into a relocatable tar.gz (the usual way to
# ship a Linux game outside a store: binary + assets + bundled libs + launcher).
#
# Usage (from the project root, inside `nix develop` so ldd can resolve libs):
#   Engine/Tools/PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]
#   e.g. Engine/Tools/PackageLinuxBuild.sh Game_linux_Release Game_EntryPoint_x64
#
# Bundled next to the binary in lib/:
#   - shared libs resolved from the engine checkout (Ultralight, FMOD) — their
#     baked rpaths point at the build machine and are dead on any other box
#   - libSDL2 / libstdc++ / libgcc_s from the toolchain, so the game doesn't
#     depend on the target distro's versions
# Left to the host system: glibc, the GL/EGL/Vulkan driver stack, X11/Wayland,
# GTK3/fontconfig/ALSA/curl — bundling those breaks more than it fixes.
set -e

builddir=".build/${1:?Usage: PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]}"
binary="${2:?Usage: PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]}"
output="${3:-${1}.tar.gz}"

if [ ! -x "$builddir/$binary" ]; then
    echo "error: $builddir/$binary not found (or not executable)" >&2
    exit 1
fi

mkdir -p "$builddir/lib"

ldd "$builddir/$binary" | awk '$3 ~ /^\// { print $3 }' | while read -r lib; do
    case "$(basename "$lib")" in
        libSDL2*|libstdc++*|libgcc_s*) ;;
        *)
            case "$lib" in
                */ThirdParty/*) ;; # UltralightSDK / FMOD out of the engine tree
                *) continue ;;
            esac
            ;;
    esac
    cp -Ln "$lib" "$builddir/lib/" 2>/dev/null || true
done

cat > "$builddir/run.sh" <<EOF
#!/usr/bin/env bash
# Launcher: points the loader at the bundled libs in ./lib.
# Needs from the system: glibc, GL/Vulkan drivers, X11/Wayland, GTK3,
# fontconfig, ALSA, curl (standard on any desktop distro).
cd "\$(dirname "\$0")"
LD_LIBRARY_PATH="\$PWD/lib:\$LD_LIBRARY_PATH" exec "./$binary" "\$@"
EOF
chmod +x "$builddir/run.sh"

# Strip intermediate archives out of the shipped folder via tar excludes; keep
# the folder itself untouched so incremental builds still work.
tar -czf "$output" \
    --exclude="*.a" \
    --exclude="*.log" \
    -C .build "$1"

echo "Packaged $builddir -> $output"
