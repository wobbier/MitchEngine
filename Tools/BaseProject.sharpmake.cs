using System.IO;
using Sharpmake;
using static CommonTarget;

public abstract class BaseProject : Project
{
    public BaseProject()
        : base(typeof(CommonTarget))
    {
        Name = "BaseProject";
        SourceRootPath = @"Source";
        IsFileNameToLower = false;
        IsTargetFileNameToLower = false;
        AddTargets(CommonTarget.GetDefaultTargets());


        // Ultralight UI
        if (Globals.IsUltralightEnabled)
        {
            if (Util.GetExecutingPlatform() == Platform.mac)
            {
                ResourceFiles.Add(Globals.RootDir + "Engine/ThirdParty/UltralightSDK/bin/macOS/AppCore.dylib");
            }
        }
    }

    public static class ConfigurePriorities
    {
        public const int All = -75;
        public const int Platform = -50;
        public const int Optimization = -25;
        /*     SHARPMAKE DEFAULT IS 0     */
        public const int Blobbing = 10;
        public const int BuildSystem = 30;
    }

    [ConfigurePriority(ConfigurePriorities.All)]
    [Configure]
    public virtual void ConfigureAll(Project.Configuration conf, CommonTarget target)
    {
        conf.ProjectPath = Path.Combine("[project.SharpmakeCsPath]", ".tmp/project");

        //conf.Options.Add(Sharpmake.Project.Configuration.LocalDebuggerWorkingDirectory.Vc.Compiler.Exceptions.Enable);
        conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
        //conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = Globals.RootDir;
        conf.VcxprojUserFile.OverwriteExistingFile = true;
        conf.ProjectFileName = @"[project.Name]_[target.Platform]";
        conf.TargetPath = "[project.SharpmakeCsPath]/.build/[target.Name]/";
        conf.LibraryPaths.Add("[project.SharpmakeCsPath]/.build/[target.Name]/");

        if (Globals.IsUltralightEnabled)
        {
            conf.Defines.Add("DEFINE_ME_ULTRALIGHT");
        }

        // RenderDoc DLL
        // TODO: Is this a chocolatey package? cross platform?
        // Shitty detection atm, but it works without this?
        if (File.Exists("C:/Program Files/RenderDoc/renderdoc.dll"))
        {
            conf.Defines.Add("DEFINE_ME_RENDERDOC");
            conf.IncludePaths.Add("C:/Program Files/RenderDoc/");
            // If we have RenderDoc installed, it better be the same version 😤
            var copyDirBuildStep = new Configuration.BuildStepCopy(
                $@"[project.SharpmakeCsPath]/ThirdParty/RenderDoc",
                Globals.RootDir + "/.build/[target.Name]");

            copyDirBuildStep.IsFileCopy = false;
            copyDirBuildStep.CopyPattern = "*.dll";
            conf.EventPostBuildExe.Add(copyDirBuildStep);
        }
        conf.Options.Add(
            new Options.Vc.Compiler.DisableSpecificWarnings(
                "4267",
                "4244"
                )
        );
        //conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
        if (target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.Defines.Add("DEFINE_ME_EDITOR");
            conf.Defines.Add("DEFINE_ME_TOOLS");
        }

        conf.Defines.Add("NOMINMAX");

        if (Directory.Exists(Globals.FMOD_Win64_Dir) || Directory.Exists(Globals.FMOD_UWP_Dir))
        {
            conf.Defines.Add("_DISABLE_EXTENDED_ALIGNED_STORAGE");
        }

        if (target.Optimization == Optimization.Debug)
        {
            conf.Defines.Add("BX_CONFIG_DEBUG=1");
        }
        else
        {
            conf.Defines.Add("BX_CONFIG_DEBUG=0");
        }


        // Optick
        if (target.Optimization == Optimization.Retail)
        {
            conf.Defines.Add("USE_OPTICK=0");
        }
        else
        {
            // TODO: Update macOS libs for Optick? Does Optick even have macOS exe's?
            if (target.SubPlatform == SubPlatformType.Win64)
            {
                conf.Defines.Add("DEFINE_ME_OPTICK");
                conf.Defines.Add("USE_OPTICK=1");

                // Optick DLL
                if (target.SubPlatform == SubPlatformType.Win64 || target.SubPlatform == SubPlatformType.UWP)
                {
                    var copyDirBuildStep = new Configuration.BuildStepCopy(
                        Path.Combine(Globals.RootDir, $"Engine/ThirdParty/Lib/Optick/Win64/{CommonTarget.GetThirdPartyOptimization(target.Optimization)}"),
                        Globals.RootDir + "/.build/[target.Name]");
                    copyDirBuildStep.IsFileCopy = false;
                    copyDirBuildStep.CopyPattern = "OptickCore.dll";
                    conf.EventPostBuildExe.Add(copyDirBuildStep);
                }
            }
        }

        conf.Options.Add(Options.Vc.General.WarningLevel.Level3);
        conf.Options.Add(Options.Vc.General.TreatWarningsAsErrors.Enable);
        conf.Options.Add(Options.Vc.Compiler.ConformanceMode.Enable);
    }

    #region Platfoms

    [ConfigurePriority(ConfigurePriorities.Platform)]
    [Configure(SubPlatformType.Win64)]
    public virtual void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
        conf.Options.Add(Options.Vc.Compiler.RTTI.Enable);
        conf.Options.Add(Options.Vc.General.CharacterSet.Unicode);
        conf.Options.Add(Options.Vc.Compiler.Exceptions.Enable);

