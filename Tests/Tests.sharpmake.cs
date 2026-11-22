using System.IO;
using Sharpmake;

// Headless unit tests for the engine (doctest). Run from the repo root:
//   ./.build/<config>/MitchEngine_Tests
[Generate]
public class EngineTests : BaseProject
{
    public EngineTests()
        : base()
    {
        Name = "MitchEngine_Tests";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "Source");
        // Editor history is independent of the Havana application; test its save-point semantics directly.
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../Modules/Havana/Source/Editor/UndoStack.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../Modules/Havana/Source/Editor/SequenceDocument.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../Modules/Havana/Source/Widgets/SequenceTimeline.cpp"));
        // A game's editor code (linked into editor builds of this runner) may route through Havana's
        // actions and icons; MSVC resolves every symbol of each object it pulls from the game lib.
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../Modules/Havana/Source/Editor/EditorActions.cpp"));
        SourceFiles.Add(Path.Combine("[project.SharpmakeCsPath]", "../Modules/Havana/Source/Utils/EditorIcons.cpp"));
        // A game's own unit tests (Game/Tests/Source) build into the same runner.
        string gameTests = Path.Combine(Globals.RootDir, "Game/Tests/Source");
        if (Globals.HasGameModule && Directory.Exists(gameTests))
        {
            AdditionalSourceRootPaths.Add(gameTests);
        }
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../ThirdParty/doctest"));
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;

        // doctest and CLog.h both define INFO; the later one wins, as on gcc/clang. MSVC's
        // macro-redefinition warning (C4005) would otherwise fail the warnings-as-errors build.
        conf.Options.Add(new Options.Vc.Compiler.DisableSpecificWarnings("4005"));

        if (Globals.HasGameModule)
        {
            conf.AddPublicDependency(target, Globals.GameProjectType, DependencySetting.Default);
        }
        else
        {
            conf.AddPublicDependency<Engine>(target, DependencySetting.Default);
        }
    }
}
