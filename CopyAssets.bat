mkdir Build\\Debug\\AppX\\Assets
mkdir Build\\Release\\AppX\\Assets

robocopy "Assets" "Build\\Debug\\AppX\\Assets" *.* /mir /w:0 /r:1 /v /log:"Build\\Debug\\Assets.log"
robocopy "Assets" "Build\\Debug\\Assets" *.* /mir /w:0 /r:1 /v /log:"Build\\Debug\\Assets.log"
robocopy "Assets" "Build\\Release\\AppX\\Assets" *.* /mir /w:0 /r:1 /v /log:"Build\\Release\\Assets.log"
robocopy "Assets" "Build\\Release\\Assets" *.* /mir /w:0 /r:1 /v /log:"Build\\Release\\Assets.log"

exit 0