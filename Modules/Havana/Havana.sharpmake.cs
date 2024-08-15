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

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        // Copy custom game dlls until I figure this out
        {
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                Path.Combine(Globals.RootDir, $@"ThirdParty/Bin/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}/"),
                Globals.RootDir + "/.build/[target.Name]");

            copyDirBuildStep.IsFileCopy = false;
            copyDirBuildStep.CopyPattern = "*.dll";
            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }
    }
}