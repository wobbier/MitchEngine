using System.IO;
using Sharpmake;

[Generate]
public class Havana : BaseProject
{
    public Havana()
        : base()
    {
        Name = "Havana";
        SourceRootPath = Path.Combine("[project.SharpmakeCsPath]", "Source");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;

        conf.SolutionFolder = "Apps";

        conf.IncludePaths.Add("[project.SourceRootPath]");

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";

        conf.AddPublicDependency<SharpGameProject>(target, DependencySetting.Default);
    }
}