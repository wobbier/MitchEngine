using Sharpmake;

[Generate]
public class MitchHubProject : BaseProject
{
    public MitchHubProject()
        : base()
    {
        Name = "HUB";
        SourceRootPath = @"Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Tools";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = "[project.SharpmakeCsPath]/Assets";

        conf.AddPublicDependency<Dementia>(target, DependencySetting.Default);
        conf.AddPublicDependency<Moonlight>(target, DependencySetting.Default);
        conf.AddPublicDependency<Engine>(target, DependencySetting.Default);

        // Can probably remove this once you move the ModelResource metadata out of here.
        conf.AddPublicDependency<ImGui>(target, DependencySetting.Default);
        //conf.AddPublicDependency<SharpGameProject>(target, DependencySetting.Default);


        //// SDL DLL
        //{
        //    var copyDirBuildStep = new Configuration.BuildStepCopy(
        //        @"[project.SharpmakeCsPath]/../../ThirdParty/Lib/SDL/Win64/[target.Optimization]",
        //        @"[project.SharpmakeCsPath]/.build/[target.Name]");
//
        //    copyDirBuildStep.IsFileCopy = false;
        //    copyDirBuildStep.CopyPattern = "*.dll";
        //    conf.EventPostBuildExe.Add(copyDirBuildStep);
        //}
//
        //// Ultralight DLL
        //{
        //    var copyDirBuildStep = new Configuration.BuildStepCopy(
        //        @"[project.SharpmakeCsPath]/../../ThirdParty/UltralightSDK/bin/Win64/",
        //        @"[project.SharpmakeCsPath]/.build/[target.Name]");
//
        //    conf.EventPostBuildExe.Add(copyDirBuildStep);
        //}

    }
}