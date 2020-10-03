mkdir Build\\Debug\\AppX\\Assets
mkdir Build\\Release\\AppX\\Assets


robocopy "Engine/Assets" "Build\\Debug\\AppX\\Assets" *.* /xd Havana /mir /w:0 /r:1 /v /log:"Build\\Debug\\Assets.log"
robocopy "Engine/Assets" "Build\\Debug\\Assets" *.* /xd Havana /mir /w:0 /r:1 /v /log:"Build\\Debug\\Assets.log"
robocopy "Engine/Assets" "Build\\Release\\AppX\\Assets" *.* /xd Havana /mir /w:0 /r:1 /v /log:"Build\\Release\\Assets.log"
robocopy "Engine/Assets" "Build\\Release\\Assets" *.* /xd Havana /mir /w:0 /r:1 /v /log:"Build\\Release\\Assets.log"

robocopy "Assets" "Build\\Debug\\AppX\\Assets" *.* /xd Havana /w:0 /r:1 /v /E /log:"Build\\Debug\\Assets.log"
robocopy "Assets" "Build\\Debug\\Assets" *.* /xd Havana /w:0 /r:1 /v /E /log:"Build\\Debug\\Assets.log"
robocopy "Assets" "Build\\Release\\AppX\\Assets" *.* /xd Havana /w:0 /r:1 /v /E /log:"Build\\Release\\Assets.log"
robocopy "Assets" "Build\\Release\\Assets" *.* /xd Havana /w:0 /r:1 /v /E /log:"Build\\Release\\Assets.log"

exit 0