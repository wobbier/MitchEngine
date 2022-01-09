using System.IO;
using Sharpmake;

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

        if (target.SelectedMode == CommonTarget.Mode.Editor)
        {
            conf.Defines.Add("ME_EDITOR");
            conf.Defines.Add("ME_TOOLS");
        }

        conf.Defines.Add("NOMINMAX");

        if (target.Optimization == Optimization.Debug)
        {
        }
        else
        {

        }
    }

    #region Platfoms

    [ConfigurePriority(ConfigurePriorities.Platform)]
    [Configure(Platform.win64)]
    public virtual void ConfigureWin64(Configuration conf, CommonTarget target)
    {
        conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
        conf.Options.Add(Options.Vc.Compiler.RTTI.Enable);
        conf.Options.Add(Options.Vc.General.CharacterSet.Unicode);
        conf.Options.Add(Options.Vc.Compiler.Exceptions.Enable);

        conf.Defines.Add("ME_PLATFORM_WIN64");

        conf.Options.Add(
            new Options.Vc.Compiler.DisableSpecificWarnings(
                "4201",
                "4100"
                )
        );
    }

    [ConfigurePriority(ConfigurePriorities.Platform)]
    [Configure(Platform.mac)]
    public virtual void ConfigureMac(Configuration conf, CommonTarget target)
    {
        conf.Options.Add(Options.XCode.Compiler.OnlyActiveArch.Enable);
        conf.Options.Add(Options.XCode.Compiler.CppLanguageStandard.GNU17);
        conf.Options.Add(Options.XCode.Compiler.RTTI.Enable);
        conf.Options.Add(Options.XCode.Compiler.Exceptions.Enable);

        conf.Defines.Add("ME_PLATFORM_MACOS");
    }

    #endregion

    #region Optimizations

    [ConfigurePriority(ConfigurePriorities.Optimization)]
    [Configure(Optimization.Debug)]
    public virtual void ConfigureDebug(Configuration conf, CommonTarget target)
    {
        conf.DefaultOption = Options.DefaultTarget.Debug;

        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDebugDLL);
    }

    [ConfigurePriority(ConfigurePriorities.Optimization)]
    [Configure(Optimization.Release)]
    public virtual void ConfigureRelease(Configuration conf, CommonTarget target)
    {
        conf.DefaultOption = Options.DefaultTarget.Release;
        conf.Options.Add(Sharpmake.Options.Vc.Compiler.RuntimeLibrary.MultiThreadedDLL);
    }

    #endregion
}
