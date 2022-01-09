using System;
using System.Collections.Generic;
using System.IO;
using Sharpmake;

[module: Sharpmake.Include("Modules/Dementia/Dementia.sharpmake.cs")]
[module: Sharpmake.Include("Modules/ImGui/ImGui.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Moonlight/Moonlight.sharpmake.cs")]
[module: Sharpmake.Include("Modules/Havana/Havana.sharpmake.cs")]
[module: Sharpmake.Include("Tools/BaseProject.sharpmake.cs")]
[module: Sharpmake.Include("Tools/CommonTarget.sharpmake.cs")]
[module: Sharpmake.Include("Tools/HUB/MitchHub.sharpmake.cs")]
[module: Sharpmake.Include("Tools/SharpmakeProject.sharpmake.cs")]

public abstract class BaseGameProject : BaseProject
{
    public BaseGameProject()
        : base()
    {
        SourceRootPath = @"Game/Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Apps/Game";

        conf.IncludePaths.Add("[project.SourceRootPath]");
        conf.LibraryFiles.Add("[project.Name]");

        conf.AddPublicDependency<Engine>(target);
    }
}

[Generate]
public class EntryPointGameProject : BaseProject
{
    public EntryPointGameProject()
        : base()
    {
        Name = "EntryPointGameProject";
        SourceRootPath = Globals.RootDir + @"/Game_EntryPoint/Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";

        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = "$(OutDir)";

        conf.AddPublicDependency<SharpGameProject>(target);
    }
}

[Generate]
public class Engine : BaseProject
{
    public Engine()
        : base()
    {
        Name = "MitchEngine";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Lib;
        conf.SolutionFolder = "Engine";

        conf.PrecompHeader = "PCH.h";
        conf.PrecompSource = "PCH.cpp";

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "Source"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "Modules/Singleton/Source"));

        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/SDL/include"));
        conf.IncludePaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/UltralightSDK/include"));

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/BGFX/Win64/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/UltralightSDK/lib/Win64"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/SDL/Win64/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Bullet/Win64/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Assimp/[target.Optimization]"));
        conf.LibraryFiles.Add("assimp-vc140-mt.lib");
        conf.LibraryFiles.Add("MitchEngine");

        conf.LibraryFiles.Add("AppCore.lib");
        conf.LibraryFiles.Add("Ultralight.lib");
        conf.LibraryFiles.Add("UltralightCore.lib");
        conf.LibraryFiles.Add("WebCore.lib");
        conf.LibraryFiles.Add("bgfx[target.Optimization].lib");
        conf.LibraryFiles.Add("bx[target.Optimization].lib");
        conf.LibraryFiles.Add("bimg[target.Optimization].lib");
        conf.LibraryFiles.Add("bimg_decode[target.Optimization].lib");

        // SDL DLL
        {
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                @"[project.SharpmakeCsPath]/ThirdParty/Lib/SDL/Win64/[target.Optimization]",
                Globals.RootDir + "/.build/[target.Name]");

            copyDirBuildStep.IsFileCopy = false;
            copyDirBuildStep.CopyPattern = "*.dll";
            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }

        // Ultralight DLL
        {
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                @"[project.SharpmakeCsPath]/ThirdParty/UltralightSDK/bin/Win64/",
                Globals.RootDir + "/.build/[target.Name]");

            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }

        conf.AddPublicDependency<Dementia>(target);
        conf.AddPublicDependency<ImGui>(target);
        conf.AddPublicDependency<Moonlight>(target);

        // Do a virtual method for different configs
        if (target.Optimization == Optimization.Debug)
        {
            conf.LibraryFiles.Add("SDL2d.lib");
            conf.LibraryFiles.Add("BulletCollision_Debug.lib");
            conf.LibraryFiles.Add("BulletDynamics_Debug.lib");
            conf.LibraryFiles.Add("LinearMath_Debug.lib");
            conf.LibraryFiles.Add("zlibstaticd.lib");
        }
        else
        {
            conf.LibraryFiles.Add("SDL2.lib");
            conf.LibraryFiles.Add("BulletCollision_MinsizeRel.lib");
            conf.LibraryFiles.Add("BulletDynamics_MinsizeRel.lib");
            conf.LibraryFiles.Add("LinearMath_MinsizeRel.lib");
            conf.LibraryFiles.Add("zlibstatic.lib");
        }
    }
}

public class BaseGameSolution : Solution
{
    public BaseGameSolution()
        : base(typeof(CommonTarget))
    {
        Name = "BaseGameSolution";

        AddTargets(CommonTarget.GetDefaultTargets());

        IsFileNameToLower = false;
    }

    [Configure]
    public virtual void ConfigureAll(Solution.Configuration conf, CommonTarget target)
    {
        conf.SolutionPath = @"[solution.SharpmakeCsPath]";
        conf.SolutionFileName = "[solution.Name]";
        Globals.RootDir = Path.GetFullPath("../");

        conf.AddProject<Dementia>(target);
        conf.AddProject<ImGui>(target);
        conf.AddProject<Moonlight>(target);
        conf.AddProject<Engine>(target);

        if(target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.AddProject<Havana>(target);
            conf.AddProject<MitchHubProject>(target);
        }
        else
        {
            conf.AddProject<EntryPointGameProject>(target);
        }

        if(target.Platform == Platform.win64)
        {
            conf.AddProject<UserSharpmakeProject>(target);
        }

        conf.AddProject<SharpGameProject>(target);
    }
}

public class Globals
{
    public static string RootDir = string.Empty;
}


public static class Main
{
    [Sharpmake.Main]
    public static void SharpmakeMain(Sharpmake.Arguments arguments)
    {
        KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2019, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.v10_0_19041_0);
        arguments.Generate<SharpGameSolution>();
    }
}