using System.IO;
using Sharpmake;

// The Player (SHIP-4, Docs/Projects.md): the generic game runtime, the engine with no game module.
// It runs any C#-only project (`Player --project <folder>`); projects with C++ game code ship
// their own Game_EntryPoint instead. Game solutions build it next to Game_EntryPoint.
[Generate]
public class PlayerProject : BaseProject
{
    public PlayerProject()
        : base()
    {
        Name = "Player";
        SourceRootPath = "Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";
        conf.TargetFileName = "Player";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;

        conf.AddPublicDependency<Engine>(target);
    }

    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        // The third-party DLLs the engine loads, next to the executable (as Game_EntryPoint does).
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
