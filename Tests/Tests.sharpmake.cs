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

        conf.AddPublicDependency<SharpGameProject>(target, DependencySetting.Default);
    }
}
