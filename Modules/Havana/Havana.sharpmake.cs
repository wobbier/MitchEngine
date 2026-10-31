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
        // The Project Hub runs on the Tool app framework (an ImGui window without the engine).
        SourceFiles.Add(@"[project.SharpmakeCsPath]/../Tool/Source/Tool.h");
        SourceFiles.Add(@"[project.SharpmakeCsPath]/../Tool/Source/Tool.cpp");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.IncludePaths.Add("[project.SharpmakeCsPath]/../Tool/Source");
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;

        //conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "../../ThirdParty/Lib/Optick/Win64/[target.Optimization]"));
        //conf.LibraryFiles.Add("OptickCore.lib");

        // A game checkout's editor links its game module (components and cores register as it
        // loads); the engine's own solution (EngineSolution) has none and edits C#-only projects.
        if (Globals.HasGameModule)
        {
            conf.AddPublicDependency(target, Globals.GameProjectType, DependencySetting.Default);
            conf.Defines.Add("DEFINE_ME_GAME_MODULE");
        }
        else
        {
            conf.AddPublicDependency<Engine>(target, DependencySetting.Default);
        }
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