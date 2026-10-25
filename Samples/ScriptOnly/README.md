# ScriptOnly: a C#-only project

A project with no C++ in it: `Assets/` (a scene and one script), `Project/Game.meproj` and
`Project/Game.Script.csproj`. The editor and the Player compile its scripts themselves with
`dotnet build`, and use the engine they were built from, so the project carries no copy of it.
See [Projects](../../Docs/Projects.md).

```bash
# Open it in the editor (from any checkout with a built Havana)
./.build/Editor_Debug/Havana --project Engine/Samples/ScriptOnly

# Run it: the Player boots Product.StartupScene (Assets/Scenes/Main.lvl)
./.build/Game_linux_Debug/Player --project Engine/Samples/ScriptOnly
```

Copy the folder anywhere to start your own; File > Open Project in the editor switches to it.
