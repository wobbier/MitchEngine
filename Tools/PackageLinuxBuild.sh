#!/usr/bin/env bash
# Packages a Linux build folder into a fully self-contained tar.gz — the kind
# of artifact you can upload to itch.io and expect to run on any x86-64 distro.
#
# Usage (from the project root, inside `nix develop` so ldd can resolve libs):
#   Engine/Tools/PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]
#   e.g. Engine/Tools/PackageLinuxBuild.sh Game_linux_Release Game_EntryPoint_x64
#
# Why bundle everything: the binaries come out of the nix toolchain, so their
# ELF interpreter and every dependency live under /nix/store — paths that exist
# on nobody else's machine, and nix's patched loader never falls back to
# /usr/lib or the ld.so cache. Leaving "common" libs to the host therefore
# doesn't work at all (and is glibc-version-fragile even with a normal
# toolchain). Instead we ship the entire resolved closure, glibc and its
# dynamic loader included, and run.sh launches the game *through the bundled
# loader*. The engine resolves assets relative to the working directory (no
# /proc/self/exe use), so exec'ing ld-linux directly is safe.
#
# The host still provides — appended to the loader search path by run.sh so
# bundled libs always win when both exist:
#   - GPU userspace drivers (libGL / libEGL / libvulkan + vendor bits)
#   - PulseAudio/PipeWire client libs (dlopen'd by FMOD), libudev (SDL joystick)
# Bundled libs bake /nix/store paths for their *data* (fontconfig config, XKB
# keymaps, ALSA config); run.sh repoints those at the host's copies via env.
set -euo pipefail

builddir=".build/${1:?Usage: PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]}"
binary="${2:?Usage: PackageLinuxBuild.sh <BuildFolder> <Binary> [Output.tar.gz]}"
output="${3:-${1}.tar.gz}"

if [ ! -x "$builddir/$binary" ]; then
    echo "error: $builddir/$binary not found (or not executable)" >&2
    exit 1
fi

libdir="$builddir/lib"
rm -rf "$libdir"   # stale bundles from previous packaging runs poison the ship
mkdir -p "$libdir"

fail=0
bundle() { # bundle <absolute lib path> [as-name]
    local name="${2:-$(basename "$1")}"
    [ -e "$libdir/$name" ] && return 0
    cp -L "$1" "$libdir/$name"
    chmod 644 "$libdir/$name"
}

# Every resolved dependency, transitively (ldd output is the flat closure).
# Anything unresolved means the packaging environment is wrong — hard error,
# never ship a binary with holes in it.
closure_of() {
    if ldd "$1" | grep -q "not found"; then
        echo "error: unresolved libraries for $1 (run inside 'nix develop'):" >&2
        ldd "$1" | grep "not found" >&2
        fail=1
    fi
    ldd "$1" | awk '$2 == "=>" && $3 ~ /^\// { print $3 }'
}

for lib in $(closure_of "$builddir/$binary"); do
    bundle "$lib"
done
[ "$fail" -eq 0 ] || exit 1

# The dynamic loader itself, invoked directly by run.sh.
interp="$(readelf -l "$builddir/$binary" | sed -n 's/.*Requesting program interpreter: \(.*\)]$/\1/p')"
if [ -z "$interp" ] || [ ! -e "$interp" ]; then
    echo "error: cannot resolve ELF interpreter '$interp'" >&2
    exit 1
fi
bundle "$interp" "ld-linux-x86-64.so.2"
chmod 755 "$libdir/ld-linux-x86-64.so.2"   # run.sh execs the loader directly

# glibc resolves hostnames through dlopen'd NSS modules that must match the
# bundled libc exactly — without these, DNS (DLC downloads) silently breaks.
glibcdir="$(dirname "$(ldd "$builddir/$binary" | awk '$1 == "libc.so.6" { print $3 }')")"
bundle "$glibcdir/libnss_files.so.2"
bundle "$glibcdir/libnss_dns.so.2"

# sdl2-compat dlopens libSDL3.so.0 and finds it only via a baked /nix/store
# RUNPATH — bundle SDL3 and its closure explicitly or SDL dies on player
# machines. Locate it the same way the loader would: through that RUNPATH.
sdl2="$libdir/libSDL2-2.0.so.0"
if [ -e "$sdl2" ]; then
    sdl3=""
    for dir in $(readelf -d "$sdl2" | sed -n 's/.*R\(UN\)\?PATH.*\[\(.*\)\]/\2/p' | tr ':' ' '); do
        [ -e "$dir/libSDL3.so.0" ] && { sdl3="$dir/libSDL3.so.0"; break; }
    done
    if [ -z "$sdl3" ]; then
        echo "error: libSDL2 is sdl2-compat but libSDL3.so.0 was not found via its RUNPATH" >&2
        exit 1
    fi
    for lib in $(closure_of "$sdl3"); do
        bundle "$lib"
    done
    [ "$fail" -eq 0 ] || exit 1
    bundle "$sdl3"

    # nixpkgs patches SDL3 to dlopen the Vulkan loader (bgfx's backend here) and
    # libX11-xcb by absolute /nix/store paths that exist on no player machine.
    # Bundle both under their plain names; run.sh points SDL at the bundled
    # loader via SDL_VULKAN_LIBRARY (bgfx already dlopens by plain name, which
    # the launcher's --library-path resolves).
    for baked in libvulkan.so.1 libX11-xcb.so.1; do
        path="$(strings -a "$libdir/libSDL3.so.0" | grep -m1 "^/nix/store/.*/$baked\$" || true)"
        if [ -n "$path" ] && [ -e "$path" ]; then
            bundle "$path" "$baked"
            for lib in $(closure_of "$path"); do
                bundle "$lib"
            done
        fi
    done
    [ "$fail" -eq 0 ] || exit 1
