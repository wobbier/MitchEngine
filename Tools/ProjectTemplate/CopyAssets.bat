mkdir .build\\Debug\\AppX\\Assets
mkdir .build\\Release\\AppX\\Assets


robocopy "Engine/Assets" ".build\\Debug\\AppX\\Assets" *.* /mir /w:0 /r:1 /v /log:".build\\Debug\\Assets.log"
robocopy "Engine/Assets" ".build\\Debug\\Assets" *.* /mir /w:0 /r:1 /v /log:".build\\Debug\\Assets.log"
robocopy "Engine/Assets" ".build\\Release\\AppX\\Assets" *.* /mir /w:0 /r:1 /v /log:".build\\Release\\Assets.log"
robocopy "Engine/Assets" ".build\\Release\\Assets" *.* /mir /w:0 /r:1 /v /log:".build\\Release\\Assets.log"

robocopy "Assets" ".build\\Debug\\AppX\\Assets" *.* /w:0 /r:1 /v /E /log:".build\\Debug\\Assets.log"
robocopy "Assets" ".build\\Debug\\Assets" *.* /w:0 /r:1 /v /E /log:".build\\Debug\\Assets.log"
robocopy "Assets" ".build\\Release\\AppX\\Assets" *.* /w:0 /r:1 /v /E /log:".build\\Release\\Assets.log"
robocopy "Assets" ".build\\Release\\Assets" *.* /w:0 /r:1 /v /E /log:".build\\Release\\Assets.log"

exit 0