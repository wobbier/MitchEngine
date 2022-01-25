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
        Name = "Game_EntryPoint";
        
        SourceRootPath = Globals.RootDir + @"/Game_EntryPoint/Source";
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.Output = Configuration.OutputType.Exe;
        conf.SolutionFolder = "Apps";
        if(target.GetPlatform() == Platform.win64)
        {
            conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        }
        conf.TargetPath = Globals.RootDir + "/.build/[target.Name]/";
        conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = "$(OutDir)";
        if(target.SubPlatform == CommonTarget.SubPlatformType.UWP)
        {
            conf.ConsumeWinRTExtensions.Add("main.cpp");
        }

        conf.AddPublicDependency<SharpGameProject>(target);
    }
}
[Generate]
public class EntryPointGameProjectUWP : EntryPointGameProject
{
    public EntryPointGameProjectUWP()
        : base()
    {
        Name = "Game_EntryPoint_UWP";
        RootPath = Path.Combine(RootPath, "Game_EntryPoint");
        //ResourceFiles.Add(Globals.RootDir + "/Game_EntryPoint/Assets/*.*");
        //ResourceFiles.Add();
        //ResourceFilesExtensions.Add(".appxmanifest");
    }

    public override void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        base.ConfigureAll(conf, target);
        conf.ProjectPath = Path.Combine(Globals.RootDir, "Game_EntryPoint");

        conf.ConsumeWinRTExtensions.Add("main.cpp");
        conf.AppxManifestFilePath = Path.Combine(Globals.RootDir, "Game_EntryPoint/Package.appxmanifest");
        conf.NeedsAppxManifestFile = true;
        conf.IsUniversalWindowsPlatform = true;
        conf.PackageCertificateKeyFile = Path.Combine(Globals.RootDir, "Game_EntryPoint/Game_EntryPoint_UWP_TemporaryKey.pfx");
        conf.PackageCertificateThumbprint = "60E3DE390F85DDE86FE881ABCB08591FB0B5D556";
        conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);

        conf.Images.Add("Assets/SplashScreen.scale-200.png");
        conf.Images.Add("Assets/LockScreenLogo.scale-200.png");
        conf.Images.Add("Assets/Wide310x150Logo.scale-200.png");
        conf.Images.Add("Assets/Square44x44Logo.scale-200.png");
        conf.Images.Add("Assets/Square44x44Logo.targetsize-24_altform-unplated.png");
        conf.Images.Add("Assets/Square150x150Logo.scale-200.png");
        conf.Images.Add("Assets/StoreLogo.png");
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

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/UltralightSDK/lib/[target.SubPlatform]"));

        conf.LibraryFiles.Add("MitchEngine");
        conf.LibraryFiles.Add("Ultralight");
        conf.LibraryFiles.Add("UltralightCore");
        conf.LibraryFiles.Add("WebCore");

        conf.AddPublicDependency<Dementia>(target);
        conf.AddPublicDependency<ImGui>(target);
        conf.AddPublicDependency<Moonlight>(target);

        if(!string.IsNullOrEmpty(Globals.FMOD_Win64_Dir) || !string.IsNullOrEmpty(Globals.FMOD_UWP_Dir))
        {
            conf.Defines.Add("FMOD_ENABLED");
            conf.Defines.Add("_DISABLE_EXTENDED_ALIGNED_STORAGE");
        }
    }
    
    public override void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        base.ConfigureWin64(conf, target);

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Assimp/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/SDL/Win64/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Bullet/Win64/[target.Optimization]"));

        conf.LibraryFiles.Add("assimp-vc140-mt.lib");
        conf.LibraryFiles.Add("AppCore");

        if (!string.IsNullOrEmpty(Globals.FMOD_Win64_Dir))
        {
            conf.IncludePaths.Add(Path.Combine(Globals.FMOD_Win64_Dir, "api/core/inc"));
            conf.LibraryPaths.Add(Path.Combine(Globals.FMOD_Win64_Dir, "api/core/lib/x64"));

            conf.LibraryFiles.Add("fmodL_vc.lib");

            // FMOD DLL
            {
                var copyDirBuildStep = new Configuration.BuildStepCopy(
                    Path.Combine(Globals.FMOD_Win64_Dir, "api/core/lib/x64"),
                    Globals.RootDir + "/.build/[target.Name]");

                copyDirBuildStep.IsFileCopy = false;
                copyDirBuildStep.CopyPattern = "*.dll";
                conf.EventPostBuildExe.Add(copyDirBuildStep);
            }
        }

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
    }

    public override void ConfigureUWP(Configuration conf, CommonTarget target)
    {
        base.ConfigureUWP(conf, target);

        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Assimp/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Bullet/Win64/[target.Optimization]"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/SDL/UWP/[target.Optimization]"));

        conf.LibraryPaths.Add("$(VCInstallDir)\\lib\\store\\amd64");
        conf.LibraryPaths.Add("$(VCInstallDir)\\lib\\amd64");

        conf.LibraryFiles.Add("assimp-vc140-mt.lib");
        conf.LibraryFiles.Add("SDL2.lib");
        if (target.Optimization == Optimization.Debug)
        {
            conf.LibraryFiles.Add("BulletCollision_Debug.lib");
            conf.LibraryFiles.Add("BulletDynamics_Debug.lib");
            conf.LibraryFiles.Add("LinearMath_Debug.lib");
            conf.LibraryFiles.Add("zlibstaticd.lib");
        }
        else
        {
            conf.LibraryFiles.Add("BulletCollision_MinsizeRel.lib");
            conf.LibraryFiles.Add("BulletDynamics_MinsizeRel.lib");
            conf.LibraryFiles.Add("LinearMath_MinsizeRel.lib");
            conf.LibraryFiles.Add("zlibstatic.lib");
        }
    }

    public override void ConfigureMac(Configuration conf, CommonTarget target)
    {
        base.ConfigureMac(conf, target);

        // What the actual fuck lmao                                                                 v
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Bullet/macOS/Debug"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/SDL/macOS/Debug"));
        conf.LibraryPaths.Add(Path.Combine("[project.SharpmakeCsPath]", "ThirdParty/Lib/Assimp/macOS/[target.Optimization]"));

        conf.LibraryFiles.Add("assimp");
        conf.LibraryFiles.Add("BulletCollision");
        conf.LibraryFiles.Add("BulletDynamics");
        conf.LibraryFiles.Add("LinearMath");
        conf.LibraryFiles.Add("SDL2d");
        conf.LibraryFiles.Add("AppCore");
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
            if (target.SubPlatform == CommonTarget.SubPlatformType.UWP)
            {
                conf.AddProject<EntryPointGameProjectUWP>(target);
            }
            else
            {
                conf.AddProject<EntryPointGameProject>(target);
            }
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
    public static string FMOD_Win64_Dir = string.Empty;
    public static string FMOD_UWP_Dir = string.Empty;
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