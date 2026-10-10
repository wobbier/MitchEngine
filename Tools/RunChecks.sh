#!/usr/bin/env bash
# Everything that guards the engine, in one go (Linux, from the project root, inside the dev shell):
#   1. builds the editor, the game and the unit tests (debug);
#   2. runs the unit tests;
#   3. runs every editor regression script (Tools/RunEditorFlows.py);
#   4. compares the showcase scenes with their reference screenshots (Tools/ScreenshotRegression.py).
# GUI runs go through Tools/Headless.sh (a private virtual display with software Vulkan), so nothing
# opens on your desktop and nothing plays through your speakers.
#
#   Engine/Tools/RunChecks.sh                  # all of it
#   Engine/Tools/RunChecks.sh --skip-build     # use the current binaries
#   Engine/Tools/RunChecks.sh --only Nav       # just the matching editor flows (still tests + screenshots)
# Exits non-zero if anything failed.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build=1
only=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) build=0 ;;
        --only) only="$2"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

mkdir -p .tmp
failed=()
step() { echo; echo "== $1"; }

if [[ $build -eq 1 ]]; then
    step "Build"
    for target in "Havana config=editor_debug" "Game_EntryPoint config=game_linux_debug" "MitchEngine_Tests config=game_linux_debug"; do
        # shellcheck disable=SC2086
        if ! make -f MitchGame.make $target -j"$(nproc)" > ".tmp/RunChecks.build.log" 2>&1; then
            grep -E "error" ".tmp/RunChecks.build.log" | head -20
            failed+=("build: $target")
        fi
    done
    if [[ ${#failed[@]} -gt 0 ]]; then
        echo "Build failed: ${failed[*]}"
        exit 1
    fi
fi

step "Unit tests"
if ! ./.build/Game_linux_Debug/MitchEngine_Tests > .tmp/RunChecks.tests.log 2>&1; then
    grep -E "ERROR|FAILED|Status" .tmp/RunChecks.tests.log | head -30
    failed+=("unit tests")
else
    tail -3 .tmp/RunChecks.tests.log | head -2
fi

step "Editor flows"
if ! python3 "$here/RunEditorFlows.py" --editor .build/Editor_Debug/Havana --wrapper "$here/Headless.sh" --only "$only"; then
    failed+=("editor flows")
fi

if [[ -d Assets/Scenes/Showcase/Reference ]]; then
    step "Screenshot regression"
    if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Game_EntryPoint_x64 --reference Assets/Scenes/Showcase/Reference --wrapper "$here/Headless.sh" Assets/Scenes/Showcase/*.lvl; then
        failed+=("screenshot regression")
    fi
fi

echo
if [[ ${#failed[@]} -gt 0 ]]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All checks passed."
