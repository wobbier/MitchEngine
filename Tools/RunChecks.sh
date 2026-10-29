#!/usr/bin/env bash
# Everything that guards the engine, in one go (Linux, from the project root, inside the dev shell):
#   1. builds the editor, the game and the unit tests (debug);
#   2. runs the unit tests;
#   3. runs every editor regression script (Tools/RunEditorFlows.py);
#   4. compares the showcase scenes with their reference screenshots (Tools/ScreenshotRegression.py);
#   5. cooks three showcases (Havana --cook: Lighting, Playground and RenderTexture's camera feeds)
#      and compares the game on the cooked tree and its pak;
#   6. builds them as a shippable Retail game (Havana --build-game) and compares that, run from
#      outside the repo without a .NET install;
#   7. opens the C#-only sample project (Samples/ScriptOnly), copied out of the repo, in the editor
#      and runs it in the Player, both on this checkout's engine with no C++ build, then ships it
#      with Build Game (the Retail Player) and compares that, run from outside the repo the same way;
#   8. starts the editor outside any project, so it opens the Project Hub, which creates a project
#      from the Empty template and opens it in the editor (Tests/Hub/HubFlows.edscript).
#   9. creates a project from every template (Havana --new-project) outside the repo, plays it in
#      the editor (Tests/Templates/<Id>/<Id>Flows.edscript), ships it with Build Game and compares
#      a frame of the shipped build with Tests/Templates/<Id>/Reference.
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
    for target in "Havana config=editor_debug" "Game_EntryPoint config=game_linux_debug" "Player config=game_linux_debug" "MitchEngine_Tests config=game_linux_debug"; do
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

step "Runtime I/O lint"
if ! python3 "$here/LintRuntimeIO.py"; then
    failed+=("runtime io lint")
fi

step "Source generator tests"
if ! { dotnet build Engine/Modules/ScriptCore/Generators.Tests/MitchEngine.Generators.Tests.csproj --artifacts-path .tmp/DotNet/GeneratorTests -v quiet --nologo \
        && dotnet test .tmp/DotNet/GeneratorTests/bin/MitchEngine.Generators.Tests/debug/MitchEngine.Generators.Tests.dll --nologo; } > .tmp/RunChecks.generators.log 2>&1; then
    tail -20 .tmp/RunChecks.generators.log
    failed+=("generator tests")
else
    grep -E "Passed!|Failed!" .tmp/RunChecks.generators.log | tail -1
fi

step "Editor flows"
# 1500s covers the longest flow (LaunchNight) on a cold 2-core CI runner.
if ! python3 "$here/RunEditorFlows.py" --editor .build/Editor_Debug/Havana --wrapper "$here/Headless.sh" --only "$only" --timeout 1500; then
    failed+=("editor flows")
fi

