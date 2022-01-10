using System.IO;
using Sharpmake;

[Generate]
public class Havana : BaseProject
{
    public Havana()
        : base()
    {
        Name = "Havana";
        SourceRootPath = "Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");

        conf.AddPublicDependency<SharpGameProject>(target, DependencySetting.Default);
    }
}