fi

# Pass 2: ldd's flat closure quietly hides two things it resolved just fine —
# a NEEDED name satisfied by a same-inode alias of an already-loaded lib (nix's
# libbz2.so.1 vs libWebCore's NEEDED libbz2.so.1.0), and absolute /nix/store
# NEEDED paths (libtinysparql -> ...-sqlite/lib/libsqlite3.so) that point at
# nothing on a player machine. Walk every bundled object's NEEDED list until
# stable: re-resolve misses through the referring object (its RPATH plus this
# shell's LD_LIBRARY_PATH), rewrite absolute references to plain names with
# patchelf, and fall back to SONAME-alias symlinks. Anything left is a hole.
soname_of() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p'; }
needed_of() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p'; }
resolve_via() { ldd "$1" 2>/dev/null | awk -v n="$2" '$1 == n && $2 == "=>" && $3 ~ /^\// { print $3; exit }'; }
for _pass in 1 2 3 4 5; do
    added=0
    for so in "$builddir/$binary" "$libdir"/*; do
        [ "${so##*/}" = "ld-linux-x86-64.so.2" ] && continue
        for needed in $(needed_of "$so"); do
            case "$needed" in
            /*)
                name="${needed##*/}"
                bundle "$needed" "$name"
                for dep in $(closure_of "$needed"); do bundle "$dep"; done
                patchelf --replace-needed "$needed" "$name" "$so"
                added=1
                ;;
            *)
                [ -e "$libdir/$needed" ] && continue
                src="$(resolve_via "$so" "$needed")"
                if [ -n "$src" ]; then
                    bundle "$src" "$needed"
                    for dep in $(closure_of "$src"); do bundle "$dep"; done
                    added=1
                    continue
                fi
                for cand in "$libdir"/*; do
                    if [ "$(soname_of "$cand")" = "$needed" ]; then
                        ln -s "${cand##*/}" "$libdir/$needed"
                        added=1
                        break
                    fi
                done
                [ -e "$libdir/$needed" ] && continue
                echo "error: bundled ${so##*/} needs $needed, which is not in the bundle" >&2
                fail=1
                ;;
            esac
        done
    done
    [ "$fail" -eq 0 ] || exit 1
    [ "$added" -eq 0 ] && break
done

cat > "$builddir/run.sh" <<EOF
#!/bin/sh
# Self-contained launcher: runs the game through the bundled dynamic loader.
# The host only provides GPU drivers, PulseAudio/PipeWire, and udev.
# Shell builtins only (no dirname) so a weird PATH can't break the launcher.
case "\$0" in */*) cd "\${0%/*}" || exit 1 ;; esac

libs="\$PWD/lib"
# Host lib dirs are APPENDED so bundled libs always win when both exist; these
# cover Debian/Ubuntu, Fedora/openSUSE, Arch, and the NixOS driver path.
for d in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib64 /usr/lib /run/opengl-driver/lib; do
    [ -d "\$d" ] && libs="\$libs:\$d"
done

# Bundled libs bake /nix/store data paths; repoint them at the host's copies.
if [ -z "\${FONTCONFIG_PATH:-}" ] && [ -d /etc/fonts ]; then
    FONTCONFIG_PATH=/etc/fonts; export FONTCONFIG_PATH
fi
if [ -z "\${XKB_CONFIG_ROOT:-}" ] && [ -d /usr/share/X11/xkb ]; then
    XKB_CONFIG_ROOT=/usr/share/X11/xkb; export XKB_CONFIG_ROOT
fi
if [ -z "\${ALSA_CONFIG_PATH:-}" ] && [ -f /usr/share/alsa/alsa.conf ]; then
    ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf; export ALSA_CONFIG_PATH
fi
if [ -z "\${SSL_CERT_FILE:-}" ]; then
    for f in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/ca-bundle.pem; do
        [ -f "\$f" ] && { SSL_CERT_FILE="\$f"; export SSL_CERT_FILE; break; }
    done
fi

# X11 everywhere (XWayland included): the wayland path would need the host's
# gbm/libdecor stack to cooperate with our bundled libs. Override if you like.
SDL_VIDEODRIVER="\${SDL_VIDEODRIVER:-x11}"; export SDL_VIDEODRIVER

# SDL's built-in Vulkan loader path is a /nix/store path; use the bundled one.
if [ -f ./lib/libvulkan.so.1 ] && [ -z "\${SDL_VULKAN_LIBRARY:-}" ]; then
    SDL_VULKAN_LIBRARY=libvulkan.so.1; export SDL_VULKAN_LIBRARY
fi

exec ./lib/ld-linux-x86-64.so.2 --library-path "\$libs" "./$binary" "\$@"
EOF
chmod +x "$builddir/run.sh"

# Strip intermediate archives out of the shipped folder via tar excludes; keep
# the folder itself untouched so incremental builds still work.
tar -czf "$output" \
    --exclude="*.a" \
    --exclude="*.log" \
    -C .build "$1"

echo "Packaged $builddir -> $output ($(du -h "$output" | cut -f1), $(ls "$libdir" | wc -l) bundled libs)"