if [[ -d Assets/Scenes/Showcase/Reference ]]; then
    step "Screenshot regression"
    if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Game_EntryPoint_x64 --reference Assets/Scenes/Showcase/Reference --wrapper "$here/Headless.sh" Assets/Scenes/Showcase/*.lvl; then
        failed+=("screenshot regression")
    fi
fi

# The cooker (SHIP-2): cook three showcases (each recorded through the game first), then run the
# game on the cooked tree and, from an empty directory, on its pak alone. All must still match
# the references: nothing the scenes need may be left behind with the sources (RenderTexture's
# .rendertexture assets ship as data, its monitors are checked region by region).
cook_scenes=(Assets/Scenes/Showcase/Lighting.lvl Assets/Scenes/Showcase/Playground.lvl Assets/Scenes/Showcase/RenderTexture.lvl)
cook_scene_args=()
cook_scenes_present=1
for scene in "${cook_scenes[@]}"; do
    [[ -f $scene ]] || cook_scenes_present=0
    cook_scene_args+=(--scene "$scene")
done
if [[ $cook_scenes_present == 1 && -d Assets/Scenes/Showcase/Reference ]]; then
    step "Cooked build"
    if ! "$here/Headless.sh" ./.build/Editor_Debug/Havana --cook "${cook_scene_args[@]}" --out .tmp/Cook --pak .tmp/Cook.mpak \
            --game .build/Game_linux_Debug/Game_EntryPoint_x64 > .tmp/RunChecks.cook.log 2>&1; then
        grep -E "Cook|Error" .tmp/RunChecks.cook.log | tail -20
        failed+=("cook")
    else
        grep -E "Cook finished" .tmp/RunChecks.cook.log
        if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Game_EntryPoint_x64 --reference Assets/Scenes/Showcase/Reference --wrapper "$here/Headless.sh" \
                --out .tmp/ScreenshotRegression/Cooked --cwd .tmp/Cook "${cook_scenes[@]}"; then
            failed+=("cooked tree screenshots")
        fi
        rm -rf .tmp/CookEmpty && mkdir -p .tmp/CookEmpty
        if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Game_EntryPoint_x64 --reference Assets/Scenes/Showcase/Reference --wrapper "$here/Headless.sh" \
                --out .tmp/ScreenshotRegression/CookedPak --cwd .tmp/CookEmpty --game-args "--pak $PWD/.tmp/Cook.mpak" "${cook_scenes[@]}"; then
            failed+=("cooked pak screenshots")
        fi
    fi
fi

# Build Game (SHIP-3): a Retail build of the same showcases, copied out of the repo and started from
# an empty directory with no DOTNET_ROOT and none of the repo's library paths. It must run its
# scripts on the app-local .NET and still match the references. The project's main menu ships too:
# its scripts draw ImGui debug windows, which Retail (no ImGui) must survive.
smoke_scene=Assets/Main.lvl
if [[ $cook_scenes_present == 1 && -f $smoke_scene && -d Assets/Scenes/Showcase/Reference ]]; then
    step "Build Game"
    if ! "$here/Headless.sh" ./.build/Editor_Debug/Havana --build-game "${cook_scene_args[@]}" --scene "$smoke_scene" --out .tmp/BuildGame/Checks \
            --game .build/Game_linux_Debug/Game_EntryPoint_x64 > .tmp/RunChecks.buildgame.log 2>&1; then
        grep -E "Build Game|Error" .tmp/RunChecks.buildgame.log | tail -20
        failed+=("build game")
    else
        grep -E "Build Game finished" .tmp/RunChecks.buildgame.log
        shipped="$(mktemp -d)"
        cp -a .tmp/BuildGame/Checks "$shipped/Game" && mkdir -p "$shipped/Empty"
        launcher="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["Launcher"])' "$shipped/Game/BuildInfo.json")"
        system_libs="$(tr ':' '\n' <<< "${LD_LIBRARY_PATH:-}" | grep -vF "$PWD" | paste -sd: -)"
        rm -rf .tmp/ScreenshotRegression/Shipped
        if ! env -u DOTNET_ROOT LD_LIBRARY_PATH="$system_libs" python3 "$here/ScreenshotRegression.py" --game "$shipped/Game/$launcher" \
                --reference Assets/Scenes/Showcase/Reference --wrapper "$here/Headless.sh" --out .tmp/ScreenshotRegression/Shipped --cwd "$shipped/Empty" "${cook_scenes[@]}"; then
            failed+=("shipped build screenshots")
        elif grep -L "Scripts: .NET runtime from the app-local install" .tmp/ScreenshotRegression/Shipped/*.log | grep -q .; then
            echo "The shipped build didn't run its scripts on the app-local .NET (see .tmp/ScreenshotRegression/Shipped)"
            failed+=("shipped build runtime")
        fi
        smoke_log="$PWD/.tmp/RunChecks.shipped-menu.log"
        if ! ( cd "$shipped/Empty" && env -u DOTNET_ROOT LD_LIBRARY_PATH="$system_libs" "$here/Headless.sh" "$shipped/Game/$launcher" \
                --scene "$smoke_scene" --frames 60 --no-audio --exit > "$smoke_log" 2>&1 ); then
            echo "The shipped build failed on $smoke_scene (see $smoke_log)"
            failed+=("shipped build menu")
        else
            echo "ok    $smoke_scene runs in the shipped build"
        fi
        rm -rf "$shipped"
    fi
fi

# A C#-only project (SHIP-4): the engine's sample, copied out of the repo beside a second copy.
# Neither has C++ or an Engine/ of its own; the editor and the Player run them on this checkout's
# engine and build their scripts into the project. The editor flow plays the scripts and asks
# File > Open Project for the sibling; the Player's capture must match the sample's reference, and
# so must the Retail Player that Build Game ships.
sample="$here/../Samples/ScriptOnly"
sample_checks="$here/../Tests/Samples/ScriptOnly"
if [[ -d $sample && -d $sample_checks/Reference ]]; then
    step "C#-only project"
    projects="$(mktemp -d)"
    cp -a "$sample" "$projects/ScriptOnly" && cp -a "$sample" "$projects/Other"
    if ! "$here/Headless.sh" ./.build/Editor_Debug/Havana --project "$projects/ScriptOnly" --editor-exec "$sample_checks/ScriptOnlyFlows.edscript" \
            > .tmp/RunChecks.scriptonly.log 2>&1 || ! grep -q "\[editor-exec\] PASSED" .tmp/RunChecks.scriptonly.log; then
        grep -E "editor-exec|Error" .tmp/RunChecks.scriptonly.log | tail -20
        failed+=("C#-only project in the editor")
    elif [[ ! -f $projects/ScriptOnly/.tmp/Scripts/Havana-Debug/Game.Script.dll ]]; then
        echo "The editor didn't build the project's scripts into the project (see .tmp/RunChecks.scriptonly.log)"
        failed+=("C#-only project scripts")
    else
        echo "ok    ScriptOnlyFlows (editor)"
    fi
    # The runs are byte-identical, and the scene without its script differs by about 1.4%, so the
    # limit is tight and the log must show the script assembly loaded.
    if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Player --reference "$sample_checks/Reference" --wrapper "$here/Headless.sh" \
            --out .tmp/ScreenshotRegression/ScriptOnly --cwd "$projects/ScriptOnly" --max-changed 0.002 Assets/Scenes/Main.lvl; then
        failed+=("C#-only project in the Player")
    elif ! grep -q "Loaded 1 type(s) from Game.Script.dll" .tmp/ScreenshotRegression/ScriptOnly/Main.log; then
        echo "The Player didn't load the project's scripts (see .tmp/ScreenshotRegression/ScriptOnly/Main.log)"
        failed+=("C#-only project scripts in the Player")
    fi
    # Build Game (ME-266) ships the Player: a Retail build of the copy, moved out beside it and
    # captured from an empty directory with no DOTNET_ROOT and none of the repo's library paths.
    if ! "$here/Headless.sh" ./.build/Editor_Debug/Havana --project "$projects/ScriptOnly" --build-game --out "$projects/Built" \
            > .tmp/RunChecks.scriptonly-build.log 2>&1; then
        grep -E "Build Game|Error" .tmp/RunChecks.scriptonly-build.log | tail -20
        failed+=("C#-only project build game")
    else
        grep -E "Build Game finished" .tmp/RunChecks.scriptonly-build.log
        mv "$projects/Built" "$projects/Game" && mkdir -p "$projects/Empty"
        launcher="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["Launcher"])' "$projects/Game/BuildInfo.json")"
        system_libs="$(tr ':' '\n' <<< "${LD_LIBRARY_PATH:-}" | grep -vF "$PWD" | paste -sd: -)"
        rm -rf .tmp/ScreenshotRegression/ScriptOnlyShipped
        if ! env -u DOTNET_ROOT LD_LIBRARY_PATH="$system_libs" python3 "$here/ScreenshotRegression.py" --game "$projects/Game/$launcher" \
                --reference "$sample_checks/Reference" --wrapper "$here/Headless.sh" --out .tmp/ScreenshotRegression/ScriptOnlyShipped \
                --cwd "$projects/Empty" --max-changed 0.002 Assets/Scenes/Main.lvl; then
            failed+=("C#-only project shipped build")
        elif ! grep -q "Scripts: .NET runtime from the app-local install" .tmp/ScreenshotRegression/ScriptOnlyShipped/Main.log \
                || ! grep -q "Loaded 1 type(s) from Game.Script.dll" .tmp/ScreenshotRegression/ScriptOnlyShipped/Main.log; then
            echo "The shipped C#-only build didn't run its scripts on the app-local .NET (see .tmp/ScreenshotRegression/ScriptOnlyShipped/Main.log)"
            failed+=("C#-only project shipped runtime")
        fi
    fi
    rm -rf "$projects"
fi

# The Project Hub (SHIP-5): the editor started in an empty folder (no project) opens the Hub. The
# flow creates NewGame from the Empty template there; the editor the Hub starts opens it and runs
# the rest of the flow. The Hub's recent list must name it, and the editor must have built its scripts.
hub_checks="$here/../Tests/Hub"
if [[ -f $hub_checks/HubFlows.edscript ]]; then
    step "Project Hub"
    hub_dir="$(mktemp -d)"
    editor="$PWD/.build/Editor_Debug/Havana"
    cp -a "$sample" "$hub_dir/Other"
    if ! ( cd "$hub_dir" && "$here/Headless.sh" "$editor" --editor-exec "$hub_checks/HubFlows.edscript" ) \
            > .tmp/RunChecks.hub.log 2>&1 || ! grep -q "\[editor-exec\] PASSED" .tmp/RunChecks.hub.log; then
        grep -E "editor-exec|Project Hub|Error" .tmp/RunChecks.hub.log | tail -20
        failed+=("Project Hub")
    elif ! grep -q "\"Root\": \"$hub_dir/NewGame\"" "$hub_dir/.tmp/Hub/RecentProjects.json" 2>/dev/null; then
        echo "The Hub's recent list doesn't name the new project (see .tmp/RunChecks.hub.log)"
        failed+=("Project Hub recent list")
    elif [[ ! -f $hub_dir/NewGame/.tmp/Scripts/Havana-Debug/Game.Script.dll ]]; then
        echo "The editor didn't build the new project's scripts (see .tmp/RunChecks.hub.log)"
        failed+=("Project Hub new project scripts")
    else
        echo "ok    HubFlows (Hub, then the editor on the new project)"
    fi
    rm -rf "$hub_dir"
fi

# Templates (SHIP-6): every engine template with checks under Tests/Templates/<Id>/ becomes a new
# project outside the repo, as File > New Project would make it. Its flow plays it in the editor
# (held keys move the player); Build Game ships it on the Retail Player, and a frame of that build,
# run from an empty directory without DOTNET_ROOT, must match the template's reference. The runs are
# byte-identical, so the limit is tight, and the log must show the scripts loaded on the app-local .NET.
template_checks="$here/../Tests/Templates"
if [[ -d $template_checks ]]; then
    step "Templates"
    templates_dir="$(mktemp -d)"
    editor="$PWD/.build/Editor_Debug/Havana"
    system_libs="$(tr ':' '\n' <<< "${LD_LIBRARY_PATH:-}" | grep -vF "$PWD" | paste -sd: -)"
    mkdir -p "$templates_dir/Empty"
    for checks in "$template_checks"/*/; do
        id="$(basename "$checks")"
        project="$templates_dir/New$id"
        log=".tmp/RunChecks.template-$id.log"
        if ! ( cd "$templates_dir" && "$here/Headless.sh" "$editor" --new-project "$id" --name "New$id" ) > "$log" 2>&1; then
            tail -5 "$log"
            failed+=("template $id: new project")
            continue
        fi
        if ! "$here/Headless.sh" "$editor" --project "$project" --editor-exec "$checks/${id}Flows.edscript" >> "$log" 2>&1 \
                || ! grep -q "\[editor-exec\] PASSED" "$log"; then
            grep -E "editor-exec|Error" "$log" | tail -20
            failed+=("template $id: editor")
            continue
        fi
        echo "ok    ${id}Flows (new project in the editor)"
        if ! "$here/Headless.sh" "$editor" --project "$project" --build-game --out "$templates_dir/Built$id" >> "$log" 2>&1; then
            grep -E "Build Game|Error" "$log" | tail -20
            failed+=("template $id: build game")
            continue
        fi
        launcher="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["Launcher"])' "$templates_dir/Built$id/BuildInfo.json")"
        out=".tmp/ScreenshotRegression/Template$id"
        rm -rf "$out"
        if ! env -u DOTNET_ROOT LD_LIBRARY_PATH="$system_libs" python3 "$here/ScreenshotRegression.py" --game "$templates_dir/Built$id/$launcher" \
                --reference "$checks/Reference" --wrapper "$here/Headless.sh" --out "$out" --cwd "$templates_dir/Empty" --max-changed 0.002 Assets/Scenes/Main.lvl; then
            failed+=("template $id: shipped build")
        elif ! grep -q "Scripts: .NET runtime from the app-local install" "$out/Main.log" \
                || ! grep -q "Loaded [0-9]* type(s) from Game.Script.dll" "$out/Main.log"; then
            echo "The shipped $id build didn't load its scripts on the app-local .NET (see $out/Main.log)"
            failed+=("template $id: shipped runtime")
        fi
    done
    rm -rf "$templates_dir"
fi

# Test scenes with a committed reference (UITest and friends) regress the same way.
# (A plain glob test: the devshell's bash is built without compgen.)
test_references=(Assets/Scenes/Tests/Reference/*.png)
if [[ -e "${test_references[0]}" ]]; then
    step "Test-scene screenshots"
    test_scenes=()
    for reference in Assets/Scenes/Tests/Reference/*.png; do
        scene="Assets/Scenes/Tests/$(basename "${reference%.png}").lvl"
        [[ -f $scene ]] && test_scenes+=("$scene")
    done
    if ! python3 "$here/ScreenshotRegression.py" --game .build/Game_linux_Debug/Game_EntryPoint_x64 --reference Assets/Scenes/Tests/Reference --wrapper "$here/Headless.sh" "${test_scenes[@]}"; then
        failed+=("test screenshots")
    fi
fi

echo
if [[ ${#failed[@]} -gt 0 ]]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All checks passed."
