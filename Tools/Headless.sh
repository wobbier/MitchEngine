#!/usr/bin/env bash
# Runs a command on a private virtual display (Xvfb) with software Vulkan (Mesa's lavapipe), so
# automated runs never open windows on your desktop and render the same pixels on any machine.
#
#   Engine/Tools/Headless.sh ./.build/Game_linux_Debug/Game_EntryPoint_x64 --scene X.lvl --frames 60 --exit
#
# Display :99 by default (HEADLESS_DISPLAY=:N to change it). Xvfb is started there if nothing is
# running yet, and stopped again afterwards. LAVAPIPE_ICD overrides where the lavapipe ICD is found.
# Linux only.
set -u
display="${HEADLESS_DISPLAY:-:99}"
socket="/tmp/.X11-unix/X${display#:}"

icd="${LAVAPIPE_ICD:-}"
if [[ -z "$icd" ]]; then
    for candidate in /run/opengl-driver/share/vulkan/icd.d/lvp_icd.x86_64.json \
                     /usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
                     /usr/share/vulkan/icd.d/lvp_icd.json \
                     /etc/vulkan/icd.d/lvp_icd.x86_64.json; do
        if [[ -f "$candidate" ]]; then icd="$candidate"; break; fi
    done
fi
if [[ -z "$icd" ]]; then
    echo "Headless.sh: lavapipe not found (install Mesa's Vulkan drivers or set LAVAPIPE_ICD)" >&2
    exit 3
fi

started=""
if [[ ! -S "$socket" ]]; then
    if ! command -v Xvfb > /dev/null; then
        echo "Headless.sh: Xvfb isn't installed" >&2
        exit 3
    fi
    Xvfb "$display" -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
    started=$!
    for _ in $(seq 1 50); do
        [[ -S "$socket" ]] && break
        sleep 0.1
    done
fi

env -u WAYLAND_DISPLAY DISPLAY="$display" SDL_VIDEODRIVER=x11 VK_ICD_FILENAMES="$icd" "$@"
status=$?
if [[ -n "$started" ]]; then
    kill "$started" 2> /dev/null
    wait "$started" 2> /dev/null
fi
exit $status