        // Hot-Reloading
        {
            conf.Options.Add(Options.Vc.General.DebugInformation.ProgramDatabaseEnC);
            conf.Options.Add(Options.Vc.Compiler.FunctionLevelLinking.Enable);
            conf.Options.Add(Options.Vc.Linker.Incremental.Enable);
        }

        conf.Defines.Add("DEFINE_ME_PLATFORM_WIN64");

        conf.Options.Add(
            new Options.Vc.Compiler.DisableSpecificWarnings(
                "4201",
                "4100"
                )
        );

        if (Directory.Exists(Globals.FMOD_Win64_Dir))
        {
            conf.Defines.Add("DEFINE_ME_FMOD");
        }
        if (Directory.Exists(Globals.MONO_Win64_Dir))
        {
            conf.Defines.Add("DEFINE_ME_MONO");
        }
    }

    [ConfigurePriority(ConfigurePriorities.Platform)]
    [Configure(SubPlatformType.UWP)]
    public virtual void ConfigureUWP(Configuration conf, CommonTarget target)
    {
        conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
        conf.Options.Add(Options.Vc.Compiler.RTTI.Enable);
        conf.Options.Add(Options.Vc.General.CharacterSet.Unicode);
        conf.Options.Add(Options.Vc.Compiler.Exceptions.Enable);

        conf.Defines.Add("DEFINE_ME_PLATFORM_UWP");

        conf.LibraryPaths.Add("C:\\Program Files (x86)\\Windows Kits\\10\\UnionMetadata");

        conf.Options.Add(
            new Options.Vc.Compiler.DisableSpecificWarnings(
                "4201",
                "4100"
                )
        );

        if (Directory.Exists(Globals.FMOD_UWP_Dir))
        {
            conf.Defines.Add("DEFINE_ME_FMOD");
        }
        if (Directory.Exists(Globals.MONO_Win64_Dir))
        {
            conf.Defines.Add("DEFINE_ME_MONO");
        }
    }

    [ConfigurePriority(ConfigurePriorities.Platform)]
    [Configure(SubPlatformType.macOS)]
    public virtual void ConfigureMac(Configuration conf, CommonTarget target)
    {
        conf.Options.Add(Options.XCode.Compiler.OnlyActiveArch.Enable);
        conf.Options.Add(Options.XCode.Compiler.CppLanguageStandard.GNU17);
        conf.Options.Add(Options.XCode.Compiler.RTTI.Enable);
        conf.Options.Add(Options.XCode.Compiler.Exceptions.Enable);
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("Cocoa"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("Metal"));
        conf.Options.Add(new Options.XCode.Compiler.UserFrameworks("libiconv.tbd"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("IOKit"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("ForceFeedback"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("Carbon"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("CoreVideo"));
        //conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("Foundation"));
        conf.Options.Add(new Options.XCode.Compiler.SystemFrameworks("Quartz"));
        conf.Options.Add(new Options.XCode.Compiler.OnlyActiveArch());

        conf.Defines.Add("DEFINE_ME_PLATFORM_MACOS");

        if (Directory.Exists(Globals.MONO_macOS_Dir))
        {
            conf.Defines.Add("DEFINE_ME_MONO");
        }
    }

    #endregion

    #region Optimizations

    [ConfigurePriority(ConfigurePriorities.Optimization)]
    [Configure(Optimization.Debug)]
    public virtual void ConfigureDebug(Configuration conf, CommonTarget target)
    {
        conf.DefaultOption = Options.DefaultTarget.Debug;

        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDebugDLL);
        conf.Options.Add(Options.Vc.Compiler.Inline.Disable);
        conf.Defines.Add("DEFINE_ME_DEBUG");
        conf.Defines.Add("DEFINE_ME_TOOLS");
    }

    [ConfigurePriority(ConfigurePriorities.Optimization)]
    [Configure(Optimization.Release)]
    public virtual void ConfigureRelease(Configuration conf, CommonTarget target)
    {
        conf.DefaultOption = Options.DefaultTarget.Release;
        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDLL);
        conf.Options.Add(Options.Vc.General.DebugInformation.ProgramDatabase);
        conf.Options.Add(Options.Vc.Compiler.Inline.OnlyInline);
        conf.Defines.Add("DEFINE_ME_RELEASE");
        conf.Defines.Add("DEFINE_ME_TOOLS");

        if (conf.Platform == Platform.win64 )
        {
            // Should be covered by Options.Vc.General.DebugInformation.ProgramDatabase
            //conf.AdditionalCompilerOptions.Add("/Zi");
        }
        else if (conf.Platform == Platform.mac)
        {
            conf.AdditionalCompilerOptions.Add("-g");
        }
    }


    [ConfigurePriority(ConfigurePriorities.Optimization)]
    [Configure(Optimization.Retail)]
    public virtual void ConfigureRetail(Configuration conf, CommonTarget target)
    {
        conf.DefaultOption = Options.DefaultTarget.Release;

        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDLL);

        // Full inlining
        conf.Options.Add(Options.Vc.Compiler.Inline.AnySuitable);
        conf.Options.Add(Options.Vc.Compiler.Optimization.MaximizeSpeed);

        conf.Options.Add(Options.Vc.Compiler.Exceptions.EnableWithSEH);
        conf.Options.Add(Options.Vc.Compiler.EnhancedInstructionSet.AdvancedVectorExtensions2);

        // Undo Hot-Reloading
        {
            conf.Options.Add(Options.Vc.General.DebugInformation.Disable);
            conf.Options.Add(Options.Vc.Compiler.FunctionLevelLinking.Disable);
            conf.Options.Add(Options.Vc.Linker.Incremental.Disable);
        }

        conf.Defines.Add("DEFINE_ME_RETAIL");
    }

    #endregion
}